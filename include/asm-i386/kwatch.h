#ifndef _ASM_KWATCH_H
#define _ASM_KWATCH_H
/*
 * Dynamic Probes (kwatch points) support
 *  	Vamsi Krishna S <vamsi_krishna@in.ibm.com>, Oct, 2002
 */
#include <linux/types.h>
#include <linux/ptrace.h>

struct kwatch;
typedef void (*kwatch_handler_t)(struct kwatch *, struct pt_regs *, int );

struct kwatch {
	unsigned long addr;	/* location of watchpoint */
	u8 length;	/* range of address */
	u8 type;	/* type of watchpoint */
	kwatch_handler_t handler;
};

#define RF_MASK	0x00010000

#ifdef CONFIG_KWATCH
extern int register_kwatch(unsigned long addr, u8 length, u8 type, kwatch_handler_t handler);
extern void unregister_kwatch(int debugreg);
extern int kwatch_handler(unsigned long condition, struct pt_regs *regs);
#else
static inline int register_kwatch(unsigned long addr, u8 length, u8 type, kwatch_handler_t handler) { return -ENOSYS; }
static inline void unregister_kwatch(int debugreg) { }
static inline int kwatch_handler(unsigned long condition, struct pt_regs *regs) { return 0; }
#endif
#endif /* _ASM_KWATCH_H */
