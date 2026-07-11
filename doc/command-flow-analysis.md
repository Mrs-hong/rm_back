# 命令流程分析报告

本文档从添加命令的完整流程出发，分析 qifeng-scm 项目的模块设计、耦合度与内聚问题，并提出优化建议。
本文档仅作分析，不涉及代码改动。

---

## 1. 完整命令处理流程

一条命令从用户输入到结果输出，经历以下 10 个阶段，横跨 CLI 客户端（`qf_scmc`）和服务端（`qf_scmd`）两个进程，通过 Unix Domain Socket + JSON 行协议通信。

### 流程总览

```
用户输入
  │
  ▼
┌───────────────────── CLI 客户端 (qf_scmc) ─────────────────────┐
│ 1. CLI 解析         CliPaser::Parse → CLI::App 解析子命令和参数  │
│ 2. ScmRequest 构建  CliCommand::BuildRequest 填充 ScmRequest.data│
│ 3. JSON 序列化      ScmRequest::ToJson → ControlProtocol::Encode │
│ 4. UDS 发送         ScmCtlClient::SendRequest                    │
└──────────────────────────────┬──────────────────────────────────┘
                               │ UDS
                               ▼
┌───────────────────── 服务端 (qf_scmd) ──────────────────────────┐
│ 5. UDS 接收         ScmServer::HandleClient → recv              │
│ 6. JSON 反序列化    ControlProtocol::DecodeRequest → ScmRequest  │
│ 7. 命令分发         CommandDispatcher::Dispatch                  │
│ 8. Handler 执行     ICommandHandler::Handle → ServiceControl    │
│ 9. 响应构建         ScmResponse → EncodeResponse → send          │
└──────────────────────────────┬──────────────────────────────────┘
                               │ UDS
                               ▼
┌───────────────────── CLI 客户端 (qf_scmc) ─────────────────────┐
│ 10. 输出格式化      ScmCtlClient::FormatOutput → stdout/stderr  │
└────────────────────────────────────────────────────────────────┘
```

### 各阶段详解

#### 阶段 1：CLI 解析

- **入口**：`main()` ([qifeng_scmctl.cpp](file:///home/hong/code/rm_back/src/scmctl/qifeng_scmctl.cpp))
- **解析器**：`CliPaser::Parse()` ([cli_paser.cpp](file:///home/hong/code/rm_back/src/scmctl/cli_paser.cpp))
- **机制**：
  1. 创建 `CLI::App`，设置版本标志 `--version/-v`
  2. **手工构造** 18 个 `CliCommand` 子类实例，放入 `std::vector`
  3. 遍历调用每个命令的 `Setup(app)`，向 `CLI::App` 注册子命令和参数
  4. `app.parse(argc, argv)` 解析命令行
- **关键类**：`CliCommand`（抽象基类，[cli_commands.h](file:///home/hong/code/rm_back/include/scmctl/cli_commands.h)），每个子命令独立负责参数注册

#### 阶段 2：ScmRequest 构建

- **机制**：解析成功后，遍历所有 `CliCommand`，调用 `BuildRequest(app, req)`
  - 返回 `code=0`：命中且组装成功
  - 返回 `code=-1`：命中但参数校验失败
  - 返回 `code=2`：未命中，继续下一个
- **数据结构**：`ScmRequest` ([data_def.h](file:///home/hong/code/rm_back/include/ipc/data_def.h))
  ```cpp
  struct ScmRequest {
      ScmRequestData data;  // std::variant<23种请求结构体>
      ScmCommand Command() const;  // 通过 if-constexpr 从 variant 推导枚举
  };
  ```
- **示例**：`InstallCommand::BuildRequest` 设置 `req.data = InstallRequest{serviceName, tarDir}`

#### 阶段 3：JSON 序列化

- **方法**：`ScmRequest::ToJson()` ([protocol.cpp](file:///home/hong/code/rm_back/src/ipc/protocol.cpp))
- **机制**：
  ```cpp
  Json::Value ScmRequest::ToJson() const {
      root["command"] = static_cast<int>(Command());  // 枚举→int
      root["params"] = std::visit([](auto& param) {
          return ParamsToJson(param);  // 23个重载，按variant类型分发
      }, data);
  }
  ```
- **编码**：`ControlProtocol::EncodeRequest` 将 JSON 字符串封装为行协议（长度前缀+JSON+换行）

#### 阶段 4：UDS 发送

- **类**：`ScmCtlClient` ([scmctl_client.cpp](file:///home/hong/code/rm_back/src/scmctl/scmctl_client.cpp))
- **流程**：创建 `UdsWrapper(CLIENT)` → `Initialize` → `Connect` → `Send(encoded)` → 循环 `Receive` → `ExtractMessages` 提取完整消息

#### 阶段 5：UDS 接收

- **方法**：`ScmServer::HandleClient` ([scmd_server.cpp](file:///home/hong/code/rm_back/src/scmd/scmd_server.cpp))
- **机制**：主事件循环 `Accept → HandleClient → close`，`HandleClient` 内循环 `recv` + `ExtractMessages`

#### 阶段 6：JSON 反序列化

- **方法**：`ControlProtocol::DecodeRequest` → `ScmRequest::FromJson` ([protocol.cpp](file:///home/hong/code/rm_back/src/ipc/protocol.cpp))
- **机制**：
  ```cpp
  static std::optional<ScmRequest> FromJson(const Json::Value& root) {
      int cmdValue = root["command"].asInt();
      auto cmd = static_cast<ScmCommand>(cmdValue);
      switch (cmd) {  // 23个case，每个调用对应的ParamsFromJson
          case ScmCommand::INSTALL: {
              InstallRequest param;
              ParamsFromJson(param, params);
              request.data = param;
              break;
          }
          // ...
      }
  }
  ```

#### 阶段 7：命令分发

- **类**：`CommandDispatcher` ([command_dispatcher.cpp](file:///home/hong/code/rm_back/src/scmd/command_dispatcher.cpp))
- **机制**：
  ```cpp
  ScmResponse Dispatch(const ScmRequest& request, ...) {
      ScmCommand cmd = request.Command();
      auto it = mHandlers.find(cmd);  // map<ScmCommand, unique_ptr<ICommandHandler>>
      return it->second->Handle(request, serviceControl, recorder);
  }
  ```
- **Handler 注册**：
  - `ScmServer::RegisterHandlers()` 构造 `HandlerContext`，调用 `mDispatcher.LoadFromRegistry(ctx)`
  - `HandlerRegistry::BuildAll(ctx)` 遍历所有已注册的工厂函数，构造 handler 实例
  - **自注册机制**：每个 handler.cpp 末尾的 `REGISTER_COMMAND_HANDLER` 宏 ([handler_registry.h](file:///home/hong/code/rm_back/include/scmd/handler_registry.h)) 在静态初始化期将工厂函数注册到 `HandlerRegistry` 单例

#### 阶段 8：Handler 执行

- **接口**：`ICommandHandler` ([command_handler.h](file:///home/hong/code/rm_back/include/scmd/command_handler.h))
  ```cpp
  class ICommandHandler {
      virtual ScmCommand GetCommand() const = 0;
      virtual ScmResponse Handle(const ScmRequest&, ServiceControl&, KeyOperationRecorder&) = 0;
      virtual ResultMsg Recover(const KeyOperationRecord&, ServiceControl&);  // 可选
  };
  ```
- **示例**：`InstallHandler::Handle` ([install_handler.cpp](file:///home/hong/code/rm_back/src/scmd/handlers/install_handler.cpp))
  1. `std::get_if<InstallRequest>(&request.data)` 提取参数
  2. `recorder.RecordOperation(...)` 记录关键操作
  3. `serviceControl.Installed(...)` 调用业务逻辑
  4. 组装 `ScmResponse` 返回
- **HandlerContext**：注入运行期依赖
  ```cpp
  struct HandlerContext {
      std::string selfTestConfigPath;         // CheckHandler 使用
      std::function<void()> shutdownCallback;  // KillHandler 使用
  };
  ```

#### 阶段 9：响应构建与返回

- **数据结构**：`ScmResponse` ([data_def.h](file:///home/hong/code/rm_back/include/ipc/data_def.h))
  ```cpp
  struct ScmResponse {
      int code;         // 0:成功, -1:失败, 1:警告
      std::string message;
      Json::Value data;  // 灵活的数据载荷
  };
  ```
- **流程**：`HandleClient` 中 `EncodeResponse` → `send(clientFd, ...)`

#### 阶段 10：输出格式化

- **方法**：`ScmCtlClient::FormatOutput` ([scmctl_client.cpp](file:///home/hong/code/rm_back/src/scmctl/scmctl_client.cpp))
- **机制**：
  - `code=0`：`[OK]` 前缀 + message + data 格式化
  - `code=1`：`[WARN]` 前缀 + message + data 格式化
  - `code=-1`：`[FAIL]` 前缀 + message
  - `data` 为 Object：键值对格式
  - `data` 为 Array：表格格式（list 命令）
  - SLOG 命令特例：直接输出 message 原文

---

## 2. 添加新命令的步骤清单

经过 P0/P1/P4 优化后，添加一个新命令需要修改 **7 处**，横跨 6 个文件（原步骤5已消除）：

| 步骤 | 文件 | 修改内容 | 必要性 |
|------|------|----------|--------|
| 1 | [include/ipc/data_def.h](file:///home/hong/code/rm_back/include/ipc/data_def.h) | `ScmCommand` 枚举新增值 + 请求结构体 + `ScmRequestData` variant 新增类型 + `RequestCommand<T>` trait 特化 | 必需 |
| 2 | [src/ipc/protocol.cpp](file:///home/hong/code/rm_back/src/ipc/protocol.cpp) | `kCommandNameMap` 新增映射（CTAD 自动计算大小）+ `ParamsToJson` 重载 + `ParamsFromJson` 重载 + `FromJson` switch 新增 case | 必需 |
| 3 | [include/scmctl/cli_commands.h](file:///home/hong/code/rm_back/include/scmctl/cli_commands.h) | 新增 `CliCommand` 子类声明 + 成员变量 | 必需 |
| 4 | [src/scmctl/cli_commands.cpp](file:///home/hong/code/rm_back/src/scmctl/cli_commands.cpp) | 实现 `Setup()` + `BuildRequest()` + 末尾 `REGISTER_CLI_COMMAND` 宏 | 必需 |
| 5 | [include/scmd/handlers/new_handler.h](file:///home/hong/code/rm_back/include/scmd/handlers) | 新增 `ICommandHandler` 子类声明 | 必需 |
| 6 | [src/scmd/handlers/new_handler.cpp](file:///home/hong/code/rm_back/src/scmd/handlers) | 实现 `Handle()` + 末尾 `REGISTER_COMMAND_HANDLER` 宏 | 必需 |
| 7 | [include/scmd/service_ctl.h](file:///home/hong/code/rm_back/include/scmd/service_ctl.h) + [src/scmd/service_ctl.cpp](file:///home/hong/code/rm_back/src/scmd/service_ctl.cpp) | 新增业务逻辑方法（如需要） | 可选 |

> **优化效果**：原步骤5（`cli_paser.cpp` 手工 `emplace_back`）已通过 `REGISTER_CLI_COMMAND` 宏自注册消除；步骤1的 `if-constexpr` 链已通过 `RequestCommand<T>` trait 消除；步骤2的 `kCommandCount` 手工常量已通过 CTAD 自动推导消除。

### 步骤间依赖关系

```
步骤1（枚举+结构体+trait） ──┬──→ 步骤2（序列化）   ──→ 编译通过
                             ├──→ 步骤3/4（CLI命令+自注册宏）
                             └──→ 步骤5/6（handler+自注册宏）
步骤7（业务逻辑） ───────────→ 步骤6 调用
```

步骤 1 是所有后续步骤的基础，步骤 2-6 依赖步骤 1，步骤 7 依赖业务需求。

---

## 3. 耦合点与内聚问题

### ~~耦合点 1：data_def.h 四层手工映射~~（已优化）

`include/ipc/data_def.h` 中原存在四层必须手工同步的映射：

```
ScmCommand 枚举 (23个值)
    ↕ 手工对应
请求结构体 (23个struct)
    ↕ 手工加入
ScmRequestData variant (23个类型)
    ↕ ~~手工编写 if-constexpr~~ → 已改为 RequestCommand<T> trait
Command() 方法 (~~23个分支~~ → 1行 std::visit)
```

**已优化**：通过 `RequestCommand<T>` trait 模板特化替代 `if-constexpr` 链。`Command()` 方法简化为 `std::visit([](const auto& param) { return RequestCommand<std::decay_t<decltype(param)>>::value; }, data)`。遗漏 trait 特化会在编译期报错（incomplete type），安全可靠。

### ~~耦合点 2：protocol.cpp 五点手工同步~~（部分已优化）

`src/ipc/protocol.cpp` 中原存在五处必须同时修改的代码：

```
~~kCommandCount (常量，必须与枚举数一致)~~ → 已通过 CTAD 自动推导
    ↕
kCommandNameMap (数组，大小自动计算)
    ↕
ParamsToJson (23个重载)
    ↕
ParamsFromJson (23个重载)
    ↕
FromJson switch (23个case)
```

**已优化**：`kCommandCount` 手工常量已消除，`kCommandNameMap` 使用 C++17 CTAD 自动推导数组大小。剩余四处仍需手工同步，但最危险的数组越界问题已解决。

### ~~耦合点 3：CLI/服务端双端注册不对称~~（已优化）

| 端 | 注册方式 | 修改位置 |
|----|----------|----------|
| ~~CLI 客户端~~ | ~~手工 `emplace_back` 列表~~ → `REGISTER_CLI_COMMAND` 宏自注册 | ~~`cli_paser.cpp`~~ → `cli_commands.cpp` 末尾 |
| 服务端 | `REGISTER_COMMAND_HANDLER` 宏自注册 | 各 handler.cpp 末尾 |

**已优化**：CLI 端通过 `CliCommandRegistry` + `REGISTER_CLI_COMMAND` 宏实现与服务端对称的自注册机制。`cli_paser.cpp` 不再需要手工命令列表，新增 CLI 命令只需在命令实现末尾添加宏调用。

### 耦合点 4：ServiceControl 门面类过胖（未优化）

`ServiceControl` ([service_ctl.h](file:///home/hong/code/rm_back/include/scmd/service_ctl.h)) 作为门面类，承载 30+ 公有方法，协调 6 个领域组件：

```
ServiceControl (30+ 方法)
    ├── ConfigLoader        (配置加载)
    ├── ServiceManager      (服务生命周期)
    ├── NginxManager        (Nginx配置)
    ├── ModelManager        (模型文件)
    ├── UpgradeService      (升级编排)
    └── DatabaseService     (数据库操作)
```

**问题**：
- 所有 handler 都依赖 `ServiceControl&` 引用，即使只需单一领域功能（如 `VersionHandler` 不需要任何业务方法，但仍接收引用）
- ServiceControl 大量方法只是简单转发（如 `StartService` → `mServiceManager->StartService`），增加维护成本
- 新增领域方法时，必须在 ServiceControl 添加转发签名，而非直接在领域管理器上扩展

### ~~内聚问题 1：cli_paser.cpp 命令列表与类定义分离~~（已优化）

~~`CliCommand` 子类定义在 `cli_commands.h/.cpp`，但实例化注册在 `cli_paser.cpp`。~~

**已优化**：通过 `REGISTER_CLI_COMMAND` 宏，命令的"定义"和"注册"内聚到 `cli_commands.cpp` 同一文件。`cli_paser.cpp` 只需调用 `CliCommandRegistry::Instance().BuildAll()` 获取所有命令，无需知道具体命令类。

### 内聚问题 2：protocol.cpp 序列化逻辑与数据定义分离（未优化）

请求结构体在 `data_def.h` 定义，但其 JSON 序列化/反序列化逻辑在 `protocol.cpp` 实现。结构体字段变更时，必须同步修改 `protocol.cpp` 中的 `ParamsToJson` 和 `ParamsFromJson`，两者物理分离，容易遗漏。

**对比**：`ScmResponse` 已使用 `BEGIN_JSON_PARSER/ADD_MEMBER` 宏将序列化逻辑内聚到结构体定义处，但请求结构体未采用此模式（因宏不支持必填字段校验）。

### ~~内聚问题 3：Command() 方法的 if-constexpr 链~~（已优化）

~~`ScmRequest::Command()` 方法内含 23 个 `if constexpr` 分支~~

**已优化**：通过 `RequestCommand<T>` trait 替代，`Command()` 方法简化为 1 行 `std::visit` 调用。trait 特化与请求结构体定义在同一文件，内聚性更好。

---

## 4. 优化建议

其中 P0、P1、P4 已实施，P2/P3/P5 为后续设计方向。按收益/成本比排序：

### 建议 1：kCommandCount 自动计算（低成本高收益）

**现状**：`kCommandCount` 是手工常量，与枚举数不一致时编译错误。

**方案**：利用 `std::array` 的 `size()` 方法自动计算：
```cpp
constexpr std::array<CommandNamePair, 23> kCommandNameMap = {{...}};
// 使用 kCommandNameMap.size() 替代 kCommandCount
```

**收益**：消除手工常量，新增命令时只需修改数组内容，无需更新计数。

### 建议 2：CLI 宏驱动注册（中等成本高收益）

**现状**：`cli_paser.cpp` 手工 `emplace_back` 18 个命令。

**方案**：提供类似 `REGISTER_COMMAND_HANDLER` 的 CLI 自注册宏：
```cpp
// cli_commands.h
#define REGISTER_CLI_COMMAND(CommandClass) \
    namespace { \
        struct CommandClass##_CliAutoReg { \
            CommandClass##_CliAutoReg() { \
                CliCommandRegistry::Instance().Register( \
                    []() { return std::make_unique<CommandClass>(); }); \
            } \
        }; \
        static CommandClass##_CliAutoReg g_##CommandClass##_cli_reg; \
    }

// 各 cli_commands.cpp 末尾
REGISTER_CLI_COMMAND(InstallCommand)
```

**收益**：新增 CLI 命令只需在命令类文件末尾加宏，无需修改 `cli_paser.cpp`，实现开闭原则。

### 建议 3：请求结构体序列化内聚（中等成本中等收益）

**现状**：请求结构体定义在 `data_def.h`，序列化在 `protocol.cpp`，分离维护。

**方案**：请求结构体采用 `ScmResponse` 已有的 `BEGIN_JSON_PARSER/ADD_MEMBER` 宏模式：
```cpp
struct InstallRequest {
    std::string serviceName;
    std::string tarDir;
    BEGIN_JSON_PARSER
    ADD_MEMBER(serviceName)
    ADD_MEMBER(tarDir)
    END_JSON_PARSER
};
```

**收益**：字段定义与序列化内聚于一处，消除 `ParamsToJson/ParamsFromJson` 两个重载和 `FromJson` switch 中的对应 case。

**限制**：`ADD_MEMBER` 宏可能不支持可选字段（如 `UpgradesRequest.tarDir`）和类型校验逻辑，需扩展宏能力或保留部分手工处理。

### 建议 4：Command() 枚举自动推导（高成本中等收益）

**现状**：`Command()` 方法含 23 个 `if constexpr` 分支。

**方案**：使用模板元编程或 `std::variant` 的索引自动推导：
```cpp
// 为每个请求结构体定义 trait
template<typename T> struct RequestCommand;
template<> struct RequestCommand<VersionRequest> { static constexpr ScmCommand value = ScmCommand::VERSION; };
// ...

ScmCommand Command() const {
    return std::visit([](const auto& param) {
        return RequestCommand<std::decay_t<decltype(param)>>::value;
    }, data);
}
```

**收益**：消除 if-constexpr 链，新增命令时只需添加一个 trait 特化。

**限制**：仍需手工添加 trait 特化，只是将映射从方法体转移到 trait 定义，本质复杂度不变。可结合代码生成工具自动产生。

### 建议 5：ServiceControl 按领域拆分接口（高成本高收益）

**现状**：ServiceControl 承载 30+ 方法，所有 handler 依赖完整引用。

**方案**：按领域拆分为抽象接口：
```cpp
class IServiceLifecycle {  // 启停/安装/卸载
    virtual ResultMsg StartService(...) = 0;
    virtual ResultMsg StopService(...) = 0;
    // ...
};
class INginxOps { virtual ResultMsg InitNginx(...) = 0; ... };
class IModelOps { virtual ResultMsg AddModel(...) = 0; ... };
class IUpgradeOps { virtual ResultMsg UpgradeService(...) = 0; ... };

// ServiceControl 继承所有接口
class ServiceControl : public IServiceLifecycle, public INginxOps, ... {};
```

Handler 只依赖所需接口：
```cpp
class InstallHandler : public ICommandHandler {
    ScmResponse Handle(..., IServiceLifecycle& lifecycle, ...);
};
```

**收益**：handler 依赖最小化，符合接口隔离原则。新增领域方法时只需扩展对应接口，不影响其他 handler。

**限制**：`ICommandHandler::Handle` 签名需改为传递多个接口引用或注入"服务上下文"对象，涉及全部 handler 改造，成本较高。

### 建议 6：FromJson switch 自动化（高成本低收益）

**现状**：`FromJson` 方法含 23 个 case，每个 case 结构相同（构造结构体→ParamsFromJson→赋值给variant）。

**方案**：利用 `std::variant` 的索引与枚举值的对应关系，通过模板查找表自动反序列化：
```cpp
template<ScmCommand Cmd> struct RequestType;
template<> struct RequestType<ScmCommand::INSTALL> { using Type = InstallRequest; };
// ...

template<ScmCommand... Cmds>
bool FromJsonImpl(ScmCommand cmd, const Json::Value& params, ScmRequestData& data) {
    return ((cmd == Cmds && (data = typename RequestType<Cmds>::Type{}, 
             ParamsFromJson(std::get<typename RequestType<Cmds>::Type>(data), params))) || ...);
}
```

**收益**：消除 switch-case，新增命令时只需添加 `RequestType` 特化。

**限制**：折叠表达式语法复杂，可读性差，且调试困难。若建议 3（序列化内聚）实施后，此 switch 可大幅简化，此建议的边际收益降低。

### 优化优先级总结

| 建议 | 成本 | 收益 | 优先级 | 状态 |
|------|------|------|--------|------|
| 1. kCommandCount 自动计算 | 低 | 高 | P0 | ✅ 已实施 |
| 2. CLI 宏驱动注册 | 中 | 高 | P1 | ✅ 已实施 |
| 3. 请求结构体序列化内聚 | 中 | 中 | P2 | 未实施（宏不支持必填校验） |
| 5. ServiceControl 按领域拆分 | 高 | 高 | P3 | 未实施（改造成本高） |
| 4. Command() 枚举自动推导 | 高 | 中 | P4 | ✅ 已实施 |
| 6. FromJson switch 自动化 | 高 | 低 | P5 | 未实施（收益低） |

---

## 附：关键文件索引

| 模块 | 文件 |
|------|------|
| CLI 入口 | [src/scmctl/qifeng_scmctl.cpp](file:///home/hong/code/rm_back/src/scmctl/qifeng_scmctl.cpp) |
| CLI 解析 | [src/scmctl/cli_paser.cpp](file:///home/hong/code/rm_back/src/scmctl/cli_paser.cpp) |
| CLI 命令定义 | [include/scmctl/cli_commands.h](file:///home/hong/code/rm_back/include/scmctl/cli_commands.h) + [src/scmctl/cli_commands.cpp](file:///home/hong/code/rm_back/src/scmctl/cli_commands.cpp) |
| CLI 命令注册表 | [include/scmctl/cli_command_registry.h](file:///home/hong/code/rm_back/include/scmctl/cli_command_registry.h) + [src/scmctl/cli_command_registry.cpp](file:///home/hong/code/rm_back/src/scmctl/cli_command_registry.cpp) |
| UDS 客户端 | [src/scmctl/scmctl_client.cpp](file:///home/hong/code/rm_back/src/scmctl/scmctl_client.cpp) |
| 协议数据定义 | [include/ipc/data_def.h](file:///home/hong/code/rm_back/include/ipc/data_def.h) |
| JSON 序列化 | [src/ipc/protocol.cpp](file:///home/hong/code/rm_back/src/ipc/protocol.cpp) |
| 服务端主类 | [include/scmd/scmd_server.h](file:///home/hong/code/rm_back/include/scmd/scmd_server.h) + [src/scmd/scmd_server.cpp](file:///home/hong/code/rm_back/src/scmd/scmd_server.cpp) |
| 命令分发 | [include/scmd/command_dispatcher.h](file:///home/hong/code/rm_back/include/scmd/command_dispatcher.h) + [src/scmd/command_dispatcher.cpp](file:///home/hong/code/rm_back/src/scmd/command_dispatcher.cpp) |
| Handler 注册表 | [include/scmd/handler_registry.h](file:///home/hong/code/rm_back/include/scmd/handler_registry.h) |
| Handler 接口 | [include/scmd/command_handler.h](file:///home/hong/code/rm_back/include/scmd/command_handler.h) |
| 业务门面 | [include/scmd/service_ctl.h](file:///home/hong/code/rm_back/include/scmd/service_ctl.h) + [src/scmd/service_ctl.cpp](file:///home/hong/code/rm_back/src/scmd/service_ctl.cpp) |
| Handler 实现 | [src/scmd/handlers/](file:///home/hong/code/rm_back/src/scmd/handlers) (23个handler) |
