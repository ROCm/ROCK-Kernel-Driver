/*
 *
 *    Copyright (c) 1998-1999 TiVo, Inc.
 *      Original implementation.
 *    Copyright (c) 1999-2000 Grant Erickson <grant@lcse.umn.edu>
 *      Minor rework.
 *
 *    Module name: 4xx_tlb.c
 *
 *    Description:
 *      Routines for manipulating the TLB on PowerPC 400-class processors.
 *
 */

#include <linux/mm.h>

#include <asm/processor.h>
#include <asm/io.h>
#include <asm/mmu.h>
#include <asm/pgtable.h>
#include <asm/system.h>


/* Preprocessor Defines */

#if !defined(TRUE) || TRUE != 1
#define TRUE    1
#endif

#if !defined(FALSE) || FALSE != 0
#define FALSE   0
#endif


/* Global Variables */

static int pinned = 0;


/* Function Prototypes */

static int PPC4xx_tlb_miss(struct pt_regs *, unsigned long, int);

extern void do_page_fault(struct pt_regs *, unsigned long, unsigned long);


/*
 * ()
 *
 * Description:
 *   This routine...
 *
 * Input(s):
 *
 *
 * Output(s):
 *
 *
 * Returns:
 *
 *
 */
static inline void
PPC4xx_tlb_write(unsigned long tag, unsigned long data, unsigned int index)
{
	asm("tlbwe %0,%1,1" : : "r" (data), "r" (index));
	asm("tlbwe %0,%1,0" : : "r" (tag), "r" (index));
}

/*
 * ()
 *
 * Description:
 *   This routine...
 *
 * Input(s):
 *
 *
 * Output(s):
 *
 *
 * Returns:
 *
 *
 */
void
PPC4xx_flush_tlb_all(void)
{
	int i;
	unsigned long flags, pid;

	save_flags(flags);
	cli();

	pid = mfspr(SPRN_PID);
	mtspr(SPRN_PID, 0);

	for (i = pinned; i < PPC4XX_TLB_SIZE; i++) {
		PPC4xx_tlb_write(0, 0, i);
	}
	asm("sync;isync");

	mtspr(SPRN_PID, pid);
	restore_flags(flags);
}

/*
 * ()
 *
 * Description:
 *   This routine...
 *
 * Input(s):
 *
 *
 * Output(s):
 *
 *
 * Returns:
 *
 *
 */
void
PPC4xx_dtlb_miss(struct pt_regs *regs)
{
	unsigned long addr = mfspr(SPRN_DEAR);
	int write = mfspr(SPRN_ESR) & ESR_DST;

	if (PPC4xx_tlb_miss(regs, addr, write) < 0) {
		sti();
		do_page_fault(regs, addr, write);
		cli();
	}
	
}

/*
 * ()
 *
 * Description:
 *   This routine...
 *
 * Input(s):
 *
 *
 * Output(s):
 *
 *
 * Returns:
 *
 *
 */
void
PPC4xx_itlb_miss(struct pt_regs *regs)
{
	unsigned long addr = regs->nip;

	if (PPC4xx_tlb_miss(regs, addr, 0) < 0) {
		sti();
		do_page_fault(regs, addr, 0);
		cli();
	}
}

/*
 * ()
 *
 * Description:
 *   This routine...
 *
 * Input(s):
 *
 *
 * Output(s):
 *
 *
 * Returns:
 *
 *
 */
void
PPC4xx_tlb_pin(unsigned long va, unsigned long pa, int pagesz, int cache)
{
	unsigned long tag, data;
	unsigned long opid;

	if (pinned >= PPC4XX_TLB_SIZE)
		return;

	opid = mfspr(SPRN_PID);
	mtspr(SPRN_PID, 0);

	data = (pa & TLB_RPN_MASK) | TLB_WR;

	if (cache)
		data |= (TLB_EX);
	else
		data |= (TLB_G | TLB_I);

	tag = (va & TLB_EPN_MASK) | TLB_VALID | pagesz;

	PPC4xx_tlb_write(tag, data, pinned++);

	mtspr(SPRN_PID, opid);
	return;
}

/*
 * ()
 *
 * Description:
 *   This routine...
 *
 * Input(s):
 *
 *
 * Output(s):
 *
 *
 * Returns:
 *
 *
 */
void
PPC4xx_tlb_unpin(unsigned long va, unsigned long pa, int size)
{
	/* XXX - To be implemented. */
}

/*
 * ()
 *
 * Description:
 *   This routine...
 *
 * Input(s):
 *
 *
 * Output(s):
 *
 *
 * Returns:
 *
 *
 */
static inline void
PPC4xx_tlb_update(unsigned long addr, pte_t *pte)
{
        unsigned long data, tag, rand;
        int i, found = 1;

        /* Construct the hardware TLB entry from the Linux-style PTE */

        tag = tag = (addr & PAGE_MASK) | TLB_VALID | TLB_PAGESZ(PAGESZ_4K);
        data = data = (pte_val(*pte) & PAGE_MASK) | TLB_EX | TLB_WR;

#if 0
        if (pte_val(*pte) & _PAGE_HWWRITE)
                data |= TLB_WR;
#endif

        if (pte_val(*pte) & _PAGE_NO_CACHE)
                data |= TLB_I;

        if (pte_val(*pte) & _PAGE_GUARDED)
                data |= TLB_G;

        if (addr < KERNELBASE)
                data |= TLB_ZSEL(1);

        /* Attempt to match the new tag to an existing entry in the TLB. */

        asm("tlbsx. %0,0,%2;"
	    "beq 1f;"
	    "li %1,0;1:" : "=r" (i), "=r" (found) : "r" (tag));

	/*
	 * If we found a match for the tag, reuse the entry index and update
	 * the tag and data portions. Otherwise, we did not find a match. Use
	 * the lower 5 bits of the lower time base register as a pseudo-random
	 * index into the TLB and replace the entry at that index.
	 */

        if (found) {
		PPC4xx_tlb_write(tag, data, i);
        } else {
		rand = mfspr(SPRN_TBLO) & (PPC4XX_TLB_SIZE - 1);
		rand += pinned;
		if (rand >= PPC4XX_TLB_SIZE)
			rand -= pinned;

		PPC4xx_tlb_write(tag, data, rand);
		asm("isync;sync");
        }
}

/*
 * ()
 *
 * Description:
 *   This routine...
 *
 * Input(s):
 *
 *
 * Output(s):
 *
 *
 * Returns:
 *
 *
 */
static int
PPC4xx_tlb_miss(struct pt_regs *regs, unsigned long addr, int write)
{
        unsigned long spid, ospid;
        struct mm_struct *mm;
        pgd_t *pgd;
        pmd_t *pmd;
        pte_t *pte;

        if (!user_mode(regs) && (addr >= KERNELBASE)) {
                mm = &init_mm;
                spid = 0;
        } else {
                mm = current->mm;
                spid = mfspr(SPRN_PID);
        }

        pgd = pgd_offset(mm, addr);
        if (pgd_none(*pgd))
                goto bad;

        pmd = pmd_offset(pgd, addr);
        if (pmd_none(*pmd))
                goto bad;

        pte = pte_offset(pmd, addr);
        if (pte_none(*pte) || !pte_present(*pte))
                goto bad;

        if (write) {
                if (!pte_write(*pte))
                        goto bad;

                set_pte(pte, pte_mkdirty(*pte));
        }
        set_pte(pte, pte_mkyoung(*pte));

        ospid = mfspr(SPRN_PID);
        mtspr(SPRN_PID, spid);
        PPC4xx_tlb_update(addr, pte);
        mtspr(SPRN_PID, ospid);

	return (0);
bad:
	return (-1);
}
