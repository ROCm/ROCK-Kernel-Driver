#define DEBUG

#include <linux/uaccess.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/init.h>

#include <asm/mtrr.h>
#include <asm/pat.h>
#include "mtrr.h"

/* arch_phys_wc_add returns an MTRR register index plus this offset. */
#define MTRR_TO_PHYS_WC_OFFSET 1000

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

const struct mtrr_ops generic_mtrr_ops = {
	.use_intel_if      = 1,
	.get               = generic_get_mtrr,
};

const struct mtrr_ops *mtrr_if = &generic_mtrr_ops;
unsigned int num_var_ranges;
static bool __mtrr_enabled;

static bool mtrr_enabled(void)
{
	return __mtrr_enabled;
}

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

	if (!mtrr_enabled())
		return -ENXIO;

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
		pr_warning("mtrr: size and base must be multiples of 4 kiB\n");
		pr_debug("mtrr: size: 0x%lx  base: 0x%lx\n", size, base);
		dump_stack();
		return -1;
	}
	return 0;
}

int mtrr_add(unsigned long base, unsigned long size, unsigned int type,
	     bool increment)
{
	if (!mtrr_enabled())
		return -ENODEV;
	if (mtrr_check(base, size))
		return -EINVAL;
	return mtrr_add_page(base >> PAGE_SHIFT, size >> PAGE_SHIFT, type,
			     increment);
}
EXPORT_SYMBOL(mtrr_add);

int mtrr_del_page(int reg, unsigned long base, unsigned long size)
{
	unsigned i;
	mtrr_type ltype;
	unsigned long lbase, lsize;
	int error = -EINVAL;
	struct xen_platform_op op;

	if (!mtrr_enabled())
		return -ENODEV;

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
			pr_debug("mtrr: no MTRR for %lx000,%lx000 found\n",
				 base, size);
			goto out;
		}
	}
	if (mtrr_usage_table[reg] < 1) {
		pr_warning("mtrr: reg: %d has count=0\n", reg);
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

int mtrr_del(int reg, unsigned long base, unsigned long size)
{
	if (!mtrr_enabled())
		return -ENODEV;
	if (mtrr_check(base, size))
		return -EINVAL;
	return mtrr_del_page(reg, base >> PAGE_SHIFT, size >> PAGE_SHIFT);
}
EXPORT_SYMBOL(mtrr_del);

/**
 * arch_phys_wc_add - add a WC MTRR and handle errors if PAT is unavailable
 * @base: Physical base address
 * @size: Size of region
 *
 * If PAT is available, this does nothing.  If PAT is unavailable, it
 * attempts to add a WC MTRR covering size bytes starting at base and
 * logs an error if this fails.
 *
 * The called should provide a power of two size on an equivalent
 * power of two boundary.
 *
 * Drivers must store the return value to pass to mtrr_del_wc_if_needed,
 * but drivers should not try to interpret that return value.
 */
int arch_phys_wc_add(unsigned long base, unsigned long size)
{
	int ret;

	if (pat_enabled() || !mtrr_enabled())
		return 0;  /* Success!  (We don't need to do anything.) */

	ret = mtrr_add(base, size, MTRR_TYPE_WRCOMB, true);
	if (ret < 0) {
		pr_warn("Failed to add WC MTRR for [%p-%p]; performance may suffer.",
			(void *)base, (void *)(base + size - 1));
		return ret;
	}
	return ret + MTRR_TO_PHYS_WC_OFFSET;
}
EXPORT_SYMBOL(arch_phys_wc_add);

/*
 * arch_phys_wc_del - undoes arch_phys_wc_add
 * @handle: Return value from arch_phys_wc_add
 *
 * This cleans up after mtrr_add_wc_if_needed.
 *
 * The API guarantees that mtrr_del_wc_if_needed(error code) and
 * mtrr_del_wc_if_needed(0) do nothing.
 */
void arch_phys_wc_del(int handle)
{
	if (handle >= 1) {
		WARN_ON(handle < MTRR_TO_PHYS_WC_OFFSET);
		mtrr_del(handle - MTRR_TO_PHYS_WC_OFFSET, 0, 0);
	}
}
EXPORT_SYMBOL(arch_phys_wc_del);

/*
 * arch_phys_wc_index - translates arch_phys_wc_add's return value
 * @handle: Return value from arch_phys_wc_add
 *
 * This will turn the return value from arch_phys_wc_add into an mtrr
 * index suitable for debugging.
 *
 * Note: There is no legitimate use for this function, except possibly
 * in printk line.  Alas there is an illegitimate use in some ancient
 * drm ioctls.
 */
int arch_phys_wc_index(int handle)
{
	if (handle < MTRR_TO_PHYS_WC_OFFSET)
		return -1;
	else
		return handle - MTRR_TO_PHYS_WC_OFFSET;
}
EXPORT_SYMBOL_GPL(arch_phys_wc_index);

/**
 * mtrr_type_lookup - look up memory type in MTRR
 *
 * Return Values:
 * MTRR_TYPE_(type)  - The effective MTRR type for the region
 * MTRR_TYPE_INVALID - MTRR is disabled
 *
 * Output Argument:
 * uniform - Set to 1 when an MTRR covers the region uniformly, i.e. the
 *	     region is fully covered by a single MTRR entry or the default
 *	     type.
 */
u8 mtrr_type_lookup(u64 start, u64 end, u8 *uniform)
{
	int i, error;
	u64 start_mfn, end_mfn, base_mfn, top_mfn;
	u8 prev_match, curr_match;
	struct xen_platform_op op;

	*uniform = 1;
	if (!is_initial_xendomain())
		return MTRR_TYPE_WRBACK;

	if (!num_var_ranges)
		return MTRR_TYPE_INVALID;

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
		*uniform = 0;
		return MTRR_TYPE_UNCACHABLE;
	}

	/*
	 * Look in variable ranges
	 * Look of multiple ranges matching this address and pick type
	 * as per MTRR precedence
	 */
	prev_match = MTRR_TYPE_INVALID;
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

		if (base_mfn > start_mfn || end_mfn > top_mfn)
			*uniform = 0;

		curr_match = op.u.read_memtype.type;
		if (prev_match == MTRR_TYPE_INVALID) {
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

	if (prev_match != MTRR_TYPE_INVALID)
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

static int __init _amd_special_default_mtrr(void)
{
	u32 l, h;

	if (!is_initial_xendomain())
		return 0;
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
	if (_amd_special_default_mtrr()) {
		/* TOP_MEM2 */
		rdmsrl(MSR_K8_TOP_MEM2, tom2);
		tom2 &= 0xffffff8000000ULL;
	}

	if (!mtrr_enabled())
		pr_info("MTRR: Disabled\n");
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
