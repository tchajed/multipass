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

#include "shell.h"
#include "common_cli.h"

#include "animated_spinner.h"
#include <multipass/cli/argparser.h>
#include <multipass/constants.h>
#include <multipass/exceptions/cmd_exceptions.h>
#include <multipass/settings.h>
#include <multipass/ssh/ssh_client.h>

namespace mp = multipass;
namespace cmd = multipass::cmd;
using RpcMethod = mp::Rpc::Stub;

mp::ReturnCode cmd::Shell::run(mp::ArgParser* parser)
{
    petenv_name = MP_SETTINGS.get(petenv_key);
    auto ret = parse_args(parser);
    if (ret != ParseCode::Ok)
    {
        return parser->returnCodeFrom(ret);
    }

    // We can assume the first array entry since `shell` only uses one instance
    // at a time
    auto instance_name = request.instance_name()[0];

    auto on_success = [this](mp::SSHInfoReply& reply) {
        // TODO: mainly for testing - need a better way to test parsing
        if (reply.ssh_info().empty())
            return ReturnCode::Ok;

        // TODO: this should setup a reader that continously prints out
        // streaming replies from the server corresponding to stdout/stderr streams
        const auto& ssh_info = reply.ssh_info().begin()->second;
        const auto& host = ssh_info.host();
        const auto& port = ssh_info.port();
        const auto& username = ssh_info.username();
        const auto& priv_key_blob = ssh_info.priv_key_base64();

        try
        {
            auto console_creator = [this](auto channel) { return Console::make_console(channel, term); };
            mp::SSHClient ssh_client{host, port, username, priv_key_blob, console_creator};
            ssh_client.connect();
        }
        catch (const std::exception& e)
        {
            cerr << "shell failed: " << e.what() << "\n";
            return ReturnCode::CommandFail;
        }
        return ReturnCode::Ok;
    };

    auto on_failure = [this, &instance_name, parser](grpc::Status& status) {
        QStringList retry_args{};

        if (status.error_code() == grpc::StatusCode::NOT_FOUND && instance_name == petenv_name.toStdString())
            retry_args.append({"multipass", "launch", "--name", petenv_name});
        else if (status.error_code() == grpc::StatusCode::ABORTED)
            retry_args.append({"multipass", "start", QString::fromStdString(instance_name)});
        else
            return standard_failure_handler_for(name(), cerr, status);

        if (parser->isSet("timeout"))
            retry_args.append({"--timeout", parser->value("timeout")});

        return run_cmd_and_retry(retry_args, parser, cout, cerr);
    };

    request.set_verbosity_level(parser->verbosityLevel());
    ReturnCode return_code;
    while ((return_code = dispatch(&RpcMethod::ssh_info, request, on_success, on_failure)) == ReturnCode::Retry)
        ;

    return return_code;
}

std::string cmd::Shell::name() const { return "shell"; }

std::vector<std::string> cmd::Shell::aliases() const
{
    return {name(), "sh", "connect"};
}

QString cmd::Shell::short_help() const
{
    return QStringLiteral("Open a shell on a running instance");
}

QString cmd::Shell::description() const
{
    return QStringLiteral("Open a shell prompt on the instance.");
}

mp::ParseCode cmd::Shell::parse_args(mp::ArgParser* parser)
{
    parser->addPositionalArgument(
        "name",
        QString{
            "Name of the instance to open a shell on. If omitted, '%1' (the configured primary instance name) will be "
            "assumed. If the instance is not running, an attempt is made to start it (see `start` for more info)."}
            .arg(petenv_name),
        "[<name>]");

    mp::cmd::add_timeout(parser);

    auto status = parser->commandParse(this);

    if (status != ParseCode::Ok)
    {
        return status;
    }

    try
    {
        mp::cmd::parse_timeout(parser);
    }
    catch (const mp::ValidationException& e)
    {
        cerr << "error: " << e.what() << std::endl;
        return ParseCode::CommandLineError;
    }

    const auto pos_args = parser->positionalArguments();
    const auto num_args = pos_args.count();
    if (num_args > 1)
    {
        cerr << "Too many arguments given\n";
        status = ParseCode::CommandLineError;
    }
    else
    {
        auto entry = request.add_instance_name();
        entry->append(num_args ? pos_args.first().toStdString() : petenv_name.toStdString());
    }

    return status;
}
