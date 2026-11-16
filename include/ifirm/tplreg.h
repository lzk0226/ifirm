#ifndef IFIRM_TPLREG_H
#define IFIRM_TPLREG_H

#include <ifirm/types.h>
#include <ifirm/template.h>

#define TPL_MAX 8u

typedef struct {
    char             name[TPL_NAME_LEN];
    const u8        *img;
    u64              len;
    const tpl_hdr_t *hdr;
} tpl_ent_t;

ifirm_status_t    tplreg_init(void);
u32               tplreg_count(void);
const tpl_ent_t  *tplreg_at(u32 i);
const tpl_ent_t  *tplreg_find(const char *name);
u64               tplreg_composite_digest(void);

#endif /* IFIRM_TPLREG_H */
