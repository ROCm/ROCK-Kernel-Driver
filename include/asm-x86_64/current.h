#ifndef _X86_64_CURRENT_H
#define _X86_64_CURRENT_H

#if !defined(__ASSEMBLY__) 
struct task_struct;

#include <asm/pda.h>

static inline struct task_struct *get_current(void) 
{ 
	struct task_struct *t = read_pda(pcurrent); 
	return t;
} 


#define stack_current() \
({								\
	struct thread_info *ti;					\
	__asm__("andq %%rsp,%0; ":"=r" (ti) : "0" (~8191UL));	\
	ti->task;					\
})


#define current get_current()

#else

#ifndef ASM_OFFSET_H
#include <asm/offset.h> 
#endif

#define GET_CURRENT(reg) movq %gs:(pda_pcurrent),reg

#endif

#endif /* !(_X86_64_CURRENT_H) */
