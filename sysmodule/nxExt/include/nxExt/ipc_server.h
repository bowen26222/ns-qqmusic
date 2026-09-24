/*
 * --------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <p-sam@d3vs.net>, <natinusala@gmail.com>, <m4x@m4xw.net>
 * wrote this file. As long as you retain this notice you can do whatever you
 * want with this stuff. If you meet any of us some day, and you think this
 * stuff is worth it, you can buy them a beer in return.  - The sys-clk authors
 *
 * Vendored from dammitjeff/streamfin-switch (sys-tune/nxExt).
 * --------------------------------------------------------------------------
 */

#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

#include <switch.h>

// Horizon 的 TLS IPC 命令区只有 0x100 字节；HIPC + CMIF + 对齐占 0x28。
// 路径、元数据及封面必须通过 MapAlias 缓冲区传输。
#define IPC_SERVER_EXT_RESPONSE_MAX_DATA_SIZE (0xD8)

typedef struct
{
    void* ptr;
    size_t size;
} IpcServerBuffer;

typedef struct
{
    SmServiceName srvName;
    Handle handles[MAX_WAIT_OBJECTS];
    u32 max;
    u32 count;
} IpcServer;

typedef struct
{
    u64 cmdId;
    void* ptr;
    size_t size;
} IpcServerRequestData;

typedef struct
{
    u32 type;
    IpcServerRequestData data;
    IpcServerBuffer send_buffer;
    IpcServerBuffer recv_buffer;
    // Handler 内调用其他服务会覆盖 TLS，连同标量请求一起在调用前保存。
    u64 raw_data[IPC_SERVER_EXT_RESPONSE_MAX_DATA_SIZE / sizeof(u64)];
} IpcServerRequest;

typedef Result (*IpcServerRequestHandler)(void* userdata, const IpcServerRequest* r, u8* out_data, size_t* out_dataSize);

Result ipcServerInit(IpcServer* server, const char* name, u32 max_sessions);
Result ipcServerExit(IpcServer* server);
Result ipcServerProcess(IpcServer* server, IpcServerRequestHandler handler, void* userdata);

#ifdef __cplusplus
}
#endif