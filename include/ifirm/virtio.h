#ifndef IFIRM_VIRTIO_H
#define IFIRM_VIRTIO_H

#include <ifirm/types.h>

/* QEMU virt：8 个 virtio-mmio 槽，从 0x10001000 开始，每个 0x1000（§附录 D） */
#define VIRTIO_MMIO_BASE    0x10001000u
#define VIRTIO_MMIO_STRIDE  0x1000u
#define VIRTIO_MMIO_SLOTS   8u
#define VIRTIO_MAGIC        0x74726976u
#define VIRTIO_ID_BLOCK     2u

ifirm_status_t virtio_blk_init(void);
ifirm_status_t virtio_blk_read(u64 lba, u32 count, void *dst);
ifirm_status_t virtio_blk_write(u64 lba, u32 count, const void *src);
u32            virtio_blk_version(void);
u64            virtio_blk_capacity(void);

#endif /* IFIRM_VIRTIO_H */
