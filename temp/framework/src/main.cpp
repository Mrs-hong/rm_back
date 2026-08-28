/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include <fstream>
#include <grpc/grpc.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/support/status.h>
#include <iostream>
#include <thread>
#include <unistd.h>
#include <workflow/WFTaskFactory.h>
#include <workflow/Workflow.h>

#include "asr/model.h"
#include "common/config_manager.h"
#include "common/logger.h"
#include "common/resource_monitor.h"
#include "common/scope_exit.h"
#include "dao/db_pool.h"
#include "http/http.h"
#include "lms/metting.h"
#include "lms/model.h"
#include "openai/openai_chat.h"
#include "qifeng_rpc.grpc.pb.h"
#include "qifeng_rpc.pb.h"
#include "rpc/grpc_server_manager.h"

extern "C" {
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libavutil/opt.h"
#include "libavutil/samplefmt.h"
#include "libswresample/swresample.h"
}

// 测试 ConfigManager 的定时 demo（每5秒输出一次，死循环）

// [[maybe_unused]] static void TestConfigManagerDemo() {
//     auto& cfg = ConfigManager::GetInstance();
//     cfg.Initialize("test_service", "./config/config.yaml", 3000);
//     while (true) {
//         std::cout << "log.log_debug: " << cfg.GetInt("log", "log_debug", -1) << ", "
//                   << "mysql_server: " << cfg.GetString("database", "mysql_server", "") << ", "
//                   << "mysql_port: " << cfg.GetInt("database", "mysql_port", -1) << ", "
//                   << "mysql_db: " << cfg.GetString("database", "mysql_db", "") << ", "
//                   << "mysql_user: " << cfg.GetString("database", "mysql_user", "") << ", "
//                   << "mysql_password: " << cfg.GetString("database", "mysql_password", "") << ", "
//                   << "redis_server: " << cfg.GetString("database", "redis_server", "") << ", "
//                   << "redis_port: " << cfg.GetInt("database", "redis_port", -1) << ", "
//                   << "redis_retry_times: " << cfg.GetInt("database", "redis_retry_times", -1) << std::endl;
//         std::this_thread::sleep_for(std::chrono::seconds(5));
//     }
// }

// // Server端逻辑
// class GreeterServiceImpl final : public qifeng::proto_test::Greeter::CallbackService {
//     [[maybe_unused]] grpc::ServerUnaryReactor* SayHello(grpc::CallbackServerContext* context,
//                                                         const qifeng::proto_test::HelloRequest* request,
//                                                         qifeng::proto_test::HelloReply* reply) override {
//         std::string prefix("Hello ");
//         reply->set_message(prefix + request->name());
//         SLOG_INFO << "resp message: " << reply->message();

//         auto* reactor = context->DefaultReactor();
//         reactor->Finish(grpc::Status::OK);
//         return reactor;
//     }
// };

// // Client端逻辑
// class GreeterClient {
// public:
//     explicit GreeterClient(std::shared_ptr<grpc::Channel> channel)
//         : mStub(qifeng::proto_test::Greeter::NewStub(channel)) {
//     }

//     grpc::Status SayHello(const qifeng::proto_test::HelloRequest& request, qifeng::proto_test::HelloReply* reply) {
//         grpc::ClientContext context;
//         std::mutex mu;
//         std::condition_variable cv;
//         bool done = false;
//         grpc::Status status;

//         // rpc. async 是异步逻辑，其中对rsp的后续处理都需要在lambda中处理
//         mStub->async()->SayHello(&context, &request, reply, [&mu, &cv, &done, &status](grpc::Status s) {
//             status = std::move(s);
//             std::lock_guard<std::mutex> lock(mu);
//             done = true;
//             cv.notify_one();
//         });

//         // 同步使用样例
//         std::unique_lock<std::mutex> lock(mu);
//         while (!done) {
//             cv.wait(lock);
//         }
//         return status;
//     }

//     static void RunSayHello() {
//         qifeng::proto_test::HelloRequest request;
//         qifeng::proto_test::HelloReply response;
//         request.set_name("world");
//         // NOTE(yangfei): 这里后续应该添加服务发现
//         GreeterClient greeter(grpc::CreateChannel("127.0.0.1:50051", grpc::InsecureChannelCredentials()));
//         auto status = greeter.SayHello(request, &response);
//         if (status.ok()) {
//             std::cerr << "response: " << response.message();
//         } else {
//             std::cout << status.error_code() << ": " << status.error_message() << std::endl;
//         }
//     }

// private:
//     std::unique_ptr<qifeng::proto_test::Greeter::Stub> mStub;
// };

// [[maybe_unused]] static void TestOpenAIChat() {
//     auto& cfg = ConfigManager::GetInstance();
//     cfg.Initialize("test_service", "./config/config.yaml", 3000);
//     qifeng::OpenAIChat openAIChat("", cfg.GetString("openai", "api_base"));
//     // 构建聊天完成请求参数
//     qifeng::HttpRequest req;
//     req.mUrl = "chat/completions";
//     req.mMethod = qifeng::HttpMethod::POST;
//     // req.mStream = true;  // 启用流式响应
//     Json::Value messages;
//     Json::Value message1;
//     message1["role"] = "system";
//     message1["content"] = "你是一个乐于助人的助手";
//     messages.append(message1);
//     Json::Value message2;
//     message2["role"] = "user";
//     message2["content"] = "介绍一下你自己";
//     messages.append(message2);
//     // 发送聊天完成请求
//     Json::Value response = openAIChat.ChatCompletionsCreate(req, messages);
//     std::cout << "Chat Completion Response: " << response["choices"][0]["message"]["content"].asString() <<
//     std::endl;
// }
// [[maybe_unused]] static void TestOpenAIChatStream() {
//     auto& cfg = ConfigManager::GetInstance();
//     cfg.Initialize("test_service", "./config/config.yaml", 3000);

//     qifeng::OpenAIChat openAIChat("", cfg.GetString("openai", "api_base"));

//     // 构建请求参数
//     qifeng::HttpRequest req;
//     req.mUrl = "chat/completions";
//     req.mMethod = qifeng::HttpMethod::POST;

//     // 构建 messages
//     Json::Value messages;
//     Json::Value systemMsg;
//     systemMsg["role"] = "system";
//     systemMsg["content"] = "你是一个乐于助人的助手";
//     messages.append(systemMsg);

//     Json::Value userMsg;
//     userMsg["role"] = "user";
//     userMsg["content"] = "介绍一下你自己";
//     messages.append(userMsg);

//     std::cout << "=== Streaming Response ===" << std::endl;
//     openAIChat.ChatCompletionsCreateStream(req, messages);
//     std::cout << "\n=== Stream Ended ===" << std::endl;
// }
// [[maybe_unused]] static void TestDBPool() {
//     auto& cfg = ConfigManager::GetInstance();
//     cfg.Initialize("test_db_pool", "./config/config.yaml", 3000);
//     // 初始化连接池
//     if (!DBPool::GetInstance().Initialize()) {
//         SLOG_ERROR << "Failed to initialize database connection pool";
//     }
// }

int main(int argc, char* argv[]) {
    // 初始化Logger
    if (!Logger::GetInstance().Initialize("test_service")) {
        return 1;
    }
    // 关闭全局log(保证return后主动调用)
    ScopeExit([]() { spdlog::shutdown(); });
    // 使用函数式调用打印日志
    // FLOG_INFO("Hello World!");

    // 使用流式调用打印日志
    // SLOG_INFO << "Hello World from stream!";
    //     // rpc 测试
    //     std::shared_ptr<std::thread> thread;
    //     ScopeExit([&thread]() {
    //         if (thread && thread->joinable()) {
    //             thread->join();
    //         }
    //     });
    //     if (argc > 1 && std::string(argv[1]) == "client") {
    //         GreeterClient::RunSayHello();
    //     } else {
    //         // 启动gRPC 服务器管理器
    //         thread = std::make_shared<std::thread>([]() {
    //         qifeng::RunRpcServer<GreeterServiceImpl>("GreeterService");
    //         });
    //     }
    //     TestDBPool();
    //     TestOpenAIChat();
    //     TestOpenAIChatStream();
    //     ResourceMonitor monitor;
    //     monitor.Run();
    //     int time = 30;
    //     while (time--) {
    //         sleep(1);
    //         // 刷新日志缓冲区
    //         Logger::GetInstance().Flush();
    //     }

    //     auto echoTask = WFTaskFactory::create_go_task("echo", []() {
    //         std::cerr << "workflow: Hello World"
    //                   << "\n";
    //     });
    //     Workflow::start_series_work(echoTask, nullptr);

    //     // 测试 ConfigManager demo
    //     TestConfigManagerDemo();

    return 0;
}