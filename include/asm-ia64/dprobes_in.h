#ifndef __ASM_IA64_DPROBES_IN_H__
#define __ASM_IA64_DPROBES_IN_H__

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
/* System, Processor Status Register (PSR) */
#define DP_PSR		0x0000		/* PSR  */
#define DP_PSR_L	(0x0001|DP_PSR)
#define DP_PSR_UM	(0x0002|DP_PSR)

/* System, Control Registers (CR) */
#define DP_CR		0x0100
#define DP_CR0		(0x0001|DP_CR)	/* DCR  */
#define DP_CR1		(0x0002|DP_CR)	/* ITM  */
#define DP_CR2		(0x0003|DP_CR)	/* IVA  */
    /* CR3-CR7: reserved */
#define DP_CR8		(0x0004|DP_CR)	/* PTA  */
    /* CR9-CR15: reserved */
#define DP_CR16		(0x0005|DP_CR)	/* IPSR */ 
#define DP_CR17		(0x0006|DP_CR)	/* ISR  */
    /* CR18: reserved */
#define DP_CR19		(0x0007|DP_CR)	/* IIP  */
#define DP_CR20		(0x0008|DP_CR)	/* IFA  */
#define DP_CR21		(0x0009|DP_CR)	/* ITIR */
#define DP_CR22		(0x000a|DP_CR)	/* IIPA */
#define DP_CR23		(0x000b|DP_CR)	/* IFS  */
#define DP_CR24		(0x000c|DP_CR)	/* IIM  */
#define DP_CR25		(0x000d|DP_CR)	/* IHA  */
    /* CR26-CR63: reserved */
#define DP_CR64		(0x000e|DP_CR)	/* LID  */
#define DP_CR65		(0x000f|DP_CR)	/* IVR  */
#define DP_CR66		(0x0010|DP_CR)	/* TPR  */
#define DP_CR67		(0x0011|DP_CR)	/* EOI  */
#define DP_CR68		(0x0012|DP_CR)	/* IRR0 */
#define DP_CR69		(0x0013|DP_CR)	/* IRR1 */
#define DP_CR70		(0x0014|DP_CR)	/* IRR2 */
#define DP_CR71		(0x0015|DP_CR)	/* IRR3 */
#define DP_CR72		(0x0016|DP_CR)	/* ITV  */
#define DP_CR73		(0x0017|DP_CR)	/* PMV  */
#define DP_CR74		(0x0018|DP_CR)	/* CMCV */
    /* CR75-CR79: reserved */
#define DP_CR80		(0x0020|DP_CR)	/* LRR0 */
#define DP_CR81		(0x0021|DP_CR)	/* LRR1 */
    /* CR82-CR127: reserved */

/* System, Debug Data Breakpoint Registers (DBR) */ 
#define DP_DBR		0x0200
#define DP_DBR0		(0x0000|DP_DBR)
#define DP_DBR1		(0x0001|DP_DBR)
#define DP_DBR2		(0x0002|DP_DBR)
#define DP_DBR3		(0x0003|DP_DBR)
#define DP_DBR4		(0x0004|DP_DBR)
#define DP_DBR5		(0x0005|DP_DBR)
#define DP_DBR6		(0x0006|DP_DBR)
#define DP_DBR7		(0x0007|DP_DBR)

/* System, Debug Instruction Breakpoint Registers (IBR) */
#define DP_IBR		0x0300
#define DP_IBR0		(0x0000|DP_IBR)
#define DP_IBR1		(0x0001|DP_IBR)
#define DP_IBR2		(0x0002|DP_IBR)
#define DP_IBR3		(0x0003|DP_IBR)
#define DP_IBR4		(0x0004|DP_IBR)
#define DP_IBR5		(0x0005|DP_IBR)
#define DP_IBR6		(0x0006|DP_IBR)
#define DP_IBR7		(0x0007|DP_IBR)

/* System, Performance Monitor Configuration Registers (PMC) */
#define DP_PMC		0x0400
#define DP_PMC0		(0x0000|DP_PMC)
#define DP_PMC1		(0x0001|DP_PMC)
#define DP_PMC2		(0x0002|DP_PMC)
#define DP_PMC3		(0x0003|DP_PMC)
#define DP_PMC4		(0x0004|DP_PMC)
#define DP_PMC5		(0x0005|DP_PMC)
#define DP_PMC6		(0x0006|DP_PMC)
#define DP_PMC7		(0x0007|DP_PMC)
#define DP_PMC8		(0x0008|DP_PMC)
#define DP_PMC9		(0x0009|DP_PMC)
#define DP_PMC10	(0x000a|DP_PMC)
#define DP_PMC11	(0x000b|DP_PMC)
#define DP_PMC12	(0x000c|DP_PMC)
#define DP_PMC13	(0x000d|DP_PMC)
#define DP_PMC14	(0x000e|DP_PMC)
#define DP_PMC15	(0x000f|DP_PMC)
#define DP_PMC16	(0x0010|DP_PMC)
#define DP_PMC17	(0x0011|DP_PMC)
#define DP_PMC18	(0x0012|DP_PMC)
#define DP_PMC19	(0x0013|DP_PMC)
#define DP_PMC20	(0x0014|DP_PMC)
#define DP_PMC21	(0x0015|DP_PMC)
#define DP_PMC22	(0x0016|DP_PMC)
#define DP_PMC23	(0x0017|DP_PMC)
#define DP_PMC24	(0x0018|DP_PMC)
#define DP_PMC25	(0x0019|DP_PMC)
#define DP_PMC26	(0x001a|DP_PMC)
#define DP_PMC27	(0x001b|DP_PMC)
#define DP_PMC28	(0x001c|DP_PMC)
#define DP_PMC29	(0x001d|DP_PMC)
#define DP_PMC30	(0x001e|DP_PMC)
#define DP_PMC31	(0x001f|DP_PMC)

/* System, Region Registers (RR) */
#define DP_RR		0x0500
#define DP_RR0		(0x0000|DP_RR)
#define DP_RR1		(0x0001|DP_RR)
#define DP_RR2		(0x0002|DP_RR)
#define DP_RR3		(0x0003|DP_RR)
#define DP_RR4		(0x0004|DP_RR)
#define DP_RR5		(0x0005|DP_RR)
#define DP_RR6		(0x0006|DP_RR)
#define DP_RR7		(0x0007|DP_RR)

/* System, Protection Key Registers (PKR) */
#define DP_PKR		0x0600
#define DP_PKR0		(0x0000|DP_PKR)
#define DP_PKR1		(0x0001|DP_PKR)
#define DP_PKR2		(0x0002|DP_PKR)
#define DP_PKR3		(0x0003|DP_PKR)
#define DP_PKR4		(0x0004|DP_PKR)
#define DP_PKR5		(0x0005|DP_PKR)
#define DP_PKR6		(0x0006|DP_PKR)
#define DP_PKR7		(0x0007|DP_PKR)
#define DP_PKR8		(0x0008|DP_PKR)
#define DP_PKR9		(0x0009|DP_PKR)
#define DP_PKR10	(0x000a|DP_PKR)
#define DP_PKR11	(0x000b|DP_PKR)
#define DP_PKR12	(0x000c|DP_PKR)
#define DP_PKR13	(0x000d|DP_PKR)
#define DP_PKR14	(0x000e|DP_PKR)
#define DP_PKR15	(0x000f|DP_PKR)
#define DP_PKR16	(0x0010|DP_PKR)
#define DP_PKR17	(0x0011|DP_PKR)

/* System, Translation Lookaside Buffer (TLB) */ 
#define DP_ITR		0x0700
#define DP_ITR0		(0x0000|DP_ITR)
#define DP_ITR1		(0x0001|DP_ITR)
#define DP_ITR2		(0x0002|DP_ITR)
#define DP_ITR3		(0x0003|DP_ITR)
#define DP_ITR4		(0x0004|DP_ITR)
#define DP_ITR5		(0x0005|DP_ITR)
#define DP_ITR6		(0x0006|DP_ITR)
#define DP_ITR7		(0x0007|DP_ITR)

#define DP_DTR		0x0800
#define DP_DTR0		(0x0000|DP_DTR)
#define DP_DTR1		(0x0001|DP_DTR)
#define DP_DTR2		(0x0002|DP_DTR)
#define DP_DTR3		(0x0003|DP_DTR)
#define DP_DTR4		(0x0004|DP_DTR)
#define DP_DTR5		(0x0005|DP_DTR)
#define DP_DTR6		(0x0006|DP_DTR)
#define DP_DTR7		(0x0007|DP_DTR)

#define DP_ITC		0x0900
#define DP_DTC		0x0a00

/* Application, General Registers (GRs) */
#define DP_GR_STATIC	0x0b00
#define DP_GR0		(0x0000|DP_GR_STATIC)
#define DP_GR1		(0x0001|DP_GR_STATIC)
#define DP_GR2		(0x0002|DP_GR_STATIC)
#define DP_GR3		(0x0003|DP_GR_STATIC)
#define DP_GR4		(0x0004|DP_GR_STATIC)
#define DP_GR5		(0x0005|DP_GR_STATIC)
#define DP_GR6		(0x0006|DP_GR_STATIC)
#define DP_GR7		(0x0007|DP_GR_STATIC)
#define DP_GR8		(0x0008|DP_GR_STATIC)
#define DP_GR9		(0x0009|DP_GR_STATIC)
#define DP_GR10		(0x000a|DP_GR_STATIC)
#define DP_GR11		(0x000b|DP_GR_STATIC)
#define DP_GR12		(0x000c|DP_GR_STATIC)
#define DP_GR13		(0x000d|DP_GR_STATIC)
#define DP_GR14		(0x000e|DP_GR_STATIC)
#define DP_GR15		(0x000f|DP_GR_STATIC)
#define DP_GR16		(0x0010|DP_GR_STATIC)
#define DP_GR17		(0x0011|DP_GR_STATIC)
#define DP_GR18		(0x0012|DP_GR_STATIC)
#define DP_GR19		(0x0013|DP_GR_STATIC)
#define DP_GR20		(0x0014|DP_GR_STATIC)
#define DP_GR21		(0x0015|DP_GR_STATIC)
#define DP_GR22		(0x0016|DP_GR_STATIC)
#define DP_GR23		(0x0017|DP_GR_STATIC)
#define DP_GR24		(0x0018|DP_GR_STATIC)
#define DP_GR25		(0x0019|DP_GR_STATIC)
#define DP_GR26		(0x001a|DP_GR_STATIC)
#define DP_GR27		(0x001b|DP_GR_STATIC)
#define DP_GR28		(0x001c|DP_GR_STATIC)
#define DP_GR29		(0x001d|DP_GR_STATIC)
#define DP_GR30		(0x001e|DP_GR_STATIC)
#define DP_GR31		(0x001f|DP_GR_STATIC)

#define DP_GR_STACKED	0x0c00
#define DP_GR32		(0x0020|DP_GR_STACKED)
#define DP_GR33		(0x0021|DP_GR_STACKED)
#define DP_GR34		(0x0022|DP_GR_STACKED)
#define DP_GR35		(0x0023|DP_GR_STACKED)
#define DP_GR36		(0x0024|DP_GR_STACKED)
#define DP_GR37		(0x0025|DP_GR_STACKED)
#define DP_GR38		(0x0026|DP_GR_STACKED)
#define DP_GR39		(0x0027|DP_GR_STACKED)
#define DP_GR40		(0x0028|DP_GR_STACKED)
#define DP_GR41		(0x0029|DP_GR_STACKED)
#define DP_GR42		(0x002a|DP_GR_STACKED)
#define DP_GR43		(0x002b|DP_GR_STACKED)
#define DP_GR44		(0x002c|DP_GR_STACKED)
#define DP_GR45		(0x002d|DP_GR_STACKED)
#define DP_GR46		(0x002e|DP_GR_STACKED)
#define DP_GR47		(0x002f|DP_GR_STACKED)
#define DP_GR48		(0x0030|DP_GR_STACKED)
#define DP_GR49		(0x0031|DP_GR_STACKED)
#define DP_GR50		(0x0032|DP_GR_STACKED)
#define DP_GR51		(0x0033|DP_GR_STACKED)
#define DP_GR52		(0x0034|DP_GR_STACKED)
#define DP_GR53		(0x0035|DP_GR_STACKED)
#define DP_GR54		(0x0036|DP_GR_STACKED)
#define DP_GR55		(0x0037|DP_GR_STACKED)
#define DP_GR56		(0x0038|DP_GR_STACKED)
#define DP_GR57		(0x0039|DP_GR_STACKED)
#define DP_GR58		(0x003a|DP_GR_STACKED)
#define DP_GR59		(0x003b|DP_GR_STACKED)
#define DP_GR60		(0x003c|DP_GR_STACKED)
#define DP_GR61		(0x003d|DP_GR_STACKED)
#define DP_GR62		(0x003e|DP_GR_STACKED)
#define DP_GR63		(0x003f|DP_GR_STACKED)
#define DP_GR64		(0x0040|DP_GR_STACKED)
#define DP_GR65		(0x0041|DP_GR_STACKED)
#define DP_GR66		(0x0042|DP_GR_STACKED)
#define DP_GR67		(0x0043|DP_GR_STACKED)
#define DP_GR68		(0x0044|DP_GR_STACKED)
#define DP_GR69		(0x0045|DP_GR_STACKED)
#define DP_GR70		(0x0046|DP_GR_STACKED)
#define DP_GR71		(0x0047|DP_GR_STACKED)
#define DP_GR72		(0x0048|DP_GR_STACKED)
#define DP_GR73		(0x0049|DP_GR_STACKED)
#define DP_GR74		(0x004a|DP_GR_STACKED)
#define DP_GR75		(0x004b|DP_GR_STACKED)
#define DP_GR76		(0x004c|DP_GR_STACKED)
#define DP_GR77		(0x004d|DP_GR_STACKED)
#define DP_GR78		(0x004e|DP_GR_STACKED)
#define DP_GR79		(0x004f|DP_GR_STACKED)
#define DP_GR80		(0x0050|DP_GR_STACKED)
#define DP_GR81		(0x0051|DP_GR_STACKED)
#define DP_GR82		(0x0052|DP_GR_STACKED)
#define DP_GR83		(0x0053|DP_GR_STACKED)
#define DP_GR84		(0x0054|DP_GR_STACKED)
#define DP_GR85		(0x0055|DP_GR_STACKED)
#define DP_GR86		(0x0056|DP_GR_STACKED)
#define DP_GR87		(0x0057|DP_GR_STACKED)
#define DP_GR88		(0x0058|DP_GR_STACKED)
#define DP_GR89		(0x0059|DP_GR_STACKED)
#define DP_GR90		(0x005a|DP_GR_STACKED)
#define DP_GR91		(0x005b|DP_GR_STACKED)
#define DP_GR92		(0x005c|DP_GR_STACKED)
#define DP_GR93		(0x005d|DP_GR_STACKED)
#define DP_GR94		(0x005e|DP_GR_STACKED)
#define DP_GR95		(0x005f|DP_GR_STACKED)
#define DP_GR96		(0x0060|DP_GR_STACKED)
#define DP_GR97		(0x0061|DP_GR_STACKED)
#define DP_GR98		(0x0062|DP_GR_STACKED)
#define DP_GR99		(0x0063|DP_GR_STACKED)
#define DP_GR100	(0x0064|DP_GR_STACKED)
#define DP_GR101	(0x0065|DP_GR_STACKED)
#define DP_GR102	(0x0066|DP_GR_STACKED)
#define DP_GR103	(0x0067|DP_GR_STACKED)
#define DP_GR104	(0x0068|DP_GR_STACKED)
#define DP_GR105	(0x0069|DP_GR_STACKED)
#define DP_GR106	(0x006a|DP_GR_STACKED)
#define DP_GR107	(0x006b|DP_GR_STACKED)
#define DP_GR108	(0x006c|DP_GR_STACKED)
#define DP_GR109	(0x006d|DP_GR_STACKED)
#define DP_GR110	(0x006e|DP_GR_STACKED)
#define DP_GR111	(0x006f|DP_GR_STACKED)
#define DP_GR112	(0x0070|DP_GR_STACKED)
#define DP_GR113	(0x0071|DP_GR_STACKED)
#define DP_GR114	(0x0072|DP_GR_STACKED)
#define DP_GR115	(0x0073|DP_GR_STACKED)
#define DP_GR116	(0x0074|DP_GR_STACKED)
#define DP_GR117	(0x0075|DP_GR_STACKED)
#define DP_GR118	(0x0076|DP_GR_STACKED)
#define DP_GR119	(0x0077|DP_GR_STACKED)
#define DP_GR120	(0x0078|DP_GR_STACKED)
#define DP_GR121	(0x0079|DP_GR_STACKED)
#define DP_GR122	(0x007a|DP_GR_STACKED)
#define DP_GR123	(0x007b|DP_GR_STACKED)
#define DP_GR124	(0x007c|DP_GR_STACKED)
#define DP_GR125	(0x007d|DP_GR_STACKED)
#define DP_GR126	(0x007e|DP_GR_STACKED)
#define DP_GR127	(0x007f|DP_GR_STACKED)

/* Application, Floating-point Registers (FRs) */
#define DP_FR_STATIC	0x0d00
#define DP_FR0		(0x0000|DP_FR_STATIC)
#define DP_FR1		(0x0001|DP_FR_STATIC)
#define DP_FR2		(0x0002|DP_FR_STATIC)
#define DP_FR3		(0x0003|DP_FR_STATIC)
#define DP_FR4		(0x0004|DP_FR_STATIC)
#define DP_FR5		(0x0005|DP_FR_STATIC)
#define DP_FR6		(0x0006|DP_FR_STATIC)
#define DP_FR7		(0x0007|DP_FR_STATIC)
#define DP_FR8		(0x0008|DP_FR_STATIC)
#define DP_FR9		(0x0009|DP_FR_STATIC)
#define DP_FR10		(0x000a|DP_FR_STATIC)
#define DP_FR11		(0x000b|DP_FR_STATIC)
#define DP_FR12		(0x000c|DP_FR_STATIC)
#define DP_FR13		(0x000d|DP_FR_STATIC)
#define DP_FR14		(0x000e|DP_FR_STATIC)
#define DP_FR15		(0x000f|DP_FR_STATIC)
#define DP_FR16		(0x0010|DP_FR_STATIC)
#define DP_FR17		(0x0011|DP_FR_STATIC)
#define DP_FR18		(0x0012|DP_FR_STATIC)
#define DP_FR19		(0x0013|DP_FR_STATIC)
#define DP_FR20		(0x0014|DP_FR_STATIC)
#define DP_FR21		(0x0015|DP_FR_STATIC)
#define DP_FR22		(0x0016|DP_FR_STATIC)
#define DP_FR23		(0x0017|DP_FR_STATIC)
#define DP_FR24		(0x0018|DP_FR_STATIC)
#define DP_FR25		(0x0019|DP_FR_STATIC)
#define DP_FR26		(0x001a|DP_FR_STATIC)
#define DP_FR27		(0x001b|DP_FR_STATIC)
#define DP_FR28		(0x001c|DP_FR_STATIC)
#define DP_FR29		(0x001d|DP_FR_STATIC)
#define DP_FR30		(0x001e|DP_FR_STATIC)
#define DP_FR31		(0x001f|DP_FR_STATIC)

#define DP_FR_ROTATING	0x0e00
#define DP_FR32		(0x0020|DP_FR_ROTATING)
#define DP_FR33		(0x0021|DP_FR_ROTATING)
#define DP_FR34		(0x0022|DP_FR_ROTATING)
#define DP_FR35		(0x0023|DP_FR_ROTATING)
#define DP_FR36		(0x0024|DP_FR_ROTATING)
#define DP_FR37		(0x0025|DP_FR_ROTATING)
#define DP_FR38		(0x0026|DP_FR_ROTATING)
#define DP_FR39		(0x0027|DP_FR_ROTATING)
#define DP_FR40		(0x0028|DP_FR_ROTATING)
#define DP_FR41		(0x0029|DP_FR_ROTATING)
#define DP_FR42		(0x002a|DP_FR_ROTATING)
#define DP_FR43		(0x002b|DP_FR_ROTATING)
#define DP_FR44		(0x002c|DP_FR_ROTATING)
#define DP_FR45		(0x002d|DP_FR_ROTATING)
#define DP_FR46		(0x002e|DP_FR_ROTATING)
#define DP_FR47		(0x002f|DP_FR_ROTATING)
#define DP_FR48		(0x0030|DP_FR_ROTATING)
#define DP_FR49		(0x0031|DP_FR_ROTATING)
#define DP_FR50		(0x0032|DP_FR_ROTATING)
#define DP_FR51		(0x0033|DP_FR_ROTATING)
#define DP_FR52		(0x0034|DP_FR_ROTATING)
#define DP_FR53		(0x0035|DP_FR_ROTATING)
#define DP_FR54		(0x0036|DP_FR_ROTATING)
#define DP_FR55		(0x0037|DP_FR_ROTATING)
#define DP_FR56		(0x0038|DP_FR_ROTATING)
#define DP_FR57		(0x0039|DP_FR_ROTATING)
#define DP_FR58		(0x003a|DP_FR_ROTATING)
#define DP_FR59		(0x003b|DP_FR_ROTATING)
#define DP_FR60		(0x003c|DP_FR_ROTATING)
#define DP_FR61		(0x003d|DP_FR_ROTATING)
#define DP_FR62		(0x003e|DP_FR_ROTATING)
#define DP_FR63		(0x003f|DP_FR_ROTATING)
#define DP_FR64		(0x0040|DP_FR_ROTATING)
#define DP_FR65		(0x0041|DP_FR_ROTATING)
#define DP_FR66		(0x0042|DP_FR_ROTATING)
#define DP_FR67		(0x0043|DP_FR_ROTATING)
#define DP_FR68		(0x0044|DP_FR_ROTATING)
#define DP_FR69		(0x0045|DP_FR_ROTATING)
#define DP_FR70		(0x0046|DP_FR_ROTATING)
#define DP_FR71		(0x0047|DP_FR_ROTATING)
#define DP_FR72		(0x0048|DP_FR_ROTATING)
#define DP_FR73		(0x0049|DP_FR_ROTATING)
#define DP_FR74		(0x004a|DP_FR_ROTATING)
#define DP_FR75		(0x004b|DP_FR_ROTATING)
#define DP_FR76		(0x004c|DP_FR_ROTATING)
#define DP_FR77		(0x004d|DP_FR_ROTATING)
#define DP_FR78		(0x004e|DP_FR_ROTATING)
#define DP_FR79		(0x004f|DP_FR_ROTATING)
#define DP_FR80		(0x0050|DP_FR_ROTATING)
#define DP_FR81		(0x0051|DP_FR_ROTATING)
#define DP_FR82		(0x0052|DP_FR_ROTATING)
#define DP_FR83		(0x0053|DP_FR_ROTATING)
#define DP_FR84		(0x0054|DP_FR_ROTATING)
#define DP_FR85		(0x0055|DP_FR_ROTATING)
#define DP_FR86		(0x0056|DP_FR_ROTATING)
#define DP_FR87		(0x0057|DP_FR_ROTATING)
#define DP_FR88		(0x0058|DP_FR_ROTATING)
#define DP_FR89		(0x0059|DP_FR_ROTATING)
#define DP_FR90		(0x005a|DP_FR_ROTATING)
#define DP_FR91		(0x005b|DP_FR_ROTATING)
#define DP_FR92		(0x005c|DP_FR_ROTATING)
#define DP_FR93		(0x005d|DP_FR_ROTATING)
#define DP_FR94		(0x005e|DP_FR_ROTATING)
#define DP_FR95		(0x005f|DP_FR_ROTATING)
#define DP_FR96		(0x0060|DP_FR_ROTATING)
#define DP_FR97		(0x0061|DP_FR_ROTATING)
#define DP_FR98		(0x0062|DP_FR_ROTATING)
#define DP_FR99		(0x0063|DP_FR_ROTATING)
#define DP_FR100	(0x0064|DP_FR_ROTATING)
#define DP_FR101	(0x0065|DP_FR_ROTATING)
#define DP_FR102	(0x0066|DP_FR_ROTATING)
#define DP_FR103	(0x0067|DP_FR_ROTATING)
#define DP_FR104	(0x0068|DP_FR_ROTATING)
#define DP_FR105	(0x0069|DP_FR_ROTATING)
#define DP_FR106	(0x006a|DP_FR_ROTATING)
#define DP_FR107	(0x006b|DP_FR_ROTATING)
#define DP_FR108	(0x006c|DP_FR_ROTATING)
#define DP_FR109	(0x006d|DP_FR_ROTATING)
#define DP_FR110	(0x006e|DP_FR_ROTATING)
#define DP_FR111	(0x006f|DP_FR_ROTATING)
#define DP_FR112	(0x0070|DP_FR_ROTATING)
#define DP_FR113	(0x0071|DP_FR_ROTATING)
#define DP_FR114	(0x0072|DP_FR_ROTATING)
#define DP_FR115	(0x0073|DP_FR_ROTATING)
#define DP_FR116	(0x0074|DP_FR_ROTATING)
#define DP_FR117	(0x0075|DP_FR_ROTATING)
#define DP_FR118	(0x0076|DP_FR_ROTATING)
#define DP_FR119	(0x0077|DP_FR_ROTATING)
#define DP_FR120	(0x0078|DP_FR_ROTATING)
#define DP_FR121	(0x0079|DP_FR_ROTATING)
#define DP_FR122	(0x007a|DP_FR_ROTATING)
#define DP_FR123	(0x007b|DP_FR_ROTATING)
#define DP_FR124	(0x007c|DP_FR_ROTATING)
#define DP_FR125	(0x007d|DP_FR_ROTATING)
#define DP_FR126	(0x007e|DP_FR_ROTATING)
#define DP_FR127	(0x007f|DP_FR_ROTATING)

/* Application, Predicate Registers (PRs) */
#define DP_PR		0x0f00
#define DP_PR0		(0x0001|DP_PR)
#define DP_PR1		(0x0002|DP_PR)
#define DP_PR2		(0x0003|DP_PR)
#define DP_PR3		(0x0004|DP_PR)
#define DP_PR4		(0x0005|DP_PR)
#define DP_PR5		(0x0006|DP_PR)
#define DP_PR6		(0x0007|DP_PR)
#define DP_PR7		(0x0008|DP_PR)
#define DP_PR8		(0x0009|DP_PR)
#define DP_PR9		(0x000a|DP_PR)
#define DP_PR10		(0x000b|DP_PR)
#define DP_PR11		(0x000c|DP_PR)
#define DP_PR12		(0x000d|DP_PR)
#define DP_PR13		(0x000e|DP_PR)
#define DP_PR14		(0x000f|DP_PR)
#define DP_PR15		(0x0010|DP_PR)
#define DP_PR16		(0x0011|DP_PR)
#define DP_PR17		(0x0012|DP_PR)
#define DP_PR18		(0x0013|DP_PR)
#define DP_PR19		(0x0014|DP_PR)
#define DP_PR20		(0x0015|DP_PR)
#define DP_PR21		(0x0016|DP_PR)
#define DP_PR22		(0x0017|DP_PR)
#define DP_PR23		(0x0018|DP_PR)
#define DP_PR24		(0x0019|DP_PR)
#define DP_PR25		(0x001a|DP_PR)
#define DP_PR26		(0x001b|DP_PR)
#define DP_PR27		(0x001c|DP_PR)
#define DP_PR28		(0x001d|DP_PR)
#define DP_PR29		(0x001e|DP_PR)
#define DP_PR30		(0x001f|DP_PR)
#define DP_PR31		(0x0020|DP_PR)
#define DP_PR32		(0x0021|DP_PR)
#define DP_PR33		(0x0022|DP_PR)
#define DP_PR34		(0x0023|DP_PR)
#define DP_PR35		(0x0024|DP_PR)
#define DP_PR36		(0x0025|DP_PR)
#define DP_PR37		(0x0026|DP_PR)
#define DP_PR38		(0x0027|DP_PR)
#define DP_PR39		(0x0028|DP_PR)
#define DP_PR40		(0x0029|DP_PR)
#define DP_PR41		(0x002a|DP_PR)
#define DP_PR42		(0x002b|DP_PR)
#define DP_PR43		(0x002c|DP_PR)
#define DP_PR44		(0x002d|DP_PR)
#define DP_PR45		(0x002e|DP_PR)
#define DP_PR46		(0x002f|DP_PR)
#define DP_PR47		(0x0030|DP_PR)
#define DP_PR48		(0x0031|DP_PR)
#define DP_PR49		(0x0032|DP_PR)
#define DP_PR50		(0x0033|DP_PR)
#define DP_PR51		(0x0034|DP_PR)
#define DP_PR52		(0x0035|DP_PR)
#define DP_PR53		(0x0036|DP_PR)
#define DP_PR54		(0x0037|DP_PR)
#define DP_PR55		(0x0038|DP_PR)
#define DP_PR56		(0x0039|DP_PR)
#define DP_PR57		(0x003a|DP_PR)
#define DP_PR58		(0x003b|DP_PR)
#define DP_PR59		(0x003c|DP_PR)
#define DP_PR60		(0x003d|DP_PR)
#define DP_PR61		(0x003e|DP_PR)
#define DP_PR62		(0x003f|DP_PR)
#define DP_PR63		(0x0040|DP_PR)
#define DP_PR_ROT	(0x0041|DP_PR)

/* Application, Branch Registers (BRs) */
#define DP_BR		0x1000
#define DP_BR0		(0x0000|DP_BR)
#define DP_BR1		(0x0001|DP_BR)
#define DP_BR2		(0x0002|DP_BR)
#define DP_BR3		(0x0003|DP_BR)
#define DP_BR4		(0x0004|DP_BR)
#define DP_BR5		(0x0005|DP_BR)
#define DP_BR6		(0x0006|DP_BR)
#define DP_BR7		(0x0007|DP_BR)

/* Application, Instruction Pointer (IP) */
#define DP_IP		0x1100

/* Application, Application Registers (ARs) */
#define DP_AR		0x1300
#define DP_AR0		(0x0000|DP_AR)	/* KR0 */
#define DP_AR1		(0x0001|DP_AR)	/* KR1 */
#define DP_AR2		(0x0002|DP_AR)	/* KR2 */
#define DP_AR3		(0x0003|DP_AR)	/* KR3 */
#define DP_AR4		(0x0004|DP_AR)	/* KR4 */
#define DP_AR5		(0x0005|DP_AR)	/* KR5 */
#define DP_AR6		(0x0006|DP_AR)	/* KR6 */
#define DP_AR7		(0x0007|DP_AR)	/* KR7 */
/* AR8-AR15: reserved */
#define DP_AR16		(0x0008|DP_AR)	/* RSC */
#define DP_AR17		(0x0009|DP_AR)	/* BSP */
#define DP_AR18		(0x000a|DP_AR)	/* BSPSTORE */
#define DP_AR19		(0x000b|DP_AR)	/* RNAT */
/* AR20: reserved */
/* AR21: IA-32 register */
/* AR22-AR23: reserved */
/* AR24-AR30: IA-32 registers */
/* AR31: reserved */
#define DP_AR32		(0x000c|DP_AR)	/* CCV */
/* AR33-AR35: reserved */
#define DP_AR36		(0x000d|DP_AR)	/* UNAT */
/* AR37-AR39: reserved */
#define DP_AR40		(0x000e|DP_AR)	/* FPSR */
/* AR41-AR43: reserved */
#define DP_AR44		(0x000f|DP_AR)	/* ITC */
/* AR45-AR47: reserved, AR48-AR63: ignored */
#define DP_AR64		(0x0010|DP_AR)	/* PFS */
#define DP_AR65		(0x0011|DP_AR)	/* LC */
#define DP_AR66		(0x0012|DP_AR)	/* EC */
/* AR67-AR111: reserved, AR112-AR127: ignored */

/* Application, Performance Monitor Data Registers (PMD) */
#define DP_PMD		0x1400
#define DP_PMD0		(0x0000|DP_PMD)
#define DP_PMD1		(0x0001|DP_PMD)
#define DP_PMD2		(0x0002|DP_PMD)
#define DP_PMD3		(0x0003|DP_PMD)
#define DP_PMD4		(0x0004|DP_PMD)
#define DP_PMD5		(0x0005|DP_PMD)
#define DP_PMD6		(0x0006|DP_PMD)
#define DP_PMD7		(0x0007|DP_PMD)
#define DP_PMD8		(0x0008|DP_PMD)
#define DP_PMD9		(0x0009|DP_PMD)
#define DP_PMD10	(0x000a|DP_PMD)
#define DP_PMD11	(0x000b|DP_PMD)
#define DP_PMD12	(0x000c|DP_PMD)
#define DP_PMD13	(0x000d|DP_PMD)
#define DP_PMD14	(0x000e|DP_PMD)
#define DP_PMD15	(0x000f|DP_PMD)
#define DP_PMD16	(0x0010|DP_PMD)
#define DP_PMD17	(0x0011|DP_PMD)
#define DP_PMD18	(0x0012|DP_PMD)
#define DP_PMD19	(0x0013|DP_PMD)
#define DP_PMD20	(0x0014|DP_PMD)
#define DP_PMD21	(0x0015|DP_PMD)
#define DP_PMD22	(0x0016|DP_PMD)
#define DP_PMD23	(0x0017|DP_PMD)
#define DP_PMD24	(0x0018|DP_PMD)
#define DP_PMD25	(0x0019|DP_PMD)
#define DP_PMD26	(0x001a|DP_PMD)
#define DP_PMD27	(0x001b|DP_PMD)
#define DP_PMD28	(0x001c|DP_PMD)
#define DP_PMD29	(0x001d|DP_PMD)
#define DP_PMD30	(0x001e|DP_PMD)
#define DP_PMD31	(0x001f|DP_PMD)
				
/* Application, Processor Identifiers (CPUID) */
#define DP_CPUID	0x1500
#define DP_CPUID0	0x0000
#define DP_CPUID1	0x0001
#define DP_CPUID2	0x0002
#define DP_CPUID3	0x0003
#define DP_CPUID4	0x0004

#ifndef __KERNEL__

#ifndef PAGE_OFFSET
#define PAGE_OFFSET		0xe000000000000000
#endif
#endif /* !__KERNEL__ */

#endif
