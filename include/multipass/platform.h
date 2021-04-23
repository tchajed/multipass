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

#ifndef MULTIPASS_PLATFORM_H
#define MULTIPASS_PLATFORM_H

#include <multipass/days.h>
#include <multipass/logging/logger.h>
#include <multipass/network_interface_info.h>
#include <multipass/process/process.h>
#include <multipass/process/process_spec.h>
#include <multipass/singleton.h>
#include <multipass/sshfs_server_config.h>
#include <multipass/update_prompt.h>
#include <multipass/url_downloader.h>
#include <multipass/virtual_machine_factory.h>
#include <multipass/vm_image_vault.h>

#include <libssh/sftp.h>

#include <QString>

#include <functional>
#include <memory>
#include <string>
#include <vector>

#define MP_PLATFORM multipass::platform::Platform::instance()

namespace multipass
{
namespace platform
{
class Platform : public Singleton<Platform>
{
public:
    Platform(const Singleton::PrivatePass&) noexcept;
    // Get information on the network interfaces that are seen by the platform, indexed by name
    virtual std::map<std::string, NetworkInterfaceInfo> get_network_interfaces_info() const;
    virtual QString get_workflows_url_override();
    virtual bool is_alias_supported(const std::string& alias, const std::string& remote);
    virtual bool is_remote_supported(const std::string& remote);
    virtual int chown(const char* path, unsigned int uid, unsigned int gid);
    virtual bool link(const char* target, const char* link);
    virtual bool symlink(const char* target, const char* link, bool is_dir);
    virtual int utime(const char* path, int atime, int mtime);
};

std::map<QString, QString> extra_settings_defaults();

QString interpret_setting(const QString& key, const QString& val);
void sync_winterm_profiles();

QString autostart_test_data(); // returns a platform-specific string, for testing purposes
void setup_gui_autostart_prerequisites();

std::string default_server_address();
QString default_driver();

QString daemon_config_home();                      // temporary

bool is_backend_supported(const QString& backend); // temporary
VirtualMachineFactory::UPtr vm_backend(const Path& data_dir);
logging::Logger::UPtr make_logger(logging::Level level);
UpdatePrompt::UPtr make_update_prompt();
std::unique_ptr<Process> make_sshfs_server_process(const SSHFSServerConfig& config);
std::unique_ptr<Process> make_process(std::unique_ptr<ProcessSpec>&& process_spec);
int symlink_attr_from(const char* path, sftp_attributes_struct* attr);
bool is_image_url_supported();

std::function<int()> make_quit_watchdog(); // call while single-threaded; call result later, in dedicated thread

std::string reinterpret_interface_id(const std::string& ux_id); // give platforms a chance to reinterpret network IDs

} // namespace platform
} // namespace multipass

inline multipass::platform::Platform::Platform(const PrivatePass& pass) noexcept : Singleton(pass)
{
}

#endif // MULTIPASS_PLATFORM_H
