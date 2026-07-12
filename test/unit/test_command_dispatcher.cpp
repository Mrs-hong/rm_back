/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "scmd/command_dispatcher.h"

#include "common/types.h"
#include "ipc/protocol.h"
#include "scmd/command_handler.h"
#include "scmd/handlers/kill_handler.h"
#include "scmd/handlers/list_handler.h"
#include "scmd/handlers/version_handler.h"
#include "service_manger/key_recoder.h"
#include "service_manger/service_context.h"

#include <gtest/gtest.h>

namespace qifeng::scm {

    // 用于测试的简单 Mock 处理器
    class MockHandler : public ICommandHandler {
    public:
        explicit MockHandler(ScmCommand cmd) : mCommand(cmd) {
        }

        ScmCommand GetCommand() const override {
            return mCommand;
        }

        ScmResponse Handle(const ScmRequest& /*request*/,
                           const ServiceContext& /*ctx*/,
                           KeyOperationRecorder& /*recorder*/) override {
            ScmResponse resp;
            resp.code = 0;
            resp.message = "handled";
            return resp;
        }

    private:
        ScmCommand mCommand;
    };

    TEST(CommandDispatcherTest, RegisterAndDispatch) {
        CommandDispatcher dispatcher;
        dispatcher.Register(std::make_unique<MockHandler>(ScmCommand::LIST));
        dispatcher.Register(std::make_unique<MockHandler>(ScmCommand::VERSION));

        ScmRequest listReq = MakeRequest(ListRequest{});
        ScmRequest versionReq = MakeRequest(VersionRequest{});

        // MockHandler 不使用 ctx，传入空 ServiceContext 即可
        ServiceContext ctx;
        KeyOperationRecorder recorder;

        ScmResponse listResp = dispatcher.Dispatch(listReq, ctx, recorder);
        EXPECT_EQ(listResp.code, 0);
        EXPECT_EQ(listResp.message, "handled");

        ScmResponse versionResp = dispatcher.Dispatch(versionReq, ctx, recorder);
        EXPECT_EQ(versionResp.code, 0);
        EXPECT_EQ(versionResp.message, "handled");
    }

    TEST(CommandDispatcherTest, UnknownCommand) {
        CommandDispatcher dispatcher;
        dispatcher.Register(std::make_unique<MockHandler>(ScmCommand::LIST));

        ScmRequest unknownReq = MakeRequest(KillRequest{});
        ServiceContext ctx;
        KeyOperationRecorder recorder;

        ScmResponse resp = dispatcher.Dispatch(unknownReq, ctx, recorder);
        EXPECT_EQ(resp.code, -1);
        EXPECT_EQ(resp.message, "Unknown command");
    }

    TEST(CommandDispatcherTest, DuplicateRegistrationIgnored) {
        CommandDispatcher dispatcher;
        dispatcher.Register(std::make_unique<MockHandler>(ScmCommand::LIST));
        dispatcher.Register(std::make_unique<MockHandler>(ScmCommand::LIST));

        ScmRequest req = MakeRequest(ListRequest{});
        ServiceContext ctx;
        KeyOperationRecorder recorder;

        ScmResponse resp = dispatcher.Dispatch(req, ctx, recorder);
        EXPECT_EQ(resp.code, 0);
        EXPECT_EQ(resp.message, "handled");
    }

}  // namespace qifeng::scm
