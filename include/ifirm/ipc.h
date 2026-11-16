#ifndef IFIRM_IPC_H
#define IFIRM_IPC_H

#include <ifirm/types.h>
#include <ifirm/status.h>

#define IPC_MSG_SIZE 64u
#define IPC_QSLOTS   4u
#define MAX_CHANNELS 4u

typedef struct {
    u8  data[IPC_QSLOTS][IPC_MSG_SIZE];
    u16 len[IPC_QSLOTS];
    u16 head;
    u16 tail;
    u16 count;
    u8  ep_dom[2];          /* 固定一对端点（§17.2） */
    u16 ep_cap[2];
    u8  call_pending;       /* 1 = 有一次未完成的 call */
    u8  caller_dom;
    u16 caller_tid;
    u64 caller_va;          /* 调用方的回复缓冲（虚拟地址） */
    u32 caller_cap;
    u8  closed;
} channel_t;

extern channel_t g_channels[MAX_CHANNELS];

ifirm_status_t ipc_channel_create(u8 dom_a, u8 dom_b);
channel_t     *ipc_channel_get(u32 id);
ifirm_status_t ipc_enqueue(channel_t *ch, const u8 *msg, u32 len);
ifirm_status_t ipc_dequeue(channel_t *ch, u8 *buf, u32 *len);
ifirm_status_t ipc_send(channel_t *ch, u8 from_dom, const u8 *msg, u32 len, bool nonblock);
ifirm_status_t ipc_recv(channel_t *ch, u8 from_dom, u8 *buf, u32 cap, u32 *out_len);

#endif /* IFIRM_IPC_H */
