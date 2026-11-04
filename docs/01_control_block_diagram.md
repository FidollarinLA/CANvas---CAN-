# 控制框图

估计对象：永磁同步电机转子永磁体表面温度 **T15**。  
实现载体：TI **TMS320F28P550**（片上 NPU 跑量化 CNN；定义 `RT_NPU`）。

## 1. 系统级闭环位置（为何算「进入实时控制环」）

本模块不是离线后处理脚本，而是板端 **100 ms 周期任务** 中的在线估计器：

1. 台架/控制器经 **CAN** 连续下发 Id/Iq/Ud/Uq/转速/T01/T03/T08；
2. MCU 在 MCAN 中断中只拆帧、写最新采样缓存（不做网络推理）；
3. CPU Timer0 每 100 ms 置标志，`main` 后台循环调用 `rt_step()`；
4. 窗口满后，**同一次调用路径内** 完成特征更新 + CNN（NPU）前向 + 输出 EWMA；
5. `valid=1` 时经 CAN `0x180` 回传 T15 估计，可供上位机显示、保护逻辑或外环限功率使用。

因此 AI（量化 CNN）参与的是 **实时估计环**，而非仅离线分析。若下游把 `0x180` 接入限流/降额，即可构成完整保护闭环；本开源包提供估计器与协议，下游接入由应用配置。

```mermaid
flowchart LR
  subgraph Plant["电机 / 台架"]
    M[PMSM + 热电偶 T01/T03/T08]
  end
  subgraph Host["控制器 / 上位机"]
    CTRL[电流电压转速与温度采样]
  end
  subgraph MCU["F28P550 板端"]
    ISR[MCAN ISR<br/>仅拆帧存 g_can_u]
    T0[Timer0 100 ms]
    RT[rt_step 估计器]
    NPU[CNN on NPU]
    TX[CAN TX 0x180 T15]
  end
  M --> CTRL
  CTRL -->|CAN RX 0x100/110/120..| ISR
  T0 --> RT
  ISR -->|最新 u[8]| RT
  RT --> NPU
  NPU --> RT
  RT -->|valid| TX
  TX -->|估计温度| CTRL
```

## 2. 估计模块内部框图

```mermaid
flowchart TB
  U["u[8]: Id Iq Ud Uq n T01 T03 T08"]
  RDY{输入就绪?<br/>有限且非全 0<br/>且未断流看门狗}
  SETTLE{启动等待<br/>10 次调用}
  DECIM{抽取<br/>每 10 次调用 = 1 模型步}
  FEAT[rt_features_step<br/>→ 56 维特征]
  WIN[滑动窗口 20 步 = 20 s]
  CNN[rt_cnn_forward<br/>标准化 + NPU/CPU CNN + 浮点头]
  SANE{输出 ∈ (-60, 250) ℃?}
  EWMA[输出 EWMA τ=1 s]
  HOLD[保持 yHold]
  OUT["y / valid / raw"]

  U --> RDY
  RDY -->|否| CLR[清空状态 valid=0]
  RDY -->|是| SETTLE
  SETTLE -->|未满| WAIT[valid=0]
  SETTLE -->|满| DECIM
  DECIM -->|非模型步| HOLD
  DECIM -->|模型步| FEAT --> WIN
  WIN -->|fill&lt;20| HOLD
  WIN -->|fill≥20| CNN --> SANE
  SANE -->|是| EWMA --> HOLD
  SANE -->|否| BAD[lastOk=0 保留旧值]
  HOLD --> OUT
  BAD --> OUT
  CLR --> OUT
```

## 3. CNN 计算分工（CPU ↔ NPU）

| 步骤 | 运算 | 执行位置 |
|------|------|----------|
| ① | 特征标准化并限幅 ±3.9 | CPU 浮点（`rt_cnn.c`） |
| ② | 输入 8 bit 量化 | NNC 库 / CPU 整数等价 |
| ③④ | Conv1×3 + BN + ReLU（56→16→32） | **NPU**（`npu/mod_cnn.a`） |
| ⑤ | 全局平均池化（20→1） | NPU |
| ⑥ | FC 32→32 | NPU |
| ⑦ | FC 32→1 并反标准化为 ℃ | CPU 浮点 |

不定义 `RT_NPU` 时，②～⑥ 使用与 NPU **逐位一致** 的 CPU 整数实现，便于 PC 核对与无 NPU 芯片移植。

## 4. 与物理量的对应关系（摘要）

详见 [02_signal_chain.md](02_signal_chain.md)。核心对应：

- **采样输入**：电气 dq 量 + 三路定子温度 → CAN → `g_can_u[]` → `rt_step(u)`  
- **控制/估计输出**：T15 估计 ℃ → CAN `0x180`；可选真值转发 `0x181`  
- **时基**：调用周期 100 ms；模型步 1 s；首有效输出约第 201 次调用（≈20.1 s）
