/*
 *  linux/include/asm-arm/linux_logo.h
 *
 *  Copyright (C) 1998 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Linux console driver logo definitions for ARM
 */

#include <linux/init.h>
#include <linux/version.h>

#define linux_logo_banner "ARM Linux version " UTS_RELEASE

#define LINUX_LOGO_COLORS	214

#ifdef INCLUDE_LINUX_LOGO_DATA

#define INCLUDE_LINUX_LOGOBW
#define INCLUDE_LINUX_LOGO16

#include <linux/linux_logo.h>

#else

/* prototypes only */
extern unsigned char linux_logo_red[];
extern unsigned char linux_logo_green[];
extern unsigned char linux_logo_blue[];
extern unsigned char linux_logo[];
extern unsigned char linux_logo_bw[];
extern unsigned char linux_logo16_red[];
extern unsigned char linux_logo16_green[];
extern unsigned char linux_logo16_blue[];
extern unsigned char linux_logo16[];
extern unsigned char *linux_serial_image;

extern int (*console_show_logo)(void);

#endif
