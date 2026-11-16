#include <ifirm/types.h>
#include <ifirm/sbi.h>

static inline long sbi_call(long eid, long fid, long a0, long a1, long a2)
{
    register long r_a0 __asm__("a0") = a0;
    register long r_a1 __asm__("a1") = a1;
    register long r_a2 __asm__("a2") = a2;
    register long r_a6 __asm__("a6") = fid;
    register long r_a7 __asm__("a7") = eid;
    __asm__ volatile ("ecall"
                      : "+r"(r_a0), "+r"(r_a1)
                      : "r"(r_a2), "r"(r_a6), "r"(r_a7)
                      : "memory");
    return r_a0;
}

void sbi_set_timer(u64 stime) { (void)sbi_call(0x54494D45, 0, (long)stime, 0, 0); }
void sbi_shutdown(void)       { (void)sbi_call(0x53525354, 0, 0, 0, 0); for (;;) { __asm__ volatile ("wfi"); } }
void sbi_putchar(char c)      { (void)sbi_call(0x01, 0, (long)(u8)c, 0, 0); }
