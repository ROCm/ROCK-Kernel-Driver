/*
 *  linux/include/asm-arm/hardware.h
 *
 *  Copyright (C) 1996 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Common hardware definitions
 */

#ifndef __ASM_HARDWARE_H
#define __ASM_HARDWARE_H

#include <asm/arch/hardware.h>

#ifndef __ASSEMBLY__

struct platform_device;

extern int platform_add_devices(struct platform_device **, int);
extern int platform_add_device(struct platform_device *);

#endif

#endif
