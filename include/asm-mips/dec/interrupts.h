/*  
 * Miscellaneous definitions used to initialise the interrupt vector table
 * with the machine-specific interrupt routines.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1997 by Paul M. Antoine.
 * reworked 1998 by Harald Koerfgen.
 */

#ifndef __ASM_DEC_INTERRUPTS_H 
#define __ASM_DEC_INTERRUPTS_H 

/*
 * DECstation Interrupts
 */

/*
 * This list reflects the priority of the Interrupts.
 * Exception: on kmins we have to handle Memory Error 
 * Interrupts before the TC Interrupts.
 */
#define CLOCK 	0
#define SCSI_DMA_INT 	1
#define SCSI_INT	2
#define ETHER		3
#define SERIAL		4
#define TC0		5
#define TC1		6
#define TC2		7
#define MEMORY		8
#define FPU		9
#define HALT		10

#define NR_INTS	11

#ifndef _LANGUAGE_ASSEMBLY
/*
 * Data structure to hide the differences between the DECstation Interrupts
 *
 * If asic_mask == NULL, the interrupt is directly handled by the CPU.
 * Otherwise this Interrupt is handled the IRQ Controller.
 */

typedef struct
{
	unsigned int	cpu_mask;	/* checking and enabling interrupts in CP0	*/
	unsigned int	iemask;		/* enabling interrupts in IRQ Controller	*/
} decint_t;

/*
 * Interrupt table structure to hide differences between different
 * systems such.
 */
extern void *cpu_ivec_tbl[8];
extern long cpu_mask_tbl[8];
extern long cpu_irq_nr[8];
extern long asic_irq_nr[32];
extern long asic_mask_tbl[32];

/*
 * Common interrupt routine prototypes for all DECStations
 */
extern void	dec_intr_unimplemented(void);
extern void	dec_intr_fpu(void);
extern void	dec_intr_rtc(void);

extern void	kn02_io_int(void);
extern void	kn02ba_io_int(void);
extern void	kn03_io_int(void);

extern void	intr_halt(void);

extern void	asic_intr_unimplemented(void);

#endif
#endif 

