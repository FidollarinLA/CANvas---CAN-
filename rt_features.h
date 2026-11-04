/*
 * rt_features.h —— 特征流水线：每个模型步（1 s）把 8 路原始输入变成 56 维特征
 *
 * 特征顺序（和训练时冻结的顺序一致，网络按位置读取）：
 *   [0..4]   Id、Iq、Ud、Uq、转速 的短时滤波值（EWMA，电流电压 0.5 s、转速 1 s）
 *   [5..7]   T01、T03、T08 去尖刺后的平滑值（中值 5 → 均值 5 → EWMA 3 s）
 *   [8..12]  铜耗代理 Id²+Iq² 的 EWMA，时间常数 30 / 120 / 600 / 1800 / 7200 s
 *   [13..17] 铁耗代理 |n|·(Ud²+Uq²) 的 EWMA，同上 5 个时间常数
 *   [18..22] 转速平方 n² 的 EWMA，同上 5 个时间常数
 *   [23..42] Id、Iq、Ud、Uq、转速 各自的长时 EWMA，时间常数 60 / 600 / 1800 / 7200 s
 *   [43..54] T01、T03、T08 各自的长时 EWMA，时间常数 60 / 600 / 1800 / 7200 s
 *   [55]     数据流已持续多久：1 − exp(−t / 2400 s)
 * 电气量和损耗代理的 EWMA 从 0 起步（上电时没有历史），温度的 EWMA 从第一个值起步。
 */
#ifndef RT_FEATURES_H
#define RT_FEATURES_H

#define RT_N_FEAT  56U

/* 清空滤波器状态，下一次 rt_features_step() 当作数据流的第一个模型步 */
void rt_features_reset(void);

/* 一个模型步：u[8] 原始输入（工程单位）→ feat[RT_N_FEAT] */
void rt_features_step(const float *u, float *feat);

#endif /* RT_FEATURES_H */
