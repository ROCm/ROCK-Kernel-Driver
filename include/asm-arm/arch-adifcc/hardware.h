/*
 * linux/include/asm-arm/arch-adifcc/hardware.h
 *
 * Hardware definitions for ADI based systems
 *
 * Author: Deepak Saxena <dsaxena@mvista.com>
 *
 * Copyright (C) 2000-2001 MontaVista Software Inc.
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 */

#ifndef __ASM_ARCH_HARDWARE_H
#define __ASM_ARCH_HARDWARE_H

#include <linux/config.h>

#define PCIO_BASE	0

#if defined(CONFIG_ARCH_ADI_EVB)
#include "adi_evb.h"
#endif

#endif  /* _ASM_ARCH_HARDWARE_H */
