/*
 * crc32.h
 * See linux/lib/crc32.c for license and changes
 */
#ifndef _LINUX_CRC32_H
#define _LINUX_CRC32_H

#include <linux/types.h>

extern u32  crc32_le(u32 crc, unsigned char const *p, size_t len);
extern u32  crc32_be(u32 crc, unsigned char const *p, size_t len);

#define crc32(seed, data, length)  crc32_le(seed, (unsigned char const *)data, length)
#define ether_crc_le(length, data) crc32_le(~0, data, length)
#define ether_crc(length, data)    crc32_be(~0, data, length)

#endif /* _LINUX_CRC32_H */
