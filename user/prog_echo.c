#include "ulib.h"

static int umain(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        if (i > 1) { cputs(" "); }
        cputs(argv[i]);
    }
    cputs("\n");
    return 0;
}

USER_MAIN(umain)
