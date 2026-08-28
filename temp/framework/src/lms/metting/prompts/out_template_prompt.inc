/*
 * generated from sgm/prompt/OutTemplate_Prompt.py
 * GENERAL_OUTPUT_TEMPLATE, OUTPUT_TEMPLATE
 */

// 默认输出模板
constexpr std::string_view OutputGeneral =
R"(
## 会议纪要

---

### 会议概览
[2~4句话，涵盖：本次会议围绕什么议题、核心讨论焦点是什么、最终达成了哪些关键结论。]

---
)";

// 详细输出模板（预留，当前未使用）
constexpr std::string_view OutputDetailed =
R"(# 输出格式

### 会议基本信息
- **会议时间**：[请填写]
- **会议地点**：[请填写]
- **主持人**：[请填写]
- **参会人员**：[请填写]
---
### 会议概览
- **会议总览**：[一段精炼、连贯的文字，全面概述会议主要议程、讨论焦点与整体结论]
---
### XXXX（关于 XXX 的讨论）
#### 主要内容
- XXXXX
#### 决策项
- XXXX
---
)";
