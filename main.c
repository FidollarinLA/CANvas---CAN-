/*
 * main.c —— F28P550 板级运行层：上电自检、CAN 收发、100 ms 定时调用估计模块
 *
 * CAN 输入（默认 CAN_INPUT_USE_FCN = 1，经典 CAN 100 kbit/s，GPIO4 = TX，GPIO5 = RX）：
 *   0x100 DLC=8：有符号大端 20/20/24 位，Id / Iq / 转速，各乘 0.001 → u[0] A、u[1] A、u[4] rpm
 *   0x110 DLC=8：有符号大端 20/20/24 位，Ud / Uq / 转矩（转矩不用），各乘 0.001 → u[2] V、u[3] V
 *   0x120 DLC=4：小端 float32，T01 → u[5] ℃
 *   0x121 DLC=4：小端 float32，T03 → u[6] ℃
 *   0x122 DLC=4：小端 float32，T08 → u[7] ℃
 * CAN_INPUT_USE_FCN = 0 时改用回放协议：小端 float32，两路输入一帧，按 u 的顺序：
 *   0x100：u[0] u[1]；0x101：u[2] u[3]；0x102：u[4] u[5]；0x103：u[6] u[7]
 * 共用：
 *   0x106 DLC=4：可选，真实转子温度（小端 float32），板端原样转发到 0x181，不进模型
 *   0x107：新一段数据开始：清空模块状态（滤波器、窗口），输入清零
 *   0x180 DLC=4：转子温度估计（小端 float32，℃），valid 时每次调用（每 100 ms）发一帧
 *   0x555 DLC=4：上电自检通过后的就绪帧，值 1.0
 *
 * 调用方式：CPU Timer0 每 100 ms 置一次标志，main 的后台循环用 CAN 中断里存下的
 * 最新值调用一次 rt_step()（和控制器里定时任务调用模块的方式一样）。
 * CAN 中断只拆帧、存值，不在中断里跑网络。
 *
 * 上电自检：回放一段记录的前 221 次调用，在第 201、211、221 次调用处和 PC 上同一份
 * 代码算出的结果对照（见 board_self_test.h），偏差超过 0.1 ℃ 就不发就绪帧。
 * 定义了 RT_NPU 时这一步同时核对了 NPU 算出的和 PC 上的整数实现是否一致。
 *
 * CCS 里看这些变量（Expressions 窗口）：
 *   g_board_self_test_passed / g_board_self_test_error_code / g_board_self_test_max_error_c  自检结果
 *   g_can_u[]  各路输入的最新值；g_can_input_frames[]  各路收到的帧数；g_can_bad_frame_count  坏帧数
 *   g_board_last_valid / g_board_last_temperature_c  最近一次调用的输出
 *   g_board_last_inference_us / g_board_max_inference_cycles  一次调用的耗时
 */
#include <math.h>
#include <string.h>
#include "driverlib.h"
#include "device.h"
#include "rt_estimator.h"
#include "board_self_test.h"

#ifndef CAN_INPUT_USE_FCN
#define CAN_INPUT_USE_FCN           1U
#endif
#define CAN_FCN_CURRENT_RX_ID       0x100U
#define CAN_FCN_VOLTAGE_RX_ID       0x110U
#define CAN_TEMP_RX_ID_T01          0x120U
#define CAN_TEMP_RX_ID_T03          0x121U
#define CAN_TEMP_RX_ID_T08          0x122U
#define CAN_TRUE_RX_ID              0x106U
#define CAN_PRED_TX_ID              0x180U
#define CAN_TRUE_TX_ID              0x181U
#define CAN_PROFILE_RESET_RX_ID     0x107U
#define CAN_READY_TX_ID             0x555U
#define CAN_REPLAY_FIRST_RX_ID      0x100U
#define CAN_REPLAY_FRAMES           4U
#define CAN_BASE                    MCANA_DRIVER_BASE
#define CAN_TX_BUF_PRED             0U          /* 发送缓冲区：估计值 */
#define CAN_TX_BUF_TRUE             1U          /* 发送缓冲区：转发的真实温度 */
#define CAN_TX_BUF_READY            2U          /* 发送缓冲区：就绪帧 */
#define CAN_TX_BUFS                 3U

volatile uint32_t g_can_rx_frame_count = 0UL;
volatile uint32_t g_can_bad_frame_count = 0UL;
volatile uint32_t g_can_bus_off_count = 0UL;
volatile uint32_t g_can_prediction_tx_count = 0UL;
volatile float g_can_u[RT_N_IN];                  /* 各路输入的最新值；收到第一帧之前是 0 */
volatile uint32_t g_can_input_frames[RT_N_IN];    /* 各路收到的帧数 */
volatile float g_can_true_c;
static volatile uint16_t g_can_true_pending;
static volatile uint16_t g_can_reset_requested = 0U;
static volatile uint16_t g_tick = 0U;

volatile uint16_t g_board_boot_finished = 0U;
volatile uint16_t g_board_self_test_passed = 0U;
volatile uint16_t g_board_self_test_error_code = 0U;
volatile float g_board_self_test_max_error_c = 0.0F;
volatile uint32_t g_board_call_count = 0UL;
volatile uint16_t g_board_last_valid = 0U;
volatile float g_board_last_temperature_c;
volatile uint32_t g_board_last_inference_cycles = 0UL;
volatile uint32_t g_board_max_inference_cycles = 0UL;
volatile uint64_t g_board_total_inference_cycles = 0ULL;
volatile uint32_t g_board_inference_count = 0UL;
volatile float g_board_last_inference_us = 0.0F;

/* MCAN 的 data[] 每个元素的低 8 位存一个字节；IEEE754 float32 按小端拼成 32 位。 */
static float can_unpack_float(const uint16_t data[4])
{
    union { uint32_t bits; float value; } converter;
    converter.bits = ((uint32_t)(data[0] & 0x00FFU)) |
                     ((uint32_t)(data[1] & 0x00FFU) << 8U) |
                     ((uint32_t)(data[2] & 0x00FFU) << 16U) |
                     ((uint32_t)(data[3] & 0x00FFU) << 24U);
    return converter.value;
}

#if CAN_INPUT_USE_FCN
/*
 * 20/20/24 位有符号大端拆包（现场 FCN 协议）。
 * 字节流：| a19..12 | a11..4 | a3..0 b19..16 | b15..8 | b7..0 | c23..16 | c15..8 | c7..0 |
 * 先扩成 32 位再移位：C28x 的 unsigned int 只有 16 位，直接移位会丢高位。
 * 符号：20 位用 2^20、24 位用 2^24 做补码折回；工程单位 = 整数 × 0.001。
 */
static void can_unpack_fcn(const uint16_t data[8], float values[3])
{
    uint32_t a = ((uint32_t)(data[0] & 0xFFU) << 12U) |
                 ((uint32_t)(data[1] & 0xFFU) << 4U) |
                 ((uint32_t)(data[2] & 0xFFU) >> 4U);
    uint32_t b = ((uint32_t)(data[2] & 0x0FU) << 16U) |
                 ((uint32_t)(data[3] & 0xFFU) << 8U) |
                 (uint32_t)(data[4] & 0xFFU);
    uint32_t c = ((uint32_t)(data[5] & 0xFFU) << 16U) |
                 ((uint32_t)(data[6] & 0xFFU) << 8U) |
                 (uint32_t)(data[7] & 0xFFU);
    int32_t sa = (int32_t)a;
    int32_t sb = (int32_t)b;
    int32_t sc = (int32_t)c;
    if((a & 0x80000UL) != 0UL) sa -= 1048576L;     /* 2^20：20 位数符号扩展 */
    if((b & 0x80000UL) != 0UL) sb -= 1048576L;
    if((c & 0x800000UL) != 0UL) sc -= 16777216L;  /* 2^24：24 位数符号扩展 */
    values[0] = (float)sa * 0.001F;
    values[1] = (float)sb * 0.001F;
    values[2] = (float)sc * 0.001F;
}
#endif

/* 与 can_unpack_float 对称：把 float32 按小端拆进 MCAN data[] 低字节。 */
static void can_pack_float(float value, uint16_t data[4])
{
    union { uint32_t bits; float value; } converter;
    converter.value = value;
    data[0] = (uint16_t)(converter.bits & 0xFFUL);
    data[1] = (uint16_t)((converter.bits >> 8U) & 0xFFUL);
    data[2] = (uint16_t)((converter.bits >> 16U) & 0xFFUL);
    data[3] = (uint16_t)((converter.bits >> 24U) & 0xFFUL);
}

/*
 * 发一帧 4 字节 float（标准帧）。
 * 若该 TX buffer 上一帧尚未发出（ReqPend 置位），直接丢弃本帧，避免阻塞实时环；
 * 估计值下一拍 100 ms 还会再发，短暂丢失可接受。
 */
static void can_transmit_float(uint16_t can_id, float value, uint16_t tx_buffer)
{
    MCAN_TxBufElement frame;
    if((MCAN_getTxBufReqPend(CAN_BASE) & (1UL << tx_buffer)) != 0UL) return;
    memset(&frame, 0, sizeof(frame));
    frame.id = ((uint32_t)can_id) << 18U; /* MCAN 标准 ID 放在 [28:18] */
    frame.dlc = 4U;
    frame.xtd = 0U;
    frame.rtr = 0U;
    frame.fdf = 0U;
    frame.brs = 0U;
    can_pack_float(value, frame.data);
    MCAN_writeMsgRam(CAN_BASE, MCAN_MEM_TYPE_BUF, tx_buffer, &frame);
    MCAN_txBufAddReq(CAN_BASE, tx_buffer);
}

/*
 * CAN 中断里调用：只拆帧、写 g_can_u[] / 真值 / 复位标志。
 * 禁止在此路径调用 rt_step / NPU：ISR 必须短，推理放到 100 ms 后台任务。
 */
static void can_handle_frame(const MCAN_RxBufElement *frame)
{
    uint32_t id;
    if((frame->xtd != 0U) || (frame->rtr != 0U) || (frame->fdf != 0U)) {
        ++g_can_bad_frame_count;
        return;
    }
    id = frame->id >> 18U;
    ++g_can_rx_frame_count;
    if(g_can_reset_requested != 0U) return;

#if CAN_INPUT_USE_FCN
    if((id == CAN_FCN_CURRENT_RX_ID) || (id == CAN_FCN_VOLTAGE_RX_ID)) {
        float values[3];
        if(frame->dlc != 8U) { ++g_can_bad_frame_count; return; }
        can_unpack_fcn(frame->data, values);
        if(id == CAN_FCN_CURRENT_RX_ID) {
            g_can_u[RT_PORT_ID] = values[0];
            g_can_u[RT_PORT_IQ] = values[1];
            g_can_u[RT_PORT_SPEED] = values[2];
            ++g_can_input_frames[RT_PORT_ID];
            ++g_can_input_frames[RT_PORT_IQ];
            ++g_can_input_frames[RT_PORT_SPEED];
        } else {
            g_can_u[RT_PORT_UD] = values[0];
            g_can_u[RT_PORT_UQ] = values[1];
            ++g_can_input_frames[RT_PORT_UD];
            ++g_can_input_frames[RT_PORT_UQ];
        }
        return;
    }
    if(id == CAN_TEMP_RX_ID_T01) {
        if(frame->dlc != 4U) { ++g_can_bad_frame_count; return; }
        g_can_u[RT_PORT_T01] = can_unpack_float(frame->data);
        ++g_can_input_frames[RT_PORT_T01];
        return;
    }
    if(id == CAN_TEMP_RX_ID_T03) {
        if(frame->dlc != 4U) { ++g_can_bad_frame_count; return; }
        g_can_u[RT_PORT_T03] = can_unpack_float(frame->data);
        ++g_can_input_frames[RT_PORT_T03];
        return;
    }
    if(id == CAN_TEMP_RX_ID_T08) {
        if(frame->dlc != 4U) { ++g_can_bad_frame_count; return; }
        g_can_u[RT_PORT_T08] = can_unpack_float(frame->data);
        ++g_can_input_frames[RT_PORT_T08];
        return;
    }
#else
    if((id >= CAN_REPLAY_FIRST_RX_ID) && (id < CAN_REPLAY_FIRST_RX_ID + CAN_REPLAY_FRAMES)) {
        uint16_t first = (uint16_t)((id - CAN_REPLAY_FIRST_RX_ID) * 2U);
        if(frame->dlc != 8U) { ++g_can_bad_frame_count; return; }
        g_can_u[first] = can_unpack_float(&frame->data[0]);
        g_can_u[first + 1U] = can_unpack_float(&frame->data[4]);
        ++g_can_input_frames[first];
        ++g_can_input_frames[first + 1U];
        return;
    }
#endif
    if(id == CAN_TRUE_RX_ID) {
        if(frame->dlc != 4U) { ++g_can_bad_frame_count; return; }
        g_can_true_c = can_unpack_float(&frame->data[0]);
        g_can_true_pending = 1U;
        return;
    }
    if(id == CAN_PROFILE_RESET_RX_ID) {
        g_can_reset_requested = 1U;
        return;
    }
    ++g_can_bad_frame_count;
}

/* MCAN Line0：新报文进 FIFO0 后逐条分拣；Bus-Off 只计数，恢复策略留给上层。 */
__interrupt void mcan_line0_isr(void)
{
    uint32_t status = MCAN_getIntrStatus(CAN_BASE);
    if((status & MCAN_INTR_SRC_RX_FIFO0_NEW_MSG) != 0UL) {
        MCAN_RxFIFOStatus fifo;
        fifo.num = MCAN_RX_FIFO_NUM_0;
        MCAN_getRxFIFOStatus(CAN_BASE, &fifo);
        while(fifo.fillLvl != 0U) {
            MCAN_RxBufElement frame;
            MCAN_readMsgRam(CAN_BASE, MCAN_MEM_TYPE_FIFO, 0U, MCAN_RX_FIFO_NUM_0, &frame);
            can_handle_frame(&frame);
            MCAN_writeRxFIFOAck(CAN_BASE, MCAN_RX_FIFO_NUM_0, fifo.getIdx);
            MCAN_getRxFIFOStatus(CAN_BASE, &fifo);
        }
    }
    if((status & MCAN_INTR_SRC_BUS_OFF_STATUS) != 0UL) ++g_can_bus_off_count;
    MCAN_clearIntrStatus(CAN_BASE, status);
    MCAN_clearInterrupt(CAN_BASE, 0x1U);
    Interrupt_clearACKGroup(INTERRUPT_ACK_GROUP9);
}

/* 仅置位软标志；真正的 rt_step 在 main 后台执行，避免在定时中断里跑 CNN。 */
__interrupt void timer0_isr(void)
{
    g_tick = 1U;
    Interrupt_clearACKGroup(INTERRUPT_ACK_GROUP1);
}

/* MCAN：经典 CAN 100 kbit/s，只收标准帧，全部进 FIFO0，在中断里按 ID 分拣 */
static void can_init_100k(void)
{
    MCAN_InitParams init_params;
    MCAN_BitTimingParams bit_times;
    MCAN_MsgRAMConfigParams ram;
    MCAN_StdMsgIDFilterElement filter;
    MCAN_RevisionId revision;

    GPIO_setPinConfig(GPIO_5_MCANA_RX);
    GPIO_setPadConfig(5U, GPIO_PIN_TYPE_STD);
    GPIO_setQualificationMode(5U, GPIO_QUAL_ASYNC);
    GPIO_setPinConfig(GPIO_4_MCANA_TX);
    GPIO_setPadConfig(4U, GPIO_PIN_TYPE_STD);
    GPIO_setQualificationMode(4U, GPIO_QUAL_ASYNC);

    SysCtl_setMCANClk(SYSCTL_MCANA, SYSCTL_MCANCLK_DIV_5); /* 150 MHz / 5 = 30 MHz */
    memset(&init_params, 0, sizeof(init_params));
    memset(&bit_times, 0, sizeof(bit_times));
    memset(&ram, 0, sizeof(ram));
    memset(&filter, 0, sizeof(filter));

    init_params.fdMode = false;
    init_params.brsEnable = false;
    init_params.wdcPreload = 255U;
    init_params.tdcConfig.tdcf = 10U;
    init_params.tdcConfig.tdco = 6U;

    /* 30 MHz MCAN 时钟下的 100 kbit/s，和验证过的示例工程一样 */
    bit_times.nomRatePrescalar = 19U;
    bit_times.nomTimeSeg1 = 10U;
    bit_times.nomTimeSeg2 = 2U;
    bit_times.nomSynchJumpWidth = 2U;
    bit_times.dataRatePrescalar = 3U;
    bit_times.dataTimeSeg1 = 4U;
    bit_times.dataTimeSeg2 = 3U;
    bit_times.dataSynchJumpWidth = 3U;

    ram.flssa = 0U;
    ram.lss = 1U;
    ram.flesa = 4U;
    ram.lse = 0U;
    ram.rxFIFO0startAddr = 12U;
    ram.rxFIFO0size = 8U;
    ram.rxFIFO0waterMark = 0U;
    ram.rxFIFO0OpMode = 0U;
    ram.rxFIFO0ElemSize = 0U;
    ram.rxFIFO1startAddr = 140U;
    ram.rxFIFO1size = 0U;
    ram.rxBufStartAddr = 140U;
    ram.rxBufElemSize = 7U;
    ram.txStartAddr = 140U;
    ram.txBufNum = CAN_TX_BUFS;
    ram.txFIFOSize = 0U;
    ram.txBufMode = 0U;
    ram.txBufElemSize = 0U;
    ram.txEventFIFOStartAddr = 140U + 4U * CAN_TX_BUFS;
    ram.txEventFIFOSize = 0U;

    MCAN_getRevisionId(CAN_BASE, &revision);
    while(MCAN_isMemInitDone(CAN_BASE) == 0U) { }
    MCAN_setOpMode(CAN_BASE, MCAN_OPERATION_MODE_SW_INIT);
    while(MCAN_getOpMode(CAN_BASE) != MCAN_OPERATION_MODE_SW_INIT) { }
    MCAN_init(CAN_BASE, &init_params);
    MCAN_setBitTime(CAN_BASE, &bit_times);
    MCAN_msgRAMConfig(CAN_BASE, &ram);

    filter.sft = MCAN_STDFILT_CLASSIC;
    filter.sfec = MCAN_STDFILTEC_FIFO0;
    filter.sfid1 = 0U;
    filter.sfid2 = 0U; /* 掩码 0：所有标准帧都收，在中断里按 ID 判断 */
    MCAN_addStdMsgIDFilter(CAN_BASE, 0U, &filter);

    MCAN_lpbkModeEnable(CAN_BASE, MCAN_LPBK_MODE_EXTERNAL, false);
    MCAN_setOpMode(CAN_BASE, MCAN_OPERATION_MODE_NORMAL);
    while(MCAN_getOpMode(CAN_BASE) != MCAN_OPERATION_MODE_NORMAL) { }
    MCAN_enableIntr(CAN_BASE, MCAN_IR_BO_MASK | MCAN_IR_RF0N_MASK, 1U);
    MCAN_selectIntrLine(CAN_BASE, MCAN_IR_BO_MASK | MCAN_IR_RF0N_MASK, MCAN_INTR_LINE_NUM_0);
    MCAN_enableIntrLine(CAN_BASE, MCAN_INTR_LINE_NUM_0, 1U);
}

/* CPU Timer2 自由向下计数，用来量耗时：150 MHz 下 周期数 / 150 = 微秒 */
static void board_timer2_init(void)
{
    CPUTimer_stopTimer(CPUTIMER2_BASE);
    CPUTimer_setPreScaler(CPUTIMER2_BASE, 0U);
    CPUTimer_setPeriod(CPUTIMER2_BASE, 0xFFFFFFFFUL);
    CPUTimer_reloadTimerCounter(CPUTIMER2_BASE);
    CPUTimer_startTimer(CPUTIMER2_BASE);
}

/* CPU Timer0：每个调用周期（100 ms）一次中断 */
static void board_timer0_init(void)
{
    CPUTimer_stopTimer(CPUTIMER0_BASE);
    CPUTimer_setPreScaler(CPUTIMER0_BASE, 0U);
    CPUTimer_setPeriod(CPUTIMER0_BASE, (uint32_t)(DEVICE_SYSCLK_FREQ / 1000UL) * (uint32_t)RT_CALL_PERIOD_MS - 1UL);
    CPUTimer_reloadTimerCounter(CPUTIMER0_BASE);
    CPUTimer_enableInterrupt(CPUTIMER0_BASE);
    Interrupt_register(INT_TIMER0, &timer0_isr);
    Interrupt_enable(INT_TIMER0);
    CPUTimer_startTimer(CPUTIMER0_BASE);
}

/*
 * 调一次估计模块并统计耗时（Timer2 向下计数：begin − end = 周期数）。
 * 此处即「AI 进入实时环」的调用点：与控制器 100 ms 任务同拍。
 */
static void board_step_timed(const float *u, float *y, uint16_t *valid, float *raw)
{
    uint32_t begin = CPUTimer_getTimerCount(CPUTIMER2_BASE);
    uint32_t elapsed;
    rt_step(u, y, valid, raw);
    elapsed = begin - CPUTimer_getTimerCount(CPUTIMER2_BASE);
    g_board_last_inference_cycles = elapsed;
    if(elapsed > g_board_max_inference_cycles) g_board_max_inference_cycles = elapsed;
    g_board_total_inference_cycles += (uint64_t)elapsed;
    ++g_board_inference_count;
    g_board_last_inference_us = ((float)elapsed * 1000000.0F) / (float)DEVICE_SYSCLK_FREQ;
}

#define BOARD_ENABLE_BOOT_SELF_TEST  1U
#define BOARD_TOLERANCE_C            0.1F

/*
 * 上电自检：回放验证场前 221 次调用，在第 201/211/221 次与 PC 同源结果比对本。
 * 定义 RT_NPU 时，同时验证 NPU 路径与 PC 整数实现一致；失败则不发就绪帧。
 */
static void board_run_boot_self_test(void)
{
    uint32_t row;
    uint16_t check = 0U, valid;
    float y[RT_N_OUT], raw[RT_N_OUT], err;
    rt_reset();
    g_board_self_test_passed = 0U;
    g_board_self_test_error_code = 0U;
    g_board_self_test_max_error_c = 0.0F;
    g_board_max_inference_cycles = 0UL;
    g_board_total_inference_cycles = 0ULL;
    g_board_inference_count = 0UL;
    for(row = 0; row < BOARD_SELF_TEST_ROWS; ++row) {
        board_step_timed(g_board_self_test_u[row], y, &valid, raw);
        if(check < BOARD_SELF_TEST_CHECKS && row == g_board_check_calls[check]) {
            if(valid != 1U) {
                g_board_self_test_error_code = 1U; /* PC 上这里有有效输出，板上没有 */
                return;
            }
            err = fabsf(y[0] - g_board_expected_c[check]);
            if(err > g_board_self_test_max_error_c) g_board_self_test_max_error_c = err;
            ++check;
        }
    }
    if(check != BOARD_SELF_TEST_CHECKS) {
        g_board_self_test_error_code = 2U; /* 检查点不够 */
        return;
    }
    if(g_board_self_test_max_error_c > BOARD_TOLERANCE_C) {
        g_board_self_test_error_code = 3U; /* 板上和 PC 差得超过容差 */
        return;
    }
    g_board_self_test_passed = 1U;
}

/* 上电：自检，然后启动 CAN 和调用周期定时器 */
void board_init(void)
{
    Device_init();
    Device_initGPIO();
    Interrupt_initModule();
    Interrupt_initVectorTable();
#ifdef RT_NPU
    /* 打开 NPU 的中断屏蔽位；中断服务函数由 NNC 库在第一次推理时自己登记 */
    extern void NPU_enableInterrupt(void);
    NPU_enableInterrupt();
#endif
    Interrupt_register(INT_MCANA_0, &mcan_line0_isr);
    Interrupt_enable(INT_MCANA_0);
    board_timer2_init();

#if BOARD_ENABLE_BOOT_SELF_TEST
    board_run_boot_self_test();
#else
    g_board_self_test_passed = 1U;
#endif
    /* 自检会留下状态：真正的数据流从干净的状态开始 */
    rt_reset();
    memset((void *)g_can_u, 0, sizeof(g_can_u));
    memset((void *)g_can_input_frames, 0, sizeof(g_can_input_frames));
    g_can_true_pending = 0U;
    g_board_last_temperature_c = 0.0F;
    can_init_100k();
    board_timer0_init();
    EINT;
    ERTM;
    g_board_boot_finished = 1U;
    if(g_board_self_test_passed) can_transmit_float(CAN_READY_TX_ID, 1.0F, CAN_TX_BUF_READY);
}

/*
 * 后台循环的一轮（实时估计环本体）：
 *   1) 处理 0x107 复位：清空估计状态与输入缓存；
 *   2) 每个 Timer0 节拍：关中断快照 g_can_u → rt_step → 有效则发 0x180；
 *   3) 若有 0x106 真值待转发，发 0x181（不进模型）。
 * main() 空转调用本函数；与“控制器里定时任务调用模块”的集成方式一致。
 */
void board_loop_once(void)
{
    float u[RT_N_IN], y[RT_N_OUT], raw[RT_N_OUT];
    uint16_t valid, i;

    if(g_can_reset_requested != 0U) {
        DINT;
        rt_reset();
        for(i = 0; i < RT_N_IN; ++i) g_can_u[i] = 0.0F;
        g_can_reset_requested = 0U;
        EINT;
    }
    if(g_tick != 0U) {
        g_tick = 0U;
        DINT;
        for(i = 0; i < RT_N_IN; ++i) u[i] = g_can_u[i];   /* 关中断拷一份，避免拿到拆了一半的帧 */
        EINT;
        board_step_timed(u, y, &valid, raw);
        ++g_board_call_count;
        g_board_last_valid = valid;
        if(valid) {
            g_board_last_temperature_c = y[0];
            can_transmit_float(CAN_PRED_TX_ID, y[0], CAN_TX_BUF_PRED);
            ++g_can_prediction_tx_count;
        }
    }
    if(g_can_true_pending != 0U) {
        float t = g_can_true_c;
        g_can_true_pending = 0U;
        can_transmit_float(CAN_TRUE_TX_ID, t, CAN_TX_BUF_TRUE);
    }
}

void main(void)
{
    board_init();
    for(;;) board_loop_once();
}
