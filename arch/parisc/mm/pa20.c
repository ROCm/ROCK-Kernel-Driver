/* $Id: pa20.c,v 1.1 1999/03/17 01:05:41 pjlahaie Exp $
 *
 * pa20.c: PA 2.0 specific mmu/cache code.
 *
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>

#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/sgialib.h>
#include <asm/mmu_context.h>

extern unsigned long mips_tlb_entries;

/* page functions */
void pa20_clear_page(unsigned long page)
{
}

static void pa20_copy_page(unsigned long to, unsigned long from)
{
}

/* Cache operations. */
static inline void pa20_flush_cache_all(void) { }
static void pa20_flush_cache_mm(struct mm_struct *mm) { }
static void pa20_flush_cache_range(struct mm_struct *mm,
				    unsigned long start,
				    unsigned long end)
{
}

static void pa20_flush_cache_page(struct vm_area_struct *vma,
				   unsigned long page)
{
}

static void pa20_flush_page_to_ram(unsigned long page)
{
}

static void pa20_flush_cache_sigtramp(unsigned long page)
{
}

/* TLB operations. */
static inline void pa20_flush_tlb_all(void)
{
	unsigned long flags;
	int entry;

	save_and_cli(flags);
/* Here we will need to flush all the TLBs */
	restore_flags(flags);
}

static void pa20_flush_tlb_mm(struct mm_struct *mm)
{
/* This is what the MIPS does..  Is it the right thing for PA-RISC? */
	if(mm == current->mm)
		pa20_flush_tlb_all();
}

static void pa20_flush_tlb_range(struct mm_struct *mm, unsigned long start,
				  unsigned long end)
{
	if(mm == current->mm)
		pa20_flush_tlb_all();
}

static void pa20_flush_tlb_page(struct vm_area_struct *vma, unsigned long page)
{
	if(vma->vm_mm == current->mm)
		pa20_flush_tlb_all();
}

static void pa20_load_pgd(unsigned long pg_dir)
{
	unsigned long flags;
    /* We need to do the right thing here */
}

/*
 * Initialize new page directory with pointers to invalid ptes
 */
static void pa20_pgd_init(unsigned long page)
{
	unsigned long dummy1, dummy2;

}

static void pa20_update_mmu_cache(struct vm_area_struct * vma,
				   unsigned long address, pte_t pte)
{
	pa20_flush_tlb_page(vma, address);
}

static void pa20_show_regs(struct pt_regs * regs)
{
	/*
	 * Saved main processor registers
	 */
	printk("$0 : %08x %08lx %08lx %08lx %08lx %08lx %08lx %08lx\n",
	       0, (unsigned long) regs->regs[1], (unsigned long) regs->regs[2],
	       (unsigned long) regs->regs[3], (unsigned long) regs->regs[4],
	       (unsigned long) regs->regs[5], (unsigned long) regs->regs[6],
	       (unsigned long) regs->regs[7]);
	printk("$8 : %08lx %08lx %08lx %08lx %08lx %08lx %08lx %08lx\n",
	       (unsigned long) regs->regs[8], (unsigned long) regs->regs[9],
	       (unsigned long) regs->regs[10], (unsigned long) regs->regs[11],
               (unsigned long) regs->regs[12], (unsigned long) regs->regs[13],
	       (unsigned long) regs->regs[14], (unsigned long) regs->regs[15]);
	printk("$16: %08lx %08lx %08lx %08lx %08lx %08lx %08lx %08lx\n",
	       (unsigned long) regs->regs[16], (unsigned long) regs->regs[17],
	       (unsigned long) regs->regs[18], (unsigned long) regs->regs[19],
               (unsigned long) regs->regs[20], (unsigned long) regs->regs[21],
	       (unsigned long) regs->regs[22], (unsigned long) regs->regs[23]);
	printk("$24: %08lx %08lx                   %08lx %08lx %08lx %08lx\n",
	       (unsigned long) regs->regs[24], (unsigned long) regs->regs[25],
	       (unsigned long) regs->regs[28], (unsigned long) regs->regs[29],
               (unsigned long) regs->regs[30], (unsigned long) regs->regs[31]);

	/*
	 * Saved cp0 registers
	 */
	printk("epc  : %08lx\nStatus: %08x\nCause : %08x\n",
	       (unsigned long) regs->cp0_epc, (unsigned int) regs->cp0_status,
	       (unsigned int) regs->cp0_cause);
}

static int pa20_user_mode(struct pt_regs *regs)
{
       /* Return user mode stuff?? */
}

__initfunc(void ld_mmu_pa20(void))
{

    /* Taken directly from the MIPS arch..  Lots of bad things here */
	clear_page = pa20_clear_page;
	copy_page = pa20_copy_page;

	flush_cache_all = pa20_flush_cache_all;
	flush_cache_mm = pa20_flush_cache_mm;
	flush_cache_range = pa20_flush_cache_range;
	flush_cache_page = pa20_flush_cache_page;
	flush_cache_sigtramp = pa20_flush_cache_sigtramp;
	flush_page_to_ram = pa20_flush_page_to_ram;

	flush_tlb_all = pa20_flush_tlb_all;
	flush_tlb_mm = pa20_flush_tlb_mm;
	flush_tlb_range = pa20_flush_tlb_range;
	flush_tlb_page = pa20_flush_tlb_page;
	pa20_asid_setup();

	load_pgd = pa20_load_pgd;
	pgd_init = pa20_pgd_init;
	update_mmu_cache = pa20_update_mmu_cache;

	show_regs = pa20_show_regs;
    
        add_wired_entry = pa20_add_wired_entry;

	user_mode = pa20_user_mode;
	flush_tlb_all();
}
