/*
 * rt_estimator.c —— 模块状态机：输入就绪判定、断流看门狗、启动等待、1 s 抽取、
 * 20 步窗口、调网络、合理性检查、输出滤波（EWMA 1 s）和保持。
 *
 * 和训练、离线评估用的 Python 参考逐行对应；PC 上逐拍核对过（见 说明.md）。
 */
#include <math.h>
#include <string.h>
#include "rt_estimator.h"
#include "rt_features.h"
#include "rt_cnn.h"

#define RT_DECIMATION     10L          /* 每 10 次调用走一个模型步（1 s） */
#define RT_FROZEN_CALLS   12L          /* 8 路输入连续这么多次完全不变就当作断流 */
#define RT_SETTLE_CALLS   10L          /* 新数据流开始后先等这么多次调用 */
#define RT_OUT_ALPHA      0.632120559F /* 输出 EWMA，τ = 1 s：1 − exp(−1) */
#define RT_SANE_LO_C      -60.0F       /* 网络输出超出这个范围就当作数值故障 */
#define RT_SANE_HI_C      250.0F

typedef struct {
    int32_t decim;                      /* 距下一个模型步还差几次调用 */
    int32_t fill;                       /* 窗口里已有几步 */
    float buf[RT_WINDOW][RT_N_FEAT];    /* 最近 20 步的特征，buf[0] 最旧 */
    uint16_t hasOut, lastOk;            /* 出过有效值；最近一次网络输出合理 */
    float yEwma, yHold, yRawLast;
} estimator_state_t;

static estimator_state_t S;
static uint16_t streamActive = 0U;     /* 数据流正在进行（已过启动等待） */
static float uPrev[RT_N_IN];            /* 上一次调用的输入，给看门狗比较 */
static int32_t frozenN = 0L, settleN = 0L;

static void fresh_state(void)
{
    memset(&S, 0, sizeof(S));
    S.yRawLast = NAN;
    rt_features_reset();
}

void rt_reset(void)
{
    fresh_state();
    streamActive = 0U;
    memset(uPrev, 0, sizeof(uPrev));
    frozenN = 0L;
    settleN = 0L;
}

/* 输入就绪：(1) 每一路都是有限值——带超时监视的 CAN 层可以写 NaN 表示报文丢了；
 * (2) 不是全 0——CAN 拆包层收到第一帧之前各路都是 0，表示“还没收到”。 */
static uint16_t inputs_ready(const float *u)
{
    uint16_t i, allZero = 1U;
    for (i = 0; i < RT_N_IN; ++i) {
        if (!isfinite(u[i])) return 0U;
        if (u[i] != 0.0F) allZero = 0U;
    }
    return allZero ? 0U : 1U;
}

/*
 * 每 100 ms 调用一次。返回约定：
 *   valid=0 且 y[0]=0  → 「此刻没有可用估计」，不要把 0 当成 0 ℃；
 *   valid=1            → y[0] 为滤波后的 T15 ℃，可发 CAN / 参与保护；
 *   raw[0]             → 最近一次网络原始输出（诊断），尚无则为 NaN。
 */
void rt_step(const float *u, float *y, uint16_t *valid, float *raw)
{
    uint16_t ready = inputs_ready(u), same, updated = 0U;
    int32_t i;
    float feat[RT_N_FEAT], yRaw;

    if (ready) {
        /* 断流看门狗：收不到帧的接收层会一直交出最后的值；正常运转的总线上
         * 8 路输入不会这么久完全不变（按训练数据里最长的不变段定的）。
         * 若台架真出现 >1.2 s 电气量完全恒定，可能误判——多工况报告里需说明。 */
        same = 1U;
        for (i = 0; i < (int32_t)RT_N_IN; ++i)
            if (u[i] != uPrev[i]) same = 0U;
        if (same) {
            if (frozenN < RT_FROZEN_CALLS) ++frozenN;
        } else {
            frozenN = 0L;
        }
        memcpy(uPrev, u, sizeof(uPrev));
        if (frozenN >= RT_FROZEN_CALLS) ready = 0U;
    } else {
        frozenN = 0L;
        memset(uPrev, 0, sizeof(uPrev));
    }

    if (!ready) {
        /* 断流或还没开始：清空全部状态，旧的历史不带进下一段数据 */
        if (streamActive) fresh_state();
        streamActive = 0U;
        settleN = 0L;
        y[0] = 0.0F;
        raw[0] = NAN;
        *valid = 0U;
        return;
    }
    if (!streamActive && settleN < RT_SETTLE_CALLS) {
        /* 启动等待：各路输入来自不同的 CAN 帧，新数据流的头几次调用可能有的到了、
         * 有的还是 0，等 10 次调用（1 s）再取第一个样本。 */
        ++settleN;
        y[0] = 0.0F;
        raw[0] = NAN;
        *valid = 0U;
        return;
    }
    streamActive = 1U;

    if (S.decim == 0L) {
        /* 模型步：就绪后的第一次调用，之后每 10 次一次；取当前这一次的输入（点采样，不做块平均） */
        S.decim = RT_DECIMATION - 1L;
        updated = 1U;
        rt_features_step(u, feat);
        /* 窗口左移：buf[0] 最旧，buf[RT_WINDOW-1] 最新 */
        memmove(&S.buf[0][0], &S.buf[1][0], sizeof(float) * (RT_WINDOW - 1U) * RT_N_FEAT);
        memcpy(&S.buf[RT_WINDOW - 1U][0], feat, sizeof(feat));
        if (S.fill < (int32_t)RT_WINDOW) ++S.fill;
    } else {
        --S.decim;
    }

    if (updated && S.fill >= (int32_t)RT_WINDOW) {
        /* 窗口已满：跑 CNN；此后每个模型步更新一次，中间 100 ms 调用保持 yHold */
        yRaw = rt_cnn_forward(&S.buf[0][0]);
        S.yRawLast = yRaw;
        if (isfinite(yRaw) && yRaw > RT_SANE_LO_C && yRaw < RT_SANE_HI_C) {
            if (!S.hasOut) S.yEwma = yRaw;                      /* 首样本：直接灌入，避免从 0 爬升 */
            else S.yEwma += RT_OUT_ALPHA * (yRaw - S.yEwma);   /* α = 1−e^{−1}，τ=1 s */
            S.yHold = S.yEwma;
            S.hasOut = 1U;
            S.lastOk = 1U;
        } else {
            /* 保留上一个好的值，但标成无效，让数值故障看得见而不是被悄悄保持 */
            S.lastOk = 0U;
        }
    }

    y[0] = S.hasOut ? S.yHold : 0.0F;
    raw[0] = S.yRawLast;
    *valid = (S.hasOut && S.lastOk) ? 1U : 0U;
}
