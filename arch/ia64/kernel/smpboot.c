/*
 * SMP Support
 *
 * Application processor startup code, moved from smp.c to better support kernel profile
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/smp.h>
#include <linux/kernel_stat.h>
#include <linux/mm.h>
#include <linux/delay.h>

#include <asm/atomic.h>
#include <asm/bitops.h>
#include <asm/current.h>
#include <asm/delay.h>
#include <asm/efi.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/pgalloc.h>
#include <asm/processor.h>
#include <asm/ptrace.h>
#include <asm/sal.h>
#include <asm/system.h>
#include <asm/unistd.h>

/* 
 * SAL shoves the AP's here when we start them.  Physical mode, no kernel TR, 
 * no RRs set, better than even chance that psr is bogus.  Fix all that and 
 * call _start.  In effect, pretend to be lilo.
 *
 * Stolen from lilo_start.c.  Thanks David! 
 */
void
start_ap(void)
{
	extern void _start (void);
	unsigned long flags;

	/*
	 * Install a translation register that identity maps the
	 * kernel's 256MB page(s).
	 */
	ia64_clear_ic(flags);
	ia64_set_rr(          0, (0x1000 << 8) | (_PAGE_SIZE_1M << 2));
	ia64_set_rr(PAGE_OFFSET, (ia64_rid(0, PAGE_OFFSET) << 8) | (_PAGE_SIZE_256M << 2));
	ia64_srlz_d();
	ia64_itr(0x3, 1, PAGE_OFFSET,
		 pte_val(mk_pte_phys(0, __pgprot(__DIRTY_BITS|_PAGE_PL_0|_PAGE_AR_RWX))),
		 _PAGE_SIZE_256M);
	ia64_srlz_i();

	flags = (IA64_PSR_IT | IA64_PSR_IC | IA64_PSR_DT | IA64_PSR_RT | IA64_PSR_DFH | 
		 IA64_PSR_BN);
	
	asm volatile ("movl r8 = 1f\n"
		      ";;\n"
		      "mov cr.ipsr=%0\n"
		      "mov cr.iip=r8\n" 
		      "mov cr.ifs=r0\n"
		      ";;\n"
		      "rfi;;"
		      "1:\n"
		      "movl r1 = __gp" :: "r"(flags) : "r8");
	_start();
}


