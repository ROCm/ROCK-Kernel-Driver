#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/ctype.h>
#include <linux/module.h>
#include <linux/seq_file.h>
#include <asm/uaccess.h>
#include <linux/mutex.h>

#include <asm/mtrr.h>
#include "mtrr.h"

static DEFINE_MUTEX(mtrr_mutex);

void generic_get_mtrr(unsigned int reg, unsigned long *base,
		      unsigned long *size, mtrr_type * type)
{
	struct xen_platform_op op;

	op.cmd = XENPF_read_memtype;
	op.u.read_memtype.reg = reg;
	if (unlikely(HYPERVISOR_platform_op(&op)))
		memset(&op.u.read_memtype, 0, sizeof(op.u.read_memtype));

	*size = op.u.read_memtype.nr_mfns;
	*base = op.u.read_memtype.mfn;
	*type = op.u.read_memtype.type;
}

struct mtrr_ops generic_mtrr_ops = {
	.use_intel_if      = 1,
	.get               = generic_get_mtrr,
};

struct mtrr_ops *mtrr_if = &generic_mtrr_ops;
unsigned int num_var_ranges;
unsigned int mtrr_usage_table[MTRR_MAX_VAR_RANGES];

static u64 tom2;

static void __init set_num_var_ranges(void)
{
	struct xen_platform_op op;

	for (num_var_ranges = 0; ; num_var_ranges++) {
		op.cmd = XENPF_read_memtype;
		op.u.read_memtype.reg = num_var_ranges;
		if (HYPERVISOR_platform_op(&op) != 0)
			break;
	}
}

static void __init init_table(void)
{
	int i, max;

	max = num_var_ranges;
	for (i = 0; i < max; i++)
		mtrr_usage_table[i] = 0;
}

int mtrr_add_page(unsigned long base, unsigned long size, 
		  unsigned int type, bool increment)
{
	int error;
	struct xen_platform_op op;

	mutex_lock(&mtrr_mutex);

	op.cmd = XENPF_add_memtype;
	op.u.add_memtype.mfn     = base;
	op.u.add_memtype.nr_mfns = size;
	op.u.add_memtype.type    = type;
	error = HYPERVISOR_platform_op(&op);
	if (error) {
		mutex_unlock(&mtrr_mutex);
		BUG_ON(error > 0);
		return error;
	}

	if (increment)
		++mtrr_usage_table[op.u.add_memtype.reg];

	mutex_unlock(&mtrr_mutex);

	return op.u.add_memtype.reg;
}

static int mtrr_check(unsigned long base, unsigned long size)
{
	if ((base & (PAGE_SIZE - 1)) || (size & (PAGE_SIZE - 1))) {
		printk(KERN_WARNING
			"mtrr: size and base must be multiples of 4 kiB\n");
		printk(KERN_DEBUG
			"mtrr: size: 0x%lx  base: 0x%lx\n", size, base);
		dump_stack();
		return -1;
	}
	return 0;
}

int
mtrr_add(unsigned long base, unsigned long size, unsigned int type,
	 bool increment)
{
	if (mtrr_check(base, size))
		return -EINVAL;
	return mtrr_add_page(base >> PAGE_SHIFT, size >> PAGE_SHIFT, type,
			     increment);
}

int mtrr_del_page(int reg, unsigned long base, unsigned long size)
{
	unsigned i;
	mtrr_type ltype;
	unsigned long lbase, lsize;
	int error = -EINVAL;
	struct xen_platform_op op;

	mutex_lock(&mtrr_mutex);

	if (reg < 0) {
		/*  Search for existing MTRR  */
		for (i = 0; i < num_var_ranges; ++i) {
			mtrr_if->get(i, &lbase, &lsize, &ltype);
			if (lbase == base && lsize == size) {
				reg = i;
				break;
			}
		}
		if (reg < 0) {
			printk(KERN_DEBUG "mtrr: no MTRR for %lx000,%lx000 found\n", base,
			       size);
			goto out;
		}
	}
	if (mtrr_usage_table[reg] < 1) {
		printk(KERN_WARNING "mtrr: reg: %d has count=0\n", reg);
		goto out;
	}
	if (--mtrr_usage_table[reg] < 1) {
		op.cmd = XENPF_del_memtype;
		op.u.del_memtype.handle = 0;
		op.u.del_memtype.reg    = reg;
		error = HYPERVISOR_platform_op(&op);
		if (error) {
			BUG_ON(error > 0);
			goto out;
		}
	}
	error = reg;
 out:
	mutex_unlock(&mtrr_mutex);
	return error;
}

int
mtrr_del(int reg, unsigned long base, unsigned long size)
{
	if (mtrr_check(base, size))
		return -EINVAL;
	return mtrr_del_page(reg, base >> PAGE_SHIFT, size >> PAGE_SHIFT);
}

EXPORT_SYMBOL(mtrr_add);
EXPORT_SYMBOL(mtrr_del);

/*
 * Returns the effective MTRR type for the region
 * Error returns:
 * - 0xFE - when the range is "not entirely covered" by _any_ var range MTRR
 * - 0xFF - when MTRR is not enabled
 */
u8 mtrr_type_lookup(u64 start, u64 end)
{
	int i, error;
	u64 start_mfn, end_mfn, base_mfn, top_mfn;
	u8 prev_match, curr_match;
	struct xen_platform_op op;

	if (!is_initial_xendomain())
		return MTRR_TYPE_WRBACK;

	if (!num_var_ranges)
		return 0xFF;

	start_mfn = start >> PAGE_SHIFT;
	/* Make end inclusive end, instead of exclusive */
	end_mfn = --end >> PAGE_SHIFT;

	/* Look in fixed ranges. Just return the type as per start */
	if (start_mfn < 0x100) {
#if 0//todo
		op.cmd = XENPF_read_memtype;
		op.u.read_memtype.reg = ???;
		error = HYPERVISOR_platform_op(&op);
		if (!error)
			return op.u.read_memtype.type;
#endif
		return MTRR_TYPE_UNCACHABLE;
	}

	/*
	 * Look in variable ranges
	 * Look of multiple ranges matching this address and pick type
	 * as per MTRR precedence
	 */
	prev_match = 0xFF;
	for (i = 0; i < num_var_ranges; ++i) {
		op.cmd = XENPF_read_memtype;
		op.u.read_memtype.reg = i;
		error = HYPERVISOR_platform_op(&op);

		if (error || !op.u.read_memtype.nr_mfns)
			continue;

		base_mfn = op.u.read_memtype.mfn;
		top_mfn = base_mfn + op.u.read_memtype.nr_mfns - 1;

		if (base_mfn > end_mfn || start_mfn > top_mfn) {
			continue;
		}

		if (base_mfn > start_mfn || end_mfn > top_mfn) {
			return 0xFE;
		}

		curr_match = op.u.read_memtype.type;
		if (prev_match == 0xFF) {
			prev_match = curr_match;
			continue;
		}

		if (prev_match == MTRR_TYPE_UNCACHABLE ||
		    curr_match == MTRR_TYPE_UNCACHABLE) {
			return MTRR_TYPE_UNCACHABLE;
		}

		if ((prev_match == MTRR_TYPE_WRBACK &&
		     curr_match == MTRR_TYPE_WRTHROUGH) ||
		    (prev_match == MTRR_TYPE_WRTHROUGH &&
		     curr_match == MTRR_TYPE_WRBACK)) {
			prev_match = MTRR_TYPE_WRTHROUGH;
			curr_match = MTRR_TYPE_WRTHROUGH;
		}

		if (prev_match != curr_match) {
			return MTRR_TYPE_UNCACHABLE;
		}
	}

	if (tom2) {
		if (start >= (1ULL<<32) && (end < tom2))
			return MTRR_TYPE_WRBACK;
	}

	if (prev_match != 0xFF)
		return prev_match;

#if 0//todo
	op.cmd = XENPF_read_def_memtype;
	error = HYPERVISOR_platform_op(&op);
	if (!error)
		return op.u.read_def_memtype.type;
#endif
	return MTRR_TYPE_UNCACHABLE;
}

/*
 * Newer AMD K8s and later CPUs have a special magic MSR way to force WB
 * for memory >4GB. Check for that here.
 * Note this won't check if the MTRRs < 4GB where the magic bit doesn't
 * apply to are wrong, but so far we don't know of any such case in the wild.
 */
#define Tom2Enabled (1U << 21)
#define Tom2ForceMemTypeWB (1U << 22)

int __init amd_special_default_mtrr(void)
{
	u32 l, h;

	if (boot_cpu_data.x86_vendor != X86_VENDOR_AMD)
		return 0;
	if (boot_cpu_data.x86 < 0xf || boot_cpu_data.x86 > 0x11)
		return 0;
	/* In case some hypervisor doesn't pass SYSCFG through */
	if (rdmsr_safe(MSR_K8_SYSCFG, &l, &h) < 0)
		return 0;
	/*
	 * Memory between 4GB and top of mem is forced WB by this magic bit.
	 * Reserved before K8RevF, but should be zero there.
	 */
	if ((l & (Tom2Enabled | Tom2ForceMemTypeWB)) ==
		 (Tom2Enabled | Tom2ForceMemTypeWB))
		return 1;
	return 0;
}

void __init mtrr_bp_init(void)
{
	if (amd_special_default_mtrr()) {
		/* TOP_MEM2 */
		rdmsrl(MSR_K8_TOP_MEM2, tom2);
		tom2 &= 0xffffff8000000ULL;
	}
}

void mtrr_ap_init(void)
{
}

static int __init mtrr_init(void)
{
	struct cpuinfo_x86 *c = &boot_cpu_data;

	if (!is_initial_xendomain())
		return -ENODEV;

	if ((!cpu_has(c, X86_FEATURE_MTRR)) &&
	    (!cpu_has(c, X86_FEATURE_K6_MTRR)) &&
	    (!cpu_has(c, X86_FEATURE_CYRIX_ARR)) &&
	    (!cpu_has(c, X86_FEATURE_CENTAUR_MCR)))
		return -ENODEV;

	set_num_var_ranges();
	init_table();

	return 0;
}

subsys_initcall(mtrr_init);
