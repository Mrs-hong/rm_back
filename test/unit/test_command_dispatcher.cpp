/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "scmd/command_dispatcher.h"

#include "common/types.h"
#include "ipc/data_def.h"
#include "scmd/command_handler.h"
#include "service_manager/key_recoder.h"

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
                           ServiceControl& /*serviceControl*/,
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

        ScmRequest listReq{ListRequest{}};
        ScmRequest versionReq{VersionRequest{}};

        // 由于 MockHandler 不依赖 ServiceControl，可以传入 nullptr 并确保不会解引用
        ServiceControl* serviceControl = nullptr;
        KeyOperationRecorder recorder;

        ScmResponse listResp = dispatcher.Dispatch(listReq, *serviceControl, recorder);
        EXPECT_EQ(listResp.code, 0);
        EXPECT_EQ(listResp.message, "handled");

        ScmResponse versionResp = dispatcher.Dispatch(versionReq, *serviceControl, recorder);
        EXPECT_EQ(versionResp.code, 0);
        EXPECT_EQ(versionResp.message, "handled");
    }

    TEST(CommandDispatcherTest, UnknownCommand) {
        CommandDispatcher dispatcher;
        dispatcher.Register(std::make_unique<MockHandler>(ScmCommand::LIST));

        ScmRequest unknownReq{KillRequest{}};
        ServiceControl* serviceControl = nullptr;
        KeyOperationRecorder recorder;

        ScmResponse resp = dispatcher.Dispatch(unknownReq, *serviceControl, recorder);
        EXPECT_EQ(resp.code, -1);
        EXPECT_EQ(resp.message, "Unknown command");
    }

    TEST(CommandDispatcherTest, DuplicateRegistrationIgnored) {
        CommandDispatcher dispatcher;
        dispatcher.Register(std::make_unique<MockHandler>(ScmCommand::LIST));
        dispatcher.Register(std::make_unique<MockHandler>(ScmCommand::LIST));

        ScmRequest req{ListRequest{}};
        ServiceControl* serviceControl = nullptr;
        KeyOperationRecorder recorder;

        ScmResponse resp = dispatcher.Dispatch(req, *serviceControl, recorder);
        EXPECT_EQ(resp.code, 0);
        EXPECT_EQ(resp.message, "handled");
    }

}  // namespace qifeng::scm
