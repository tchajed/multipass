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

#include <multipass/constants.h>
#include <multipass/logging/log.h>
#include <multipass/name_generator.h>
#include <multipass/version.h>
#include <multipass/virtual_machine_factory.h>
#include <multipass/vm_image_host.h>

#include "daemon_test_fixture.h"
#include "dummy_ssh_key_provider.h"
#include "extra_assertions.h"
#include "file_operations.h"
#include "mock_daemon.h"
#include "mock_environment_helpers.h"
#include "mock_logger.h"
#include "mock_process_factory.h"
#include "mock_utils.h"
#include "mock_virtual_machine.h"
#include "mock_vm_image_vault.h"
#include "mock_vm_workflow_provider.h"

#include <yaml-cpp/yaml.h>

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkProxyFactory>
#include <QStorageInfo>
#include <QString>
#include <QSysInfo>

#include <scope_guard.hpp>

#include <memory>
#include <ostream>
#include <stdexcept>
#include <string>

namespace mp = multipass;
namespace mpl = multipass::logging;
namespace mpt = multipass::test;
using namespace testing;
using namespace multipass::utils;

namespace YAML
{
void PrintTo(const YAML::Node& node, ::std::ostream* os)
{
    YAML::Emitter emitter;
    emitter.SetIndent(4);
    emitter << node;
    *os << "\n" << emitter.c_str();
}
} // namespace YAML

namespace
{
const qint64 default_total_bytes{16'106'127'360}; // 15G

template<typename R>
  bool is_ready(std::future<R> const& f)
  { return f.wait_for(std::chrono::seconds(0)) == std::future_status::ready; }

struct StubNameGenerator : public mp::NameGenerator
{
    explicit StubNameGenerator(std::string name) : name{std::move(name)}
    {
    }
    std::string make_name() override
    {
        return name;
    }
    std::string name;
};
} // namespace

struct Daemon : public mpt::DaemonTestFixture
{
    Daemon()
    {
        ON_CALL(*mock_utils, filesystem_bytes_available(_)).WillByDefault([this](const QString& data_directory) {
            return mock_utils->Utils::filesystem_bytes_available(data_directory);
        });
    }

    mpt::MockUtils::GuardedMock attr{mpt::MockUtils::inject()};
    NiceMock<mpt::MockUtils>* mock_utils = attr.first;
};

TEST_F(Daemon, receives_commands)
{
    mpt::MockDaemon daemon{config_builder.build()};

    EXPECT_CALL(daemon, create(_, _, _))
        .WillOnce(Invoke(&daemon, &mpt::MockDaemon::set_promise_value<mp::CreateRequest, mp::CreateReply>));
    EXPECT_CALL(daemon, launch(_, _, _))
        .WillOnce(Invoke(&daemon, &mpt::MockDaemon::set_promise_value<mp::LaunchRequest, mp::LaunchReply>));
    EXPECT_CALL(daemon, purge(_, _, _))
        .WillOnce(Invoke(&daemon, &mpt::MockDaemon::set_promise_value<mp::PurgeRequest, mp::PurgeReply>));
    EXPECT_CALL(daemon, find(_, _, _))
        .WillOnce(Invoke(&daemon, &mpt::MockDaemon::set_promise_value<mp::FindRequest, mp::FindReply>));
    EXPECT_CALL(daemon, ssh_info(_, _, _))
        .WillOnce(Invoke(&daemon, &mpt::MockDaemon::set_promise_value<mp::SSHInfoRequest, mp::SSHInfoReply>));
    EXPECT_CALL(daemon, info(_, _, _))
        .WillOnce(Invoke(&daemon, &mpt::MockDaemon::set_promise_value<mp::InfoRequest, mp::InfoReply>));
    EXPECT_CALL(daemon, list(_, _, _))
        .WillOnce(Invoke(&daemon, &mpt::MockDaemon::set_promise_value<mp::ListRequest, mp::ListReply>));
    EXPECT_CALL(daemon, recover(_, _, _))
        .WillOnce(Invoke(&daemon, &mpt::MockDaemon::set_promise_value<mp::RecoverRequest, mp::RecoverReply>));
    EXPECT_CALL(daemon, start(_, _, _))
        .WillOnce(Invoke(&daemon, &mpt::MockDaemon::set_promise_value<mp::StartRequest, mp::StartReply>));
    EXPECT_CALL(daemon, stop(_, _, _))
        .WillOnce(Invoke(&daemon, &mpt::MockDaemon::set_promise_value<mp::StopRequest, mp::StopReply>));
    EXPECT_CALL(daemon, suspend(_, _, _))
        .WillOnce(Invoke(&daemon, &mpt::MockDaemon::set_promise_value<mp::SuspendRequest, mp::SuspendReply>));
    EXPECT_CALL(daemon, restart(_, _, _))
        .WillOnce(Invoke(&daemon, &mpt::MockDaemon::set_promise_value<mp::RestartRequest, mp::RestartReply>));
    EXPECT_CALL(daemon, delet(_, _, _))
        .WillOnce(Invoke(&daemon, &mpt::MockDaemon::set_promise_value<mp::DeleteRequest, mp::DeleteReply>));
    EXPECT_CALL(daemon, version(_, _, _))
        .WillOnce(Invoke(&daemon, &mpt::MockDaemon::set_promise_value<mp::VersionRequest, mp::VersionReply>));
    EXPECT_CALL(daemon, mount(_, _, _))
        .WillOnce(Invoke(&daemon, &mpt::MockDaemon::set_promise_value<mp::MountRequest, mp::MountReply>));
    EXPECT_CALL(daemon, umount(_, _, _))
        .WillOnce(Invoke(&daemon, &mpt::MockDaemon::set_promise_value<mp::UmountRequest, mp::UmountReply>));

    send_commands({{"test_create", "foo"},
                   {"launch", "foo"},
                   {"delete", "foo"},
                   {"exec", "foo", "--", "cmd"},
                   {"info", "foo"},
                   {"list"},
                   {"purge"},
                   {"recover", "foo"},
                   {"start", "foo"},
                   {"stop", "foo"},
                   {"suspend", "foo"},
                   {"restart", "foo"},
                   {"version"},
                   {"find", "something"},
                   {"mount", ".", "target"},
                   {"umount", "instance"}});
}

TEST_F(Daemon, provides_version)
{
    mp::Daemon daemon{config_builder.build()};

    std::stringstream stream;
    send_command({"version"}, stream);

    EXPECT_THAT(stream.str(), HasSubstr(mp::version_string));
}

TEST_F(Daemon, failed_restart_command_returns_fulfilled_promise)
{
    mp::Daemon daemon{config_builder.build()};

    auto nonexistant_instance = new mp::InstanceNames; // on heap as *Request takes ownership
    nonexistant_instance->add_instance_name("nonexistant");
    mp::RestartRequest request;
    request.set_allocated_instance_names(nonexistant_instance);
    std::promise<grpc::Status> status_promise;

    daemon.restart(&request, nullptr, &status_promise);
    EXPECT_TRUE(is_ready(status_promise.get_future()));
}

TEST_F(Daemon, proxy_contains_valid_info)
{
    auto guard = sg::make_scope_guard([]() noexcept {          // std::terminate ok if this throws
        QNetworkProxyFactory::setUseSystemConfiguration(true); // Resets proxy back to what the system is configured for
    });

    QString username{"username"};
    QString password{"password"};
    QString hostname{"192.168.1.1"};
    qint16 port{3128};
    QString proxy = QString("%1:%2@%3:%4").arg(username).arg(password).arg(hostname).arg(port);

    mpt::SetEnvScope env("http_proxy", proxy.toUtf8());

    auto config = config_builder.build();

    EXPECT_THAT(config->network_proxy->user(), username);
    EXPECT_THAT(config->network_proxy->password(), password);
    EXPECT_THAT(config->network_proxy->hostName(), hostname);
    EXPECT_THAT(config->network_proxy->port(), port);
}

TEST_F(Daemon, data_path_valid)
{
    QTemporaryDir data_dir, cache_dir;

    EXPECT_CALL(mpt::MockStandardPaths::mock_instance(), writableLocation(mp::StandardPaths::CacheLocation))
        .WillOnce(Return(cache_dir.path()));

    EXPECT_CALL(mpt::MockStandardPaths::mock_instance(), writableLocation(mp::StandardPaths::AppDataLocation))
        .WillOnce(Return(data_dir.path()));

    config_builder.data_directory = "";
    config_builder.cache_directory = "";
    auto config = config_builder.build();

    EXPECT_EQ(config->data_directory.toStdString(), data_dir.path().toStdString());
    EXPECT_EQ(config->cache_directory.toStdString(), cache_dir.path().toStdString());
}

TEST_F(Daemon, data_path_with_storage_valid)
{
    QTemporaryDir storage_dir;

    mpt::SetEnvScope storage("MULTIPASS_STORAGE", storage_dir.path().toUtf8());
    EXPECT_CALL(mpt::MockStandardPaths::mock_instance(), writableLocation(_)).Times(0);

    config_builder.data_directory = "";
    config_builder.cache_directory = "";
    auto config = config_builder.build();

    EXPECT_EQ(config->data_directory.toStdString(), storage_dir.filePath("data").toStdString());
    EXPECT_EQ(config->cache_directory.toStdString(), storage_dir.filePath("cache").toStdString());
}

namespace
{
struct DaemonCreateLaunchTestSuite : public Daemon, public WithParamInterface<std::string>
{
};

struct MinSpaceRespectedSuite : public Daemon,
                                public WithParamInterface<std::tuple<std::string, std::string, std::string>>
{
};

struct MinSpaceViolatedSuite : public Daemon,
                               public WithParamInterface<std::tuple<std::string, std::string, std::string>>
{
};

struct LaunchImgSizeSuite : public Daemon,
                            public WithParamInterface<std::tuple<std::string, std::vector<std::string>, std::string>>
{
};

struct LaunchStorageCheckSuite : public Daemon, public WithParamInterface<std::string>
{
};

struct LaunchWithBridges
    : public Daemon,
      public WithParamInterface<
          std::pair<std::vector<std::tuple<std::string, std::string, std::string>>, std::vector<std::string>>>
{
};

struct LaunchWithNoNetworkCloudInit : public Daemon, public WithParamInterface<std::vector<std::string>>
{
};

struct RefuseBridging : public Daemon, public WithParamInterface<std::tuple<std::string, std::string>>
{
};

struct ListIP : public Daemon,
                public WithParamInterface<
                    std::tuple<mp::VirtualMachine::State, std::vector<std::string>, std::vector<std::string>>>
{
};

TEST_P(DaemonCreateLaunchTestSuite, creates_virtual_machines)
{
    auto mock_factory = use_a_mock_vm_factory();
    mp::Daemon daemon{config_builder.build()};

    EXPECT_CALL(*mock_factory, create_virtual_machine(_, _));
    send_command({GetParam()});
}

TEST_P(DaemonCreateLaunchTestSuite, on_creation_hooks_up_platform_prepare_source_image)
{
    auto mock_factory = use_a_mock_vm_factory();
    mp::Daemon daemon{config_builder.build()};

    EXPECT_CALL(*mock_factory, prepare_source_image(_));
    send_command({GetParam()});
}

TEST_P(DaemonCreateLaunchTestSuite, on_creation_hooks_up_platform_prepare_instance_image)
{
    auto mock_factory = use_a_mock_vm_factory();
    mp::Daemon daemon{config_builder.build()};

    EXPECT_CALL(*mock_factory, prepare_instance_image(_, _));
    send_command({GetParam()});
}

TEST_P(DaemonCreateLaunchTestSuite, on_creation_handles_instance_image_preparation_failure)
{
    auto mock_factory = use_a_mock_vm_factory();
    mp::Daemon daemon{config_builder.build()};

    std::string cause = "motive";
    EXPECT_CALL(*mock_factory, prepare_instance_image(_, _)).WillOnce(Throw(std::runtime_error{cause}));
    EXPECT_CALL(*mock_factory, remove_resources_for(_));

    std::stringstream err_stream;
    send_command({GetParam()}, trash_stream, err_stream);

    EXPECT_THAT(err_stream.str(), AllOf(HasSubstr("failed"), HasSubstr(cause)));
}

TEST_P(DaemonCreateLaunchTestSuite, generates_name_on_creation_when_client_does_not_provide_one)
{
    const std::string expected_name{"pied-piper-valley"};

    config_builder.name_generator = std::make_unique<StubNameGenerator>(expected_name);
    mp::Daemon daemon{config_builder.build()};

    std::stringstream stream;
    send_command({GetParam()}, stream);

    EXPECT_THAT(stream.str(), HasSubstr(expected_name));
}

MATCHER_P2(YAMLNodeContainsString, key, val, "")
{
    if (!arg.IsMap())
    {
        return false;
    }
    if (!arg[key])
    {
        return false;
    }
    if (!arg[key].IsScalar())
    {
        return false;
    }
    return arg[key].Scalar() == val;
}

MATCHER_P2(YAMLNodeContainsStringStartingWith, key, val, "")
{
    if (!arg.IsMap())
    {
        return false;
    }
    if (!arg[key])
    {
        return false;
    }
    if (!arg[key].IsScalar())
    {
        return false;
    }
    return arg[key].Scalar().find(val) == 0;
}

MATCHER_P(YAMLNodeContainsSubString, val, "")
{
    if (!arg.IsSequence())
    {
        return false;
    }

    return arg[0].Scalar().find(val) != std::string::npos;
}

MATCHER_P2(YAMLNodeContainsStringArray, key, values, "")
{
    if (!arg.IsMap())
    {
        return false;
    }
    if (!arg[key])
    {
        return false;
    }
    if (!arg[key].IsSequence())
    {
        return false;
    }
    if (arg[key].size() != values.size())
    {
        return false;
    }
    for (auto i = 0u; i < values.size(); ++i)
    {
        if (arg[key][i].template as<std::string>() != values[i])
        {
            return false;
        }
    }
    return true;
}

MATCHER_P(YAMLNodeContainsMap, key, "")
{
    if (!arg.IsMap())
    {
        return false;
    }
    if (!arg[key])
    {
        return false;
    }
    return arg[key].IsMap();
}

MATCHER_P(YAMLNodeContainsSequence, key, "")
{
    if (!arg.IsMap())
    {
        return false;
    }
    if (!arg[key])
    {
        return false;
    }
    return arg[key].IsSequence();
}

MATCHER_P(YAMLSequenceContainsStringMap, values, "")
{
    if (!arg.IsSequence())
    {
        return false;
    }
    for (const auto& node : arg)
    {
        if (node.size() != values.size())
            continue;
        for (auto it = values.cbegin();; ++it)
        {
            if (it == values.cend())
                return true;
            else if (node[it->first].template as<std::string>() != it->second)
                break;
        }
    }
    return false;
}

TEST_P(DaemonCreateLaunchTestSuite, default_cloud_init_grows_root_fs)
{
    auto mock_factory = use_a_mock_vm_factory();
    mp::Daemon daemon{config_builder.build()};

    EXPECT_CALL(*mock_factory, prepare_instance_image(_, _))
        .WillOnce(Invoke([](const multipass::VMImage&, const mp::VirtualMachineDescription& desc) {
            EXPECT_THAT(desc.vendor_data_config, YAMLNodeContainsMap("growpart"));

            if (desc.vendor_data_config["growpart"])
            {
                auto const& growpart_stanza = desc.vendor_data_config["growpart"];

                EXPECT_THAT(growpart_stanza, YAMLNodeContainsString("mode", "auto"));
                EXPECT_THAT(growpart_stanza, YAMLNodeContainsStringArray("devices", std::vector<std::string>({"/"})));
                EXPECT_THAT(growpart_stanza, YAMLNodeContainsString("ignore_growroot_disabled", "false"));
            }
        }));

    send_command({GetParam()});
}

TEST_P(DaemonCreateLaunchTestSuite, adds_ssh_keys_to_cloud_init_config)
{
    auto mock_factory = use_a_mock_vm_factory();
    std::string expected_key{"thisitnotansshkeyactually"};
    config_builder.ssh_key_provider = std::make_unique<mpt::DummyKeyProvider>(expected_key);
    mp::Daemon daemon{config_builder.build()};

    EXPECT_CALL(*mock_factory, prepare_instance_image(_, _))
        .WillOnce(Invoke([&expected_key](const multipass::VMImage&, const mp::VirtualMachineDescription& desc) {
            ASSERT_THAT(desc.vendor_data_config, YAMLNodeContainsSequence("ssh_authorized_keys"));
            auto const& ssh_keys_stanza = desc.vendor_data_config["ssh_authorized_keys"];
            EXPECT_THAT(ssh_keys_stanza, YAMLNodeContainsSubString(expected_key));
        }));

    send_command({GetParam()});
}

TEST_P(DaemonCreateLaunchTestSuite, adds_pollinate_user_agent_to_cloud_init_config)
{
    auto mock_factory = use_a_mock_vm_factory();
    std::vector<std::pair<std::string, std::string>> const& expected_pollinate_map{
        {"path", "/etc/pollinate/add-user-agent"},
        {"content", fmt::format("multipass/version/{} # written by Multipass\n"
                                "multipass/driver/mock-1234 # written by Multipass\n"
                                "multipass/host/{}-{} # written by Multipass\n",
                                multipass::version_string, QSysInfo::productType(), QSysInfo::productVersion())},
    };
    mp::Daemon daemon{config_builder.build()};

    EXPECT_CALL(*mock_factory, prepare_instance_image(_, _))
        .WillOnce(
            Invoke([&expected_pollinate_map](const multipass::VMImage&, const mp::VirtualMachineDescription& desc) {
                EXPECT_THAT(desc.vendor_data_config, YAMLNodeContainsSequence("write_files"));

                if (desc.vendor_data_config["write_files"])
                {
                    auto const& write_stanza = desc.vendor_data_config["write_files"];

                    EXPECT_THAT(write_stanza, YAMLSequenceContainsStringMap(expected_pollinate_map));
                }
            }));

    send_command({GetParam()});
}

TEST_P(DaemonCreateLaunchTestSuite, workflow_found_passes_expected_data)
{
    auto mock_factory = use_a_mock_vm_factory();
    auto mock_image_vault = std::make_unique<NiceMock<mpt::MockVMImageVault>>();
    auto mock_workflow_provider = std::make_unique<NiceMock<mpt::MockVMWorkflowProvider>>();

    const int num_cores = 4;
    const mp::MemorySize mem_size{"4G"};
    const mp::MemorySize disk_space{"25G"};
    const std::string release{"focal"};
    const std::string remote{"release"};

    EXPECT_CALL(*mock_factory, create_virtual_machine(_, _))
        .WillOnce([&num_cores, &mem_size, &disk_space](const mp::VirtualMachineDescription& vm_desc, auto&) {
            EXPECT_EQ(vm_desc.num_cores, num_cores);
            EXPECT_EQ(vm_desc.mem_size, mem_size);
            EXPECT_EQ(vm_desc.disk_space, disk_space);

            return std::make_unique<mpt::StubVirtualMachine>();
        });

    EXPECT_CALL(*mock_image_vault, fetch_image(_, _, _, _))
        .WillOnce(Invoke([&release, &remote](const mp::FetchType& fetch_type, const mp::Query& query,
                                             const mp::VMImageVault::PrepareAction& prepare,
                                             const mp::ProgressMonitor& monitor) {
            EXPECT_EQ(query.release, release);
            EXPECT_EQ(query.remote_name, remote);

            return mpt::StubVMImageVault().fetch_image(fetch_type, query, prepare, monitor);
        }));

    EXPECT_CALL(*mock_workflow_provider, fetch_workflow_for(_, _))
        .WillOnce(Invoke([&mem_size, &disk_space, &release,
                          &remote](const auto&, mp::VirtualMachineDescription& vm_desc) -> mp::Query {
            vm_desc.num_cores = num_cores;
            vm_desc.mem_size = mem_size;
            vm_desc.disk_space = disk_space;

            return {"", release, false, remote, mp::Query::Type::Alias};
        }));

    config_builder.workflow_provider = std::move(mock_workflow_provider);
    config_builder.vault = std::move(mock_image_vault);
    mp::Daemon daemon{config_builder.build()};

    send_command({GetParam()});
}

TEST_P(DaemonCreateLaunchTestSuite, workflow_not_found_passes_expected_data)
{
    auto mock_factory = use_a_mock_vm_factory();
    auto mock_image_vault = std::make_unique<NiceMock<mpt::MockVMImageVault>>();

    EXPECT_CALL(*mock_factory, create_virtual_machine(_, _))
        .WillOnce([](const mp::VirtualMachineDescription& vm_desc, auto&) {
            EXPECT_EQ(vm_desc.num_cores, 1);
            EXPECT_EQ(vm_desc.mem_size, mp::MemorySize("1G"));
            EXPECT_EQ(vm_desc.disk_space, mp::MemorySize("5G"));

            return std::make_unique<mpt::StubVirtualMachine>();
        });

    EXPECT_CALL(*mock_image_vault, fetch_image(_, _, _, _))
        .WillOnce(Invoke([](const mp::FetchType& fetch_type, const mp::Query& query,
                            const mp::VMImageVault::PrepareAction& prepare, const mp::ProgressMonitor& monitor) {
            EXPECT_EQ(query.release, "default");
            EXPECT_TRUE(query.remote_name.empty());

            return mpt::StubVMImageVault().fetch_image(fetch_type, query, prepare, monitor);
        }));

    config_builder.vault = std::move(mock_image_vault);
    mp::Daemon daemon{config_builder.build()};

    send_command({GetParam()});
}

TEST_P(LaunchWithNoNetworkCloudInit, no_network_cloud_init)
{
    mpt::MockVirtualMachineFactory* mock_factory = use_a_mock_vm_factory();
    mp::Daemon daemon{config_builder.build()};

    const auto launch_args = GetParam();

    EXPECT_CALL(*mock_factory, prepare_instance_image(_, _))
        .WillOnce(Invoke([](const multipass::VMImage&, const mp::VirtualMachineDescription& desc) {
            EXPECT_TRUE(desc.network_data_config.IsNull());
        }));

    send_command(launch_args);
}

std::vector<std::string> make_args(const std::vector<std::string>& args)
{
    std::vector<std::string> all_args{"launch"};

    for (const auto& a : args)
        all_args.push_back(a);

    return all_args;
}

INSTANTIATE_TEST_SUITE_P(
    Daemon, LaunchWithNoNetworkCloudInit,
    Values(make_args({}), make_args({"xenial"}), make_args({"xenial", "--network", "name=eth0,mode=manual"}),
           make_args({"groovy"}), make_args({"groovy", "--network", "name=eth0,mode=manual"}),
           make_args({"--network", "name=eth0,mode=manual"}), make_args({"devel"}),
           make_args({"hirsute", "--network", "name=eth0,mode=manual"}), make_args({"daily:21.04"}),
           make_args({"daily:21.04", "--network", "name=eth0,mode=manual"}),
           make_args({"appliance:openhab", "--network", "name=eth0,mode=manual"}), make_args({"appliance:nextcloud"}),
           make_args({"snapcraft:core18", "--network", "name=eth0,mode=manual"}), make_args({"snapcraft:core20"})));

TEST_P(LaunchWithBridges, creates_network_cloud_init_iso)
{
    mpt::MockVirtualMachineFactory* mock_factory = use_a_mock_vm_factory();
    mp::Daemon daemon{config_builder.build()};

    const auto test_params = GetParam();
    const auto args = test_params.first;
    const auto forbidden_names = test_params.second;

    EXPECT_CALL(*mock_factory, prepare_instance_image(_, _))
        .WillOnce(Invoke([&args, &forbidden_names](const multipass::VMImage&,
                                                   const mp::VirtualMachineDescription& desc) {
            EXPECT_THAT(desc.network_data_config, YAMLNodeContainsMap("ethernets"));

            EXPECT_THAT(desc.network_data_config["ethernets"], YAMLNodeContainsMap("default"));
            auto const& default_network_stanza = desc.network_data_config["ethernets"]["default"];
            EXPECT_THAT(default_network_stanza, YAMLNodeContainsMap("match"));
            EXPECT_THAT(default_network_stanza["match"], YAMLNodeContainsStringStartingWith("macaddress", "52:54:00:"));
            EXPECT_THAT(default_network_stanza, YAMLNodeContainsString("dhcp4", "true"));

            for (const auto& arg : args)
            {
                std::string name = std::get<1>(arg);
                if (!name.empty())
                {
                    EXPECT_THAT(desc.network_data_config["ethernets"], YAMLNodeContainsMap(name));
                    auto const& extra_stanza = desc.network_data_config["ethernets"][name];
                    EXPECT_THAT(extra_stanza, YAMLNodeContainsMap("match"));

                    std::string mac = std::get<2>(arg);
                    if (mac.size() == 17)
                        EXPECT_THAT(extra_stanza["match"], YAMLNodeContainsString("macaddress", mac));
                    else
                        EXPECT_THAT(extra_stanza["match"], YAMLNodeContainsStringStartingWith("macaddress", mac));

                    EXPECT_THAT(extra_stanza, YAMLNodeContainsString("dhcp4", "true"));
                    EXPECT_THAT(extra_stanza, YAMLNodeContainsMap("dhcp4-overrides"));
                    EXPECT_THAT(extra_stanza["dhcp4-overrides"], YAMLNodeContainsString("route-metric", "200"));
                    EXPECT_THAT(extra_stanza, YAMLNodeContainsString("optional", "true"));
                }
            }

            for (const auto& forbidden : forbidden_names)
            {
                EXPECT_THAT(desc.network_data_config["ethernets"], Not(YAMLNodeContainsMap(forbidden)));
            }
        }));

    std::vector<std::string> command(1, "launch");

    for (const auto& arg : args)
    {
        command.push_back("--network");
        command.push_back(std::get<0>(arg));
    }

    send_command(command);
}

typedef typename std::pair<std::vector<std::tuple<std::string, std::string, std::string>>, std::vector<std::string>>
    BridgeTestArgType;

INSTANTIATE_TEST_SUITE_P(
    Daemon, LaunchWithBridges,
    Values(BridgeTestArgType({{"eth0", "extra0", "52:54:00:"}}, {"extra1"}),
           BridgeTestArgType({{"name=eth0,mac=01:23:45:ab:cd:ef,mode=auto", "extra0", "01:23:45:ab:cd:ef"},
                              {"wlan0", "extra1", "52:54:00:"}},
                             {"extra2"}),
           BridgeTestArgType({{"name=eth0,mode=manual", "", ""}, {"name=wlan0", "extra1", "52:54:00:"}},
                             {"extra0", "extra2"})));

TEST_P(MinSpaceRespectedSuite, accepts_launch_with_enough_explicit_memory)
{
    auto mock_factory = use_a_mock_vm_factory();
    mp::Daemon daemon{config_builder.build()};

    const auto param = GetParam();
    const auto& cmd = std::get<0>(param);
    const auto& opt_name = std::get<1>(param);
    const auto& opt_value = std::get<2>(param);

    EXPECT_CALL(*mock_factory, create_virtual_machine(_, _));
    send_command({cmd, opt_name, opt_value});
}

TEST_P(MinSpaceViolatedSuite, refuses_launch_with_memory_below_threshold)
{
    auto mock_factory = use_a_mock_vm_factory();
    mp::Daemon daemon{config_builder.build()};

    std::stringstream stream;
    const auto param = GetParam();
    const auto& cmd = std::get<0>(param);
    const auto& opt_name = std::get<1>(param);
    const auto& opt_value = std::get<2>(param);

    EXPECT_CALL(*mock_factory, create_virtual_machine(_, _)).Times(0); // expect *no* call
    send_command({cmd, opt_name, opt_value}, std::cout, stream);
    EXPECT_THAT(stream.str(), AllOf(HasSubstr("fail"), AnyOf(HasSubstr("memory"), HasSubstr("disk"))));
}

TEST_P(LaunchImgSizeSuite, launches_with_correct_disk_size)
{
    auto mock_factory = use_a_mock_vm_factory();
    const auto param = GetParam();
    const auto& first_command_line_parameter = std::get<0>(param);
    const auto& other_command_line_parameters = std::get<1>(param);
    const auto& img_size_str = std::get<2>(param);
    const auto img_size = mp::MemorySize(img_size_str);

    auto mock_image_vault = std::make_unique<NiceMock<mpt::MockVMImageVault>>();
    ON_CALL(*mock_image_vault.get(), minimum_image_size_for(_)).WillByDefault([&img_size_str](auto...) {
        return mp::MemorySize{img_size_str};
    });

    EXPECT_CALL(*mock_utils, filesystem_bytes_available(_)).WillRepeatedly(Return(default_total_bytes));

    config_builder.vault = std::move(mock_image_vault);
    mp::Daemon daemon{config_builder.build()};

    std::vector<std::string> all_parameters{first_command_line_parameter};
    for (const auto& p : other_command_line_parameters)
        all_parameters.push_back(p);

    if (other_command_line_parameters.size() > 0 && mp::MemorySize(other_command_line_parameters[1]) < img_size)
    {
        std::stringstream stream;
        EXPECT_CALL(*mock_factory, create_virtual_machine(_, _)).Times(0);
        send_command(all_parameters, trash_stream, stream);
        EXPECT_THAT(stream.str(), AllOf(HasSubstr("Requested disk"), HasSubstr("below minimum for this image")));
    }
    else
    {
        EXPECT_CALL(*mock_factory, create_virtual_machine(_, _));
        send_command(all_parameters);
    }
}

TEST_P(LaunchStorageCheckSuite, launch_warns_when_overcommitting_disk)
{
    auto mock_factory = use_a_mock_vm_factory();
    mp::Daemon daemon{config_builder.build()};

    EXPECT_CALL(*mock_utils, filesystem_bytes_available(_)).WillRepeatedly(Return(0));

    auto logger_scope = mpt::MockLogger::inject();
    logger_scope.mock_logger->screen_logs(mpl::Level::error);
    logger_scope.mock_logger->expect_log(mpl::Level::error, "autostart prerequisites", AtMost(1));
    logger_scope.mock_logger->expect_log(mpl::Level::warning,
                                         fmt::format("Reserving more disk space ({} bytes) than available (0 bytes)",
                                                     mp::MemorySize{mp::default_disk_size}.in_bytes()));
    EXPECT_CALL(*mock_factory, create_virtual_machine(_, _));
    send_command({GetParam()});
}

TEST_P(LaunchStorageCheckSuite, launch_fails_when_space_less_than_image)
{
    auto mock_factory = use_a_mock_vm_factory();

    auto mock_image_vault = std::make_unique<NiceMock<mpt::MockVMImageVault>>();
    ON_CALL(*mock_image_vault.get(), minimum_image_size_for(_)).WillByDefault([](auto...) {
        return mp::MemorySize{"1"};
    });
    config_builder.vault = std::move(mock_image_vault);

    mp::Daemon daemon{config_builder.build()};

    EXPECT_CALL(*mock_utils, filesystem_bytes_available(_)).WillRepeatedly(Return(0));

    std::stringstream stream;
    EXPECT_CALL(*mock_factory, create_virtual_machine(_, _)).Times(0);
    send_command({GetParam()}, trash_stream, stream);
    EXPECT_THAT(stream.str(), HasSubstr("Available disk (0 bytes) below minimum for this image (1 bytes)"));
}

TEST_P(LaunchStorageCheckSuite, launch_fails_with_invalid_data_directory)
{
    auto mock_factory = use_a_mock_vm_factory();
    config_builder.data_directory = QString("invalid_data_directory");
    mp::Daemon daemon{config_builder.build()};

    std::stringstream stream;
    EXPECT_CALL(*mock_factory, create_virtual_machine(_, _)).Times(0);
    send_command({GetParam()}, trash_stream, stream);
    EXPECT_THAT(stream.str(), HasSubstr("Failed to determine information about the volume containing"));
}

INSTANTIATE_TEST_SUITE_P(Daemon, DaemonCreateLaunchTestSuite, Values("launch", "test_create"));
INSTANTIATE_TEST_SUITE_P(Daemon, MinSpaceRespectedSuite,
                         Combine(Values("test_create", "launch"), Values("--mem", "--disk"),
                                 Values("1024m", "2Gb", "987654321")));
INSTANTIATE_TEST_SUITE_P(Daemon, MinSpaceViolatedSuite,
                         Combine(Values("test_create", "launch"), Values("--mem", "--disk"),
                                 Values("0", "0B", "0GB", "123B", "42kb", "100")));
INSTANTIATE_TEST_SUITE_P(Daemon, LaunchImgSizeSuite,
                         Combine(Values("test_create", "launch"),
                                 Values(std::vector<std::string>{}, std::vector<std::string>{"--disk", "4G"}),
                                 Values("1G", mp::default_disk_size, "10G")));
INSTANTIATE_TEST_SUITE_P(Daemon, LaunchStorageCheckSuite, Values("test_create", "launch"));

std::string fake_json_contents(const std::string& default_mac, const std::vector<mp::NetworkInterface>& extra_ifaces)
{
    QString contents("{\n"
                     "    \"real-zebraphant\": {\n"
                     "        \"deleted\": false,\n"
                     "        \"disk_space\": \"5368709120\",\n"
                     "        \"extra_interfaces\": [\n");

    QStringList extra_json;
    for (auto extra_interface : extra_ifaces)
    {
        extra_json += QString::fromStdString(fmt::format("            {{\n"
                                                         "                \"auto_mode\": {},\n"
                                                         "                \"id\": \"{}\",\n"
                                                         "                \"mac_address\": \"{}\"\n"
                                                         "            }}\n",
                                                         extra_interface.auto_mode, extra_interface.id,
                                                         extra_interface.mac_address));
    }
    contents += extra_json.join(',');

    contents += QString::fromStdString(fmt::format("        ],\n"
                                                   "        \"mac_addr\": \"{}\",\n"
                                                   "        \"mem_size\": \"1073741824\",\n"
                                                   "        \"metadata\": {{\n"
                                                   "            \"arguments\": [\n"
                                                   "                \"many\",\n"
                                                   "                \"arguments\"\n"
                                                   "            ],\n"
                                                   "            \"machine_type\": \"dmc-de-lorean\"\n"
                                                   "        }},\n"
                                                   "        \"mounts\": [\n"
                                                   "        ],\n"
                                                   "        \"num_cores\": 1,\n"
                                                   "        \"ssh_username\": \"ubuntu\",\n"
                                                   "        \"state\": 2\n"
                                                   "    }}\n"
                                                   "}}",
                                                   default_mac));

    return contents.toStdString();
}

void check_interfaces_in_json(const QString& file, const std::string& mac,
                              const std::vector<mp::NetworkInterface>& extra_interfaces)
{
    QByteArray json = mpt::load(file);

    QJsonParseError parse_error;
    const auto doc = QJsonDocument::fromJson(json, &parse_error);
    EXPECT_FALSE(doc.isNull());
    EXPECT_TRUE(doc.isObject());

    const auto doc_object = doc.object();
    const auto instance_object = doc_object["real-zebraphant"].toObject();
    const auto default_mac = instance_object["mac_addr"].toString().toStdString();
    ASSERT_EQ(default_mac, mac);

    const auto extra = instance_object["extra_interfaces"].toArray();
    ASSERT_EQ((unsigned)extra.size(), extra_interfaces.size());

    auto it = extra_interfaces.cbegin();
    for (const auto& extra_i : extra)
    {
        const auto interface = extra_i.toObject();
        ASSERT_EQ(interface["mac_address"].toString().toStdString(), it->mac_address);
        ASSERT_EQ(interface["id"].toString().toStdString(), it->id);
        ASSERT_EQ(interface["auto_mode"].toBool(), it->auto_mode);
        ++it;
    }
}

TEST_F(Daemon, reads_mac_addresses_from_json)
{
    config_builder.vault = std::make_unique<NiceMock<mpt::MockVMImageVault>>();

    std::string mac_addr("52:54:00:73:76:28");
    std::vector<mp::NetworkInterface> extra_interfaces{
        mp::NetworkInterface{"wlx60e3270f55fe", "52:54:00:bd:19:41", true},
        mp::NetworkInterface{"enp3s0", "01:23:45:67:89:ab", false}};

    std::string json_contents = fake_json_contents(mac_addr, extra_interfaces);

    mpt::TempDir temp_dir;
    QString filename(temp_dir.path() + "/multipassd-vm-instances.json");

    mpt::make_file_with_content(filename, json_contents);

    // Make the daemon look for the JSON on our temporary directory. It will read the contents of the file.
    config_builder.data_directory = temp_dir.path();
    mp::Daemon daemon{config_builder.build()};

    // By issuing the `list` command, we check at least that the instance was indeed read and there were no errors.
    std::stringstream stream;
    send_command({"list"}, stream);
    EXPECT_THAT(stream.str(), HasSubstr("real-zebraphant"));

    // Removing the JSON is possible now because data was already read. This step is not necessary, but doing it we
    // make sure that the file was indeed rewritten after the next step.
    QFile::remove(filename);

    // The purge command will be apparently no-op, because there are no deleted instances. However, it will trigger
    // a rewriting of the JSON, which will be useful for us to check if the data was correctly read.
    send_command({"purge"});

    // Finally, check the contents of the file. If they match with what we read, we are done.
    check_interfaces_in_json(filename, mac_addr, extra_interfaces);
}

TEST_F(Daemon, launches_with_valid_network_interface)
{
    mpt::MockVirtualMachineFactory* mock_factory = use_a_mock_vm_factory();

    mp::Daemon daemon{config_builder.build()};

    EXPECT_CALL(*mock_factory, networks());

    ASSERT_NO_THROW(send_command({"launch", "--network", "eth0"}));
}

TEST_F(Daemon, refuses_launch_with_invalid_network_interface)
{
    mpt::MockVirtualMachineFactory* mock_factory = use_a_mock_vm_factory();

    mp::Daemon daemon{config_builder.build()};

    EXPECT_CALL(*mock_factory, networks());

    std::stringstream err_stream;
    send_command({"launch", "--network", "eth2"}, std::cout, err_stream);
    EXPECT_THAT(err_stream.str(), HasSubstr("Invalid network options supplied"));
}

TEST_F(Daemon, refuses_launch_because_bridging_is_not_implemented)
{
    // Use the stub factory, which throws when networks() is called.
    mp::Daemon daemon{config_builder.build()};

    std::stringstream err_stream;
    send_command({"launch", "--network", "eth0"}, std::cout, err_stream);
    EXPECT_THAT(err_stream.str(), HasSubstr("The bridging feature is not implemented on this backend"));
}

TEST_P(RefuseBridging, old_image)
{
    const auto [remote, image] = GetParam();
    std::string full_image_name = remote.empty() ? image : remote + ":" + image;

    auto mock_factory = use_a_mock_vm_factory();

    mp::Daemon daemon{config_builder.build()};

    EXPECT_CALL(*mock_factory, networks());

    std::stringstream err_stream;
    send_command({"launch", full_image_name, "--network", "eth0"}, std::cout, err_stream);
    EXPECT_THAT(err_stream.str(), HasSubstr("Automatic network configuration not available for"));
}

std::vector<std::string> old_releases{"10.04",   "lucid",  "11.10",   "oneiric", "12.04", "precise", "12.10",
                                      "quantal", "13.04",  "raring",  "13.10",   "saucy", "14.04",   "trusty",
                                      "14.10",   "utopic", "15.04",   "vivid",   "15.10", "wily",    "16.04",
                                      "xenial",  "16.10",  "yakkety", "17.04",   "zesty"};

INSTANTIATE_TEST_SUITE_P(DaemonRefuseRelease, RefuseBridging, Combine(Values("release", ""), ValuesIn(old_releases)));
INSTANTIATE_TEST_SUITE_P(DaemonRefuseSnapcraft, RefuseBridging, Values(std::make_tuple("snapcraft", "core")));

std::unique_ptr<mpt::TempDir> plant_instance_json(const std::string& contents) // unique_ptr bypasses missing move ctor
{
    auto temp_dir = std::make_unique<mpt::TempDir>();
    QString filename(temp_dir->path() + "/multipassd-vm-instances.json");

    mpt::make_file_with_content(filename, contents);

    return temp_dir;
}

constexpr auto ghost_template = R"(
"{}": {{
    "deleted": false,
    "disk_space": "0",
    "mac_addr": "",
    "mem_size": "0",
    "metadata": {{}},
    "mounts": [],
    "num_cores": 0,
    "ssh_username": "",
    "state": 0
}})";
constexpr auto valid_template = R"(
"{}": {{
    "deleted": false,
    "disk_space": "3232323232",
    "mac_addr": "ab:cd:ef:12:34:{}",
    "mem_size": "2323232323",
    "metadata": {{}},
    "mounts": [],
    "num_cores": 4,
    "ssh_username": "ubuntu",
    "state": 1
}})";

TEST_F(Daemon, skips_over_instance_ghosts_in_db) // which will have been sometimes writen for purged instances
{
    config_builder.vault = std::make_unique<NiceMock<mpt::MockVMImageVault>>();

    const auto id1 = "valid1";
    const auto id2 = "valid2";
    auto ghost1 = fmt::format(ghost_template, "ghost1");
    auto ghost2 = fmt::format(ghost_template, "ghost2");
    auto valid1 = fmt::format(valid_template, id1, "56");
    auto valid2 = fmt::format(valid_template, id2, "78");
    auto temp_dir = plant_instance_json(fmt::format("{{\n{},\n{},\n{},\n{}\n}}", std::move(ghost1), std::move(ghost2),
                                                    std::move(valid1), std::move(valid2)));

    config_builder.data_directory = temp_dir->path();
    auto mock_factory = use_a_mock_vm_factory();

    EXPECT_CALL(*mock_factory, create_virtual_machine).Times(0);
    EXPECT_CALL(*mock_factory, create_virtual_machine(Field(&mp::VirtualMachineDescription::vm_name, id1), _)).Times(1);
    EXPECT_CALL(*mock_factory, create_virtual_machine(Field(&mp::VirtualMachineDescription::vm_name, id2), _)).Times(1);

    mp::Daemon daemon{config_builder.build()};
}

TEST_P(ListIP, lists_with_ip)
{
    auto mock_factory = use_a_mock_vm_factory();
    config_builder.vault = std::make_unique<NiceMock<mpt::MockVMImageVault>>();

    mp::Daemon daemon{config_builder.build()};

    auto instance_ptr = std::make_unique<NiceMock<mpt::MockVirtualMachine>>("mock");
    EXPECT_CALL(*mock_factory, create_virtual_machine).WillRepeatedly([&instance_ptr](const auto&, auto&) {
        return std::move(instance_ptr);
    });

    auto [state, cmd, strs] = GetParam();

    EXPECT_CALL(*instance_ptr, current_state()).WillRepeatedly(Return(state));
    EXPECT_CALL(*instance_ptr, ensure_vm_is_running()).WillRepeatedly(Throw(std::runtime_error("Not running")));

    send_command({"launch"});

    std::stringstream stream;
    send_command(cmd, stream);

    for (const auto& s : strs)
        EXPECT_THAT(stream.str(), HasSubstr(s));
}

INSTANTIATE_TEST_SUITE_P(
    Daemon, ListIP,
    Values(std::make_tuple(mp::VirtualMachine::State::running, std::vector<std::string>{"list"},
                           std::vector<std::string>{"Running", "192.168.2.123"}),
           std::make_tuple(mp::VirtualMachine::State::running, std::vector<std::string>{"list", "--no-ipv4"},
                           std::vector<std::string>{"Running", "--"}),
           std::make_tuple(mp::VirtualMachine::State::off, std::vector<std::string>{"list"},
                           std::vector<std::string>{"Stopped", "--"}),
           std::make_tuple(mp::VirtualMachine::State::off, std::vector<std::string>{"list", "--no-ipv4"},
                           std::vector<std::string>{"Stopped", "--"})));

TEST_F(Daemon, prevents_repetition_of_loaded_mac_addresses)
{
    config_builder.vault = std::make_unique<NiceMock<mpt::MockVMImageVault>>();

    std::string repeated_mac{"52:54:00:bd:19:41"};
    auto temp_dir = plant_instance_json(fake_json_contents(repeated_mac, {}));
    config_builder.data_directory = temp_dir->path();

    auto mock_factory = use_a_mock_vm_factory();
    mp::Daemon daemon{config_builder.build()};

    std::stringstream stream;
    EXPECT_CALL(*mock_factory, create_virtual_machine).Times(0); // expect *no* call
    send_command({"launch", "--network", fmt::format("name=eth0,mac={}", repeated_mac)}, std::cout, stream);
    EXPECT_THAT(stream.str(), AllOf(HasSubstr("fail"), HasSubstr("Repeated MAC"), HasSubstr(repeated_mac)));
}

TEST_F(Daemon, does_not_hold_on_to_repeated_mac_addresses_when_loading)
{
    config_builder.vault = std::make_unique<NiceMock<mpt::MockVMImageVault>>();

    std::string mac_addr("52:54:00:73:76:28");
    std::vector<mp::NetworkInterface> extra_interfaces{mp::NetworkInterface{"eth0", mac_addr, true}};

    auto temp_dir = plant_instance_json(fake_json_contents(mac_addr, extra_interfaces));
    config_builder.data_directory = temp_dir->path();

    auto mock_factory = use_a_mock_vm_factory();
    mp::Daemon daemon{config_builder.build()};

    EXPECT_CALL(*mock_factory, create_virtual_machine);
    send_command({"launch", "--network", fmt::format("name=eth0,mac={}", mac_addr)});
}

TEST_F(Daemon, does_not_hold_on_to_macs_when_loading_fails)
{
    config_builder.vault = std::make_unique<NiceMock<mpt::MockVMImageVault>>();

    std::string mac1{"52:54:00:73:76:28"}, mac2{"52:54:00:bd:19:41"};
    std::vector<mp::NetworkInterface> extra_interfaces{mp::NetworkInterface{"eth0", mac2, true}};

    auto temp_dir = plant_instance_json(fake_json_contents(mac1, extra_interfaces));
    config_builder.data_directory = temp_dir->path();

    auto mock_factory = use_a_mock_vm_factory();
    EXPECT_CALL(*mock_factory, create_virtual_machine)
        .Times(3)                          // expect one call in the constructor and three in launch
        .WillOnce(Throw(std::exception{})) // fail the first one
        .WillRepeatedly(DoDefault());      // succeed the rest (this avoids gmock warnings)
    mp::Daemon daemon{config_builder.build()};

    for (const auto& mac : {mac1, mac2})
        send_command({"launch", "--network", fmt::format("name=eth0,mac={}", mac)});
}

TEST_F(Daemon, does_not_hold_on_to_macs_when_image_preparation_fails)
{
    auto mock_factory = use_a_mock_vm_factory();
    mp::Daemon daemon{config_builder.build()};

    // fail the first prepare call, succeed the second one
    InSequence seq;
    EXPECT_CALL(*mock_factory, prepare_instance_image).WillOnce(Throw(std::exception{})).WillOnce(DoDefault());
    EXPECT_CALL(*mock_factory, create_virtual_machine).Times(1);

    auto cmd = std::vector<std::string>{"launch", "--network", "mac=52:54:00:73:76:28,name=wlan0"};
    send_command(cmd); // we cause this one to fail
    send_command(cmd); // and confirm we can repeat the same mac
}

TEST_F(Daemon, releases_macs_when_launch_fails)
{
    auto mock_factory = use_a_mock_vm_factory();
    mp::Daemon daemon{config_builder.build()};

    EXPECT_CALL(*mock_factory, create_virtual_machine).WillOnce(Throw(std::exception{})).WillOnce(DoDefault());

    auto cmd = std::vector<std::string>{"launch", "--network", "mac=52:54:00:73:76:28,name=wlan0"};
    send_command(cmd); // we cause this one to fail
    send_command(cmd); // and confirm we can repeat the same mac
}

TEST_F(Daemon, releases_macs_of_purged_instances_but_keeps_the_rest)
{
    auto mock_factory = use_a_mock_vm_factory();
    mp::Daemon daemon{config_builder.build()};

    auto mac1 = "52:54:00:73:76:28", mac2 = "52:54:00:bd:19:41", mac3 = "01:23:45:67:89:ab";

    auto mac_matcher = [](const auto& mac) {
        return Field(&mp::VirtualMachineDescription::extra_interfaces,
                     Contains(Field(&mp::NetworkInterface::mac_address, mac)));
    };
    EXPECT_CALL(*mock_factory, create_virtual_machine(mac_matcher(mac1), _)).Times(1);
    EXPECT_CALL(*mock_factory, create_virtual_machine(mac_matcher(mac2), _)).Times(1);
    EXPECT_CALL(*mock_factory, create_virtual_machine(mac_matcher(mac3), _)).Times(2); // this one gets reused

    send_command({"launch", "--network", fmt::format("name=eth0,mac={}", mac1), "--name", "vm1"});
    send_command({"launch", "--network", fmt::format("name=eth0,mac={}", mac2), "--name", "vm2"});
    send_command({"launch", "--network", fmt::format("name=eth0,mac={}", mac3), "--name", "vm3"});

    send_command({"delete", "vm1"});
    send_command({"delete", "--purge", "vm3"}); // so that mac3 can be reused

    send_command({"launch", "--network", fmt::format("name=eth0,mac={}", mac1)}); // repeated mac is rejected
    send_command({"launch", "--network", fmt::format("name=eth0,mac={}", mac2)}); // idem
    send_command(
        {"launch", "--network", fmt::format("name=eth0,mac={}", mac3)}); // mac is free after purge, so accepted
}

} // namespace
