/*
 *  linux/arch/arm/mm/fault-armv.c
 *
 *  Copyright (C) 1995  Linus Torvalds
 *  Modifications for ARM processor (c) 1995-2001 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/config.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/ptrace.h>
#include <linux/mman.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/proc_fs.h>
#include <linux/bitops.h>
#include <linux/init.h>

#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/pgalloc.h>
#include <asm/pgtable.h>
#include <asm/unaligned.h>

extern void die_if_kernel(const char *str, struct pt_regs *regs, int err);
extern void show_pte(struct mm_struct *mm, unsigned long addr);
extern int do_page_fault(unsigned long addr, int error_code,
			 struct pt_regs *regs);
extern int do_translation_fault(unsigned long addr, int error_code,
				struct pt_regs *regs);
extern void do_bad_area(struct task_struct *tsk, struct mm_struct *mm,
			unsigned long addr, int error_code,
			struct pt_regs *regs);

#ifdef CONFIG_ALIGNMENT_TRAP
/*
 * 32-bit misaligned trap handler (c) 1998 San Mehat (CCC) -July 1998
 * /proc/sys/debug/alignment, modified and integrated into
 * Linux 2.1 by Russell King
 *
 * Speed optimisations and better fault handling by Russell King.
 *
 * *** NOTE ***
 * This code is not portable to processors with late data abort handling.
 */
#define CODING_BITS(i)	(i & 0x0e000000)

#define LDST_I_BIT(i)	(i & (1 << 26))		/* Immediate constant	*/
#define LDST_P_BIT(i)	(i & (1 << 24))		/* Preindex		*/
#define LDST_U_BIT(i)	(i & (1 << 23))		/* Add offset		*/
#define LDST_W_BIT(i)	(i & (1 << 21))		/* Writeback		*/
#define LDST_L_BIT(i)	(i & (1 << 20))		/* Load			*/

#define LDST_P_EQ_U(i)	((((i) ^ ((i) >> 1)) & (1 << 23)) == 0)

#define LDSTH_I_BIT(i)	(i & (1 << 22))		/* half-word immed	*/
#define LDM_S_BIT(i)	(i & (1 << 22))		/* write CPSR from SPSR	*/

#define RN_BITS(i)	((i >> 16) & 15)	/* Rn			*/
#define RD_BITS(i)	((i >> 12) & 15)	/* Rd			*/
#define RM_BITS(i)	(i & 15)		/* Rm			*/

#define REGMASK_BITS(i)	(i & 0xffff)
#define OFFSET_BITS(i)	(i & 0x0fff)

#define IS_SHIFT(i)	(i & 0x0ff0)
#define SHIFT_BITS(i)	((i >> 7) & 0x1f)
#define SHIFT_TYPE(i)	(i & 0x60)
#define SHIFT_LSL	0x00
#define SHIFT_LSR	0x20
#define SHIFT_ASR	0x40
#define SHIFT_RORRRX	0x60

static unsigned long ai_user;
static unsigned long ai_sys;
static unsigned long ai_skipped;
static unsigned long ai_half;
static unsigned long ai_word;
static unsigned long ai_multi;

#ifdef CONFIG_SYSCTL
static int proc_alignment_read(char *page, char **start, off_t off,
			       int count, int *eof, void *data)
{
	char *p = page;
	int len;

	p += sprintf(p, "User:\t\t%li\n", ai_user);
	p += sprintf(p, "System:\t\t%li\n", ai_sys);
	p += sprintf(p, "Skipped:\t%li\n", ai_skipped);
	p += sprintf(p, "Half:\t\t%li\n", ai_half);
	p += sprintf(p, "Word:\t\t%li\n", ai_word);
	p += sprintf(p, "Multi:\t\t%li\n", ai_multi);

	len = (p - page) - off;
	if (len < 0)
		len = 0;

	*eof = (len <= count) ? 1 : 0;
	*start = page + off;

	return len;
}

/*
 * This needs to be done after sysctl_init, otherwise sys/
 * will be overwritten.
 */
static int __init alignment_init(void)
{
	create_proc_read_entry("sys/debug/alignment", 0, NULL,
				proc_alignment_read, NULL);
	return 0;
}

__initcall(alignment_init);
#endif /* CONFIG_SYSCTL */

union offset_union {
	unsigned long un;
	  signed long sn;
};

#define TYPE_ERROR	0
#define TYPE_FAULT	1
#define TYPE_LDST	2
#define TYPE_DONE	3

#define get8_unaligned_check(val,addr,err)		\
	__asm__(					\
	"1:	ldrb	%1, [%2], #1\n"			\
	"2:\n"						\
	"	.section .fixup,\"ax\"\n"		\
	"	.align	2\n"				\
	"3:	mov	%0, #1\n"			\
	"	b	2b\n"				\
	"	.previous\n"				\
	"	.section __ex_table,\"a\"\n"		\
	"	.align	3\n"				\
	"	.long	1b, 3b\n"			\
	"	.previous\n"				\
	: "=r" (err), "=&r" (val), "=r" (addr)		\
	: "0" (err), "2" (addr))

#define get8t_unaligned_check(val,addr,err)		\
	__asm__(					\
	"1:	ldrbt	%1, [%2], #1\n"			\
	"2:\n"						\
	"	.section .fixup,\"ax\"\n"		\
	"	.align	2\n"				\
	"3:	mov	%0, #1\n"			\
	"	b	2b\n"				\
	"	.previous\n"				\
	"	.section __ex_table,\"a\"\n"		\
	"	.align	3\n"				\
	"	.long	1b, 3b\n"			\
	"	.previous\n"				\
	: "=r" (err), "=&r" (val), "=r" (addr)		\
	: "0" (err), "2" (addr))

#define get16_unaligned_check(val,addr)				\
	do {							\
		unsigned int err = 0, v, a = addr;		\
		get8_unaligned_check(val,a,err);		\
		get8_unaligned_check(v,a,err);			\
		val |= v << 8;					\
		if (err)					\
			goto fault;				\
	} while (0)

#define put16_unaligned_check(val,addr)				\
	do {							\
		unsigned int err = 0, v = val, a = addr;	\
		__asm__(					\
		"1:	strb	%1, [%2], #1\n"			\
		"	mov	%1, %1, lsr #8\n"		\
		"2:	strb	%1, [%2]\n"			\
		"3:\n"						\
		"	.section .fixup,\"ax\"\n"		\
		"	.align	2\n"				\
		"4:	mov	%0, #1\n"			\
		"	b	3b\n"				\
		"	.previous\n"				\
		"	.section __ex_table,\"a\"\n"		\
		"	.align	3\n"				\
		"	.long	1b, 4b\n"			\
		"	.long	2b, 4b\n"			\
		"	.previous\n"				\
		: "=r" (err), "=&r" (v), "=&r" (a)		\
		: "0" (err), "1" (v), "2" (a));			\
		if (err)					\
			goto fault;				\
	} while (0)

#define __put32_unaligned_check(ins,val,addr)			\
	do {							\
		unsigned int err = 0, v = val, a = addr;	\
		__asm__(					\
		"1:	"ins"	%1, [%2], #1\n"			\
		"	mov	%1, %1, lsr #8\n"		\
		"2:	"ins"	%1, [%2], #1\n"			\
		"	mov	%1, %1, lsr #8\n"		\
		"3:	"ins"	%1, [%2], #1\n"			\
		"	mov	%1, %1, lsr #8\n"		\
		"4:	"ins"	%1, [%2]\n"			\
		"5:\n"						\
		"	.section .fixup,\"ax\"\n"		\
		"	.align	2\n"				\
		"6:	mov	%0, #1\n"			\
		"	b	5b\n"				\
		"	.previous\n"				\
		"	.section __ex_table,\"a\"\n"		\
		"	.align	3\n"				\
		"	.long	1b, 6b\n"			\
		"	.long	2b, 6b\n"			\
		"	.long	3b, 6b\n"			\
		"	.long	4b, 6b\n"			\
		"	.previous\n"				\
		: "=r" (err), "=&r" (v), "=&r" (a)		\
		: "0" (err), "1" (v), "2" (a));			\
		if (err)					\
			goto fault;				\
	} while (0)

#define get32_unaligned_check(val,addr)				\
	do {							\
		unsigned int err = 0, v, a = addr;		\
		get8_unaligned_check(val,a,err);		\
		get8_unaligned_check(v,a,err);			\
		val |= v << 8;					\
		get8_unaligned_check(v,a,err);			\
		val |= v << 16;					\
		get8_unaligned_check(v,a,err);			\
		val |= v << 24;					\
		if (err)					\
			goto fault;				\
	} while (0)

#define put32_unaligned_check(val,addr)	 \
	__put32_unaligned_check("strb", val, addr)

#define get32t_unaligned_check(val,addr)			\
	do {							\
		unsigned int err = 0, v, a = addr;		\
		get8t_unaligned_check(val,a,err);		\
		get8t_unaligned_check(v,a,err);			\
		val |= v << 8;					\
		get8t_unaligned_check(v,a,err);			\
		val |= v << 16;					\
		get8t_unaligned_check(v,a,err);			\
		val |= v << 24;					\
		if (err)					\
			goto fault;				\
	} while (0)

#define put32t_unaligned_check(val,addr) \
	__put32_unaligned_check("strbt", val, addr)

static void
do_alignment_finish_ldst(unsigned long addr, unsigned long instr, struct pt_regs *regs, union offset_union offset)
{
	if (!LDST_U_BIT(instr))
		offset.un = -offset.un;

	if (!LDST_P_BIT(instr))
		addr += offset.un;

	if (!LDST_P_BIT(instr) || LDST_W_BIT(instr))
		regs->uregs[RN_BITS(instr)] = addr;
}

static int
do_alignment_ldrhstrh(unsigned long addr, unsigned long instr, struct pt_regs *regs)
{
	unsigned int rd = RD_BITS(instr);

	if ((instr & 0x01f00ff0) == 0x01000090)
		goto swp;

	if ((instr & 0x90) != 0x90 || (instr & 0x60) == 0)
		goto bad;

	ai_half += 1;

	if (LDST_L_BIT(instr)) {
		unsigned long val;
		get16_unaligned_check(val, addr);

		/* signed half-word? */
		if (instr & 0x40)
			val = (signed long)((signed short) val);

		regs->uregs[rd] = val;
	} else
		put16_unaligned_check(regs->uregs[rd], addr);

	return TYPE_LDST;

swp:
	printk(KERN_ERR "Alignment trap: not handling swp instruction\n");
bad:
	return TYPE_ERROR;

fault:
	return TYPE_FAULT;
}

static int
do_alignment_ldrstr(unsigned long addr, unsigned long instr, struct pt_regs *regs)
{
	unsigned int rd = RD_BITS(instr);

	ai_word += 1;

	if (!LDST_P_BIT(instr) && LDST_W_BIT(instr))
		goto trans;

	if (LDST_L_BIT(instr))
		get32_unaligned_check(regs->uregs[rd], addr);
	else
		put32_unaligned_check(regs->uregs[rd], addr);
	return TYPE_LDST;

trans:
	if (LDST_L_BIT(instr))
		get32t_unaligned_check(regs->uregs[rd], addr);
	else
		put32t_unaligned_check(regs->uregs[rd], addr);
	return TYPE_LDST;

fault:
	return TYPE_FAULT;
}

/*
 * LDM/STM alignment handler.
 *
 * There are 4 variants of this instruction:
 *
 * B = rn pointer before instruction, A = rn pointer after instruction
 *              ------ increasing address ----->
 *	        |    | r0 | r1 | ... | rx |    |
 * PU = 01             B                    A
 * PU = 11        B                    A
 * PU = 00        A                    B
 * PU = 10             A                    B
 */
static int
do_alignment_ldmstm(unsigned long addr, unsigned long instr, struct pt_regs *regs)
{
	unsigned int rd, rn, correction, nr_regs, regbits;
	unsigned long eaddr, newaddr;

	if (LDM_S_BIT(instr))
		goto bad;

	correction = 4; /* processor implementation defined */
	regs->ARM_pc += correction;

	ai_multi += 1;

	/* count the number of registers in the mask to be transferred */
	nr_regs = hweight16(REGMASK_BITS(instr)) * 4;

	rn = RN_BITS(instr);
	newaddr = eaddr = regs->uregs[rn];

	if (!LDST_U_BIT(instr))
		nr_regs = -nr_regs;
	newaddr += nr_regs;
	if (!LDST_U_BIT(instr))
		eaddr = newaddr;

	if (LDST_P_EQ_U(instr))	/* U = P */
		eaddr += 4;

	/*
	 * This is a "hint" - we already have eaddr worked out by the
	 * processor for us.
	 */
	if (addr != eaddr) {
		printk(KERN_ERR "LDMSTM: PC = %08lx, instr = %08lx, "
			"addr = %08lx, eaddr = %08lx\n",
			 instruction_pointer(regs), instr, addr, eaddr);
		show_regs(regs);
	}

	for (regbits = REGMASK_BITS(instr), rd = 0; regbits; regbits >>= 1, rd += 1)
		if (regbits & 1) {
			if (LDST_L_BIT(instr))
				get32_unaligned_check(regs->uregs[rd], eaddr);
			else
				put32_unaligned_check(regs->uregs[rd], eaddr);
			eaddr += 4;
		}

	if (LDST_W_BIT(instr))
		regs->uregs[rn] = newaddr;
	if (!LDST_L_BIT(instr) || !(REGMASK_BITS(instr) & (1 << 15)))
		regs->ARM_pc -= correction;
	return TYPE_DONE;

fault:
	regs->ARM_pc -= correction;
	return TYPE_FAULT;

bad:
	printk(KERN_ERR "Alignment trap: not handling ldm with s-bit set\n");
	return TYPE_ERROR;
}

static int
do_alignment(unsigned long addr, int error_code, struct pt_regs *regs)
{
	union offset_union offset;
	unsigned long instr, instrptr;
	int (*handler)(unsigned long addr, unsigned long instr, struct pt_regs *regs);
	unsigned int type;

	if (user_mode(regs))
		goto user;

	ai_sys += 1;

	instrptr = instruction_pointer(regs);
	instr = *(unsigned long *)instrptr;

	regs->ARM_pc += 4;

	switch (CODING_BITS(instr)) {
	case 0x00000000:	/* ldrh or strh */
		if (LDSTH_I_BIT(instr))
			offset.un = (instr & 0xf00) >> 4 | (instr & 15);
		else
			offset.un = regs->uregs[RM_BITS(instr)];
		handler = do_alignment_ldrhstrh;
		break;

	case 0x04000000:	/* ldr or str immediate */
		offset.un = OFFSET_BITS(instr);
		handler = do_alignment_ldrstr;
		break;

	case 0x06000000:	/* ldr or str register */
		offset.un = regs->uregs[RM_BITS(instr)];

		if (IS_SHIFT(instr)) {
			unsigned int shiftval = SHIFT_BITS(instr);

			switch(SHIFT_TYPE(instr)) {
			case SHIFT_LSL:
				offset.un <<= shiftval;
				break;

			case SHIFT_LSR:
				offset.un >>= shiftval;
				break;

			case SHIFT_ASR:
				offset.sn >>= shiftval;
				break;

			case SHIFT_RORRRX:
				if (shiftval == 0) {
					offset.un >>= 1;
					if (regs->ARM_cpsr & CC_C_BIT)
						offset.un |= 1 << 31;
				} else
					offset.un = offset.un >> shiftval |
							  offset.un << (32 - shiftval);
				break;
			}
		}
		handler = do_alignment_ldrstr;
		break;

	case 0x08000000:	/* ldm or stm */
		handler = do_alignment_ldmstm;
		break;

	default:
		goto bad;
	}

	type = handler(addr, instr, regs);

	if (type == TYPE_ERROR || type == TYPE_FAULT)
		goto bad_or_fault;

	if (type == TYPE_LDST)
		do_alignment_finish_ldst(addr, instr, regs, offset);

	return 0;

bad_or_fault:
	if (type == TYPE_ERROR)
		goto bad;
	regs->ARM_pc -= 4;
	/*
	 * We got a fault - fix it up, or die.
	 */
	do_bad_area(current, current->mm, addr, error_code, regs);
	return 0;

bad:
	/*
	 * Oops, we didn't handle the instruction.
	 */
	printk(KERN_ERR "Alignment trap: not handling instruction "
		"%08lx at [<%08lx>]\n", instr, instrptr);
	ai_skipped += 1;
	return 1;

user:
	set_cr(cr_no_alignment);
	ai_user += 1;
	return 0;
}

#else

#define do_alignment NULL

#endif

/*
 * Some section permission faults need to be handled gracefully, for
 * instance, when they happen due to a __{get,put}_user during an oops).
 */
static int
do_sect_fault(unsigned long addr, int error_code, struct pt_regs *regs)
{
	struct task_struct *tsk = current;
	do_bad_area(tsk, tsk->active_mm, addr, error_code, regs);
	return 0;
}

/*
 * Hook for things that need to trap external faults.  Note that
 * we don't guarantee that this will be the final version of the
 * interface.
 */
int (*external_fault)(unsigned long addr, struct pt_regs *regs);

static int
do_external_fault(unsigned long addr, int error_code, struct pt_regs *regs)
{
	if (external_fault)
		return external_fault(addr, regs);
	return 1;
}

static const struct fsr_info {
	int	(*fn)(unsigned long addr, int error_code, struct pt_regs *regs);
	int	sig;
	char	*name;
} fsr_info[] = {
	{ NULL,			SIGSEGV, "vector exception"		   },
	{ do_alignment,		SIGILL,	 "alignment exception"		   },
	{ NULL,			SIGKILL, "terminal exception"		   },
	{ do_alignment,		SIGILL,	 "alignment exception"		   },
	{ do_external_fault,	SIGBUS,	 "external abort on linefetch"	   },
	{ do_translation_fault,	SIGSEGV, "section translation fault"	   },
	{ do_external_fault,	SIGBUS,	 "external abort on linefetch"	   },
	{ do_page_fault,	SIGSEGV, "page translation fault"	   },
	{ do_external_fault,	SIGBUS,	 "external abort on non-linefetch" },
	{ NULL,			SIGSEGV, "section domain fault"		   },
	{ do_external_fault,	SIGBUS,	 "external abort on non-linefetch" },
	{ NULL,			SIGSEGV, "page domain fault"		   },
	{ NULL,			SIGBUS,	 "external abort on translation"   },
	{ do_sect_fault,	SIGSEGV, "section permission fault"	   },
	{ NULL,			SIGBUS,	 "external abort on translation"   },
	{ do_page_fault,	SIGSEGV, "page permission fault"	   }
};

/*
 * Currently dropped down to debug level
 */
asmlinkage void
do_DataAbort(unsigned long addr, int error_code, struct pt_regs *regs, int fsr)
{
	const struct fsr_info *inf = fsr_info + (fsr & 15);

#if defined(CONFIG_CPU_SA110) || defined(CONFIG_CPU_SA1100) || defined(CONFIG_DEBUG_ERRORS)
	if (addr == regs->ARM_pc)
		goto sa1_weirdness;
#endif

	if (!inf->fn)
		goto bad;

	if (!inf->fn(addr, error_code, regs))
		return;
bad:
	printk(KERN_ALERT "Unhandled fault: %s (%X) at 0x%08lx\n",
		inf->name, fsr, addr);
	show_pte(current->mm, addr);
	force_sig(inf->sig, current);
	die_if_kernel("Oops", regs, 0);
	return;

#if defined(CONFIG_CPU_SA110) || defined(CONFIG_CPU_SA1100) || defined(CONFIG_DEBUG_ERRORS)
sa1_weirdness:
	if (user_mode(regs)) {
		static int first = 1;
		if (first) {
			printk(KERN_DEBUG "Fixing up bad data abort at %08lx\n", addr);
#ifdef CONFIG_DEBUG_ERRORS
			show_pte(current->mm, addr);
#endif
		}
		first = 0;
		return;
	}

	if (!inf->fn || inf->fn(addr, error_code, regs))
		goto bad;
	return;
#endif
}

asmlinkage void
do_PrefetchAbort(unsigned long addr, struct pt_regs *regs)
{
	do_translation_fault(addr, 0, regs);
}

/*
 * We take the easy way out of this problem - we make the
 * PTE uncacheable.  However, we leave the write buffer on.
 */
static void adjust_pte(struct vm_area_struct *vma, unsigned long address)
{
	pgd_t *pgd;
	pmd_t *pmd;
	pte_t *pte, entry;

	pgd = pgd_offset(vma->vm_mm, address);
	if (pgd_none(*pgd))
		return;
	if (pgd_bad(*pgd))
		goto bad_pgd;

	pmd = pmd_offset(pgd, address);
	if (pmd_none(*pmd))
		return;
	if (pmd_bad(*pmd))
		goto bad_pmd;

	pte = pte_offset(pmd, address);
	entry = *pte;

	/*
	 * If this page isn't present, or is already setup to
	 * fault (ie, is old), we can safely ignore any issues.
	 */
	if (pte_present(entry) && pte_val(entry) & L_PTE_CACHEABLE) {
		flush_cache_page(vma, address);
		pte_val(entry) &= ~L_PTE_CACHEABLE;
		set_pte(pte, entry);
		flush_tlb_page(vma, address);
	}
	return;

bad_pgd:
	pgd_ERROR(*pgd);
	pgd_clear(pgd);
	return;

bad_pmd:
	pmd_ERROR(*pmd);
	pmd_clear(pmd);
	return;
}

/*
 * Take care of architecture specific things when placing a new PTE into
 * a page table, or changing an existing PTE.  Basically, there are two
 * things that we need to take care of:
 *
 *  1. If PG_dcache_dirty is set for the page, we need to ensure
 *     that any cache entries for the kernels virtual memory
 *     range are written back to the page.
 *  2. If we have multiple shared mappings of the same space in
 *     an object, we need to deal with the cache aliasing issues.
 *
 * Note that the page_table_lock will be held.
 */
void update_mmu_cache(struct vm_area_struct *vma, unsigned long addr, pte_t pte)
{
	struct page *page = pte_page(pte);
	struct vm_area_struct *mpnt;
	struct mm_struct *mm;
	unsigned long pgoff;
	int aliases;

	if (!VALID_PAGE(page) || !page->mapping)
		return;

	if (test_and_clear_bit(PG_dcache_dirty, &page->flags)) {
		unsigned long kvirt = (unsigned long)page_address(page);
		cpu_cache_clean_invalidate_range(kvirt, kvirt + PAGE_SIZE, 0);
	}

	mm = vma->vm_mm;
	pgoff = (addr - vma->vm_start) >> PAGE_SHIFT;
	aliases = 0;

	/*
	 * If we have any shared mappings that are in the same mm
	 * space, then we need to handle them specially to maintain
	 * cache coherency.
	 */
	for (mpnt = page->mapping->i_mmap_shared; mpnt;
	     mpnt = mpnt->vm_next_share) {
		unsigned long off;

		/*
		 * If this VMA is not in our MM, we can ignore it.
		 * Note that we intentionally don't mask out the VMA
		 * that we are fixing up.
		 */
		if (mpnt->vm_mm != mm && mpnt != vma)
			continue;

		/*
		 * If the page isn't in this VMA, we can also ignore it.
		 */
		if (pgoff < mpnt->vm_pgoff)
			continue;

		off = pgoff - mpnt->vm_pgoff;
		if (off >= (mpnt->vm_end - mpnt->vm_start) >> PAGE_SHIFT)
			continue;

		/*
		 * Ok, it is within mpnt.  Fix it up.
		 */
		adjust_pte(mpnt, mpnt->vm_start + (off << PAGE_SHIFT));
		aliases ++;
	}
	if (aliases)
		adjust_pte(vma, addr);
}
