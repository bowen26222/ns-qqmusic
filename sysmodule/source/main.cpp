#include "impl/player.hpp"
#include "impl/aud_wrapper.h"
#include "sdmc/sdmc.hpp"
#include "pm/pm.hpp"
#include "service.hpp"
#include "result.hpp"

// 静态 socket 配置（M3 在线播放用；sysmodule 内 TCP-only 即可，
// 传输内存取自静态堆，(tx_max + rx_max) * sb_efficiency ~= 56KB）。
namespace {
    constexpr SocketInitConfig g_socket_conf = {
        .tcp_tx_buf_size     = 0x800,
        .tcp_rx_buf_size     = 0x1000,
        .tcp_tx_buf_max_size = 0x2EE0,
        .tcp_rx_buf_max_size = 0x4000,
        .udp_tx_buf_size     = 0,
        .udp_rx_buf_size     = 0,
        .sb_efficiency       = 2,
        .num_bsd_sessions    = 2,
        .bsd_service_type    = BsdServiceType_User,
    };
}

extern "C" {
u32 __nx_applet_type     = AppletType_None;
u32 __nx_fs_num_sessions = 1;

// 静态堆：socket 传输内存 + 解码缓冲 + 播放列表条目。
// 解码器/重采样器/文件缓冲按需从这块堆分配（见 player.cpp 注释）。
void __libnx_initheap(void) {
    static char inner_heap[1024 * 768];
    extern char *fake_heap_start;
    extern char *fake_heap_end;

    fake_heap_start = inner_heap;
    fake_heap_end   = inner_heap + sizeof(inner_heap);
}

void __appInit() {
    R_ABORT_UNLESS(smInitialize());
    R_ABORT_UNLESS(setsysInitialize());
    {
        SetSysFirmwareVersion version;
        R_ABORT_UNLESS(setsysGetFirmwareVersion(&version));
        hosversionSet(MAKEHOSVERSION(version.major, version.minor, version.micro));
        setsysExit();
    }

    R_ABORT_UNLESS(gpioInitialize());
    R_ABORT_UNLESS(fsInitialize());
    R_ABORT_UNLESS(audWrapperInitialize());
    R_ABORT_UNLESS(pm::Initialize());
    R_ABORT_UNLESS(sdmc::Open());

    // socket 失败不阻塞启动：M3 的 NetTest 会报告，M1 纯本地不需要。
    socketInitialize(&g_socket_conf);
}

void __appExit(void) {
    socketExit();
    sdmc::Close();
    pm::Exit();
    audWrapperExit();
    fsExit();
    gpioExit();
    smExit();
}
} // extern "C"

namespace {
    alignas(0x1000) u8 gpioThreadBuffer[0x1000];
    alignas(0x1000) u8 pmdmntThreadBuffer[0x1000];
    alignas(0x1000) u8 playerThreadBuffer[0x6000];
}

int main(int, char *[]) {
    R_ABORT_UNLESS(qqmusic::impl::Initialize());

    // 耳机插拔暂停线程：GpioPadName 0x15 = 耳机孔检测脚。
    GpioPadSession headphone_detect_session;
    R_ABORT_UNLESS(gpioOpenSession(&headphone_detect_session, GpioPadName(0x15)));

    ::Thread gpioThread;
    ::Thread pmdmtThread;
    ::Thread playerThread;
    R_ABORT_UNLESS(threadCreate(&gpioThread, qqmusic::impl::GpioThreadFunc, &headphone_detect_session, gpioThreadBuffer, sizeof(gpioThreadBuffer), 0x20, -2));
    R_ABORT_UNLESS(threadCreate(&pmdmtThread, qqmusic::impl::PmdmntThreadFunc, nullptr, pmdmntThreadBuffer, sizeof(pmdmntThreadBuffer), 0x20, -2));
    R_ABORT_UNLESS(threadCreate(&playerThread, qqmusic::impl::TuneThreadFunc, nullptr, playerThreadBuffer, sizeof(playerThreadBuffer), 0x18, -2));

    R_ABORT_UNLESS(threadStart(&gpioThread));
    R_ABORT_UNLESS(threadStart(&pmdmtThread));
    R_ABORT_UNLESS(threadStart(&playerThread));

    // IPC 服务（阻塞循环，直到 overlay 侧断开/重启）。
    R_ABORT_UNLESS(qqmusic::InitializeServer());
    qqmusic::LoopProcess();
    R_ABORT_UNLESS(qqmusic::ExitServer());

    qqmusic::impl::Exit();
    svcCancelSynchronization(gpioThread.handle);

    R_ABORT_UNLESS(threadWaitForExit(&gpioThread));
    R_ABORT_UNLESS(threadWaitForExit(&pmdmtThread));
    R_ABORT_UNLESS(threadWaitForExit(&playerThread));

    R_ABORT_UNLESS(threadClose(&gpioThread));
    R_ABORT_UNLESS(threadClose(&pmdmtThread));
    R_ABORT_UNLESS(threadClose(&playerThread));

    gpioPadClose(&headphone_detect_session);

    return 0;
}