#include <ifirm/types.h>
#include <ifirm/template.h>
#include <ifirm/status.h>

/* FNV-1a 64：可以分段续算（text||data 两段共用一条链） */
static u64 digest_cont(const u8 *p, u64 n, u64 h)
{
    for (u64 i = 0; i < n; i++) { h ^= (u64)p[i]; h *= 0x100000001b3ull; }
    return h;
}

u64 tpl_digest(const u8 *p, u64 n)
{
    return digest_cont(p, n, 0xcbf29ce484222325ull);
}

static bool magic_ok(const u8 *m)
{
    static const char want[8] = { 'I','F','I','R','M','T','P','L' };
    for (u32 i = 0; i < 8u; i++) {
        if (m[i] != (u8)want[i]) { return false; }
    }
    return true;
}

/* 原子模板的准入检查：头完整 + 版本对 + 长度合理 + 摘要一致（覆盖 text||data） */
ifirm_status_t tpl_verify(const u8 *img, u64 avail, u32 *text_len, u32 *data_len, u32 *name_hash)
{
    if (img == NULL || avail < (u64)TPL_HDR_SIZE) { return IFIRM_ERR_CORRUPT; }
    const tpl_hdr_t *h = (const tpl_hdr_t *)(const void *)img;
    if (!magic_ok(h->magic)) { return IFIRM_ERR_CORRUPT; }
    if (h->version != TPL_VERSION || h->hdr_size != (u16)TPL_HDR_SIZE) { return IFIRM_ERR_CORRUPT; }
    if (h->text_len == 0u) { return IFIRM_ERR_CORRUPT; }
    if ((u64)h->text_len + (u64)h->data_len + (u64)TPL_HDR_SIZE > avail) { return IFIRM_ERR_CORRUPT; }
    if ((u64)h->text_len > (u64)TPL_MAX_TEXT || (u64)h->data_len > (u64)TPL_MAX_DATA) { return IFIRM_ERR_CORRUPT; }

    u64 d = digest_cont(img + TPL_HDR_SIZE, (u64)h->text_len, 0xcbf29ce484222325ull);
    d = digest_cont(img + TPL_HDR_SIZE + h->text_len, (u64)h->data_len, d);
    if (d != h->digest) { return IFIRM_ERR_BAD_DIGEST; }
    if (text_len != NULL) { *text_len = h->text_len; }
    if (data_len != NULL) { *data_len = h->data_len; }
    if (name_hash != NULL) { *name_hash = h->name_hash; }
    return IFIRM_OK;
}
