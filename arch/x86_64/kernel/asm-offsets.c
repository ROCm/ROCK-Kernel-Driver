/*
 * Generate definitions needed by assembly language modules.
 * This code generates raw asm output which is post-processed to extract
 * and format the required data.
 */

#include <linux/sched.h> 
#include <linux/stddef.h>
#include <linux/errno.h> 
#include <asm/pda.h>
#include <asm/hardirq.h>
#include <asm/processor.h>
#include <asm/segment.h>
#include <asm/thread_info.h>

#define DEFINE(sym, val) \
        asm volatile("\n->" #sym " %0 " #val : : "i" (val))

#define BLANK() asm volatile("\n->" : : )

int main(void)
{
#define ENTRY(entry) DEFINE(tsk_ ## entry, offsetof(struct task_struct, entry))
	ENTRY(state);
	ENTRY(flags); 
	ENTRY(thread); 
	ENTRY(pid);
	BLANK();
#undef ENTRY
#define ENTRY(entry) DEFINE(threadinfo_ ## entry, offsetof(struct thread_info, entry))
	ENTRY(flags);
	ENTRY(addr_limit);
	ENTRY(preempt_count);
	BLANK();
#undef ENTRY
#define ENTRY(entry) DEFINE(pda_ ## entry, offsetof(struct x8664_pda, entry))
	ENTRY(kernelstack); 
	ENTRY(oldrsp); 
	ENTRY(pcurrent); 
	ENTRY(irqrsp);
	ENTRY(irqcount);
	ENTRY(cpunumber);
	ENTRY(irqstackptr);
	BLANK();
#undef ENTRY
	return 0;
}
