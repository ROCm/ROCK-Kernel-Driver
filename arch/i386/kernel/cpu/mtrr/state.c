#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <asm/io.h>
#include <asm/mtrr.h>
#include <asm/msr.h>
#include "mtrr.h"

struct mtrr_state {
	struct mtrr_var_range *var_ranges;
	mtrr_type fixed_ranges[NUM_FIXED_RANGES];
	unsigned char enabled;
	mtrr_type def_type;
};

static unsigned long smp_changes_mask __initdata = 0;
struct mtrr_state mtrr_state = {};

static int __init set_fixed_ranges(mtrr_type * frs)
{
	unsigned long *p = (unsigned long *) frs;
	int changed = FALSE;
	int i;
	unsigned long lo, hi;

	rdmsr(MTRRfix64K_00000_MSR, lo, hi);
	if (p[0] != lo || p[1] != hi) {
		wrmsr(MTRRfix64K_00000_MSR, p[0], p[1]);
		changed = TRUE;
	}

	for (i = 0; i < 2; i++) {
		rdmsr(MTRRfix16K_80000_MSR + i, lo, hi);
		if (p[2 + i * 2] != lo || p[3 + i * 2] != hi) {
			wrmsr(MTRRfix16K_80000_MSR + i, p[2 + i * 2],
			      p[3 + i * 2]);
			changed = TRUE;
		}
	}

	for (i = 0; i < 8; i++) {
		rdmsr(MTRRfix4K_C0000_MSR + i, lo, hi);
		if (p[6 + i * 2] != lo || p[7 + i * 2] != hi) {
			wrmsr(MTRRfix4K_C0000_MSR + i, p[6 + i * 2],
			      p[7 + i * 2]);
			changed = TRUE;
		}
	}
	return changed;
}

/*  Set the MSR pair relating to a var range. Returns TRUE if
    changes are made  */
static int __init set_mtrr_var_ranges(unsigned int index, struct mtrr_var_range *vr)
{
	unsigned int lo, hi;
	int changed = FALSE;

	rdmsr(MTRRphysBase_MSR(index), lo, hi);
	if ((vr->base_lo & 0xfffff0ffUL) != (lo & 0xfffff0ffUL)
	    || (vr->base_hi & 0xfUL) != (hi & 0xfUL)) {
		wrmsr(MTRRphysBase_MSR(index), vr->base_lo, vr->base_hi);
		changed = TRUE;
	}

	rdmsr(MTRRphysMask_MSR(index), lo, hi);

	if ((vr->mask_lo & 0xfffff800UL) != (lo & 0xfffff800UL)
	    || (vr->mask_hi & 0xfUL) != (hi & 0xfUL)) {
		wrmsr(MTRRphysMask_MSR(index), vr->mask_lo, vr->mask_hi);
		changed = TRUE;
	}
	return changed;
}

static unsigned long set_mtrr_state(u32 deftype_lo, u32 deftype_hi)
/*  [SUMMARY] Set the MTRR state for this CPU.
    <state> The MTRR state information to read.
    <ctxt> Some relevant CPU context.
    [NOTE] The CPU must already be in a safe state for MTRR changes.
    [RETURNS] 0 if no changes made, else a mask indication what was changed.
*/
{
	unsigned int i;
	unsigned long change_mask = 0;

	for (i = 0; i < num_var_ranges; i++)
		if (set_mtrr_var_ranges(i, &mtrr_state.var_ranges[i]))
			change_mask |= MTRR_CHANGE_MASK_VARIABLE;

	if (set_fixed_ranges(mtrr_state.fixed_ranges))
		change_mask |= MTRR_CHANGE_MASK_FIXED;

	/*  Set_mtrr_restore restores the old value of MTRRdefType,
	   so to set it we fiddle with the saved value  */
	if ((deftype_lo & 0xff) != mtrr_state.def_type
	    || ((deftype_lo & 0xc00) >> 10) != mtrr_state.enabled) {
		deftype_lo |= (mtrr_state.def_type | mtrr_state.enabled << 10);
		change_mask |= MTRR_CHANGE_MASK_DEFTYPE;
	}

	return change_mask;
}


/*  Some BIOS's are fucked and don't set all MTRRs the same!  */
static void __init mtrr_state_warn(void)
{
	unsigned long mask = smp_changes_mask;
	if (!mask)
		return;
	if (mask & MTRR_CHANGE_MASK_FIXED)
		printk
		    ("mtrr: your CPUs had inconsistent fixed MTRR settings\n");
	if (mask & MTRR_CHANGE_MASK_VARIABLE)
		printk
		    ("mtrr: your CPUs had inconsistent variable MTRR settings\n");
	if (mask & MTRR_CHANGE_MASK_DEFTYPE)
		printk
		    ("mtrr: your CPUs had inconsistent MTRRdefType settings\n");
	printk("mtrr: probably your BIOS does not setup all CPUs\n");
}

/*  Free resources associated with a struct mtrr_state  */
static void __init finalize_mtrr_state(void)
{
	if (mtrr_state.var_ranges)
		kfree(mtrr_state.var_ranges);
	mtrr_state.var_ranges = NULL;
}

/*  Get the MSR pair relating to a var range  */
static void __init
get_mtrr_var_range(unsigned int index, struct mtrr_var_range *vr)
{
	rdmsr(MTRRphysBase_MSR(index), vr->base_lo, vr->base_hi);
	rdmsr(MTRRphysMask_MSR(index), vr->mask_lo, vr->mask_hi);
}

static void __init
get_fixed_ranges(mtrr_type * frs)
{
	unsigned long *p = (unsigned long *) frs;
	int i;

	rdmsr(MTRRfix64K_00000_MSR, p[0], p[1]);

	for (i = 0; i < 2; i++)
		rdmsr(MTRRfix16K_80000_MSR + i, p[2 + i * 2], p[3 + i * 2]);
	for (i = 0; i < 8; i++)
		rdmsr(MTRRfix4K_C0000_MSR + i, p[6 + i * 2], p[7 + i * 2]);
}

/*  Grab all of the MTRR state for this CPU into *state  */
void get_mtrr_state(void)
{
	unsigned int i;
	struct mtrr_var_range *vrs;
	unsigned long lo, dummy;

	if (!mtrr_state.var_ranges) {
		mtrr_state.var_ranges = kmalloc(num_var_ranges * sizeof (struct mtrr_var_range), 
						GFP_KERNEL);
		if (!mtrr_state.var_ranges)
			return;
	} 
	vrs = mtrr_state.var_ranges;

	for (i = 0; i < num_var_ranges; i++)
		get_mtrr_var_range(i, &vrs[i]);
	get_fixed_ranges(mtrr_state.fixed_ranges);

	rdmsr(MTRRdefType_MSR, lo, dummy);
	mtrr_state.def_type = (lo & 0xff);
	mtrr_state.enabled = (lo & 0xc00) >> 10;
}


/*  Put the processor into a state where MTRRs can be safely set  */
void set_mtrr_prepare_save(struct set_mtrr_context *ctxt)
{
	unsigned int cr0;

	/*  Disable interrupts locally  */
	local_irq_save(ctxt->flags);

	if (use_intel() || is_cpu(CYRIX)) {

		/*  Save value of CR4 and clear Page Global Enable (bit 7)  */
		if ( cpu_has_pge ) {
			ctxt->cr4val = read_cr4();
			write_cr4(ctxt->cr4val & (unsigned char) ~(1 << 7));
		}

		/*  Disable and flush caches. Note that wbinvd flushes the TLBs as
		    a side-effect  */
		cr0 = read_cr0() | 0x40000000;
		wbinvd();
		write_cr0(cr0);
		wbinvd();

		if (use_intel())
			/*  Save MTRR state */
			rdmsr(MTRRdefType_MSR, ctxt->deftype_lo, ctxt->deftype_hi);
		else
			/* Cyrix ARRs - everything else were excluded at the top */
			ctxt->ccr3 = getCx86(CX86_CCR3);
	}
}

void set_mtrr_cache_disable(struct set_mtrr_context *ctxt)
{
	if (use_intel()) 
		/*  Disable MTRRs, and set the default type to uncached  */
		wrmsr(MTRRdefType_MSR, ctxt->deftype_lo & 0xf300UL,
		      ctxt->deftype_hi);
	else if (is_cpu(CYRIX))
		/* Cyrix ARRs - everything else were excluded at the top */
		setCx86(CX86_CCR3, (ctxt->ccr3 & 0x0f) | 0x10);
}

/*  Restore the processor after a set_mtrr_prepare  */
void set_mtrr_done(struct set_mtrr_context *ctxt)
{
	if (use_intel() || is_cpu(CYRIX)) {

		/*  Flush caches and TLBs  */
		wbinvd();

		/*  Restore MTRRdefType  */
		if (use_intel())
			/* Intel (P6) standard MTRRs */
			wrmsr(MTRRdefType_MSR, ctxt->deftype_lo, ctxt->deftype_hi);
		else
			/* Cyrix ARRs - everything else was excluded at the top */
			setCx86(CX86_CCR3, ctxt->ccr3);
		
		/*  Enable caches  */
		write_cr0(read_cr0() & 0xbfffffff);

		/*  Restore value of CR4  */
		if ( cpu_has_pge )
			write_cr4(ctxt->cr4val);
	}
	/*  Re-enable interrupts locally (if enabled previously)  */
	local_irq_restore(ctxt->flags);
}

void __init generic_init_secondary(void)
{
	u32 cr0, cr4 = 0;
	u32 deftype_lo, deftype_hi;
	unsigned long mask, count;

	/*  Note that this is not ideal, since the cache is only flushed/disabled
	   for this CPU while the MTRRs are changed, but changing this requires
	   more invasive changes to the way the kernel boots  */

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

	/* Actually set the state */
	mask = set_mtrr_state(deftype_lo,deftype_hi);

	/*  Flush caches and TLBs  */
	wbinvd();

	/* Intel (P6) standard MTRRs */
	wrmsr(MTRRdefType_MSR, deftype_lo, deftype_hi);
		
	/*  Enable caches  */
	write_cr0(read_cr0() & 0xbfffffff);

	/*  Restore value of CR4  */
	if ( cpu_has_pge )
		write_cr4(cr4);

	/*  Use the atomic bitops to update the global mask  */
	for (count = 0; count < sizeof mask * 8; ++count) {
		if (mask & 0x01)
			set_bit(count, &smp_changes_mask);
		mask >>= 1;
	}
}

/**
 * mtrr_init_secondary - setup AP MTRR state
 * 
 * Yes, this code is exactly the same as the set_mtrr code, except for the 
 * piece in the middle - you set all the ranges at once, instead of one
 * register at a time.
 * Shoot me.
 */
void __init mtrr_init_secondary_cpu(void)
{
	unsigned long flags;

	if (!mtrr_if || !mtrr_if->init_secondary) {
		/* I see no MTRRs I can support in SMP mode... */
		printk("mtrr: SMP support incomplete for this vendor\n");
		return;
	}

	local_irq_save(flags);
	mtrr_if->init_secondary();
	local_irq_restore(flags);
}

/**
 * mtrr_final_init  - finalize initialization sequence.
 */
static int __init mtrr_finalize_state(void)
{
	if (use_intel()) {
		finalize_mtrr_state();
		mtrr_state_warn();
	}
	return 0;
}

arch_initcall(mtrr_finalize_state);

