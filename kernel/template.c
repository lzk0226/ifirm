#include <ifirm/template.h>
#include <ifirm/types.h>
#include <ifirm/status.h>

u32 tpl_name_hash(const char *name)
{
    u32 h = 2166136261u;
    for (u32 i = 0; name[i] != '\0' && i < TPL_NAME_LEN; i++) {
        h ^= (u32)(u8)name[i];
        h *= 16777619u;
    }
    return h;
}

u64 tpl_digest(const u8 *p, u64 n)
{
    u64 h = 1469598103934665603ull;
    for (u64 i = 0; i < n; i++) {
        h ^= (u64)p[i];
        h *= 1099511628211ull;
    }
    return h;
}

ifirm_status_t tpl_verify(const u8 *img, u64 avail, const tpl_hdr_t **out)
{
    if (img == NULL || avail < TPL_HDR_SIZE) { return IFIRM_ERR_CORRUPT; }
    const tpl_hdr_t *h = (const tpl_hdr_t *)(const void *)img;

    static const char magic[8] = { 'I', 'F', 'I', 'R', 'M', 'T', 'P', 'L' };
    for (u32 i = 0; i < 8u; i++) {
        if (h->magic[i] != (u8)magic[i]) { return IFIRM_ERR_CORRUPT; }
    }
    if (h->version != TPL_VERSION || h->hdr_size != TPL_HDR_SIZE) { return IFIRM_ERR_UNSUPPORTED; }
    if (h->abi != TPL_ABI) { return IFIRM_ERR_UNSUPPORTED; }
    if (h->text_len == 0u || h->text_len > TPL_MAX_TEXT) { return IFIRM_ERR_CORRUPT; }
    if (h->data_len > TPL_MAX_DATA) { return IFIRM_ERR_CORRUPT; }
    if (h->stack_pages == 0u || h->stack_pages > 16u) { return IFIRM_ERR_CORRUPT; }
    if (TPL_HDR_SIZE + (u64)h->text_len + (u64)h->data_len > avail) { return IFIRM_ERR_CORRUPT; }
    if (tpl_digest(img + TPL_HDR_SIZE, (u64)h->text_len + (u64)h->data_len) != h->digest) {
        return IFIRM_ERR_BAD_DIGEST;
    }
    if (tpl_name_hash(h->name) != h->name_hash) { return IFIRM_ERR_BAD_DIGEST; }
    if (out != NULL) { *out = h; }
    return IFIRM_OK;
}
