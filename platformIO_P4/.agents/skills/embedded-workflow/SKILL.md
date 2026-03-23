---
name: embedded-workflow
description: 用于标准化基于 ESP32 系列芯片、PlatformIO 和 Arduino 框架的嵌入式演示工程开发流程。适用于用户希望把原理图、模块芯片数据手册、引脚映射、接口说明、现有项目目录或模块需求，转化为可重复执行的 demo 工作流、硬件分析结论、example 拆分方案、PlatformIO 配置、可编译的演示样例代码和交付清单。
---

使用这个 skill，把“硬件资料 + 模块需求”稳定地转成“可交付的演示工程”。

## 默认假设
除非用户另有说明，否则按以下默认值执行：
- 芯片系列：ESP32，优先兼容 ESP32-P4
- 软件栈：PlatformIO + Arduino
- 工程风格：以 `examples` 为中心的模块化演示工程
- 交付目标：既能演示功能，也便于快速验证和后续复用
- 输出语言：中文

## 先收集这些输入
只补问真正缺失的关键信息，不重复询问用户已经给出的内容。
- 模块名称和功能目标
- 主控型号与开发板型号
- 原理图、数据手册、引脚表、接口说明
- 项目目录结构或现有仓库结构
- 期望输出深度：仅工作流、仅模板、生成代码、或完整方案

如果用户没有给全资料，也先按现有信息推进，并明确标出不确定项。

## 任务模式
根据请求选择一种或组合多种模式：

1. **流程标准化**
   输出推荐目录、命名规则、SOP、交付物模板和自动化建议。

2. **硬件资料解读**
   从原理图和数据手册中提取软件开发真正需要的事实：
   - 供电和电平
   - GPIO 角色
   - 使能、复位、中断、唤醒脚
   - 通讯接口和时序
   - 上电、初始化、校准顺序
   - 风险点和限制条件

3. **demo 拆分设计**
   把模块能力拆成多个独立 example，确保每个 example 只承担一个主要目标。

4. **样例代码生成**
   根据拆分设计和代码规范，生成可编译的 example 代码和 PlatformIO 配置片段。
   代码必须能直接编译、烧录，并且串口日志能清晰反映执行步骤和结果。
   优先使用 lib 中的公共函数和配置，避免在 example 中出现重复代码。
   当 lib 中缺乏必要功能时，优先补齐公共层，而不是在 example 中临时代码。
   生成 `examples/*`、`platformio.ini` 片段和必要说明。

5. **交付物整理**
   输出硬件分析记录、example 清单、演示步骤、验证标准、固件归档和测试报告模板。
   输出模板见 [references/output-templates.md](references/output-templates.md)。

## 必须遵循的标准工作流
除非用户明确只要其中一部分，否则按下面顺序执行。

1. **明确演示目标**
   先区分这是“功能演示”、“接口验证”还是“联调样例”。
   输出一句话目标：演示给谁看、要证明什么、成功标准是什么。

2. **阅读硬件资料并做软件抽象**
   使用 [references/review-checklist.md](references/review-checklist.md)。
   只保留软件真正需要的硬件事实：
   - 引脚角色
   - 默认电平
   - 启动顺序
   - 延时要求
   - 依赖电源或外设
   - 可观测结果

3. **把模块功能拆成最小可展示单元**
   一个 example 只负责一个明确目标。
   例如 Wi-Fi 模块优先拆成：
   - `wifi_scan`
   - `wifi_sta`
   - `wifi_ap`

4. **建立工程骨架**
   优先采用以下目录：

   ```text
   project/
   |- examples/
   |- firmware/
   |- hardware/
   |  |- schematic/   # 原理图
   |  |- datasheet/   # 芯片/模块数据手册
   |  |- pinmap/      # 引脚映射表
   |  `- notes/       # 硬件分析记录
   |- lib/
   |  `- demo_core/
   |- scripts/
   |- docs/
   `- platformio.ini
   ```

   如果用户已有结构，尽量沿用，只补齐缺失层。
   其中 `hardware/` 默认标准化为以下结构，后续生成目录树、交付清单和资料整理建议时都优先使用这一版：

   ```text
   hardware/
   |- schematic/   # 原理图
   |- datasheet/   # 芯片/模块数据手册
   |- pinmap/      # 引脚映射表
   `- notes/       # 硬件分析记录
   ```

5. **沉淀公共层**
   把重复内容收敛到 `lib/demo_core`：
   - 串口日志
   - 通用检查宏
   - 公共配置
   - 引脚映射
   - 超时等待和重试逻辑

6. **生成 example**
   每个 example 都必须说明：
   - 演示目的
   - 前置条件
   - 关键步骤
   - 成功判据
   - 失败日志

7. **定义验证与交付规则**
   每个 example 必须给出：
   - 如何编译
   - 如何烧录
   - 串口观察点
   - 成功条件
   - 失败排查入口

8. **归档固件和结果**
   编译出的 `.bin` 放入 `firmware/`，命名包含模块、功能、日期或版本号。
   如果用户需要交付报告，使用 [references/output-templates.md](references/output-templates.md)。

详细节奏见 [references/workflow-sop.md](references/workflow-sop.md)。

## 命名规则
优先采用带上测试层级和目标的命名规则，例如：
examples/
├─ wifi/
│  ├─ smoke_scan/
│  ├─ func_sta_connect/
│  ├─ func_ap_start/
│  ├─ robust_wrong_password/
│  └─ stress_reconnect_1000/
├─ i2c/
└─ gpio/

命名格式建议：
- `<module>_<goal>`
- `<module>_<goal>_<variant>`

避免把多个行为混进同一个 example。

## 代码规则
生成代码时始终遵循：
- 保持 Arduino 风格，除非用户明确要求 ESP-IDF
- 不在 `loop()` 中隐藏复杂状态机，优先让演示入口清晰
- 所有阻塞操作必须有超时
- 日志必须能让串口直接看出步骤、状态和失败点
- 配置集中到 `demo_config.h`
- 引脚集中到 `demo_pins.h`
- 重复逻辑下沉到 `lib/demo_core`
- 不凭空捏造原理图或数据手册中不存在的事实

## 输出顺序
对标准化请求，优先按这个顺序输出：
1. 推荐工作流
2. 目录结构
3. example 拆分方案
4. 公共层设计
5. 代码规范
6. 自动化建议
7. 可选脚手架

在输出目录结构时，`hardware/` 必须优先展开为：
- `hardware/schematic`
- `hardware/datasheet`
- `hardware/pinmap`
- `hardware/notes`

对代码生成请求，优先按这个顺序输出：
1. 文件树
2. `lib/demo_core` 公共头文件和源文件
3. 每个 example 的 `main.cpp`
4. `platformio.ini` 片段
5. 编译和烧录说明
6. 风险和待确认项

对硬件分析请求，优先按这个顺序输出：
1. 关键硬件结论
2. 引脚映射表
3. 初始化与时序要求
4. 软件实现约束
5. 建议拆分出的 example

## 参考文件
需要时按需加载，不要一次性全部展开：
- [references/workflow-sop.md](references/workflow-sop.md)：标准 SOP 和阶段出口
- [references/output-templates.md](references/output-templates.md)：分析、规划、报告模板
- [references/review-checklist.md](references/review-checklist.md)：原理图和数据手册阅读清单

## 可选脚手架
如果用户想快速创建 example，使用 `scripts/scaffold_example.py`。
- 默认生成扁平目录：`examples/<module>_<case_name>`
- 可选生成嵌套目录：`examples/<module>/<case_name>`

## 边界
- 资料不全时，明确写出“已确认”和“待确认”。
- 不把 demo 样例伪装成量产代码。
- 不把单次项目经验写死成不可扩展的结构。
