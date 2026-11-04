# 部署说明（CCS / F28P550 / NPU）

## 1. 环境要求

| 组件 | 建议版本 / 说明 |
|------|-----------------|
| IDE | Code Composer Studio（支持 F28P55x） |
| 器件支持 | C2000Ware（工程变量 `C2000WARE_ROOT` → `COM_TI_C2000WARE_INSTALL_DIR`） |
| 器件 | TMS320F28P550SJ9 LaunchPad（见 `targetConfigs/`） |
| 编译宏 | 工程已定义 **`RT_NPU`**（走片上 NPU）；去掉后可用 CPU 整数等价路径 |
| 工具链 | TI C2000（NNC 生成库注明 cl2000 / eabi / fpu32） |

## 2. 导入工程

1. 克隆本仓库（或解压开源包），根目录即为 CCS 工程（含 `.project` / `.cproject` / `.ccsproject`）。  
2. CCS：*File → Import → CCS Projects*，选择本仓库根目录。  
3. 确认 `C2000WARE_ROOT` 指向本机 C2000Ware 安装路径。  
4. `c2000ware_sources.c` 通过 `#include "device.c"`（及 `RT_NPU` 下的 `f28p55x_npu.c`）编入工程，一般无需再手工添加这两个源文件链接。

## 3. 关键源文件清单

| 文件 | 是否必须 | 说明 |
|------|----------|------|
| `main.c` | 是 | 板级初始化、CAN、定时、自检、主循环 |
| `rt_estimator.c` | 是 | 估计状态机 |
| `rt_features.c` | 是 | 特征 |
| `rt_cnn.c` | 是 | 前向（含 NPU/CPU 分支） |
| `rt_cnn_params.c` | 是 | 权重与标准化常数（勿手改） |
| `board_self_test.h` | 是 | 上电自检数据（勿手改） |
| `model_flash_lnk.cmd` | 是 | Flash/RAM 布局；含 NPU `.rodata.tvm` / `.bss.noinit.tvm` |
| `npu/mod_cnn.a` | `RT_NPU` 时必须 | NNC 生成静态库 |
| `npu/tvmgen_cnn.h` | `RT_NPU` 时必须 | 推理 API |
| `npu/npu_cnn.onnx` | 归档 | 转换用模型；运行时不直接加载 |
| `CPU1_FLASH/*.out` | 可选 | 已构建固件，可直接烧录对照 |

## 4. 构建与烧录

1. 配置为 **CPU1_FLASH**（与仓库内产物目录名一致）。  
2. *Project → Build*。  
3. 连接 LaunchPad，使用 `targetConfigs/TMS320F28P550SJ9_LaunchPad.ccxml`。  
4. 烧录 `.out`，复位运行。  
5. 上电后若自检通过，总线出现 **`0x555 = 1.0`** 就绪帧，再开始灌输入。

### 预构建产物

路径：`CPU1_FLASH/rotor_temp_cnn_npu_T15_T01_T03_T08_f28p550.out`  
同目录 `.map` / `_linkInfo.xml` 可用于核对段地址与 NPU 工作区是否落在 GS RAM。

## 5. 现场接线检查清单

- [ ] CANH/CANL 与收发器正确；终端电阻按总线规范  
- [ ] GPIO4/5 与原理图一致（TX/RX）  
- [ ] 比特率双方均为 **100 kbit/s** 经典 CAN  
- [ ] 输入 ID/DLC/端序与 [02_signal_chain.md](02_signal_chain.md) 一致  
- [ ] dq 坐标与训练台架一致  

## 6. 集成到自有控制工程

若只需估计模块、不用本仓库 `main.c`：

1. 将 `rt_*.c/h`、`board_self_test.h`（若仍要自检）、`npu/`、链接脚本中的 TVM 段加入你的工程。  
2. 在自己的 **100 ms**（或同周期）任务中调用：

```c
rt_reset();   /* 上电或新数据段 */
/* 每个调用周期： */
rt_step(u, y, &valid, raw);
if (valid) {
    /* 使用 y[0] 作为 T15 ℃ */
}
```

3. CAN 层只负责填充 `u[8]`；短时缺帧应 **保持上一值**，不要填 0；确认新流开始时再 `rt_reset()` 或发 `0x107`。

## 7. 性能观察

运行后在 CCS Expressions 查看：

- `g_board_last_inference_us` / `g_board_max_inference_cycles`  
- `g_board_self_test_max_error_c`（应 ≤ 0.1 ℃ 容差）  

自检阶段会连续跑 221 次调用，同时锻炼 NPU 路径。

## 8. 常见问题

| 现象 | 可能原因 |
|------|----------|
| 无 `0x555` | 自检失败：看 `g_board_self_test_error_code` |
| 长期 `valid=0` | 输入未齐、全 0、断流看门狗、或窗口未满 20 s |
| 估计偏差大 | dq 标度/零位不一致；温度通道错位；未等启动等待 |
| 链接失败找不到 TVM 符号 | 未链 `npu/mod_cnn.a` 或未定义 `RT_NPU` 却包含了 NPU 头 |
| NPU 卡死 | 中断未开；检查 `NPU_enableInterrupt` 与 NNC 首次登记 |
