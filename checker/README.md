# bm1684-selftest

BM1684 边缘设备开机自检程序（独立 C++17 CMake 工程）。

## 功能

开机时对设备关键部件做并发自检，产出结构化 JSON 报告并以退出码反映整体健康度：

| 检查项 | 严重级 | 说明 |
|---|---|---|
| disk | critical | 挂载点/容量/读写校验 |
| memory | critical | 内存总量/可用/ECC |
| display | warning | libdrm connector 或 sysfs 降级 |
| fingerprint | warning | 串口模组握手 |
| microphone | warning | ALSA 录音 RMS 能量 |
| light | warning | sysfs GPIO 翻转回读 |
| network | critical | 网卡枚举 + ping 网关 |
| tpu | critical | Sophon SDK 设备内存校验 |
| model_inference | critical | 加载 probe.bmodel 跑一次推理 |

## 构建

```bash
cd checker
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
```

CMake 自动探测 Sophon SDK / ALSA / libdrm；缺失项对应检查器在运行期返回 `Skipped`。

## 运行

```bash
./build/bm1684-selftest [config.json]
```

默认读取 `/etc/bm1684-selftest/selftest.json`，缺失则用内置默认。报告写入配置中
`report_path`，控制台同时打印汇总表。退出码：0=整体 OK，1=存在 critical 失败。

## 部署

```bash
sudo ./scripts/install.sh
```

会安装二进制、配置、systemd 单元并 `systemctl enable`。

## 扩展

新增检查器：实现 `IChecker` 子类（`include/checker/checkers/`），在
`src/checkers/register_all.cpp` 增加一行 `r.add<MyChecker>();` 即可，无需改动 core。
