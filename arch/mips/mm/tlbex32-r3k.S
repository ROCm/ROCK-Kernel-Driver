/*
 * TLB exception handling code for R2000/R3000.
 *
 * Copyright (C) 1994, 1995, 1996 by Ralf Baechle and Andreas Busse
 *
 * Multi-CPU abstraction reworking:
 * Copyright (C) 1996 David S. Miller (dm@engr.sgi.com)
 *
 * Further modifications to make this work:
 * Copyright (c) 1998 Harald Koerfgen
 * Copyright (c) 1998, 1999 Gleb Raiko & Vladimir Roganov
 * Copyright (c) 2001 Ralf Baechle
 * Copyright (c) 2001 MIPS Technologies, Inc.
 */
#include <linux/init.h>
#include <asm/asm.h>
#include <asm/cachectl.h>
#include <asm/fpregdef.h>
#include <asm/mipsregs.h>
#include <asm/page.h>
#include <asm/pgtable-bits.h>
#include <asm/regdef.h>
#include <asm/stackframe.h>

#define TLB_OPTIMIZE /* If you are paranoid, disable this. */

	/* ABUSE of CPP macros 101. */

	/* After this macro runs, the pte faulted on is
	 * in register PTE, a ptr into the table in which
	 * the pte belongs is in PTR.
	 */
#define LOAD_PTE(pte, ptr) \
	mfc0	pte, CP0_BADVADDR; \
	lw	ptr, pgd_current; \
	srl	pte, pte, 22; \
	sll	pte, pte, 2; \
	addu	ptr, ptr, pte; \
	mfc0	pte, CP0_CONTEXT; \
	lw	ptr, (ptr); \
	andi	pte, pte, 0xffc; \
	addu	ptr, ptr, pte; \
	lw	pte, (ptr); \
	nop;

	/* This places the even/odd pte pair in the page
	 * table at PTR into ENTRYLO0 and ENTRYLO1 using
	 * TMP as a scratch register.
	 */
#define PTE_RELOAD(ptr) \
	lw	ptr, (ptr)	; \
	nop			; \
	mtc0	ptr, CP0_ENTRYLO0; \
	nop;

#define DO_FAULT(write) \
	.set	noat; \
	.set	macro; \
	SAVE_ALL; \
	mfc0	a2, CP0_BADVADDR; \
	KMODE; \
	.set	at; \
	move	a0, sp; \
	jal	do_page_fault; \
	 li	a1, write; \
	j	ret_from_exception; \
	 nop; \
	.set	noat; \
	.set	nomacro;

	/* Check is PTE is present, if not then jump to LABEL.
	 * PTR points to the page table where this PTE is located,
	 * when the macro is done executing PTE will be restored
	 * with it's original value.
	 */
#define PTE_PRESENT(pte, ptr, label) \
	andi	pte, pte, (_PAGE_PRESENT | _PAGE_READ); \
	xori	pte, pte, (_PAGE_PRESENT | _PAGE_READ); \
	bnez	pte, label; \
	.set	push;       \
	.set	reorder;    \
	 lw	pte, (ptr); \
	.set	pop;

	/* Make PTE valid, store result in PTR. */
#define PTE_MAKEVALID(pte, ptr) \
	ori	pte, pte, (_PAGE_VALID | _PAGE_ACCESSED); \
	sw	pte, (ptr);

	/* Check if PTE can be written to, if not branch to LABEL.
	 * Regardless restore PTE with value from PTR when done.
	 */
#define PTE_WRITABLE(pte, ptr, label) \
	andi	pte, pte, (_PAGE_PRESENT | _PAGE_WRITE); \
	xori	pte, pte, (_PAGE_PRESENT | _PAGE_WRITE); \
	bnez	pte, label; \
	.set    push;       \
	.set    reorder;    \
	lw      pte, (ptr); \
	.set    pop;


	/* Make PTE writable, update software status bits as well,
	 * then store at PTR.
	 */
#define PTE_MAKEWRITE(pte, ptr) \
	ori	pte, pte, (_PAGE_ACCESSED | _PAGE_MODIFIED | \
			   _PAGE_VALID | _PAGE_DIRTY); \
	sw	pte, (ptr);

/*
 * The index register may have the probe fail bit set,
 * because we would trap on access kseg2, i.e. without refill.
 */
#define TLB_WRITE(reg) \
	mfc0	reg, CP0_INDEX; \
	nop; \
	bltz    reg, 1f; \
	 nop; \
	tlbwi; \
	j	2f; \
	 nop; \
1:	tlbwr; \
2:

#define RET(reg) \
	mfc0	reg, CP0_EPC; \
	nop; \
	jr	reg; \
	 rfe

	.set	noreorder

	.align	5
NESTED(handle_tlbl, PT_SIZE, sp)
	.set	noat

#ifdef TLB_OPTIMIZE
	/* Test present bit in entry. */
	LOAD_PTE(k0, k1)
        tlbp
        PTE_PRESENT(k0, k1, nopage_tlbl)
        PTE_MAKEVALID(k0, k1)
        PTE_RELOAD(k1)
	TLB_WRITE(k0)
	RET(k0)
nopage_tlbl:
#endif

	DO_FAULT(0)
END(handle_tlbl)

NESTED(handle_tlbs, PT_SIZE, sp)
	.set	noat

#ifdef TLB_OPTIMIZE
	LOAD_PTE(k0, k1)
	tlbp                            # find faulting entry
	PTE_WRITABLE(k0, k1, nopage_tlbs)
	PTE_MAKEWRITE(k0, k1)
	PTE_RELOAD(k1)
	TLB_WRITE(k0)
	RET(k0)
nopage_tlbs:
#endif

	DO_FAULT(1)
END(handle_tlbs)

	.align	5
NESTED(handle_mod, PT_SIZE, sp)
	.set	noat
#ifdef TLB_OPTIMIZE
	LOAD_PTE(k0, k1)
	tlbp					# find faulting entry
	andi	k0, k0, _PAGE_WRITE
	beqz	k0, nowrite_mod
	.set	push
	.set    reorder
	lw	k0, (k1)
	.set    pop

	/* Present and writable bits set, set accessed and dirty bits. */
	PTE_MAKEWRITE(k0, k1)

	/* Now reload the entry into the tlb. */
	PTE_RELOAD(k1)
	tlbwi
	RET(k0)
#endif

nowrite_mod:
	DO_FAULT(1)
END(handle_mod)
