#include <linux/config.h>
#include "../kernel/ppc_defs.h"
#include "../kernel/ppc_asm.tmpl"
#include <asm/processor.h>
#include <asm/cache.h>

	.text

/*
 * $Id: head.S,v 1.6 1999/09/15 00:02:25 dmalek Exp $
 *	
 * This code is loaded by the ROM loader at some arbitrary location.
 * Move it to high memory so that it can load the kernel at 0x0000.
 *
 * This is a three step process that will also work when booting from
 * a Flash PROM normally located in high memory.
 *
 * First, the entire image is loaded into some high memory address.
 * This is usually at or above 0x02000000.  This is done by a network
 * boot function supported by the board or a debugger over BDM port.
 *
 * Second, the start up function here will relocate the decompress
 * function to run at the link address of 0x01000000.
 *
 * Last, the decompression function will reloate the initrd, zImage, and
 * the residual data to locations under 8 Meg.  This is necessary because
 * the embedded kernel start up uses 8 Meg translations to access physical
 * space before the MMU is enabled.  Finally, the zImage is uncompressed
 * to location 0 and we jump to it.
 *
 * On the MBX,
 *              R1 - Stack pointer at a high memory address.
 *              R3 - Pointer to Board Information Block.
 *              R4 - Pointer to argument string.
 *              Interrupts masked, cache and MMU disabled.
 *
 *		...and the first and second functions listed above are
 *			done for us (it knows ELF images).
 *
 * For other embedded boards we build the Board Information Block.
 */

	.globl	start
start:
	bl	start_
start_:
#ifndef CONFIG_MBX
	lis	r11, local_bd_info@h
	ori	r11, r11, local_bd_info@l
#else
	mr	r11, r3
#endif

	mfmsr	r3		/* Turn off interrupts  */
	li	r4,0
	ori	r4,r4,MSR_EE
	andc	r3,r3,r4
	mtmsr	r3

/* check if we need to relocate ourselves to the link addr or were we
   loaded there to begin with -- Cort */
	lis	r4,start@h
	ori	r4,r4,start@l
	mflr	r3
	subi	r3,r3,4		/* we get the nip, not the ip of the branch */
	mr	r8,r3
#if 0
	cmp	0,r3,r4
	beq	start_ldr	/* Branch if loaded OK */
#endif

/* 
 * no matter where we're loaded, move ourselves to -Ttext address
 * This computes the sizes we need to determine other things.
 */
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
/* Most 8xx boards don't boot up with the I-cache enabled.  Do that
 * now because the decompress runs much faster that way.
 */
	lis	r3, IDC_INVALL@h
	mtspr	IC_CST, r3
	lis	r3, IDC_ENABLE@h
	mtspr	IC_CST, r3

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

	lis	r1,.stack@h
	ori	r1,r1,.stack@l
	addi	r1,r1,4096*2
	subi	r1,r1,256
	li	r2,0x000F		/* Mask pointer to 16-byte boundary */
	andc	r1,r1,r2

	/* Perform configuration of the various boards.  This is done
	 * by reading some configuration data from EEPROM and building
	 * the board information structure.
	 */
	mr	r3, r11
        mr      r21, r11
	mr	r22, r8
	mr	r23, r7
	mr	r24, r6

#if defined(CONFIG_RPXLITE) || defined(CONFIG_RPXCLASSIC)
	bl	rpx_cfg
	mr	r3, r21
#endif
#ifdef CONFIG_BSEIP
	bl	bseip_cfg
	mr	r3, r21
#endif
	bl      serial_init		/* Init MBX serial port */

	mr      r11, r21
	mr	r8, r22
	mr	r7, r23
	mr	r6, r24

#ifdef CONFIG_MBX
	lis	r18, 0xfa200000@h	/* Disable Ethernet SCC */
	li	r0, 0
	stw	r0, 0x0a00(r18)

	/* On the MBX (or anything that will TFTP load an ELF image),
	 * we have to find the intermediate address.  The ELF loader
	 * only moves the Linux boostrap/decompress, not the zImage.
	 */
#define ILAP_ADDRESS    0xfa000020
        lis     r8, ILAP_ADDRESS@h
        lwz     r8, ILAP_ADDRESS@l(r8)
        addis   r8, r8, 1               /* Add 64K */
#endif

	mr	r3,r8			/* Load point */
	mr	r4,r7			/* Program length */
	mr	r5,r6			/* Checksum */
	mr	r6,r11			/* Residual data */
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
	
	/* The world starts from the beginning.
	*/
	li	r9,0x0
	mtlr	r9

	/* Invalidate the instruction cache because we just copied a
	 * bunch of kernel instructions.
	 */
	lis	r9, IDC_INVALL@h
	mtspr	IC_CST, r9

	blr
hang:
	b	hang	

/*
 * Delay for a number of microseconds
 * -- Use the BUS timer (assumes 66MHz)
 */
	.globl	udelay
udelay:		
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

.globl _get_MSR
_get_MSR:		
	mfmsr	r3
	blr
	
.globl _put_MSR
_put_MSR:		
	mtmsr	r3
	blr

	.comm	.stack,4096*2,4
#ifndef CONFIG_MBX
local_bd_info:
	.long	0
	.long	0x01000000
	.long	64
	.long	64
	.long	0
	.long	0
	.long	0
#endif
