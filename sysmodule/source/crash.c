// 崩溃记录器（sysmodule 侧）：覆盖 libnx 的弱符号 __libnx_exception_entry（见 crash_entry.s）。
// 内核异常入口约定：x1 = 内核异常帧指针（ThreadExceptionFrameA64），x0 = 异常类型。
// 镜像基址 = &__nx_exit - 0xE0（devkitA64 CRT 固定布局）。
// 日志：sdmc:/qqmusic-sys-crash.log；写完 svcReturnFromException(0xF801) 照常蓝屏。
#include <switch.h>

#include <stdio.h>
#include <string.h>
// 内核异常帧 ThreadExceptionFrameA64。字段偏移按 libnx exception.o 的
// “帧 → __nx_exceptiondump” 拷贝序列逐条核对（读帧的顺序）：
//   x0..x8 @0x00（内核向量只用到这 9 个通用寄存器）、lr @0x48、sp @0x50、
//   elr（故障 PC）@0x58、pstate @0x60、afsr0 @0x64、afsr1 @0x68、esr @0x6C、far @0x70。
// 其中 0x50/0x58 会被 libnx 入口改写为它自己的异常栈/返回入口，故仅作参考。
typedef struct {
    u64 gprs[9]; // x0..x8：故障现场
    u64 lr;      // x30
    u64 sp;
    u64 elr;     // 故障 PC
    u32 pstate;
    u32 afsr0;
    u32 afsr1;
    u32 esr;
    u64 far;     // 故障地址
} CrashFrame;


// __nx_exit 在 devkitA64 CRT 中的固定 ELF 偏移（构建后用 nm 复核）。
#define K_NX_EXIT_OFF 0xE0

volatile u32 g_crash_in = 0;

// crash_entry.s 专用崩溃栈（页对齐 4K）：故障线程自身的栈可能接近耗尽，
// crashLog 的 snprintf/stdio 需要干净栈空间。
u8 s_crashStack[0x1000] __attribute__((aligned(0x1000), used));

// raw-fs 追加（不用 stdio/malloc：异常上下文 + 4K 专用栈上 stdio 不可靠，
// 且崩溃可能发生在 stdio 就绪之前）。
static void RawAppend(const char *path, const char *s, size_t n) {
    // 保留调试输出；FS 尚未初始化时不能发起文件服务 IPC。
    svcOutputDebugString(s, n);
    if (!serviceIsActive(fsGetServiceSession()))
        return;

    FsFileSystem fs;
    Result rc = fsOpenSdCardFileSystem(&fs);
    if (R_FAILED(rc))
        goto failed;
    FsFile fd;
    const u32 mode = FsOpenMode_Write | FsOpenMode_Append;
    rc = fsFsOpenFile(&fs, path, mode, &fd);
    if (R_FAILED(rc)) {
        rc = fsFsCreateFile(&fs, path, 0, 0);
        if (R_SUCCEEDED(rc))
            rc = fsFsOpenFile(&fs, path, mode, &fd);
        if (R_FAILED(rc)) {
            fsFsClose(&fs);
            goto failed;
        }
    }
    s64 fsize;
    rc = fsFileGetSize(&fd, &fsize);
    if (R_SUCCEEDED(rc))
        rc = fsFileWrite(&fd, fsize, s, n, FsWriteOption_Flush);
    fsFileClose(&fd);
    fsFsClose(&fs);
failed:
    if (R_FAILED(rc)) {
        char error[128];
        snprintf(error, sizeof(error), "SYS log write failed path=%s rc=0x%08x\n", path, (unsigned)rc);
        svcOutputDebugString(error, strlen(error));
    }
}

static void CrashAppend(const char *s, size_t n) { RawAppend("/qqmusic-sys-crash.log", s, n); }

// 通用流程日志（启动打点 / IPC 循环诊断）→ /qqmusic-sys.log。
// FS 初始化前仅发往调试输出；不能在仍引用 TLS 请求数据时调用。
void sysLog(const char *s) { RawAppend("/qqmusic-sys.log", s, strlen(s)); }

static void CrashLine(const char *line) { CrashAppend(line, strlen(line)); }

// 由 crash_entry.s 调用：type = 异常类型（内核置于 x0），frame = 内核异常帧，
// nx_exit = &__nx_exit（绝对地址）。
void crashLog(u64 type, u64 nx_exit, u64 frame) {
    const u64 base = nx_exit - K_NX_EXIT_OFF;
    const CrashFrame *fr = (const CrashFrame *)frame;
    char buf[352];

    snprintf(buf, sizeof(buf),
             "CRASH sysmod type=0x%llx frame=0x%llx base=0x%llx\n"
             "  pc=0x%llx (off 0x%llx) far=0x%llx esr=0x%08x afsr0=0x%08x afsr1=0x%08x pstate=0x%08x\n"
             "  lr=0x%llx sp=0x%llx\n"
             "  x0=0x%llx x1=0x%llx x2=0x%llx x3=0x%llx\n"
             "  x4=0x%llx x5=0x%llx x6=0x%llx x7=0x%llx x8=0x%llx\n",
             (unsigned long long)type, (unsigned long long)frame, (unsigned long long)base,
             (unsigned long long)fr->elr, (unsigned long long)(fr->elr - base),
             (unsigned long long)fr->far, fr->esr, fr->afsr0, fr->afsr1, fr->pstate,
             (unsigned long long)fr->lr, (unsigned long long)fr->sp,
             (unsigned long long)fr->gprs[0], (unsigned long long)fr->gprs[1],
             (unsigned long long)fr->gprs[2], (unsigned long long)fr->gprs[3],
             (unsigned long long)fr->gprs[4], (unsigned long long)fr->gprs[5],
             (unsigned long long)fr->gprs[6], (unsigned long long)fr->gprs[7],
             (unsigned long long)fr->gprs[8]);
    CrashAppend(buf, strlen(buf));

    // 原始帧 0x80 字节：字段偏移若要修正，直接按这段离线还原，无需再来一次真机复现。
    for (u32 i = 0; i < 0x80; i += 16) {
        char hex[96];
        int n = snprintf(hex, sizeof(hex), "  f%02x:", (unsigned)i);
        for (u32 j = 0; j < 16; j++) {
            n += snprintf(hex + n, sizeof(hex) - (size_t)n, " %02x",
                          ((const u8 *)fr)[i + j]);
        }
        snprintf(hex + n, sizeof(hex) - (size_t)n, "\n");
        CrashAppend(hex, strlen(hex));
    }
}

// sysmodule 每次启动写一行标记。
void crashBoot(void) { CrashLine("BOOT sysmod\n"); }