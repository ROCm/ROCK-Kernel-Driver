#ifndef _LINUX_CRC16_H
#define _LINUX_CRC16_H

#include <linux/types.h>

extern u16 const crc16_table[256];

extern u16 crc16(u16 crc, const u8 *buffer, size_t len);

static inline u16 crc16_byte(u16 crc, const u8 c)
{
	return (crc >> 8) ^ crc16_table[(crc ^ c) & 0xff];
}

#endif /* _LINUX_CRC16_H */
