/*
 *  arch/ppc/platforms/iSeries_pic.c
 *
 */


#include <linux/ptrace.h>
#include <linux/errno.h>
#include <linux/threads.h>
#include <linux/kernel_stat.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/timex.h>
#include <linux/config.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/irq.h>
#include <linux/proc_fs.h>
#include <linux/random.h>

#include <asm/uaccess.h>
#include <asm/bitops.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/irq.h>
#include <asm/cache.h>
#include <asm/prom.h>
#include <asm/ptrace.h>
#include <asm/iSeries/LparData.h>
extern void iSeries_smp_message_recv( struct pt_regs * );
extern int is_soft_enabled(void);

extern void __no_lpq_restore_flags(unsigned long flags);
extern void iSeries_smp_message_recv( struct pt_regs * );
unsigned lpEvent_count = 0;

void do_IRQ(struct pt_regs *regs)
{
	int cpu = smp_processor_id();
	unsigned long flags;
	struct Paca * paca;
	struct ItLpQueue *lpq;

	if ( is_soft_enabled() )
		BUG();
	
	irq_enter();

	paca = (struct Paca *)mfspr(SPRG1);

#ifdef CONFIG_SMP
	if ( paca->xLpPacaPtr->xIpiCnt ) {
		paca->xLpPacaPtr->xIpiCnt = 0;
		iSeries_smp_message_recv( regs );
	}
#endif /* CONFIG_SMP */

	lpq = paca->lpQueuePtr;
	if ( lpq ) {
		local_irq_save(flags);
		lpEvent_count += ItLpQueue_process( lpq, regs );
		local_irq_restore( flags );
	}

	irq_exit();

	if ( paca->xLpPacaPtr->xDecrInt ) {
		paca->xLpPacaPtr->xDecrInt = 0;
		/* Signal a fake decrementer interrupt */
		timer_interrupt( regs );
	}
}
