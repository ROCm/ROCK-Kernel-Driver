/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 2001-2004 Silicon Graphics, Inc.  All rights reserved.
 */

#ifndef _ASM_IA64_SN_SHUB_H
#define _ASM_IA64_SN_SHUB_H


#define MD_MEM_BANKS            4


/*
 * Junk Bus Address Space
 *   The junk bus is used to access the PROM, LED's, and UART. It's 
 *   accessed through the local block MMR space. The data path is
 *   16 bits wide. This space requires address bits 31-27 to be set, and
 *   is further divided by address bits 26:15.
 *   The LED addresses are write-only. To read the LEDs, you need to use
 *   SH_JUNK_BUS_LED0-3, defined in shub_mmr.h
 *		
 */
#define SH_REAL_JUNK_BUS_LED0           0x7fed00000UL
#define SH_REAL_JUNK_BUS_LED1           0x7fed10000UL
#define SH_REAL_JUNK_BUS_LED2           0x7fed20000UL
#define SH_REAL_JUNK_BUS_LED3           0x7fed30000UL
#define SH_JUNK_BUS_UART0               0x7fed40000UL
#define SH_JUNK_BUS_UART1               0x7fed40008UL
#define SH_JUNK_BUS_UART2               0x7fed40010UL
#define SH_JUNK_BUS_UART3               0x7fed40018UL
#define SH_JUNK_BUS_UART4               0x7fed40020UL
#define SH_JUNK_BUS_UART5               0x7fed40028UL
#define SH_JUNK_BUS_UART6               0x7fed40030UL
#define SH_JUNK_BUS_UART7               0x7fed40038UL

#endif /* _ASM_IA64_SN_SHUB_H */
