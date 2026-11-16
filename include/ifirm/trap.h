#ifndef IFIRM_TRAP_H
#define IFIRM_TRAP_H

#include <ifirm/types.h>

void trap_init(void);
void timer_arm(void);
void timer_enable(void);
u64  trap_ticks(void);
void panic(const char *why);
void trap_return(trap_frame_t *f);

#endif /* IFIRM_TRAP_H */
