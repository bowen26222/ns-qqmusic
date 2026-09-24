// 崩溃记录器（ovl 侧）：覆盖 libnx 的弱符号 __libnx_exception_entry（见 crash.s）。
//
// 链路：ovlloader 进程内任意线程发生异常（ovl 代码或 ovlloader 自身代码）→
// 内核调用 ovlloader 的 __libnx_exception_entry → `br g_nroAddr` 转发到 ovl 的 VMA 0 →
// devkitA64 CRT 分发器（x0=异常帧指针, x1=1 → 异常路径）→ 本文件的 __libnx_exception_entry。
//
// 内核异常帧 = ThreadExceptionFrameA64（对照 libnx __libnx_exception_entry_start
// 的帧→__nx_exceptiondump 拷贝序列推导）：gprs[9] @0x00 (x0..x8), lr @0x48, sp @0x50,
// reserved @0x58（入口改写为异常栈顶）, elr(故障 PC) @0x60, pstate @0x68,
// afsr0 @0x6C, afsr1 @0x70, esr @0x74, far @0x78
//
// 日志写到 sdmc:/qqmusic-crash.log（append），随后 svcReturnFromException(0xF801)
// 让内核照常蓝屏（行为不变，只是留下第一现场）。
#include <switch.h>

#include <stdio.h>
#include <string.h>

typedef struct {
    u64 gprs[9];
    u64 lr;
    u64 sp;
    u64 reserved;
    u64 elr; // 故障 PC
    u32 pstate;
    u32 afsr0;
    u32 afsr1;
    u32 esr;
    u64 far; // 故障地址
} CrashFrame;
volatile u32 g_crash_in = 0;

// crash_entry.s 专用崩溃栈（页对齐 4K）：故障线程自身的栈可能接近耗尽，
// crashLog 的 snprintf/stdio 需要干净栈空间。
u8 s_crashStack[0x1000] __attribute__((aligned(0x1000), used));

static void CrashAppend(const char *s, size_t n) {
    FILE *f = fopen("/qqmusic-crash.log", "ab");
    if (!f)
        return;
    fwrite(s, 1, n, f);
    fflush(f);
    fclose(f);
}

static void CrashLine(const char *line) { CrashAppend(line, strlen(line)); }

// 由 crash.s 调用：frame = 内核异常帧，base = ovl 映射基址（ovlloader 经 x7 传入）。
void crashLog(u64 frame, u64 base) {
    const CrashFrame *fr = (const CrashFrame *)frame;
    char buf[320];

    snprintf(buf, sizeof(buf),
             "CRASH ovll frame=0x%016llx base=0x%016llx\n"
             "  pc=0x%016llx lr=0x%016llx sp=0x%016llx\n"
             "  far=0x%016llx esr=0x%08x pstate=0x%08x\n"
             "  x0=0x%016llx x1=0x%016llx x2=0x%016llx x3=0x%016llx\n"
             "  x4=0x%016llx x5=0x%016llx x6=0x%016llx x8=0x%016llx\n",
             (unsigned long long)frame, (unsigned long long)base,
             (unsigned long long)fr->elr, (unsigned long long)fr->lr, (unsigned long long)fr->sp,
             (unsigned long long)fr->far, fr->esr, fr->pstate,
             (unsigned long long)fr->gprs[0], (unsigned long long)fr->gprs[1],
             (unsigned long long)fr->gprs[2], (unsigned long long)fr->gprs[3],
             (unsigned long long)fr->gprs[4], (unsigned long long)fr->gprs[5],
             (unsigned long long)fr->gprs[6], (unsigned long long)fr->gprs[8]);
    CrashAppend(buf, strlen(buf));
}

// ovl 每次启动写一行标记（区分崩溃发生在哪个会话）。
void crashBoot(void) { CrashLine("BOOT ovl diag=2\n"); }

// 非致命错误记录（如 initServices 失败 → 显示错误界面）：
// 让错误界面的出现原因可追溯（此前 ErrorGui 显示时无任何日志）。
void crashErr(const char *s, u32 rc) {
    char buf[192];
    snprintf(buf, sizeof(buf), "ERR ovl %s rc=0x%08X\n", s ? s : "?", rc);
    CrashAppend(buf, strlen(buf));
}

// 步骤面包屑：Atmosphère 的 2168-0002 属 svcBreak/abort 致命错误，
// 不经过内核异常入口，故 crashLog 收不到第一现场。在关键步骤前打点，
// 崩溃后日志的最后一行即为最后成功到达的位置。
void crashTrace(const char *s) {
    char buf[160];
    snprintf(buf, sizeof(buf), "TRACE ovl %s\n", s ? s : "?");
    CrashAppend(buf, strlen(buf));
}