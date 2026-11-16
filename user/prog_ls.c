#include "ulib.h"

/* ls [路径]：列目录。目录 = parent 指向自己的一组 inode，内核线性扫描。 */
static int umain(int argc, char **argv)
{
    const char *path = (argc > 1) ? argv[1] : "/";
    i64 ino = f_open(path, FS_OPEN_READ);
    if (ino < 0) { cprintf("ls: %s: st=%d\n", path, ino); return 1; }

    dirent_t d;
    u32 i = 0;
    u32 n = 0;
    cprintf("%s:\n", path);
    while (f_readdir(ino, i, &d) == 1) {
        cprintf("  %s", d.name);
        for (u32 k = slen(d.name); k < 16u; k++) { cputs(" "); }
        if (d.type == 2u) { cputs("<dir>\n"); }
        else { cprintf("%d\n", (i64)d.size); }
        i++;
        n++;
    }
    cprintf("%d entries\n", (i64)n);
    return 0;
}

USER_MAIN(umain)
