/* 
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2000-2001 Silicon Graphics, Inc. All rights reserved.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <asm/sn/sn_sal.h>
#include <asm/sn/sn_cpuid.h>
#include <asm/processor.h>
#include <asm/pgtable.h>


extern	int autotest_enabled;
int	mcatest=0;



/* 
 * mcatest
 *	1 = expected MCA
 *	2 = unexpected MCA
 *	3 = expected MCA + unexpected MCA
 *	4 = INIT
 *	5 = speculative load to garbage memory address
 *	6 = speculative load with ld8.s (needs poison hack in PROM)
 *	7 = speculative load from mis-predicted branch (needs poison hack in PROM)
 */
static int __init set_mcatest(char *str)
{
	get_option(&str, &mcatest);
	return 1;
}

__setup("mcatest=", set_mcatest);

void
sgi_mcatest(void)
{
	if (mcatest == 1 || mcatest == 3) {
		long	*p, result, adrs[] = {0xc0000a000f021004UL, 0xc0000a000f026004UL, 0x800000000, 0x500000, 0};
		long	size[] = {1,2,4,8};
		int	r, i, j;
		p = (long*)0xc000000000000000UL;
		ia64_fc(p);
		*p = 0x0123456789abcdefL;
		for (i=0; i<5; i++) {
			for (j=0; j<4; j++) {
				printk("Probing 0x%lx, size %ld\n", adrs[i], size[j]);
				result = -1;
				r = ia64_sn_probe_io_slot (adrs[i], size[j], &result);
				printk("    status %d, val 0x%lx\n", r, result); 
			}
		}
	}
	if (mcatest == 2 || mcatest == 3) {
		void zzzmca(int, int, int);
		printk("About to cause unexpected MCA\n");
		zzzmca(mcatest, 0x32dead, 0x33dead);
	}
	if (mcatest == 4) {
		long	*p;
		int	delivery_mode = 5;
		printk("About to try to cause an INIT on cpu 0\n");
		p = (long*)((0xc0000a0000000000LL | ((long)get_nasid())<<33) | 0x1800080);
		*p = (delivery_mode << 8);
		udelay(10000);
		printk("Returned from INIT\n");
	}
	if (mcatest == 5) {
		int zzzspec(long);
		int	i;
		long	flags, dcr, res, val, addr=0xff00000000UL;

		dcr = ia64_get_dcr();
		for (i=0; i<5; i++) {
			printk("Default DCR: 0x%lx\n", ia64_get_dcr());
			printk("zzzspec: 0x%x\n", zzzspec(addr));
			ia64_set_dcr(0);
			printk("New     DCR: 0x%lx\n", ia64_get_dcr());
			printk("zzzspec: 0x%x\n", zzzspec(addr));
			ia64_set_dcr(dcr);
			res = ia64_sn_probe_io_slot(0xff00000000UL, 8, &val);
			printk("zzzspec: probe %ld, 0x%lx\n", res, val);
			ia64_clear_ic(flags);
			ia64_itc(0x2, 0xe00000ff00000000UL,
			          pte_val(mk_pte_phys(0xff00000000UL,
				  __pgprot(__DIRTY_BITS|_PAGE_PL_0|_PAGE_AR_RW))), _PAGE_SIZE_256M);
			local_irq_restore(flags);
			ia64_srlz_i ();
		}

	}
	if (mcatest == 6) {
		int zzzspec(long);
		int	i;
		long	dcr, addr=0xe000000008000000UL;

		dcr = ia64_get_dcr();
		for (i=0; i<5; i++) {
			printk("zzzspec: 0x%x\n", zzzspec(addr));
			ia64_set_dcr(0);
		}
		ia64_set_dcr(dcr);
	}
	if (mcatest == 7) {
		int zzzspec2(long, long);
		int	i;
		long	addr=0xe000000008000000UL;
		long	addr2=0xe000000007000000UL;

		for (i=0; i<5; i++) {
			printk("zzzspec2\n");
			zzzspec2(addr, addr2);
		}
	}
}
