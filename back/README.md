# 会议文档生成器 - 使用说明

## 概述

`meeting_doc_generator.py` 是一个独立的、可迁移的会议文档生成模块，从原项目 `record_content.py` 中提取了核心功能：

- **笔记生成**：HTML / Markdown / Editor.js JSON / 纯文本 → DOCX
- **纪要生成**：支持白板模式 + 三种模板（默认模板/会议纪要模板/联合行文模板）
- **ZIP 打包**：多文件打包为 ZIP

---

## 依赖安装

```bash
pip install -r requirements.txt
```

---

## 数据传递说明

### 1. 笔记生成 `build_note()`

```python
from meeting_doc_generator import MeetingDocGenerator

gen = MeetingDocGenerator(template_dir="./templates", output_dir="./output")

success, result, err = gen.build_note(
    content="<p>会议讨论了项目进度...</p>",  # 笔记内容
    title="项目周会 - 笔记",                 # 文档标题
    content_type="html",                     # 内容类型
    output_path="./output/笔记"              # 输出路径（不含扩展名）
)
# result: "./output/笔记.docx" 或 bytes
```

**content_type 支持的值：**

| 值 | 说明 |
|---|---|
| `"auto"` | 自动检测（推荐） |
| `"html"` | HTML 格式 |
| `"markdown"` | Markdown 格式 |
| `"editorjs"` | Editor.js JSON 格式 |
| `"text"` | 纯文本 |

---

### 2. 纪要生成 `build_summary()`

```python
success, result, err = gen.build_summary(
    content="<h2>会议纪要</h2><p>讨论内容...</p>",  # 纪要正文
    meeting_data={                                  # 会议元数据
        "theme": "项目周会",                         # 会议主题
        "recording_time": 1719123456000,             # 会议时间（毫秒时间戳）
        "places": "3楼会议室",                       # 会议地点
        "moderator": "张三",                         # 主持人
        "attendees": ["李四", "王五"],               # 参会人员列表
        "kind": 0,                                   # 会议类型
        "remark": ""                                 # 备注
    },
    template_type=2,                                 # 模板类型
    content_type="html",                             # 内容类型
    output_path="./output/会议纪要"                   # 输出路径
)
```

**meeting_data 字段说明：**

| 字段 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `theme` | str | 否 | 会议主题，默认 "会议纪要" |
| `recording_time` | int | 否 | 会议时间，毫秒时间戳 |
| `places` | str | 否 | 会议地点 |
| `moderator` | str | 否 | 主持人 |
| `attendees` | list | 否 | 参会人员列表 |
| `kind` | int | 否 | 会议类型：0=标准 1=党委 2=决策 3=晨会 |
| `remark` | str | 否 | 备注 |

**template_type 说明：**

| 值 | 说明 |
|---|---|
| `1` | 白板模式（不使用模板，直接渲染内容 + 会议信息） |
| `2` | 会议纪要模板（需 `templates/summary/会议纪要模板.docx`） |
| `3` | 联合行文模板（需 `templates/summary/联合行文模板.docx`） |

---

### 3. ZIP 打包 `package_to_zip()`

```python
success, zip_path, err = gen.package_to_zip(
    files=[
        ("笔记.docx", note_bytes),
        ("会议纪要.docx", summary_bytes),
    ],
    zip_name="项目周会_文档包"
)
# zip_path: "./output/项目周会_文档包.zip"
```

---

### 4. 一站式生成 `build_and_package()`

```python
success, zip_path, err = gen.build_and_package(
    note_content="<p>笔记内容...</p>",
    summary_content="<h2>纪要内容...</h2>",
    meeting_data={"theme": "项目周会", "recording_time": 1719123456000},
    template_type=2,
    content_type="html",
    zip_name="项目周会_文档包"
)
```

---

## 模板文件结构

```
templates/
├── 默认模板.docx              # 通用默认模板
├── note/
│   └── 默认笔记模板.docx       # 笔记模板
└── summary/
    ├── 会议纪要模板.docx        # 纪要模板（template_type=2）
    └── 联合行文模板.docx        # 联合行文模板（template_type=3）
```

模板中使用 `{{占位符名}}` 语法定义占位符，支持的占位符：

| 占位符 | 说明 |
|---|---|
| `{{会议主题}}` | 会议主题 |
| `{{会议时间}}` | 会议时间 |
| `{{会议地点}}` | 会议地点 |
| `{{主持人}}` | 主持人 |
| `{{参会人员}}` | 参会人员 |
| `{{会议类型}}` | 会议类型 |
| `{{正文}}` | 纪要正文（富文本占位符） |
| `{{备注}}` | 备注 |

---

## 编译为 .exe

使用 PyInstaller 编译：

```bash
pip install pyinstaller

# 编译为单文件 .exe
pyinstaller --onefile --name meeting_doc_gen meeting_doc_generator.py

# 如果需要包含模板目录
pyinstaller --onefile --name meeting_doc_gen --add-data "templates:templates" meeting_doc_generator.py
```

编译后的使用方式：

```bash
# 生成笔记
./meeting_doc_gen note -c content.html -t "笔记标题" -o output.docx

# 生成纪要
./meeting_doc_gen summary -c content.html -d '{"theme":"项目周会"}' -m 2 -o output.docx

# 打包 ZIP
./meeting_doc_gen package -f file1.docx file2.docx -n "文档包" -o ./output
```

---

## 快速使用（无需创建实例）

```python
from meeting_doc_generator import quick_build_note, quick_build_summary

# 快速生成笔记
success, path, err = quick_build_note(
    content="<p>笔记内容</p>",
    output_path="./笔记.docx",
    title="我的笔记"
)

# 快速生成纪要
success, path, err = quick_build_summary(
    content="<h2>纪要内容</h2>",
    output_path="./纪要.docx",
    meeting_data={"theme": "项目周会"},
    template_type=2
)
```

---

## 返回值约定

所有生成方法返回 `(success, result, error)` 三元组：

| success | result | error | 说明 |
|---|---|---|---|
| `True` | 文件路径(str) 或 bytes | `""` | 成功 |
| `False` | `None` | 错误信息(str) | 失败 |

- 当 `output_path` 参数为 `None` 时，`result` 返回 `bytes`
- 当 `output_path` 参数指定路径时，`result` 返回文件路径字符串
