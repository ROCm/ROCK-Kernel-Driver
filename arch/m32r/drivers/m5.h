/*
 * Flash Memory Driver for M32700UT-CPU
 *
 * Copyright 2003 (C)   Takeo Takahashi
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * 2003-02-01: 	Takeo Takahashi, support M5M29GT320VP.
 */

#include <asm/m32r.h>

#ifdef __KERNEL__
#undef DEBUG
/* debug routine:
 *   0x00000001: print debug information
 */
#  define DEBUG(n, args...) if ((n) & debug) \
	printk(KERN_DEBUG args)
#endif	/* __KERNEL__ */

/*
 * data type to access flash memory
 */
typedef volatile unsigned short m5_t;

/*
 * - Page program buffer size in byte
 * - block size in byte
 * - number of block
 */
#define M5_PAGE_SIZE		(256)
#define M5_BLOCK_SIZE8		(8*1024)
#define M5_BLOCK_SIZE64		(64*1024)
#define MAX_BLOCK_NUM		70

/*
 * Software commands
 */
#define M5_CMD_READ_ARRAY            0xff
#define M5_CMD_DEVICE_IDENT          0x90
#define M5_CMD_READ_STATUS           0x70
#define M5_CMD_CLEAR_STATUS          0x50
#define M5_CMD_BLOCK_ERASE           0x20
#define M5_CMD_CONFIRM               0xd0
#define M5_CMD_PROGRAM_BYTE          0x40
#define M5_CMD_PROGRAM_WORD          M5_CMD_PROGRAM_BYTE
#define M5_CMD_PROGRAM_PAGE          0x41
#define M5_CMD_SINGLE_LOAD_DATA      0x74
#define M5_CMD_BUFF2FLASH            0x0e
#define M5_CMD_FLASH2BUFF            0xf1
#define M5_CMD_CLEAR_BUFF            0x55
#define M5_CMD_SUSPEND               0xb0
#define M5_CMD_RESUME                0xd0

/*
 * Status
 */
#define M5_STATUS_READY              0x80 /* 0:busy 1:ready */
#define M5_STATUS_SUSPEND            0x40 /* 0:progress/complete 1:suspend */
#define M5_STATUS_ERASE              0x20 /* 0:pass 1:error */
#define M5_STATUS_PROGRAM            0x10 /* 0:pass 1:error */
#define M5_STATUS_BLOCK              0x08 /* 0:pass 1:error */

/*
 * Device Code
 */
#define M5_MAKER		(0x1c)
#define M5_M5M29GT320VP		(0x20)
#define M5_M5M29GB320VP		(0x21)
