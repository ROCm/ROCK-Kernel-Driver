/*
 *  linux/include/asm-arm/arch-omap/board.h
 *
 *  Information structures for board-specific data
 *
 *  Copyright (C) 2004	Nokia Corporation
 *  Written by Juha Yrjölä <juha.yrjola@nokia.com>
 */

#ifndef _OMAP_BOARD_H
#define _OMAP_BOARD_H

#include <linux/config.h>
#include <linux/types.h>

/* Different peripheral ids */
#define OMAP_TAG_CLOCK		0x4f01
#define OMAP_TAG_MMC		0x4f02
#define OMAP_TAG_UART		0x4f03

struct omap_clock_info {
	/* 0 for 12 MHz, 1 for 13 MHz and 2 for 19.2 MHz */
	u8 system_clock_type;
};

struct omap_mmc_info {
	u8 mmc_blocks;
	s8 mmc1_power_pin, mmc2_power_pin;
	s8 mmc1_switch_pin, mmc2_switch_pin;
};

struct omap_uart_info {
	u8 console_uart;
	u32 console_speed;
};

struct omap_board_info_entry {
	u16 tag;
	u16 len;
	u8  data[0];
};

extern const void *__omap_get_per_info(u16 tag, size_t len);

#define omap_get_per_info(tag, type) \
	((const type *) __omap_get_per_info((tag), sizeof(type)))

#endif
