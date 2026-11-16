#include <ifirm/tplreg.h>
#include <ifirm/template.h>
#include <ifirm/console.h>
#include <ifirm/status.h>

static tpl_ent_t g_tpl[TPL_MAX];
static u32       g_n;

/* 模板镜像随内核映像一起加载（_binary_tpl_*.img_start），但要先过摘要校验 */
extern const u8 _binary_tpl_producer_img_start[];
extern const u8 _binary_tpl_producer_img_end[];
extern const u8 _binary_tpl_consumer_img_start[];
extern const u8 _binary_tpl_consumer_img_end[];
extern const u8 _binary_tpl_faulty_img_start[];
extern const u8 _binary_tpl_faulty_img_end[];

static ifirm_status_t reg(const u8 *img, const u8 *end)
{
    if (g_n >= TPL_MAX) { return IFIRM_ERR_NO_SPACE; }
    const tpl_hdr_t *h = NULL;
    ifirm_status_t st = tpl_verify(img, (u64)(end - img), &h);
    if (st != IFIRM_OK) { return st; }

    tpl_ent_t *e = &g_tpl[g_n];
    for (u32 i = 0; i < TPL_NAME_LEN; i++) { e->name[i] = h->name[i]; }
    e->name[TPL_NAME_LEN - 1u] = '\0';
    e->img = img;
    e->len = (u64)(end - img);
    e->hdr = h;
    g_n++;

    printk("IFIRM-TPL n=%u name=%s ver=%u abi=%u svc=0x%x stack=%u digest=0x%x\n",
           g_n, e->name, (u32)h->version, (u32)h->abi, (u32)h->services,
           (u32)h->stack_pages, (u32)(h->digest & 0xFFFFFFFFu));
    return IFIRM_OK;
}

ifirm_status_t tplreg_init(void)
{
    g_n = 0;
    ifirm_status_t st = reg(_binary_tpl_producer_img_start, _binary_tpl_producer_img_end);
    if (st == IFIRM_OK) { st = reg(_binary_tpl_consumer_img_start, _binary_tpl_consumer_img_end); }
    if (st == IFIRM_OK) { st = reg(_binary_tpl_faulty_img_start, _binary_tpl_faulty_img_end); }
    if (st != IFIRM_OK) { return st; }
    printk("IFIRM-TPLSET n=%u ok digest=0x%x\n", g_n, (u32)tplreg_composite_digest());
    return IFIRM_OK;
}

u32 tplreg_count(void) { return g_n; }

const tpl_ent_t *tplreg_at(u32 i) { return (i < g_n) ? &g_tpl[i] : NULL; }

const tpl_ent_t *tplreg_find(const char *name)
{
    for (u32 i = 0; i < g_n; i++) {
        bool same = true;
        for (u32 k = 0; k < TPL_NAME_LEN; k++) {
            if (g_tpl[i].name[k] != name[k]) { same = false; break; }
            if (name[k] == '\0') { break; }
        }
        if (same) { return &g_tpl[i]; }
    }
    return NULL;
}

u64 tplreg_composite_digest(void)
{
    u64 h = 1469598103934665603ull;
    for (u32 i = 0; i < g_n; i++) {
        h ^= g_tpl[i].hdr->digest;
        h *= 1099511628211ull;
    }
    return h;
}
