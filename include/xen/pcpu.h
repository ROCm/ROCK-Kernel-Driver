#ifndef _XEN_SYSCTL_H
#define _XEN_SYSCTL_H

#include <linux/cpu.h>
#include <linux/notifier.h>

int register_pcpu_notifier(struct notifier_block *);
void unregister_pcpu_notifier(struct notifier_block *);

#ifdef CONFIG_X86
int __must_check rdmsr_safe_on_pcpu(unsigned int pcpu, u32 msr_no,
				    u32 *l, u32 *h);
int __must_check wrmsr_safe_on_pcpu(unsigned int pcpu, u32 msr_no,
				    u32 l, u32 h);
int __must_check rdmsr_safe_regs_on_pcpu(unsigned int pcpu, u32 *regs);
int __must_check wrmsr_safe_regs_on_pcpu(unsigned int pcpu, u32 *regs);
#endif

#endif /* _XEN_SYSCTL_H */
