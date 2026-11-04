/*
 * c2000ware_sources.c —— 把 C2000Ware 里要用的两个源文件编进工程
 *
 *   device.c       时钟、Flash 等待周期、外设时钟的初始化（Device_init 等），TI 示例工程同款
 *   f28p55x_npu.c  NNC 库调用的 NPU 驱动（只在定义 RT_NPU 时需要）
 *
 * 用 #include 而不是在工程里链接文件：导入 CCS 后不用再加链接资源，只要 C2000WARE_ROOT
 * 指到 C2000Ware 就能找到它们（工程的头文件路径里已经有 device_support/f28p55x/common/source）。
 */
#include "device.c"

#ifdef RT_NPU
#include "f28p55x_npu.c"
#endif
