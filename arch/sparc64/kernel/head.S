/* $Id: head.S,v 1.65 2000/05/09 17:40:13 davem Exp $
 * head.S: Initial boot code for the Sparc64 port of Linux.
 *
 * Copyright (C) 1996,1997 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1996 David Sitsky (David.Sitsky@anu.edu.au)
 * Copyright (C) 1997,1998 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 * Copyright (C) 1997 Miguel de Icaza (miguel@nuclecu.unam.mx)
 */

#include <linux/config.h>
#include <linux/version.h>
#include <linux/errno.h>
#include <asm/asm_offsets.h>
#include <asm/asi.h>
#include <asm/pstate.h>
#include <asm/ptrace.h>
#include <asm/spitfire.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/errno.h>
#include <asm/signal.h>
#include <asm/processor.h>
#include <asm/lsu.h>
#include <asm/head.h>
#include <asm/ttable.h>
	
/* This section from from _start to sparc64_boot_end should fit into
 * 0x0000.0000.0040.4000 to 0x0000.0000.0040.8000 and will be sharing space
 * with bootup_user_stack, which is from 0x0000.0000.0040.4000 to
 * 0x0000.0000.0040.6000 and empty_bad_page, which is from
 * 0x0000.0000.0040.6000 to 0x0000.0000.0040.8000. 
 */

	.text
	.globl	start, _start, stext, _stext
_start:
start:
_stext:
stext:
bootup_user_stack:
! 0x0000000000404000
	b	sparc64_boot
	 flushw					/* Flush register file.      */

/* This stuff has to be in sync with SILO and other potential boot loaders
 * Fields should be kept upward compatible and whenever any change is made,
 * HdrS version should be incremented.
 */
        .global root_flags, ram_flags, root_dev
        .global sparc_ramdisk_image, sparc_ramdisk_size
	.globl	silo_args

        .ascii  "HdrS"
        .word   LINUX_VERSION_CODE
        .half   0x0203          /* HdrS version */
root_flags:
        .half   1
root_dev:
        .half   0
ram_flags:
        .half   0
sparc_ramdisk_image:
        .word   0
sparc_ramdisk_size:
        .word   0
        .xword  reboot_command
	.xword	bootstr_len
	.word	_end

	/* We must be careful, 32-bit OpenBOOT will get confused if it
	 * tries to save away a register window to a 64-bit kernel
	 * stack address.  Flush all windows, disable interrupts,
	 * remap if necessary, jump onto kernel trap table, then kernel
	 * stack, or else we die.
	 *
	 * PROM entry point is on %o4
	 */
sparc64_boot:
	/* Typically PROM has already enabled both MMU's and both on-chip
	 * caches, but we do it here anyway just to be paranoid.
	 */
	mov	(LSU_CONTROL_IC|LSU_CONTROL_DC|LSU_CONTROL_IM|LSU_CONTROL_DM), %g1
	stxa	%g1, [%g0] ASI_LSU_CONTROL

	/*
	 * Make sure we are in privileged mode, have address masking,
         * using the ordinary globals and have enabled floating
         * point.
	 *
	 * Again, typically PROM has left %pil at 13 or similar, and
	 * (PSTATE_PRIV | PSTATE_PEF | PSTATE_IE) in %pstate.
         */
	wrpr    %g0, (PSTATE_PRIV|PSTATE_PEF|PSTATE_IE), %pstate
	wr	%g0, 0, %fprs

create_mappings:
	/* %g5 holds the tlb data */
        sethi   %uhi(_PAGE_VALID | _PAGE_SZ4MB), %g5
        sllx    %g5, 32, %g5
        or      %g5, (_PAGE_CP | _PAGE_CV | _PAGE_P | _PAGE_L | _PAGE_W | _PAGE_G), %g5

	/* Base of physical memory cannot reliably be assumed to be
	 * at 0x0!  Figure out where it happens to be. -DaveM
	 */

	/* Put PADDR tlb data mask into %g3. */
	sethi	%uhi(_PAGE_PADDR), %g3
	or	%g3, %ulo(_PAGE_PADDR), %g3
	sllx	%g3, 32, %g3
	sethi	%hi(_PAGE_PADDR), %g7
	or	%g7, %lo(_PAGE_PADDR), %g7
	or	%g3, %g7, %g3

	/* Walk through entire ITLB, looking for entry which maps
	 * our %pc currently, stick PADDR from there into %g5 tlb data.
	 */
	clr	%l0			/* TLB entry walker. */
	set	0x1fff, %l2		/* Page mask. */
	rd	%pc, %l3
	andn	%l3, %l2, %g2		/* vaddr comparator */
1:
	/* Yes, the nops seem to be necessary for now, don't ask me why. -DaveM */
	ldxa	[%l0] ASI_ITLB_TAG_READ, %g1
	nop
	nop
	nop
	andn	%g1, %l2, %g1		/* Get vaddr */
	cmp	%g1, %g2
	be,a,pn	%xcc, got_tlbentry
	 ldxa	[%l0] ASI_ITLB_DATA_ACCESS, %g1
	cmp	%l0, (63 << 3)
	blu,pt	%xcc, 1b
	 add	%l0, (1 << 3), %l0

got_tlbentry:
	/* Nops here again, perhaps Cheetah/Blackbird are better behaved... */
	nop
	nop
	nop
	and	%g1, %g3, %g1		/* Mask to just get paddr bits.       */
	sub	%g1, %g2, %g1		/* Get rid of %pc offset to get base. */

	/* NOTE: We hold on to %g1 paddr base as we need it below to lock
	 * NOTE: the PROM cif code into the TLB.
	 */

	or	%g5, %g1, %g5		/* Or it into TAG being built.        */

	clr	%l0			/* TLB entry walker. */
	sethi	%hi(KERNBASE), %g3	/* 4M lower limit */
	sethi	%hi(KERNBASE<<1), %g7	/* 8M upper limit */
	mov	TLB_TAG_ACCESS, %l7
1:
	/* Yes, the nops seem to be necessary for now, don't ask me why. -DaveM */
	ldxa	[%l0] ASI_ITLB_TAG_READ, %g1
	nop
	nop
	nop
	andn	%g1, %l2, %g1		/* Get vaddr */
	cmp	%g1, %g3
	blu,pn	%xcc, 2f
	 cmp	%g1, %g7
	bgeu,pn	%xcc, 2f
	 nop
	stxa	%g0, [%l7] ASI_IMMU
	stxa	%g0, [%l0] ASI_ITLB_DATA_ACCESS
2:
	cmp	%l0, (63 << 3)
	blu,pt	%xcc, 1b
	 add	%l0, (1 << 3), %l0

	nop; nop; nop

	clr	%l0			/* TLB entry walker. */
1:
	/* Yes, the nops seem to be necessary for now, don't ask me why. -DaveM */
	ldxa	[%l0] ASI_DTLB_TAG_READ, %g1
	nop
	nop
	nop
	andn	%g1, %l2, %g1		/* Get vaddr */
	cmp	%g1, %g3
	blu,pn	%xcc, 2f
	 cmp	%g1, %g7
	bgeu,pn	%xcc, 2f
	 nop
	stxa	%g0, [%l7] ASI_DMMU
	stxa	%g0, [%l0] ASI_DTLB_DATA_ACCESS
2:
	cmp	%l0, (63 << 3)
	blu,pt	%xcc, 1b
	 add	%l0, (1 << 3), %l0

	nop; nop; nop


	/* PROM never puts any TLB entries into the MMU with the lock bit
	 * set.  So we gladly use tlb entry 63 for KERNBASE.
	 */

	sethi	%hi(KERNBASE), %g3
	mov	(63 << 3), %g7
	stxa	%g3, [%l7] ASI_DMMU		/* KERNBASE into TLB TAG	*/
	stxa	%g5, [%g7] ASI_DTLB_DATA_ACCESS	/* TTE into TLB DATA		*/
	membar	#Sync
	stxa	%g3, [%l7] ASI_IMMU		/* KERNBASE into TLB TAG	*/
	stxa	%g5, [%g7] ASI_ITLB_DATA_ACCESS	/* TTE into TLB DATA		*/
	membar	#Sync
	flush	%g3
	membar	#Sync
	ba,pt	%xcc, 1f
	 nop
1:
	set	sun4u_init, %g2
	jmpl    %g2 + %g0, %g0
	 nop

sun4u_init:
	/* Set ctx 0 */
	mov	PRIMARY_CONTEXT, %g7
	stxa	%g0, [%g7] ASI_DMMU
	membar	#Sync

	mov	SECONDARY_CONTEXT, %g7
	stxa	%g0, [%g7] ASI_DMMU
	membar	#Sync

	sethi	%uhi(PAGE_OFFSET), %g4
	sllx	%g4, 32, %g4

	/* We are now safely (we hope) in Nucleus context (0), rewrite
	 * the KERNBASE TTE's so they no longer have the global bit set.
	 * Don't forget to setup TAG_ACCESS first 8-)
	 */
	mov	TLB_TAG_ACCESS, %g2
	stxa	%g3, [%g2] ASI_IMMU
	stxa	%g3, [%g2] ASI_DMMU

	mov	(63 << 3), %g7
	ldxa	[%g7] ASI_ITLB_DATA_ACCESS, %g1
	andn	%g1, (_PAGE_G), %g1
	stxa	%g1, [%g7] ASI_ITLB_DATA_ACCESS
	membar	#Sync

	ldxa	[%g7] ASI_DTLB_DATA_ACCESS, %g1
	andn	%g1, (_PAGE_G), %g1
	stxa	%g1, [%g7] ASI_DTLB_DATA_ACCESS
	membar	#Sync

	/* Kill instruction prefetch queues. */
	flush	%g3
	membar	#Sync

	sethi	%hi(init_task_union), %g6
	or	%g6, %lo(init_task_union), %g6
	mov	%sp, %l6
	mov	%o4, %l7

#if 0	/* We don't do it like this anymore, but for historical hack value
	 * I leave this snippet here to show how crazy we can be sometimes. 8-)
	 */

	/* Setup "Linux Current Register", thanks Sun 8-) */
	wr	%g0, 0x1, %pcr

	/* Blackbird errata workaround.  See commentary in
	 * smp.c:smp_percpu_timer_interrupt() for more
	 * information.
	 */
	ba,pt	%xcc, 99f
	 nop
	.align	64
99:	wr	%g6, %g0, %pic
	rd	%pic, %g0
#endif

	wr	%g0, ASI_P, %asi
	mov	1, %g5
	sllx	%g5, (PAGE_SHIFT + 1), %g5
	sub	%g5, (REGWIN_SZ + STACK_BIAS), %g5
	add	%g6, %g5, %sp
	mov	0, %fp

	wrpr	%g0, 0, %wstate
	wrpr	%g0, 0x0, %tl

	/* Clear the bss */
	sethi	%hi(8191), %l2
	or	%l2, %lo(8191), %l2
	sethi	%hi(__bss_start), %l0
	or	%l0, %lo(__bss_start), %l0
	sethi	%hi(_end), %l1
	or	%l1, %lo(_end), %l1
	add	%l1, %l2, %l1
	andn	%l1, %l2, %l1
	add	%l2, 1, %l2
	add	%l0, %g0, %o0
1:
	mov	%l2, %o1
	call	__bzero
	 add	%l0, %l2, %l0
	cmp	%l0, %l1
	blu,pt	%xcc, 1b
	 add	%l0, %g0, %o0

	/* Now clear empty_zero_page */
	mov	%l2, %o1
	call	__bzero
	 mov	%g3, %o0

	mov	%l6, %o1			! OpenPROM stack
	call	prom_init
	 mov	%l7, %o0			! OpenPROM cif handler

	/* Off we go.... */
	call	start_kernel
	 nop
	/* Not reached... */

/* IMPORTANT NOTE: Whenever making changes here, check
 * trampoline.S as well. -jj */
	.globl	setup_tba
setup_tba:	/* i0 = is_starfire */
	save	%sp, -160, %sp

	rdpr	%tba, %g7
	sethi	%hi(prom_tba), %o1
	or	%o1, %lo(prom_tba), %o1
	stx	%g7, [%o1]

	/* Setup "Linux" globals 8-) */
	rdpr	%pstate, %o1
	mov	%g6, %o2
	wrpr	%o1, (PSTATE_AG|PSTATE_IE), %pstate
	sethi	%hi(sparc64_ttable_tl0), %g5
	wrpr	%g5, %tba
	mov	%o2, %g6

	/* Set up MMU globals */
	wrpr	%o1, (PSTATE_MG|PSTATE_IE), %pstate

	/* Set fixed globals used by dTLB miss handler. */
#define KERN_HIGHBITS		((_PAGE_VALID | _PAGE_SZ4MB) ^ 0xfffff80000000000)
#define KERN_LOWBITS		(_PAGE_CP | _PAGE_CV | _PAGE_P | _PAGE_W)
#ifdef THIS_IS_CHEETAH
#error Dave, make sure you took care of other issues in rest of sparc64 code...
#define VPTE_BASE		0xffe0000000000000
#else /* Spitfire/Blackbird */
#define VPTE_BASE		0xfffffffe00000000
#endif
	mov	TSB_REG, %g1
	stxa	%g0, [%g1] ASI_DMMU
	membar	#Sync
	mov	TLB_SFSR, %g1
	sethi	%uhi(KERN_HIGHBITS), %g2
	or	%g2, %ulo(KERN_HIGHBITS), %g2
	sllx	%g2, 32, %g2
	or	%g2, KERN_LOWBITS, %g2
	sethi	%uhi(VPTE_BASE), %g3
	or	%g3, %ulo(VPTE_BASE), %g3
	sllx	%g3, 32, %g3
	clr	%g7
#undef KERN_HIGHBITS
#undef KERN_LOWBITS
#undef VPTE_BASE

	/* Setup Interrupt globals */
	wrpr	%o1, (PSTATE_IG|PSTATE_IE), %pstate
#ifndef CONFIG_SMP
	sethi	%hi(__up_workvec), %g5
	or	%g5, %lo(__up_workvec), %g6
#else
	/* By definition of where we are, this is boot_cpu. */
	sethi	%hi(cpu_data), %g5
	or	%g5, %lo(cpu_data), %g5

	brz,pt	%i0, not_starfire
	 sethi	%hi(0x1fff4000), %g1
	or	%g1, %lo(0x1fff4000), %g1
	sllx	%g1, 12, %g1
	or	%g1, 0xd0, %g1
	lduwa	[%g1] ASI_PHYS_BYPASS_EC_E, %g1
	b,pt	%xcc, set_worklist
	 nop

not_starfire:
	ldxa	[%g0] ASI_UPA_CONFIG, %g1
	srlx	%g1, 17, %g1
	and	%g1, 0x1f, %g1

	/* In theory this is: &(cpu_data[boot_cpu_id].irq_worklists[0]) */
set_worklist:
	sllx	%g1, 7, %g1
	add	%g5, %g1, %g5
	add	%g5, 64, %g6
#endif

	/* Kill PROM timer */
	wr	%g0, 0, %tick_cmpr

	/* Ok, we're done setting up all the state our trap mechanims needs,
	 * now get back into normal globals and let the PROM know what is up.
	 */
	wrpr	%g0, %g0, %wstate
	wrpr	%o1, PSTATE_IE, %pstate

	sethi	%hi(sparc64_ttable_tl0), %g5
	call	prom_set_trap_table
	 mov	%g5, %o0

	rdpr	%pstate, %o1
	or	%o1, PSTATE_IE, %o1
	wrpr	%o1, 0, %pstate

	ret
	 restore

sparc64_boot_end:
	.skip	0x2000 + _start - sparc64_boot_end
bootup_user_stack_end:

	.globl	empty_bad_page
empty_bad_page:
	.skip	0x2000

#ifdef CONFIG_SBUS
/* This is just a hack to fool make depend config.h discovering
   strategy: As the .S files below need config.h, but
   make depend does not find it for them, we include config.h
   in head.S */
#endif

! 0x0000000000408000

#include "ttable.S"
#include "systbls.S"

	.align	1024
	.globl	swapper_pg_dir
swapper_pg_dir:
	.word	0

#include "etrap.S"
#include "rtrap.S"
#include "winfixup.S"
#include "entry.S"

	/* This is just anal retentiveness on my part... */
	.align	16384

	.data
	.align	8
	.globl	prom_tba
prom_tba:	.xword	0
	.section	".fixup",#alloc,#execinstr
	.globl	__ret_efault
__ret_efault:
	ret
	 restore %g0, -EFAULT, %o0

