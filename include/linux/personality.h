#ifndef _PERSONALITY_H
#define _PERSONALITY_H

#include <linux/linkage.h>
#include <linux/ptrace.h>
#include <asm/current.h>

/* Flags for bug emulation. These occupy the top three bytes. */
#define STICKY_TIMEOUTS		0x4000000
#define WHOLE_SECONDS		0x2000000
#define ADDR_LIMIT_32BIT	0x0800000

/* Personality types. These go in the low byte. Avoid using the top bit,
 * it will conflict with error returns.
 */
#define PER_MASK		(0x00ff)
#define PER_LINUX		(0x0000)
#define PER_LINUX_32BIT		(0x0000 | ADDR_LIMIT_32BIT)
#define PER_SVR4		(0x0001 | STICKY_TIMEOUTS)
#define PER_SVR3		(0x0002 | STICKY_TIMEOUTS)
#define PER_SCOSVR3		(0x0003 | STICKY_TIMEOUTS | WHOLE_SECONDS)
#define PER_WYSEV386		(0x0004 | STICKY_TIMEOUTS)
#define PER_ISCR4		(0x0005 | STICKY_TIMEOUTS)
#define PER_BSD			(0x0006)
#define PER_SUNOS		(PER_BSD | STICKY_TIMEOUTS)
#define PER_XENIX		(0x0007 | STICKY_TIMEOUTS)
#define PER_LINUX32		(0x0008)
#define PER_IRIX32              (0x0009 | STICKY_TIMEOUTS) /* IRIX5 32-bit     */
#define PER_IRIXN32             (0x000a | STICKY_TIMEOUTS) /* IRIX6 new 32-bit */
#define PER_IRIX64              (0x000b | STICKY_TIMEOUTS) /* IRIX6 64-bit     */
#define PER_RISCOS		(0x000c)
#define PER_SOLARIS		(0x000d | STICKY_TIMEOUTS)

/* Prototype for an lcall7 syscall handler. */
typedef void (*lcall7_func)(int, struct pt_regs *);


/* Description of an execution domain - personality range supported,
 * lcall7 syscall handler, start up / shut down functions etc.
 * N.B. The name and lcall7 handler must be where they are since the
 * offset of the handler is hard coded in kernel/sys_call.S.
 */
struct exec_domain {
	const char *name;
	lcall7_func handler;
	unsigned char pers_low, pers_high;
	unsigned long * signal_map;
	unsigned long * signal_invmap;
	struct module * module;
	struct exec_domain *next;
};

extern struct exec_domain default_exec_domain;

extern int register_exec_domain(struct exec_domain *it);
extern int unregister_exec_domain(struct exec_domain *it);
#define put_exec_domain(it) \
	if (it && it->module) __MOD_DEC_USE_COUNT(it->module);
#define get_exec_domain(it) \
	if (it && it->module) __MOD_INC_USE_COUNT(it->module);
extern void __set_personality(unsigned long personality);
#define set_personality(pers) do {	\
	if (current->personality != pers) \
		__set_personality(pers); \
} while (0)
asmlinkage long sys_personality(unsigned long personality);

#endif /* _PERSONALITY_H */
