/*
 * rt_cnn_params.h —— 网络常量（从量化感知训练后的模型导出，不要手改）
 *
 * 结构和各层的算法见 rt_cnn.c。整数权重和偏置是 TI tinyml_torchmodelopt 转换出来的
 * 8 位量化模型里的数（npu/npu_cnn.onnx，NNC 编 npu/mod_cnn.a 用的也是它）；
 * 标准化的均值、标准差是训练集上的；最后一层全连接是浮点。
 */
#ifndef RT_CNN_PARAMS_H
#define RT_CNN_PARAMS_H

#include <stdint.h>

#define CNN_IN_CH      56U      /* 输入通道 = 特征数 */
#define CNN_K          3U       /* 卷积核长度（沿时间） */
#define CNN_C1         16U      /* 第一层卷积输出通道 */
#define CNN_C2         32U      /* 第二层卷积输出通道 */
#define CNN_HID        32U      /* NPU 上全连接的输出个数 */

#define CNN_CLIP       3.9F    /* 标准化后的限幅：输入量化的范围是 ±4 */
#define CNN_IN_OFFSET  0.015625F   /* 输入量化：q = floor((x + 偏移) × 刻度)，即按 1/32 四舍五入 */
#define CNN_IN_SCALE   32.0F
#define CNN_GAP_OFFSET 10L      /* 全局平均池化：((和 + 偏移) × 乘数) >> 移位 */
#define CNN_GAP_MULT   205L
#define CNN_GAP_SHIFT  12
#define CNN_FC_SHIFT   7        /* NPU 上全连接的再量化移位 */
#define CNN_OUT_SCALE  0.0625F   /* NPU 输出的量化步长 */
#define CNN_HEAD_B     0.0342213027F   /* 最后一层全连接（浮点）的偏置 */
#define CNN_Y_MEAN     63.3227272F    /* T15 的训练集均值，℃ */
#define CNN_Y_STD      32.6117249F    /* T15 的训练集标准差，℃ */

extern const float cnn_feat_mean[CNN_IN_CH];                 /* 各特征的训练集均值 */
extern const float cnn_feat_std[CNN_IN_CH];                  /* 各特征的训练集标准差 */
extern const int16_t cnn_w1[CNN_C1][CNN_IN_CH][CNN_K];       /* 第一层卷积权重（8 位） */
extern const int32_t cnn_b1[CNN_C1];                         /* 第一层整数偏置（BN 已并入） */
extern const uint16_t cnn_s1[CNN_C1];                        /* 第一层再量化移位 */
extern const int16_t cnn_w2[CNN_C2][CNN_C1][CNN_K];
extern const int32_t cnn_b2[CNN_C2];
extern const uint16_t cnn_s2[CNN_C2];
extern const int16_t cnn_wfc[CNN_HID][CNN_C2];               /* NPU 上全连接的权重 [输出][输入] */
extern const int32_t cnn_bfc[CNN_HID];
extern const float cnn_head_w[CNN_HID];                      /* 最后一层全连接（浮点）的权重 */

#endif /* RT_CNN_PARAMS_H */
