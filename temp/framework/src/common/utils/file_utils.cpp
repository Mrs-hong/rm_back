#include <fstream>

#include "common/utils/file_utils.h"

namespace common {
    namespace utils {

        std::string LoadAllContentFromFile(const std::filesystem::path& path) {
            std::ifstream fs {path, std::ios::in};
            if (fs.fail()) {
                throw std::runtime_error {"File " + std::string {path.c_str()} + " can not be opened."};
            }
            fs.seekg(0, std::ios::end);
            if (fs.fail()) {
                throw std::runtime_error {"File " + std::string {path.c_str()} + " can not be opened."};
            }
            std::size_t size = static_cast<std::size_t>(fs.tellg());
            fs.seekg(0, std::ios::beg);
            if (fs.fail()) {
                throw std::runtime_error {"File " + std::string {path.c_str()} + " can not be opened."};
            }
            std::string content {};
            content.resize(size);
            fs.read(content.data(), size);
            if (fs.fail()) {
                throw std::runtime_error {"File " + std::string {path.c_str()} + " can not be opened."};
            }
            return content;
        }

    }  // namespace utils
}  // namespace common