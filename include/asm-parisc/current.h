#ifndef _PARISC_CURRENT_H
#define _PARISC_CURRENT_H

#include <asm/processor.h>

struct task_struct;

static inline struct task_struct * get_current(void)
{
	struct task_struct *current;

	asm("copy 30,%0" : "=r" (current));
	
	return (struct task_struct *)((long) current & ~(THREAD_SIZE-1));
}
 
#define current get_current()

#endif /* !(_PARISC_CURRENT_H) */
