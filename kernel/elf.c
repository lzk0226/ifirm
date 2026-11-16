#include <ifirm/elf.h>
#include <ifirm/vmm.h>
#include <ifirm/pmm.h>
#include <ifirm/status.h>

#define PT_LOAD   1u
#define PF_X      1u
#define PF_W      2u
#define PF_R      4u
#define EM_RISCV  243u
#define ET_EXEC   2u

static u16 rd16(const u8 *p) { return (u16)(p[0] | ((u16)p[1] << 8)); }
static u32 rd32(const u8 *p)
{
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}
static u64 rd64(const u8 *p)
{
    return (u64)rd32(p) | ((u64)rd32(p + 4) << 32);
}

ifirm_status_t elf_check(const u8 *img, u64 len)
{
    if (len < 64u) { return IFIRM_ERR_CORRUPT; }
    if (img[0] != 0x7fu || img[1] != (u8)'E' || img[2] != (u8)'L' || img[3] != (u8)'F') {
        return IFIRM_ERR_BAD_DIGEST;
    }
    if (img[4] != 2u || img[5] != 1u) { return IFIRM_ERR_UNSUPPORTED; }   /* ELF64 / 小端 */
    if (rd16(img + 18) != EM_RISCV) { return IFIRM_ERR_UNSUPPORTED; }
    if (rd16(img + 16) != ET_EXEC) { return IFIRM_ERR_UNSUPPORTED; }
    if (rd16(img + 54) != 56u || rd16(img + 56) == 0u) { return IFIRM_ERR_CORRUPT; }
    return IFIRM_OK;
}

static u64 seg_perm(u32 flags)
{
    u64 p = PTE_U | PTE_R | PTE_A | PTE_D;
    if ((flags & PF_W) != 0u) { p |= PTE_W; }
    if ((flags & PF_X) != 0u) { p |= PTE_X; }
    return p;
}

/*
 * 把一个 PT_LOAD 段铺进新地址空间：按页分配，整页填零后拷入文件内容。
 * 同一页被两个段覆盖时复用已映射的物理页（vmm_walk 非 0 即已映射）。
 */
static ifirm_status_t load_seg(u64 *root, const u8 *img, u64 len, u64 phoff, u32 *pages)
{
    u32 type = rd32(img + phoff + 0u);
    if (type != PT_LOAD) { return IFIRM_OK; }

    u32 flags  = rd32(img + phoff + 4u);
    u64 off    = rd64(img + phoff + 8u);
    u64 vaddr  = rd64(img + phoff + 16u);
    u64 filesz = rd64(img + phoff + 32u);
    u64 memsz  = rd64(img + phoff + 40u);

    if (memsz == 0u) { return IFIRM_OK; }
    /* 减法形式：提前用加法判断会在 off 很大时回绕，把越界文件放进来 */
    if (off > len || filesz > len - off) { return IFIRM_ERR_CORRUPT; }
    if (vaddr + memsz < vaddr) { return IFIRM_ERR_OVERFLOW; }

    u64 done = 0u;
    while (done < memsz) {
        u64 va = (vaddr + done) & ~0xFFFull;
        u64 pte = vmm_walk_pte(root, va);
        u64 pa;
        /*
         * 只有带 PTE_U 的 4 KiB 项才是这个地址空间自己的页（同一段跨页、
         * 或两个段共享一页时复用它）。0..1GiB 的恒等映射没有 PTE_U，
         * 不能当成"已映射"，否则会直接往低地址物理区写。
         */
        if ((pte & PTE_V) != 0u && (pte & PTE_U) != 0u &&
            (pte & (PTE_R | PTE_W | PTE_X)) != 0u) {
            pa = ((pte >> 10) << 12) | (va & 0xFFFull);
        } else {
            pa = pmm_alloc();
            if (pa == 0u) { return IFIRM_ERR_NO_MEMORY; }
            for (u32 i = 0; i < PAGE_SIZE; i++) { ((u8 *)pa)[i] = 0u; }
            ifirm_status_t st = vmm_map(root, va, pa, seg_perm(flags));
            if (st != IFIRM_OK) { pmm_free(pa); return st; }
            (*pages)++;
        }

        u8 *dst = (u8 *)pa;
        for (u32 i = 0; i < PAGE_SIZE; i++) {
            u64 rel = va + i - vaddr;
            if (rel >= memsz) { break; }
            if (rel < filesz) { dst[i] = img[off + rel]; }
        }
        done = (va + PAGE_SIZE) - vaddr;
    }
    return IFIRM_OK;
}

ifirm_status_t elf_load(u64 *root, const u8 *img, u64 len, elf_info_t *info)
{
    ifirm_status_t st = elf_check(img, len);
    if (st != IFIRM_OK) { return st; }

    u64 phoff = rd64(img + 32u);
    u16 phnum = rd16(img + 56u);
    if (phoff > len || (u64)phnum > (len - phoff) / 56u) { return IFIRM_ERR_CORRUPT; }

    info->entry = rd64(img + 24u);
    info->segs = 0u;
    info->pages = 0u;

    for (u16 i = 0; i < phnum; i++) {
        u64 po = phoff + (u64)i * 56u;
        if (rd32(img + po) == PT_LOAD) { info->segs++; }
        st = load_seg(root, img, len, po, &info->pages);
        if (st != IFIRM_OK) { return st; }
    }
    if (info->segs == 0u) { return IFIRM_ERR_CORRUPT; }
    return IFIRM_OK;
}
