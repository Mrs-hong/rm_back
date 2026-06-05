/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "ipc/uds.h"

#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <iostream>

// 对系统接口调用、需要使用c-style和reinterpret_cast，避免使用c++的类型转换符
// NOLINTBEGIN(cppcoreguidelines-pro-type-reinterpret-cast,cppcoreguidelines-pro-type-vararg)
namespace qifeng::scm {

    UdsWrapper::UdsWrapper(UdsMode mode, const std::string &socketPath, int maxConnections, int socketMode)
        : mMode(mode), mSocketPath(socketPath), mMaxConnections(maxConnections), mSocketMode(socketMode),
          mSocketFd(-1) {
    }

    UdsWrapper::~UdsWrapper() {
        Close();
    }

    // NOLINTNEXTLINE(readability-function-size, readability-function-cognitive-complexity)
    bool UdsWrapper::Initialize() {
        if (mSocketFd >= 0) {
            Close();
        }

        if (mMode == UdsMode::SERVER) {
            if (!PrepareSocketPath()) {
                return false;
            }

            mSocketFd = socket(AF_UNIX, SOCK_STREAM, 0);
            if (mSocketFd < 0) {
                std::cerr << "UdsWrapper::Initialize socket failed: " << std::strerror(errno) << std::endl;
                return false;
            }

            struct sockaddr_un addr {};
            addr.sun_family = AF_UNIX;
            if (mSocketPath.size() >= sizeof(addr.sun_path)) {
                std::cerr << "UdsWrapper::Initialize socket path too long: " << mSocketPath << std::endl;
                close(mSocketFd);
                mSocketFd = -1;
                return false;
            }
            std::strncpy(addr.sun_path, mSocketPath.c_str(), sizeof(addr.sun_path) - 1);

            if (bind(mSocketFd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
                std::cerr << "UdsWrapper::Initialize bind failed: " << std::strerror(errno) << std::endl;
                close(mSocketFd);
                mSocketFd = -1;
                return false;
            }

            // 设置socket文件权限，允许普通用户连接（scmd以sudo运行时需要）
            if (chmod(mSocketPath.c_str(), static_cast<mode_t>(mSocketMode)) < 0) {
                std::cerr << "UdsWrapper::Initialize chmod failed: " << std::strerror(errno) << std::endl;
                close(mSocketFd);
                mSocketFd = -1;
                RemoveSocketFile();
                return false;
            }

            if (listen(mSocketFd, mMaxConnections) < 0) {
                std::cerr << "UdsWrapper::Initialize listen failed: " << std::strerror(errno) << std::endl;
                close(mSocketFd);
                mSocketFd = -1;
                RemoveSocketFile();
                return false;
            }
        } else {
            mSocketFd = socket(AF_UNIX, SOCK_STREAM, 0);
            if (mSocketFd < 0) {
                std::cerr << "UdsWrapper::Initialize socket failed: " << std::strerror(errno) << std::endl;
                return false;
            }
        }

        return true;
    }

    void UdsWrapper::Close() {
        if (mSocketFd >= 0) {
            close(mSocketFd);
            mSocketFd = -1;
        }

        if (mMode == UdsMode::SERVER) {
            RemoveSocketFile();
        }
    }

    int UdsWrapper::AcceptClient() {
        if (mMode != UdsMode::SERVER || mSocketFd < 0) {
            std::cerr << "UdsWrapper::AcceptClient invalid state" << std::endl;
            return -1;
        }

        struct sockaddr_un clientAddr {};
        socklen_t clientLen = sizeof(clientAddr);
        int clientFd = -1;
        while (true) {
            clientFd = accept(mSocketFd, reinterpret_cast<struct sockaddr*>(&clientAddr), &clientLen);
            if (clientFd < 0) {
                if (errno == EINTR) {
                    continue;
                }
                std::cerr << "UdsWrapper::AcceptClient accept failed: " << std::strerror(errno) << std::endl;
                return -1;
            }
            break;
        }

        return clientFd;
    }

    bool UdsWrapper::Connect() {
        if (mMode != UdsMode::CLIENT || mSocketFd < 0) {
            std::cerr << "UdsWrapper::Connect invalid state" << std::endl;
            return false;
        }

        struct sockaddr_un addr {};
        addr.sun_family = AF_UNIX;
        if (mSocketPath.size() >= sizeof(addr.sun_path)) {
            std::cerr << "UdsWrapper::Connect socket path too long: " << mSocketPath << std::endl;
            return false;
        }
        std::strncpy(addr.sun_path, mSocketPath.c_str(), sizeof(addr.sun_path) - 1);

        if (connect(mSocketFd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
            std::cerr << "UdsWrapper::Connect connect failed: " << std::strerror(errno) << std::endl;
            return false;
        }

        return true;
    }

    ssize_t UdsWrapper::Send(int fd, const std::string &data) {
        ssize_t totalSent = 0;
        size_t remaining = data.size();
        const char* ptr = data.data();

        while (remaining > 0) {
            ssize_t sent = write(fd, ptr, remaining);
            if (sent < 0) {
                if (errno == EINTR) {
                    continue;
                }
                std::cerr << "UdsWrapper::Send write failed: " << std::strerror(errno) << std::endl;
                return -1;
            }
            totalSent += sent;
            ptr += static_cast<size_t>(sent);
            remaining -= static_cast<size_t>(sent);
        }

        return totalSent;
    }

    ssize_t UdsWrapper::Receive(int fd, char* buf, size_t bufSize) {
        while (true) {
            ssize_t bytesRead = read(fd, buf, bufSize);
            if (bytesRead < 0) {
                if (errno == EINTR) {
                    continue;
                }
                std::cerr << "UdsWrapper::Receive read failed: " << std::strerror(errno) << std::endl;
                return -1;
            }
            return bytesRead;
        }
    }

    bool UdsWrapper::SetNonBlocking(int fd) {
        int flags = fcntl(fd, F_GETFL, 0);
        if (flags < 0) {
            std::cerr << "UdsWrapper::SetNonBlocking fcntl F_GETFL failed: " << std::strerror(errno) << std::endl;
            return false;
        }

        if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
            std::cerr << "UdsWrapper::SetNonBlocking fcntl F_SETFL failed: " << std::strerror(errno) << std::endl;
            return false;
        }

        return true;
    }

    const std::string &UdsWrapper::GetSocketPath() const {
        return mSocketPath;
    }

    int UdsWrapper::GetSocketFd() const {
        return mSocketFd;
    }

    bool UdsWrapper::PrepareSocketPath() {
        struct sockaddr_un addr {};
        if (mSocketPath.size() >= sizeof(addr.sun_path)) {
            std::cerr << "UdsWrapper::PrepareSocketPath socket path too long: " << mSocketPath << std::endl;
            return false;
        }

        RemoveSocketFile();

        return true;
    }

    void UdsWrapper::RemoveSocketFile() {
        if (!mSocketPath.empty()) {
            unlink(mSocketPath.c_str());
        }
    }

}  // namespace qifeng::scm

// NOLINTEND(cppcoreguidelines-pro-type-reinterpret-cast,cppcoreguidelines-pro-type-vararg)
