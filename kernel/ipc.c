#include <ifirm/types.h>
#include <ifirm/ipc.h>
#include <ifirm/status.h>

channel_t g_channels[MAX_CHANNELS];

channel_t *ipc_channel_get(u32 id)
{
    if (id == 0u || id > MAX_CHANNELS) { return NULL; }
    return &g_channels[id - 1u];
}

ifirm_status_t ipc_channel_create(u8 dom_a, u8 dom_b)
{
    for (u32 i = 0; i < MAX_CHANNELS; i++) {
        if (g_channels[i].closed == 0u && g_channels[i].ep_dom[0] == 0u) {
            g_channels[i].ep_dom[0] = dom_a;
            g_channels[i].ep_dom[1] = dom_b;
            g_channels[i].closed = 0u;
            g_channels[i].head = 0u;
            g_channels[i].tail = 0u;
            g_channels[i].count = 0u;
            return IFIRM_OK;
        }
    }
    return IFIRM_ERR_NO_SPACE;
}

ifirm_status_t ipc_enqueue(channel_t *ch, const u8 *msg, u32 len)
{
    if (ch == NULL) { return IFIRM_ERR_BAD_HANDLE; }
    if (len > IPC_MSG_SIZE || ch->count >= IPC_QSLOTS) { return IFIRM_ERR_NO_SPACE; }
    u16 slot = ch->tail;
    for (u32 i = 0; i < len; i++) { ch->data[slot][i] = msg[i]; }
    ch->len[slot] = (u16)len;
    ch->tail = (u16)((ch->tail + 1u) % IPC_QSLOTS);
    ch->count++;
    return IFIRM_OK;
}

ifirm_status_t ipc_dequeue(channel_t *ch, u8 *buf, u32 *len)
{
    if (ch == NULL || ch->count == 0u) { return IFIRM_ERR_AGAIN; }
    u16 slot = ch->head;
    u32 n = ch->len[slot];
    for (u32 i = 0; i < n; i++) { buf[i] = ch->data[slot][i]; }
    ch->head = (u16)((ch->head + 1u) % IPC_QSLOTS);
    ch->count--;
    *len = n;
    return IFIRM_OK;
}

ifirm_status_t ipc_send(channel_t *ch, u8 from_dom, const u8 *msg, u32 len, bool nonblock)
{
    (void)nonblock;
    if (ch == NULL) { return IFIRM_ERR_BAD_HANDLE; }
    if (from_dom != ch->ep_dom[0] && from_dom != ch->ep_dom[1]) { return IFIRM_ERR_DENIED; }
    if (len > IPC_MSG_SIZE) { return IFIRM_ERR_INVALID_ARG; }
    return ipc_enqueue(ch, msg, len);
}

ifirm_status_t ipc_recv(channel_t *ch, u8 from_dom, u8 *buf, u32 cap, u32 *out_len)
{
    if (ch == NULL) { return IFIRM_ERR_BAD_HANDLE; }
    if (from_dom != ch->ep_dom[0] && from_dom != ch->ep_dom[1]) { return IFIRM_ERR_DENIED; }
    if (ch->count == 0u) { return IFIRM_ERR_AGAIN; }
    u32 n = ch->len[ch->head];
    if (n > cap) { return IFIRM_ERR_OVERFLOW; }
    return ipc_dequeue(ch, buf, out_len);
}
