#ifndef _LINUX_CRC_CCITT_H
#define _LINUX_CRC_CCITT_H

#include <linux/types.h>

extern __u16 const crc_ccitt_table[256];

extern __u16 crc_ccitt(__u16 crc, const __u8 *buffer, size_t len);

static inline __u16 crc_ccitt_byte(__u16 crc, const __u8 c)
{
	return (crc >> 8) ^ crc_ccitt_table[(crc ^ c) & 0xff];
}

#endif /* _LINUX_CRC_CCITT_H */
