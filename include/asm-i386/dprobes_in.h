#ifndef __ASM_I386_DPROBES_IN_H__
#define __ASM_I386_DPROBES_IN_H__

/*
 * IBM Dynamic Probes
 * Copyright (c) International Business Machines Corp., 2000
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or 
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.  
 */

/*
 * This header file defines the opcodes for the RPN instructions
 *
 * Opcodes 0x00 to 0xAF are for common, arch-independent instructions.
 * Opcodes 0xB0 to 0xDF are for arch-dependent instructions.
 * Opcodes 0xF1 to 0xFF are for two byte instructions.
 */

/*
 * Misc
 */
#define DP_SEG2LIN		0xB0

/*
 * IO Group.
 */
#define DP_PUSH_IO_U8		0xB1
#define DP_PUSH_IO_U16		0xB2
#define DP_PUSH_IO_U32		0xB3
#define DP_POP_IO_U8		0xB4
#define DP_POP_IO_U16		0xB5
#define DP_POP_IO_U32		0xB6

/*
 * Intel32 Register Assignments.
 */
#define DP_CS			0x0000
#define DP_DS			0x0001
#define DP_ES			0x0002
#define DP_FS			0x0003
#define DP_GS			0x0004
#define DP_SS			0x0005
#define DP_EAX			0x0006
#define DP_EBX			0x0007
#define DP_ECX			0x0008
#define DP_EDX			0x0009
#define DP_EDI			0x000a
#define DP_ESI			0x000b
#define DP_EFLAGS		0x000c
#define DP_EIP			0x000d
#define DP_ESP			0x000e
#define DP_EBP			0x000f

#define DP_TR			0x0020
#define DP_LDTR			0x0021
#define DP_GDTR			0x0022
#define DP_IDTR			0x0023
#define DP_CR0			0x0024
#define DP_CR1			0x0025
#define DP_CR2			0x0026
#define DP_CR3			0x0027
#define DP_CR4			0x0028

#define DP_DR0			0x002c
#define DP_DR1			0x002d
#define DP_DR2			0x002e
#define DP_DR3			0x002f
#define DP_DR4			0x0030
#define DP_DR5			0x0031
#define DP_DR6			0x0032
#define DP_DR7			0x0033

#define DP_CPUID		0x003c
#define DP_MSR			0x003d

#define DP_FR0			0x003e
#define DP_FR1			0x003f
#define DP_FR2			0x0040
#define DP_FR3			0x0041
#define DP_FR4			0x0042
#define DP_FR5			0x0043
#define DP_FR6			0x0044
#define DP_FR7			0x0045
#define DP_FCW			0x0046
#define DP_FSW			0x0047
#define DP_FTW			0x0048
#define DP_FIP			0x0049
#define DP_FCS			0x004a
#define DP_FDP			0x004b
#define DP_FDS			0x004c

#define DP_XMM0			0x004d
#define DP_XMM1			0x004e
#define DP_XMM2			0x004f
#define DP_XMM3			0x0050
#define DP_XMM4			0x0051
#define DP_XMM5			0x0052
#define DP_XMM6			0x0053
#define DP_XMM7			0x0054
#define DP_MXCSR		0x0055

#ifndef __KERNEL__
#ifndef PAGE_OFFSET
#define PAGE_OFFSET		0xc0000000
#endif
#endif /* !__KERNEL__ */

#endif
