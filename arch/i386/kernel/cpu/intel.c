#include <linux/config.h>
#include <linux/init.h>
#include <linux/kernel.h>

#include <linux/string.h>
#include <linux/bitops.h>
#include <linux/smp.h>
#include <linux/thread_info.h>

#include <asm/processor.h>
#include <asm/msr.h>
#include <asm/uaccess.h>

#include "cpu.h"

#ifdef CONFIG_X86_LOCAL_APIC
#include <asm/mpspec.h>
#include <asm/apic.h>
#include <mach_apic.h>
#endif

extern int trap_init_f00f_bug(void);

#ifdef CONFIG_X86_INTEL_USERCOPY
/*
 * Alignment at which movsl is preferred for bulk memory copies.
 */
struct movsl_mask movsl_mask;
#endif

void __init early_intel_workaround(struct cpuinfo_x86 *c)
{
	if (c->x86_vendor != X86_VENDOR_INTEL)
		return;
	/* Netburst reports 64 bytes clflush size, but does IO in 128 bytes */
	if (c->x86 == 15 && c->x86_cache_alignment == 64)
		c->x86_cache_alignment = 128;
}

/*
 *	Early probe support logic for ppro memory erratum #50
 *
 *	This is called before we do cpu ident work
 */
 
int __init ppro_with_ram_bug(void)
{
	/* Uses data from early_cpu_detect now */
	if (boot_cpu_data.x86_vendor == X86_VENDOR_INTEL &&
	    boot_cpu_data.x86 == 6 &&
	    boot_cpu_data.x86_model == 1 &&
	    boot_cpu_data.x86_mask < 8) {
		printk(KERN_INFO "Pentium Pro with Errata#50 detected. Taking evasive action.\n");
		return 1;
	}
	return 0;
}
	
#define LVL_1_INST	1
#define LVL_1_DATA	2
#define LVL_2		3
#define LVL_3		4
#define LVL_TRACE	5

struct _cache_table
{
	unsigned char descriptor;
	char cache_type;
	short size;
};

/* all the cache descriptor types we care about (no TLB or trace cache entries) */
static struct _cache_table cache_table[] __initdata =
{
	{ 0x06, LVL_1_INST, 8 },
	{ 0x08, LVL_1_INST, 16 },
	{ 0x0a, LVL_1_DATA, 8 },
	{ 0x0c, LVL_1_DATA, 16 },
	{ 0x22, LVL_3,      512 },
	{ 0x23, LVL_3,      1024 },
	{ 0x25, LVL_3,      2048 },
	{ 0x29, LVL_3,      4096 },
	{ 0x2c, LVL_1_DATA, 32 },
	{ 0x30, LVL_1_INST, 32 },
	{ 0x39, LVL_2,      128 },
	{ 0x3b, LVL_2,      128 },
	{ 0x3c, LVL_2,      256 },
	{ 0x41, LVL_2,      128 },
	{ 0x42, LVL_2,      256 },
	{ 0x43, LVL_2,      512 },
	{ 0x44, LVL_2,      1024 },
	{ 0x45, LVL_2,      2048 },
	{ 0x66, LVL_1_DATA, 8 },
	{ 0x67, LVL_1_DATA, 16 },
	{ 0x68, LVL_1_DATA, 32 },
	{ 0x70, LVL_TRACE,  12 },
	{ 0x71, LVL_TRACE,  16 },
	{ 0x72, LVL_TRACE,  32 },
	{ 0x79, LVL_2,      128 },
	{ 0x7a, LVL_2,      256 },
	{ 0x7b, LVL_2,      512 },
	{ 0x7c, LVL_2,      1024 },
	{ 0x82, LVL_2,      256 },
	{ 0x83, LVL_2,      512 },
	{ 0x84, LVL_2,      1024 },
	{ 0x85, LVL_2,      2048 },
	{ 0x86, LVL_2,      512 },
	{ 0x87, LVL_2,      1024 },
	{ 0x00, 0, 0}
};

/*
 * P4 Xeon errata 037 workaround.
 * Hardware prefetcher may cause stale data to be loaded into the cache.
 */
static void __init Intel_errata_workarounds(struct cpuinfo_x86 *c)
{
	unsigned long lo, hi;

	if ((c->x86 == 15) && (c->x86_model == 1) && (c->x86_mask == 1)) {
		rdmsr (MSR_IA32_MISC_ENABLE, lo, hi);
		if ((lo & (1<<9)) == 0) {
			printk (KERN_INFO "CPU: C0 stepping P4 Xeon detected.\n");
			printk (KERN_INFO "CPU: Disabling hardware prefetching (Errata 037)\n");
			lo |= (1<<9);	/* Disable hw prefetching */
			wrmsr (MSR_IA32_MISC_ENABLE, lo, hi);
		}
	}
}


static void __init init_intel(struct cpuinfo_x86 *c)
{
	char *p = NULL;
	unsigned int trace = 0, l1i = 0, l1d = 0, l2 = 0, l3 = 0; /* Cache sizes */

#ifdef CONFIG_X86_F00F_BUG
	/*
	 * All current models of Pentium and Pentium with MMX technology CPUs
	 * have the F0 0F bug, which lets nonprivileged users lock up the system.
	 * Note that the workaround only should be initialized once...
	 */
	c->f00f_bug = 0;
	if ( c->x86 == 5 ) {
		static int f00f_workaround_enabled = 0;

		c->f00f_bug = 1;
		if ( !f00f_workaround_enabled ) {
			trap_init_f00f_bug();
			printk(KERN_NOTICE "Intel Pentium with F0 0F bug - workaround enabled.\n");
			f00f_workaround_enabled = 1;
		}
	}
#endif

	select_idle_routine(c);
	if (c->cpuid_level > 1) {
		/* supports eax=2  call */
		int i, j, n;
		int regs[4];
		unsigned char *dp = (unsigned char *)regs;

		/* Number of times to iterate */
		n = cpuid_eax(2) & 0xFF;

		for ( i = 0 ; i < n ; i++ ) {
			cpuid(2, &regs[0], &regs[1], &regs[2], &regs[3]);
			
			/* If bit 31 is set, this is an unknown format */
			for ( j = 0 ; j < 3 ; j++ ) {
				if ( regs[j] < 0 ) regs[j] = 0;
			}

			/* Byte 0 is level count, not a descriptor */
			for ( j = 1 ; j < 16 ; j++ ) {
				unsigned char des = dp[j];
				unsigned char k = 0;

				/* look up this descriptor in the table */
				while (cache_table[k].descriptor != 0)
				{
					if (cache_table[k].descriptor == des) {
						switch (cache_table[k].cache_type) {
						case LVL_1_INST:
							l1i += cache_table[k].size;
							break;
						case LVL_1_DATA:
							l1d += cache_table[k].size;
							break;
						case LVL_2:
							l2 += cache_table[k].size;
							break;
						case LVL_3:
							l3 += cache_table[k].size;
							break;
						case LVL_TRACE:
							trace += cache_table[k].size;
							break;
						}

						break;
					}

					k++;
				}
			}
		}

		if ( trace )
			printk (KERN_INFO "CPU: Trace cache: %dK uops", trace);
		else if ( l1i )
			printk (KERN_INFO "CPU: L1 I cache: %dK", l1i);
		if ( l1d )
			printk(", L1 D cache: %dK\n", l1d);
		else
			printk("\n");
		if ( l2 )
			printk(KERN_INFO "CPU: L2 cache: %dK\n", l2);
		if ( l3 )
			printk(KERN_INFO "CPU: L3 cache: %dK\n", l3);

		/*
		 * This assumes the L3 cache is shared; it typically lives in
		 * the northbridge.  The L1 caches are included by the L2
		 * cache, and so should not be included for the purpose of
		 * SMP switching weights.
		 */
		c->x86_cache_size = l2 ? l2 : (l1i+l1d);
	}

	/* SEP CPUID bug: Pentium Pro reports SEP but doesn't have it until model 3 mask 3 */
	if ((c->x86<<8 | c->x86_model<<4 | c->x86_mask) < 0x633)
		clear_bit(X86_FEATURE_SEP, c->x86_capability);

	/* Names for the Pentium II/Celeron processors 
	   detectable only by also checking the cache size.
	   Dixon is NOT a Celeron. */
	if (c->x86 == 6) {
		switch (c->x86_model) {
		case 5:
			if (c->x86_mask == 0) {
				if (l2 == 0)
					p = "Celeron (Covington)";
				else if (l2 == 256)
					p = "Mobile Pentium II (Dixon)";
			}
			break;
			
		case 6:
			if (l2 == 128)
				p = "Celeron (Mendocino)";
			else if (c->x86_mask == 0 || c->x86_mask == 5)
				p = "Celeron-A";
			break;
			
		case 8:
			if (l2 == 128)
				p = "Celeron (Coppermine)";
			break;
		}
	}

	if ( p )
		strcpy(c->x86_model_id, p);
	
#ifdef CONFIG_X86_HT
	if (cpu_has(c, X86_FEATURE_HT)) {
		extern	int phys_proc_id[NR_CPUS];
		
		u32 	eax, ebx, ecx, edx;
		int 	index_lsb, index_msb, tmp;
		int 	cpu = smp_processor_id();

		cpuid(1, &eax, &ebx, &ecx, &edx);
		smp_num_siblings = (ebx & 0xff0000) >> 16;

		if (smp_num_siblings == 1) {
			printk(KERN_INFO  "CPU: Hyper-Threading is disabled\n");
		} else if (smp_num_siblings > 1 ) {
			index_lsb = 0;
			index_msb = 31;

			if (smp_num_siblings > NR_CPUS) {
				printk(KERN_WARNING "CPU: Unsupported number of the siblings %d", smp_num_siblings);
				smp_num_siblings = 1;
				goto too_many_siblings;
			}
			tmp = smp_num_siblings;
			while ((tmp & 1) == 0) {
				tmp >>=1 ;
				index_lsb++;
			}
			tmp = smp_num_siblings;
			while ((tmp & 0x80000000 ) == 0) {
				tmp <<=1 ;
				index_msb--;
			}
			if (index_lsb != index_msb )
				index_msb++;
			phys_proc_id[cpu] = phys_pkg_id((ebx >> 24) & 0xFF, index_msb);

			printk(KERN_INFO  "CPU: Physical Processor ID: %d\n",
                               phys_proc_id[cpu]);
		}

	}
too_many_siblings:

#endif

	/* Work around errata */
	Intel_errata_workarounds(c);

#ifdef CONFIG_X86_INTEL_USERCOPY
	/*
	 * Set up the preferred alignment for movsl bulk memory moves
	 */
	switch (c->x86) {
	case 4:		/* 486: untested */
		break;
	case 5:		/* Old Pentia: untested */
		break;
	case 6:		/* PII/PIII only like movsl with 8-byte alignment */
		movsl_mask.mask = 7;
		break;
	case 15:	/* P4 is OK down to 8-byte alignment */
		movsl_mask.mask = 7;
		break;
	}
#endif

	if (c->x86 == 15) 
		set_bit(X86_FEATURE_P4, c->x86_capability);
	if (c->x86 == 6) 
		set_bit(X86_FEATURE_P3, c->x86_capability);
}


static unsigned int intel_size_cache(struct cpuinfo_x86 * c, unsigned int size)
{
	/* Intel PIII Tualatin. This comes in two flavours.
	 * One has 256kb of cache, the other 512. We have no way
	 * to determine which, so we use a boottime override
	 * for the 512kb model, and assume 256 otherwise.
	 */
	if ((c->x86 == 6) && (c->x86_model == 11) && (size == 0))
		size = 256;
	return size;
}

static struct cpu_dev intel_cpu_dev __initdata = {
	.c_vendor	= "Intel",
	.c_ident 	= { "GenuineIntel" },
	.c_models = {
		{ .vendor = X86_VENDOR_INTEL, .family = 4, .model_names = 
		  { 
			  [0] = "486 DX-25/33", 
			  [1] = "486 DX-50", 
			  [2] = "486 SX", 
			  [3] = "486 DX/2", 
			  [4] = "486 SL", 
			  [5] = "486 SX/2", 
			  [7] = "486 DX/2-WB", 
			  [8] = "486 DX/4", 
			  [9] = "486 DX/4-WB"
		  }
		},
		{ .vendor = X86_VENDOR_INTEL, .family = 5, .model_names =
		  { 
			  [0] = "Pentium 60/66 A-step", 
			  [1] = "Pentium 60/66", 
			  [2] = "Pentium 75 - 200",
			  [3] = "OverDrive PODP5V83", 
			  [4] = "Pentium MMX",
			  [7] = "Mobile Pentium 75 - 200", 
			  [8] = "Mobile Pentium MMX"
		  }
		},
		{ .vendor = X86_VENDOR_INTEL, .family = 6, .model_names =
		  { 
			  [0] = "Pentium Pro A-step",
			  [1] = "Pentium Pro", 
			  [3] = "Pentium II (Klamath)", 
			  [4] = "Pentium II (Deschutes)", 
			  [5] = "Pentium II (Deschutes)", 
			  [6] = "Mobile Pentium II",
			  [7] = "Pentium III (Katmai)", 
			  [8] = "Pentium III (Coppermine)", 
			  [10] = "Pentium III (Cascades)",
			  [11] = "Pentium III (Tualatin)",
		  }
		},
		{ .vendor = X86_VENDOR_INTEL, .family = 15, .model_names =
		  {
			  [0] = "Pentium 4 (Unknown)",
			  [1] = "Pentium 4 (Willamette)",
			  [2] = "Pentium 4 (Northwood)",
			  [4] = "Pentium 4 (Foster)",
			  [5] = "Pentium 4 (Foster)",
		  }
		},
	},
	.c_init		= init_intel,
	.c_identify	= generic_identify,
	.c_size_cache	= intel_size_cache,
};

__init int intel_cpu_init(void)
{
	cpu_devs[X86_VENDOR_INTEL] = &intel_cpu_dev;
	return 0;
}

// arch_initcall(intel_cpu_init);

