/*
* Copyright (c) Huawei Technologies Co., Ltd. 2020-2021. All rights reserved.
* gazelle is licensed under the Mulan PSL v2.
* You can use this software according to the terms and conditions of the Mulan PSL v2.
* You may obtain a copy of Mulan PSL v2 at:
*     http://license.coscl.org.cn/MulanPSL2
* THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR
* PURPOSE.
* See the Mulan PSL v2 for more details.
*/

#define _GNU_SOURCE
#include <dlfcn.h>

#include <signal.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/epoll.h>
#include <net/if.h>
#include <securec.h>

#include <lwip/posix_api.h>
#include <lwip/lwipsock.h>
#include <lwip/tcp.h>

#include "posix/lstack_unistd.h"
#include "lstack_log.h"
#include "lstack_cfg.h"
#include "lstack_lwip.h"
#include "gazelle_base_func.h"
#include "lstack_preload.h"

#include "lstack_rtc_api.h"
#include "lstack_rtw_api.h"

#ifndef SOCK_TYPE_MASK
#define SOCK_TYPE_MASK 0xf
#endif

posix_api_t g_wrap_api_value;
posix_api_t *g_wrap_api;

void wrap_api_init(void)
{
    if (g_wrap_api != NULL) {
        return;
    }

    g_wrap_api = &g_wrap_api_value;
    if (get_global_cfg_params()->stack_mode_rtc) {
        g_wrap_api->socket_fn        = rtc_socket;
        g_wrap_api->accept_fn        = lwip_accept;
        g_wrap_api->accept4_fn       = lwip_accept4;
        g_wrap_api->bind_fn          = lwip_bind;
        g_wrap_api->listen_fn        = lwip_listen;
        g_wrap_api->connect_fn       = lwip_connect;
        g_wrap_api->setsockopt_fn    = lwip_setsockopt;
        g_wrap_api->getsockopt_fn    = lwip_getsockopt;
        g_wrap_api->getpeername_fn   = lwip_getpeername;
        g_wrap_api->getsockname_fn   = lwip_getsockname;
        g_wrap_api->read_fn          = lwip_read;
        g_wrap_api->readv_fn         = lwip_readv;
        g_wrap_api->write_fn         = lwip_write;
        g_wrap_api->writev_fn        = lwip_writev;
        g_wrap_api->recv_fn          = lwip_recv;
        g_wrap_api->send_fn          = lwip_send;
        g_wrap_api->recv_msg         = lwip_recvmsg;
        g_wrap_api->send_msg         = lwip_sendmsg;
        g_wrap_api->recv_from        = lwip_recvfrom;
        g_wrap_api->send_to          = lwip_sendto;
        g_wrap_api->epoll_wait_fn    = rtc_epoll_wait;
        g_wrap_api->poll_fn          = rtc_poll;
        g_wrap_api->close_fn         = rtc_close;
        g_wrap_api->epoll_ctl_fn     = rtc_epoll_ctl;
        g_wrap_api->epoll_create1_fn = rtc_epoll_create1;
        g_wrap_api->epoll_create_fn  = rtc_epoll_create;
    } else {
        g_wrap_api->socket_fn        = rtw_socket;
        g_wrap_api->accept_fn        = rtw_accept;
        g_wrap_api->accept4_fn       = rtw_accept4;
        g_wrap_api->bind_fn          = rtw_bind;
        g_wrap_api->listen_fn        = rtw_listen;
        g_wrap_api->connect_fn       = rtw_connect;
        g_wrap_api->setsockopt_fn    = rtw_setsockopt;
        g_wrap_api->getsockopt_fn    = rtw_getsockopt;
        g_wrap_api->getpeername_fn   = rtw_getpeername;
        g_wrap_api->getsockname_fn   = rtw_getsockname;
        g_wrap_api->read_fn          = rtw_read;
        g_wrap_api->readv_fn         = rtw_readv;
        g_wrap_api->write_fn         = rtw_write;
        g_wrap_api->writev_fn        = rtw_writev;
        g_wrap_api->recv_fn          = rtw_recv;
        g_wrap_api->send_fn          = rtw_send;
        g_wrap_api->recv_msg         = rtw_recvmsg;
        g_wrap_api->send_msg         = rtw_sendmsg;
        g_wrap_api->recv_from        = rtw_recvfrom;
        g_wrap_api->send_to          = rtw_sendto;
        g_wrap_api->epoll_wait_fn    = rtw_epoll_wait;
        g_wrap_api->poll_fn          = rtw_poll;
        g_wrap_api->close_fn         = rtw_close;
        g_wrap_api->epoll_ctl_fn     = rtw_epoll_ctl;
        g_wrap_api->epoll_create1_fn = rtw_epoll_create1;
        g_wrap_api->epoll_create_fn  = rtw_epoll_create;
    }
}

static inline int32_t do_epoll_create1(int32_t flags)
{
    if (select_posix_path() == PATH_KERNEL) {
        return posix_api->epoll_create1_fn(flags);
    }

    return g_wrap_api->epoll_create1_fn(flags);
}

static inline int32_t do_epoll_create(int32_t size)
{
    if (select_posix_path() == PATH_KERNEL) {
        return posix_api->epoll_create_fn(size);
    }

    return g_wrap_api->epoll_create_fn(size);
}

static inline int32_t do_epoll_ctl(int32_t epfd, int32_t op, int32_t fd, struct epoll_event* event)
{
    if (select_posix_path() == PATH_KERNEL) {
        return posix_api->epoll_ctl_fn(epfd, op, fd, event);
    }

    return g_wrap_api->epoll_ctl_fn(epfd, op, fd, event);
}

static inline int32_t do_epoll_wait(int32_t epfd, struct epoll_event* events, int32_t maxevents, int32_t timeout)
{
    if (select_posix_path() == PATH_KERNEL) {
        return posix_api->epoll_wait_fn(epfd, events, maxevents, timeout);
    }

    if (epfd < 0) {
        GAZELLE_RETURN(EBADF);
    }

    if ((events == NULL) || (timeout < -1) || (maxevents <= 0)) {
        GAZELLE_RETURN(EINVAL);
    }

    return g_wrap_api->epoll_wait_fn(epfd, events, maxevents, timeout);
}

static inline int32_t do_accept(int32_t s, struct sockaddr *addr, socklen_t *addrlen)
{
    if (select_fd_posix_path(s, NULL) == PATH_KERNEL) {
        return posix_api->accept_fn(s, addr, addrlen);
    }

    int32_t fd = g_wrap_api->accept_fn(s, addr, addrlen);
    if (fd >= 0) {
        return fd;
    }

    return posix_api->accept_fn(s, addr, addrlen);
}

static int32_t do_accept4(int32_t s, struct sockaddr *addr, socklen_t *addrlen, int32_t flags)
{
    if (addr == NULL || addrlen == NULL) {
        GAZELLE_RETURN(EINVAL);
    }

    if (select_fd_posix_path(s, NULL) == PATH_KERNEL) {
        return posix_api->accept4_fn(s, addr, addrlen, flags);
    }

    int32_t fd = g_wrap_api->accept4_fn(s, addr, addrlen, flags);
    if (fd >= 0) {
        return fd;
    }

    return posix_api->accept4_fn(s, addr, addrlen, flags);
}

#define SIOCGIFADDR        0x8915
static int get_addr(struct sockaddr_in *sin, char *interface)
{
    int sockfd = 0;
    struct ifreq ifr;

    if ((sockfd = posix_api->socket_fn(AF_INET, SOCK_STREAM, 0)) < 0) return -1;

    memset_s(&ifr, sizeof(ifr), 0, sizeof(ifr));
    snprintf_s(ifr.ifr_name, sizeof(ifr.ifr_name), (sizeof(ifr.ifr_name) - 1), "%s", interface);

    if (posix_api->ioctl_fn(sockfd, SIOCGIFADDR, &ifr) < 0) {
        posix_api->close_fn(sockfd);
        return -1;
    }
    posix_api->close_fn(sockfd);

    memcpy_s(sin, sizeof(struct sockaddr_in), &ifr.ifr_addr, sizeof(struct sockaddr_in));

    return 0;
}

static int32_t do_bind(int32_t s, const struct sockaddr *name, socklen_t namelen)
{
    if (name == NULL) {
        GAZELLE_RETURN(EINVAL);
    }

    struct lwip_sock *sock = NULL;
    if (select_fd_posix_path(s, &sock) == PATH_KERNEL) {
        return posix_api->bind_fn(s, name, namelen);
    }

    if (match_host_addr(((struct sockaddr_in *)name)->sin_addr.s_addr)) {
        /* maybe kni addr */
        posix_api->bind_fn(s, name, namelen);
        return g_wrap_api->bind_fn(s, name, namelen);
    } else {
        SET_CONN_TYPE_HOST(sock->conn);
        return posix_api->bind_fn(s, name, namelen);
    }
}

bool is_dst_ip_localhost(const struct sockaddr *addr)
{
    struct sockaddr_in *servaddr = (struct sockaddr_in *) addr;
    char *line = NULL;
    char *p;
    size_t linel = 0;
    int linenum = 0;
    if (get_global_cfg_params()->host_addr.addr == servaddr->sin_addr.s_addr) {
        return true;
    }

    FILE *ifh = fopen("/proc/net/dev", "r");
    if (ifh == NULL) {
        LSTACK_LOG(ERR, LSTACK, "failed to open /proc/net/dev, errno is %d\n", errno);
        return false;
    }
    struct sockaddr_in* sin = malloc(sizeof(struct sockaddr_in));
    if (sin == NULL) {
        LSTACK_LOG(ERR, LSTACK, "sockaddr_in malloc failed\n");
        fclose(ifh);
        return false;
    }

    while (getdelim(&line, &linel, '\n', ifh) > 0) {
        /* 2: skip the first two lines, which are not nic name */
        if (linenum++ < 2) {
            continue;
        }

        p = line;
        while (isspace(*p)) {
            ++p;
        }
        int n = strcspn(p, ": \t");

        char interface[20] = {0}; /* 20: nic name len */
        strncpy_s(interface, sizeof(interface), p, n);

        memset_s(sin, sizeof(struct sockaddr_in), 0, sizeof(struct sockaddr_in));
        int ret = get_addr(sin, interface);
        if (ret == 0) {
            if (sin->sin_addr.s_addr == servaddr->sin_addr.s_addr) {
                free(sin);
                fclose(ifh);
                return true;
            }
        }
    }
    free(sin);
    fclose(ifh);

    return false;
}

static int32_t do_connect(int32_t s, const struct sockaddr *name, socklen_t namelen)
{
    if (name == NULL) {
        GAZELLE_RETURN(EINVAL);
    }

    struct lwip_sock *sock = NULL;
    if (select_fd_posix_path(s, &sock) == PATH_KERNEL) {
        return posix_api->connect_fn(s, name, namelen);
    }

    sock = get_socket(s);
    if (sock == NULL) {
        return posix_api->connect_fn(s, name, namelen);
    }

    if (!netconn_is_nonblocking(sock->conn)) {
        LSTACK_LOG(ERR, LSTACK, "connect does not support blocking fd currently\n");
        GAZELLE_RETURN(EINVAL);
    }

    int32_t ret = 0;
    char listen_ring_name[RING_NAME_LEN];
    int remote_port = htons(((struct sockaddr_in *)name)->sin_port);
    snprintf_s(listen_ring_name, sizeof(listen_ring_name), sizeof(listen_ring_name) - 1,
        "listen_rx_ring_%d", remote_port);
    if (is_dst_ip_localhost(name) && rte_ring_lookup(listen_ring_name) == NULL) {
        ret = posix_api->connect_fn(s, name, namelen);
        SET_CONN_TYPE_HOST(sock->conn);
    } else {
        ret = g_wrap_api->connect_fn(s, name, namelen);
        SET_CONN_TYPE_LIBOS(sock->conn);
    }

    return ret;
}

static inline int32_t do_listen(int32_t s, int32_t backlog)
{
    if (select_fd_posix_path(s, NULL) == PATH_KERNEL) {
        return posix_api->listen_fn(s, backlog);
    }

    int32_t ret = g_wrap_api->listen_fn(s, backlog);
    if (ret != 0) {
        return ret;
    }

    return posix_api->listen_fn(s, backlog);
}

static inline int32_t do_getpeername(int32_t s, struct sockaddr *name, socklen_t *namelen)
{
    if (name == NULL || namelen == NULL) {
        GAZELLE_RETURN(EINVAL);
    }

    if (select_fd_posix_path(s, NULL) == PATH_LWIP) {
        return g_wrap_api->getpeername_fn(s, name, namelen);
    }

    return posix_api->getpeername_fn(s, name, namelen);
}

static inline int32_t do_getsockname(int32_t s, struct sockaddr *name, socklen_t *namelen)
{
    if (name == NULL || namelen == NULL) {
        GAZELLE_RETURN(EINVAL);
    }

    if (select_fd_posix_path(s, NULL) == PATH_LWIP) {
        return g_wrap_api->getsockname_fn(s, name, namelen);
    }

    return posix_api->getsockname_fn(s, name, namelen);
}

static bool unsupport_optname(int32_t optname)
{
    if (optname == SO_BROADCAST ||
        optname == SO_PROTOCOL  ||
        optname == TCP_QUICKACK ||
        optname == SO_SNDTIMEO  ||
        optname == SO_RCVTIMEO) {
        return true;
    }
    return false;
}

static inline int32_t do_getsockopt(int32_t s, int32_t level, int32_t optname, void *optval, socklen_t *optlen)
{
    if (select_fd_posix_path(s, NULL) == PATH_LWIP && !unsupport_optname(optname)) {
        return g_wrap_api->getsockopt_fn(s, level, optname, optval, optlen);
    }

    return posix_api->getsockopt_fn(s, level, optname, optval, optlen);
}

static inline int32_t do_setsockopt(int32_t s, int32_t level, int32_t optname, const void *optval, socklen_t optlen)
{
    if (select_fd_posix_path(s, NULL) == PATH_KERNEL || unsupport_optname(optname)) {
        return posix_api->setsockopt_fn(s, level, optname, optval, optlen);
    }

    /* we both set kernel and lwip */
    int32_t ret = posix_api->setsockopt_fn(s, level, optname, optval, optlen);
    if (ret != 0) {
        return ret;
    }

    return g_wrap_api->setsockopt_fn(s, level, optname, optval, optlen);
}

static inline int32_t do_socket(int32_t domain, int32_t type, int32_t protocol)
{
    int32_t ret;
    /* process not init completed or not hajacking thread */
    if (select_posix_path() == PATH_KERNEL) {
        return posix_api->socket_fn(domain, type, protocol);
    }

    if ((domain != AF_INET && domain != AF_UNSPEC) ||
        ((type & SOCK_DGRAM) && !get_global_cfg_params()->udp_enable)) {
        return posix_api->socket_fn(domain, type, protocol);
    }

    ret = g_wrap_api->socket_fn(domain, type, protocol);
    /* if udp_enable = 1 in lstack.conf, udp protocol must be in user path currently */
    if ((ret >= 0) && (type & SOCK_DGRAM)) {
        struct lwip_sock *sock = get_socket(ret);
        if (sock != NULL && sock->conn != NULL) {
            SET_CONN_TYPE_LIBOS(sock->conn);
        }
    }

    return ret;
}

static inline ssize_t do_recv(int32_t sockfd, void *buf, size_t len, int32_t flags)
{
    if (buf == NULL) {
        GAZELLE_RETURN(EINVAL);
    }

    if (len == 0) {
        return 0;
    }

    if (select_posix_path() == PATH_KERNEL || // maybe fd is created by open before posix_api_init called
        select_fd_posix_path(sockfd, NULL) == PATH_KERNEL) {
        return posix_api->recv_fn(sockfd, buf, len, flags);
    }

    return g_wrap_api->recv_fn(sockfd, buf, len, flags);
}

static inline ssize_t do_read(int32_t s, void *mem, size_t len)
{
    if (mem == NULL) {
        GAZELLE_RETURN(EINVAL);
    }

    if (len == 0) {
        return 0;
    }

    if (select_posix_path() == PATH_KERNEL ||
        select_fd_posix_path(s, NULL) == PATH_KERNEL) {
        return posix_api->read_fn(s, mem, len);
    }

    return g_wrap_api->read_fn(s, mem, len);
}

static inline ssize_t do_readv(int32_t s, const struct iovec *iov, int iovcnt)
{
    if (select_posix_path() == PATH_KERNEL ||
        select_fd_posix_path(s, NULL) == PATH_KERNEL) {
        return posix_api->readv_fn(s, iov, iovcnt);
    }

    return g_wrap_api->readv_fn(s, iov, iovcnt);
}

static inline ssize_t do_send(int32_t sockfd, const void *buf, size_t len, int32_t flags)
{
    if (select_posix_path() == PATH_KERNEL ||
        select_fd_posix_path(sockfd, NULL) == PATH_KERNEL) {
        return posix_api->send_fn(sockfd, buf, len, flags);
    }

    return g_wrap_api->send_fn(sockfd, buf, len, flags);
}

static inline ssize_t do_write(int32_t s, const void *mem, size_t size)
{
    if (select_posix_path() == PATH_KERNEL ||
        select_fd_posix_path(s, NULL) == PATH_KERNEL) {
        return posix_api->write_fn(s, mem, size);
    }

    return g_wrap_api->write_fn(s, mem, size);
}

static inline ssize_t do_writev(int32_t s, const struct iovec *iov, int iovcnt)
{
    struct lwip_sock *sock;
    if (select_posix_path() == PATH_KERNEL ||
        select_fd_posix_path(s, &sock) == PATH_KERNEL) {
        return posix_api->writev_fn(s, iov, iovcnt);
    }

    return g_wrap_api->writev_fn(s, iov, iovcnt);
}

static inline ssize_t do_recvmsg(int32_t s, struct msghdr *message, int32_t flags)
{
    if (message == NULL) {
        GAZELLE_RETURN(EINVAL);
    }

    if (select_posix_path() == PATH_KERNEL ||
        select_fd_posix_path(s, NULL) == PATH_KERNEL) {
        return posix_api->recv_msg(s, message, flags);
    }

    return g_wrap_api->recv_msg(s, message, flags);
}

static inline ssize_t do_sendmsg(int32_t s, const struct msghdr *message, int32_t flags)
{
    if (message == NULL) {
        GAZELLE_RETURN(EINVAL);
    }

    struct lwip_sock *sock;
    if (select_posix_path() == PATH_KERNEL ||
        select_fd_posix_path(s, &sock) == PATH_KERNEL) {
        return posix_api->send_msg(s, message, flags);
    }

    return g_wrap_api->send_msg(s, message, flags);
}

static inline ssize_t do_recvfrom(int32_t sockfd, void *buf, size_t len, int32_t flags,
                                  struct sockaddr *addr, socklen_t *addrlen)
{
    if (buf == NULL) {
        GAZELLE_RETURN(EINVAL);
    }

    if (len == 0) {
        return 0;
    }

    struct lwip_sock *sock = NULL;
    if (select_fd_posix_path(sockfd, &sock) == PATH_LWIP) {
        return g_wrap_api->recv_from(sockfd, buf, len, flags, addr, addrlen);
    }

    return posix_api->recv_from(sockfd, buf, len, flags, addr, addrlen);
}

static inline ssize_t do_sendto(int32_t sockfd, const void *buf, size_t len, int32_t flags,
                                const struct sockaddr *addr, socklen_t addrlen)
{
    struct lwip_sock *sock = NULL;
    if (select_fd_posix_path(sockfd, &sock) != PATH_LWIP) {
        return posix_api->send_to(sockfd, buf, len, flags, addr, addrlen);
    }

    return g_wrap_api->send_to(sockfd, buf, len, flags, addr, addrlen);
}

static inline int32_t do_close(int32_t s)
{
    struct lwip_sock *sock = NULL;
    if (select_posix_path() == PATH_KERNEL ||
        select_fd_posix_path(s, &sock) == PATH_KERNEL) {
        /* we called lwip_socket, even if kernel fd */
        if (posix_api != NULL && !posix_api->ues_posix &&
            /* contain posix_api->close_fn if success */
            g_wrap_api->close_fn(s) == 0) {
            return 0;
        } else {
            return posix_api->close_fn(s);
        }
    }
    return g_wrap_api->close_fn(s);
}

static int32_t do_poll(struct pollfd *fds, nfds_t nfds, int32_t timeout)
{
    if ((select_posix_path() == PATH_KERNEL) || fds == NULL || nfds == 0) {
        return posix_api->poll_fn(fds, nfds, timeout);
    }

    return g_wrap_api->poll_fn(fds, nfds, timeout);
}

static int32_t do_ppoll(struct pollfd *fds, nfds_t nfds, const struct timespec *tmo_p, const sigset_t *sigmask)
{
    int32_t ready;
    int32_t timeout;

    if (fds == NULL || tmo_p == NULL) {
        GAZELLE_RETURN(EINVAL);
    }

    // s * 1000 and ns / 1000000 -> ms
    timeout = (tmo_p == NULL) ? -1 : (tmo_p->tv_sec * 1000 + tmo_p->tv_nsec / 1000000);
    ready = do_poll(fds, nfds, timeout);

    return ready;
}

typedef int32_t (*sigaction_fn)(int32_t signum, const struct sigaction *act, struct sigaction *oldact);
static int32_t do_sigaction(int32_t signum, const struct sigaction *act, struct sigaction *oldact)
{
    if (posix_api == NULL) {
        sigaction_fn sf = (sigaction_fn)dlsym(RTLD_NEXT, "sigaction");
        if (sf == NULL) {
            return -1;
        }
        return sf(signum, act, oldact);
    }

    return lstack_sigaction(signum, act, oldact);
}

#define WRAP_VA_PARAM(_fd, _cmd, _lwip_fcntl, _fcntl_fn) \
    do { \
        unsigned long val; \
        va_list ap; \
        va_start(ap, _cmd); \
        val = va_arg(ap, typeof(val)); \
        va_end(ap); \
        struct lwip_sock *sock = NULL; \
        if (select_posix_path() == PATH_KERNEL || \
            select_fd_posix_path(_fd, &sock) == PATH_KERNEL) \
            return _fcntl_fn(_fd, _cmd, val); \
        int32_t ret = _fcntl_fn(_fd, _cmd, val); \
        if (ret == -1) \
            return ret; \
        return _lwip_fcntl(_fd, _cmd, val); \
    } while (0)


/*  --------------------------------------------------------
 *  -------  LD_PRELOAD mode replacement interface  --------
 *  --------------------------------------------------------
 */
int32_t epoll_create1(int32_t flags)
{
    return do_epoll_create1(flags);
}
int32_t epoll_create(int32_t size)
{
    return do_epoll_create(size);
}
int32_t epoll_ctl(int32_t epfd, int32_t op, int32_t fd, struct epoll_event* event)
{
    return do_epoll_ctl(epfd, op, fd, event);
}
int32_t epoll_wait(int32_t epfd, struct epoll_event* events, int32_t maxevents, int32_t timeout)
{
    return do_epoll_wait(epfd, events, maxevents, timeout);
}
int32_t fcntl64(int32_t s, int32_t cmd, ...)
{
    WRAP_VA_PARAM(s, cmd, lwip_fcntl, posix_api->fcntl64_fn);
}
int32_t fcntl(int32_t s, int32_t cmd, ...)
{
    WRAP_VA_PARAM(s, cmd, lwip_fcntl, posix_api->fcntl_fn);
}
int32_t ioctl(int32_t s, int32_t cmd, ...)
{
    WRAP_VA_PARAM(s, cmd, lwip_ioctl, posix_api->ioctl_fn);
}
int32_t accept(int32_t s, struct sockaddr *addr, socklen_t *addrlen)
{
    return do_accept(s, addr, addrlen);
}
int32_t accept4(int32_t s, struct sockaddr *addr, socklen_t *addrlen, int32_t flags)
{
    return do_accept4(s, addr, addrlen, flags);
}
int32_t bind(int32_t s, const struct sockaddr *name, socklen_t namelen)
{
    return do_bind(s, name, namelen);
}
int32_t connect(int32_t s, const struct sockaddr *name, socklen_t namelen)
{
    return do_connect(s, name, namelen);
}
int32_t listen(int32_t s, int32_t backlog)
{
    return do_listen(s, backlog);
}
int32_t getpeername(int32_t s, struct sockaddr *name, socklen_t *namelen)
{
    return do_getpeername(s, name, namelen);
}
int32_t getsockname(int32_t s, struct sockaddr *name, socklen_t *namelen)
{
    return do_getsockname(s, name, namelen);
}
int32_t getsockopt(int32_t s, int32_t level, int32_t optname, void *optval, socklen_t *optlen)
{
    return do_getsockopt(s, level, optname, optval, optlen);
}
int32_t setsockopt(int32_t s, int32_t level, int32_t optname, const void *optval, socklen_t optlen)
{
    return do_setsockopt(s, level, optname, optval, optlen);
}
int32_t socket(int32_t domain, int32_t type, int32_t protocol)
{
    return do_socket(domain, type, protocol);
}
ssize_t read(int32_t s, void *mem, size_t len)
{
    return do_read(s, mem, len);
}
ssize_t readv(int32_t s, const struct iovec *iov, int iovcnt)
{
    return do_readv(s, iov, iovcnt);
}
ssize_t write(int32_t s, const void *mem, size_t size)
{
    return do_write(s, mem, size);
}
ssize_t writev(int32_t s, const struct iovec *iov, int iovcnt)
{
    return do_writev(s, iov, iovcnt);
}
ssize_t __wrap_write(int32_t s, const void *mem, size_t size)
{
    return do_write(s, mem, size);
}
ssize_t __wrap_writev(int32_t s, const struct iovec *iov, int iovcnt)
{
    return do_writev(s, iov, iovcnt);
}
ssize_t recv(int32_t sockfd, void *buf, size_t len, int32_t flags)
{
    return do_recv(sockfd, buf, len, flags);
}
ssize_t send(int32_t sockfd, const void *buf, size_t len, int32_t flags)
{
    return do_send(sockfd, buf, len, flags);
}
ssize_t recvmsg(int32_t s, struct msghdr *message, int32_t flags)
{
    return do_recvmsg(s, message, flags);
}
ssize_t sendmsg(int32_t s, const struct msghdr *message, int32_t flags)
{
    return do_sendmsg(s, message, flags);
}
ssize_t recvfrom(int32_t sockfd, void *buf, size_t len, int32_t flags,
                 struct sockaddr *addr, socklen_t *addrlen)
{
    return do_recvfrom(sockfd, buf, len, flags, addr, addrlen);
}
ssize_t sendto(int32_t sockfd, const void *buf, size_t len, int32_t flags,
               const struct sockaddr *addr, socklen_t addrlen)
{
    return do_sendto(sockfd, buf, len, flags, addr, addrlen);
}
int32_t close(int32_t s)
{
    return do_close(s);
}
int32_t poll(struct pollfd *fds, nfds_t nfds, int32_t timeout)
{
    return do_poll(fds, nfds, timeout);
}
int32_t ppoll(struct pollfd *fds, nfds_t nfds, const struct timespec *tmo_p, const sigset_t *sigmask)
{
    return do_ppoll(fds, nfds, tmo_p, sigmask);
}
int32_t sigaction(int32_t signum, const struct sigaction *act, struct sigaction *oldact)
{
    return do_sigaction(signum, act, oldact);
}
pid_t fork(void)
{
    return lstack_fork();
}

/*  --------------------------------------------------------
 *  -------  Compile mode replacement interface  -----------
 *  --------------------------------------------------------
 */

int32_t __wrap_epoll_create1(int32_t size)
{
    return do_epoll_create1(size);
}
int32_t __wrap_epoll_create(int32_t size)
{
    return do_epoll_create(size);
}
int32_t __wrap_epoll_ctl(int32_t epfd, int32_t op, int32_t fd, struct epoll_event* event)
{
    return do_epoll_ctl(epfd, op, fd, event);
}
int32_t __wrap_epoll_wait(int32_t epfd, struct epoll_event* events, int32_t maxevents, int32_t timeout)
{
    return do_epoll_wait(epfd, events, maxevents, timeout);
}
int32_t __wrap_fcntl64(int32_t s, int32_t cmd, ...)
{
    WRAP_VA_PARAM(s, cmd, lwip_fcntl, posix_api->fcntl64_fn);
}
int32_t __wrap_fcntl(int32_t s, int32_t cmd, ...)
{
    WRAP_VA_PARAM(s, cmd, lwip_fcntl, posix_api->fcntl_fn);
}
int32_t __wrap_ioctl(int32_t s, int32_t cmd, ...)
{
    WRAP_VA_PARAM(s, cmd, lwip_ioctl, posix_api->ioctl_fn);
}

int32_t __wrap_accept(int32_t s, struct sockaddr *addr, socklen_t *addrlen)
{
    return do_accept(s, addr, addrlen);
}
int32_t __wrap_accept4(int32_t s, struct sockaddr *addr, socklen_t *addrlen, int32_t flags)
{
    return do_accept4(s, addr, addrlen, flags);
}
int32_t __wrap_bind(int32_t s, const struct sockaddr *name, socklen_t namelen)
{
    return do_bind(s, name, namelen);
}
int32_t __wrap_connect(int32_t s, const struct sockaddr *name, socklen_t namelen)
{
    return do_connect(s, name, namelen);
}
int32_t __wrap_listen(int32_t s, int32_t backlog)
{
    return do_listen(s, backlog);
}
int32_t __wrap_getpeername(int32_t s, struct sockaddr *name, socklen_t *namelen)
{
    return do_getpeername(s, name, namelen);
}
int32_t __wrap_getsockname(int32_t s, struct sockaddr *name, socklen_t *namelen)
{
    return do_getsockname(s, name, namelen);
}
int32_t __wrap_getsockopt(int32_t s, int32_t level, int32_t optname, void *optval, socklen_t *optlen)
{
    return do_getsockopt(s, level, optname, optval, optlen);
}
int32_t __wrap_setsockopt(int32_t s, int32_t level, int32_t optname, const void *optval, socklen_t optlen)
{
    return do_setsockopt(s, level, optname, optval, optlen);
}
int32_t __wrap_socket(int32_t domain, int32_t type, int32_t protocol)
{
    return do_socket(domain, type, protocol);
}
ssize_t __wrap_read(int32_t s, void *mem, size_t len)
{
    return do_read(s, mem, len);
}
ssize_t __wrap_readv(int32_t s, const struct iovec *iov, int iovcnt)
{
    return do_readv(s, iov, iovcnt);
}
ssize_t __wrap_recv(int32_t sockfd, void *buf, size_t len, int32_t flags)
{
    return do_recv(sockfd, buf, len, flags);
}
ssize_t __wrap_send(int32_t sockfd, const void *buf, size_t len, int32_t flags)
{
    return do_send(sockfd, buf, len, flags);
}
ssize_t __wrap_recvmsg(int32_t s, struct msghdr *message, int32_t flags)
{
    return do_recvmsg(s, message, flags);
}
ssize_t __wrap_sendmsg(int32_t s, const struct msghdr *message, int32_t flags)
{
    return do_sendmsg(s, message, flags);
}
ssize_t __wrap_recvfrom(int32_t sockfd, void *buf, size_t len, int32_t flags,
                        struct sockaddr *addr, socklen_t *addrlen)
{
    return do_recvfrom(sockfd, buf, len, flags, addr, addrlen);
}
ssize_t __wrap_sendto(int32_t sockfd, const void *buf, size_t len, int32_t flags,
                      const struct sockaddr *addr, socklen_t addrlen)
{
    return do_sendto(sockfd, buf, len, flags, addr, addrlen);
}
int32_t __wrap_close(int32_t s)
{
    return do_close(s);
}
int32_t __wrap_poll(struct pollfd *fds, nfds_t nfds, int32_t timeout)
{
    return do_poll(fds, nfds, timeout);
}
int32_t __wrap_ppoll(struct pollfd *fds, nfds_t nfds, const struct timespec *tmo_p, const sigset_t *sigmask)
{
    return do_ppoll(fds, nfds, tmo_p, sigmask);
}
int32_t __wrap_sigaction(int32_t signum, const struct sigaction *act, struct sigaction *oldact)
{
    return do_sigaction(signum, act, oldact);
}
pid_t __wrap_fork(void)
{
    return lstack_fork();
}
