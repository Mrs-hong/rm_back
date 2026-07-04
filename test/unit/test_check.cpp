/*
 * Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
 */

#include "checker/checker_runner.h"
#include "checker/checkers/disk_checker.h"
#include "checker/checkers/memory_checker.h"
#include "checker/core/checker.h"
#include "checker/core/registry.h"
#include "checker/core/runner.h"
#include "common/json_load.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <string>

using namespace qifeng::scm;

namespace {

    /**
     * @brief 测试用桩 checker：用于验证注册/启用策略/配置注入，不依赖硬件
     */
    struct FakeConfig {
        int threshold = 10;
        std::string label = "default";
    };

    class FakeChecker : public IChecker<FakeConfig> {
    public:
        static std::string ClassName() { return "fake"; }
        std::string Name() const override { return ClassName(); }
        enum Severity Severity() const override { return Severity::WARNING; }
        CheckResult Run() override {
            CheckResult r(Name());
            // 用 mConfig 的值体现"配置已注入"
            r.status = mConfig.threshold > 0 ? Status::PASS : Status::FAIL;
            r.message = mConfig.label;
            r.details.emplace_back("threshold", std::to_string(mConfig.threshold));
            return r;
        }

    private:
        void ParseConfig(const Json::Value &j, FakeConfig &cfg) override {
            cfg.threshold = j.isMember("threshold") && j["threshold"].isInt() ? j["threshold"].asInt()
                                                                              : cfg.threshold;
            cfg.label = j.isMember("label") && j["label"].isString() ? j["label"].asString() : cfg.label;
        }
    };

    /**
     * @brief 写临时文件并返回路径
     */
    std::string WriteTempFile(const std::string &name, const std::string &content) {
        auto path = std::filesystem::temp_directory_path() / name;
        std::ofstream ofs(path);
        ofs << content;
        ofs.close();
        return path.string();
    }

}  // namespace

// ============================================================================
// JsonLoad 单元测试
// ============================================================================

TEST(JsonLoadTest, LoadFromStringAndGetOr) {
    JsonLoad loader;
    ASSERT_TRUE(loader.LoadFromString(R"({"name":"disk","min_free_pct":8,"parallel":true,"ratio":0.5})"));
    EXPECT_TRUE(loader.IsValid());

    EXPECT_EQ(loader.GetOr<std::string>("name", ""), "disk");
    EXPECT_EQ(loader.GetOr<int>("min_free_pct", 0), 8);
    EXPECT_EQ(loader.GetOr<bool>("parallel", false), true);
    EXPECT_DOUBLE_EQ(loader.GetOr<double>("ratio", 0.0), 0.5);
}

TEST(JsonLoadTest, GetOrReturnsDefaultOnMissing) {
    JsonLoad loader;
    ASSERT_TRUE(loader.LoadFromString(R"({"a":1})"));
    EXPECT_EQ(loader.GetOr<int>("not_exist", 42), 42);
    EXPECT_EQ(loader.GetOr<std::string>("not_exist", "fallback"), "fallback");
}

TEST(JsonLoadTest, GetOrParentChild) {
    JsonLoad loader;
    ASSERT_TRUE(loader.LoadFromString(R"({"disk":{"min_free_pct":7,"mounts":["/"]}})"));
    EXPECT_EQ(loader.GetOr<int>("disk", "min_free_pct", 0), 7);
    EXPECT_EQ(loader.GetOr<int>("disk", "missing", 99), 99);
    EXPECT_EQ(loader.GetOr<int>("missing_parent", "x", 99), 99);
}

TEST(JsonLoadTest, HasAndGet) {
    JsonLoad loader;
    ASSERT_TRUE(loader.LoadFromString(R"({"disk":{}})"));
    EXPECT_TRUE(loader.Has("disk"));
    EXPECT_FALSE(loader.Has("memory"));
    EXPECT_TRUE(loader.Get("disk").isObject());
    EXPECT_TRUE(loader.Get("memory").isNull());
}

TEST(JsonLoadTest, InvalidJsonReturnsFalse) {
    JsonLoad loader;
    EXPECT_FALSE(loader.LoadFromString("{not json"));
    EXPECT_FALSE(loader.IsValid());
}

TEST(JsonLoadTest, LoadFromFile) {
    std::string path = WriteTempFile("json_load_test.json", R"({"k":"v","n":3})");
    JsonLoad loader;
    ASSERT_TRUE(loader.LoadFromFile(path));
    EXPECT_EQ(loader.GetOr<std::string>("k", ""), "v");
    EXPECT_EQ(loader.GetOr<int>("n", 0), 3);
    std::filesystem::remove(path);
}

TEST(JsonLoadTest, LoadFromFileMissing) {
    JsonLoad loader;
    EXPECT_FALSE(loader.LoadFromFile("/tmp/__definitely_not_exist__.json"));
    EXPECT_FALSE(loader.IsValid());
}

// ============================================================================
// CheckerRegistry 单元测试
// ============================================================================

TEST(CheckerRegistryTest, RegisteredNames) {
    CheckerRegistry reg;
    reg.Register<DiskChecker>();
    reg.Register<MemoryChecker>();
    auto names = reg.RegisteredNames();
    ASSERT_EQ(names.size(), 2u);
    EXPECT_EQ(names[0], "disk");
    EXPECT_EQ(names[1], "memory");
}

TEST(CheckerRegistryTest, BuildAllEnablePolicy) {
    // 仅含 disk 段，不含 memory 段
    JsonLoad loader;
    ASSERT_TRUE(loader.LoadFromString(R"({"disk":{"min_free_pct":5}})"));

    CheckerRegistry reg;
    reg.Register<DiskChecker>();
    reg.Register<MemoryChecker>();
    auto checkers = reg.BuildAll(loader.Root());

    // 启用策略：json 含 disk → 实例化；不含 memory → 跳过
    ASSERT_EQ(checkers.size(), 1u);
    EXPECT_EQ(checkers[0]->Name(), "disk");
}

TEST(CheckerRegistryTest, BuildAllBothEnabled) {
    JsonLoad loader;
    ASSERT_TRUE(loader.LoadFromString(R"({"disk":{},"memory":{}})"));
    CheckerRegistry reg;
    reg.Register<DiskChecker>();
    reg.Register<MemoryChecker>();
    auto checkers = reg.BuildAll(loader.Root());
    EXPECT_EQ(checkers.size(), 2u);
}

TEST(CheckerRegistryTest, BuildAllConfigInjectionWithFakeChecker) {
    JsonLoad loader;
    ASSERT_TRUE(loader.LoadFromString(R"({"fake":{"threshold":100,"label":"injected"}})"));
    CheckerRegistry reg;
    reg.Register<FakeChecker>();
    auto checkers = reg.BuildAll(loader.Root());
    ASSERT_EQ(checkers.size(), 1u);
    // 执行 Run 验证配置已注入
    auto r = checkers[0]->Run();
    EXPECT_EQ(r.status, Status::PASS);
    EXPECT_EQ(r.message, "injected");
    EXPECT_EQ(r.details[0].second, "100");
}

TEST(CheckerRegistryTest, BuildAllSkipsMissingSection) {
    // 注册 FakeChecker 但 json 不含 "fake" 段
    JsonLoad loader;
    ASSERT_TRUE(loader.LoadFromString(R"({"disk":{}})"));
    CheckerRegistry reg;
    reg.Register<FakeChecker>();
    auto checkers = reg.BuildAll(loader.Root());
    EXPECT_EQ(checkers.size(), 0u);
}

// ============================================================================
// DiskChecker / MemoryChecker 单元测试
// ============================================================================

TEST(DiskCheckerTest, NameAndSeverity) {
    DiskChecker c;
    EXPECT_EQ(c.Name(), "disk");
    EXPECT_EQ(c.Severity(), Severity::CRITICAL);
}

TEST(DiskCheckerTest, ParseConfig) {
    DiskChecker c;
    Json::Value j;
    j["min_free_pct"] = 15;
    j["mounts"][0] = "/";
    j["mounts"][1] = "/data";
    c.SetConfig(j);
    // severity 由 checker 自身声明（r.severity 字段由 Runner::RunOne 统一填充，
    // 直接调用 Run() 不经过 RunOne，故此处验证 c.Severity() 而非 r.severity）
    EXPECT_EQ(c.Severity(), Severity::CRITICAL);
    // Run 验证配置生效（root 分区在 CI 环境应可 statvfs）
    auto r = c.Run();
    EXPECT_EQ(r.item, "disk");
    // status 应为四种合法状态之一
    EXPECT_TRUE(r.status == Status::PASS || r.status == Status::FAIL || r.status == Status::SKIPPED ||
                r.status == Status::WARNING)
        << "unexpected status: " << StatusToString(r.status);
    EXPECT_GE(r.elapsed_ms, 0);
}

TEST(MemoryCheckerTest, NameAndSeverity) {
    MemoryChecker c;
    EXPECT_EQ(c.Name(), "memory");
    EXPECT_EQ(c.Severity(), Severity::CRITICAL);
}

TEST(MemoryCheckerTest, ParseConfigAndRun) {
    MemoryChecker c;
    Json::Value j;
    j["min_available_mb"] = 1;  // 极低阈值，确保 PASS
    c.SetConfig(j);
    // severity 由 checker 自身声明（r.severity 字段由 Runner::RunOne 统一填充，
    // 直接调用 Run() 不经过 RunOne，故此处验证 c.Severity() 而非 r.severity）
    EXPECT_EQ(c.Severity(), Severity::CRITICAL);
    auto r = c.Run();
    EXPECT_EQ(r.item, "memory");
    EXPECT_TRUE(r.status == Status::PASS || r.status == Status::FAIL)
        << "unexpected status: " << StatusToString(r.status);
}

// ============================================================================
// Runner 单元测试
// ============================================================================

TEST(RunnerTest, RunAllSequential) {
    CheckerRegistry reg;
    reg.Register<FakeChecker>();
    JsonLoad loader;
    ASSERT_TRUE(loader.LoadFromString(R"({"fake":{"threshold":1}})"));
    auto checkers = reg.BuildAll(loader.Root());
    ASSERT_FALSE(checkers.empty());

    Runner runner;
    auto results = runner.RunAll(checkers, 5, false);
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].item, "fake");
    EXPECT_EQ(results[0].severity, Severity::WARNING);
}

TEST(RunnerTest, RunAllParallel) {
    CheckerRegistry reg;
    reg.Register<FakeChecker>();
    JsonLoad loader;
    ASSERT_TRUE(loader.LoadFromString(R"({"fake":{"threshold":1}})"));
    auto checkers = reg.BuildAll(loader.Root());

    Runner runner;
    auto results = runner.RunAll(checkers, 5, true);
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].status, Status::PASS);
}

// ============================================================================
// CheckerRunner 端到端测试
// ============================================================================

TEST(CheckerRunnerTest, RunEndToEnd) {
    // 构造临时 selftest.json（含 disk + memory + 全局字段）
    std::string reportPath =
        (std::filesystem::temp_directory_path() / "selftest_report.json").string();
    std::string json = R"({"per_item_timeout_sec":5,"parallel":false,"report_path":")" + reportPath +
                       R"(","disk":{"min_free_pct":5,"mounts":["/"]},"memory":{"min_available_mb":1}})";
    std::string cfgPath = WriteTempFile("selftest_e2e.json", json);

    JsonLoad loader;
    ASSERT_TRUE(loader.LoadFromFile(cfgPath)) << "cfg path: " << cfgPath;

    auto report = CheckerRunner::Run(loader);

    // 至少 disk/memory 应出现在 details（两者 json 段都存在）
    EXPECT_TRUE(report.details.isMember("disk")) << "disk should be enabled";
    EXPECT_TRUE(report.details.isMember("memory")) << "memory should be enabled";
    EXPECT_TRUE(report.overallStatus == "OK" || report.overallStatus == "FAIL");
    EXPECT_FALSE(report.summary.empty());
    EXPECT_EQ(report.reportPath, reportPath);

    // 报告文件应已写入
    EXPECT_TRUE(std::filesystem::exists(reportPath)) << "report file should exist: " << reportPath;

    std::filesystem::remove(cfgPath);
    std::filesystem::remove(reportPath);
}

TEST(CheckerRunnerTest, RunWithMemoryOnly) {
    // 验证：json 仅含 memory 段时 disk 不启用
    std::string reportPath =
        (std::filesystem::temp_directory_path() / "selftest_report2.json").string();
    std::string json = R"({"report_path":")" + reportPath + R"(","memory":{"min_available_mb":1}})";
    std::string cfgPath = WriteTempFile("selftest_memory_only.json", json);

    JsonLoad loader;
    ASSERT_TRUE(loader.LoadFromFile(cfgPath));

    auto report = CheckerRunner::Run(loader);
    // json 仅含 memory → disk 不启用
    EXPECT_FALSE(report.details.isMember("disk"));
    EXPECT_TRUE(report.details.isMember("memory"));

    std::filesystem::remove(cfgPath);
    std::filesystem::remove(reportPath);
}
