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

#include <sys/types.h>
#include <lwip/sockets.h>
#include <lwip/tcp.h>
#include <lwipsock.h>
#include <arch/sys_arch.h>
#include <lwip/pbuf.h>
#include <lwip/priv/tcp_priv.h>
#include <securec.h>
#include <rte_errno.h>

#include "gazelle_base_func.h"
#include "lstack_ethdev.h"
#include "lstack_protocol_stack.h"
#include "lstack_log.h"
#include "lstack_weakup.h"
#include "lstack_lwip.h"

#define HALF_DIVISOR                    (2)
#define USED_IDLE_WATERMARK             (VDEV_IDLE_QUEUE_SZ >> 2)

void get_sockaddr_by_fd(struct sockaddr_in *addr, struct lwip_sock *sock)
{
    (void)memset_s(addr, sizeof(*addr), 0, sizeof(*addr));

    addr->sin_family = AF_INET;
    addr->sin_port = htons(sock->conn->pcb.tcp->local_port);
    (void)memcpy_s(&addr->sin_addr.s_addr, sizeof(struct in_addr), &sock->conn->pcb.tcp->local_ip,
        sizeof(struct in_addr));
}

void listen_list_add_node(int32_t head_fd, int32_t add_fd)
{
    struct lwip_sock *sock = NULL;
    int32_t fd = head_fd;

    while (fd > 0) {
        sock = get_socket(fd);
        if (sock == NULL) {
            LSTACK_LOG(ERR, LSTACK, "tid %ld, %d get sock null\n", get_stack_tid(), fd);
            return;
        }
        fd = sock->nextfd;
    }
    sock->nextfd = add_fd;
}

void gazelle_init_sock(int32_t fd)
{
    struct lwip_sock *sock = get_socket(fd);
    if (sock == NULL) {
        return;
    }

    int32_t ret;
    char name[RTE_RING_NAMESIZE] = {0};
    static uint32_t name_tick = 0;

    ret = snprintf_s(name, sizeof(name), RTE_RING_NAMESIZE - 1, "%s_%d", "sock_recv", name_tick++);
    if (ret < 0) {
        LSTACK_LOG(ERR, LSTACK, "%s create failed.\n", name);
        return;
    }
    sock->recv_ring = rte_ring_create(name, SOCK_RECV_RING_SIZE, rte_socket_id(), RING_F_SP_ENQ | RING_F_SC_DEQ);
    if (sock->recv_ring == NULL) {
        LSTACK_LOG(ERR, LSTACK, "%s create failed. errno: %d.\n", name, rte_errno);
        return;
    }

    ret = snprintf_s(name, sizeof(name), RTE_RING_NAMESIZE - 1, "%s_%d", "sock_send", name_tick++);
    if (ret < 0) {
        LSTACK_LOG(ERR, LSTACK, "%s create failed. errno: %d.\n", name, rte_errno);
        return;
    }
    sock->send_ring = rte_ring_create(name, SOCK_SEND_RING_SIZE, rte_socket_id(), RING_F_SP_ENQ | RING_F_SC_DEQ);
    if (sock->send_ring == NULL) {
        LSTACK_LOG(ERR, LSTACK, "%s create failed. errno: %d.\n", name, rte_errno);
        return;
    }

    sock->stack = get_protocol_stack();
    sock->recv_lastdata = NULL;
    sock->send_lastdata = NULL;
    init_list_node(&sock->recv_list);
    sock->stack->conn_num++;
    sock->nextfd = 0;
}

void gazelle_clean_sock(int32_t fd)
{
    struct lwip_sock *sock = get_socket(fd);
    if (sock == NULL) {
        return;
    }

    /* check null pointer in ring_free func */
    if (sock->recv_ring) {
        rte_ring_free(sock->recv_ring);
    }
    if (sock->send_ring) {
        rte_ring_free(sock->send_ring);
    }

    sock->stack = NULL;
    sock->weakup = NULL;
    sock->events = 0;
    sock->nextfd = 0;

    if (sock->recv_lastdata) {
        pbuf_free(sock->recv_lastdata);
        sock->recv_lastdata = NULL;
    }

    if (sock->send_lastdata) {
        pbuf_free(sock->send_lastdata);
        sock->send_lastdata = NULL;
    }

    list_del_node_init(&sock->recv_list);
}

static void gazelle_free_pbuf(struct pbuf *p)
{
    struct rte_mbuf *mbuf = pbuf_to_mbuf(p);
    rte_pktmbuf_free(mbuf);
}

static struct pbuf *tcp_pktmbuf_alloc(struct rte_mempool *pool, pbuf_layer layer, u16_t len)
{
    struct rte_mbuf *mbuf = NULL;
    struct pbuf *pbuf = NULL;
    struct pbuf_custom *pbuf_custom = NULL;

    u16_t offset = layer;
    u16_t total_len = LWIP_MEM_ALIGN_SIZE(offset) + LWIP_MEM_ALIGN_SIZE(len);

    int32_t ret = rte_pktmbuf_alloc_bulk(pool, &mbuf, 1);
    if (ret) {
        LSTACK_LOG(ERR, LSTACK, "tid %ld pktmbuf_alloc failed\n", get_stack_tid());
        return NULL;
    }

    uint8_t *data = (uint8_t *)rte_pktmbuf_append(mbuf, total_len);
    if (!data) {
        rte_pktmbuf_free(mbuf);
        return NULL;
    }

    pbuf_custom = mbuf_to_pbuf(mbuf);
    pbuf_custom->custom_free_function = gazelle_free_pbuf;
    pbuf = pbuf_alloced_custom(layer, len, PBUF_RAM, pbuf_custom, data, total_len);
    pbuf->flags |= PBUF_FLAG_SND_SAVE_CPY;

    return pbuf;
}

void stack_replenish_send_idlembuf(struct protocol_stack *stack)
{
    uint32_t replenish_cnt = rte_ring_free_count(stack->send_idle_ring);

    for (uint32_t i = 0; i < replenish_cnt; i++) {
        struct pbuf *pbuf = tcp_pktmbuf_alloc(stack->tx_pktmbuf_pool, PBUF_TRANSPORT, TCP_MSS);
        if (pbuf == NULL) {
            break;
        }

        int32_t ret = rte_ring_sp_enqueue(stack->send_idle_ring, (void *)pbuf);
        if (ret < 0) {
            gazelle_free_pbuf(pbuf);
            break;
        }
    }
}

static void update_lwip_outevent(struct netconn *conn)
{
    /* If the queued byte- or pbuf-count drops below the configured low-water limit,
       let select mark this pcb as writable again. */
    if ((conn->pcb.tcp != NULL) && (tcp_sndbuf(conn->pcb.tcp) > TCP_SNDLOWAT) &&
        (tcp_sndqueuelen(conn->pcb.tcp) < TCP_SNDQUEUELOWAT)) {
        netconn_clear_flags(conn, NETCONN_FLAG_CHECK_WRITESPACE);
        API_EVENT(conn, NETCONN_EVT_SENDPLUS, 0);
    }
}

uint32_t stack_send(int32_t fd, int32_t flags)
{
    struct lwip_sock *sock = get_socket(fd);
    if (sock == NULL) {
        return -EINVAL;
    }

    struct pbuf *pbuf = NULL;
    ssize_t send, pbuf_len;
    int32_t ret;
    uint16_t available;

    do {
        if (sock->send_lastdata) {
            pbuf = sock->send_lastdata;
            sock->send_lastdata = NULL;
        } else {
            ret = rte_ring_sc_dequeue(sock->send_ring, (void **)&pbuf);
            if (ret != 0) {
                break;
            }
        }

        available = tcp_sndbuf(sock->conn->pcb.tcp);
        if (available < pbuf->tot_len) {
            sock->send_lastdata = pbuf;
            break;
        }

        pbuf_len = pbuf->tot_len;
        send = lwip_send(fd, pbuf, pbuf->tot_len, flags);
        if (send != pbuf_len) {
            sock->stack->stats.write_lwip_drop++;
            break;
        }

        sock->stack->stats.write_lwip_cnt++;
    } while (ret == 0);

    update_lwip_outevent(sock->conn);

    return rte_ring_count(sock->send_ring);
}

ssize_t write_stack_data(int32_t fd, const void *buf, size_t len)
{
    struct lwip_sock *sock = get_socket(fd);
    if (sock == NULL) {
        LSTACK_LOG(ERR, LSTACK, "get_socket null fd %d.\n", fd);
        GAZELLE_RETURN(EINVAL);
    }

    uint32_t free_count = rte_ring_free_count(sock->send_ring);
    if (free_count == 0) {
        GAZELLE_RETURN(EAGAIN);
    }

    uint32_t idle_cont = rte_ring_count(sock->stack->send_idle_ring);
    struct pbuf *pbuf = NULL;
    ssize_t send_len = 0;
    size_t copy_len;
    uint32_t send_pkt = 0;

    while (send_len < len && send_pkt < idle_cont) {
        int32_t ret = rte_ring_sc_dequeue(sock->stack->send_idle_ring, (void **)&pbuf);
        if (ret < 0) {
            sock->stack->stats.app_write_idlefail++;
            break;
        }

        copy_len = (len - send_len > pbuf->len) ? pbuf->len : (len - send_len);
        pbuf_take(pbuf, buf + send_len, copy_len);
        pbuf->tot_len = pbuf->len = copy_len;

        ret = rte_ring_sp_enqueue(sock->send_ring, pbuf);
        if (ret != 0) {
            sock->stack->stats.app_write_drop++;
            gazelle_free_pbuf(pbuf);
            break;
        }

        sock->stack->stats.app_write_cnt++;
        send_len += copy_len;
        send_pkt++;
    }

    if (rte_ring_free_count(sock->stack->send_idle_ring) > USED_IDLE_WATERMARK) {
        rpc_call_replenish_idlembuf(sock->stack);
    }

    return send_len;
}

ssize_t read_lwip_data(struct lwip_sock *sock, int32_t flags, u8_t apiflags)
{
    if (sock->conn->recvmbox == NULL) {
        return 0;
    }

    uint32_t free_count = rte_ring_free_count(sock->recv_ring);
    uint32_t data_count = rte_ring_count(sock->conn->recvmbox->ring);

    struct pbuf *pbuf = NULL;
    uint32_t read_count = LWIP_MIN(free_count, data_count);
    ssize_t recv_len = 0;
    int32_t ret;

    for (uint32_t i = 0; i < read_count; i++) {
        err_t err = netconn_recv_tcp_pbuf_flags(sock->conn, &pbuf, apiflags);
        if (err != ERR_OK) {
            if (recv_len > 0) {
                /* already received data, return that (this trusts in getting the same error from
                   netconn layer again next time netconn_recv is called) */
                break;
            }

            return (err == ERR_CLSD) ? 0 : -1;
        }

        if (!(flags & MSG_PEEK)) {
            ret = rte_ring_sp_enqueue(sock->recv_ring, pbuf);
            if (ret != 0) {
                pbuf_free(pbuf);
                sock->stack->stats.read_lwip_drop++;
                break;
            }
        }

        recv_len += pbuf->len;

        /* once we have some data to return, only add more if we don't need to wait */
        apiflags |= NETCONN_DONTBLOCK | NETCONN_NOFIN;
    }

    if (data_count > free_count) {
        add_recv_list(sock->conn->socket);
    }

    if (recv_len > 0 && (flags & MSG_PEEK) == 0) {
        add_epoll_event(sock->conn, EPOLLIN);
    }
    sock->stack->stats.read_lwip_cnt += read_count;
    return recv_len;
}

ssize_t read_stack_data(int32_t fd, void *buf, size_t len, int32_t flags)
{
    size_t recv_left = len;
    struct pbuf *pbuf = NULL;
    ssize_t recvd = 0;
    int32_t ret;
    u16_t copy_len;

    struct lwip_sock *sock = get_socket(fd);
    if (sock == NULL) {
        LSTACK_LOG(ERR, LSTACK, "get_socket null fd %d.\n", fd);
        GAZELLE_RETURN(EINVAL);
    }
    sock->recv_flags = flags;

    while (recv_left > 0) {
        if (sock->recv_lastdata) {
            pbuf = sock->recv_lastdata;
        } else {
            ret = rte_ring_sc_dequeue(sock->recv_ring, (void **)&pbuf);
            if (ret != 0) {
                break;
            }
        }

        copy_len = (recv_left > pbuf->tot_len) ? pbuf->tot_len : (u16_t)recv_left;
        pbuf_copy_partial(pbuf, buf + recvd, copy_len, 0);

        recvd += copy_len;
        recv_left -= copy_len;

        if (pbuf->tot_len - copy_len > 0) {
            sock->recv_lastdata = pbuf_free_header(pbuf, copy_len);
        } else {
            sock->recv_lastdata = NULL;
            sock->stack->stats.app_read_cnt++;
            pbuf_free(pbuf);
        }
    }

    if (rte_ring_count(sock->recv_ring) || sock->recv_lastdata) {
        if (sock->in_event == 0) {
            rpc_call_addevent(sock->stack, sock, EPOLLIN);
        }
    }

    if (recvd == 0) {
        GAZELLE_RETURN(EAGAIN);
    }
    return recvd;
}

void add_recv_list(int32_t fd)
{
    struct lwip_sock *sock = get_socket(fd);

    if (list_is_empty(&sock->recv_list) && sock->stack) {
        list_add_node(&sock->stack->recv_list, &sock->recv_list);
    }
}

void read_recv_list(void)
{
    struct protocol_stack *stack = get_protocol_stack();
    struct list_node *list = &(stack->recv_list);
    struct list_node *node, *temp;
    struct lwip_sock *sock;
    struct lwip_sock *first_sock = NULL;

    list_for_each_safe(node, temp, list) {
        sock = container_of(node, struct lwip_sock, recv_list);

        /* when read_lwip_data have data wait to read, add sock into recv_list. read_recv_list read this sock again.
           this is dead loop. so every sock just read one time */
        if (sock == first_sock) {
            break;
        }
        if (first_sock == NULL) {
            first_sock = sock;
        }

        /* recv_ring and send_ring maybe create fail, so check here */
        if (sock->conn && sock->recv_ring && sock->send_ring && rte_ring_free_count(sock->recv_ring)) {
            list_del_node_init(&sock->recv_list);
            lwip_recv(sock->conn->socket, NULL, 0, sock->recv_flags);
        }
    }
}

static void copy_pcb_to_conn(struct gazelle_stat_lstack_conn_info *conn, const struct tcp_pcb *pcb)
{
    struct netconn *netconn = (struct netconn *)pcb->callback_arg;

    conn->lip = pcb->local_ip.addr;
    conn->rip = pcb->remote_ip.addr;
    conn->l_port = pcb->local_port;
    conn->r_port = pcb->remote_port;
    conn->in_send = pcb->snd_queuelen;
    conn->tcp_sub_state = pcb->state;

    if (netconn != NULL && netconn->recvmbox != NULL) {
        conn->recv_cnt = rte_ring_count(netconn->recvmbox->ring);

        struct lwip_sock *sock = get_socket(netconn->socket);
        if (sock != NULL && sock->recv_ring != NULL && sock->send_ring != NULL) {
            conn->recv_ring_cnt = rte_ring_count(sock->recv_ring);
            conn->send_ring_cnt = rte_ring_count(sock->send_ring);
        }
    }
}

void get_lwip_conntable(struct rpc_msg *msg)
{
    struct tcp_pcb *pcb = NULL;
    uint32_t conn_num = 0;
    struct gazelle_stat_lstack_conn_info *conn = (struct gazelle_stat_lstack_conn_info *)msg->args[MSG_ARG_0].p;
    uint32_t max_num = msg->args[MSG_ARG_1].u;

    if (conn == NULL) {
        msg->result = -1;
        return;
    }

    for (pcb = tcp_active_pcbs; pcb != NULL && conn_num < max_num; pcb = pcb->next) {
        conn[conn_num].state = ACTIVE_LIST;
        copy_pcb_to_conn(conn + conn_num, pcb);
        conn_num++;
    }

    for (struct tcp_pcb_listen *pcbl = tcp_listen_pcbs.listen_pcbs; pcbl != NULL && conn_num < max_num;
        pcbl = pcbl->next) {
        conn[conn_num].state = LISTEN_LIST;
        conn[conn_num].lip = pcbl->local_ip.addr;
        conn[conn_num].l_port = pcbl->local_port;
        conn[conn_num].tcp_sub_state = pcbl->state;
        struct netconn *netconn = (struct netconn *)pcbl->callback_arg;
        if (netconn != NULL && netconn->acceptmbox != NULL) {
            conn[conn_num].recv_cnt = rte_ring_count(netconn->acceptmbox->ring);
        }
        conn_num++;
    }

    for (pcb = tcp_tw_pcbs; pcb != NULL && conn_num < max_num; pcb = pcb->next) {
        conn[conn_num].state = TIME_WAIT_LIST;
        copy_pcb_to_conn(conn + conn_num, pcb);
        conn_num++;
    }

    msg->result = conn_num;
}

void get_lwip_connnum(struct rpc_msg *msg)
{
    struct tcp_pcb *pcb = NULL;
    struct tcp_pcb_listen *pcbl = NULL;
    uint32_t conn_num = 0;

    for (pcb = tcp_active_pcbs; pcb != NULL; pcb = pcb->next) {
        conn_num++;
    }

    for (pcbl = tcp_listen_pcbs.listen_pcbs; pcbl != NULL; pcbl = pcbl->next) {
        conn_num++;
    }

    for (pcb = tcp_tw_pcbs; pcb != NULL; pcb = pcb->next) {
        conn_num++;
    }

    msg->result = conn_num;
}

void stack_add_event(struct rpc_msg *msg)
{
    struct lwip_sock *sock = msg->args[MSG_ARG_0].p;
    uint32_t event = msg->args[MSG_ARG_1].u;

    add_epoll_event(sock->conn, event);
}

void stack_recvlist_count(struct rpc_msg *msg)
{
    struct protocol_stack *stack = get_protocol_stack();
    struct list_node *list = &(stack->recv_list);
    struct list_node *node, *temp;
    uint32_t count = 0;

    list_for_each_safe(node, temp, list) {
        count++;
    }

    msg->result = count;
}
