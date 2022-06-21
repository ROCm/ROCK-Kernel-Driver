/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Pointer abstraction for IO/system memory
 * Copied from include/kcl/iosys-map.h
 */

#ifndef _KCL_KCL__IOSYS_MAP_H__H__
#define _KCL_KCL__IOSYS_MAP_H__H__

#include <linux/iosys-map.h>

#ifndef HAVE_LINUX_IOSYS_MAP_H
#include <linux/io.h>
#include <linux/string.h>

/**
 * struct iosys_map - Pointer to IO/system memory
 * @vaddr_iomem:        The buffer's address if in I/O memory
 * @vaddr:              The buffer's address if in system memory
 * @is_iomem:           True if the buffer is located in I/O memory, or false
 *                      otherwise.
 */
struct iosys_map {
        union {
                void __iomem *vaddr_iomem;
                void *vaddr;
        };
        bool is_iomem;
};

/**
 * IOSYS_MAP_INIT_VADDR - Initializes struct iosys_map to an address in system memory
 * @vaddr_:     A system-memory address
 */
#define IOSYS_MAP_INIT_VADDR(vaddr_)    \
        {                               \
                .vaddr = (vaddr_),      \
                .is_iomem = false,      \
        }


/**
 * iosys_map_set_vaddr - Sets a iosys mapping structure to an address in system memory
 * @map:        The iosys_map structure
 * @vaddr:      A system-memory address
 *
 * Sets the address and clears the I/O-memory flag.
 */
static inline void iosys_map_set_vaddr(struct iosys_map *map, void *vaddr)
{
        map->vaddr = vaddr;
        map->is_iomem = false;
}

/**
 * iosys_map_set_vaddr_iomem - Sets a iosys mapping structure to an address in I/O memory
 * @map:                The iosys_map structure
 * @vaddr_iomem:        An I/O-memory address
 *
 * Sets the address and the I/O-memory flag.
 */
static inline void iosys_map_set_vaddr_iomem(struct iosys_map *map,
                                             void __iomem *vaddr_iomem)
{
        map->vaddr_iomem = vaddr_iomem;
        map->is_iomem = true;
}

/**
 * iosys_map_is_equal - Compares two iosys mapping structures for equality
 * @lhs:        The iosys_map structure
 * @rhs:        A iosys_map structure to compare with
 *
 * Two iosys mapping structures are equal if they both refer to the same type of memory
 * and to the same address within that memory.
 *
 * Returns:
 * True is both structures are equal, or false otherwise.
 */
static inline bool iosys_map_is_equal(const struct iosys_map *lhs,
                                      const struct iosys_map *rhs)
{
        if (lhs->is_iomem != rhs->is_iomem)
                return false;
        else if (lhs->is_iomem)
                return lhs->vaddr_iomem == rhs->vaddr_iomem;
        else
                return lhs->vaddr == rhs->vaddr;
}

/**
 * iosys_map_is_null - Tests for a iosys mapping to be NULL
 * @map:        The iosys_map structure
 *
 * Depending on the state of struct iosys_map.is_iomem, tests if the
 * mapping is NULL.
 *
 * Returns:
 * True if the mapping is NULL, or false otherwise.
 */
static inline bool iosys_map_is_null(const struct iosys_map *map)
{
        if (map->is_iomem)
                return !map->vaddr_iomem;
        return !map->vaddr;
}

/**
 * iosys_map_is_set - Tests if the iosys mapping has been set
 * @map:        The iosys_map structure
 *
 * Depending on the state of struct iosys_map.is_iomem, tests if the
 * mapping has been set.
 *
 * Returns:
 * True if the mapping is been set, or false otherwise.
 */
static inline bool iosys_map_is_set(const struct iosys_map *map)
{
        return !iosys_map_is_null(map);
}

/**
 * iosys_map_clear - Clears a iosys mapping structure
 * @map:        The iosys_map structure
 *
 * Clears all fields to zero, including struct iosys_map.is_iomem, so
 * mapping structures that were set to point to I/O memory are reset for
 * system memory. Pointers are cleared to NULL. This is the default.
 */
static inline void iosys_map_clear(struct iosys_map *map)
{
        if (map->is_iomem) {
                map->vaddr_iomem = NULL;
                map->is_iomem = false;
        } else {
                map->vaddr = NULL;
        }
}

/**
 * iosys_map_memcpy_to - Memcpy into offset of iosys_map
 * @dst:        The iosys_map structure
 * @dst_offset: The offset from which to copy
 * @src:        The source buffer
 * @len:        The number of byte in src
 *
 * Copies data into a iosys_map with an offset. The source buffer is in
 * system memory. Depending on the buffer's location, the helper picks the
 * correct method of accessing the memory.
 */
static inline void iosys_map_memcpy_to(struct iosys_map *dst, size_t dst_offset,
                                       const void *src, size_t len)
{
        if (dst->is_iomem)
                memcpy_toio(dst->vaddr_iomem + dst_offset, src, len);
        else
                memcpy(dst->vaddr + dst_offset, src, len);
}

/**
 * iosys_map_incr - Increments the address stored in a iosys mapping
 * @map:        The iosys_map structure
 * @incr:       The number of bytes to increment
 *
 * Increments the address stored in a iosys mapping. Depending on the
 * buffer's location, the correct value will be updated.
 */
static inline void iosys_map_incr(struct iosys_map *map, size_t incr)
{
        if (map->is_iomem)
                map->vaddr_iomem += incr;
        else
                map->vaddr += incr;
}

#endif /* HAVE_LINUX_IOSYS_MAP_H */

#endif
