/*
 *  linux/arch/cris/mm/fault.c
 *
 *  Low level bus fault handler
 *
 *
 *  Copyright (C) 2000, 2001  Axis Communications AB
 *
 *  Authors:  Bjorn Wesen 
 * 
 */

#include <linux/mm.h>
#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include <asm/arch/svinto.h>

/* debug of low-level TLB reload */
#undef DEBUG

#ifdef DEBUG
#define D(x) x
#else
#define D(x)
#endif

extern volatile pgd_t *current_pgd;

extern const struct exception_table_entry
	*search_exception_tables(unsigned long addr);

asmlinkage void do_page_fault(unsigned long address, struct pt_regs *regs,
                              int protection, int writeaccess);

/* fast TLB-fill fault handler
 * this is called from entry.S with interrupts disabled
 */

void
handle_mmu_bus_fault(struct pt_regs *regs)
{
	int cause;
#ifdef DEBUG
	int select;
	int index;
	int page_id;
	int acc, inv;
#endif
	int miss, we, writeac;
	pmd_t *pmd;
	pte_t pte;
	unsigned long address;

	cause = *R_MMU_CAUSE;

	address = cause & PAGE_MASK; /* get faulting address */

#ifdef DEBUG
	select = *R_TLB_SELECT;
	page_id = IO_EXTRACT(R_MMU_CAUSE,  page_id,   cause);
	acc     = IO_EXTRACT(R_MMU_CAUSE,  acc_excp,  cause);
	inv     = IO_EXTRACT(R_MMU_CAUSE,  inv_excp,  cause);  
	index   = IO_EXTRACT(R_TLB_SELECT, index,     select);
#endif
	miss    = IO_EXTRACT(R_MMU_CAUSE,  miss_excp, cause);
	we      = IO_EXTRACT(R_MMU_CAUSE,  we_excp,   cause);
	writeac = IO_EXTRACT(R_MMU_CAUSE,  wr_rd,     cause);

	/* ETRAX 100LX TR89 bugfix: if the second half of an unaligned
	 * write causes a MMU-fault, it will not be restarted correctly.
	 * This could happen if a write crosses a page-boundary and the
	 * second page is not yet COW'ed or even loaded. The workaround
	 * is to clear the unaligned bit in the CPU status record, so 
	 * that the CPU will rerun both the first and second halves of
	 * the instruction. This will not have any sideeffects unless
	 * the first half goes to any device or memory that can't be
	 * written twice, and which is mapped through the MMU.
	 *
	 * We only need to do this for writes.
	 */

	if(writeac)
		regs->csrinstr &= ~(1 << 5);
	
	D(printk("bus_fault from IRP 0x%lx: addr 0x%lx, miss %d, inv %d, we %d, acc %d, dx %d pid %d\n",
		 regs->irp, address, miss, inv, we, acc, index, page_id));

	/* for a miss, we need to reload the TLB entry */

	if (miss) {
		/* see if the pte exists at all
		 * refer through current_pgd, dont use mm->pgd
		 */

		pmd = (pmd_t *)(current_pgd + pgd_index(address));
		if (pmd_none(*pmd)) {
			do_page_fault(address, regs, 0, writeac);
			return;
		}
		if (pmd_bad(*pmd)) {
			printk("bad pgdir entry 0x%lx at 0x%p\n", *(unsigned long*)pmd, pmd);
			pmd_clear(pmd);
			return;
		}
		pte = *pte_offset_kernel(pmd, address);
		if (!pte_present(pte)) {
			do_page_fault(address, regs, 0, writeac);
			return;
		}

#ifdef DEBUG
		printk(" found pte %lx pg %p ", pte_val(pte), pte_page(pte));
		if (pte_val(pte) & _PAGE_SILENT_WRITE)
			printk("Silent-W ");
		if (pte_val(pte) & _PAGE_KERNEL)
			printk("Kernel ");
		if (pte_val(pte) & _PAGE_SILENT_READ)
			printk("Silent-R ");
		if (pte_val(pte) & _PAGE_GLOBAL)
			printk("Global ");
		if (pte_val(pte) & _PAGE_PRESENT)
			printk("Present ");
		if (pte_val(pte) & _PAGE_ACCESSED)
			printk("Accessed ");
		if (pte_val(pte) & _PAGE_MODIFIED)
			printk("Modified ");
		if (pte_val(pte) & _PAGE_READ)
			printk("Readable ");
		if (pte_val(pte) & _PAGE_WRITE)
			printk("Writeable ");
		printk("\n");
#endif

		/* load up the chosen TLB entry
		 * this assumes the pte format is the same as the TLB_LO layout.
		 *
		 * the write to R_TLB_LO also writes the vpn and page_id fields from
		 * R_MMU_CAUSE, which we in this case obviously want to keep
		 */

		*R_TLB_LO = pte_val(pte);

		return;
	}

	/* leave it to the MM system fault handler */
	do_page_fault(address, regs, 1, we);
}

/* Called from arch/cris/mm/fault.c to find fixup code. */
int
find_fixup_code(struct pt_regs *regs)
{
	const struct exception_table_entry *fixup;

	if ((fixup = search_exception_tables(regs->irp)) != 0) {
		/* Adjust the instruction pointer in the stackframe. */
		regs->irp = fixup->fixup;
		
		/* 
		 * Don't return by restoring the CPU state, so switch
		 * frame-type. 
		 */
		regs->frametype = CRIS_FRAME_NORMAL;
		return 1;
	}

	return 0;
}
