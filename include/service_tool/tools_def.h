#pragma once

#include <set>
#include <string>
namespace qifeng::scm::tool {
    struct MariadbDef {
        std::string host = "127.0.0.1";
        std::string port = "3306";
        std::string adminUser = "root";
        std::string adminPassword = "Test@123";
    };
    inline bool IsSystemdService(const std::string &serviceName) {
        static const std::set<std::string> SystemdServices = {"mariadb", "mysql", "openGauss", "bind"};
        return SystemdServices.find(serviceName) != SystemdServices.end();
    }
    inline bool IsMariadbService(const std::string &serviceName) {
        return serviceName == "mariadb" || serviceName == "mysql";
    }
}  // namespace qifeng::scm::tool