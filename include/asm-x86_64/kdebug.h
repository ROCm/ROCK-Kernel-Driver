#ifndef _X86_64_KDEBUG_H
#define _X86_64_KDEBUG_H 1

#include <linux/notifier.h>

struct pt_regs;

struct die_args { 
	struct pt_regs *regs;
	const char *str;
	long err; 
}; 

extern struct notifier_block *die_chain;

/* Grossly misnamed. */
enum die_val { 
	DIE_OOPS = 1,
	DIE_INT3,
	DIE_DEBUG,
	DIE_PANIC,
	DIE_NMI,
	DIE_DIE,
	DIE_CALL,
	DIE_CPUINIT,	/* not really a die, but .. */
	DIE_TRAPINIT,	/* not really a die, but .. */
	DIE_STOP, 
}; 
	
static inline int notify_die(enum die_val val,char *str,struct pt_regs *regs,long err)
{ 
	struct die_args args = { regs: regs, str: str, err: err }; 
	return notifier_call_chain(&die_chain, val, &args); 
} 

int printk_address(unsigned long address);

#endif
