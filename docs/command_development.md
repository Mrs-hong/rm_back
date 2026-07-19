# 新增命令开发指南

本文档说明如何在 qifeng-scm 中新增一个完整的客户端命令 + 服务端处理器。系统采用"自注册"机制，新增命令只需在对应文件末尾添加一行注册代码，无需修改中央分发逻辑。

## 1. 整体流程

新增一个命令（例如 `backup`）的完整步骤：

1. 在协议层定义请求结构体与命令枚举；
2. 在客户端实现 `Command` 子类并注册；
3. 在服务端实现 `ICommandHandler` 子类并注册；
4. 编译验证。

## 2. 协议层（共享）

### 2.1 添加命令枚举

在 [include/ipc/data_def.h](../include/ipc/data_def.h) 的 `ScmCommand` 枚举中新增一项：

```cpp
enum class ScmCommand {
    // ... 已有命令
    BACKUP,  // 新增：备份服务数据
    // ...
};
```

### 2.2 添加请求结构体

在同一文件中定义请求参数结构体（字段需为可 JSON 序列化的基本类型）：

```cpp
struct BackupRequest {
    std::string serviceName;       // 必填：服务名称
    std::string backupDir;         // 可选：备份目标目录
};
```

### 2.3 声明字段元信息（用于自动序列化）

通过 `SCM_DEFINE_REQUEST` 宏声明字段元信息，`protocol.cpp` 中的模板会自动生成 `ParamsToJson`/`ParamsFromJson`，无需手写：

```cpp
// 字段顺序即 JSON 序列化顺序；第三个参数 true 表示必填
SCM_DEFINE_REQUEST(BackupRequest, ScmCommand::BACKUP,
    SCM_FIELD(serviceName, "service_name", true),
    SCM_FIELD(backupDir, "backup_dir", false))
```

> 若请求无字段（如 `VersionRequest`），使用 `SCM_DEFINE_REQUEST_EMPTY` 宏。

### 2.4 添加到 ScmRequest 变体

在 `ScmRequest` 的 `std::variant` 中追加 `BackupRequest`：

```cpp
using ScmRequestData = std::variant<
    // ... 已有请求类型
    BackupRequest,
    // ...
>;
```

## 3. 客户端实现

### 3.1 创建 Command 子类

在 [include/scmctl/cli_commands.h](../include/scmctl/cli_commands.h) 中声明：

```cpp
class BackupCommand : public CliCommand {
public:
    const char* Name() const override;
    const char* Description() const override;
    void Setup(CLI::App& app) override;
    ResultMsg BuildRequest(CLI::App& app, ScmRequest& req) override;

private:
    std::string mServiceName;
    std::string mBackupDir;
};
```

### 3.2 实现 Command 子类

在 [src/scmctl/cli_commands.cpp](../src/scmctl/cli_commands.cpp) 中实现，并在文件末尾注册：

```cpp
// -------------------- BackupCommand --------------------
const char* BackupCommand::Name() const {
    return "backup";
}
const char* BackupCommand::Description() const {
    return "备份服务数据";
}
void BackupCommand::Setup(CLI::App& app) {
    auto* cmd = app.add_subcommand(Name(), Description());
    cmd->add_option("--name,-n", mServiceName, "服务名称")->required();
    cmd->add_option("--dir,-d", mBackupDir, "备份目标目录（可选）");
}
ResultMsg BackupCommand::BuildRequest(CLI::App& app, ScmRequest& req) {
    if (!app.got_subcommand(Name())) {
        return ResultMsg(2, "");
    }
    // 将相对路径转为绝对路径
    std::string absDir = mBackupDir.empty() ? mBackupDir : std::filesystem::absolute(mBackupDir).string();
    req.data = BackupRequest{mServiceName, absDir};
    return MakeSuccess();
}

REGISTER_CLI_COMMAND(BackupCommand)  // 自注册：无需修改 cli_parser.cpp
```

## 4. 服务端实现

### 4.1 创建 Handler 子类

在 [include/scmd/handlers/backup_handler.h](../include/scmd/handlers/backup_handler.h) 中声明：

```cpp
#pragma once

#include "scmd/command_handler.h"

namespace qifeng::scm {

    /**
     * @brief BACKUP 命令处理器
     * @details 备份指定服务的数据目录到指定位置。
     */
    class BackupHandler : public ICommandHandler {
    public:
        explicit BackupHandler(const HandlerContext&) {}
        ScmCommand GetCommand() const override;
        ScmResponse Handle(const ScmRequest& request,
                           const ServiceContext& ctx,
                           KeyOperationRecorder& recorder) override;
    };

}  // namespace qifeng::scm
```

### 4.2 实现 Handler 子类

在 [src/scmd/handlers/backup_handler.cpp](../src/scmd/handlers/backup_handler.cpp) 中实现并注册：

```cpp
#include "scmd/handlers/backup_handler.h"
#include "scmd/handler_registry.h"

#include "common/types.h"
#include "ipc/data_def.h"
#include "qifeng_framework/common/logger.h"
#include "service_manager/key_recoder.h"
#include "service_manager/service_context.h"
#include "service_manager/service_manager.h"

namespace qifeng::scm {

    ScmCommand BackupHandler::GetCommand() const {
        return ScmCommand::BACKUP;
    }

    ScmResponse BackupHandler::Handle(const ScmRequest& request,
                                      const ServiceContext& ctx,
                                      KeyOperationRecorder& recorder) {
        const auto* params = std::get_if<BackupRequest>(&request.data);
        if (params == nullptr) {
            ScmResponse response;
            response.code = 1;
            response.message = "Invalid backup request";
            return response;
        }

        ScmResponse response;
        // 记录关键操作（result=2 表示进行中），用于异常恢复
        recorder.RecordOperation({"backup", params->serviceName, 2, "", ""});

        // 调用领域管理器执行业务（此处假设 ServiceManager 提供 BackupServiceData 方法）
        auto result = ctx.serviceManager->BackupServiceData(params->serviceName, params->backupDir);
        response.code = result.code;
        response.message = result.msg;

        // 更新操作结果：0=成功，1=失败
        recorder.UpdateResult(result.IsDefaultSuccess() ? 0 : 1);
        if (result.IsDefaultSuccess()) {
            recorder.Clear();
        }
        return response;
    }

    REGISTER_COMMAND_HANDLER(ScmCommand::BACKUP, BackupHandler)  // 自注册

}  // namespace qifeng::scm
```

## 5. 编译验证

```bash
# 编译
./build.sh -r

# 验证命令已注册
./build/bin/qf_scmc backup --help

# 执行命令
./build/bin/qf_scmc backup -n mariadb -d /tmp/mariadb_backup
```

## 6. 关键约定

### 6.1 命名规范

- 命令名：小写单词，多个单词用下划线连接（如 `add_model`、`clear_model`）
- 类名：CamelCase，以 `Command` 或 `Handler` 结尾（如 `BackupCommand`、`BackupHandler`）
- 文件名：snake_case，与命令名一致（如 `backup_command.cpp`、`backup_handler.cpp`）

### 6.2 关键操作记录

涉及服务状态变更的命令（install/uninstall/start/stop/upgrade 等）应通过 `KeyOperationRecorder` 记录：

1. 操作前调用 `recorder.RecordOperation({opName, serviceName, 2, "", ""})`（result=2 表示进行中）；
2. 操作后调用 `recorder.UpdateResult(success ? 0 : 1)`；
3. 成功后调用 `recorder.Clear()` 清除记录；
4. 实现 `Recover` 方法用于断点恢复（异常终止后 scmd 重启时自动调用）。

### 6.3 ServiceContext 访问

通过 `ServiceContext` 直接访问各领域管理器，无需经过 `ServiceControl` 门面：

```cpp
ctx.serviceManager->...   // 服务生命周期管理
ctx.fileManager->...      // 文件目录管理
ctx.dbusManager->...      // systemd DBus 接口
ctx.databaseService->...  // 数据库服务
ctx.nginxManager->...     // Nginx 配置管理
ctx.modelManager->...     // 模型文件管理
ctx.upgradeService->...   // 升级编排
```

## 7. 参考实现

已有命令的完整实现可作为参考：

| 命令类型 | 参考文件 |
| --- | --- |
| 简单查询（无副作用） | [src/scmd/handlers/version_handler.cpp](../src/scmd/handlers/version_handler.cpp) |
| 服务操作（带记录与恢复） | [src/scmd/handlers/start_handler.cpp](../src/scmd/handlers/start_handler.cpp) |
| 复杂参数（路径转换） | [src/scmd/handlers/upgrade_handler.cpp](../src/scmd/handlers/upgrade_handler.cpp) |
| 操作 scmd 自身 | [src/scmd/handlers/kill_handler.cpp](../src/scmd/handlers/kill_handler.cpp) |
| 一体化升级 | [src/scmd/handlers/upgrades_handler.cpp](../src/scmd/handlers/upgrades_handler.cpp) |

---

> 如需了解项目整体架构，请参阅 [README.md](../README.md)。