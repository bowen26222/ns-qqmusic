// 覆盖 libnx 的弱符号 __libnx_exception_entry（强定义，链接时胜出）。
//
// 内核异常入口约定（对照 devkitA64 CRT 分发器 @VMA0 与 libnx 自身入口反汇编得出）：
//   x0 = 内核异常帧指针（ThreadExceptionFrameA64），x1 = 1。
// ovlloader 转发（trampoline.s: br g_nroAddr）保留 x7 = g_nroAddr（ovl 映射基址）。
//
// 注意：guard 的读/写必须用独立寄存器。`ldr w8, [x8]` 会把 32 位结果零扩展进 X8，
// 摧毁 adrp 放在 x8 的页基址，随后 `str` 落到 0x1+0xBD4=0xBDD 触发二级故障
// （2026-09-22 420000000007E51A 蓝屏根因之一）。
//
// bl crashLog 之前切到专用 4K 崩溃栈：故障线程自身的栈可能已接近耗尽
// （如 ovl 主线程跑在 ovlloader 的 trampoline 栈上），而 crashLog 里的
// snprintf/stdio 需要栈空间。guard 路径（.Lbail）不碰栈，可安全嵌套重入。
//
// 处理完（日志写完）后 svcReturnFromException(0xF801)：未处理 → 内核照常蓝屏。
.global __libnx_exception_entry
.type __libnx_exception_entry, %function
.align 2
__libnx_exception_entry:
    adrp  x8, g_crash_in
    ldr   w9, [x8, #:lo12:g_crash_in]
    cbnz  w9, .Lbail
    mov   w9, #1
    str   w9, [x8, #:lo12:g_crash_in]
    // 切崩溃栈（s_crashStack 为页对齐 4K 数组，见 crash.c）
    adrp  x8, s_crashStack
    add   x8, x8, #:lo12:s_crashStack
    add   x8, x8, #0x1000
    mov   sp, x8
    // x0 = 异常帧（内核传入，未动），x1 = ovl 基址（x7 由 ovlloader 转发保留）
    mov   x1, x7
    bl    crashLog
.Lbail:
    mov   w0, #0xF801
    bl   svcReturnFromException
    b    .