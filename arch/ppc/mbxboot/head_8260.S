#include "../kernel/ppc_defs.h"
#include "../kernel/ppc_asm.tmpl"
#include <asm/processor.h>
#include <asm/cache.h>

	.text

/*
 * $Id: head.S,v 1.33 1999/09/08 01:06:58 cort Exp $
 *	
 * Boot loader philosophy:
 *
 *      ROM loads us to some arbitrary location
 *      ROM loads these registers:
 *	
 *          R3 = Pointer to the board configuration data
 *          R5 = Pointer to Open Firmware data 	
 *		 	
 *      ROM jumps to start/start_
 *      Move the boot code to the link address (4 MB)
 *      Call decompress_kernel()
 *         Relocate the initrd, zimage and residual data to 4 MB
 *         Decompress the kernel to 0
 *      Jump to the kernel entry
 *            -- Cort
 */
	.globl	start
start:
	bl	start_
start_:
	mr	r11,r3		/* Save pointer to residual/board data */
	mr      r25,r5          /* Save OFW pointer */
	li	r3,MSR_IP	/* Establish default MSR value */
	mtmsr	r3

/* check if we need to relocate ourselves to the link addr or were we
   loaded there to begin with -- Cort */
	lis	r4,start@h
	ori	r4,r4,start@l
	mflr	r3
	subi	r3,r3,4		/* we get the nip, not the ip of the branch */
	mr	r8,r3
	cmp	0,r3,r4
	bne	1010f
/* compute size of whole image in words.  this should be moved to
 * start_ldr() -- Cort
 */
	lis	r4,start@h
	ori	r4,r4,start@l
	lis	r5,end@h
	ori	r5,r5,end@l
	addi	r5,r5,3		/* round up */
	sub	r5,r5,r4
	srwi	r5,r5,2
	mr	r7,r5
	b	start_ldr
1010:
/* 
 * no matter where we're loaded, move ourselves to -Ttext address
 */
relocate:
	mflr	r3		/* Compute code bias */
	subi	r3,r3,4
	mr	r8,r3
	lis	r4,start@h
	ori	r4,r4,start@l
	lis	r5,end@h
	ori	r5,r5,end@l
	addi	r5,r5,3			/* Round up - just in case */
	sub	r5,r5,r4		/* Compute # longwords to move */
	srwi	r5,r5,2
	mtctr	r5
	mr	r7,r5
	li	r6,0
	subi	r3,r3,4			/* Set up for loop */
	subi	r4,r4,4
00:	lwzu	r5,4(r3)
	stwu	r5,4(r4)
	xor	r6,r6,r5
	bdnz	00b
  	lis	r3,start_ldr@h
	ori	r3,r3,start_ldr@l
	mtlr	r3			/* Easiest way to do an absolute jump */
	blr
start_ldr:
/* Clear all of BSS */
	lis	r3,edata@h
	ori	r3,r3,edata@l
	lis	r4,end@h
	ori	r4,r4,end@l
	subi	r3,r3,4
	subi	r4,r4,4
	li	r0,0
50:	stwu	r0,4(r3)
	cmp	0,r3,r4
	bne	50b
90:	mr	r9,r1			/* Save old stack pointer (in case it matters) */
	lis	r1,.stack@h
	ori	r1,r1,.stack@l
	addi	r1,r1,4096*2
	subi	r1,r1,256
	li	r2,0x000F		/* Mask pointer to 16-byte boundary */
	andc	r1,r1,r2

	/* Speed us up a little.
	*/
	bl	flush_instruction_cache

/* Run loader */
	mr	r3,r8			/* Load point */
	mr	r4,r7			/* Program length */
	mr	r5,r6			/* Checksum */
	mr	r6,r11			/* Residual data */
	mr      r7,r25                  /* OFW interfaces */
	bl	decompress_kernel
	
	/* changed to use r3 (as firmware does) for kernel
	   as ptr to residual -- Cort*/
	lis	r6,cmd_line@h
	ori	r6,r6,cmd_line@l
	lwz	r6, 0(r6)
	subi	r7,r6,1
00:	lbzu	r2,1(r7)
	cmpi	0,r2,0
	bne	00b

	/* r4,r5 have initrd_start, size */
	lis	r2,initrd_start@h
	ori	r2,r2,initrd_start@l
	lwz	r4,0(r2)
	lis	r2,initrd_end@h
	ori	r2,r2,initrd_end@l
	lwz	r5,0(r2)
	
	/* tell kernel we're prep */
	/* 
	 * get start address of kernel code which is stored as a coff
	 * entry.  see boot/head.S -- Cort 
	 */
	li	r9,0x4
	mtlr	r9
	lis	r10,0xdeadc0de@h
	ori	r10,r10,0xdeadc0de@l
	li	r9,0
	stw	r10,0(r9)
/*
 * The Radstone firmware maps PCI memory at 0xc0000000 using BAT2
 * so disable BATs before setting this to avoid a clash
 */
	li      r8,0
	mtspr   DBAT0U,r8
	mtspr   DBAT1U,r8
	mtspr   DBAT2U,r8
	mtspr   DBAT3U,r8
	mtspr   IBAT0U,r8
	mtspr   IBAT1U,r8
	mtspr   IBAT2U,r8
	mtspr   IBAT3U,r8

	blr
hang:
	b	hang	

/*
 * Delay for a number of microseconds
 * -- Use the BUS timer (assumes 66MHz)
 */
	.globl	udelay
udelay:		
	mfspr	r4,PVR
	srwi	r4,r4,16
	cmpi	0,r4,1		/* 601 ? */
	bne	.udelay_not_601
00:	li	r0,86	/* Instructions / microsecond? */
	mtctr	r0
10:	addi	r0,r0,0 /* NOP */
	bdnz	10b
	subic.	r3,r3,1
	bne	00b
	blr

.udelay_not_601:		
	mulli	r4,r3,1000	/* nanoseconds */
	addi	r4,r4,59
	li	r5,60
	divw	r4,r4,r5	/* BUS ticks */
1:	mftbu	r5
	mftb	r6
	mftbu	r7
	cmp	0,r5,r7
	bne	1b		/* Get [synced] base time */
	addc	r9,r6,r4	/* Compute end time */
	addze	r8,r5
2:	mftbu	r5
	cmp	0,r5,r8
	blt	2b
	bgt	3f
	mftb	r6
	cmp	0,r6,r9
	blt	2b
3:	blr		

.globl _get_HID0
_get_HID0:		
	mfspr	r3,HID0
	blr

.globl _put_HID0
_put_HID0:		
	mtspr	HID0,r3
	blr
		
.globl _get_MSR
_get_MSR:		
	mfmsr	r3
	blr
	
.globl _put_MSR
_put_MSR:		
	mtmsr	r3
	blr

/*
 * Flush instruction cache
 * *** I'm really paranoid here!
 */
_GLOBAL(flush_instruction_cache)
	mflr	r5
	bl	flush_data_cache
	mfspr	r3,HID0	/* Caches are controlled by this register */
	li	r4,0
	ori	r4,r4,(HID0_ICE|HID0_ICFI)
	or	r3,r3,r4	/* Need to enable+invalidate to clear */
	mtspr	HID0,r3
	andc	r3,r3,r4
	ori	r3,r3,HID0_ICE	/* Enable cache */
	mtspr	HID0,r3
	mtlr	r5
	blr
	
#define NUM_CACHE_LINES 128*8
#define CACHE_LINE_SIZE 32 
#define cache_flush_buffer 0x1000

/*
 * Flush data cache
 * *** I'm really paranoid here!
 */
_GLOBAL(flush_data_cache)
	lis	r3,cache_flush_buffer@h
	ori	r3,r3,cache_flush_buffer@l
	li	r4,NUM_CACHE_LINES
	mtctr	r4
00:	lwz	r4,0(r3)
	addi	r3,r3,CACHE_LINE_SIZE	/* Next line, please */
	bdnz	00b	
10:	blr
	.comm	.stack,4096*2,4
