/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1995, 1996 by Ralf Baechle, Andreas Busse,
 *                             Stoned Elipot and Paul M. Antoine.
 */
#ifndef _ASM_BOOTINFO_H
#define _ASM_BOOTINFO_H

/*
 * Values for machgroup
 */
#define MACH_GROUP_UNKNOWN      0 /* whatever... */
#define MACH_GROUP_JAZZ     	1 /* Jazz                                     */
#define MACH_GROUP_DEC          2 /* Digital Equipment                        */
#define MACH_GROUP_ARC		3 /* Wreckstation Tyne, rPC44, possibly other */
#define MACH_GROUP_SNI_RM	4 /* Siemens Nixdorf RM series                */
#define MACH_GROUP_ACN		5
#define MACH_GROUP_SGI          6 /* Silicon Graphics                         */
#define MACH_GROUP_COBALT       7 /* Cobalt servers		 	      */
#define MACH_GROUP_NEC_DDB	8 /* NEC DDB                                  */
#define MACH_GROUP_BAGET	9 /* Baget                                    */
#define MACH_GROUP_COSINE      10 /* CoSine Orion                             */
#define MACH_GROUP_GALILEO     11 /* Galileo Eval Boards                      */
#define MACH_GROUP_MOMENCO     12 /* Momentum Boards                          */
#define MACH_GROUP_ITE         13 /* ITE Semi Eval Boards                     */
#define MACH_GROUP_PHILIPS     14
#define MACH_GROUP_GLOBESPAN   15 /* Globespan PVR Referrence Board           */
#define MACH_GROUP_SIBYTE      16 /* Sibyte Eval Boards                       */
#define MACH_GROUP_TOSHIBA     17 /* Toshiba Reference Systems TSBREF         */
#define MACH_GROUP_ALCHEMY     18 /* Alchemy Semi Eval Boards*/

#define GROUP_NAMES { "unknown", "Jazz", "Digital", "ARC", "SNI", "ACN",      \
	"SGI", "Cobalt", "NEC DDB", "Baget", "Cosine", "Galileo", "Momentum", \
	"ITE", "Philips", "Globepspan", "SiByte", "Toshiba", "Alchemy" }

/*
 * Valid machtype values for group unknown (low order halfword of mips_machtype)
 */
#define MACH_UNKNOWN		0	/* whatever...			*/

#define GROUP_UNKNOWN_NAMES { "unknown" }

/*
 * Valid machtype values for group JAZZ
 */
#define MACH_ACER_PICA_61	0	/* Acer PICA-61 (PICA1)		*/
#define MACH_MIPS_MAGNUM_4000	1	/* Mips Magnum 4000 "RC4030"	*/
#define MACH_OLIVETTI_M700      2	/* Olivetti M700-10 (-15 ??)    */

#define GROUP_JAZZ_NAMES { "Acer PICA 61", "Mips Magnum 4000", "Olivetti M700" }

/*
 * Valid machtype for group DEC 
 */
#define MACH_DSUNKNOWN		0
#define MACH_DS23100		1	/* DECstation 2100 or 3100	*/
#define MACH_DS5100		2	/* DECstation 5100		*/
#define MACH_DS5000_200		3	/* DECstation 5000/200		*/
#define MACH_DS5000_1XX		4	/* DECstation 5000/120, 125, 133, 150 */
#define MACH_DS5000_XX		5	/* DECstation 5000/20, 25, 33, 50 */
#define MACH_DS5000_2X0		6	/* DECstation 5000/240, 260	*/
#define MACH_DS5400		7	/* DECstation 5400		*/
#define MACH_DS5500		8	/* DECstation 5500		*/
#define MACH_DS5800		9	/* DECstation 5800		*/

#define GROUP_DEC_NAMES { "unknown", "DECstation 2100/3100", "DECstation 5100", \
	"DECstation 5000/200", "DECstation 5000/1xx", "Personal DECstation 5000/xx", \
	"DECstation 5000/2x0", "DECstation 5400", "DECstation 5500", \
	"DECstation 5800" }

/*
 * Valid machtype for group ARC
 */
#define MACH_DESKSTATION_RPC44  0	/* Deskstation rPC44 */
#define MACH_DESKSTATION_TYNE	1	/* Deskstation Tyne */

#define GROUP_ARC_NAMES { "Deskstation rPC44", "Deskstation Tyne" }

/*
 * Valid machtype for group SNI_RM
 */
#define MACH_SNI_RM200_PCI	0	/* RM200/RM300/RM400 PCI series */

#define GROUP_SNI_RM_NAMES { "RM200 PCI" }

/*
 * Valid machtype for group ACN
 */
#define MACH_ACN_MIPS_BOARD	0       /* ACN MIPS single board        */

#define GROUP_ACN_NAMES { "ACN" }

/*
 * Valid machtype for group SGI
 */
#define MACH_SGI_INDY		0	/* R4?K and R5K Indy workstations */
#define MACH_SGI_CHALLENGE_S	1	/* The Challenge S server */
#define MACH_SGI_INDIGO2	2	/* The Indigo2 system */

#define GROUP_SGI_NAMES { "Indy", "Challenge S", "Indigo2" }

/*
 * Valid machtype for group COBALT
 */
#define MACH_COBALT_27 		 0	/* Proto "27" hardware */

#define GROUP_COBALT_NAMES { "Microserver 27" }

/*
 * Valid machtype for group NEC DDB
 */
#define MACH_NEC_DDB5074	 0	/* NEC DDB Vrc-5074 */
#define MACH_NEC_DDB5476         1      /* NEC DDB Vrc-5476 */
#define MACH_NEC_DDB5477         2      /* NEC DDB Vrc-5477 */

#define GROUP_NEC_DDB_NAMES { "Vrc-5074", "Vrc-5476", "Vrc-5477"}

/*
 * Valid machtype for group BAGET
 */
#define MACH_BAGET201		0	/* BT23-201 */
#define MACH_BAGET202		1	/* BT23-202 */

#define GROUP_BAGET_NAMES { "BT23-201", "BT23-202" }

/*
 * Cosine boards.
 */
#define MACH_COSINE_ORION	0

#define GROUP_COSINE_NAMES { "Orion" }

/*
 * Valid machtype for group GALILEO
 */
#define MACH_EV96100		0	/* EV96100 */
#define MACH_EV64120A		1	/* EV64120A */

#define GROUP_GALILEO_NAMES { "EV96100" , "EV64120A" }

/*
 * Valid machtype for group MOMENCO
 */
#define MACH_MOMENCO_OCELOT		0

#define GROUP_MOMENCO_NAMES { "Ocelot" }

 
/*
 * Valid machtype for group ITE
 */
#define MACH_QED_4N_S01B	0	/* ITE8172 based eval board */
 
#define GROUP_ITE_NAMES { "QED-4N-S01B" } /* the actual board name */
	
/*
 * Valid machtype for group Globespan
 */
#define MACH_IVR       0                  /* IVR eval board */

#define GROUP_GLOBESPAN_NAMES { "IVR" }   /* the actual board name */   

/*
 * Valid machtype for group PHILIPS
 */
#define MACH_PHILIPS_NINO	0	/* Nino */
#define MACH_PHILIPS_VELO	1	/* Velo */

#define GROUP_PHILIPS_NAMES { "Nino" , "Velo" }

/*
 * Valid machtype for group SIBYTE
 */
#define MACH_SWARM              0

#define GROUP_SIBYTE_NAMES {"SWARM" }

/*
 * Valid machtypes for group Toshiba
 */
#define MACH_PALLAS		0
#define MACH_TOPAS		1
#define MACH_JMR		2

#define GROUP_TOSHIBA_NAMES { "Pallas", "TopasCE", "JMR" }

/*
 * Valid machtype for group Alchemy
 */
#define MACH_PB1000	0	         /* Au1000-based eval board */
 
#define GROUP_ALCHEMY_NAMES { "PB1000" } /* the actual board name */

/*
 * Valid cputype values
 */
#define CPU_UNKNOWN		0
#define CPU_R2000		1
#define CPU_R3000		2
#define CPU_R3000A		3
#define CPU_R3041		4
#define CPU_R3051		5
#define CPU_R3052		6
#define CPU_R3081		7
#define CPU_R3081E		8
#define CPU_R4000PC		9
#define CPU_R4000SC		10
#define CPU_R4000MC		11
#define CPU_R4200		12
#define CPU_R4400PC		13
#define CPU_R4400SC		14
#define CPU_R4400MC		15
#define CPU_R4600		16
#define CPU_R6000		17
#define CPU_R6000A		18
#define CPU_R8000		19
#define CPU_R10000		20
#define CPU_R4300		21
#define CPU_R4650		22
#define CPU_R4700		23
#define CPU_R5000		24
#define CPU_R5000A		25
#define CPU_R4640		26
#define CPU_NEVADA		27	/* RM5230, RM5260 */
#define CPU_RM7000		28
#define CPU_R5432		29
#define CPU_4KC			30
#define CPU_5KC			31
#define CPU_R4310		32
#define CPU_SB1			33
#define CPU_TX3912		34
#define CPU_TX3922		35
#define CPU_TX3927		36
#define CPU_AU1000		37
#define CPU_4KEC		38
#define CPU_4KSC		39
#define CPU_VR41XX		40
#define CPU_LAST		40


#define CPU_NAMES { "unknown", "R2000", "R3000", "R3000A", "R3041", "R3051", \
        "R3052", "R3081", "R3081E", "R4000PC", "R4000SC", "R4000MC",         \
        "R4200", "R4400PC", "R4400SC", "R4400MC", "R4600", "R6000",          \
        "R6000A", "R8000", "R10000", "R4300", "R4650", "R4700", "R5000",     \
        "R5000A", "R4640", "Nevada", "RM7000", "R5432", "MIPS 4Kc",          \
        "MIPS 5Kc", "R4310", "SiByte SB1", "TX3912", "TX3922", "TX3927",     \
	"Au1000", "MIPS 4KEc", "MIPS 4KSc", "NEC Vr41xx" }

#define COMMAND_LINE_SIZE	256

#define BOOT_MEM_MAP_MAX	32
#define BOOT_MEM_RAM		1
#define BOOT_MEM_ROM_DATA	2
#define BOOT_MEM_RESERVED	3

#ifndef __ASSEMBLY__

/*
 * Some machine parameters passed by the bootloaders. 
 */

struct drive_info_struct {
	char dummy[32];
};

/* This is the same as in Milo but renamed for the sake of kernel's */
/* namespace */
typedef struct mips_arc_DisplayInfo {	/* video adapter information */
	unsigned short cursor_x;
	unsigned short cursor_y;
	unsigned short columns;
	unsigned short lines;
} mips_arc_DisplayInfo;

/* default values for drive info */
#define DEFAULT_DRIVE_INFO { {0,}}

/*
 * These are the kernel variables initialized from
 * the tag. And they have to be initialized to dummy/default
 * values in setup.c (or whereever suitable) so they are in
 * .data section
 */
extern struct mips_cpu mips_cpu;
extern unsigned long mips_machtype;
extern unsigned long mips_machgroup;
extern unsigned long mips_tlb_entries;

/*
 * A memory map that's built upon what was determined
 * or specified on the command line.
 */
struct boot_mem_map {
	int nr_map;
	struct {
		unsigned long addr;	/* start of memory segment */
		unsigned long size;	/* size of memory segment */
		long type;		/* type of memory segment */
	} map[BOOT_MEM_MAP_MAX];
};

extern struct boot_mem_map boot_mem_map;

extern void add_memory_region(unsigned long start, unsigned long size,
			      long type);

#endif /* !__ASSEMBLY__ */

#endif /* _ASM_BOOTINFO_H */
