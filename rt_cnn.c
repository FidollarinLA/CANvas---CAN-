/*
 * rt_cnn.c —— 网络前向
 *
 * 训练好的 PyTorch 模型做了量化感知训练，转成 8 位整数后各层分工如下：
 *
 *   ① 标准化 (x − 均值) / 标准差，限幅到 ±3.9           CPU，浮点（本文件）
 *   ② 输入量化成 8 位：q = floor((x + 1/64) × 32)          NNC 库里的 CPU 代码
 *   ③ Conv 1×3（两端补零）56 → 16 通道 + BN + ReLU         NPU
 *   ④ Conv 1×3（两端补零）16 → 32 通道 + BN + ReLU         NPU
 *   ⑤ 全局平均池化（20 步 → 1）                            NPU
 *   ⑥ 全连接 32 → 32                                       NPU
 *   ⑦ 全连接 32 → 1，反标准化成 ℃                          CPU，浮点（本文件）
 *
 * 窗口当作 1 × 20 的“图像”，56 个特征是通道，所以卷积核是 1×3、沿时间方向滑动。
 * BN 在量化时已经并进卷积的整数偏置和移位。⑥ 训练时从单位阵起步：TI 的模型转换要求
 * 网络以全连接结尾，而真正出温度的 ⑦ 要留在 CPU 用浮点算——NPU 的输出只有 8 位，
 * 直接出温度分辨率不够。
 *
 * 编译时定义 RT_NPU（CCS 工程里已定义）：② ～ ⑥ 调 TI NNC 编出的库 npu/mod_cnn.a，
 * 在 NPU 上跑。不定义 RT_NPU：② ～ ⑥ 在 CPU 上用下面等价的整数运算算，结果和 NPU
 * 逐位相同（每层都是整数乘加 + 加偏置 + 算术右移 + 限幅，中间量不超过 2^24），
 * PC 上的逐拍核对就是用它做的，没有 NPU 的芯片也能直接用。
 */
#include <math.h>
#include <stdint.h>
#include "rt_cnn.h"
#include "rt_cnn_params.h"

static float img[CNN_IN_CH * RT_WINDOW];   /* ① 的结果：通道优先，img[c * 20 + t] */
static float hid[CNN_HID];                 /* ⑥ 的输出（已乘回量化步长，浮点） */

#ifdef RT_NPU

#include "tvmgen_cnn.h"

/* ② ～ ⑥：交给 NNC 库。库在第一次调用时登记 NPU 中断（并打开全局中断），
 * NPU 算完由中断把 tvmgen_cnn_finished 置 1。 */
static void cnn_body(void)
{
    struct tvmgen_cnn_inputs in = { (void *)img };
    struct tvmgen_cnn_outputs out = { (void *)hid };
    tvmgen_cnn_run(&in, &out);
    while (!tvmgen_cnn_finished) { }
}

#else

static int16_t q0[CNN_IN_CH * RT_WINDOW];  /* ② 输入，8 位有符号，通道优先 */
static int16_t q1[CNN_C1 * RT_WINDOW];     /* ③ 输出，8 位无符号（ReLU 之后） */
static int16_t q2[CNN_C2 * RT_WINDOW];     /* ④ 输出 */
static int16_t q3[CNN_C2];                 /* ⑤ 输出 */

static int16_t clip_i(int32_t v, int32_t lo, int32_t hi)
{
    return (int16_t)(v < lo ? lo : (v > hi ? hi : v));
}

/* 卷积 1×3（时间方向两端各补一个 0）+ 再量化 + ReLU：
 *   acc = Σ_c Σ_k w[o][c][k] · in[c][t + k − 1]
 *   out[o][t] = clip((acc + b[o]) >> s[o], 0, 255)
 * TI 的再量化一般是 ((acc + b) × m) >> s，本模型各通道的 m 都是 1。
 * 有符号数右移按算术右移（向下取整），TI C28x 编译器和 gcc 都是这样。 */
static void conv3_relu(const int16_t *in, int32_t cin, int16_t *out, int32_t cout,
                       const int16_t *w, const int32_t *b, const uint16_t *s)
{
    int32_t o, c, t, k, tt, acc;
    for (o = 0; o < cout; ++o)
        for (t = 0; t < (int32_t)RT_WINDOW; ++t) {
            acc = 0;
            for (c = 0; c < cin; ++c)
                for (k = 0; k < (int32_t)CNN_K; ++k) {
                    tt = t + k - 1L;
                    if (tt >= 0 && tt < (int32_t)RT_WINDOW)
                        acc += (int32_t)w[(o * cin + c) * (int32_t)CNN_K + k] * (int32_t)in[c * (int32_t)RT_WINDOW + tt];
                }
            out[o * (int32_t)RT_WINDOW + t] = clip_i((acc + b[o]) >> s[o], 0L, 255L);
        }
}

/* ② ～ ⑥ 的 CPU 等价实现 */
static void cnn_body(void)
{
    int32_t i, j, sum, acc;

    /* ② 输入量化：步长 1/32，加 1/64 再向下取整即四舍五入；①已限幅 ±3.9，不会溢出 */
    for (i = 0; i < (int32_t)(CNN_IN_CH * RT_WINDOW); ++i)
        q0[i] = clip_i((int32_t)floorf((img[i] + CNN_IN_OFFSET) * CNN_IN_SCALE), -128L, 127L);

    /* ③ ④ 两层卷积 */
    conv3_relu(q0, CNN_IN_CH, q1, CNN_C1, &cnn_w1[0][0][0], cnn_b1, cnn_s1);
    conv3_relu(q1, CNN_C1, q2, CNN_C2, &cnn_w2[0][0][0], cnn_b2, cnn_s2);

    /* ⑤ 全局平均池化：先求 20 步的和，再乘 205/4096（≈ 1/20）并加 0.5 取整 */
    for (i = 0; i < (int32_t)CNN_C2; ++i) {
        sum = 0;
        for (j = 0; j < (int32_t)RT_WINDOW; ++j) sum += q2[i * (int32_t)RT_WINDOW + j];
        q3[i] = clip_i(((sum + CNN_GAP_OFFSET) * CNN_GAP_MULT) >> CNN_GAP_SHIFT, 0L, 255L);
    }

    /* ⑥ 全连接 32 → 32，输出 8 位有符号，再乘回量化步长变成浮点 */
    for (i = 0; i < (int32_t)CNN_HID; ++i) {
        acc = cnn_bfc[i];
        for (j = 0; j < (int32_t)CNN_C2; ++j) acc += (int32_t)cnn_wfc[i][j] * (int32_t)q3[j];
        hid[i] = (float)clip_i(acc >> CNN_FC_SHIFT, -128L, 127L) * CNN_OUT_SCALE;
    }
}

#endif /* RT_NPU */

/*
 * window 布局：时间优先 window[t * RT_N_FEAT + f]，t=0 最旧。
 * 进入 NPU/整数卷积前转为通道优先 img[c * RT_WINDOW + t]，与 ONNX NCHW 一致。
 */
float rt_cnn_forward(const float *window)
{
    int32_t c, t;
    float x, s;

    /* ① 标准化（训练集上的均值、标准差）并限幅，排成通道优先 */
    for (c = 0; c < (int32_t)CNN_IN_CH; ++c)
        for (t = 0; t < (int32_t)RT_WINDOW; ++t) {
            x = (window[t * (int32_t)RT_N_FEAT + c] - cnn_feat_mean[c]) / cnn_feat_std[c];
            img[c * (int32_t)RT_WINDOW + t] = fminf(fmaxf(x, -CNN_CLIP), CNN_CLIP);
        }

    cnn_body(); /* ②～⑥：NPU 或 CPU 整数等价；完成后 hid[] 已是反量化浮点 */

    /* ⑦ 全连接 32 → 1（浮点），再反标准化成 ℃（均值/方差见 rt_cnn_params.h） */
    s = CNN_HEAD_B;
    for (c = 0; c < (int32_t)CNN_HID; ++c) s += hid[c] * cnn_head_w[c];
    return s * CNN_Y_STD + CNN_Y_MEAN;
}
