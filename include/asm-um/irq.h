#ifndef __UM_IRQ_H
#define __UM_IRQ_H

/* The i386 irq.h has a struct task_struct in a prototype without including
 * sched.h.  This forward declaration kills the resulting warning.
 */
struct task_struct;

#include "asm/ptrace.h"

#undef NR_IRQS

#define TIMER_IRQ		0
#define UMN_IRQ			1
#define CONSOLE_IRQ		2
#define CONSOLE_WRITE_IRQ	3
#define UBD_IRQ			4
#define UM_ETH_IRQ		5
#define SSL_IRQ			6
#define SSL_WRITE_IRQ		7
#define ACCEPT_IRQ		8
#define MCONSOLE_IRQ		9
#define WINCH_IRQ		10
#define SIGIO_WRITE_IRQ 	11
#define TELNETD_IRQ 		12
#define XTERM_IRQ 		13

#define LAST_IRQ XTERM_IRQ
#define NR_IRQS (LAST_IRQ + 1)

extern int um_request_irq(unsigned int irq, int fd, int type,
			  void (*handler)(int, void *, struct pt_regs *),
			  unsigned long irqflags,  const char * devname,
			  void *dev_id);
#endif
