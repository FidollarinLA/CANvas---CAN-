# CANvas

CANvas 是一个面向 CAN 调试场景的 Web 工作台 MVP，支持：

- Excel/CSV 上传与数据预览
- 从 Excel 生成 .list 格式 CAN 报文
- .list 报文规则校验与比赛 Excel 导出
- AI 脚本助手（当前为 Mock API）

## 技术栈

- React + Vite + TypeScript
- Tailwind CSS
- xlsx + papaparse
- prismjs

## 本地启动

1. 安装 Node.js 20+（当前机器未检测到 Node/npm）。
2. 安装依赖：

```bash
npm install
```

3. 启动开发：

```bash
npm run dev
```

4. 构建产物：

```bash
npm run build
```

5. 运行后可以上传 `.list` 文件进行规则校验，并导出 `CANvas-校验结果.xlsx`。
5. 运行后可以上传 Excel，生成 `.list` 报文，并导出 `CANvas-比赛展示.xlsx`。


一、 产品方案：CANvas - 智能 CAN 通信载荷转换与协同工作台

项目背景与痛点分析 (The Problem) 在新能源汽车电子、机器人运动控制等前沿领域，CAN 通信是不可或缺的底层基础设施。然而，在进行硬件在环（HIL）或实车调试时，开发者常常陷入“报文泥潭”：
格式转换繁琐： 上位机软件通常只接受特定的十六进制列表文件（如 .asc, .txt 等）。将研究人员常用的 Excel 表格数据（特别是涉及多组浮点数、物理值与原始值的换算）手动或通过劣质脚本转换为十六进制，过程极其痛苦且易出错。

时序修改低效： 调试过程中，经常需要动态调整报文的发送周期和触发条件。传统方式下，这意味着要重新修改上位机脚本（如 CAPL 脚本），耗时费力，打断了开发者的心流状态。

产品简介 (The Solution) CANvas 是一款专为硬件开发者和算法工程师设计的 Web 端智能通信辅助工具。它将繁杂的数据转换流程自动化，并引入大语言模型（LLM），让开发者可以通过自然语言“对话”来实时生成和修改通信脚本。

核心功能 (Core Features)

无缝数据映射 (Data-to-Hex Engine)： 支持一键导入含有十进制特征数据的 Excel 表格，内置强大的解析引擎，精准处理诸如“单 ID 下多路 Float (Single) 变量”等复杂协议，自动打包生成可供上位机直接读取的 16 进制 List 格式文件。

AI 驱动脚本重构 (AI Script Copilot)： 接入大模型 API，只需输入自然语言（例如：“帮我把所有电机控制指令的发送间隔改为 10ms，并在第五帧加入唤醒信号”），即可实时生成或修改对应的控制脚本。

可视化命令看板 (Visual Command Dashboard)： 提供一个清爽的控制台界面，用户可以在 Web 端直观地拖拽或填表来自定义各项命令的发送时间戳、周期和触发逻辑，彻底告别反人类的纯文本编辑。

比赛亮点 (Why We Win)
直击真实痛点： 来源于一线工程实践，不是伪需求，具备极高的实用价值。

AI 赋能生产力： 将 AI 的能力（自然语言编程）完美融入传统的硬件调试工作流，展现了跨界融合的创新。

极简优雅体验： 用现代化的 Web 界面取代了传统工业软件粗糙的交互，降低了使用门槛。

二、 软件总体架构图

Unable to render rich display

Parse error on line 2:
...TD subgraph 前端 Web (用户交互层) UI[极简 UI 界
----------------------^
Expecting 'SEMI', 'NEWLINE', 'SPACE', 'EOF', 'GRAPH', 'DIR', 'subgraph', 'SQS', 'end', 'AMP', 'COLON', 'START_LINK', 'STYLE', 'LINKSTYLE', 'CLASSDEF', 'CLASS', 'CLICK', 'DOWN', 'UP', 'NUM', 'NODE_STRING', 'BRKT', 'MINUS', 'MULT', 'UNICODE_TEXT', got 'PS'

For more information, see https://docs.github.com/get-started/writing-on-github/working-with-advanced-formatting/creating-diagrams#creating-mermaid-diagrams

graph TD
	subgraph 前端 Web (用户交互层)
		UI[极简 UI 界面]
		Upload[Excel/CSV 上传模块]
		Table[数据可视化预览表]
		AIChat[AI 交互控制台]
		Export[十六进制 List 导出]
	end

	subgraph 后端 Server (逻辑处理层)
		Parser[表格解析引擎]
		HexCore[十六进制转换核心<br/>IEEE 754 Float 封包]
		AIAgent[LLM 提示词组装与路由]
		ScriptGen[控制脚本生成器]
	end

	subgraph 外部服务层
		LLM[大语言模型 API<br/>通义千问/智谱/DeepSeek等]
	end

	%% 数据流向
	Upload -->|上传表格| Parser
	Parser -->|提取特征数据| HexCore
	HexCore -->|生成 16进制 Payload| Table
	Table --> Export
    
	AIChat -->|用户自然语言需求| AIAgent
	AIAgent -->|携带上下文| LLM
	LLM -->|返回代码/配置| ScriptGen
	ScriptGen -->|渲染脚本| AIChat
	ScriptGen --> Export

- `ID`
- `变量名`
- `原始值 (10进制)`
- `目标类型`
- `周期`

也兼容英文键名：`id`、`variable`、`rawValue`、`targetType`、`periodMs`。
