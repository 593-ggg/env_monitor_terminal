#include "tcp_protocol.h"
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/time.h>

int recv_n(int fd, void* buf, size_t len)
{
    uint8_t* ptr = (uint8_t*) buf;
    size_t remain = len;
    ssize_t ret;

    // 循环读取直到凑齐len字节
    while (remain > 0)
    {
        ret = recv(fd, ptr, remain, 0);
        // ret=0 对端正常关闭连接
        if (ret == 0)
        {
            errno = ECONNRESET;
            return -1;
        }
        // 读取出错
        if (ret < 0)
        {
            // 被信号中断，重试
            if (errno == EINTR)
                continue;
            return -1;
        }
        ptr += ret;
        remain -= ret;
    }
    return 0;
}

int send_n(int fd, const void* buf, size_t len)
{
    const uint8_t* ptr = (const uint8_t*) buf;
    size_t remain = len;
    ssize_t ret;

    // MSG_NOSIGNAL：关闭连接时不抛出SIGPIPE杀死进程
    while (remain > 0)
    {
        ret = send(fd, ptr, remain, MSG_NOSIGNAL);
        if (ret <= 0)
        {
            if (errno == EINTR)
                continue;
            return -1;
        }
        ptr += ret;
        remain -= ret;
    }
    return 0;
}

void pkt_header_init(PacketHeader* hdr, enum PKT_TYPE pkt_type, uint8_t pkt_sub_type, uint32_t body_len_host)
{
    memset(hdr, 0, sizeof(PacketHeader));
    // 魔数转网络字节序
    hdr->magic = htobe32(PACKET_MAGIC);
    // 数据包类型转网络字节序
    hdr->pkt_type = htobe16((uint16_t) pkt_type);
    // 数据包细类类型
    hdr->pkt_sub_type = pkt_sub_type;
    // 包体长度转网络字节序
    hdr->body_len = htobe32(body_len_host);
}

int pkt_header_check(const PacketHeader* hdr)
{
    // 转换为本地字节序校验
    uint32_t magic_host = be32toh(hdr->magic);
    uint16_t pkt_type_host = be16toh(hdr->pkt_type);
    uint32_t blen_host = be32toh(hdr->body_len);

    // 校验魔数
    if (magic_host != PACKET_MAGIC)
        return RESP_ERR_MAGIC;
    // 校验数据包类型合法性
    if (pkt_type_host <= PKT_INVALID || pkt_type_host > PKT_HEARTBEAT)
    {
        printf("pkt_type_host: %d\n", pkt_type_host);
        return RESP_ERR_CMD;
    }
    // 校验包体不超过8MB上限
    if (blen_host > MAX_BODY_SIZE)
        return RESP_ERR_BODY_LIMIT;

    return RESP_OK;
}


void packet_init(Packet* pkt)
{
    if (!pkt)
        return;
    // 全部清零
    memset(pkt, 0, sizeof(Packet));
    pkt->body = NULL;
    pkt->body_len = 0;
}

void packet_free_body(Packet* pkt)
{
    if (!pkt)
        return;
    // 释放动态包体内存
    if (pkt->body)
    {
        free(pkt->body);
        pkt->body = NULL;
    }
    pkt->body_len = 0;
    // 包头清零
    memset(&pkt->hdr, 0, sizeof(PacketHeader));
}

int packet_set_body(Packet* pkt, enum PKT_TYPE pkt_type, uint8_t pkt_sub_type, const uint8_t* body,
                    uint32_t body_len_host)
{
    if (!pkt)
        return -1;
    // 先释放原有包体
    packet_free_body(pkt);

    pkt->body_len = body_len_host;
    // 存在包体数据则分配内存拷贝
    if (body_len_host > 0 && body != NULL)
    {
        pkt->body = malloc(body_len_host);
        if (!pkt->body)
            return -1;
        memcpy(pkt->body, body, body_len_host);
    }
    // 自动填充包头并转换字节序
    pkt_header_init(&pkt->hdr, pkt_type, pkt_sub_type, body_len_host);
    return 0;
}


int pkt_send_full(int fd, Packet* pkt)
{
    if (!pkt || fd < 0)
        return -1;

    // 一次性发送完整数据包（包头+包体），不等待应答
    if (send_n(fd, &pkt->hdr, HEADER_SIZE) != 0)
        return -1;
    if (pkt->body_len > 0 && pkt->body != NULL)
    {
        if (send_n(fd, pkt->body, pkt->body_len) != 0)
            return -1;
    }
    return 0;
}


int pkt_recv_full(int fd, Packet* out_pkt)
{
    packet_init(out_pkt);
    // 读取固定32字节包头
    if (recv_n(fd, &out_pkt->hdr, HEADER_SIZE) != 0)
        return -1;

    // 转换主机序包体长度缓存
    uint32_t blen_host = be32toh(out_pkt->hdr.body_len);
    out_pkt->body_len = blen_host;

    // 无包体直接返回
    if (blen_host == 0)
        return 0;

    // 分配包体缓冲区
    out_pkt->body = malloc(blen_host);
    if (!out_pkt->body)
        return -2;

    // 读取全部包体数据
    if (recv_n(fd, out_pkt->body, blen_host) != 0)
    {
        packet_free_body(out_pkt);
        return -1;
    }
    return 0;
}

int client_send_enqueue(ClientConn* conn, Packet* pkt)
{
    pthread_mutex_lock(&conn->send_mtx);
    if (!conn || !pkt || !conn->running)
        return -1;
    int next_tail = (conn->q_tail + 1) % conn->q_capacity;
    if (next_tail == conn->q_head)
    {
        return -1;
    }
    Packet* slot = &conn->send_queue[conn->q_tail];
    packet_free_body(slot);
    packet_set_body(slot, be16toh(pkt->hdr.pkt_type), pkt->hdr.pkt_sub_type, pkt->body, pkt->body_len);
    conn->q_tail = next_tail;
    pthread_mutex_unlock(&conn->send_mtx);
    return 0;
}

Packet* client_send_dequeue(ClientConn* conn)
{
    if (!conn)
        return NULL;
    pthread_mutex_lock(&conn->send_mtx);
    if (conn->q_head == conn->q_tail)
    {
        pthread_mutex_unlock(&conn->send_mtx);
        return NULL;
    }
    Packet* pkt = &conn->send_queue[conn->q_head];
    conn->q_head = (conn->q_head + 1) % conn->q_capacity;
    pthread_mutex_unlock(&conn->send_mtx);
    return pkt;
}
