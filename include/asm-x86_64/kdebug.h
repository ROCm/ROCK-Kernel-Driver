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

enum { 
	DIE_DIE = 1,
	DIE_INT3,
	DIE_DEBUG,
	DIE_PANIC,
}; 
	
#endif
