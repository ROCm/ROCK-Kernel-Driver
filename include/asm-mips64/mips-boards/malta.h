/*
 * Carsten Langgaard, carstenl@mips.com
 * Copyright (C) 2000 MIPS Technologies, Inc.  All rights reserved.
 *
 * ########################################################################
 *
 *  This program is free software; you can distribute it and/or modify it
 *  under the terms of the GNU General Public License (Version 2) as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * ########################################################################
 *
 * Defines of the Malta board specific address-MAP, registers, etc.
 *
 */
#ifndef _MIPS_MALTA_H
#define _MIPS_MALTA_H

#include <asm/addrspace.h>
#include <asm/io.h>

/* 
 * Malta I/O ports base address. 
*/
#define MALTA_PORT_BASE      (KSEG1ADDR(0x18000000))

/* 
 * Malta RTC-device indirect register access.
 */
#define MALTA_RTC_ADR_REG       0x70
#define MALTA_RTC_DAT_REG       0x71

/*
 * Malta SMSC FDC37M817 Super I/O Controller register.
 */
#define SMSC_CONFIG_REG		0x3f0
#define SMSC_DATA_REG		0x3f1

#define SMSC_CONFIG_DEVNUM	0x7
#define SMSC_CONFIG_ACTIVATE	0x30
#define SMSC_CONFIG_ENTER	0x55
#define SMSC_CONFIG_EXIT	0xaa

#define SMSC_CONFIG_DEVNUM_FLOPPY     0

#define SMSC_CONFIG_ACTIVATE_ENABLE   1

#define SMSC_WRITE(x,a)     outb(x,a)

#endif /* !(_MIPS_MALTA_H) */
