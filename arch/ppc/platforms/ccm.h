/*
 * Siemens Card Controller Module specific definitions
 * 
 * Copyright (c) 2001 Wolfgang Denk (wd@denx.de)
 */

#ifndef __MACH_CCM_H
#define __MACH_CCM_H

#include <linux/config.h>
 
#include <asm/ppcboot.h>

#define	CCM_IMMR_BASE    0xF0000000	/* phys. addr of IMMR			*/
#define	CCM_IMAP_SIZE   (64 * 1024)	/* size of mapped area			*/

#define	IMAP_ADDR     CCM_IMMR_BASE	/* physical base address of IMMR area	*/
#define IMAP_SIZE     CCM_IMAP_SIZE	/* mapped size of IMMR area		*/

#define	FEC_INTERRUPT	15		/* = SIU_LEVEL7				*/
#define	DEC_INTERRUPT	13		/* = SIU_LEVEL6				*/
#define	CPM_INTERRUPT	11		/* = SIU_LEVEL5 (was: SIU_LEVEL2)	*/

/* We don't use the 8259.
*/
#define NR_8259_INTS	0

#endif	/* __MACH_CCM_H */
