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
 * It can find out the THP large page, or
 * HUGETLB page in tlb_flush when THP disabled
 */
static inline unsigned long has_large_page(struct mm_struct *mm,
				 unsigned long start, unsigned long end)
{
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
	unsigned long addr = ALIGN(start, HPAGE_SIZE);
	for (; addr < end; addr += HPAGE_SIZE) {
		pgd = pgd_offset(mm, addr);
		if (likely(!pgd_none(*pgd))) {
			pud = pud_offset(pgd, addr);
			if (likely(!pud_none(*pud))) {
				pmd = pmd_offset(pud, addr);
				if (likely(!pmd_none(*pmd)))
					if (pmd_large(*pmd))
						return addr;
			}
		}
	}
	return 0;
}

void flush_tlb_mm_range(struct mm_struct *mm, unsigned long start,
				unsigned long end, unsigned long vmflag)
{
	unsigned long addr;
	unsigned act_entries, tlb_entries = 0;
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

	if (end == TLB_FLUSH_ALL || tlb_flushall_shift == -1
				 || vmflag == VM_HUGETLB)
		goto flush_all;

	/* In modern CPU, last level tlb used for both data/ins */
	if (vmflag & VM_EXEC)
		tlb_entries = tlb_lli_4k[ENTRIES];
	else
		tlb_entries = tlb_lld_4k[ENTRIES];
	/* Assume all of TLB entries was occupied by this task */
	act_entries = mm->total_vm > tlb_entries ? tlb_entries : mm->total_vm;

	/* tlb_flushall_shift is on balance point, details in commit log */
	if (((end - start) >> PAGE_SHIFT)
	    <= (act_entries >> tlb_flushall_shift)
	    && !has_large_page(mm, start, end)) {
		/* flush range by one by one 'invlpg' */
		for (addr = start; addr < end; addr += PAGE_SIZE)
			xen_invlpg_mask(mask, addr);
	} else {
flush_all:
		xen_tlb_flush_mask(mask);
	}

	if (mask != mm_cpumask(mm))
		free_cpumask_var(temp);
	preempt_enable();
}

void flush_tlb_kernel_range(unsigned long start, unsigned long end)
{
	unsigned act_entries;

	/* In modern CPU, last level tlb used for both data/ins */
	act_entries = tlb_lld_4k[ENTRIES];

	/* Balance as user space task's flush, a bit conservative */
	if (end == TLB_FLUSH_ALL || tlb_flushall_shift == -1 ||
		(end - start) >> PAGE_SHIFT > act_entries >> tlb_flushall_shift)

		xen_tlb_flush_all();
	else {
		unsigned long addr;

		/* flush range by one by one 'invlpg' */
		for (addr = start; addr < end; addr += PAGE_SIZE)
			xen_invlpg_all(addr);
	}
}

#ifdef CONFIG_DEBUG_TLBFLUSH
static ssize_t tlbflush_read_file(struct file *file, char __user *user_buf,
			     size_t count, loff_t *ppos)
{
	char buf[32];
	unsigned int len;

	len = sprintf(buf, "%hd\n", tlb_flushall_shift);
	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static ssize_t tlbflush_write_file(struct file *file,
		 const char __user *user_buf, size_t count, loff_t *ppos)
{
	char buf[32];
	ssize_t len;
	s8 shift;

	len = min(count, sizeof(buf) - 1);
	if (copy_from_user(buf, user_buf, len))
		return -EFAULT;

	buf[len] = '\0';
	if (kstrtos8(buf, 0, &shift))
		return -EINVAL;

	if (shift < -1 || shift >= BITS_PER_LONG)
		return -EINVAL;

	tlb_flushall_shift = shift;
	return count;
}

static const struct file_operations fops_tlbflush = {
	.read = tlbflush_read_file,
	.write = tlbflush_write_file,
	.llseek = default_llseek,
};

static int __init create_tlb_flushall_shift(void)
{
	if (cpu_has_invlpg) {
		debugfs_create_file("tlb_flushall_shift", S_IRUSR | S_IWUSR,
			arch_debugfs_dir, NULL, &fops_tlbflush);
	}
	return 0;
}
late_initcall(create_tlb_flushall_shift);
#endif
