# 路线图与未完成特性

本文档记录 qifeng-scm 尚未完全实现或计划中的特性，与稳定的核心功能（详见 README.md）区分开。

## 1. 操作超时配置

**当前状态**：`scmd.yaml` 中的 `opt_timeout_sec` 字段已被解析并存储于 `ConfigInfo::optTimeoutSec`，默认值 10 秒。

**待完善**：
- 部分服务操作（如安装、卸载、升级）尚未完全接入该超时配置；
- 计划在后续版本中统一应用到所有 `ServiceManager` 长时间操作上，超时后取消操作并返回错误。

**临时方案**：通过 systemd 单元自身的 `TimeoutStopSec` 控制服务停止超时。

## 2. 数据库初始化后端模块

**当前状态**：`src/service_manager/database_service.cpp` 已实现数据库服务的自动识别与配置注入；`src/service_tool/tool_mariadb.cpp` 提供 MariaDB 交互工具。

**待完善**：
- OpenGauss 后端的 SQL 执行路径尚未完整测试；
- 数据库版本查询与兼容性矩阵的边界情况处理。

## 3. 一体化升级的回滚机制

**当前状态**：`src/service_manager/integrated_upgrade.cpp` 已实现"服务+模型+Nginx"的一体化升级流程，升级前会自动备份到 `backup/` 目录。

**待完善**：
- 升级失败时的自动回滚尚未实现，当前需要人工从 `backup/` 目录恢复；
- 计划引入"升级事务"概念，任一步骤失败自动回滚到备份状态。

## 4. 自检（self-check）模块的硬件扩展

**当前状态**：`src/checker/checkers/` 下已有 13 个自检项（disk/fan/light/memory/microphone/network/pcba/tpu/display/fingerprint/model_inference 等），通过 `IChecker<ConfigT>` 模板基类统一管理。

**待完善**：
- 部分检测器（如 `display_checker`、`fingerprint_checker`）依赖特定硬件，在 x86 开发环境上会返回 `SKIPPED`；
- 计划新增 GPU/PCIe/USB 等检测项。

## 5. 客户端命令补全与历史记录

**当前状态**：客户端通过 `REGISTER_CLI_COMMAND` 宏自注册，已支持 18 个子命令（install/start/stop/restart/upgrade/upgrades/info/list/uninstall/kill/reload/reload_all/restart_all/version/log/slog/check/add_model/clear_model/init_nginx/reset_nginx）。

**待完善**：
- 缺少 bash/zsh 命令补全脚本；
- 缺少命令历史记录与上次失败操作查询接口（虽然服务端有 `KeyOperationRecorder`，但客户端未暴露查询入口）。

## 6. 多语言日志支持

**当前状态**：日志统一通过 `qifeng_framework` 的 `SLOG_*` 宏输出，使用英文。

**待完善**：暂无多语言计划，保持英文输出便于日志聚合工具处理。

---

> 如需了解已稳定的核心功能，请参阅 [README.md](../README.md)。
> 如需了解运维操作命令，请参阅 [operations.md](operations.md)。
> 如需了解如何新增命令，请参阅 [command_development.md](command_development.md)。