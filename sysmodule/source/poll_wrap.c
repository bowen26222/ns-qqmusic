#include <poll.h>
#include <errno.h>
#include <string.h>
#include <sys/iosupport.h>

extern int __real_poll(struct pollfd *fds, nfds_t nfds, int timeout);

int __wrap_poll(struct pollfd *fds, nfds_t nfds, int timeout) {
    if (fds == NULL) {
        errno = EFAULT;
        return -1;
    }

    // 在 libnx 的 poll() 内部循环调用 _socketGetFd() 之前，
    // 检查每个非负 fd 是否对应合法的 "soc" socket。
    // 如果 fd 已经失效、已关闭或句柄设备指针为空，
    // 将其置为负数避免进入 libnx 触发 devoptab_list[handle->device]->name 空指针解引用 (0x0 Data Abort)。
    for (nfds_t i = 0; i < nfds; i++) {
        if (fds[i].fd >= 0) {
            __handle *h = __get_handle(fds[i].fd);
            if (!h || h->device >= STD_MAX || devoptab_list[h->device] == NULL ||
                strcmp(devoptab_list[h->device]->name, "soc") != 0) {
                fds[i].revents = POLLNVAL;
                fds[i].fd = -1;
            }
        }
    }

    return __real_poll(fds, nfds, timeout);
}
