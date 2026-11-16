/* 宿主工具：把若干文件打成 Ifirm 只读磁盘映像（不进内核） */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define SECT 512u
#define HDR_SECTORS 8u
#define HDR_SIZE (HDR_SECTORS * SECT)
#define MAX_FILES 32u

typedef struct {
    char name[16];
    uint32_t lba;
    uint32_t size;
} ent_t;

static uint64_t align_up(uint64_t v, uint64_t a) { return (v + a - 1u) / a * a; }

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: mkfs <out.img> name=path [name=path ...]\n");
        return 2;
    }
    if ((unsigned)(argc - 2) > MAX_FILES) {
        fprintf(stderr, "too many files\n");
        return 2;
    }

    ent_t ents[MAX_FILES];
    const char *paths[MAX_FILES];
    memset(ents, 0, sizeof(ents));
    memset(paths, 0, sizeof(paths));
    uint64_t total = HDR_SIZE;

    for (int i = 2; i < argc; i++) {
        char *eq = strchr(argv[i], '=');
        if (eq == NULL) { fprintf(stderr, "bad arg %s\n", argv[i]); return 2; }
        *eq = '\0';
        const char *name = argv[i];
        const char *path = eq + 1;
        paths[i - 2] = path;
        if (strlen(name) >= 16) { fprintf(stderr, "name too long: %s\n", name); return 2; }

        FILE *f = fopen(path, "rb");
        if (f == NULL) { fprintf(stderr, "cannot open %s\n", path); return 2; }
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (sz < 0) { fclose(f); return 2; }

        ent_t *e = &ents[i - 2];
        snprintf(e->name, sizeof(e->name), "%s", name);
        e->size = (uint32_t)sz;
        e->lba = (uint32_t)(total / SECT);
        total = align_up(total + (uint64_t)sz, SECT);
        fclose(f);
    }

    uint8_t *img = calloc(1, total);
    if (img == NULL) { return 2; }
    memcpy(img, "IFIRMFS1", 8);
    uint32_t hdr[4] = { 1u, (uint32_t)(argc - 2), (uint32_t)total, 0u };
    memcpy(img + 8, hdr, sizeof(hdr));
    memcpy(img + 32, ents, sizeof(ent_t) * (size_t)(argc - 2));

    for (int i = 2; i < argc; i++) {
        const char *path = paths[i - 2];
        ent_t *e = &ents[i - 2];
        FILE *f = fopen(path, "rb");
        if (f == NULL) { free(img); return 2; }
        if (fread(img + (uint64_t)e->lba * SECT, 1, e->size, f) != e->size) {
            fprintf(stderr, "short read %s\n", path);
            fclose(f); free(img); return 2;
        }
        fclose(f);
    }

    FILE *o = fopen(argv[1], "wb");
    if (o == NULL) { free(img); return 2; }
    fwrite(img, 1, total, o);
    fclose(o);
    free(img);
    printf("mkfs: %s files=%d bytes=%llu\n", argv[1], argc - 2, (unsigned long long)total);
    return 0;
}
