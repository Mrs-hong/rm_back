# BM1684 边缘设备开机自检系统 — 开发与使用指南

> 版本：1.0.0 | 更新日期：2026-06-29

---

## 1. 快速开始

### 1.1 环境要求

| 项 | 要求 |
|----|------|
| 编译器 | GCC 8+ 或 Clang 7+（需 C++17 支持） |
| CMake | 3.14+ |
| 目标平台 | ARM Linux (BM1684) 或 x86 Linux (WSL2 开发验证) |
| 可选系统库 | Sophon SDK (bmlib/bmrt/bmcv)、libasound、libdrm |

### 1.2 构建

```bash
cd checker

# 配置（默认 Release）
cmake -S . -B build

# 编译
cmake --build build -j$(nproc)

# 运行自检（使用内置默认配置）
./build/bm1684-selftest

# 运行自检（指定配置文件）
./build/bm1684-selftest config/selftest.json

# 运行测试
./build/test_json
```

### 1.3 一键构建并验证

```bash
cmake -S . -B build && cmake --build build -j$(nproc) && \
  ./build/test_json && ./build/bm1684-selftest config/selftest.json
```

预期输出（x86 环境）：

```
json+config tests PASSED
2026-06-29 ... [info] === BM1684 SelfTest v1.0.0 start ===
...
ITEM               STATUS   TIME     MESSAGE
----               ------   ----     -------
disk               PASS     0     ms disk ok
memory             PASS     0     ms memory ok
network            WARN     4     ms gateway unreachable
tpu                SKIP     0     ms no TPU device
...
=== SelfTest done: OK ===
```

退出码为 0 表示通过。

---

## 2. CMake 构建选项

### 2.1 控制可选依赖

```bash
# 禁用 Sophon SDK（TPU/模型推理检查器编译为 SKIP 桩）
cmake -S . -B build -DWITH_BM1684_SDK=OFF

# 禁用 ALSA（麦克风检查器编译为 SKIP 桩）
cmake -S . -B build -DWITH_ALSA=OFF

# 禁用 libdrm（显示器检查器降级为 sysfs 探测）
cmake -S . -B build -DWITH_DRM=OFF

# 指定 Sophon SDK 路径
cmake -S . -B build -DBM_SDK_ROOT=/opt/sophon/libsophon-current

# 禁用单元测试
cmake -S . -B build -DBUILD_TESTING=OFF

# 全部组合
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DWITH_BM1684_SDK=ON \
  -DWITH_ALSA=ON \
  -DWITH_DRM=ON \
  -DBM_SDK_ROOT=/opt/sophon/libsophon-current
```

### 2.2 交叉编译（ARM）

```bash
# 假设已安装 ARM 交叉工具链
cmake -S . -B build-arm \
  -DCMAKE_TOOLCHAIN_FILE=../aarch64-linux-gnu.cmake \
  -DWITH_BM1684_SDK=ON \
  -DBM_SDK_ROOT=/opt/sophon/libsophon-current
cmake --build build-arm -j$(nproc)
```

### 2.3 构建产物

| 产物 | 路径 | 说明 |
|------|------|------|
| 主程序 | `build/bm1684-selftest` | 自检可执行文件 |
| 测试 | `build/test_json` | JSON + 配置加载测试 |
| 生成头 | `build/generated/checker/config.h` | 编译期平台能力宏 |

---

## 3. 运行与使用

### 3.1 命令行

```bash
bm1684-selftest [配置文件路径]
```

- 无参数：使用 `/etc/bm1684-selftest/selftest.json`
- 指定路径：使用给定 JSON 配置文件

### 3.2 退出码

| 码 | 含义 |
|----|------|
| 0 | 全部通过（或仅有 warning/skip） |
| 1 | 存在 critical 项 FAIL |

### 3.3 输出

1. **控制台表格**（stderr）：人类可读的汇总表
2. **JSON 报告**（文件）：结构化机器可读报告，路径由 `report_path` 配置
3. **日志文件**：滚动日志，路径由 `log_dir` 配置

### 3.4 自定义配置

复制并修改配置文件：

```bash
cp config/selftest.json /etc/bm1684-selftest/selftest.json
vim /etc/bm1684-selftest/selftest.json
```

常用调整：

```json
{
  "per_item_timeout_sec": 10,
  "parallel": false,
  "disk": {
    "mounts": ["/", "/data"],
    "min_free_pct": 10
  },
  "network": {
    "gateway": "10.0.0.1",
    "ping_count": 5
  }
}
```

### 3.5 调试模式

```bash
# 顺序执行（便于单步调试）
# 修改配置 parallel=false
# 或临时覆盖配置文件
cat > /tmp/debug_selftest.json << 'EOF'
{
  "parallel": false,
  "per_item_timeout_sec": 60,
  "log_dir": "",
  "disk": { "mounts": ["/"] }
}
EOF
./build/bm1684-selftest /tmp/debug_selftest.json
```

---

## 4. 新增 C++ 检查器

### 4.1 三步走

**第一步**：创建头文件 `include/checker/checkers/my_checker.h`

```cpp
#pragma once
#include "checker/core/checker.h"

namespace checker {

class MyChecker : public IChecker {
 public:
  static std::string class_name() { return "my_module"; }
  std::string name() const override { return class_name(); }
  Severity severity() const override { return Severity::kWarning; }
  CheckResult run(const Context& ctx) override;
};

}  // namespace checker
```

**第二步**：创建实现文件 `src/checkers/my_checker.cpp`

```cpp
#include "checker/checkers/my_checker.h"
#include "checker/log/logger.hpp"
#include "checker/util/time_util.h"

namespace checker {

CheckResult MyChecker::run(const Context& ctx) {
  CheckResult r(name());
  Timer timer;

  // 检查硬件是否可用
  if (!硬件可用条件) {
    r.status = Status::kSkipped;
    r.message = "hardware not available";
    r.elapsed_ms = timer.elapsed_ms();
    LOG_INFO("[my_module] %s", r.message.c_str());
    return r;
  }

  // 执行检查逻辑
  bool ok = /* 检查结果 */;
  r.status = ok ? Status::kPass : Status::kFail;
  r.message = ok ? "my_module ok" : "my_module failed";
  r.details.emplace_back("key", "value");
  r.elapsed_ms = timer.elapsed_ms();
  LOG_INFO("[my_module] %s (%dms)", r.message.c_str(), r.elapsed_ms);
  return r;
}

}  // namespace checker
```

**第三步**：注册 — 编辑 `src/checkers/register_all.cpp`

```cpp
#include "checker/checkers/my_checker.h"  // 新增

void register_all(CheckerRegistry& reg) {
  // ... 已有注册 ...
  reg.add<MyChecker>();  // 新增
}
```

并在 `CMakeLists.txt` 的 `add_executable` 中追加 `src/checkers/my_checker.cpp`。

### 4.2 检查器编写规范

| 规范 | 说明 |
|------|------|
| 资源不可用 → SKIP | 区分"硬件未装配"与"硬件故障" |
| 严重级别选择 | 启动必需的设 critical，否则 warning |
| 超时自控 | 检查器自身应有合理超时（串口 read 用 select、ping 用 -W），不依赖 Runner 兜底 |
| RAII 释放 | 文件描述符、设备句柄等用 unique_ptr + 自定义 deleter 或局部 RAII 类 |
| 日志输出 | 调用 `LOG_INFO("[name] %s (%dms)", msg, ms)` 保持格式统一 |
| 异常安全 | run() 内异常会被 Runner 捕获标记 FAIL，但仍建议自行 try-catch 关键段 |
| details 使用 | 关键诊断信息写入 details（键值对），便于 JSON 报告机器解析 |

### 4.3 条件编译检查器

若检查器依赖可选系统库，需：

1. 在 `CMakeLists.txt` 中 `find_package` / `find_path` / `find_library` 探测
2. 成功时 `set(CHECKER_HAS_XXX 1)`，在 `config.h.in` 增加 `#cmakedefine01 CHECKER_HAS_XXX`
3. 在 `.cpp` 中用 `#if defined(CHECKER_HAS_XXX) && CHECKER_HAS_XXX` 包裹真实实现
4. 无库时提供桩实现（直接返回 SKIP）

参考 `tpu_checker.cpp`、`microphone_checker.cpp`、`drm_device.cpp`。

---

## 5. 接入外部 .sh 脚本

### 5.1 适用场景

- 部门同事已有成熟的 Shell 硬件检测脚本
- 某些硬件的检测逻辑用 Shell 更方便（如调用厂商 CLI 工具）
- 快速验证新硬件，暂不想写 C++ 代码

### 5.2 脚本编写规范

**必须遵守退出码协议：**

```bash
#!/bin/bash
set -euo pipefail

# 检查逻辑...

if 通过; then
  # 可选：输出 JSON 末行提供详细信息
  echo '{"message":"检测通过","details":{"version":"1.0"}}'
  exit 0   # PASS
elif 硬件未装配; then
  echo '{"message":"硬件未装配","details":{"device":"/dev/xxx"}}'
  exit 2   # SKIP
elif 非严重异常; then
  echo '{"message":"性能偏低","details":{"metric":"65"}}'
  exit 3   # WARN
else
  echo '{"message":"检测失败","details":{"error":"xxx"}}'
  exit 1   # FAIL
fi
```

**要点：**

1. 退出码必须遵循协议（0/1/2/3）
2. JSON 末行可选，但推荐提供（便于结构化报告）
3. JSON 格式：`{"message":"...","details":{...}}`
4. details 的值为字符串时直接取值，其他类型用 JSON dump
5. 脚本应设置 `set -euo pipefail` 保证错误传播
6. 脚本应自行处理超时（或依赖 ScriptChecker 的 `timeout` 命令包裹）

### 5.3 配置接入

在 `selftest.json` 的 `scripts` 数组中添加条目：

```json
{
  "scripts": [
    {
      "name": "fingerprint_sh",
      "path": "/etc/bm1684-selftest/scripts/check_fingerprint.sh",
      "severity": "warning",
      "timeout_sec": 5
    },
    {
      "name": "custom_sensor",
      "path": "/etc/bm1684-selftest/scripts/check_sensor.sh",
      "severity": "critical",
      "timeout_sec": 10
    }
  ]
}
```

无需修改任何 C++ 代码，重新运行即可生效。

### 5.4 部署脚本

```bash
# 将脚本部署到标准目录
sudo install -d /etc/bm1684-selftest/scripts
sudo install -m 0755 scripts/check_sensor.sh /etc/bm1684-selftest/scripts/

# 更新配置
sudo vim /etc/bm1684-selftest/selftest.json
# 在 scripts 数组中添加新条目
```

### 5.5 脚本 vs C++ 检查器选择建议

| 维度 | C++ 检查器 | .sh 脚本 |
|------|-----------|---------|
| 性能 | 高（直接系统调用） | 较低（fork+exec） |
| 启动延迟 | 无 | ~10ms（bash 解释器） |
| 依赖管理 | 编译期确定 | 运行期（需确保 bash、命令可用） |
| 开发速度 | 慢（需编译） | 快（直接修改运行） |
| 适用场景 | 核心硬件、高频调用 | 快速验证、已有脚本、临时检测 |
| 推荐策略 | 启动关键硬件用 C++ | 辅助检测、第三方工具用脚本 |

---

## 6. 硬件抽象层扩展

### 6.1 新增硬件接口

在 `include/checker/hw/` 下新增头文件，遵循已有模式：

```cpp
// my_device.h
#pragma once
#include <string>

namespace checker {

class MyDevice {
 public:
  MyDevice() = default;
  explicit MyDevice(const std::string& device_path);
  ~MyDevice();

  bool is_open() const { return fd_ >= 0; }
  // 设备操作方法...

 private:
  int fd_ = -1;
};

}  // namespace checker
```

**设计原则：**

- RAII：构造时打开，析构时关闭
- `is_open()` 检查：失败不抛异常，由调用方决定 SKIP/WARN/FAIL
- 避免头文件泄漏系统库（用 `void*` / 前向声明 + 条件编译）
- 条件编译：可选库用 `#if defined(CHECKER_HAS_XXX)` 包裹

### 6.2 现有硬件抽象

| 类 | 文件 | 用途 |
|----|------|------|
| SerialPort | hw/serial_port.h | POSIX 串口（指纹模组） |
| Gpio | hw/gpio.h | sysfs GPIO（指示灯） |
| DrmDevice | hw/drm_device.h | libdrm / sysfs 显示器探测 |
| AlsaCapture | hw/alsa_capture.h | ALSA PCM 录音（麦克风） |

---

## 7. 日志系统使用

### 7.1 日志宏

```cpp
#include "checker/log/logger.hpp"

LOG_TRACE("trace msg: %s", val);
LOG_DEBUG("debug msg: %d", num);
LOG_INFO("info msg: %s", str);
LOG_WARN("warning: %s", msg);
LOG_ERROR("error: %s", err);
```

printf 风格格式串，线程安全。

### 7.2 初始化

在 `main()` 中完成，其他模块直接使用宏：

```cpp
// 默认日志器初始化
checker::default_logger().init(
  "/var/log/bm1684-selftest",  // 日志目录（空则仅控制台）
  "selftest.log",              // 文件名
  LogLevel::kInfo,             // 最低级别
  1 << 20,                     // 单文件最大字节（1MB）
  3                            // 保留文件数
);
```

### 7.3 运行期调整级别

```cpp
checker::default_logger().set_level(LogLevel::kDebug);  // 开启调试输出
```

---

## 8. 测试

### 8.1 运行测试

```bash
# 构建 + 运行
cmake --build build && ./build/test_json

# 或用 CTest
cd build && ctest --output-on-failure
```

### 8.2 测试覆盖

| 测试 | 覆盖内容 |
|------|---------|
| test_json | nlohmann/json 基本操作 + load_config 配置解析 |

### 8.3 编写新测试

在 `tests/` 下创建新测试文件，在 `CMakeLists.txt` 中注册：

```cmake
add_executable(test_xxx tests/test_xxx.cpp src/core/context.cpp src/log/logger.cpp)
target_include_directories(test_xxx PRIVATE
  ${CMAKE_SOURCE_DIR}/include ${CMAKE_BINARY_DIR}/generated)
target_link_libraries(test_xxx PRIVATE checker_third_party Threads::Threads)
add_test(NAME xxx_test COMMAND test_xxx)
```

---

## 9. 代码规范

### 9.1 格式化

```bash
# 项目根目录下
find include src tests -name '*.cpp' -o -name '*.h' -o -name '*.hpp' | \
  xargs clang-format -i
```

配置文件：`.clang-format`（Google 风格，100 列宽，2 空格缩进）

### 9.2 命名规范

| 类别 | 风格 | 示例 |
|------|------|------|
| 类名 | PascalCase | `DiskChecker` |
| 函数/方法 | snake_case | `run_all()` |
| 变量 | snake_case | `elapsed_ms` |
| 常量/枚举 | kPascalCase | `Status::kPass` |
| 宏 | UPPER_SNAKE | `LOG_INFO` |
| 文件名 | snake_case | `disk_checker.cpp` |
| 命名空间 | snake_case | `checker` |

### 9.3 注释规范

- 文件头部注释：说明模块用途、设计要点
- 函数级注释：复杂逻辑用中文注释说明意图
- 关键决策：用注释说明"为什么"而非"做什么"

---

## 10. 部署到 BM1684 设备

### 10.1 交叉编译

```bash
# 在 x86 开发机上交叉编译
cmake -S . -B build-arm \
  -DCMAKE_TOOLCHAIN_FILE=/path/to/aarch64-linux-gnu.cmake \
  -DWITH_BM1684_SDK=ON \
  -DBM_SDK_ROOT=/opt/sophon/libsophon-current
cmake --build build-arm -j$(nproc)
```

### 10.2 部署

```bash
# 方式一：使用安装脚本
scp build-arm/bm1684-selftest root@device:/tmp/
scp -r config/ scripts/ root@device:/tmp/checker/
ssh root@device 'cd /tmp/checker && bash scripts/install.sh'

# 方式二：手动部署
ssh root@device 'mkdir -p /etc/bm1684-selftest /var/log/bm1684-selftest'
scp build-arm/bm1684-selftest root@device:/usr/local/bin/
scp config/selftest.json root@device:/etc/bm1684-selftest/
scp scripts/bm1684-selftest.service root@device:/etc/systemd/system/
ssh root@device 'systemctl daemon-reload && systemctl enable bm1684-selftest'
```

### 10.3 验证部署

```bash
# 手动运行
ssh root@device 'bm1684-selftest'

# 查看报告
ssh root@device 'cat /var/log/bm1684-selftest/report.json | python3 -m json.tool'

# 查看日志
ssh root@device 'tail -50 /var/log/bm1684-selftest/selftest.log'

# 查看服务状态
ssh root@device 'systemctl status bm1684-selftest'
```

---

## 11. 常见问题排查

### 11.1 编译问题

| 问题 | 原因 | 解决 |
|------|------|------|
| `Sophon SDK not found` | 未安装 SDK 或路径不对 | `-DBM_SDK_ROOT=...` 或 `-DWITH_BM1684_SDK=OFF` |
| `ALSA not found` | 无 libasound-dev | `-DWITH_ALSA=OFF`（x86 开发机正常） |
| `libdrm not found` | 无 libdrm-dev | `-DWITH_DRM=OFF`（降级为 sysfs 探测） |
| spdlog dangling-reference 警告 | GCC 16 + spdlog bundled fmt | 良性警告，不影响功能 |

### 11.2 运行问题

| 问题 | 原因 | 解决 |
|------|------|------|
| 退出码 1 但设备正常 | 配置的网关不可达 | 修改 `network.gateway` 为实际网关 |
| disk FAIL | /data 或 /opt/sophon 不存在 | 修改 `disk.mounts` 仅保留存在的挂载点 |
| 脚本 SKIP | 脚本路径不存在 | 确认脚本已部署到配置中的路径 |
| 报告写入失败 | `/var/log/bm1684-selftest` 不可写 | `sudo mkdir -p /var/log/bm1684-selftest` |
| 日志无文件输出 | `log_dir` 为空或目录不可写 | 检查配置中的 `log_dir` |

### 11.3 x86 开发环境

x86 上部分硬件检查器会 SKIP 或 WARN，这是正常的：

| 检查器 | x86 预期 |
|--------|---------|
| disk | PASS（只读分区记 io=ro） |
| memory | PASS |
| network | WARN（网关可能不可达） |
| tpu | SKIP |
| model_inference | SKIP |
| display | WARN |
| fingerprint | SKIP |
| microphone | SKIP |
| light | SKIP |

只要退出码为 0 即表示框架正常。

---

## 12. 配置模板参考

### 12.1 最小配置（仅核心硬件）

```json
{
  "per_item_timeout_sec": 5,
  "parallel": true,
  "disk": { "mounts": ["/"] },
  "memory": { "min_available_mb": 64 },
  "network": { "gateway": "192.168.1.1" }
}
```

### 12.2 完整配置（含脚本）

```json
{
  "per_item_timeout_sec": 5,
  "parallel": true,
  "report_path": "/var/log/bm1684-selftest/report.json",
  "log_dir": "/var/log/bm1684-selftest",
  "disk": {
    "mounts": ["/", "/data", "/opt/sophon"],
    "min_free_pct": 5
  },
  "memory": { "min_available_mb": 128 },
  "fingerprint": {
    "device": "/dev/ttyS3",
    "baud": 57600,
    "timeout_ms": 1500
  },
  "microphone": {
    "device": "default",
    "duration_ms": 400,
    "min_rms": 50
  },
  "light": { "gpio": "488" },
  "network": {
    "gateway": "192.168.1.1",
    "ping_count": 3
  },
  "model": { "path": "/opt/sophon/selftest/probe.bmodel" },
  "scripts": [
    {
      "name": "fingerprint_sh",
      "path": "/etc/bm1684-selftest/scripts/check_fingerprint.sh",
      "severity": "warning",
      "timeout_sec": 5
    },
    {
      "name": "sensor_sh",
      "path": "/etc/bm1684-selftest/scripts/check_sensor.sh",
      "severity": "critical",
      "timeout_sec": 10
    }
  ]
}
```

### 12.3 调试配置

```json
{
  "per_item_timeout_sec": 60,
  "parallel": false,
  "log_dir": "",
  "disk": { "mounts": ["/"] }
}
```

---

## 13. 脚本输出协议完整参考

### 13.1 退出码

| 码 | Status | 场景 |
|----|--------|------|
| 0 | PASS | 检测通过 |
| 1 | FAIL | 检测失败 |
| 2 | SKIP | 硬件未装配/不可用 |
| 3 | WARN | 非严重异常 |
| 124 | FAIL | timeout 命令超时 |
| 126 | SKIP | 脚本不可执行 |
| 127 | SKIP | 命令未找到 |
| 其他 | FAIL | 未知错误 |

### 13.2 JSON 输出（可选）

stdout 最后一行若为合法 JSON 对象，ScriptChecker 会自动解析：

```json
{
  "message": "简要描述（覆盖默认消息）",
  "details": {
    "key1": "字符串值直接取值",
    "key2": 123,
    "key3": true,
    "key4": [1, 2, 3]
  }
}
```

- `message`：字符串，覆盖退出码对应的默认消息
- `details`：对象，所有键值对填充到 CheckResult.details
  - 字符串值：直接取值
  - 非字符串值：JSON dump 为字符串

### 13.3 纯文本输出（无 JSON）

若无 JSON 末行，ScriptChecker 取 stdout 末尾非空文本作为 message（截断 200 字符）。

### 13.4 完整示例

**脚本：**

```bash
#!/bin/bash
set -euo pipefail

DEVICE="/dev/my_sensor"

if [ ! -e "$DEVICE" ]; then
  echo "sensor not found" >&2
  echo '{"message":"sensor not present","details":{"device":"'"$DEVICE"'"}}'
  exit 2
fi

VALUE=$(cat "$DEVICE/value" 2>/dev/null || echo "N/A")

if [ "$VALUE" = "N/A" ]; then
  echo '{"message":"cannot read sensor","details":{"device":"'"$DEVICE"'"}}'
  exit 3
elif [ "$VALUE" -lt 100 ]; then
  echo '{"message":"sensor ok","details":{"value":"'"$VALUE"'"}}'
  exit 0
else
  echo '{"message":"sensor value too high","details":{"value":"'"$VALUE"'","threshold":"100"}}'
  exit 1
fi
```

**报告输出：**

```json
{
  "sensor": {
    "status": "PASS",
    "elapsed_ms": 12,
    "message": "sensor ok",
    "details": {
      "severity": "warning",
      "script": "/etc/bm1684-selftest/scripts/check_sensor.sh",
      "exit_code": "0",
      "device": "/dev/my_sensor",
      "value": "42"
    }
  }
}
```

---

## 14. 扩展检查清单

新增检查器时，请确认以下事项：

- [ ] 实现 `IChecker` 接口（name / severity / run）
- [ ] 提供 `static std::string class_name()` 供 Registry 工厂
- [ ] 在 `register_all.cpp` 中 `reg.add<YourChecker>()`
- [ ] 在 `CMakeLists.txt` 中追加 `.cpp` 源文件
- [ ] 若依赖可选库：CMake 探测 + config.h.in + 条件编译
- [ ] 硬件不可用时返回 SKIP（而非 FAIL）
- [ ] 关键信息写入 `details`（键值对）
- [ ] 日志格式统一：`LOG_INFO("[name] %s (%dms)", msg, ms)`
- [ ] 运行 `clang-format -i` 格式化
- [ ] x86 验证编译通过 + 退出码 0

---

## 15. 版本历史

| 版本 | 日期 | 变更 |
|------|------|------|
| 1.0.0 | 2026-06-29 | 初始版本：9 类硬件检查器 + ScriptChecker 适配器 + nlohmann/json + spdlog |
