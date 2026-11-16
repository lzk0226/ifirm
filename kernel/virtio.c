#include <ifirm/virtio.h>
#include <ifirm/pmm.h>
#include <ifirm/console.h>
#include <ifirm/status.h>

/* virtio-mmio 寄存器偏移（virtio 1.1 §4.2.2） */
#define R_MAGIC        0x000u
#define R_VERSION      0x004u
#define R_DEVICE_ID    0x008u
#define R_DEV_FEAT     0x010u
#define R_DEV_FEAT_SEL 0x014u
#define R_DRV_FEAT     0x020u
#define R_DRV_FEAT_SEL 0x024u
#define R_GUEST_PAGE   0x028u
#define R_QUEUE_SEL    0x030u
#define R_QUEUE_NUM_MAX 0x034u
#define R_QUEUE_NUM    0x038u
#define R_QUEUE_ALIGN  0x03cu
#define R_QUEUE_PFN    0x040u
#define R_QUEUE_READY  0x044u
#define R_QUEUE_NOTIFY 0x050u
#define R_INT_STATUS   0x060u
#define R_INT_ACK      0x064u
#define R_STATUS       0x070u
#define R_QDESC_LO     0x080u
#define R_QDESC_HI     0x084u
#define R_QAVAIL_LO    0x090u
#define R_QAVAIL_HI    0x094u
#define R_QUSED_LO     0x0a0u
#define R_QUSED_HI     0x0a4u
#define R_CONFIG       0x100u

#define ST_ACKNOWLEDGE 1u
#define ST_DRIVER      2u
#define ST_DRIVER_OK   4u
#define ST_FEATURES_OK 8u

#define VQ_SIZE 8u
#define VQ_DESC_NEXT  1u
#define VQ_DESC_WRITE 2u

typedef struct {
    u64 addr;
    u32 len;
    u16 flags;
    u16 next;
} vq_desc_t;

typedef struct {
    u16 flags;
    u16 idx;
    u16 ring[VQ_SIZE];
} vq_avail_t;

typedef struct {
    u32 id;
    u32 len;
} vq_used_elem_t;

typedef struct {
    u16 flags;
    u16 idx;
    vq_used_elem_t ring[VQ_SIZE];
} vq_used_t;

typedef struct {
    u32 type;
    u32 reserved;
    u64 sector;
} blk_req_t;

static volatile u32 *g_mmio;
static u32 g_version;
static vq_desc_t *g_desc;
static vq_avail_t *g_avail;
static vq_used_t  *g_used;
static u16 g_last_used;
static u64 g_capacity;
static blk_req_t g_req;
static u8 g_status_byte;

static inline u32 rd(u32 off) { return g_mmio[off / 4u]; }
static inline void wr(u32 off, u32 v) { g_mmio[off / 4u] = v; }

static void zero(void *p, u64 n)
{
    u8 *b = (u8 *)p;
    for (u64 i = 0; i < n; i++) { b[i] = 0u; }
}

static void barrier(void)
{
    __asm__ volatile ("fence rw, rw" ::: "memory");
}

u32 virtio_blk_version(void) { return g_version; }
u64 virtio_blk_capacity(void) { return g_capacity; }

/* 连续两页（legacy 队列要求 desc/avail/used 在同一段连续物理内存里） */
static u64 alloc_contig2(void)
{
    for (u32 try = 0; try < 64u; try++) {
        u64 a = pmm_alloc();
        if (a == 0u) { return 0u; }
        u64 b = pmm_alloc();
        if (b == 0u) { pmm_free(a); return 0u; }
        if (b == a + PAGE_SIZE) { zero((void *)a, 2u * PAGE_SIZE); return a; }
        pmm_free(a);
        pmm_free(b);
    }
    return 0u;
}

ifirm_status_t virtio_blk_init(void)
{
    for (u32 i = 0; i < VIRTIO_MMIO_SLOTS; i++) {
        volatile u32 *base = (volatile u32 *)(u64)(VIRTIO_MMIO_BASE + i * VIRTIO_MMIO_STRIDE);
        if (base[R_MAGIC / 4u] != VIRTIO_MAGIC) { continue; }
        if (base[R_DEVICE_ID / 4u] != VIRTIO_ID_BLOCK) { continue; }
        g_mmio = base;
        break;
    }
    if (g_mmio == NULL) { return IFIRM_ERR_NOT_FOUND; }

    g_version = rd(R_VERSION);

    /* 复位 → ACK → DRIVER */
    wr(R_STATUS, 0u);
    u32 st = ST_ACKNOWLEDGE;
    wr(R_STATUS, st);
    st |= ST_DRIVER;
    wr(R_STATUS, st);

    if (g_version >= 2u) {
        /* 必须接受 VIRTIO_F_VERSION_1（bit 32）：feature 选择页 1 的 bit 0 */
        wr(R_DRV_FEAT_SEL, 1u);
        wr(R_DRV_FEAT, 1u);
        wr(R_DRV_FEAT_SEL, 0u);
        wr(R_DRV_FEAT, 0u);          /* 其余特性一律不要（只做同步读） */
        wr(R_STATUS, st | ST_FEATURES_OK);
        if ((rd(R_STATUS) & ST_FEATURES_OK) == 0u) {
            wr(R_STATUS, st | 0x80u); /* FAILED */
            return IFIRM_ERR_UNSUPPORTED;
        }
    } else {
        wr(R_GUEST_PAGE, PAGE_SIZE);
    }

    wr(R_QUEUE_SEL, 0u);
    u32 num_max = rd(R_QUEUE_NUM_MAX);
    if (num_max < VQ_SIZE) { return IFIRM_ERR_NO_SPACE; }
    wr(R_QUEUE_NUM, VQ_SIZE);

    if (g_version >= 2u) {
        u64 desc_pa = pmm_alloc();
        u64 avail_pa = pmm_alloc();
        u64 used_pa = pmm_alloc();
        if (desc_pa == 0u || avail_pa == 0u || used_pa == 0u) { return IFIRM_ERR_NO_MEMORY; }
        zero((void *)desc_pa, PAGE_SIZE);
        zero((void *)avail_pa, PAGE_SIZE);
        zero((void *)used_pa, PAGE_SIZE);
        g_desc = (vq_desc_t *)desc_pa;
        g_avail = (vq_avail_t *)avail_pa;
        g_used = (vq_used_t *)used_pa;
        wr(R_QDESC_LO, (u32)desc_pa);
        wr(R_QDESC_HI, (u32)(desc_pa >> 32));
        wr(R_QAVAIL_LO, (u32)avail_pa);
        wr(R_QAVAIL_HI, (u32)(avail_pa >> 32));
        wr(R_QUSED_LO, (u32)used_pa);
        wr(R_QUSED_HI, (u32)(used_pa >> 32));
        wr(R_QUEUE_READY, 1u);
    } else {
        u64 q = alloc_contig2();
        if (q == 0u) { return IFIRM_ERR_NO_MEMORY; }
        g_desc = (vq_desc_t *)q;
        g_avail = (vq_avail_t *)(q + 16u * VQ_SIZE);
        g_used = (vq_used_t *)(q + PAGE_SIZE);
        wr(R_QUEUE_ALIGN, PAGE_SIZE);
        wr(R_QUEUE_PFN, (u32)(q >> 12));
    }

    wr(R_STATUS, st | ST_DRIVER_OK);

    /* 容量在 config 空间（字节数），legacy 与 modern 都是 u64 */
    u64 lo = rd(R_CONFIG);
    u64 hi = rd(R_CONFIG + 4u);
    g_capacity = (hi << 32) | lo;
    return IFIRM_OK;
}

/* type: 0 = VIRTIO_BLK_T_IN（读），1 = VIRTIO_BLK_T_OUT（写） */
static ifirm_status_t blk_xfer(u32 type, u64 lba, u32 count, void *buf)
{
    if (g_mmio == NULL) { return IFIRM_ERR_NOT_READY; }
    if (count == 0u) { return IFIRM_OK; }

    g_req.type = type;
    g_req.reserved = 0u;
    g_req.sector = lba;
    g_status_byte = 0xffu;

    g_desc[0].addr = (u64)&g_req;
    g_desc[0].len = sizeof(g_req);
    g_desc[0].flags = VQ_DESC_NEXT;
    g_desc[0].next = 1u;

    g_desc[1].addr = (u64)buf;
    g_desc[1].len = count * 512u;
    /* 读：设备写这块内存；写：设备从这块内存读，不能带 WRITE */
    g_desc[1].flags = VQ_DESC_NEXT | ((type == 0u) ? VQ_DESC_WRITE : 0u);
    g_desc[1].next = 2u;

    g_desc[2].addr = (u64)&g_status_byte;
    g_desc[2].len = 1u;
    g_desc[2].flags = VQ_DESC_WRITE;
    g_desc[2].next = 0u;

    g_avail->ring[g_avail->idx % VQ_SIZE] = 0u;
    barrier();
    g_avail->idx = (u16)(g_avail->idx + 1u);
    barrier();
    wr(R_QUEUE_NOTIFY, 0u);

    u32 spin = 0u;
    while (g_used->idx == g_last_used) {
        barrier();
        if (++spin > 20000000u) { return IFIRM_ERR_TIMEOUT; }
    }
    g_last_used = g_used->idx;
    if (rd(R_INT_STATUS) != 0u) { wr(R_INT_ACK, rd(R_INT_STATUS)); }

    return (g_status_byte == 0u) ? IFIRM_OK : IFIRM_ERR_CORRUPT;
}

ifirm_status_t virtio_blk_read(u64 lba, u32 count, void *dst)
{
    return blk_xfer(0u, lba, count, dst);
}

ifirm_status_t virtio_blk_write(u64 lba, u32 count, const void *src)
{
    return blk_xfer(1u, lba, count, (void *)src);
}
