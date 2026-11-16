#include <ifirm/fs.h>
#include <ifirm/virtio.h>
#include <ifirm/console.h>
#include <ifirm/status.h>

#define FS_HDR_SECTORS 8u
#define FS_HDR_SIZE    (FS_HDR_SECTORS * 512u)

static u8  g_hdr[FS_HDR_SIZE];
static u32 g_nfiles;
static u32 g_bytes;

static int name_eq(const char *a, const char *b)
{
    for (u32 i = 0; i < FS_NAME_LEN; i++) {
        if (a[i] != b[i]) { return 0; }
        if (a[i] == '\0') { return 1; }
    }
    return 1;
}

ifirm_status_t fs_init(void)
{
    ifirm_status_t st = virtio_blk_init();
    if (st != IFIRM_OK) { return st; }

    st = virtio_blk_read(0u, FS_HDR_SECTORS, g_hdr);
    if (st != IFIRM_OK) { return st; }

    for (u32 i = 0; i < 8u; i++) {
        if (g_hdr[i] != (u8)FS_MAGIC[i]) { return IFIRM_ERR_BAD_DIGEST; }
    }

    u32 *w = (u32 *)(void *)(g_hdr + 8);
    if (w[0] != 1u) { return IFIRM_ERR_UNSUPPORTED; }
    g_nfiles = w[1];
    g_bytes = w[2];
    if (g_nfiles > FS_MAX_FILES) { return IFIRM_ERR_CORRUPT; }
    return IFIRM_OK;
}

u32 fs_count(void) { return g_nfiles; }
u32 fs_bytes(void) { return g_bytes; }

static fs_entry_t *entry_at(u32 i)
{
    return (fs_entry_t *)(void *)(g_hdr + 32u + i * sizeof(fs_entry_t));
}

ifirm_status_t fs_lookup(const char *name, fs_entry_t *out)
{
    for (u32 i = 0; i < g_nfiles; i++) {
        fs_entry_t *e = entry_at(i);
        if (name_eq(e->name, name)) {
            *out = *e;
            return IFIRM_OK;
        }
    }
    return IFIRM_ERR_NOT_FOUND;
}

ifirm_status_t fs_read(const char *name, void *dst, u32 cap, u32 *out_len)
{
    fs_entry_t e;
    ifirm_status_t st = fs_lookup(name, &e);
    if (st != IFIRM_OK) { return st; }
    if (e.size > cap) { return IFIRM_ERR_NO_SPACE; }

    u32 sectors = (e.size + 511u) / 512u;
    st = virtio_blk_read(e.lba, sectors, dst);
    if (st != IFIRM_OK) { return st; }
    if (out_len != NULL) { *out_len = e.size; }
    return IFIRM_OK;
}
