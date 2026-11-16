/* mkcharter：把策略文本打包成 CA 认的 charter 二进制（宿主工具） */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>

#define MAX_RULES 32

typedef struct {
    uint8_t dom, type, phase, pad;
    uint16_t rights, pad2;
    uint32_t object;
} rule_t;

static uint64_t fnv64(const unsigned char *p, size_t n)
{
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 1099511628211ull; }
    return h;
}

static int type_of(const char *s)
{
    if (!strcmp(s, "tpl"))  { return 1; }
    if (!strcmp(s, "dom"))  { return 2; }
    if (!strcmp(s, "mem"))  { return 3; }
    if (!strcmp(s, "chan")) { return 4; }
    if (!strcmp(s, "dev"))  { return 5; }
    return atoi(s);
}

int main(int argc, char **argv)
{
    if (argc != 3) { fprintf(stderr, "usage: mkcharter <rules.txt> <out.bin>\n"); return 2; }
    FILE *f = fopen(argv[1], "r");
    if (f == NULL) { perror(argv[1]); return 1; }

    rule_t rules[MAX_RULES];
    int n = 0;
    char line[256];
    while (fgets(line, sizeof line, f) != NULL) {
        char *p = line;
        while (*p == ' ' || *p == '\t') { p++; }
        if (*p == '#' || *p == '\n' || *p == '\0') { continue; }
        char a[32], b[32], c[32], d[32], e[32];
        if (sscanf(p, "%31s %31s %31s %31s %31s", a, b, c, d, e) != 5) {
            fprintf(stderr, "bad rule: %s", line);
            return 1;
        }
        if (n >= MAX_RULES) { fprintf(stderr, "too many rules\n"); return 1; }
        rules[n].dom = (uint8_t)atoi(a);
        rules[n].type = (uint8_t)type_of(b);
        rules[n].rights = (uint16_t)strtoul(c, NULL, 0);
        rules[n].object = (uint32_t)strtoul(d, NULL, 0);
        rules[n].phase = (uint8_t)(!strcmp(e, "run") ? 1 : 0);
        rules[n].pad = rules[n].pad2 = 0;
        n++;
    }
    fclose(f);
    if (n == 0) { fprintf(stderr, "no rules\n"); return 1; }

    unsigned char hdr[32];
    memset(hdr, 0, sizeof hdr);
    memcpy(hdr, "IFIRMCHA", 8);
    uint32_t ver = 1, nr = (uint32_t)n;
    memcpy(hdr + 8, &ver, 4);
    memcpy(hdr + 12, &nr, 4);
    uint64_t dg = fnv64((const unsigned char *)rules, sizeof(rule_t) * (size_t)n);
    memcpy(hdr + 16, &dg, 8);

    FILE *o = fopen(argv[2], "wb");
    if (o == NULL) { perror(argv[2]); return 1; }
    fwrite(hdr, 1, sizeof hdr, o);
    fwrite(rules, sizeof(rule_t), (size_t)n, o);
    fclose(o);
    printf("mkcharter: %s rules=%d digest=0x%08x%08x\n",
           argv[2], n, (unsigned)(dg >> 32), (unsigned)dg);
    return 0;
}
