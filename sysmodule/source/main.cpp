#include "service.hpp"

#include <switch.h>

extern "C" {

u32 __nx_applet_type     = AppletType_None;
u32 __nx_fs_num_sessions = 1;

// 静态 newlib 堆：M0 仅 IPC/日志占用。
// M1 起 socket 传输内存、解码缓冲都从这里出，届时按内存预算重调（参照 Streamfin 768KB）。
void __libnx_initheap(void) {
    static char inner_heap[1024 * 512];
    extern char *fake_heap_start;
    extern char *fake_heap_end;

    fake_heap_start = inner_heap;
    fake_heap_end   = inner_heap + sizeof(inner_heap);
}

void __appInit() {
    R_ABORT_UNLESS(smInitialize());
    static const char dbg[] = "qqmusic sysmodule: init\n";
    svcOutputDebugString(dbg, sizeof(dbg) - 1);
}

void __appExit() {
    smExit();
}

} // extern "C"

int main(int, char *[]) {
    R_ABORT_UNLESS(qqmusic::InitializeServer());
    qqmusic::LoopProcess();
    R_ABORT_UNLESS(qqmusic::ExitServer());
    return 0;
}