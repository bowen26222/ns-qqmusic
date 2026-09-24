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

#include "nxExt/ipc_server.h"
#include <stdio.h>
#include <string.h>

Result ipcServerInit(IpcServer* server, const char* name, u32 max_sessions)
{
    if(max_sessions < 1 || max_sessions > (MAX_WAIT_OBJECTS - 1))
    {
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);
    }

    server->srvName = smEncodeName(name);
    server->max = max_sessions + 1;
    server->count = 0;

    Result rc = svcManageNamedPort(&server->handles[0], server->srvName.name, max_sessions);
    if (R_FAILED(rc))
    {
        rc = svcManageNamedPort(&server->handles[0], server->srvName.name, 0);
        if(R_SUCCEEDED(rc))
        {
            svcCloseHandle(server->handles[0]);
            rc = svcManageNamedPort(&server->handles[0], server->srvName.name, max_sessions);
        }
    }

    if(R_SUCCEEDED(rc))
    {
        server->count = 1;
    }
    return rc;
}

Result ipcServerExit(IpcServer* server)
{
    for(u32 i = 0; i < server->count; i++)
    {
        svcCloseHandle(server->handles[i]);
    }
    server->count = 0;
    return svcManageNamedPort(&server->handles[0], server->srvName.name, 0);
}

static Result _ipcServerAddSession(IpcServer* server, Handle session)
{
    if(server->count >= server->max)
    {
        return MAKERESULT(Module_Libnx, LibnxError_OutOfMemory);
    }

    server->handles[server->count] = session;
    server->count++;
    return 0;
}

static Result _ipcServerDeleteSession(IpcServer* server, u32 index)
{
    if(!index || index >= server->count)
    {
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);
    }

    svcCloseHandle(server->handles[index]);

    for(u32 j = index; j < (server->count - 1); j++)
    {
        server->handles[j] = server->handles[j + 1];
    }
    server->count--;
    return 0;
}

static Result _ipcServerParseRequest(IpcServerRequest* r)
{
    u8* base = armGetTls();
    HipcHeader hdr;
    memcpy(&hdr, base, sizeof(hdr));
    // Validate the layout before following any descriptor pointers.
    if(hdr.has_special_header || hdr.num_send_statics || hdr.num_exch_buffers ||
       hdr.recv_static_mode || hdr.num_send_buffers > 1 || hdr.num_recv_buffers > 1)
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);
    const size_t layoutSize = sizeof(HipcHeader) +
        (hdr.num_send_buffers + hdr.num_recv_buffers) * sizeof(HipcBufferDescriptor) +
        hdr.num_data_words * sizeof(u32);
    if(layoutSize > 0x100)
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);

    HipcParsedRequest hipc = hipcParseRequest(base);
    r->type = hipc.meta.type;
    r->data.cmdId = 0;
    r->data.size = 0;
    r->data.ptr = r->raw_data;
    r->send_buffer = (IpcServerBuffer){0};
    r->recv_buffer = (IpcServerBuffer){0};

    if(r->type == CmifCommandType_Request || r->type == CmifCommandType_Control)
    {
        const size_t rawSize = hipc.meta.num_data_words * sizeof(u32);
        if(rawSize < sizeof(CmifInHeader) + 0x10)
            return MAKERESULT(Module_Libnx, LibnxError_BadInput);
        CmifInHeader* header = cmifGetAlignedDataStart(hipc.data.data_words, base);
        const size_t dataSize = rawSize - sizeof(CmifInHeader) - 0x10;
        if(dataSize > sizeof(r->raw_data) || header->magic != CMIF_IN_HEADER_MAGIC ||
           header->version != 0 || header->token != 0)
            return MAKERESULT(Module_Libnx, LibnxError_BadInput);
        r->data.cmdId = header->command_id;
        r->data.size = dataSize;
        memcpy(r->raw_data, header + 1, dataSize);
        if(hipc.meta.num_send_buffers) {
            r->send_buffer.ptr = hipcGetBufferAddress(hipc.data.send_buffers);
            r->send_buffer.size = hipcGetBufferSize(hipc.data.send_buffers);
        }
        if(hipc.meta.num_recv_buffers) {
            r->recv_buffer.ptr = hipcGetBufferAddress(hipc.data.recv_buffers);
            r->recv_buffer.size = hipcGetBufferSize(hipc.data.recv_buffers);
        }
    }
    return 0;
}

static void _ipcServerPrepareResponse(Result rc, const void* data, size_t dataSize)
{
    if(dataSize > IPC_SERVER_EXT_RESPONSE_MAX_DATA_SIZE)
        rc = MAKERESULT(Module_Libnx, LibnxError_BadInput);
    if(R_FAILED(rc))
        dataSize = 0;
    const size_t rawSize = (sizeof(CmifOutHeader) + dataSize + 0x10 + 3) & ~(size_t)3;
    u8* base = armGetTls();
    memset(base, 0, sizeof(HipcHeader) + rawSize);
    HipcRequest hipc = hipcMakeRequestInline(base,
        .type = CmifCommandType_Request,
        .num_data_words = rawSize / sizeof(u32),
    );
    CmifOutHeader* header = cmifGetAlignedDataStart(hipc.data_words, base);
    *header = (CmifOutHeader){ .magic = CMIF_OUT_HEADER_MAGIC, .result = rc };
    if(dataSize)
        memcpy(header + 1, data, dataSize);
}

static Result _ipcServerProcessNewSession(IpcServer* server)
{
    Handle session;
    Result rc = svcAcceptSession(&session, server->handles[0]);
    if(R_SUCCEEDED(rc) && R_FAILED(rc = _ipcServerAddSession(server, session)))
    {
        svcCloseHandle(session);
    }
    return rc;
}

static Result _ipcServerProcessSession(IpcServer* server, IpcServerRequestHandler handler, void* userdata, u32 handleIndex)
{
    extern void sysLog(const char *s);

    s32 unusedIndex;
    IpcServerRequest r;
    size_t dataSize = 0;
    u64 data[IPC_SERVER_EXT_RESPONSE_MAX_DATA_SIZE / sizeof(u64)];
    bool close = false;

    Result rc = svcReplyAndReceive(&unusedIndex, &server->handles[handleIndex], 1, 0, UINT64_MAX);
    if(R_SUCCEEDED(rc))
    {
        rc = _ipcServerParseRequest(&r);
        if(R_FAILED(rc))
        {
            char b[64];
            snprintf(b, sizeof(b), "SYS ipc parse fail rc=0x%08x\n", (unsigned) rc);
            sysLog(b);
        }
    }
    else
    {
        char b[64];
        snprintf(b, sizeof(b), "SYS ipc wait fail rc=0x%08x idx=%u\n", (unsigned) rc, (unsigned) handleIndex);
        sysLog(b);
    }

    if(R_SUCCEEDED(rc))
    {
        switch(r.type)
        {
            case CmifCommandType_Request:
                // C 不保证函数参数求值顺序：先完成 handler，再读取响应长度。
                {
                    Result handlerRc = handler(userdata, &r, (u8*)data, &dataSize);
                    if(R_FAILED(handlerRc)) {
                        char b[96];
                        snprintf(b, sizeof(b), "SYS ipc cmd=0x%08x result=0x%08x bytes=%u\n",
                                 (unsigned)r.data.cmdId, (unsigned)handlerRc, (unsigned)dataSize);
                        // Avoid an SD flush on every successful UI status poll.
                        sysLog(b);
                    }
                    _ipcServerPrepareResponse(handlerRc, data, dataSize);
                }
                break;
            case CmifCommandType_Control:
                // serviceCreate queries pointer capacity. This port uses MapAlias only.
                if(r.data.cmdId == 3) {
                    const u16 pointerSize = 0;
                    _ipcServerPrepareResponse(0, &pointerSize, sizeof(pointerSize));
                } else {
                    _ipcServerPrepareResponse(MAKERESULT(Module_Libnx, LibnxError_BadInput), NULL, 0);
                }
                break;
            case CmifCommandType_Close:
                _ipcServerPrepareResponse(0, NULL, 0);
                close = true;
                break;
            default:
                _ipcServerPrepareResponse(MAKERESULT(11, 403), NULL, 0);
                break;
        }

        rc = svcReplyAndReceive(&unusedIndex, &server->handles[handleIndex], 0, server->handles[handleIndex], 0);
        if(rc == KERNELRESULT(TimedOut))
        {
            rc = 0;
        }
        else if(R_FAILED(rc))
        {
            char b[64];
            snprintf(b, sizeof(b), "SYS ipc reply fail rc=0x%08x\n", (unsigned) rc);
            sysLog(b);
        }
    }

    if(R_FAILED(rc) || close)
    {
        _ipcServerDeleteSession(server, handleIndex);
        char b[64];
        snprintf(b, sizeof(b), "SYS ipc session closed rc=0x%08x\n", (unsigned) rc);
        sysLog(b);
    }

    return rc;
}

Result ipcServerProcess(IpcServer* server, IpcServerRequestHandler handler, void* userdata)
{
    s32 handleIndex = -1;
    Result rc = svcWaitSynchronization(&handleIndex, server->handles, server->count, UINT64_MAX);

    if(R_SUCCEEDED(rc) && (handleIndex < 0 || handleIndex >= server->count))
    {
        rc = MAKERESULT(Module_Libnx, LibnxError_NotFound);
    }

    if(R_SUCCEEDED(rc))
    {
        if(handleIndex)
        {
            rc = _ipcServerProcessSession(server, handler, userdata, handleIndex);
        }
        else
        {
            rc = _ipcServerProcessNewSession(server);
        }
    }

    return rc;
}