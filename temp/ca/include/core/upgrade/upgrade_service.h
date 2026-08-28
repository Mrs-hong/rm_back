//
// Copyright (C) 2026-2026 Qifeng Shunshi Co., Ltd. All rights reserved.
//

#ifndef QIFENG_CA_INCLUDE_CORE_UPGRADE_UPGRADE_SERVICE_H
#define QIFENG_CA_INCLUDE_CORE_UPGRADE_UPGRADE_SERVICE_H

#include <atomic>
#include <cstdint>

#include "qifeng_ca/common.pb.h"
#include "qifeng_ca/upgrade.pb.h"

#include "common/status.h"
#include "core/upgrade/package_metadata.h"
#include "core/upgrade/upgrade_package_deployer.h"

namespace qifeng_ca {

    // 服务升级业务层
    // 升级流程拆分为两阶段:
    //   1. PrepareUpgrade: SHA256 校验 → 解压部署 → 识别组件 → 返回结果给客户端 → 异步触发 ExecuteUpgrade
    //   2. ExecuteUpgrade: 按组件类型执行屏幕/deb/CA 升级(后台异步执行, 客户端无需等待)
    //
    // 单包全组件升级: deb 与 CA 均会杀进程, 合并为单个 shell 调用(TriggerProcessUpgrade)顺序执行,
    //   靠 setsid + nohup 脱离进程组, qifeng_ca 被杀后存活 shell 仍能继续执行后续命令。
    //
    // 并发控制: 通过静态原子标记 sUpgrading 保证同一时间只有一个升级流程(同类串行 + 跨类串行)。
    //   PrepareUpgrade 获取锁, 成功后不释放(交由 ExecuteUpgrade 释放); 失败在 ScopeExit 中释放。
    //   ExecuteUpgrade: 仅屏幕升级成功/错误路径释放锁; deb/CA 升级触发后不释放(进程即将被杀, 随重启重置)。
    class UpgradeService {
    public:
        UpgradeService() = default;
        ~UpgradeService() = default;

        UpgradeService(const UpgradeService &) = delete;
        UpgradeService &operator=(const UpgradeService &) = delete;
        UpgradeService(UpgradeService &&) noexcept = delete;
        UpgradeService &operator=(UpgradeService &&) = delete;

        // Phase 1: 验证 + 部署 + 识别组件, 成功后异步触发 ExecuteUpgrade
        // 验证部署完成后立即返回, 避免客户端长时间等待升级执行
        // req: 已由前置处理填充的升级请求(含文件路径、sha256)
        // resp: 返回给客户端的响应(校验结果 + 组件信息)
        Status PrepareUpgrade(const UpgradeRequest &req, UpgradeResponse* resp);

        // Phase 2: 执行升级(由 PrepareUpgrade 异步调用, 也可单独调用)
        // 根据 metadata/components 执行屏幕/CA/SCM 升级
        // 注意: 调用方须确保 sUpgrading 已被 PrepareUpgrade 获取
        Status ExecuteUpgrade(const PackageMetadata &metadata,
                              const UpgradePackageDeployer::DeployedComponents &components,
                              uint64_t accountId, UpgradeResponse* resp);

        // 查询升级结果(无状态, 读取结果文件)
        // resp: 升级结果(成功/失败原因+完成时间)
        Status GetUpgradeResult(GetUpgradeResultResponse* resp);

        /**
         * @brief 检查升级结果文件是否存在, 并设置屏幕为升级完成、清除升级结果文件(保证每次升级启动时只检查一次)
         * @param resultDir 升级结果文件目录路径(即upgrade目录)
         * @return Status 校验结果
         */
        static Status CheckUpgradeResultAndSetComplete(const std::string &resultDir);

    private:
        // 执行屏幕升级
        /**
         * @brief 执行屏幕升级
         * @param packageDir 升级包路径(screenxxx目录)
         * @return Status 升级结果
         */
        Status UpgradeScreen(const std::string &packageDir);

        // 执行 deb/CA 组合升级(合并为单个 shell 调用)
        /**
         * @brief 构建并触发 deb/CA 组合升级命令
         * @details deb 与 CA 均会杀掉 qifeng_ca 进程, 必须合并为单个 shell 命令顺序执行,
         *          否则先执行的分支杀进程后后续分支无法触发。命令以 setsid + nohup 后台执行,
         *          脱离当前进程组, qifeng_ca 被杀后存活 shell 继续执行后续命令。
         *          - 仅 deb(无 CA): dpkg -i <deb>; sleep 3; qf_scmc reset_nginx -n;  (命令末尾恢复 web)
         *          - 含 CA:        [dpkg -i <deb>; sleep 3;] qf_scmc upgrades -n qifeng_ca;  (web 由 qf_scmc 重启流程恢复)
         * @param hasDeb 是否包含 deb 组件
         * @param hasCa 是否包含 CA 组件
         * @param debFile deb 文件完整路径(无 deb 时忽略)
         * @return Status 升级触发结果
         */
        Status TriggerProcessUpgrade(bool hasDeb, bool hasCa, const std::string &debFile);

        // CA升级前准备: 停止录音(TOCTOU最后保护) + 屏幕切换到升级中
        /**
         * @brief 在升级CA前停止录音(防止杀进程导致录音数据丢失)并将屏幕切换为升级状态
         * @note PrepareUpgrade 入口已拒绝会议中升级, 此处停止录音仅作为 PrepareUpgrade 检查后
         *       异步触发 ExecuteUpgrade 之间 TOCTOU 窗口的最后保护
         * @return Status 升级结果
         */
        Status BeforeUpgradeCa(uint64_t accountId);

        // 升级互斥标记(static): 跨实例共享, 保证同一时间只有一个升级流程执行
        // PrepareUpgrade 获取, ExecuteUpgrade 释放(屏幕成功/错误) 或 不释放(CA/SCM 随进程重启重置)
        static std::atomic<bool> sUpgrading;
    };

}  // namespace qifeng_ca

#endif  // QIFENG_CA_INCLUDE_CORE_UPGRADE_UPGRADE_SERVICE_H
