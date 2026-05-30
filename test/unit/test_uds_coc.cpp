/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

/**
 * @brief 基于gtest测试uds以及基于uds的通信测试（创建两个进程模拟scmd和scmctl）
 */

#include "ipc/protocol.h"
#include "ipc/uds.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

using namespace qifeng::scm;

// ============================================================================
// ControlProtocol 协议编解码测试
// ============================================================================

TEST(ProtocolTest, EncodeDecodeRequest) {
    ScmRequest original;
    original.command = ScmCommand::START;
    original.serviceName = "test_service";
    original.version = "1.0.0";

    std::string encoded = ControlProtocol::EncodeRequest(original);
    EXPECT_FALSE(encoded.empty());
    EXPECT_EQ(encoded.back(), '\n');

    std::string jsonStr = encoded.substr(0, encoded.size() - 1);
    ScmRequest decoded;
    ASSERT_TRUE(ControlProtocol::DecodeRequest(jsonStr, decoded));

    EXPECT_EQ(decoded.command, ScmCommand::START);
    EXPECT_EQ(decoded.serviceName, "test_service");
    EXPECT_EQ(decoded.version, "1.0.0");
    EXPECT_TRUE(decoded.tarDir.empty());
    EXPECT_EQ(decoded.logLevel, 0);
    EXPECT_EQ(decoded.logCount, 0);
}

TEST(ProtocolTest, EncodeDecodeResponse) {
    ScmResponse original;
    original.code = 0;
    original.message = "success";
    original.data["status"] = "running";
    original.data["pid"] = 12345;

    std::string encoded = ControlProtocol::EncodeResponse(original);
    EXPECT_FALSE(encoded.empty());
    EXPECT_EQ(encoded.back(), '\n');

    std::string jsonStr = encoded.substr(0, encoded.size() - 1);
    ScmResponse decoded;
    ASSERT_TRUE(ControlProtocol::DecodeResponse(jsonStr, decoded));

    EXPECT_EQ(decoded.code, 0);
    EXPECT_EQ(decoded.message, "success");
    EXPECT_EQ(decoded.data["status"].asString(), "running");
    EXPECT_EQ(decoded.data["pid"].asInt(), 12345);
}

TEST(ProtocolTest, EncodeDecodeAllCommands) {
    ScmCommand commands[] = {
        ScmCommand::VERSION,   ScmCommand::INSTALL,     ScmCommand::START,
        ScmCommand::STOP,      ScmCommand::RESTART,      ScmCommand::RESTART_ALL,
        ScmCommand::UPGRADE,   ScmCommand::LIST,         ScmCommand::INFO,
        ScmCommand::LOG,
    };

    for (auto cmd : commands) {
        ScmRequest original;
        original.command = cmd;
        original.serviceName = "svc";

        std::string encoded = ControlProtocol::EncodeRequest(original);
        std::string jsonStr = encoded.substr(0, encoded.size() - 1);

        ScmRequest decoded;
        ASSERT_TRUE(ControlProtocol::DecodeRequest(jsonStr, decoded)) << "Failed for command: " << ScmCommandToString(cmd);
        EXPECT_EQ(decoded.command, cmd) << "Command mismatch for: " << ScmCommandToString(cmd);
        EXPECT_EQ(decoded.serviceName, "svc");
    }
}

TEST(ProtocolTest, DecodeInvalidJson) {
    ScmRequest req;
    EXPECT_FALSE(ControlProtocol::DecodeRequest("not valid json{{{", req));

    ScmResponse resp;
    EXPECT_FALSE(ControlProtocol::DecodeResponse("not valid json{{{", resp));
}

TEST(ProtocolTest, ExtractMessages) {
    std::string buffer = "{\"command\":1}\n{\"command\":2}\n";
    auto messages = ControlProtocol::ExtractMessages(buffer);

    ASSERT_EQ(messages.size(), 2u);
    EXPECT_EQ(messages[0], "{\"command\":1}");
    EXPECT_EQ(messages[1], "{\"command\":2}");
    EXPECT_TRUE(buffer.empty());
}

TEST(ProtocolTest, ExtractMessagesPartial) {
    std::string buffer = "{\"command\":1}\n{\"com";
    auto messages = ControlProtocol::ExtractMessages(buffer);

    ASSERT_EQ(messages.size(), 1u);
    EXPECT_EQ(messages[0], "{\"command\":1}");
    EXPECT_EQ(buffer, "{\"com");

    buffer += "mand\":2}\n";
    messages = ControlProtocol::ExtractMessages(buffer);
    ASSERT_EQ(messages.size(), 1u);
    EXPECT_EQ(messages[0], "{\"command\":2}");
    EXPECT_TRUE(buffer.empty());
}

TEST(ProtocolTest, ExtractMessagesEmpty) {
    std::string buffer;
    auto messages = ControlProtocol::ExtractMessages(buffer);
    EXPECT_TRUE(messages.empty());
}

TEST(ProtocolTest, ExtractMessagesNoNewline) {
    std::string buffer = "incomplete message";
    auto messages = ControlProtocol::ExtractMessages(buffer);
    EXPECT_TRUE(messages.empty());
    EXPECT_EQ(buffer, "incomplete message");
}

// ============================================================================
// ScmCommandToString 测试
// ============================================================================

TEST(ProtocolTest, ScmCommandToStringTest) {
    EXPECT_STREQ(ScmCommandToString(ScmCommand::VERSION), "VERSION");
    EXPECT_STREQ(ScmCommandToString(ScmCommand::INSTALL), "INSTALL");
    EXPECT_STREQ(ScmCommandToString(ScmCommand::START), "START");
    EXPECT_STREQ(ScmCommandToString(ScmCommand::STOP), "STOP");
    EXPECT_STREQ(ScmCommandToString(ScmCommand::RESTART), "RESTART");
    EXPECT_STREQ(ScmCommandToString(ScmCommand::RESTART_ALL), "RESTART_ALL");
    EXPECT_STREQ(ScmCommandToString(ScmCommand::UPGRADE), "UPGRADE");
    EXPECT_STREQ(ScmCommandToString(ScmCommand::LIST), "LIST");
    EXPECT_STREQ(ScmCommandToString(ScmCommand::INFO), "INFO");
    EXPECT_STREQ(ScmCommandToString(ScmCommand::LOG), "LOG");
}

// ============================================================================
// UdsWrapper 基础功能测试
// ============================================================================

TEST(UdsTest, ServerInitializeAndClose) {
    std::string sockPath = "/tmp/scmd_test_init.sock";
    unlink(sockPath.c_str());

    UdsWrapper server(UdsMode::SERVER, sockPath);
    EXPECT_TRUE(server.Initialize());
    EXPECT_GE(server.GetSocketFd(), 0);
    EXPECT_EQ(server.GetSocketPath(), sockPath);

    server.Close();
    EXPECT_EQ(server.GetSocketFd(), -1);
}

TEST(UdsTest, ClientConnectFail) {
    std::string sockPath = "/tmp/scmd_test_nonexist.sock";
    unlink(sockPath.c_str());

    UdsWrapper client(UdsMode::CLIENT, sockPath);
    EXPECT_TRUE(client.Initialize());
    EXPECT_FALSE(client.Connect());
}

TEST(UdsTest, ReInitializeProtection) {
    std::string sockPath = "/tmp/scmd_test_reinit.sock";
    unlink(sockPath.c_str());

    UdsWrapper server(UdsMode::SERVER, sockPath);
    EXPECT_TRUE(server.Initialize());
    int firstFd = server.GetSocketFd();
    EXPECT_GE(firstFd, 0);

    EXPECT_TRUE(server.Initialize());
    EXPECT_GE(server.GetSocketFd(), 0);

    server.Close();
}

// ============================================================================
// UDS 双进程通信测试（fork模拟scmd和scmctl）
// ============================================================================

class UdsIpcTest : public ::testing::Test {
protected:
    std::string mSockPath {"/tmp/scmd_test_ipc.sock"};

    void SetUp() override { unlink(mSockPath.c_str()); }

    void TearDown() override { unlink(mSockPath.c_str()); }
};

TEST_F(UdsIpcTest, RequestResponse) {
    pid_t pid = fork();
    ASSERT_GE(pid, 0);

    if (pid == 0) {
        // 子进程 = 服务端 (scmd)
        UdsWrapper server(UdsMode::SERVER, mSockPath);
        if (!server.Initialize()) {
            exit(1);
        }

        int clientFd = server.AcceptClient();
        if (clientFd < 0) {
            exit(2);
        }

        char buf[1024];
        std::string buffer;
        ssize_t n = server.Receive(clientFd, buf, sizeof(buf));
        if (n <= 0) {
            close(clientFd);
            exit(3);
        }
        buffer.append(buf, static_cast<size_t>(n));

        auto msgs = ControlProtocol::ExtractMessages(buffer);
        if (msgs.empty()) {
            close(clientFd);
            exit(4);
        }

        ScmRequest req;
        if (!ControlProtocol::DecodeRequest(msgs[0], req)) {
            close(clientFd);
            exit(5);
        }

        if (req.command != ScmCommand::START || req.serviceName != "test_service") {
            close(clientFd);
            exit(6);
        }

        ScmResponse resp;
        resp.code = 0;
        resp.message = "started";
        resp.data["pid"] = 1234;

        std::string respData = ControlProtocol::EncodeResponse(resp);
        server.Send(clientFd, respData);

        close(clientFd);
        exit(0);
    } else {
        // 父进程 = 客户端 (scmctl)
        usleep(100000);

        UdsWrapper client(UdsMode::CLIENT, mSockPath);
        ASSERT_TRUE(client.Initialize());
        ASSERT_TRUE(client.Connect());

        ScmRequest req;
        req.command = ScmCommand::START;
        req.serviceName = "test_service";

        std::string reqData = ControlProtocol::EncodeRequest(req);
        ssize_t sent = client.Send(client.GetSocketFd(), reqData);
        ASSERT_GT(sent, 0);

        char buf[1024];
        std::string buffer;
        ssize_t n = client.Receive(client.GetSocketFd(), buf, sizeof(buf));
        ASSERT_GT(n, 0);
        buffer.append(buf, static_cast<size_t>(n));

        auto msgs = ControlProtocol::ExtractMessages(buffer);
        ASSERT_FALSE(msgs.empty());

        ScmResponse resp;
        ASSERT_TRUE(ControlProtocol::DecodeResponse(msgs[0], resp));
        EXPECT_EQ(resp.code, 0);
        EXPECT_EQ(resp.message, "started");
        EXPECT_EQ(resp.data["pid"].asInt(), 1234);

        int status = 0;
        waitpid(pid, &status, 0);
        EXPECT_TRUE(WIFEXITED(status));
        EXPECT_EQ(WEXITSTATUS(status), 0);
    }
}

TEST_F(UdsIpcTest, MultipleCommands) {
    pid_t pid = fork();
    ASSERT_GE(pid, 0);

    if (pid == 0) {
        // 子进程 = 服务端 (scmd)
        UdsWrapper server(UdsMode::SERVER, mSockPath);
        if (!server.Initialize()) {
            exit(1);
        }

        int clientFd = server.AcceptClient();
        if (clientFd < 0) {
            exit(2);
        }

        char buf[1024];
        std::string buffer;
        ssize_t n = server.Receive(clientFd, buf, sizeof(buf));
        if (n <= 0) {
            close(clientFd);
            exit(3);
        }
        buffer.append(buf, static_cast<size_t>(n));

        auto msgs = ControlProtocol::ExtractMessages(buffer);

        std::string allResponses;
        for (auto& msg : msgs) {
            ScmRequest req;
            if (!ControlProtocol::DecodeRequest(msg, req)) {
                continue;
            }

            ScmResponse resp;
            resp.code = 0;
            resp.message = ScmCommandToString(req.command);
            allResponses += ControlProtocol::EncodeResponse(resp);
        }

        if (!allResponses.empty()) {
            server.Send(clientFd, allResponses);
        }

        close(clientFd);
        exit(0);
    } else {
        // 父进程 = 客户端 (scmctl)
        usleep(100000);

        UdsWrapper client(UdsMode::CLIENT, mSockPath);
        ASSERT_TRUE(client.Initialize());
        ASSERT_TRUE(client.Connect());

        ScmRequest req1;
        req1.command = ScmCommand::LIST;
        ScmRequest req2;
        req2.command = ScmCommand::VERSION;
        ScmRequest req3;
        req3.command = ScmCommand::INFO;
        req3.serviceName = "svc_a";

        std::string allRequests;
        allRequests += ControlProtocol::EncodeRequest(req1);
        allRequests += ControlProtocol::EncodeRequest(req2);
        allRequests += ControlProtocol::EncodeRequest(req3);

        ssize_t sent = client.Send(client.GetSocketFd(), allRequests);
        ASSERT_GT(sent, 0);

        char buf[1024];
        std::string buffer;
        ssize_t n = client.Receive(client.GetSocketFd(), buf, sizeof(buf));
        ASSERT_GT(n, 0);
        buffer.append(buf, static_cast<size_t>(n));

        auto msgs = ControlProtocol::ExtractMessages(buffer);
        ASSERT_EQ(msgs.size(), 3u);

        ScmResponse resp1, resp2, resp3;
        ASSERT_TRUE(ControlProtocol::DecodeResponse(msgs[0], resp1));
        EXPECT_EQ(resp1.message, "LIST");

        ASSERT_TRUE(ControlProtocol::DecodeResponse(msgs[1], resp2));
        EXPECT_EQ(resp2.message, "VERSION");

        ASSERT_TRUE(ControlProtocol::DecodeResponse(msgs[2], resp3));
        EXPECT_EQ(resp3.message, "INFO");

        int status = 0;
        waitpid(pid, &status, 0);
        EXPECT_TRUE(WIFEXITED(status));
        EXPECT_EQ(WEXITSTATUS(status), 0);
    }
}

TEST_F(UdsIpcTest, ErrorResponse) {
    pid_t pid = fork();
    ASSERT_GE(pid, 0);

    if (pid == 0) {
        // 子进程 = 服务端 (scmd)
        UdsWrapper server(UdsMode::SERVER, mSockPath);
        if (!server.Initialize()) {
            exit(1);
        }

        int clientFd = server.AcceptClient();
        if (clientFd < 0) {
            exit(2);
        }

        char buf[1024];
        std::string buffer;
        ssize_t n = server.Receive(clientFd, buf, sizeof(buf));
        if (n <= 0) {
            close(clientFd);
            exit(3);
        }
        buffer.append(buf, static_cast<size_t>(n));

        ScmResponse resp;
        resp.code = -1;
        resp.message = "Service not found";

        server.Send(clientFd, ControlProtocol::EncodeResponse(resp));

        close(clientFd);
        exit(0);
    } else {
        // 父进程 = 客户端 (scmctl)
        usleep(100000);

        UdsWrapper client(UdsMode::CLIENT, mSockPath);
        ASSERT_TRUE(client.Initialize());
        ASSERT_TRUE(client.Connect());

        ScmRequest req;
        req.command = ScmCommand::START;
        req.serviceName = "nonexistent";

        client.Send(client.GetSocketFd(), ControlProtocol::EncodeRequest(req));

        char buf[1024];
        std::string buffer;
        ssize_t n = client.Receive(client.GetSocketFd(), buf, sizeof(buf));
        ASSERT_GT(n, 0);
        buffer.append(buf, static_cast<size_t>(n));

        auto msgs = ControlProtocol::ExtractMessages(buffer);
        ASSERT_FALSE(msgs.empty());

        ScmResponse resp;
        ASSERT_TRUE(ControlProtocol::DecodeResponse(msgs[0], resp));
        EXPECT_EQ(resp.code, -1);
        EXPECT_EQ(resp.message, "Service not found");

        int status = 0;
        waitpid(pid, &status, 0);
        EXPECT_TRUE(WIFEXITED(status));
        EXPECT_EQ(WEXITSTATUS(status), 0);
    }
}
