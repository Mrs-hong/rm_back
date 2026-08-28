//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <qifeng_ca/common.pb.h>
#include <qifeng_ca/meeting.pb.h>
#include <string>
#include <thread>
#include <vector>

#include "workflow/WFTaskFactory.h"
#include <WFTask.h>

#include "core/meeting/recording_service.h"
#include "json/reader.h"
#include "json/value.h"
#include "json/writer.h"
#include "qifeng_framework/common/logger.h"
#include "qifeng_framework/common/scope_exit.h"

#include "common/atomic_write_file.h"
#include "common/config/upgrade_config.h"
#include "common/service_readiness.h"
#include "common/utils/verify.h"
#include "common/ws/system_message_notifier.h"
#include "core/upgrade/upgrade_detector_base.h"
#include "core/upgrade/upgrade_package_deployer.h"
#include "core/upgrade/upgrade_service.h"
#include "internal/display_manager.h"
#include "internal/hal/hal_bridge.h"
#include "internal/recording_manager.h"

namespace qifeng_ca {

    // qf_scmc 托管升级命令: 负责停掉qifeng_ca、升级、拉起qifeng_ca、写入升级结果
    static constexpr const char* UpgradeCommand = "qf_scmc upgrades -n qifeng_ca";

    // 升级互斥标记定义: 跨实例共享, 保证同一时间只有一个 Upgrade() 执行
    std::atomic<bool> UpgradeService::sUpgrading {false};

    // 收集目录下所有固件文件路径(非递归, 仅普通文件), 供屏幕升级使用
    static std::vector<std::string> CollectFirmwareFiles(const std::string &dirPath) {
        std::vector<std::string> files;
        std::error_code ec;
        for (const auto &entry : std::filesystem::directory_iterator(dirPath, ec)) {
            if (ec) {
                break;
            }
            if (entry.is_regular_file(ec)) {
                files.push_back(entry.path().string());
            }
        }
        return files;
    }

    // best-effort 调用 qf_scmc reset_nginx 切换 web 页面状态(升级中 -w / 正常 -n)
    // 失败仅告警, 不影响升级主流程; desc 用于日志区分调用场景
    static void RunResetNginx(const char* arg, const char* desc) {
        std::string cmd = std::string("qf_scmc reset_nginx ") + arg + " > /dev/null 2>&1";
        int ret = std::system(cmd.c_str());
        if (ret != 0) {
            SLOG_WARN << "RunResetNginx: " << desc << " failed, arg=" << arg << ", ret=" << ret;
        } else {
            SLOG_INFO << "RunResetNginx: " << desc << " ok, arg=" << arg;
        }
    }

    // ===================== .version 版本管理辅助 =====================
    // 控制版本号 = 主版本 + 子版本(如 2.11.1.u0001):
    //   主版本: 升级包 metadata.json 的 version 字段
    //   子版本: 基于该主版本完成升级的次数(U_VERSION 字段, 4 位数字, 初始值 0000)

    // 子版本号格式化为 4 位数字字符串(0000-9999, 负数归零, 溢出回绕)
    static std::string FormatSubVersion(long sub) {
        if (sub < 0) {
            sub = 0;
        }
        sub %= 10000;
        std::array<char, 8> buf {};
        std::snprintf(buf.data(), buf.size(), "%04ld", sub);
        return std::string(buf.data());
    }

    // 从 metadata 提取各组件版本, 返回 (.version 字段名 → 组件版本) 列表
    // 仅包含升级包内存在的组件, 未包含的组件不升级、不校验、不写回
    static std::vector<std::pair<std::string, std::string>> ExtractComponentVersions(
        const PackageMetadata &metadata) {
        std::vector<std::pair<std::string, std::string>> comps;
        if (metadata.screen) {
            comps.emplace_back(UpgradeConfig::kFieldScreenVersion, metadata.screen->version);
        }
        if (metadata.qifengCa) {
            comps.emplace_back(UpgradeConfig::kFieldCaVersion, metadata.qifengCa->version);
        }
        if (metadata.qifengScm) {
            comps.emplace_back(UpgradeConfig::kFieldScmVersion, metadata.qifengScm->version);
        }
        if (metadata.model) {
            comps.emplace_back(UpgradeConfig::kFieldModelVersion, metadata.model->version);
        }
        if (metadata.nginx) {
            comps.emplace_back(UpgradeConfig::kFieldNginxVersion, metadata.nginx->version);
        }
        return comps;
    }

    // 版本门槛校验: 升级包主版本与各组件版本均须 >= data/.version 记录值
    // 允许相等(同主版本重升级场景, 由子版本号区分); 校验不通过返回错误描述, 通过返回空串
    static std::string CheckUpgradeVersionGate(const PackageMetadata &metadata) {
        const auto &cfg = UpgradeConfig::GetInstance();

        // 1) 主版本必须存在且不低于当前记录值
        std::string currentMain = cfg.GetCurrentVersion();
        if (metadata.version.empty()) {
            return "升级包 metadata.json 缺少 version 字段, 拒绝升级";
        }
        if (CompareVersion(metadata.version, currentMain) < 0) {
            return "升级包版本 " + metadata.version + " 低于当前版本 " + currentMain + ", 拒绝升级";
        }

        // 2) 各组件版本不低于 .version 记录值(仅校验升级包内包含的组件)
        for (const auto &comp : ExtractComponentVersions(metadata)) {
            std::string currentComp = cfg.GetComponentVersion(comp.first);
            if (CompareVersion(comp.second, currentComp) < 0) {
                return "组件 " + comp.first + " 版本 " + comp.second + " 低于当前版本 " + currentComp + ", 拒绝升级";
            }
        }
        return "";
    }

    // 升级成功后将版本写回 data/.version(单次原子写入):
    //   VERSION=主版本, U_VERSION=子版本, 各 V_X=组件版本(仅写回包内存在的组件)
    // 子版本规则: 主版本与当前一致(同主版本重升级) → 当前子版本+1; 主版本更新 → 重置为 0001
    // 注意: 须在 .version 仍为旧值时调用(两阶段提交), 子版本计算依赖旧主版本
    static bool CommitUpgradeVersions(const std::string &mainVersion,
                                      const std::vector<std::pair<std::string, std::string>> &componentVersions) {
        const auto &cfg = UpgradeConfig::GetInstance();

        // 计算子版本号
        std::string nextSub;
        if (CompareVersion(mainVersion, cfg.GetCurrentVersion()) == 0) {
            // 同主版本重升级: 子版本号在当前基础上 +1
            long sub = 0;
            try {
                sub = std::stol(cfg.GetSubVersion());
            } catch (...) {
                sub = 0;  // 子版本号异常数据时从 0 重新计数
            }
            nextSub = FormatSubVersion(sub + 1);
        } else {
            // 新主版本: 本次升级计为该主版本的第 1 次升级
            nextSub = "0001";
        }

        // 汇总待写回字段: 主版本 + 子版本 + 各组件版本
        std::vector<std::pair<std::string, std::string>> fields;
        fields.emplace_back(UpgradeConfig::kFieldMainVersion, mainVersion);
        fields.emplace_back(UpgradeConfig::kFieldSubVersion, nextSub);
        fields.insert(fields.end(), componentVersions.begin(), componentVersions.end());

        if (!cfg.SetVersionFields(fields)) {
            SLOG_ERROR << "CommitUpgradeVersions: write .version failed, main=" << mainVersion
                      << ", sub=" << nextSub;
            return false;
        }
        SLOG_INFO << "CommitUpgradeVersions: .version updated, main=" << mainVersion << ", sub=" << nextSub;
        return true;
    }

    // 序列化待提交版本信息为 JSON(pending_version 文件内容):
    // {"version":"主版本","V_CA":"组件版本",...} 供重启后 CheckUpgradeResultAndSetComplete 写回 .version
    static std::string SerializePendingVersions(
        const std::string &mainVersion, const std::vector<std::pair<std::string, std::string>> &componentVersions) {
        Json::Value root;
        root["version"] = mainVersion;
        for (const auto &comp : componentVersions) {
            root[comp.first] = comp.second;
        }
        Json::StreamWriterBuilder builder;
        builder["indentation"] = "";
        return Json::writeString(builder, root);
    }

    // 解析 pending_version 内容: 新格式为 JSON(含主版本与各组件版本)
    // 兼容旧格式(纯版本号字符串, 仅主版本); 解析成功返回 true
    static bool ParsePendingVersions(const std::string &content, std::string &mainVersion,
                                     std::vector<std::pair<std::string, std::string>> &componentVersions) {
        // 去除首尾空白
        auto begin = content.find_first_not_of(" \t\r\n");
        if (begin == std::string::npos) {
            return false;
        }
        auto end = content.find_last_not_of(" \t\r\n");
        std::string trimmed = content.substr(begin, end - begin + 1);

        // 旧格式: 纯版本号字符串(非 '{' 开头)
        if (trimmed.front() != '{') {
            mainVersion = trimmed;
            componentVersions.clear();
            return true;
        }

        // 新格式: JSON 对象
        Json::Value root;
        Json::CharReaderBuilder readerBuilder;
        std::string errs;
        std::unique_ptr<Json::CharReader> reader(readerBuilder.newCharReader());
        if (!reader->parse(trimmed.c_str(), trimmed.c_str() + trimmed.size(), &root, &errs) || !root.isObject()) {
            SLOG_ERROR << "ParsePendingVersions: parse json failed: " << errs << ", content=" << trimmed;
            return false;
        }
        mainVersion = root.get("version", "").asString();
        if (mainVersion.empty()) {
            SLOG_ERROR << "ParsePendingVersions: version field empty, content=" << trimmed;
            return false;
        }
        componentVersions.clear();
        for (const char* field : {UpgradeConfig::kFieldScreenVersion, UpgradeConfig::kFieldCaVersion,
                                  UpgradeConfig::kFieldScmVersion, UpgradeConfig::kFieldModelVersion,
                                  UpgradeConfig::kFieldNginxVersion}) {
            if (root.isMember(field)) {
                componentVersions.emplace_back(field, root.get(field, "").asString());
            }
        }
        return true;
    }

    // ===================== 升级主流程(两阶段拆分) =====================

    // Phase 1: 验证 + 部署 + 识别组件, 成功后异步触发 ExecuteUpgrade
    // 验证部署完成后立即返回, 避免客户端长时间等待升级执行
    Status UpgradeService::PrepareUpgrade(const UpgradeRequest &req, UpgradeResponse* resp) {
        SLOG_INFO << "PrepareUpgrade: account=" << req.account_id() << ", package=" << req.package_path();

        // 0. 会议进行中检查: 升级会杀进程导致录音数据丢失, 会议中直接拒绝
        //    放在 sUpgrading 锁前快速失败, 避免占用锁、修改服务状态后又要回退
        if (RecordingManager::GetInstance().IsRecording()) {
            SLOG_WARN << "PrepareUpgrade: recording in progress, reject upgrade";
            resp->set_verified(false);
            resp->set_message("会议进行中, 请先停止会议");
            return Status {-1, "会议进行中, 请先停止会议"};
        }

        // 1. 并发互斥: 同一时间只允许一个升级流程(Web上传/OTA), 避免文件冲突和状态错乱
        bool expected = false;
        if (!sUpgrading.compare_exchange_strong(expected, true)) {
            SLOG_WARN << "PrepareUpgrade: another upgrade in progress, reject";
            resp->set_verified(false);
            resp->set_message("升级正在进行中, 请稍后再试");
            return Status {-1, "升级正在进行中"};
        }

        // 升级开始: 切换服务状态为 UpgradeInProgress
        ServiceReadiness::GetInstance().SetStatus(ServiceReadiness::ServiceStatus::UpgradeInProgress);

        // shouldRelease 控制退出时是否释放升级锁:
        //   true(默认): 任意错误路径 → 释放锁, 允许后续升级
        //   false: 验证部署成功 → 不释放锁(交由 ExecuteUpgrade 释放)
        bool shouldRelease = true;
        ScopeExit releaseGuard([&shouldRelease]() {
            if (shouldRelease) {
                UpgradeService::sUpgrading.store(false);
                ServiceReadiness::GetInstance().MarkReady();
            }
        });

        // 1. 校验软件包完整性(SHA256): 比对上传的软件包与sha256清单文件
        Status verifyStatus = Verify::VerifyUpgradePackage(req.package_path(), req.sha256_path());
        if (!verifyStatus.IsSuccess()) {
            SLOG_ERROR << "PrepareUpgrade: verify failed, " << verifyStatus.ToString();
            resp->set_verified(false);
            resp->set_message(verifyStatus.GetMsg());
            return verifyStatus;
        }

        SLOG_INFO << "PrepareUpgrade: verify ok, package=" << req.package_path();

        // 2. 取消推送WS升级通知(验证成功)

        // 3. 解压部署升级包: 委托 UpgradePackageDeployer 处理
        //    解压 → 解析 metadata.json → 按元数据移动组件到 data/src/upgrade
        std::string targetDir = UpgradeConfig::GetInstance().GetUpgradeSoftDir();
        std::string tmpUpgradeDir = std::filesystem::path(req.package_path()).parent_path().string();
        PackageMetadata metadata;
        Status deployStatus = UpgradePackageDeployer::Deploy(req.package_path(), targetDir, &metadata);

        // 4. 清理临时升级目录(无论部署成功失败)
        std::error_code ec;
        std::filesystem::remove_all(tmpUpgradeDir, ec);

        // 5. 部署失败则返回错误
        if (!deployStatus.IsSuccess()) {
            SLOG_ERROR << "PrepareUpgrade: deploy package failed, " << deployStatus.ToString();
            resp->set_verified(true);
            resp->set_message(deployStatus.GetMsg());
            return deployStatus;
        }

        // 5.5 版本门槛校验: 主版本及各组件版本均须 >= data/.version 记录值
        //     (允许相等, 支持同主版本重升级, 由子版本号区分)
        //     校验失败时部署目录中已落有组件文件, 需清理升级目录避免残留
        std::string gateError = CheckUpgradeVersionGate(metadata);
        if (!gateError.empty()) {
            SLOG_WARN << "PrepareUpgrade: version gate rejected, " << gateError;
            std::error_code gateEc;
            std::filesystem::remove_all(targetDir, gateEc);
            resp->set_verified(true);
            resp->set_message(gateError);
            return Status {-1, gateError};
        }

        // 6. 识别已部署组件: screen 目录、CA 组件、deb 文件(metadata.json + 实际目录校验)
        auto components = UpgradePackageDeployer::IdentifyComponents(targetDir, metadata);
        bool hasScreen = !components.screenDir.empty();
        bool hasCa = components.hasCa;
        bool hasDeb = !components.debFile.empty();
        SLOG_INFO << "PrepareUpgrade: hasScreen=" << hasScreen << ", hasCa=" << hasCa << ", hasDeb=" << hasDeb;

        // 既无屏幕也无CA也无deb → 升级包无有效内容
        if (!hasScreen && !hasCa && !hasDeb) {
            SLOG_ERROR << "PrepareUpgrade: no upgradable components found in " << targetDir;
            resp->set_verified(true);
            resp->set_message("升级包未包含可升级组件(screen/qifeng_ca/qifeng-ca/model/ngin/deb)");
            return Status {-1, "升级包未包含可升级组件"};
        }

        // === 验证部署完成, 异步触发升级执行 ===
        // sUpgrading 锁不释放(shouldRelease=false), 交由 ExecuteUpgrade 在完成时释放
        shouldRelease = false;

        // 构造组件描述返回给客户端
        std::string componentDesc;
        if (hasScreen)
            componentDesc += "屏幕 ";
        if (hasCa)
            componentDesc += "CA ";
        if (hasDeb)
            componentDesc += "SCM ";
        resp->set_verified(true);
        resp->set_message("升级包校验成功(" + componentDesc + "), 开始升级");
        SLOG_INFO << "PrepareUpgrade: prepare ok, async trigger execute upgrade";

        // 异步执行升级(不阻塞当前请求, 客户端已收到响应)
        uint64_t accountId = req.account_id();
        auto* execTask = WFTaskFactory::create_go_task("UpgradeExecTask", [metadata, components, accountId]() {
            UpgradeService svc;
            UpgradeResponse execResp;
            svc.ExecuteUpgrade(metadata, components, accountId, &execResp);
        });
        execTask->start();

        return {};
    }

    // Phase 2: 执行升级(由 PrepareUpgrade 异步调用, 也可单独调用)
    // 根据 metadata/components 执行屏幕/deb/CA 升级(单包可同时升级全部组件)
    Status UpgradeService::ExecuteUpgrade(const PackageMetadata &metadata,
                                          const UpgradePackageDeployer::DeployedComponents &components,
                                          uint64_t accountId, UpgradeResponse* resp) {
        bool hasScreen = !components.screenDir.empty();
        bool hasCa = components.hasCa;
        bool hasDeb = !components.debFile.empty();
        SLOG_INFO << "ExecuteUpgrade: hasScreen=" << hasScreen << ", hasCa=" << hasCa << ", hasDeb=" << hasDeb
                  << ", version=" << metadata.version;

        // shouldRelease: 仅屏幕升级成功/错误路径 → 释放锁; deb/CA 触发成功 → 不释放(进程即将被杀)
        bool shouldRelease = true;
        ScopeExit releaseGuard([&shouldRelease]() {
            if (shouldRelease) {
                UpgradeService::sUpgrading.store(false);
                ServiceReadiness::GetInstance().MarkReady();
            }
        });

        // ===== Step 1: web 页面切到「升级中」(best-effort, 与函数末尾/命令末尾的恢复形成闭环) =====
        RunResetNginx("-w", "set web upgrading at upgrade start");

        // ===== Step 2: 屏幕优先升级(若有 screen 组件, 不杀进程, 先于 deb/CA) =====
        if (hasScreen) {
            Status screenStatus = UpgradeScreen(components.screenDir);
            if (!screenStatus.IsSuccess()) {
                SLOG_ERROR << "ExecuteUpgrade: screen upgrade failed, " << screenStatus.ToString();
                SystemMessageNotifier::GetInstance().SendUpgradeNotice("upgrade_screen_failed");
                // 屏幕升级失败: best-effort 恢复 web, 避免 web 卡在升级中状态
                RunResetNginx("-n", "restore web on screen failure");
                resp->set_message(screenStatus.GetMsg());
                return screenStatus;
            }
        }

        // ===== 仅屏幕升级(无 deb 无 ca): 进程不会被杀, 屏幕升级已成功, 直接更新 .version 并恢复 web =====
        //   deb/CA 均会杀进程且失败会回退, 不能提前写 .version, 须走 pending_version 流程
        if (hasScreen && !hasDeb && !hasCa) {
            // 写回 .version: 主版本 + 子版本 + 各组件版本(屏幕升级不杀进程, 直接提交)
            if (!CommitUpgradeVersions(metadata.version, ExtractComponentVersions(metadata))) {
                SLOG_WARN << "ExecuteUpgrade: update .version failed after screen upgrade, version="
                          << metadata.version;
            } else {
                SLOG_INFO << "ExecuteUpgrade: screen-only upgrade done, .version updated to " << metadata.version;
            }
            // Step 4(屏幕场景): 与开始处的 reset_nginx -w 形成闭环, 恢复 web 为正常
            RunResetNginx("-n", "restore web after screen-only upgrade");
            resp->set_message("屏幕升级完成");
            return {};
        }

        // ===== Step 3: deb/CA 组合升级(二者均会杀进程, 合并为单个 shell 调用顺序执行) =====

        // 3a. 含 CA 时: 升级前准备(停止录音 + 屏幕切到「系统升级中」)
        //     仅 deb 不需要设置屏幕为升级中(按需求 3.1), 故仅在 hasCa 时调用 BeforeUpgradeCa
        if (hasCa) {
            Status beforeStatus = BeforeUpgradeCa(accountId);
            if (!beforeStatus.IsSuccess()) {
                SLOG_ERROR << "ExecuteUpgrade: before upgrade ca failed, " << beforeStatus.ToString();
                RunResetNginx("-n", "restore web on before-ca failure");
                resp->set_message(beforeStatus.GetMsg());
                return beforeStatus;
            }
        }

        // 3b. deb/CA 均杀进程, 先将目标版本信息(主版本 + 各组件版本, JSON 格式)写入 pending_version,
        //     下次启动 CheckUpgradeResultAndSetComplete 检测到升级成功后写回 .version
        std::string pendingPath = UpgradeConfig::GetInstance().GetPendingVersionPath();
        std::string pendingContent = SerializePendingVersions(metadata.version, ExtractComponentVersions(metadata));
        if (!AtomicFileWriter::WriteAtomic(pendingPath, pendingContent, 0644)) {
            SLOG_WARN << "ExecuteUpgrade: write pending version failed, continue upgrade anyway";
        }

        // 3c. 构建并触发组合升级命令(deb + CA 合并到一个 shell)
        //     - 仅 deb: dpkg -i <deb>; sleep 3; qf_scmc reset_nginx -n;  (命令末尾恢复 web)
        //     - 含 CA:  [dpkg -i <deb>; sleep 3;] qf_scmc upgrades -n qifeng_ca;  (web 由 qf_scmc 重启流程恢复)
        Status triggerStatus = TriggerProcessUpgrade(hasDeb, hasCa, components.debFile);
        if (!triggerStatus.IsSuccess()) {
            SLOG_ERROR << "ExecuteUpgrade: trigger process upgrade failed, " << triggerStatus.ToString();
            // 触发失败: 清理 pending_version 避免下次启动误更新 .version, 并恢复 web
            std::error_code rmEc;
            std::filesystem::remove(pendingPath, rmEc);
            RunResetNginx("-n", "restore web on trigger failure");
            resp->set_message(triggerStatus.GetMsg());
            return triggerStatus;
        }

        // ===== 升级已触发, qf_scmc 将停掉本服务并重启 =====
        // web 恢复: 含 CA 时由 qf_scmc 重启流程恢复; 仅 deb 时由命令末尾 reset_nginx -n 恢复
        resp->set_message("升级已触发, 服务即将重启");
        SLOG_INFO << "ExecuteUpgrade: upgrade triggered, service will restart";
        shouldRelease = false;  // deb/CA 升级会杀进程, 保持升级锁避免退出窗口期并发
        return {};
    }

    Status UpgradeService::UpgradeScreen(const std::string &packageDir) {
        SLOG_INFO << "UpgradeScreen: dir=" << packageDir;

        // 1. 收集目录下所有固件文件
        std::vector<std::string> files = CollectFirmwareFiles(packageDir);
        if (files.empty()) {
            SLOG_ERROR << "UpgradeScreen: no firmware files in " << packageDir;
            return Status {-1, "屏幕升级目录无固件文件: " + packageDir};
        }

        // 2. 调用HAL层固件升级接口
        int ret = HalBridge::GetInstance().UpgradeScreenFirmware(files);
        if (ret != 0) {
            std::string errMsg;
            if (ret == -1) {
                errMsg = "屏幕HAL未初始化";
            } else if (ret == -2) {
                errMsg = "屏幕升级正在进行中";
            } else {
                errMsg = "屏幕固件升级失败";
            }
            SLOG_ERROR << "UpgradeScreen: failed, ret=" << ret << ", msg=" << errMsg;
            return Status {ret, errMsg};
        }

        SLOG_INFO << "UpgradeScreen: ok, fileCount=" << files.size();
        return {};
    }

    Status UpgradeService::BeforeUpgradeCa(uint64_t accountId) {
        SLOG_INFO << "BeforeUpgradeCa: stop recording + show upgrade screen";

        // 1. 停止录音(若正在进行), 作为 TOCTOU 窗口的最后保护
        //    PrepareUpgrade 入口已拒绝会议中升级, 此处理论上无录音;
        //    但 PrepareUpgrade 检查通过后异步触发 ExecuteUpgrade 之间存在极短窗口,
        //    若用户新开启会议, 此处主动停止可避免 CA 升级杀进程导致录音数据丢失
        auto &recMgr = RecordingManager::GetInstance();
        if (recMgr.IsRecording()) {
            RecordStopRequest req;
            req.set_audio_id(recMgr.GetActiveAudioId());
            req.set_account_id(accountId);
            RecordingService recordingSvc;
            qifeng_ca::Empty resp;
            auto status = recordingSvc.RecordStop(req, &resp);
            if (!status.IsSuccess()) {
                SLOG_ERROR << "StopRecording: stop recording failed, " << status.ToString();
                return status;
            }
        }

        // 2. 屏幕切换到"系统升级中"状态
        DisplayManager::GetInstance().ShowUpgradeInProgress();

        return {};
    }

    Status UpgradeService::TriggerProcessUpgrade(bool hasDeb, bool hasCa, const std::string &debFile) {
        SLOG_INFO << "TriggerProcessUpgrade: hasDeb=" << hasDeb << ", hasCa=" << hasCa << ", deb=" << debFile;

        // 构建组合命令序列(deb 与 CA 均会杀进程, 合并到一个 shell 中顺序执行,
        // 靠 setsid + nohup 脱离当前进程组, qifeng_ca 被杀后存活 shell 仍能继续执行后续命令)
        std::string seq;
        if (hasDeb) {
            // 服务以 root 运行, 无需 sudo; 双引号包裹路径以兼容空格
            seq += "dpkg -i \"" + debFile + "\"; sleep 3; ";
        }
        if (hasCa) {
            // qf_scmc 托管升级: 停掉qifeng_ca、升级、拉起qifeng_ca、写入升级结果
            seq += std::string(UpgradeCommand) + "; ";
        } else if (hasDeb) {
            // 仅 deb(无 CA): 命令末尾恢复 web 页面(dpkg 完成后), 保持升级中状态覆盖整个 dpkg 过程
            seq += "qf_scmc reset_nginx -n; ";
        }
        // 含 CA 时不在命令中恢复 web: 由 qf_scmc 重启流程恢复(主进程即将被杀, 单独调用无法执行)

        // setsid + nohup 后台执行整个序列, 输出重定向到 /dev/null 避免阻塞
        std::string cmd = "setsid nohup sh -c '" + seq + "' > /dev/null 2>&1 &";
        SLOG_INFO << "TriggerProcessUpgrade: exec cmd: " << cmd;
        int ret = std::system(cmd.c_str());
        if (ret != 0) {
            SLOG_ERROR << "TriggerProcessUpgrade: exec failed, ret=" << ret;
            return Status {-1, "触发升级命令失败"};
        }

        SLOG_INFO << "TriggerProcessUpgrade: upgrade triggered, service will restart";
        return {};
    }

    Status UpgradeService::GetUpgradeResult(GetUpgradeResultResponse* resp) {
        std::string resultPath = UpgradeConfig::GetInstance().GetUpgradeResultPath();
        SLOG_INFO << "GetUpgradeResult: path=" << resultPath;

        // 读取升级结果文件(由 qf_scmc 写入)
        std::string content;
        Status readStatus = AtomicFileWriter::ReadFile(resultPath, content);
        if (!readStatus.IsSuccess()) {
            // 文件不存在, 返回默认值
            SLOG_INFO << "GetUpgradeResult: result file not exist, " << readStatus.ToString();
            resp->set_result("无升级记录");
            resp->set_reason("");
            resp->set_complete_time(0);
            return {};
        }

        // 解析 JSON
        Json::Value root;
        Json::CharReaderBuilder readerBuilder;
        std::string errs;
        std::unique_ptr<Json::CharReader> reader(readerBuilder.newCharReader());
        if (!reader->parse(content.c_str(), content.c_str() + content.size(), &root, &errs)) {
            SLOG_ERROR << "GetUpgradeResult: parse json failed: " << errs << ", content=" << content;
            resp->set_result("升级结果解析失败");
            resp->set_reason(content);
            resp->set_complete_time(0);
            return Status {-1, "升级结果文件格式错误"};
        }

        // 填充响应字段
        resp->set_result(root.get("result", "").asString());
        resp->set_reason(root.get("reason", "").asString());
        resp->set_complete_time(root.get("complete_time", 0).asInt64());

        SLOG_INFO << "GetUpgradeResult: result=" << resp->result() << ", complete_time=" << resp->complete_time();
        return {};
    }

    Status UpgradeService::CheckUpgradeResultAndSetComplete(const std::string &resultDir) {
        // resultDir为升级目录路径, 结果文件路径由UpgradeConfig统一管理
        std::string resultPath = UpgradeConfig::GetInstance().GetUpgradeResultPath();
        SLOG_INFO << "CheckUpgradeResultAndSetComplete: resultDir=" << resultDir << ", resultPath=" << resultPath;

        // 1. 读取升级结果文件(由 qf_scmc 在升级完成后写入)
        std::string content;
        Status readStatus = AtomicFileWriter::ReadFile(resultPath, content);
        if (!readStatus.IsSuccess()) {
            // 文件不存在, 说明无升级发生; 清理可能残留的 pending_version 后直接返回
            SLOG_INFO << "CheckUpgradeResultAndSetComplete: no result file, skip";
            std::string stalePending = UpgradeConfig::GetInstance().GetPendingVersionPath();
            std::error_code staleEc;
            if (std::filesystem::exists(stalePending, staleEc)) {
                std::filesystem::remove(stalePending, staleEc);
                SLOG_WARN << "CheckUpgradeResultAndSetComplete: removed stale pending_version, ec="
                          << staleEc.message();
            }
            return {};
        }

        // 2. 解析JSON, 判断升级结果(成功/失败)
        bool success = false;
        Json::Value root;
        Json::CharReaderBuilder readerBuilder;
        std::string errs;
        std::unique_ptr<Json::CharReader> reader(readerBuilder.newCharReader());
        if (reader->parse(content.c_str(), content.c_str() + content.size(), &root, &errs)) {
            bool upgradeSuccess = root.get("upgrade_success", false).asBool();
            std::string reason = root.get("defeat_reason", "").asString();

            success = upgradeSuccess;
            SLOG_INFO << "CheckUpgradeResultAndSetComplete: upgrade_success=" << std::to_string(upgradeSuccess)
                      << ", defeat_reason=" << reason;
        } else {
            SLOG_ERROR << "CheckUpgradeResultAndSetComplete: parse json failed: " << errs << ", content=" << content;
        }

        // 2a. 升级成功: 从 pending_version 读取目标版本信息(主版本 + 各组件版本), 写回 data/.version
        //     CA/SCM 升级前由 ExecuteUpgrade 写入 pending_version, 此处检测到成功后才提交,
        //     避免升级失败回退后 .version 已被提前修改的问题
        std::string pendingPath = UpgradeConfig::GetInstance().GetPendingVersionPath();
        if (success) {
            std::string pendingContent;
            if (AtomicFileWriter::ReadFile(pendingPath, pendingContent).IsSuccess() && !pendingContent.empty()) {
                std::string pendingMain;
                std::vector<std::pair<std::string, std::string>> pendingComponents;
                if (ParsePendingVersions(pendingContent, pendingMain, pendingComponents)) {
                    if (!CommitUpgradeVersions(pendingMain, pendingComponents)) {
                        SLOG_WARN << "CheckUpgradeResultAndSetComplete: update .version failed, version="
                                  << pendingMain;
                    } else {
                        SLOG_INFO << "CheckUpgradeResultAndSetComplete: .version updated to " << pendingMain;
                    }
                } else {
                    SLOG_WARN << "CheckUpgradeResultAndSetComplete: parse pending_version failed, skip update";
                }
            } else {
                SLOG_WARN << "CheckUpgradeResultAndSetComplete: upgrade success but no pending_version, skip update";
            }
        }

        // 3. 屏幕展示升级结果(此时HAL已初始化, 屏幕可用)
        DisplayManager::GetInstance().ShowUpgradeResult(success);

        // 4. 等待用户查看结果(3秒)
        std::this_thread::sleep_for(std::chrono::seconds(3));

        // 5. 切回待机页面, 恢复正常显示
        DisplayManager::GetInstance().ShowBootDisplay();

        // 6. 清除升级结果文件和 pending_version 文件(保证每次升级启动时只检查一次)
        std::error_code ec;
        std::filesystem::remove(resultPath, ec);
        if (ec) {
            SLOG_WARN << "CheckUpgradeResultAndSetComplete: remove result file failed: " << ec.message();
        }
        std::filesystem::remove(pendingPath, ec);
        if (ec) {
            SLOG_WARN << "CheckUpgradeResultAndSetComplete: remove pending_version failed: " << ec.message();
        }

        // 7. 清理升级包部署目录(soft_dir), 无论成功还是失败都清理
        //    避免残留旧组件文件影响下次升级, 下次 Deploy 时会自动重建目录
        std::string softDir = UpgradeConfig::GetInstance().GetUpgradeSoftDir();
        std::filesystem::remove_all(softDir, ec);
        if (ec) {
            SLOG_WARN << "CheckUpgradeResultAndSetComplete: clean soft_dir failed: " << ec.message();
        } else {
            SLOG_INFO << "CheckUpgradeResultAndSetComplete: cleaned soft_dir, dir=" << softDir;
        }

        SLOG_INFO << "CheckUpgradeResultAndSetComplete: done, success=" << std::to_string(success);
        return {};
    }

}  // namespace qifeng_ca
