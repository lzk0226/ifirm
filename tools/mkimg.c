/* mkimg：把 .text/.data 载荷包成 v2 原子模板镜像（宿主工具，不进内核） */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define HDR 64u
#define NAME_LEN 16u

static uint64_t fnv64(const unsigned char *p, size_t n)
{
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 1099511628211ull; }
    return h;
}

static uint32_t fnv32(const char *s)
{
    uint32_t h = 2166136261u;
    for (size_t i = 0; s[i] != '\0' && i < NAME_LEN; i++) { h ^= (unsigned char)s[i]; h *= 16777619u; }
    return h;
}

static unsigned char *slurp(const char *path, size_t *out, int allow_empty)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        if (allow_empty) { *out = 0; return calloc(1, 1); }
        perror(path);
        exit(1);
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0) { exit(1); }
    unsigned char *b = malloc((size_t)n + 1u);
    if (n > 0 && fread(b, 1, (size_t)n, f) != (size_t)n) { exit(1); }
    fclose(f);
    *out = (size_t)n;
    return b;
}

int main(int argc, char **argv)
{
    if (argc != 6) {
        fprintf(stderr, "usage: mkimg <name> <text.bin> <data.bin> <out.img> <services-hex>\n");
        return 2;
    }
    const char *name = argv[1];
    if (strlen(name) >= NAME_LEN) { fprintf(stderr, "name too long\n"); return 2; }

    size_t tn = 0, dn = 0;
    unsigned char *tb = slurp(argv[2], &tn, 0);
    unsigned char *db = slurp(argv[3], &dn, 1);
    unsigned long svc = strtoul(argv[5], NULL, 0);

    unsigned char hdr[HDR];
    memset(hdr, 0, sizeof hdr);
    memcpy(hdr, "IFIRMTPL", 8);
    uint16_t ver = 2, hs = HDR;
    memcpy(hdr + 8, &ver, 2);
    memcpy(hdr + 10, &hs, 2);
    uint32_t tl = (uint32_t)tn, dl = (uint32_t)dn, nh = fnv32(name);
    memcpy(hdr + 12, &tl, 4);
    memcpy(hdr + 16, &dl, 4);
    memcpy(hdr + 20, &nh, 4);
    memcpy(hdr + 32, name, strlen(name));

    unsigned char *payload = malloc((size_t)tl + dl + 1u);
    if (tl) { memcpy(payload, tb, tl); }
    if (dl) { memcpy(payload + tl, db, dl); }
    uint64_t dg = fnv64(payload, (size_t)tl + dl);
    memcpy(hdr + 24, &dg, 8);

    uint16_t abi = 1, stk = 4;
    memcpy(hdr + 48, &abi, 2);
    memcpy(hdr + 50, &stk, 2);
    uint32_t sv = (uint32_t)svc, fl = 0, rs = 0;
    memcpy(hdr + 52, &sv, 4);
    memcpy(hdr + 56, &fl, 4);
    memcpy(hdr + 60, &rs, 4);

    FILE *o = fopen(argv[4], "wb");
    if (o == NULL) { perror(argv[4]); return 1; }
    fwrite(hdr, 1, HDR, o);
    fwrite(payload, 1, (size_t)tl + dl, o);
    fclose(o);
    printf("mkimg: %s name=%s text=%u data=%u svc=0x%x digest=0x%08x%08x\n",
           argv[4], name, tl, dl, sv, (unsigned)(dg >> 32), (unsigned)dg);
    return 0;
}
