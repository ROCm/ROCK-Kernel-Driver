/*
 *  linux/include/asm-arm/mach/flash.h
 *
 *  Copyright (C) 2003 Russell King, All Rights Reserved.
 */
#ifndef ASMARM_MACH_FLASH_H
#define ASMAMR_MACH_FLASH_H

struct mtd_partition;

struct flash_platform_data {
	const char	*map_name;
	int		width;
	int		(*init)(void);
	void		(*exit)(void);
	void		(*set_vpp)(int on);
};

#endif
