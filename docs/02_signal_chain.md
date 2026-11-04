# 信号链路说明与采样/控制对照表

## 1. 端到端信号链路

```text
台架传感器 / 逆变器控制采样
        │
        ▼
控制器整理为工程单位（A / V / rpm / ℃）
        │  经典 CAN 100 kbit/s
        ▼
F28P550  MCANA  GPIO4=TX / GPIO5=RX
        │  Line0 中断 → can_handle_frame()
        ▼
g_can_u[8]  （各路最新值；缺帧保持上次，勿写 0）
        │  Timer0 100 ms → board_loop_once()
        ▼
rt_step() → 特征 → 窗口 → CNN(NPU) → EWMA
        │
        ├─ y[0], valid  ──CAN 0x180──► 上位机 / 保护逻辑
        └─ 可选真值 0x106 ──转发 0x181──► 对比显示
```

**关键设计**：CAN 中断路径 **禁止** 跑网络；推理只在 100 ms 后台任务中执行，避免拖长 ISR、保证实时可调度。

## 2. 输入端口 ↔ 物理量 ↔ CAN 帧（默认 `CAN_INPUT_USE_FCN=1`）

| `u[]` 下标 | 宏 | 物理量 | 单位 | CAN ID | DLC | 编码 | 电路/来源说明 |
|-----------|-----|--------|------|--------|-----|------|----------------|
| 0 | `RT_PORT_ID` | d 轴电流 Id | A | `0x100` | 8 | 有符号大端 20/20/24 位 ×0.001 | 与台架同一 dq 坐标与标度；来自逆变器电流采样经 Clark/Park |
| 1 | `RT_PORT_IQ` | q 轴电流 Iq | A | `0x100` | 8 | 同上第 2 段 | 同上 |
| 2 | `RT_PORT_UD` | d 轴电压 Ud | V | `0x110` | 8 | 同上 | 逆变器电压指令或重构电压 |
| 3 | `RT_PORT_UQ` | q 轴电压 Uq | V | `0x110` | 8 | 同上 | 同上；帧内第 3 段转矩字段本模块不使用 |
| 4 | `RT_PORT_SPEED` | 转速 | rpm | `0x100` | 8 | 同上第 3 段 | 编码器/旋变测速 |
| 5 | `RT_PORT_T01` | 定子温度 T01 | ℃ | `0x120` | 4 | 小端 float32 | 热电偶 T01 变送后由控制器打包 |
| 6 | `RT_PORT_T03` | 定子温度 T03 | ℃ | `0x121` | 4 | 小端 float32 | 热电偶 T03 |
| 7 | `RT_PORT_T08` | 定子温度 T08 | ℃ | `0x122` | 4 | 小端 float32 | 热电偶 T08 |

> 坐标一致性：Id/Iq/Ud/Uq 必须与训练数据同一电角度零位与同一标度。电流差约 10% 或坐标转约 15° 会使估计明显偏移（见 `rt_estimator.h`）。

## 3. 控制/诊断输出帧

| CAN ID | 方向 | 载荷 | 含义 | 与电路关系 |
|--------|------|------|------|------------|
| `0x180` | TX | float32 ℃ | T15 估计（仅 `valid=1` 时每 100 ms 发送） | 估计结果回传总线，可供保护/显示 MCU 订阅 |
| `0x181` | TX | float32 ℃ | 真值转发（收到 `0x106` 后原样转发） | 不进入模型；仅便于上位机对齐曲线 |
| `0x555` | TX | float32 `1.0` | 上电自检通过就绪帧 | 表示板端模型与 PC 对照通过，可开始灌数 |
| `0x106` | RX | float32 ℃ | 可选真实转子温度 | 不进 `rt_step` |
| `0x107` | RX | （任意） | 剖面复位：清滤波器与窗口 | 新数据段开始时由上位机触发 |

## 4. 回放协议（`CAN_INPUT_USE_FCN=0`）

小端 float32，每帧两路：

| ID | 内容 |
|----|------|
| `0x100` | u[0], u[1] |
| `0x101` | u[2], u[3] |
| `0x102` | u[4], u[5] |
| `0x103` | u[6], u[7] |

用于 PC/脚本按记录回放验证，与 FCN 现场协议二选一编译。

## 5. 板级硬件引脚

| 信号 | 引脚 / 外设 | 说明 |
|------|-------------|------|
| CAN TX | GPIO4 → MCANA_TX | LaunchPad 默认连线需与收发器一致 |
| CAN RX | GPIO5 → MCANA_RX | 异步输入限定 |
| 比特率 | 经典 CAN 100 kbit/s | MCAN 时钟 30 MHz（150/5），时序与已验证示例一致 |
| 调用节拍 | CPU Timer0 | 周期 = `RT_CALL_PERIOD_MS`（100 ms） |
| 耗时测量 | CPU Timer2 | 自由计数，换算 `g_board_last_inference_us` |
| NPU | 片上 NPU | `RT_NPU`；上电 `NPU_enableInterrupt()` |

## 6. CCS Expressions 推荐观察量

| 变量 | 含义 |
|------|------|
| `g_board_self_test_passed` | 自检是否通过 |
| `g_board_self_test_error_code` | 1 无有效输出 / 2 检查点不足 / 3 超差 |
| `g_board_self_test_max_error_c` | 自检最大绝对误差 ℃ |
| `g_can_u[]` / `g_can_input_frames[]` | 最新输入与各路帧计数 |
| `g_can_bad_frame_count` | 坏帧（扩展帧/DLC 错等） |
| `g_board_last_valid` / `g_board_last_temperature_c` | 最近一次估计 |
| `g_board_last_inference_us` | 单次 `rt_step` 耗时 |
