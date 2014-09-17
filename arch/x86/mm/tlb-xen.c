#include <linux/init.h>
#include <linux/mm.h>
#include <linux/spinlock.h>
#include <linux/smp.h>
#include <linux/cpumask.h>

#include <asm/tlbflush.h>
#include <asm/mmu_context.h>
#include <asm/cache.h>
#include <linux/debugfs.h>

/*
 * See Documentation/x86/tlb.txt for details.  We choose 33
 * because it is large enough to cover the vast majority (at
 * least 95%) of allocations, and is small enough that we are
 * confident it will not cause too much overhead.  Each single
 * flush is about 100 ns, so this caps the maximum overhead at
 * _about_ 3,000 ns.
 *
 * This is in units of pages.
 */
static unsigned long tlb_single_page_flush_ceiling __read_mostly = 33;

void flush_tlb_mm_range(struct mm_struct *mm, unsigned long start,
				unsigned long end, unsigned long vmflag)
{
	unsigned long addr;
	/* do a global flush by default */
	unsigned long base_pages_to_flush = TLB_FLUSH_ALL;
	const cpumask_t *mask = mm_cpumask(mm);
	cpumask_var_t temp;

	preempt_disable();
	if (current->active_mm != mm || !current->mm) {
		if (cpumask_any_but(mask, smp_processor_id()) >= nr_cpu_ids) {
			preempt_enable();
			return;
		}
		if (alloc_cpumask_var(&temp, GFP_ATOMIC)) {
			cpumask_andnot(temp, mask,
				       cpumask_of(smp_processor_id()));
			mask = temp;
		}
	}

	if ((end != TLB_FLUSH_ALL) && !(vmflag & VM_HUGETLB))
		base_pages_to_flush = (end - start) >> PAGE_SHIFT;

	if (base_pages_to_flush > tlb_single_page_flush_ceiling) {
		base_pages_to_flush = TLB_FLUSH_ALL;
		count_vm_tlb_event(NR_TLB_LOCAL_FLUSH_ALL);
		xen_tlb_flush_mask(mask);
	} else {
		/* flush range by one by one 'invlpg' */
		for (addr = start; addr < end; addr += PAGE_SIZE) {
			count_vm_tlb_event(NR_TLB_LOCAL_FLUSH_ONE);
			xen_invlpg_mask(mask, addr);
		}
	}
	trace_tlb_flush(TLB_LOCAL_MM_SHOOTDOWN, base_pages_to_flush);
	if (mask != mm_cpumask(mm))
		free_cpumask_var(temp);
	preempt_enable();
}

void flush_tlb_kernel_range(unsigned long start, unsigned long end)
{

	/* Balance as user space task's flush, a bit conservative */
	if (end == TLB_FLUSH_ALL ||
	    (end - start) > tlb_single_page_flush_ceiling * PAGE_SIZE) {
		xen_tlb_flush_all();
	} else {
		unsigned long addr;

		/* flush range by one by one 'invlpg' */
		for (addr = start; addr < end; addr += PAGE_SIZE)
			xen_invlpg_all(addr);
	}
}

static ssize_t tlbflush_read_file(struct file *file, char __user *user_buf,
			     size_t count, loff_t *ppos)
{
	char buf[32];
	unsigned int len;

	len = sprintf(buf, "%ld\n", tlb_single_page_flush_ceiling);
	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static ssize_t tlbflush_write_file(struct file *file,
		 const char __user *user_buf, size_t count, loff_t *ppos)
{
	char buf[32];
	ssize_t len;
	int ceiling;

	len = min(count, sizeof(buf) - 1);
	if (copy_from_user(buf, user_buf, len))
		return -EFAULT;

	buf[len] = '\0';
	if (kstrtoint(buf, 0, &ceiling))
		return -EINVAL;

	if (ceiling < 0)
		return -EINVAL;

	tlb_single_page_flush_ceiling = ceiling;
	return count;
}

static const struct file_operations fops_tlbflush = {
	.read = tlbflush_read_file,
	.write = tlbflush_write_file,
	.llseek = default_llseek,
};

static int __init create_tlb_single_page_flush_ceiling(void)
{
	debugfs_create_file("tlb_single_page_flush_ceiling", S_IRUSR | S_IWUSR,
			    arch_debugfs_dir, NULL, &fops_tlbflush);
	return 0;
}
late_initcall(create_tlb_single_page_flush_ceiling);
