/*
 * rt_estimator.h —— 转子温度估计模块（纯 CNN，网络在 F28P55x 的 NPU 上跑）对外接口
 *
 * 估计对象：永磁体表面温度 T15。
 * 调用方式：每 100 ms 调一次 rt_step()；内部每 10 次调用（1 s）走一个模型步。
 *
 * 输入 u[RT_N_IN]（工程单位，顺序固定）：
 *   u[0] Id    [A]     d 轴电流
 *   u[1] Iq    [A]     q 轴电流
 *   u[2] Ud    [V]     d 轴电压
 *   u[3] Uq    [V]     q 轴电压
 *   u[4] 转速  [rpm]
 *   u[5] T01   [℃]    定子温度（热电偶 T01）
 *   u[6] T03   [℃]    定子温度（热电偶 T03）
 *   u[7] T08   [℃]    定子温度（热电偶 T08）
 *   Id/Iq/Ud/Uq 必须和台架记录同一套 dq 坐标（同一电角度零位）、同一标度，
 *   电流差 10% 或坐标转 15° 都会让估计明显偏。
 *
 * 输出：
 *   y[0]    T15 估计 [℃]；valid = 0 时为 0，表示“没有估计”，不是 0 ℃
 *   valid   1 = y[0] 有效
 *   raw[0]  诊断用：最近一次网络的原始输出（输出滤波之前），还没有时为 NaN
 *
 * 时序：输入就绪后先等 10 次调用让各路 CAN 帧到齐，然后窗口要攒满 20 个模型步，
 * 所以第 201 次调用（约 20 s）出第一个值，之后每 1 s 更新一次，中间保持。
 *
 * 断流：任一输入不是有限值（NaN/Inf）、8 路全为 0、或 8 路连续 12 次调用（1.2 s）
 * 完全不变，都当作数据流断了：输出停发（valid = 0），全部状态清空，输入恢复后从头
 * 重建窗口（再过约 20 s 出值）。调用方短时缺数据时应保持上一个值（不要传 0），
 * 确认数据流重新开始时才调 rt_reset()。
 */
#ifndef RT_ESTIMATOR_H
#define RT_ESTIMATOR_H

#include <stdint.h>

#define RT_N_IN            8U       /* 输入路数 */
#define RT_N_OUT           1U       /* 输出路数（T15） */
#define RT_CALL_PERIOD_MS  100      /* 调用周期，ms：所有时间常数都按每秒 10 次调用算 */

/* 各路输入在 u[] 里的位置 */
#define RT_PORT_ID     0
#define RT_PORT_IQ     1
#define RT_PORT_UD     2
#define RT_PORT_UQ     3
#define RT_PORT_SPEED  4
#define RT_PORT_T01    5
#define RT_PORT_T03    6
#define RT_PORT_T08    7

/* 清空全部状态：上电时、以及调用方确认数据流重新开始时调用 */
void rt_reset(void);

/* 每 100 ms 调一次：u[RT_N_IN] 输入，y[RT_N_OUT] / *valid / raw[RT_N_OUT] 输出 */
void rt_step(const float *u, float *y, uint16_t *valid, float *raw);

#endif /* RT_ESTIMATOR_H */
