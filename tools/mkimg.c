/* 宿主工具：把 tpl_*.text.bin / tpl_*.data.bin 包成带摘要头的模板镜像（不进内核） */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define HDR 40u

static unsigned long long fnv1a64(const unsigned char *p, size_t n, unsigned long long h)
{
    for (size_t i = 0; i < n; i++) { h ^= (unsigned long long)p[i]; h *= 0x100000001b3ull; }
    return h;
}

static unsigned long long name_hash(const char *s)
{
    unsigned long long h = 0x811c9dc5ull;
    while (*s != '\0') { h ^= (unsigned long long)(unsigned char)*s++; h *= 0x01000193ull; }
    return h & 0xFFFFFFFFull;
}

static unsigned char *slurp(const char *path, long *out_n)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) { perror(path); exit(1); }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *buf = (unsigned char *)calloc(1, (size_t)(n > 0 ? n : 1));
    if (buf == NULL) { fprintf(stderr, "oom\n"); exit(1); }
    if (n > 0 && fread(buf, 1, (size_t)n, f) != (size_t)n) { fprintf(stderr, "short read\n"); exit(1); }
    fclose(f);
    *out_n = n;
    return buf;
}

int main(int argc, char **argv)
{
    if (argc != 5) { fprintf(stderr, "usage: mkimg <name> <text.bin> <data.bin> <out.img>\n"); return 2; }
    const char *name = argv[1];
    long tn = 0, dn = 0;
    unsigned char *tb = slurp(argv[2], &tn);
    unsigned char *db = slurp(argv[3], &dn);
    if (tn <= 0 || tn > 65536) { fprintf(stderr, "bad text size %ld\n", tn); return 1; }
    if (dn < 0 || dn > 65536) { fprintf(stderr, "bad data size %ld\n", dn); return 1; }

    unsigned char h[HDR];
    memset(h, 0, sizeof(h));
    memcpy(h, "IFIRMTPL", 8);
    h[8] = 1; h[9] = 0;
    h[10] = HDR; h[11] = 0;
    uint32_t tl = (uint32_t)tn, dl = (uint32_t)dn;
    memcpy(h + 12, &tl, 4);
    memcpy(h + 16, &dl, 4);
    uint32_t nh = (uint32_t)name_hash(name);
    memcpy(h + 20, &nh, 4);
    unsigned long long d = 0xcbf29ce484222325ull;
    d = fnv1a64(tb, (size_t)tn, d);
    d = fnv1a64(db, (size_t)dn, d);
    memcpy(h + 24, &d, 8);

    FILE *out = fopen(argv[4], "wb");
    if (out == NULL) { perror(argv[4]); return 1; }
    fwrite(h, 1, sizeof(h), out);
    fwrite(tb, 1, (size_t)tn, out);
    fwrite(db, 1, (size_t)dn, out);
    fclose(out);
    printf("%s: text=%ld data=%ld digest=0x%llx\n", name, tn, dn, d);
    return 0;
}
