/*
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1999 by Helge Deller
 * Copyright 1999 SuSE GmbH (Philipp Rumpf)
 * Copyright 1999 Philipp Rumpf (prumpf@tux.org)
 *
 * Initial Version 04-23-1999 by Helge Deller (helge.deller@ruhr-uni-bochum.de)
 */


#include <asm/offset.h>
#include <asm/psw.h>

#define __ASSEMBLY__
/*********
#include <asm/pdc.h>
*********/
#include <asm/assembly.h>
#include <asm/pgtable.h>


	.level 1.1

	.section	.initcall.init
	.align		4
	.export __initcall_start
__initcall_start:
	.export __initcall_end
__initcall_end:
	.export __setup_start
__setup_start:
	.export __setup_end
__setup_end:

	.text
	.align	4	
	.import init_task_union,data
	.import	$global$		/* forward declaration */
	.import fault_vector_11,code	/* IVA parisc 1.1 32 bit */
	.import fault_vector_20,code	/* IVA parisc 2.0 32 bit */
	.import start_parisc,code	/* then enable VM and go here */

	.export stext
	.export _stext,data		/* Kernel want it this way! */
_stext:
stext:
	.proc
	.callinfo

	/* Make sure sr4-sr7 are set to zero for the kernel address space */

	mtsp		%r0,%sr4
	mtsp		%r0,%sr5
	mtsp		%r0,%sr6
	mtsp		%r0,%sr7

	/* Initialize startup VM. Just map first 8 MB of memory */

	ldil		L%PA(pg0),%r1
	ldo		R%PA(pg0)(%r1),%r1
	ldo		_PAGE_TABLE(%r1),%r3
	ldil		L%PA(swapper_pg_dir),%r4
	ldo		R%PA(swapper_pg_dir)(%r4),%r4
	mtctl		%r4,%cr24	/* Initialize kernel root pointer */
	mtctl		%r4,%cr25	/* Initialize user root pointer */
	stw		%r3,0xc00(%r4)	/* Hardwired 0xc0000000 kernel vaddr start */
	ldo		0x1000(%r3),%r3
	stw		%r3,0xc04(%r4)
	ldo		_PAGE_KERNEL(%r0),%r3 /* Hardwired 0x0 phys addr start */
$pgt_fill_loop:
	stwm		%r3,4(%r1)
	ldo		0x1000(%r3),%r3
	bb,>=		%r3,8,$pgt_fill_loop
	nop

	/* Initialize the global data pointer */
	ldil		L%$global$,%dp
	ldo		R%$global$(%dp),%dp
	
	/* And the stack pointer, physical too */
	ldil		L%init_task_union+TASK_SZ_ALGN,%sp
	ldo		R%init_task_union+TASK_SZ_ALGN(%sp),%sp

	/* we need this to take interruptions directly after the rfi below */
	/* (which we need for PA2.0 boxes) */
	mtctl		%r0, %cr30
	
	/*
	 * Set up our interrupt table.  HPMCs might not work after this! 
	 *
	 * We need to install the correct iva for PA1.1 or PA2.0. The
	 * following short sequence of instructions can determine this
	 * (without being illegal on a PA1.1 machine).
	 */
	
	ldi		32,%r10
	mtctl		%r10,%cr11
	.level 2.0
	mfctl,w		%cr11,%r10
	.level 1.1
	comib,<>,n	0,%r10,$is_pa20
	ldil		L%PA(fault_vector_11),%r10
	b		$install_iva
	ldo		R%PA(fault_vector_11)(%r10),%r10

$is_pa20:
	ldil		L%PA(fault_vector_20),%r10
	ldo		R%PA(fault_vector_20)(%r10),%r10

$install_iva:
	mtctl		%r10,%cr14

	/* Disable (most) interruptions */
	mtsm		%r0			
	
	/* kernel PSW:
	 *  - no interruptions except for HPMC and TOC (which are handled by PDC)
	 *  - Q bit set (IODC / PDC interruptions)
	 *  - big-endian
	 *  - virtually mapped
	 */

	ldil		L%KERNEL_PSW,%r10
	ldo		R%KERNEL_PSW(%r10),%r10
	mtctl		%r10,%ipsw
	
	/* Set the space pointers for the post-RFI world */
	mtctl		%r0,%cr17		/* Clear two-level IIA Space Queue */
	mtctl		%r0,%cr17		/*    effectively setting kernel space. */

	/* And the return address(es) too */
	ldil		L%start_parisc,%r10
	ldo		R%start_parisc(%r10),%r10
	mtctl		%r10,%cr18
	ldo		4(%r10),%r10
	mtctl		%r10,%cr18

	/* Jump to hyperspace */
	rfi
	nop

	.procend

	.data

	.align	4
	.export	$global$,data

	.type	$global$,@object
	.size	$global$,4
$global$:	
	.word 0
