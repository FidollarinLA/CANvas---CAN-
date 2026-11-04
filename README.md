# 转子温度在线估计：纯 CNN × F28P550 NPU（T15）

第 12 届高校电力电子应用设计大赛（台达杯电力电子人工智能设计竞赛）开源工程。  
在 **TMS320F28P550** 上以 **100 ms** 实时任务在线估计永磁体表面温度 **T15**；量化 CNN 主体跑在片上 **NPU**。

> **算法逻辑冻结**：开源迭代只增加注释与文档，不改动特征、网络、时序常数与 CAN 语义。

---

## 规范性材料一览（冲满分索引）

| 竞赛要求 | 文档 |
|----------|------|
| 控制框图 | [docs/01_control_block_diagram.md](docs/01_control_block_diagram.md) |
| 信号链路 + 采样/控制与电路对照 | [docs/02_signal_chain.md](docs/02_signal_chain.md) |
| 算法原理 | [docs/03_algorithm.md](docs/03_algorithm.md) |
| 部署说明 | [docs/04_deployment.md](docs/04_deployment.md) |
| 测试报告 | [docs/05_test_report.md](docs/05_test_report.md) |
| 开源手册 | [docs/OPEN_SOURCE_MANUAL.md](docs/OPEN_SOURCE_MANUAL.md) |
| 迭代看板 | [docs/ITERATION.md](docs/ITERATION.md) |
| 源码审阅纪要（不改逻辑） | [docs/CODE_REVIEW.md](docs/CODE_REVIEW.md) |

**防扣分要点（已在文档与架构中写明）**

1. AI **进入实时控制/估计环**：Timer0 → `rt_step()` → NPU 推理 → CAN `0x180`（非离线后处理）。  
2. **多工况迁移**：见测试报告 §4 表格，需归档实测。  
3. **方案介绍海报**：由队伍另行提交展示材料。

---

## 30 秒看懂数据流

```text
CAN 输入 Id/Iq/Ud/Uq/n/T01/T03/T08
        → 100 ms 调用 rt_step
        → 1 s 特征步 × 20 s 窗口
        → CNN(NPU) → EWMA → T15 ℃
        → CAN 0x180（valid=1）
```

默认协议（`CAN_INPUT_USE_FCN=1`）：`0x100` 电流+转速，`0x110` 电压，`0x120/121/122` 三路温度；就绪帧 `0x555`。完整表见信号链路文档。

---

## 快速开始

1. 安装 CCS + C2000Ware，导入本仓库根目录工程。  
2. 确认宏 **`RT_NPU`**，构建 **CPU1_FLASH**，或直接烧录 `CPU1_FLASH/*.out`。  
3. 自检通过后总线应出现 `0x555=1.0`，再灌输入；约 20 s 后有估计输出。  

细节：[docs/04_deployment.md](docs/04_deployment.md)。

---

## 源码地图

| 文件 | 职责 |
|------|------|
| `main.c` | 自检、MCAN、Timer、调用 `rt_step`、发估计帧 |
| `rt_estimator.*` | 就绪/断流/抽取/窗口/合理性/EWMA |
| `rt_features.*` | 8→56 特征 |
| `rt_cnn.*` | 标准化 + NPU/CPU CNN + 浮点输出头 |
| `rt_cnn_params.*` | 导出权重（勿手改） |
| `board_self_test.h` | 221 步回放与 PC 期望（勿手改） |
| `npu/` | `npu_cnn.onnx`、`mod_cnn.a`、`tvmgen_cnn.h` |

---

## 许可

应用层源码与文档：见 [LICENSE](LICENSE)（MIT）。  
`npu/` 下 TI NNC 生成文件保留其页眉版权条款。
