/*
 * model_flash_lnk.cmd —— F28P550 Flash 运行链接命令
 *
 * 与普通 TI 示例的差异（NPU 相关）：
 *   .rodata.tvm     → Flash：NNC 生成的只读权重/常量
 *   .bss.noinit.tvm → RAMGS3：NPU 可访问的全局共享工作区（勿挪到 LS 局部 RAM）
 * 栈放在 RAMGS0 低 64K 字地址空间（C28x 硬件栈约束）。
 */
MEMORY
{
   BEGIN       : origin = 0x080000, length = 0x000002
   BOOT_RSVD   : origin = 0x000002, length = 0x000126
   RAMM0       : origin = 0x000128, length = 0x0002D8
   RAMM1       : origin = 0x000400, length = 0x000400
   RAMLS0      : origin = 0x008000, length = 0x000800
   RAMLS1      : origin = 0x008800, length = 0x000800
   RAMLS2      : origin = 0x009000, length = 0x000800
   RAMLS3      : origin = 0x009800, length = 0x000800
   RAMLS4      : origin = 0x00A000, length = 0x000800
   RAMLS5      : origin = 0x00A800, length = 0x000800
   RAMLS6      : origin = 0x00B000, length = 0x000800
   RAMLS7      : origin = 0x00B800, length = 0x000800
   RAMLS8      : origin = 0x014000, length = 0x002000
   RAMLS9      : origin = 0x016000, length = 0x002000
   RAMGS0      : origin = 0x00C000, length = 0x002000
   RAMGS1      : origin = 0x00E000, length = 0x002000
   RAMGS2      : origin = 0x010000, length = 0x002000
   RAMGS3      : origin = 0x012000, length = 0x002000
   FLASH_BANK0 : origin = 0x080002, length = 0x1FFFE
   FLASH_BANK1 : origin = 0x0A0000, length = 0x20000
   FLASH_BANK2 : origin = 0x0C0000, length = 0x20000
   FLASH_BANK3 : origin = 0x0E0000, length = 0x20000
   FLASH_BANK4 : origin = 0x100000, length = 0x08000
   RESET       : origin = 0x3FFFC0, length = 0x000002
}

SECTIONS
{
   codestart   : > BEGIN
   .text       : >> FLASH_BANK0 | FLASH_BANK1 | FLASH_BANK2, ALIGN(8)
   .cinit      : > FLASH_BANK0, ALIGN(8)
   .switch     : > FLASH_BANK0, ALIGN(8)
   .reset      : > RESET, TYPE = DSECT
   .init_array : > FLASH_BANK0, ALIGN(8)
   .const      : >> FLASH_BANK0 | FLASH_BANK1 | FLASH_BANK2, ALIGN(8)
   /* C28x硬件栈必须位于低64K字地址；GS0完整留给0x1000字栈。 */
   .stack      : > RAMGS0
   .bss        : >> RAMLS1 | RAMLS2 | RAMLS3 | RAMLS4 | RAMLS5 | RAMLS6 | RAMLS7 | RAMLS8 | RAMLS9 | RAMGS1 | RAMGS2
   /* NNC 模块（只在定义 RT_NPU 编译时有）：常量放 Flash，NPU 的工作区放它能访问的全局共享 RAM */
   .rodata.tvm     : > FLASH_BANK0, ALIGN(8)
   .bss.noinit.tvm : > RAMGS3
   .bss:output : > RAMM0
   .data       : >> RAMLS5 | RAMLS6
   .sysmem     : > RAMM0
   .TI.ramfunc : {} LOAD = FLASH_BANK0,
                       RUN = RAMLS0,
                       LOAD_START(RamfuncsLoadStart),
                       LOAD_SIZE(RamfuncsLoadSize),
                       LOAD_END(RamfuncsLoadEnd),
                       RUN_START(RamfuncsRunStart),
                       RUN_SIZE(RamfuncsRunSize),
                       RUN_END(RamfuncsRunEnd),
                       ALIGN(8)
}
