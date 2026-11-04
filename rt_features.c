/*
 * rt_features.c —— 特征流水线（和训练用的 Python 特征代码逐行对应，单精度）
 *
 * 每个模型步（1 s）调一次 rt_features_step()。各个 EWMA 的系数 a = 1 − exp(−Δt/τ)，
 * Δt = 1 s，已经算好写成常数；递推式统一是 x̄ += a·(x − x̄)。
 */
#include <math.h>
#include <stdint.h>
#include <string.h>
#include "rt_features.h"

#define N_ELEC     5          /* Id、Iq、Ud、Uq、转速 */
#define N_TEMP     3          /* T01、T03、T08 */
#define N_LOSS     3          /* 铜耗、铁耗、转速平方 */
#define N_LOSS_TAU 5          /* 损耗代理的时间常数个数 */
#define N_SLOW     (N_ELEC + N_TEMP)
#define N_SLOW_TAU 4          /* 长时 EWMA 的时间常数个数 */
#define ROLL       5          /* 温度去尖刺：中值窗口、均值窗口都是 5 个点 */
#define AGE_TAU_S  2400.0F    /* “数据流持续时间”特征的时间常数 */

/* 短时滤波：电流、电压 τ = 0.5 s，转速 τ = 1 s */
static const float aElec[N_ELEC] = {0.864664717F, 0.864664717F, 0.864664717F, 0.864664717F, 0.632120559F};
/* 温度平滑后的 EWMA：τ = 3 s */
static const float aTemp = 0.283468689F;
/* 损耗代理：τ = 30、120、600、1800、7200 s */
static const float aLoss[N_LOSS_TAU] = {0.0327838995F, 0.00829870736F, 0.00166527855F, 0.000555401263F, 0.000138879244F};
/* 长时记忆：τ = 60、600、1800、7200 s */
static const float aSlow[N_SLOW_TAU] = {0.0165285462F, 0.00166527855F, 0.000555401263F, 0.000138879244F};

typedef struct {
    uint16_t started;                   /* 0：下一步是数据流的第一个模型步 */
    int32_t steps;                      /* 数据流已经走过的模型步数（= 秒数） */
    float e[N_ELEC];                    /* 电气量短时滤波值 */
    float tRaw[N_TEMP][ROLL];           /* 每路温度最近 5 个原始值，最旧的在前 */
    int32_t tRawN[N_TEMP];
    float tMed[N_TEMP][ROLL];           /* 每路温度最近 5 个中值 */
    int32_t tMedN[N_TEMP];
    float eT[N_TEMP];                   /* 温度平滑值 */
    float loss[N_LOSS][N_LOSS_TAU];     /* 损耗代理的 EWMA */
    float slow[N_SLOW][N_SLOW_TAU];     /* 8 路滤波值的长时 EWMA */
} features_state_t;

static features_state_t F;

void rt_features_reset(void)
{
    memset(&F, 0, sizeof(F));
}

/* 保留最近 cap 个值，最旧的在前 */
static void push_recent(float *buf, int32_t *n, float x, int32_t cap)
{
    int32_t i;
    if (*n < cap) {
        buf[*n] = x;
        ++*n;
    } else {
        for (i = 0; i < cap - 1L; ++i) buf[i] = buf[i + 1L];
        buf[cap - 1L] = x;
    }
}

/* 前 n 个值的中值；n 为偶数时取中间两个的平均（和 Python 一致） */
static float median_recent(const float *buf, int32_t n)
{
    float v[ROLL], key;
    int32_t i, j, half;
    for (i = 0; i < n; ++i) v[i] = buf[i];
    for (i = 1; i < n; ++i) {                      /* 插入排序，n ≤ 5 */
        key = v[i];
        j = i - 1L;
        while (j >= 0L && v[j] > key) { v[j + 1L] = v[j]; --j; }
        v[j + 1L] = key;
    }
    half = n / 2L;
    return (n - half * 2L == 1L) ? v[half] : 0.5F * (v[half - 1L] + v[half]);
}

static float mean_recent(const float *buf, int32_t n)
{
    float acc = 0.0F;
    int32_t i;
    for (i = 0; i < n; ++i) acc += buf[i];
    return acc / (float)n;
}

/*
 * 每个模型步（1 s）调用一次。u[] 顺序见 rt_estimator.h；feat[56] 顺序见 rt_features.h。
 * 所有 EWMA：x̄ ← x̄ + a·(x − x̄)，a = 1 − exp(−Δt/τ)，Δt=1 s，系数已预计算。
 */
void rt_features_step(const float *u, float *feat)
{
    float inst[N_LOSS], src[N_SLOW], tSmooth;
    int32_t i, j;

    /* 1. 电气量短时滤波（第一个模型步直接取当前值） */
    for (i = 0; i < N_ELEC; ++i) {
        if (!F.started) F.e[i] = u[i];
        else F.e[i] += aElec[i] * (u[i] - F.e[i]);
    }

    /* 2. 定子温度去尖刺：因果中值(5) → 因果均值(5) → EWMA 3 s。
     *    热电偶偶尔会跳一个点，中值把单点尖刺去掉，均值和 EWMA 再把量化台阶抹平。 */
    for (i = 0; i < N_TEMP; ++i) {
        push_recent(F.tRaw[i], &F.tRawN[i], u[N_ELEC + i], ROLL);
        push_recent(F.tMed[i], &F.tMedN[i], median_recent(F.tRaw[i], F.tRawN[i]), ROLL);
        tSmooth = mean_recent(F.tMed[i], F.tMedN[i]);
        if (!F.started) F.eT[i] = tSmooth;
        else F.eT[i] += aTemp * (tSmooth - F.eT[i]);
    }

    /* 3. 损耗代理，用滤波后的电气量算：
     *    铜耗 ∝ Id² + Iq²；铁耗 ∝ |n|·(Ud² + Uq²)（电压 ∝ 磁链 × 转速）；风摩等 ∝ n²。
     *    每个代理再取 5 个时间常数的 EWMA，近似转子“最近一段时间吸收了多少热”。
     *    从 0 起步：上电时没有历史，第一步是从 0 往当前值走一步。 */
    inst[0] = F.e[0] * F.e[0] + F.e[1] * F.e[1];
    inst[1] = fabsf(F.e[4]) * (F.e[2] * F.e[2] + F.e[3] * F.e[3]);
    inst[2] = F.e[4] * F.e[4];
    for (i = 0; i < N_LOSS; ++i)
        for (j = 0; j < N_LOSS_TAU; ++j) {
            if (!F.started) F.loss[i][j] = aLoss[j] * inst[i];
            else F.loss[i][j] += aLoss[j] * (inst[i] - F.loss[i][j]);
        }

    /* 4. 8 路滤波值的长时 EWMA（60 s ～ 2 h）：转子热容大，温度取决于较长时间的历史。
     *    电气量从 0 起步，温度从当前值起步（上电时定子温度就是最好的初值）。 */
    for (i = 0; i < N_ELEC; ++i) src[i] = F.e[i];
    for (i = 0; i < N_TEMP; ++i) src[N_ELEC + i] = F.eT[i];
    for (i = 0; i < N_SLOW; ++i)
        for (j = 0; j < N_SLOW_TAU; ++j) {
            if (!F.started) F.slow[i][j] = (i < N_ELEC) ? aSlow[j] * src[i] : src[i];
            else F.slow[i][j] += aSlow[j] * (src[i] - F.slow[i][j]);
        }

    /* 5. 按训练时的顺序拼成特征行 */
    for (i = 0; i < N_ELEC; ++i) feat[i] = F.e[i];
    for (i = 0; i < N_TEMP; ++i) feat[N_ELEC + i] = F.eT[i];
    for (i = 0; i < N_LOSS; ++i)
        for (j = 0; j < N_LOSS_TAU; ++j) feat[8 + i * N_LOSS_TAU + j] = F.loss[i][j];
    for (i = 0; i < N_SLOW; ++i)
        for (j = 0; j < N_SLOW_TAU; ++j) feat[23 + i * N_SLOW_TAU + j] = F.slow[i][j];
    /* 数据流持续时间：让网络知道长时 EWMA 还没收敛（刚上电时它们偏小） */
    feat[55] = 1.0F - expf(-((float)F.steps * 1.0F) / AGE_TAU_S);

    ++F.steps;
    F.started = 1U;
}
