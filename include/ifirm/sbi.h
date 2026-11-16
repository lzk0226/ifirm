#ifndef IFIRM_SBI_H
#define IFIRM_SBI_H

#include <ifirm/types.h>

void sbi_set_timer(u64 stime);
void sbi_shutdown(void);
void sbi_putchar(char c);

#endif /* IFIRM_SBI_H */
