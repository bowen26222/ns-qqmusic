// 覆盖 libnx 的弱符号 __libnx_exception_entry（强定义，链接时胜出）。
// 内核异常入口约定（对照 libnx exception.o 中 __libnx_exception_entry 的反汇编）：
//   x1 = 内核异常帧指针（ThreadExceptionFrameA64，非空），x0 = 异常类型（小整数）。
//   libnx 自身入口同样以 x1 为帧指针（首条指令 cmp x1,#0，为零即放弃）。
// 注意：x0 不是帧指针——类型值（本次为 0x101）曾一度被当作帧指针传递，
// 使 crashLog 每次都在 0x101+0x38 处二次数据中止，日志从未写出。
// 处理完（日志写完）后 svcReturnFromException(0xF801)：未处理 → 内核照常蓝屏。
.global __libnx_exception_entry
.type __libnx_exception_entry, %function
.align 2
__libnx_exception_entry:
    cbz  x1, .Lbail
    adrp x8, g_crash_in
    ldr  w9, [x8, #:lo12:g_crash_in]
    cbnz w9, .Lbail
    mov  w9, #1
    str  w9, [x8, #:lo12:g_crash_in]
    // 切专用崩溃栈（见 crash.c s_crashStack）：故障线程栈可能已接近耗尽。
    mov  x2, x1                    // a2 = 内核异常帧
    adrp x1, __nx_exit
    add  x1, x1, #:lo12:__nx_exit  // a1 = &__nx_exit
    adrp x8, s_crashStack
    add  x8, x8, #:lo12:s_crashStack
    add  x8, x8, #0x1000
    mov  sp, x8
    bl   crashLog                  // crashLog(type = x0, nx_exit = x1, frame = x2)
.Lbail:
    mov  w0, #0xF801
    bl   svcReturnFromException
    b    .
