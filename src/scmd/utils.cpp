#include "common/utils.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <queue>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace {
    using qifeng::scm::CheckDependencyError;
    using qifeng::scm::ServiceDefinition;
    enum class VisitState { UNVISITED, IN_PROGRESS, COMPLETED };

    // 检查从当前节点是否存在循环依赖
    bool CheckCircularFromNode(const std::string &serviceName, const std::map<std::string, ServiceDefinition> &services,
                               std::map<std::string, VisitState> &state, std::vector<std::string> &currentPath) {
        state[serviceName] = VisitState::IN_PROGRESS;
        currentPath.push_back(serviceName);

        auto it = services.find(serviceName);
        if (it != services.end()) {
            for (const auto &[depName, depVersion] : it->second.dependencies) {
                if (state[depName] == VisitState::IN_PROGRESS) {
                    return true;
                }
                if (CheckCircularFromNode(depName, services, state, currentPath)) {
                    return true;
                }
            }
        }

        currentPath.pop_back();
        state[serviceName] = VisitState::COMPLETED;
        return false;
    }

    // 标记循环依赖路径中的所有节点
    void MarkCircularInPath(const std::string &circularNode, const std::vector<std::string> &currentPath,
                            std::vector<CheckDependencyError> &errors) {
        bool found = false;
        for (const auto &node : currentPath) {
            if (node == circularNode) {
                found = true;
            }
            if (found) {
                errors.push_back({CheckDependencyError::Status::CIRCULAR, node});
            }
        }
    }

    // 检测所有服务的循环依赖
    void DetectCircularDependencies(const std::map<std::string, ServiceDefinition> &services,
                                    std::vector<CheckDependencyError> &errors) {
        std::map<std::string, VisitState> state;

        for (const auto &[serviceName, serviceDef] : services) {
            if (state[serviceName] == VisitState::UNVISITED) {
                std::vector<std::string> currentPath;
                if (CheckCircularFromNode(serviceName, services, state, currentPath)) {
                    MarkCircularInPath(serviceName, currentPath, errors);
                }
            }
        }
    }

    // 检测缺失服务和版本冲突
    void DetectMissingAndVersionConflicts(const std::map<std::string, ServiceDefinition> &services,
                                          std::vector<CheckDependencyError> &errors) {
        for (const auto &[serviceName, serviceDef] : services) {
            for (const auto &[depName, depVersion] : serviceDef.dependencies) {
                auto depIt = services.find(depName);
                if (depIt == services.end()) {
                    errors.push_back({CheckDependencyError::Status::MISSING, serviceName});
                } else if (depIt->second.version != depVersion) {
                    errors.push_back({CheckDependencyError::Status::VERSION_CONFLICT, serviceName});
                }
            }
        }
    }
}  // namespace

namespace qifeng::scm::utils {
    ResultMsg CreateDirectory(const std::string &dir) {
        namespace fs = std::filesystem;
        if (fs::exists(dir)) {
            return MakeSuccess();
        }
        try {
            fs::create_directories(dir);
            return MakeSuccess();
        } catch (const std::exception &e) {
            return MakeError("Failed to create directory: " + std::string(e.what()));
        }
    }

    ResultMsg ForceDeleteDirectory(const std::string &dir) {
        namespace fs = std::filesystem;
        if (!fs::exists(dir)) {
            return MakeSuccess();
        }
        try {
            fs::remove_all(dir);
            return MakeSuccess();
        } catch (const std::exception &e) {
            return MakeError("Failed to delete directory: " + std::string(e.what()));
        }
    }

    ResultMsg MoveDirectory(const std::string &src, const std::string &dst) {
        namespace fs = std::filesystem;
        if (!fs::exists(src)) {
            return MakeError("Source directory does not exist");
        }
        try {
            fs::rename(src, dst);
            return MakeSuccess();
        } catch (const std::exception &e) {
            return MakeError("Failed to move directory: " + std::string(e.what()));
        }
    }

    ResultMsg CopyDirectory(const std::string &src, const std::string &dst) {
        namespace fs = std::filesystem;
        std::error_code ec;
        if (!fs::exists(src, ec)) {
            return MakeError("Source directory does not exist: " + src);
        }

        fs::create_directories(dst, ec);
        if (ec) {
            return MakeError("Failed to create destination directory: " + dst + ", " + ec.message());
        }

        for (const auto &entry : fs::directory_iterator(src)) {
            const auto &path = entry.path();
            fs::path destPath = fs::path(dst) / path.filename();

            if (entry.is_directory()) {
                auto result = CopyDirectory(path.string(), destPath.string());
                if (!result.IsDefalutSuccess()) {
                    return result;
                }
            } else {
                fs::copy_file(path, destPath, fs::copy_options::overwrite_existing, ec);
                if (ec) {
                    return MakeError("Failed to copy file: " + path.string() + " -> " + destPath.string() +
                                     ", " + ec.message());
                }
            }
        }
        return MakeSuccess();
    }

    ResultMsg ClearDirectoryContents(const std::string &dir) {
        namespace fs = std::filesystem;
        std::error_code ec;
        if (!fs::exists(dir, ec)) {
            return MakeSuccess();
        }

        for (const auto &entry : fs::directory_iterator(dir)) {
            fs::remove_all(entry.path(), ec);
            if (ec) {
                return MakeError("Failed to remove: " + entry.path().string() + ", " + ec.message());
            }
        }
        return MakeSuccess();
    }

    std::string GetAbsolutePath(const std::string &path) {
        namespace fs = std::filesystem;
        std::error_code ec;
        auto absPath = fs::absolute(path, ec);
        if (ec) {
            return path;
        }
        return absPath.string();
    }

    ResultMsg CreateSymbolicLink(const std::string &target, const std::string &linkPath) {
        namespace fs = std::filesystem;
        if (fs::exists(linkPath)) {
            return MakeError("Symbolic link already exists: " + linkPath);
        }
        try {
            fs::create_symlink(target, linkPath);
            return MakeSuccess();
        } catch (const std::exception &e) {
            return MakeError("Failed to create symbolic link: " + std::string(e.what()));
        }
    }

    ResultMsg DeleteSymbolicLink(const std::string &linkPath) {
        namespace fs = std::filesystem;
        if (!fs::exists(linkPath)) {
            return MakeSuccess();
        }
        if (!fs::is_symlink(linkPath)) {
            return MakeError("Path is not a symbolic link: " + linkPath);
        }
        try {
            fs::remove(linkPath);
            return MakeSuccess();
        } catch (const std::exception &e) {
            return MakeError("Failed to delete symbolic link: " + std::string(e.what()));
        }
    }

    ResultMsg ExtractTar(const std::string &tarPath, const std::string &extractDir) {
        namespace fs = std::filesystem;
        if (!fs::exists(tarPath)) {
            return MakeError("Tar file does not exist: " + tarPath);
        }

        if (auto result = CreateDirectory(extractDir); !result.IsDefalutSuccess()) {
            return result;
        }

        std::string cmd = "tar -xzf \"" + tarPath + "\" -C \"" + extractDir + "\" 2>&1";
        FILE* pipe = popen(cmd.c_str(), "r");
        if (!pipe) {
            return MakeError("Failed to execute tar command");
        }

        std::array<char, 256> buffer {};
        std::string output;
        while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
            output += buffer.data();
        }

        int status = pclose(pipe);
        if (status != 0) {
            return MakeError("Failed to extract tar: " + output);
        }
        return MakeSuccess();
    }

    ResultMsg CompressDirToTar(const std::string &dir, const std::string &tarPath) {
        namespace fs = std::filesystem;
        if (!fs::exists(dir)) {
            return MakeError("Directory does not exist: " + dir);
        }

        fs::path dirPath(dir);
        fs::path parentPath = dirPath.parent_path();
        std::string dirName = dirPath.filename().string();

        std::string cmd = "tar -czf \"" + tarPath + "\" -C \"" + parentPath.string() + "\" \"" + dirName + "\" 2>&1";
        FILE* pipe = popen(cmd.c_str(), "r");
        if (!pipe) {
            return MakeError("Failed to execute tar command");
        }

        std::array<char, 256> buffer {};
        std::string output;
        while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
            output += buffer.data();
        }

        int status = pclose(pipe);
        if (status != 0) {
            return MakeError("Failed to compress directory: " + output);
        }
        return MakeSuccess();
    }

    ResultMsg VerifyTarWithSha256(const std::string &tarPath, const std::string &sha256Path) {
        namespace fs = std::filesystem;
        if (!fs::exists(tarPath)) {
            return MakeError("Tar file does not exist: " + tarPath);
        }
        if (!fs::exists(sha256Path)) {
            return MakeError("SHA256 file does not exist: " + sha256Path);
        }

        std::ifstream shaFile(sha256Path);
        if (!shaFile.is_open()) {
            return MakeError("Failed to open SHA256 file: " + sha256Path);
        }

        std::string expectedHash;
        std::getline(shaFile, expectedHash);
        shaFile.close();

        size_t spacePos = expectedHash.find(' ');
        if (spacePos != std::string::npos) {
            expectedHash = expectedHash.substr(0, spacePos);
        }

        std::string cmd = "sha256sum \"" + tarPath + "\" 2>&1";
        FILE* pipe = popen(cmd.c_str(), "r");
        if (!pipe) {
            return MakeError("Failed to execute sha256sum command");
        }

        std::array<char, 256> buffer {};
        std::string output;
        while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
            output += buffer.data();
        }

        int status = pclose(pipe);
        if (status != 0) {
            return MakeError("Failed to compute SHA256: " + output);
        }

        spacePos = output.find(' ');
        std::string actualHash = (spacePos != std::string::npos) ? output.substr(0, spacePos) : output;

        if (actualHash != expectedHash) {
            return MakeError("SHA256 mismatch: expected " + expectedHash + ", got " + actualHash);
        }
        return MakeSuccess();
    }

    std::vector<CheckDependencyError> CheckDependenciesMap(const std::map<std::string, ServiceDefinition> &services) {
        std::vector<CheckDependencyError> errors;

        DetectCircularDependencies(services, errors);
        DetectMissingAndVersionConflicts(services, errors);

        return errors;
    }

    // 构建依赖图的邻接表和入度表
    void BuildDependencyGraph(const std::map<std::string, ServiceDefinition> &services,
                              std::unordered_map<std::string, std::vector<std::string>> &adj,
                              std::unordered_map<std::string, int> &inDegree) {
        for (const auto &[name, def] : services) {
            inDegree[name] = 0;
            adj[name] = {};
        }

        for (const auto &[name, def] : services) {
            for (const auto &[depName, depVersion] : def.dependencies) {
                if (services.count(depName)) {
                    adj[depName].push_back(name);
                    inDegree[name]++;
                }
            }
        }
    }

    // 执行 Kahn BFS 拓扑排序，返回排序结果
    std::vector<std::string> DoTopologicalSort(std::unordered_map<std::string, std::vector<std::string>> &adj,
                                               std::unordered_map<std::string, int> inDegree) {
        std::vector<std::string> order;
        std::queue<std::string> q;

        for (const auto &[name, degree] : inDegree) {
            if (degree == 0) {
                q.push(name);
            }
        }

        while (!q.empty()) {
            std::string node = q.front();
            q.pop();
            order.push_back(node);

            auto it = adj.find(node);
            if (it != adj.end()) {
                for (const auto &neighbor : it->second) {
                    inDegree[neighbor]--;
                    if (inDegree[neighbor] == 0) {
                        q.push(neighbor);
                    }
                }
            }
        }

        return order;
    }

    ServiceSequence ComputeServiceSequence(const std::map<std::string, ServiceDefinition> &services) {
        ServiceSequence result;

        if (services.empty()) {
            return result;
        }

        std::unordered_map<std::string, std::vector<std::string>> adj;
        std::unordered_map<std::string, int> inDegree;
        BuildDependencyGraph(services, adj, inDegree);

        result.startOrder = DoTopologicalSort(adj, inDegree);

        if (result.startOrder.size() != services.size()) {
            result.startOrder.clear();
            result.stopOrder.clear();
            return result;
        }

        result.stopOrder = result.startOrder;
        std::reverse(result.stopOrder.begin(), result.stopOrder.end());

        return result;
    }

}  // namespace qifeng::scm::utils
