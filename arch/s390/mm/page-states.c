/*
 * arch/s390/mm/page-states.c
 *
 * (C) Copyright IBM Corp. 2008
 *
 * Guest page hinting for unused pages.
 *
 * Author(s): Martin Schwidefsky <schwidefsky@de.ibm.com>
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/cpu.h>
#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/page-states.h>
#include <linux/pagemap.h>
#include <asm/io.h>

extern void die(const char *,struct pt_regs *,long);

#ifndef CONFIG_64BIT
#define __FAIL_ADDR_MASK 0x7ffff000
#else /* CONFIG_64BIT */
#define __FAIL_ADDR_MASK -4096L
#endif /* CONFIG_64BIT */

int cmma_flag;

void __init cmma_init(void)
{
	register unsigned long tmp asm("0") = 0;
	register int rc asm("1") = -ENOSYS;

	if (!cmma_flag)
		return;
	asm volatile(
		"       .insn rrf,0xb9ab0000,%1,%1,0,0\n"
		"0:     la      %0,0\n"
		"1:\n"
		EX_TABLE(0b,1b)
		: "+&d" (rc), "+&d" (tmp));
	if (rc)
		cmma_flag = 0;
}

static int __init cmma(char *str)
{
	char *parm;

	parm = strstrip(str);
	if (strcmp(parm, "yes") == 0 || strcmp(parm, "on") == 0) {
		cmma_flag = 1;
		return 1;
	}
	if (strcmp(parm, "no") == 0 || strcmp(parm, "off") == 0) {
		cmma_flag = 0;
		return 1;
	}
	return 0;
}

__setup("cmma=", cmma);

static inline void fixup_user_copy(struct pt_regs *regs,
				   unsigned long address, unsigned short rx)
{
	const struct exception_table_entry *fixup;
	unsigned long kaddr;

	kaddr = (regs->gprs[rx >> 12] + (rx & 0xfff)) & __FAIL_ADDR_MASK;
	if (virt_to_phys((void *) kaddr) != address)
		return;

	fixup = search_exception_tables(regs->psw.addr & PSW_ADDR_INSN);
	if (fixup)
		regs->psw.addr = fixup->fixup | PSW_ADDR_AMODE;
	else
		die("discard fault", regs, SIGSEGV);
}

/*
 * Discarded pages with a page_count() of zero are placed on
 * the page_discarded_list until all cpus have been at
 * least once in enabled code. That closes the race of page
 * free vs. discard faults.
 */
void do_discard_fault(struct pt_regs *regs, unsigned long error_code)
{
	unsigned long address;
	struct page *page;

	/*
	 * get the real address that caused the block validity
	 * exception.
	 */
	address = S390_lowcore.trans_exc_code & __FAIL_ADDR_MASK;
	page = pfn_to_page(address >> PAGE_SHIFT);

	/*
	 * Check for the special case of a discard fault in
	 * copy_{from,to}_user. User copy is done using one of
	 * three special instructions: mvcp, mvcs or mvcos.
	 */
	if (!(regs->psw.mask & PSW_MASK_PSTATE)) {
		switch (*(unsigned char *) regs->psw.addr) {
		case 0xda:	/* mvcp */
			fixup_user_copy(regs, address,
					*(__u16 *)(regs->psw.addr + 2));
			break;
		case 0xdb:	/* mvcs */
			fixup_user_copy(regs, address,
					*(__u16 *)(regs->psw.addr + 4));
			break;
		case 0xc8:	/* mvcos */
			if (regs->gprs[0] == 0x81)
				fixup_user_copy(regs, address,
						*(__u16*)(regs->psw.addr + 2));
			else if (regs->gprs[0] == 0x810000)
				fixup_user_copy(regs, address,
						*(__u16*)(regs->psw.addr + 4));
			break;
		default:
			break;
		}
	}

	if (likely(get_page_unless_zero(page))) {
		local_irq_enable();
		page_discard(page);
	}
}

static DEFINE_PER_CPU(struct list_head, page_discard_list);
static struct list_head page_gather_list = LIST_HEAD_INIT(page_gather_list);
static struct list_head page_signoff_list = LIST_HEAD_INIT(page_signoff_list);
static cpumask_t page_signoff_cpumask = CPU_MASK_NONE;
static DEFINE_SPINLOCK(page_discard_lock);

/*
 * page_free_discarded
 *
 * free_hot_cold_page calls this function if it is about to free a
 * page that has PG_discarded set. Since there might be pending
 * discard faults on other cpus on s390 we have to postpone the
 * freeing of the page until each cpu has "signed-off" the page.
 *
 * returns 1 to stop free_hot_cold_page from freeing the page.
 */
int page_free_discarded(struct page *page)
{
	local_irq_disable();
	list_add_tail(&page->lru, &__get_cpu_var(page_discard_list));
	local_irq_enable();
	return 1;
}

/*
 * page_shrink_discard_list
 *
 * This function is called from the timer tick for an active cpu or
 * from the idle notifier. It frees discarded pages in three stages.
 * In the first stage it moves the pages from the per-cpu discard
 * list to a global list. From the global list the pages are moved
 * to the signoff list in a second step. The third step is to free
 * the pages after all cpus acknoledged the signoff. That prevents
 * that a page is freed when a cpus still has a pending discard
 * fault for the page.
 */
void page_shrink_discard_list(void)
{
	struct list_head *cpu_list = &__get_cpu_var(page_discard_list);
	struct list_head free_list = LIST_HEAD_INIT(free_list);
	struct page *page, *next;
	int cpu = smp_processor_id();

	if (list_empty(cpu_list) && !cpu_isset(cpu, page_signoff_cpumask))
		return;
	spin_lock(&page_discard_lock);
	if (!list_empty(cpu_list))
		list_splice_init(cpu_list, &page_gather_list);
	cpu_clear(cpu, page_signoff_cpumask);
	if (cpus_empty(page_signoff_cpumask)) {
		list_splice_init(&page_signoff_list, &free_list);
		list_splice_init(&page_gather_list, &page_signoff_list);
		if (!list_empty(&page_signoff_list)) {
			/* Take care of the nohz race.. */
			page_signoff_cpumask = cpu_online_map;
			smp_wmb();
			cpus_andnot(page_signoff_cpumask,
				    page_signoff_cpumask, nohz_cpu_mask);
			cpu_clear(cpu, page_signoff_cpumask);
			if (cpus_empty(page_signoff_cpumask))
				list_splice_init(&page_signoff_list,
						 &free_list);
		}
	}
	spin_unlock(&page_discard_lock);
	list_for_each_entry_safe(page, next, &free_list, lru) {
		ClearPageDiscarded(page);
		free_cold_page(page);
	}
}

static int page_discard_cpu_notify(struct notifier_block *self,
				   unsigned long action, void *hcpu)
{
	int cpu = (unsigned long) hcpu;

	if (action == CPU_DEAD) {
		local_irq_disable();
		list_splice_init(&per_cpu(page_discard_list, cpu),
				 &__get_cpu_var(page_discard_list));
		local_irq_enable();
	}
	return NOTIFY_OK;
}

static struct notifier_block page_discard_cpu_notifier = {
	.notifier_call = page_discard_cpu_notify,
};

void __init page_discard_init(void)
{
	int i;

	for_each_possible_cpu(i)
		INIT_LIST_HEAD(&per_cpu(page_discard_list, i));
	if (register_cpu_notifier(&page_discard_cpu_notifier))
		panic("Couldn't register page discard cpu notifier");
}
