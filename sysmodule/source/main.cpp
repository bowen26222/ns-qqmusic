#include "impl/player.hpp"
#include "impl/meta.hpp"
#include "impl/aud_wrapper.h"
#include "sdmc/sdmc.hpp"
#include "pm/pm.hpp"
#include "service.hpp"
#include "result.hpp"
#include "net/qqmusic_api.hpp"
#include <sys/iosupport.h>

// 静态 socket 配置（M3 在线播放用；sysmodule 内 TCP-only 即可，
// 传输内存取自静态堆，(tx_max + rx_max) * sb_efficiency ~= 56KB）。
namespace {
    constexpr SocketInitConfig g_socket_conf = {
        .tcp_tx_buf_size     = 0x8000,
        .tcp_rx_buf_size     = 0x10000,
        .tcp_tx_buf_max_size = 0x10000,
        .tcp_rx_buf_max_size = 0x20000,
        .udp_tx_buf_size     = 0x2400,
        .udp_rx_buf_size     = 0x4000,
        .sb_efficiency       = 1,
        .num_bsd_sessions    = 2,
        .bsd_service_type    = BsdServiceType_Auto,
    };
}

extern "C" {
u32 __nx_applet_type     = AppletType_None;
u32 __nx_fs_num_sessions = 1;

// 启动/流程日志：委托 crash.c 的 raw-fs sysLog（/qqmusic-sys.log）。
extern "C" void sysLog(const char *s);
static inline void SysBootLog(const char *s) { sysLog(s); }

#define SYS_TRY(expr) do { \
    Result _rc = (expr); \
    if (R_FAILED(_rc)) { \
        char _b[160]; \
        snprintf(_b, sizeof(_b), "SYS FAIL rc=0x%08x line=%d call=%.96s\n", (unsigned) _rc, __LINE__, #expr); \
        SysBootLog(_b); \
        svcBreak(_rc, (uintptr_t) 0, 0); \
    } \
} while (0)

// 静态堆：socket 传输内存 + 解码缓冲 + 播放列表条目 + **在线 API 响应体**。
// 尺寸依据：一个 200 首歌的歌单响应体实测 400KB、300 首专辑约 600KB；
// std::string 按 2 倍增长时（256KB+512KB 短暂共存）峰值可达 768KB。
// 1MB 会在打开 200 首歌单时分配失败（-fno-exceptions → abort，表现为界面卡死）；
// 1.5MB 留出足够余量，同时 BSS 总量仍远低于触发 am OutOfResource 的 5.47MB。
void __libnx_initheap(void) {
    static char inner_heap[1024 * 1536];
    extern char *fake_heap_start;
    extern char *fake_heap_end;

    fake_heap_start = inner_heap;
    fake_heap_end   = inner_heap + sizeof(inner_heap);
}

void __appInit() {
    SYS_TRY(smInitialize());
    SYS_TRY(fsInitialize());
    SysBootLog("SYS diag=2 appinit sm/fs ok\n");
    SYS_TRY(setsysInitialize());
    {
        SetSysFirmwareVersion version;
        SYS_TRY(setsysGetFirmwareVersion(&version));
        hosversionSet(MAKEHOSVERSION(version.major, version.minor, version.micro));
        setsysExit();
    }
    SysBootLog("SYS appinit sm/setsys ok\n");

    SYS_TRY(gpioInitialize());
    SysBootLog("SYS appinit gpio/fs ok\n");
    SYS_TRY(audWrapperInitialize());
    SysBootLog("SYS appinit audio ok\n");
    SYS_TRY(pm::Initialize());
    SysBootLog("SYS appinit pm ok\n");
    SYS_TRY(sdmc::Open());
    SysBootLog("SYS appinit sdmc ok\n");


    // socket 失败不阻塞启动：M3 的 NetTest 会报告，M1 纯本地不需要。
    {
        Result rc = socketInitialize(&g_socket_conf);
        char b[64];
        snprintf(b, sizeof(b), "SYS appinit socket rc=0x%08x\n", (unsigned) rc);
        SysBootLog(b);
    }

    // 填补 devoptab_list 中所有未分配的空槽位，
    // 使得 libnx socket.c 内 _socketGetFd() 执行 strcmp(devoptab_list[handle->device]->name, "soc")
    // 时永远不会解引用 NULL 指针（彻底根除 Data Abort 2168-0002 Address: 0x0）。
    static const devoptab_t g_dummy_devoptab = { .name = "null" };
    for (int i = 0; i < STD_MAX; i++) {
        if (devoptab_list[i] == nullptr) {
            devoptab_list[i] = &g_dummy_devoptab;
        }
    }
    SysBootLog("SYS appinit done\n");
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
    // 播放线程会跑 dr_mp3 解码 + libcurl（multi_runsingle/Curl_connect/ssl），
    // 调用链很深；libnx 的 poll() 还用 alloca 取栈。24KB 会栈溢出，
    // 表现为 libnx poll() 内 NULL/野指针 Data Abort（2168-0002）。
    alignas(0x1000) u8 playerThreadBuffer[0x20000];
    // 封面后台取数线程：不做网络 I/O 就不会挡住 IPC 线程，同 player 线程一样要跑
    // libcurl（TLS 握手 + poll），栈给足。
    alignas(0x1000) u8 coverThreadBuffer[0x20000];
}

extern "C" void crashBoot(void);

int main(int, char *[]) {
    crashBoot();
    SysBootLog("SYS main start\n");
    SYS_TRY(qqmusic::impl::Initialize());
    SysBootLog("SYS impl init done\n");
    qqmusic::api::Init();
    SysBootLog("SYS api init done\n");

    // 耳机插拔暂停线程：GpioPadName 0x15 = 耳机孔检测脚。
    GpioPadSession headphone_detect_session;
    SYS_TRY(gpioOpenSession(&headphone_detect_session, GpioPadName(0x15)));
    SysBootLog("SYS gpio session ok\n");

    ::Thread gpioThread;
    ::Thread pmdmtThread;
    ::Thread playerThread;
    ::Thread coverThread;
    SYS_TRY(threadCreate(&gpioThread, qqmusic::impl::GpioThreadFunc, &headphone_detect_session, gpioThreadBuffer, sizeof(gpioThreadBuffer), 0x20, -2));
    SYS_TRY(threadCreate(&pmdmtThread, qqmusic::impl::PmdmntThreadFunc, nullptr, pmdmntThreadBuffer, sizeof(pmdmntThreadBuffer), 0x20, -2));
    SYS_TRY(threadCreate(&playerThread, qqmusic::impl::TuneThreadFunc, nullptr, playerThreadBuffer, sizeof(playerThreadBuffer), 0x18, -2));
    SYS_TRY(threadCreate(&coverThread, qqmusic::impl::CoverWorkerThreadFunc, nullptr, coverThreadBuffer, sizeof(coverThreadBuffer), 0x28, -2));

    SYS_TRY(threadStart(&gpioThread));
    SYS_TRY(threadStart(&pmdmtThread));
    SYS_TRY(threadStart(&playerThread));
    SYS_TRY(threadStart(&coverThread));
    SysBootLog("SYS threads up\n");

    // IPC 服务（阻塞循环，直到 overlay 侧断开/重启）。
    SYS_TRY(qqmusic::InitializeServer());
    SysBootLog("SYS ipc server up\n");
    qqmusic::LoopProcess();
    SysBootLog("SYS loop exit\n");
    SYS_TRY(qqmusic::ExitServer());

    qqmusic::impl::Exit();
    qqmusic::impl::StopCoverWorker();
    svcCancelSynchronization(gpioThread.handle);

    R_ABORT_UNLESS(threadWaitForExit(&gpioThread));
    R_ABORT_UNLESS(threadWaitForExit(&pmdmtThread));
    R_ABORT_UNLESS(threadWaitForExit(&playerThread));
    R_ABORT_UNLESS(threadWaitForExit(&coverThread));

    R_ABORT_UNLESS(threadClose(&gpioThread));
    R_ABORT_UNLESS(threadClose(&pmdmtThread));
    R_ABORT_UNLESS(threadClose(&playerThread));
    R_ABORT_UNLESS(threadClose(&coverThread));
    gpioPadClose(&headphone_detect_session);

    return 0;
}