#include <linux/mm.h>
#include <asm/io.h>
#include <asm/mtrr.h>
#include <asm/msr.h>
#include <asm/system.h>
#include <asm/cpufeature.h>
#include "mtrr.h"


int generic_get_free_region(unsigned long base, unsigned long size)
/*  [SUMMARY] Get a free MTRR.
    <base> The starting (base) address of the region.
    <size> The size (in bytes) of the region.
    [RETURNS] The index of the region on success, else -1 on error.
*/
{
	int i, max;
	mtrr_type ltype;
	unsigned long lbase, lsize;

	max = num_var_ranges;
	for (i = 0; i < max; ++i) {
		mtrr_if->get(i, &lbase, &lsize, &ltype);
		if (lsize == 0)
			return i;
	}
	return -ENOSPC;
}


void generic_get_mtrr(unsigned int reg, unsigned long *base,
		      unsigned long *size, mtrr_type * type)
{
	unsigned long mask_lo, mask_hi, base_lo, base_hi;

	rdmsr(MTRRphysMask_MSR(reg), mask_lo, mask_hi);
	if ((mask_lo & 0x800) == 0) {
		/*  Invalid (i.e. free) range  */
		*base = 0;
		*size = 0;
		*type = 0;
		return;
	}

	rdmsr(MTRRphysBase_MSR(reg), base_lo, base_hi);

	/* Work out the shifted address mask. */
	mask_lo = size_or_mask | mask_hi << (32 - PAGE_SHIFT)
	    | mask_lo >> PAGE_SHIFT;

	/* This works correctly if size is a power of two, i.e. a
	   contiguous range. */
	*size = -mask_lo;
	*base = base_hi << (32 - PAGE_SHIFT) | base_lo >> PAGE_SHIFT;
	*type = base_lo & 0xff;
}

void generic_set_mtrr(unsigned int reg, unsigned long base,
		      unsigned long size, mtrr_type type)
/*  [SUMMARY] Set variable MTRR register on the local CPU.
    <reg> The register to set.
    <base> The base address of the region.
    <size> The size of the region. If this is 0 the region is disabled.
    <type> The type of the region.
    <do_safe> If TRUE, do the change safely. If FALSE, safety measures should
    be done externally.
    [RETURNS] Nothing.
*/
{
	u32 cr0, cr4 = 0;
	u32 deftype_lo, deftype_hi;
	static spinlock_t set_atomicity_lock = SPIN_LOCK_UNLOCKED;

	spin_lock(&set_atomicity_lock);
	/*  Save value of CR4 and clear Page Global Enable (bit 7)  */
	if ( cpu_has_pge ) {
		cr4 = read_cr4();
		write_cr4(cr4 & (unsigned char) ~(1 << 7));
	}

	/*  Disable and flush caches. Note that wbinvd flushes the TLBs as
	    a side-effect  */
	cr0 = read_cr0() | 0x40000000;
	wbinvd();
	write_cr0(cr0);
	wbinvd();

	/*  Save MTRR state */
	rdmsr(MTRRdefType_MSR, deftype_lo, deftype_hi);

	/*  Disable MTRRs, and set the default type to uncached  */
	wrmsr(MTRRdefType_MSR, deftype_lo & 0xf300UL, deftype_hi);

	if (size == 0) {
		/* The invalid bit is kept in the mask, so we simply clear the
		   relevant mask register to disable a range. */
		wrmsr(MTRRphysMask_MSR(reg), 0, 0);
	} else {
		wrmsr(MTRRphysBase_MSR(reg), base << PAGE_SHIFT | type,
		      (base & size_and_mask) >> (32 - PAGE_SHIFT));
		wrmsr(MTRRphysMask_MSR(reg), -size << PAGE_SHIFT | 0x800,
		      (-size & size_and_mask) >> (32 - PAGE_SHIFT));
	}

	/*  Flush caches and TLBs  */
	wbinvd();

	/* Intel (P6) standard MTRRs */
	wrmsr(MTRRdefType_MSR, deftype_lo, deftype_hi);
		
	/*  Enable caches  */
	write_cr0(read_cr0() & 0xbfffffff);

	/*  Restore value of CR4  */
	if ( cpu_has_pge )
		write_cr4(cr4);
	spin_unlock(&set_atomicity_lock);
}

int generic_validate_add_page(unsigned long base, unsigned long size, unsigned int type)
{
	unsigned long lbase, last;

	/*  For Intel PPro stepping <= 7, must be 4 MiB aligned 
	    and not touch 0x70000000->0x7003FFFF */
	if (is_cpu(INTEL) && boot_cpu_data.x86 == 6 &&
	    boot_cpu_data.x86_model == 1 &&
	    boot_cpu_data.x86_mask <= 7) {
		if (base & ((1 << (22 - PAGE_SHIFT)) - 1)) {
			printk(KERN_WARNING
			       "mtrr: base(0x%lx000) is not 4 MiB aligned\n",
			       base);
			return -EINVAL;
		}
		if (!(base + size < 0x70000000 || base > 0x7003FFFF) &&
		    (type == MTRR_TYPE_WRCOMB
		     || type == MTRR_TYPE_WRBACK)) {
			printk(KERN_WARNING
			       "mtrr: writable mtrr between 0x70000000 and 0x7003FFFF may hang the CPU.\n");
			return -EINVAL;
		}
	}

	if (base + size < 0x100) {
		printk(KERN_WARNING
		       "mtrr: cannot set region below 1 MiB (0x%lx000,0x%lx000)\n",
		       base, size);
		return -EINVAL;
	}
	/*  Check upper bits of base and last are equal and lower bits are 0
	    for base and 1 for last  */
	last = base + size - 1;
	for (lbase = base; !(lbase & 1) && (last & 1);
	     lbase = lbase >> 1, last = last >> 1) ;
	if (lbase != last) {
		printk(KERN_WARNING
		       "mtrr: base(0x%lx000) is not aligned on a size(0x%lx000) boundary\n",
		       base, size);
		return -EINVAL;
	}
	return 0;
}


int generic_have_wrcomb(void)
{
	unsigned long config, dummy;
	rdmsr(MTRRcap_MSR, config, dummy);
	return (config & (1 << 10));
}

int positive_have_wrcomb(void)
{
	return 1;
}

/* generic structure...
 */
struct mtrr_ops generic_mtrr_ops = {
	.use_intel_if      = 1,
	.init_secondary    = generic_init_secondary,
	.get               = generic_get_mtrr,
	.get_free_region   = generic_get_free_region,
	.set               = generic_set_mtrr,
	.validate_add_page = generic_validate_add_page,
	.have_wrcomb       = generic_have_wrcomb,
};
