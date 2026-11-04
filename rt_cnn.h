/*
 * rt_cnn.h —— 网络前向：最近 20 个模型步的特征窗口 → T15 估计 [℃]
 */
#ifndef RT_CNN_H
#define RT_CNN_H

#include "rt_features.h"

#define RT_WINDOW  20U      /* 窗口长度：20 个模型步 = 20 s */

/* window[t * RT_N_FEAT + f]：第 t 步（t = 0 最旧）的第 f 个特征，工程单位。返回 ℃ */
float rt_cnn_forward(const float *window);

#endif /* RT_CNN_H */
