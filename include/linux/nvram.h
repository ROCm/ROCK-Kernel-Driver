#ifndef _LINUX_NVRAM_H
#define _LINUX_NVRAM_H

#include <linux/ioctl.h>

/* /dev/nvram ioctls */
#define NVRAM_INIT		_IO('p', 0x40) /* initialize NVRAM and set checksum */
#define	NVRAM_SETCKS	_IO('p', 0x41) /* recalculate checksum */

#ifdef __KERNEL__
extern unsigned char nvram_read_byte( int i );
extern void nvram_write_byte( unsigned char c, int i );
extern int nvram_check_checksum( void );
extern void nvram_set_checksum( void );
#endif

#endif  /* _LINUX_NVRAM_H */
