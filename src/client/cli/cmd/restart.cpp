/*
 * Copyright (C) 2017-2021 Canonical, Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "restart.h"
#include "common_cli.h"

#include "animated_spinner.h"

#include <multipass/cli/argparser.h>
#include <multipass/constants.h>
#include <multipass/exceptions/cmd_exceptions.h>
#include <multipass/settings.h>
#include <multipass/timer.h>

#include <chrono>
#include <csignal>

namespace mp = multipass;
namespace cmd = multipass::cmd;
using RpcMethod = mp::Rpc::Stub;

mp::ReturnCode cmd::Restart::run(mp::ArgParser* parser)
{
    auto ret = parse_args(parser);
    if (ret != ParseCode::Ok)
        return parser->returnCodeFrom(ret);

    auto on_success = [](mp::RestartReply& reply) { return ReturnCode::Ok; };

    AnimatedSpinner spinner{cout};
    auto on_failure = [this, &spinner](grpc::Status& status) {
        spinner.stop();
        return standard_failure_handler_for(name(), cerr, status);
    };

    spinner.start(instance_action_message_for(request.instance_names(), "Restarting "));
    request.set_verbosity_level(parser->verbosityLevel());

    std::unique_ptr<multipass::utils::Timer> timer;

    if (parser->isSet("timeout"))
    {
        timer = std::make_unique<multipass::utils::Timer>(
            std::chrono::seconds(parser->value("timeout").toInt()), [&spinner]() {
                spinner.stop();
                std::cerr << "Timed out waiting for instance to restart." << std::endl;
                std::raise(SIGINT);
            });
        timer->start();
    }

    return dispatch(&RpcMethod::restart, request, on_success, on_failure);
}

std::string cmd::Restart::name() const
{
    return "restart";
}

QString cmd::Restart::short_help() const
{
    return QStringLiteral("Restart instances");
}

QString cmd::Restart::description() const
{
    return QStringLiteral("Restart the named instances. Exits with return\n"
                          "code 0 when the instances restart, or with an\n"
                          "error code if any fail to restart.");
}

mp::ParseCode cmd::Restart::parse_args(mp::ArgParser* parser)
{
    const auto petenv_name = MP_SETTINGS.get(petenv_key);
    parser->addPositionalArgument(
        "name",
        QString{"Names of instances to restart. If omitted, and without the --all option, '%1' will be assumed."}.arg(
            petenv_name),
        "[<name> ...]");

    QCommandLineOption all_option(all_option_name, "Restart all instances");
    parser->addOption(all_option);

    mp::cmd::add_timeout(parser);

    auto status = parser->commandParse(this);

    if (status != ParseCode::Ok)
        return status;

    try
    {
        request.set_timeout(mp::cmd::parse_timeout(parser));
    }
    catch (const mp::ValidationException& e)
    {
        cerr << "error: " << e.what() << std::endl;
        return ParseCode::CommandLineError;
    }

    auto parse_code = check_for_name_and_all_option_conflict(parser, cerr, /*allow_empty=*/true);
    if (parse_code != ParseCode::Ok)
        return parse_code;

    request.mutable_instance_names()->CopyFrom(add_instance_names(parser, /*default_name=*/petenv_name.toStdString()));

    return status;
}
