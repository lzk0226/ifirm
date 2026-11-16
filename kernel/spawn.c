#include <ifirm/spawn.h>
#include <ifirm/domain.h>
#include <ifirm/fs.h>
#include <ifirm/elf.h>
#include <ifirm/pmm.h>
#include <ifirm/vmm.h>
#include <ifirm/ict.h>
#include <ifirm/cap.h>
#include <ifirm/ca.h>
#include <ifirm/thread.h>
#include <ifirm/console.h>
#include <ifirm/status.h>

#define SPAWN_MAX_ELF (64u * 1024u)
#define SPAWN_FIRST_ID 6u

static u8   g_elf[SPAWN_MAX_ELF];
static char g_names[FS_MAX_FILES][FS_NAME_LEN];

static void zero_page(u64 pa)
{
    u8 *p = (u8 *)pa;
    for (u32 i = 0; i < PAGE_SIZE; i++) { p[i] = 0u; }
}

static ifirm_status_t map_zero(u64 *root, vaddr_t va, u64 perm, u64 *out_pa)
{
    u64 pa = pmm_alloc();
    if (pa == 0u) { return IFIRM_ERR_NO_MEMORY; }
    zero_page(pa);
    ifirm_status_t st = vmm_map(root, va, pa, perm);
    if (st != IFIRM_OK) { pmm_free(pa); return st; }
    if (out_pa != NULL) { *out_pa = pa; }
    return IFIRM_OK;
}

ifirm_status_t domain_spawn_file(const char *name, u16 *out_id, elf_info_t *info)
{
    u32 len = 0u;
    ifirm_status_t st = fs_read(name, g_elf, SPAWN_MAX_ELF, &len);
    if (st != IFIRM_OK) { return st; }

    st = elf_check(g_elf, len);
    if (st != IFIRM_OK) { return st; }

    u16 id = 0u;
    for (u16 i = SPAWN_FIRST_ID; i < MAX_DOMAINS; i++) {
        if (g_domains[i].root == NULL && g_domains[i].state == DOMAIN_FREE) { id = i; break; }
    }
    if (id == 0u) { return IFIRM_ERR_NO_SPACE; }

    domain_t *d = &g_domains[id];
    d->id = id;
    d->state = DOMAIN_READY;
    d->trust_level = 1u;
    d->mem_pages = 0u;
    d->faults = 0u;
    d->tpl_id = 0u;

    /* 文件名拷到静态区：ELF 缓冲下一轮会被覆盖 */
    for (u32 i = 0; i < FS_NAME_LEN; i++) { g_names[id][i] = '\0'; }
    for (u32 i = 0; i + 1u < FS_NAME_LEN && name[i] != '\0'; i++) { g_names[id][i] = name[i]; }
    d->name = g_names[id];

    d->root = vmm_new_as();
    if (d->root == NULL) { return IFIRM_ERR_NO_MEMORY; }

    st = elf_load(d->root, g_elf, len, info);
    if (st != IFIRM_OK) { vmm_release(d->root); d->root = NULL; return st; }
    d->mem_pages = (u16)info->pages;

    /* console 能力只能由 CA 发；charter 里 dom=0 的规则覆盖全部派生域 */
    cap_t con = 0u;
    st = ca_authorize(id, OBJ_DEVICE, CAP_RIGHT_WRITE, 1u, CA_BOOT, &con);
    if (st != IFIRM_OK) { vmm_release(d->root); d->root = NULL; return st; }

    st = ict_install(id, 8u, (u16)((u32)con & CAP_IDX_MASK), ICT_F_NEEDS_CAP, 0u);
    if (st != IFIRM_OK) { vmm_release(d->root); d->root = NULL; return st; }

    u64 ict_pa = 0u;
    st = map_zero(d->root, DOMAIN_ICT_VA, PTE_R | PTE_X | PTE_U, &ict_pa);
    if (st != IFIRM_OK) { vmm_release(d->root); d->root = NULL; return st; }
    ict_build_page(id, (u8 *)ict_pa);
    d->mem_pages++;

    for (u32 i = 0; i < 4u; i++) {
        st = map_zero(d->root, DOMAIN_USTACK_BASE + (u64)i * PAGE_SIZE,
                      PTE_R | PTE_W | PTE_U, NULL);
        if (st != IFIRM_OK) { vmm_release(d->root); d->root = NULL; return st; }
        d->mem_pages++;
    }

    u64 stack_top = DOMAIN_USTACK_BASE + DOMAIN_USTACK_SIZE;
    thread_t *t = thread_create((u8)id, info->entry, 0u, (u64)con, 0u, stack_top);
    if (t == NULL) { vmm_release(d->root); d->root = NULL; return IFIRM_ERR_NO_SPACE; }
    d->thread_count = 1u;

    if (out_id != NULL) { *out_id = id; }
    return IFIRM_OK;
}
