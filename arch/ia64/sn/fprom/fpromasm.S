/* 
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 *   (Code copied from or=ther files)
 * Copyright (C) 1998-2000 Hewlett-Packard Co
 * Copyright (C) 1998-2000 David Mosberger-Tang <davidm@hpl.hp.com>
 *
 * Copyright (C) 2000 Silicon Graphics, Inc.
 * Copyright (C) 2000 by Jack Steiner (steiner@sgi.com)
 */



#define __ASSEMBLY__ 1
#include "asm/processor.h"

/*
 * This file contains additional set up code that is needed to get going on
 * Medusa.  This code should disappear once real hw is available.
 *
 * On entry to this routine, the following register values are assumed:
 *
 *	gr[8]	- BSP cpu
 *	pr[9]	- kernel entry address
 *
 * NOTE:
 *   This FPROM may be loaded/executed at an address different from the
 *   address that it was linked at. The FPROM is linked to run on node 0
 *   at address 0x100000. If the code in loaded into another node, it
 *   must be loaded at offset 0x100000 of the node. In addition, the
 *   FPROM does the following things:
 *		- determine the base address of the node it is loaded on
 *		- add the node base to _gp.
 *		- add the node base to all addresses derived from "movl" 
 *		  instructions. (I couldnt get GPREL addressing to work)
 *		  (maybe newer versions of the tools will support this)
 *		- scan the .got section and add the node base to all
 *		  pointers in this section.
 *		- add the node base to all physical addresses in the
 *		  SAL/PAL/EFI table built by the C code. (This is done
 *		  in the C code - not here)
 *		- add the node base to the TLB entries for vmlinux
 */

#define KERNEL_BASE	0xe000000000000000
#define PAGESIZE_256M	28

/* 
 * ar.k0 gets set to IOPB_PA value, on 460gx chipset it should 
 * be 0x00000ffffc000000, but on snia we use the (inverse swizzled)
 * IOSPEC_BASE value
 */
#define IOPB_PA		0x00000a0000000000 /* inv swizzle IOSPEC_BASE */

#define RR_RID		8



// ====================================================================================	
        .text
        .align 16
	.global _start
	.proc _start
_start:

// Setup psr and rse for system init
	mov		psr.l = r0;;
	srlz.d;;
	invala
	mov		ar.rsc = r0;;
	loadrs
	;;

// Set CALIAS size to zero. We dont use it.
	movl		r24=0x80000a0001000028;;	// BR_PI_CALIAS_SIZE
	st8 		[r24]=r0

// Isolate node number we are running on.
	mov		r6 = ip;;
	shr		r5 = r6,33;;			// r5 = node number
	shl		r6 = r5,33			// r6 = base memory address of node

// Set & relocate gp.
	movl		r1= __gp;;			// Add base memory address
	add 		r1 = r1,r6			// Relocate to boot node

// Lets figure out who we are & put it in the LID register.
// The BR_PI_SELF_CPU_NUM register gives us a value of 0-3.
// This identifies the cpu on the node. 
// Merge the cpu number with the NASID to generate the LID.
	movl		r24=0x80000a0001000020;;	// BR_PI_SELF_CPU_NUM
	ld8 		r25=[r24]			// Fetch PI_SELF
	movl		r27=0x80000a0001600000;;	// Fetch REVID to get local NASID
	ld8 		r27=[r27];;
	extr.u		r27=r27,32,8
	shl 		r26=r25,16;;			// Align local cpu# to lid.eid
	shl 		r27=r27,24;;			// Align NASID to lid.id
	or  		r26=r26,r27;;			// build the LID
	mov 		cr.lid=r26			// Now put in in the LID register
	
	movl		r2=FPSR_DEFAULT;;
	mov 		ar.fpsr=r2
	movl		sp = bootstacke-16;;
	add 		sp = sp,r6			// Relocate to boot node			

// Save the NASID that we are loaded on.
	movl		r2=base_nasid;;			// Save base_nasid for C code
	add 		r2 = r2,r6;;			// Relocate to boot node
  	st8 		[r2]=r5				// Uncond st8 - same on all cpus

// Save the kernel entry address. It is passed in r9 on one of
// the cpus.
	movl		r2=bsp_entry_pc
	cmp.ne		p6,p0=r9,r0;;
	add 		r2 = r2,r6;;			// Relocate to boot node
(p6)  	st8 		[r2]=r9				// Uncond st8 - same on all cpus


// The following can ONLY be done by 1 cpu. Lets set a lock - the
// cpu that gets it does the initilization. The rest just spin waiting
// til initilization is complete.
	movl		r22 = initlock;;
	add		r22 = r22,r6			// Relocate to boot node
	mov		r23 = 1;;
	xchg8		r23 = [r22],r23;;
	cmp.eq 		p6,p0 = 0,r23
(p6)	br.cond.spnt.few init
1:	ld4		r23 = [r22];;
	cmp.eq		p6,p0 = 1,r23
(p6)	br.cond.sptk	1b
	br		initx

// Add base address of node memory to each pointer in the .got section.
init:	movl		r16 = _GLOBAL_OFFSET_TABLE_;;
	add		r16 = r16,r6;;			// Relocate to boot node
1: 	ld8		r17 = [r16];;
	cmp.eq		p6,p7=0,r17
(p6)	br.cond.sptk.few.clr 2f;;
	add		r17 = r17,r6;;			// Relocate to boot node
	st8		[r16] = r17,8
	br		1b
2:
	mov		r23 = 2;;			// All done, release the spinning cpus
	st4		[r22] = r23
initx:

//
//	I/O-port space base address:
//
	movl		r2 = IOPB_PA;;
	mov		ar.k0 = r2


// Now call main & pass it the current LID value.
	alloc 		r0=ar.pfs,0,0,2,0
	mov    		r32=r26
	mov   		r33=r8;;
	br.call.sptk.few rp=fmain
	
// Initialize Region Registers
//
	mov		r10 = r0
	mov		r2 = (13<<2) 
	mov		r3 = r0;;
1:	cmp4.gtu	p6,p7 = 7, r3
	dep		r10 = r3, r10, 61, 3
	dep		r2 = r3, r2, RR_RID, 4;;
(p7)	dep		r2 = 0, r2, 0, 1;;
(p6)	dep		r2 = -1, r2, 0, 1;;
	mov		rr[r10] = r2
	add		r3 = 1, r3;;
	srlz.d;;
	cmp4.gtu	p6,p0 = 8, r3
(p6)	br.cond.sptk.few.clr 1b

//
// Return value indicates if we are the BSP or AP.
// 	   1 = BSP, 0 = AP
	mov             cr.tpr=r0;;
	cmp.eq		p6,p0=r8,r0
(p6)	br.cond.spnt	slave

//
// Initialize the protection key registers with only pkr[0] = valid.
//
// Should be initialized in accordance with the OS.
//
	mov		r2 = 1
	mov		r3 = r0;;
	mov		pkr[r3] = r2;;
	srlz.d;;
	mov		r2 = r0

1:	add		r3 = r3, r0, 1;;		// increment PKR
	cmp.gtu		p6, p0 = 16, r3;;
(p6)	mov		pkr[r3] = r2
(p6)	br.cond.sptk.few.clr 1b

	mov		ar.rnat = r0			// clear RNAT register

//
// Setup system address translation for kernel
//
// Note: The setup of Kernel Virtual address space can be done by the
// C code of the boot loader.
//
//

#define LINUX_PAGE_OFFSET       0xe000000000000000
#define ITIR(key, ps)           ((key<<8) | (ps<<2))
#define ITRGR(ed,ar,ma)         ((ed<<52) | (ar<<9) | (ma<<2) | 0x61)

#define AR_RX                   1                       // RX permission
#define AR_RW                   4                       // RW permission
#define MA_WB                   0                       // WRITEBACK memory attribute

#define TLB_PAGESIZE		28			// Use 256MB pages for now.
	mov		r16=r5

//
//     text section
//
        movl            r2 = LINUX_PAGE_OFFSET;;        // Set up IFA with VPN of linux
        mov             cr.ifa = r2
        movl            r3 = ITIR(0,TLB_PAGESIZE);;     // Set ITIR to default pagesize
        mov             cr.itir = r3

        shl             r4 = r16,33;;                   // physical addr of start of node
        movl            r5 = ITRGR(1,AR_RX,MA_WB);;     // TLB attributes
        or              r10=r4,r5;;

        itr.i           itr[r0] = r10;;                   // Dropin ITR entry
	srlz.i;;

//
//     data section
//
        movl            r2 = LINUX_PAGE_OFFSET;;        // Set up IFA with VPN of linux
        mov             cr.ifa = r2
        movl            r3 = ITIR(0,TLB_PAGESIZE);;     // Set ITIR to default pagesize
        mov             cr.itir = r3

        shl             r4 = r16,33;;                   // physical addr of start of node
        movl            r5 = ITRGR(1,AR_RW,MA_WB);;     // TLB attributes
        or              r10=r4,r5;;

        itr.d           dtr[r0] = r10;;                 // Dropin DTR entry
	srlz.d;;




//
// Turn on address translation, interrupt collection, psr.ed, protection key.
// Interrupts (PSR.i) are still off here.
//

	movl		r3 = (	IA64_PSR_BN | \
				IA64_PSR_AC | \
				IA64_PSR_IT | \
				IA64_PSR_DB | \
				IA64_PSR_DA | \
				IA64_PSR_RT | \
				IA64_PSR_DT | \
				IA64_PSR_IC   \
			     )
	;;
	mov		cr.ipsr = r3

//
// Go to kernel C startup routines
//	Need to do a "rfi" in order set "it" and "ed" bits in the PSR.
//	This is the only way to set them.

	movl		r2=bsp_entry_pc;;
	add 		r2 = r2,r6;;			// Relocate to boot node
	ld8		r2=[r2];;
	mov		cr.iip = r2
	srlz.d;;
	rfi;;
	.endp		_start

// Slave processors come here to spin til they get an interrupt. Then they launch themselves to
// the place ap_entry points. No initialization is necessary - the kernel makes no
// assumptions about state on this entry.
//	Note: should verify that the interrupt we got was really the ap_wakeup
//	      interrupt but this should not be an issue on medusa
slave:
	nop.i		0x8beef				// Medusa - put cpu to sleep til interrupt occurs
	mov		r8=cr.irr0;;			// Check for interrupt pending.
	cmp.eq		p6,p0=r8,r0
(p6)	br.cond.sptk	slave;;

	mov		r8=cr.ivr;;			// Got one. Must read ivr to accept it
	srlz.d;;
	mov		cr.eoi=r0;;			// must write eoi to clear
	movl		r8=ap_entry;;			// now jump to kernel entry
	add 		r8 = r8,r6;;			// Relocate to boot node
	ld8		r9=[r8],8;;
	ld8		r1=[r8]
	mov		b0=r9;;
	br		b0

// Here is the kernel stack used for the fake PROM
	.bss
	.align		16384
bootstack:
	.skip		16384
bootstacke:
initlock:
	data4
