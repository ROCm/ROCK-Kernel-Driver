#ifndef _LINUX_KPROBES_H
#define _LINUX_KPROBES_H
/*
 *  Kernel Probes (KProbes)
 *  include/linux/kprobes.h
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Copyright (C) IBM Corporation, 2002
 *
 * 2002-Oct	Created by Vamsi Krishna S <vamsi_krishna@in.ibm.com> Kernel
 *		Probes initial implementation ( includes suggestions from
 *		Rusty Russell).
 */
#include <linux/config.h>
#include <linux/list.h>
#include <linux/notifier.h>
#include <linux/smp.h>
#include <asm/kprobes.h>

struct kprobe;
struct pt_regs;
typedef void (*kprobe_pre_handler_t) (struct kprobe *, struct pt_regs *);
typedef void (*kprobe_post_handler_t) (struct kprobe *, struct pt_regs *,
				       unsigned long flags);
typedef int (*kprobe_fault_handler_t) (struct kprobe *, struct pt_regs *,
				       int trapnr);
struct kprobe {
	struct hlist_node hlist;

	/* location of the probe point */
	kprobe_opcode_t *addr;

	/* Called before addr is executed. */
	kprobe_pre_handler_t pre_handler;

	/* Called after addr is executed, unless... */
	kprobe_post_handler_t post_handler;

	/* ... called if executing addr causes a fault (eg. page fault).
	 * Return 1 if it handled fault, otherwise kernel will see it. */
	kprobe_fault_handler_t fault_handler;

	/* Saved opcode (which has been replaced with breakpoint) */
	kprobe_opcode_t opcode;

	/* copy of the original instruction */
	kprobe_opcode_t insn[MAX_INSN_SIZE];
};

#ifdef CONFIG_KPROBES
/* Locks kprobe: irq must be disabled */
void lock_kprobes(void);
void unlock_kprobes(void);

/* kprobe running now on this CPU? */
static inline int kprobe_running(void)
{
	extern unsigned int kprobe_cpu;
	return kprobe_cpu == smp_processor_id();
}

extern void arch_prepare_kprobe(struct kprobe *p);

/* Get the kprobe at this addr (if any).  Must have called lock_kprobes */
struct kprobe *get_kprobe(void *addr);

int register_kprobe(struct kprobe *p);
void unregister_kprobe(struct kprobe *p);
#else
static inline int kprobe_running(void)
{
	return 0;
}
static inline int register_kprobe(struct kprobe *p)
{
	return -ENOSYS;
}
static inline void unregister_kprobe(struct kprobe *p)
{
}
#endif
#endif				/* _LINUX_KPROBES_H */
