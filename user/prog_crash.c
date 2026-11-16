#include "ulib.h"

/* crash：故意写空指针。域被内核杀掉，shell 与其它域必须继续活着。 */
static int umain(int argc, char **argv)
{
    (void)argc; (void)argv;
    cputs("crash: writing to address 0 ...\n");
    *(volatile u64 *)0 = 1u;
    cputs("crash: this line must never appear\n");
    return 1;
}

USER_MAIN(umain)
