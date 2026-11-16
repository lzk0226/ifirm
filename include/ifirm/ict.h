#ifndef IFIRM_ICT_H
#define IFIRM_ICT_H

#include <ifirm/types.h>
#include <ifirm/thread.h>
#include <ifirm/status.h>

#define ICT_ENTRIES_PER_PAGE 256u
#define ICT_MAX_ENTRIES      ICT_ENTRIES_PER_PAGE

#define ICT_F_NEEDS_CAP 0x01u
#define ICT_F_BLOCKING  0x02u
#define ICT_F_AUDIT     0x04u
#define ICT_F_LOCAL     0x08u

typedef struct {
    u32 function_id;   /*  0: 0 = 空槽 */
    u32 target_off;    /*  4 */
    u16 flags;         /*  8 */
    u16 cap_slot;      /* 10 */
    u32 arg_limit;     /* 12 */
} ict_entry_t;

STATIC_ASSERT(sizeof(ict_entry_t) == 16, "ict entry frozen");

ifirm_status_t ict_install(u8 dom, u32 function_id, u16 cap_slot, u16 flags, u32 arg_limit);
i64            ict_dispatch(trap_frame_t *f, u32 slot);
u32            ict_count(u8 dom);
void           ict_build_page(u8 dom, u8 *page);

#endif /* IFIRM_ICT_H */
