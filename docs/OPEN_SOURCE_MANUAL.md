# 开源手册

面向复现者与评审：如何获取、构建、核对与二次开发本仓库。

## 1. 许可与第三方

- 本仓库应用层源码（`main.c`、`rt_*.c/h`、`board_self_test.h`、文档）以根目录 `LICENSE` 为准。  
- `npu/tvmgen_cnn.h` 及 NNC 生成物含 **Texas Instruments** 版权声明与再分发条件，使用时须保留其页眉许可文本。  
- `npu/mod_cnn.a` / `npu/npu_cnn.onnx` 为模型转换产物，再分发请同时满足 TI 工具链与本仓库许可要求。

## 2. 仓库结构

```text
.
├── README.md                 # 总览与评分材料索引
├── LICENSE
├── docs/                     # 规范性配套文档
│   ├── ITERATION.md
│   ├── 01_control_block_diagram.md
│   ├── 02_signal_chain.md
│   ├── 03_algorithm.md
│   ├── 04_deployment.md
│   ├── 05_test_report.md
│   └── OPEN_SOURCE_MANUAL.md
├── main.c                    # 板级运行
├── rt_estimator.*            # 估计状态机
├── rt_features.*             # 特征
├── rt_cnn.* / rt_cnn_params.*
├── board_self_test.h
├── npu/                      # ONNX + NNC 库
├── CPU1_FLASH/               # 预构建 .out/.map
└── targetConfigs/            # LaunchPad 连接
```

## 3. 最小复现路径（评审友好）

1. 导入 CCS 工程并配置 C2000Ware（[04_deployment.md](04_deployment.md)）。  
2. 烧录 `CPU1_FLASH/*.out` 或自行 Build。  
3. 观察自检：`g_board_self_test_passed==1` 且总线 `0x555`。  
4. 按 [02_signal_chain.md](02_signal_chain.md) 灌数，约 20 s 后应有 `0x180`。  
5. 对照 [05_test_report.md](05_test_report.md) 记录误差与多工况。

## 4. 代码阅读顺序建议

1. `rt_estimator.h` — 接口与输入输出约定  
2. `rt_features.h` — 56 维特征字典  
3. `rt_cnn.c` 文件头 — CPU/NPU 分层  
4. `rt_estimator.c` — 时序状态机  
5. `main.c` — CAN 与实时调度  

源码已含中文模块头注释；后续提交仅加厚注释，不改逻辑。

## 5. 二次开发注意

- **勿手改** `rt_cnn_params.*`、`board_self_test.h`（由训练/导出流水线生成）。  
- 改特征顺序必须同步重训并重新导出权重与自检向量。  
- 改调用周期必须重算所有 EWMA 系数（当前按 Δt=100 ms / 模型步 1 s 固化）。  
- 保持「ISR 只收数、任务里推理」的实时结构。

## 6. 竞赛材料清单（规范性自检）

| 评分要求 | 本仓库位置 |
|----------|------------|
| 代码完整开源、可读、可维护、充分注释 | 根目录源码 + 注释迭代 |
| 控制框图 | [01_control_block_diagram.md](01_control_block_diagram.md) |
| 信号链路说明 | [02_signal_chain.md](02_signal_chain.md) |
| 算法原理 | [03_algorithm.md](03_algorithm.md) |
| 部署说明 | [04_deployment.md](04_deployment.md) |
| 测试报告 | [05_test_report.md](05_test_report.md) |
| 采样/控制与电路对应 | [02_signal_chain.md](02_signal_chain.md) 表 2～5 |
| AI 进入实时环（防 −15） | 文档 § 控制框图 / 算法 §6；实现见 `main.c` Timer0→`rt_step` |
| 多工况测试（防 −10） | 测试报告 §4 表格（需实测归档） |
| 方案介绍海报（防 −10） | 需队伍另交；不在本代码仓强制 |

## 7. 问题反馈

请在 GitHub Issue 中标明：器件型号、是否定义 `RT_NPU`、`error_code`、输入协议（FCN/回放）、以及 `g_can_u` 快照。
