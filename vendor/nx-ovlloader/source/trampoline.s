.section .text.nroEntrypointTrampoline, "ax", %progbits
.global nroEntrypointTrampoline
.type   nroEntrypointTrampoline, %function
.align 2
.global __libnx_exception_entry
.type   __libnx_exception_entry, %function
.cfi_startproc
nroEntrypointTrampoline:
    // Reset stack pointer.
    adrp x8, __stack_top
    ldr  x8, [x8, #:lo12:__stack_top]
    mov  sp, x8
    // Call NRO.
    blr  x2
    // Save retval
    adrp x1, g_lastRet
    str  w0, [x1, #:lo12:g_lastRet]
    
    // Check if exit was requested
    adrp x8, __stack_top
    ldr  x8, [x8, #:lo12:__stack_top]
    mov  sp, x8
    bl   checkExitRequested
    cbnz w0, __exit_requested

    // Check if a manual reload was requested (flag file - same effect as heap change)
    adrp x8, __stack_top
    ldr  x8, [x8, #:lo12:__stack_top]
    mov  sp, x8
    bl   checkReloadRequested
    cbnz w0, __exit_for_heap_change

    // Check if heap size changed (fast check between overlays)
    adrp x8, __stack_top
    ldr  x8, [x8, #:lo12:__stack_top]
    mov  sp, x8
    bl   checkHeapSizeChange
    cbnz w0, __exit_for_heap_change
    
    // No heap change - reset stack pointer and load next NRO.
    adrp x8, __stack_top
    ldr  x8, [x8, #:lo12:__stack_top]
    mov  sp, x8
    b    loadNro

__exit_requested:
    // Exit WITHOUT restarting the loader - just terminate cleanly
    bl   svcExitProcess
    b    .

__exit_for_heap_change:
    // Heap size changed - exit to restart with new size
    b    __wrap_exit
    
.cfi_endproc
.section .text.__libnx_exception_entry, "ax", %progbits
.align 2
.cfi_startproc
// 异常入口（补丁）：先无条件记录第一现场到 sdmc:/qqmusic-crash.log，
// 再按原逻辑转发/蓝屏。覆盖 g_nroAddr==0 的窗口期（loadNro unmap/map
// 之间、ovlmenu 阶段、ovlloader 自身代码）——这些阶段的故障此前无任何
// 日志（ovl 侧日志器只覆盖 ovl 已映射且可达的窗口）。
// x0-x15/x18-x27 在异常入口均可用（x0=帧指针, x1=1）；x8 为被调用者
// 保存寄存器，跨 bl 保留基址。ovllCrashLog 自带重入 guard 与 raw-fs
// 写入（无 malloc/stdio），失败时静默返回，不影响原转发逻辑。
__libnx_exception_entry:
    adrp x7, g_nroAddr
    ldr  x7, [x7, #:lo12:g_nroAddr]
    mov  x1, x7
    bl   ovllCrashLog
    // ovllCrashLog 是 C 函数，可能污染 x8（调用者保存）——重新载入
    adrp x8, g_nroAddr
    ldr  x8, [x8, #:lo12:g_nroAddr]
    cbz  x8, __libnx_exception_entry_fail
    br   x8
__libnx_exception_entry_fail:
    mov w0, #0xf801
    bl svcReturnFromException
    b .
.cfi_endproc