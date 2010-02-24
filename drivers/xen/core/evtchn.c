/******************************************************************************
 * evtchn.c
 * 
 * Communication via Xen event channels.
 * 
 * Copyright (c) 2002-2005, K A Fraser
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation; or, when distributed
 * separately from the Linux kernel or incorporated into other
 * software packages, subject to the following license:
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this source file (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy, modify,
 * merge, publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include <linux/module.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/sched.h>
#include <linux/kernel_stat.h>
#include <linux/sysdev.h>
#include <linux/ftrace.h>
#include <linux/version.h>
#include <asm/atomic.h>
#include <asm/system.h>
#include <asm/ptrace.h>
#include <asm/synch_bitops.h>
#include <xen/evtchn.h>
#include <xen/interface/event_channel.h>
#include <xen/interface/physdev.h>
#include <asm/hypervisor.h>
#include <linux/mc146818rtc.h> /* RTC_IRQ */

/*
 * This lock protects updates to the following mapping and reference-count
 * arrays. The lock does not need to be acquired to read the mapping tables.
 */
static DEFINE_SPINLOCK(irq_mapping_update_lock);

/* IRQ <-> event-channel mappings. */
static int evtchn_to_irq[NR_EVENT_CHANNELS] = {
	[0 ...  NR_EVENT_CHANNELS-1] = -1 };

#if defined(CONFIG_SMP) && defined(CONFIG_X86)
static struct per_cpu_irqaction {
	struct irqaction action; /* must be first */
	struct per_cpu_irqaction *next;
	cpumask_t cpus;
} *virq_actions[NR_VIRQS];
/* IRQ <-> VIRQ mapping. */
static DECLARE_BITMAP(virq_per_cpu, NR_VIRQS) __read_mostly;
static DEFINE_PER_CPU(int[NR_VIRQS], virq_to_evtchn);
#define BUG_IF_VIRQ_PER_CPU(irq) \
	BUG_ON(type_from_irq(irq) == IRQT_VIRQ \
	       && test_bit(index_from_irq(irq), virq_per_cpu))
#else
#define BUG_IF_VIRQ_PER_CPU(irq) ((void)(irq))
#define PER_CPU_VIRQ_IRQ
#endif

/* IRQ <-> IPI mapping. */
#ifndef NR_IPIS
#define NR_IPIS 1
#endif
#if defined(CONFIG_SMP) && defined(CONFIG_X86)
static int ipi_to_irq[NR_IPIS] __read_mostly = {[0 ... NR_IPIS-1] = -1};
static DEFINE_PER_CPU(int[NR_IPIS], ipi_to_evtchn);
#else
#define PER_CPU_IPI_IRQ
#endif
#if !defined(CONFIG_SMP) || !defined(PER_CPU_IPI_IRQ)
#define BUG_IF_IPI(irq) BUG_ON(type_from_irq(irq) == IRQT_IPI)
#else
#define BUG_IF_IPI(irq) ((void)(irq))
#endif

/* Binding types. */
enum {
	IRQT_UNBOUND,
	IRQT_PIRQ,
	IRQT_VIRQ,
	IRQT_IPI,
	IRQT_LOCAL_PORT,
	IRQT_CALLER_PORT,
	_IRQT_COUNT
};

#define _IRQT_BITS 4
#define _EVTCHN_BITS 12
#define _INDEX_BITS (32 - _IRQT_BITS - _EVTCHN_BITS)

/* Convenient shorthand for packed representation of an unbound IRQ. */
#define IRQ_UNBOUND	(IRQT_UNBOUND << (32 - _IRQT_BITS))

static struct irq_cfg _irq_cfg[] = {
	[0 ...
#ifdef CONFIG_SPARSE_IRQ
	       BUILD_BUG_ON_ZERO(PIRQ_BASE) + NR_IRQS_LEGACY
#else
	       NR_IRQS
#endif
		       - 1].info = IRQ_UNBOUND
};

static inline struct irq_cfg *__pure irq_cfg(unsigned int irq)
{
#ifdef CONFIG_SPARSE_IRQ
	struct irq_desc *desc = irq_to_desc(irq);

	return desc ? desc->chip_data : NULL;
#else
	return irq < NR_IRQS ? _irq_cfg + irq : NULL;
#endif
}

/* Constructor for packed IRQ information. */
static inline u32 mk_irq_info(u32 type, u32 index, u32 evtchn)
{
	BUILD_BUG_ON(_IRQT_COUNT > (1U << _IRQT_BITS));

	BUILD_BUG_ON(NR_PIRQS > (1U << _INDEX_BITS));
	BUILD_BUG_ON(NR_VIRQS > (1U << _INDEX_BITS));
	BUILD_BUG_ON(NR_IPIS > (1U << _INDEX_BITS));
	BUG_ON(index >> _INDEX_BITS);

	BUILD_BUG_ON(NR_EVENT_CHANNELS > (1U << _EVTCHN_BITS));

	return ((type << (32 - _IRQT_BITS)) | (index << _EVTCHN_BITS) | evtchn);
}

/*
 * Accessors for packed IRQ information.
 */

static inline unsigned int index_from_irq(int irq)
{
	const struct irq_cfg *cfg = irq_cfg(irq);

	return cfg ? (cfg->info >> _EVTCHN_BITS) & ((1U << _INDEX_BITS) - 1)
		   : 0;
}

static inline unsigned int type_from_irq(int irq)
{
	const struct irq_cfg *cfg = irq_cfg(irq);

	return cfg ? cfg->info >> (32 - _IRQT_BITS) : IRQT_UNBOUND;
}

static inline unsigned int evtchn_from_per_cpu_irq(unsigned int irq,
						    unsigned int cpu)
{
	switch (type_from_irq(irq)) {
#ifndef PER_CPU_VIRQ_IRQ
	case IRQT_VIRQ:
		return per_cpu(virq_to_evtchn, cpu)[index_from_irq(irq)];
#endif
#ifndef PER_CPU_IPI_IRQ
	case IRQT_IPI:
		return per_cpu(ipi_to_evtchn, cpu)[index_from_irq(irq)];
#endif
	}
	BUG();
	return 0;
}

static inline unsigned int evtchn_from_irq(unsigned int irq)
{
	const struct irq_cfg *cfg;

	switch (type_from_irq(irq)) {
#ifndef PER_CPU_VIRQ_IRQ
	case IRQT_VIRQ:
#endif
#ifndef PER_CPU_IPI_IRQ
	case IRQT_IPI:
#endif
		return evtchn_from_per_cpu_irq(irq, smp_processor_id());
	}
	cfg = irq_cfg(irq);
	return cfg ? cfg->info & ((1U << _EVTCHN_BITS) - 1) : 0;
}

/* IRQ <-> VIRQ mapping. */
DEFINE_PER_CPU(int[NR_VIRQS], virq_to_irq) = {[0 ... NR_VIRQS-1] = -1};

#if defined(CONFIG_SMP) && defined(PER_CPU_IPI_IRQ)
/* IRQ <-> IPI mapping. */
DEFINE_PER_CPU(int[NR_IPIS], ipi_to_irq) = {[0 ... NR_IPIS-1] = -1};
#endif

#ifdef CONFIG_SMP

static u8 cpu_evtchn[NR_EVENT_CHANNELS];
static DEFINE_PER_CPU(unsigned long[BITS_TO_LONGS(NR_EVENT_CHANNELS)],
		      cpu_evtchn_mask);

static inline unsigned long active_evtchns(unsigned int idx)
{
	shared_info_t *sh = HYPERVISOR_shared_info;

	return (sh->evtchn_pending[idx] &
		percpu_read(cpu_evtchn_mask[idx]) &
		~sh->evtchn_mask[idx]);
}

static void bind_evtchn_to_cpu(unsigned int chn, unsigned int cpu)
{
	shared_info_t *s = HYPERVISOR_shared_info;
	int irq = evtchn_to_irq[chn];

	BUG_ON(!test_bit(chn, s->evtchn_mask));

	if (irq != -1) {
		struct irq_desc *desc = irq_to_desc(irq);

		if (!(desc->status & IRQ_PER_CPU))
			cpumask_copy(desc->affinity, cpumask_of(cpu));
		else
			cpumask_set_cpu(cpu, desc->affinity);
	}

	clear_bit(chn, per_cpu(cpu_evtchn_mask, cpu_evtchn[chn]));
	set_bit(chn, per_cpu(cpu_evtchn_mask, cpu));
	cpu_evtchn[chn] = cpu;
}

static void init_evtchn_cpu_bindings(void)
{
	int i;

	/* By default all event channels notify CPU#0. */
	for (i = 0; i < nr_irqs; i++) {
		struct irq_desc *desc = irq_to_desc(i);

		if (desc)
			cpumask_copy(desc->affinity, cpumask_of(0));
	}

	memset(cpu_evtchn, 0, sizeof(cpu_evtchn));
	memset(per_cpu(cpu_evtchn_mask, 0), ~0, sizeof(per_cpu(cpu_evtchn_mask, 0)));
}

static inline unsigned int cpu_from_evtchn(unsigned int evtchn)
{
	return cpu_evtchn[evtchn];
}

#else

static inline unsigned long active_evtchns(unsigned int idx)
{
	shared_info_t *sh = HYPERVISOR_shared_info;

	return (sh->evtchn_pending[idx] & ~sh->evtchn_mask[idx]);
}

static void bind_evtchn_to_cpu(unsigned int chn, unsigned int cpu)
{
}

static void init_evtchn_cpu_bindings(void)
{
}

static inline unsigned int cpu_from_evtchn(unsigned int evtchn)
{
	return 0;
}

#endif

#ifdef CONFIG_X86
void __init xen_init_IRQ(void);
void __init init_IRQ(void)
{
	irq_ctx_init(0);
	xen_init_IRQ();
}
#include <asm/idle.h>
#endif

/* Xen will never allocate port zero for any purpose. */
#define VALID_EVTCHN(chn)	((chn) != 0)

/*
 * Force a proper event-channel callback from Xen after clearing the
 * callback mask. We do this in a very simple manner, by making a call
 * down into Xen. The pending flag will be checked by Xen on return.
 */
void force_evtchn_callback(void)
{
	VOID(HYPERVISOR_xen_version(0, NULL));
}
/* Not a GPL symbol: used in ubiquitous macros, so too restrictive. */
EXPORT_SYMBOL(force_evtchn_callback);

static DEFINE_PER_CPU(unsigned int, upcall_count);
static DEFINE_PER_CPU(unsigned int, current_l1i);
static DEFINE_PER_CPU(unsigned int, current_l2i);

#ifndef vcpu_info_xchg
#define vcpu_info_xchg(fld, val) xchg(&current_vcpu_info()->fld, val)
#endif

#ifndef percpu_xadd
#define percpu_xadd(var, val)					\
({								\
	typeof(per_cpu_var(var)) __tmp_var__;			\
	unsigned long flags;					\
	local_irq_save(flags);					\
	__tmp_var__ = get_cpu_var(var);				\
	__get_cpu_var(var) += (val);				\
	put_cpu_var(var);					\
	local_irq_restore(flags);				\
	__tmp_var__;						\
})
#endif

/* NB. Interrupts are disabled on entry. */
asmlinkage void __irq_entry evtchn_do_upcall(struct pt_regs *regs)
{
	struct pt_regs     *old_regs = set_irq_regs(regs);
	unsigned long       l1, l2;
	unsigned long       masked_l1, masked_l2;
	unsigned int        l1i, l2i, start_l1i, start_l2i, port, count, i;
	int                 irq;

	exit_idle();
	irq_enter();

	do {
		/* Avoid a callback storm when we reenable delivery. */
		vcpu_info_write(evtchn_upcall_pending, 0);

		/* Nested invocations bail immediately. */
		if (unlikely(percpu_xadd(upcall_count, 1)))
			break;

#ifndef CONFIG_X86 /* No need for a barrier -- XCHG is a barrier on x86. */
		/* Clear master flag /before/ clearing selector flag. */
		wmb();
#else
		barrier();
#endif
		l1 = vcpu_info_xchg(evtchn_pending_sel, 0);

		start_l1i = l1i = percpu_read(current_l1i);
		start_l2i = percpu_read(current_l2i);

		for (i = 0; l1 != 0; i++) {
			masked_l1 = l1 & ((~0UL) << l1i);
			/* If we masked out all events, wrap to beginning. */
			if (masked_l1 == 0) {
				l1i = l2i = 0;
				continue;
			}
			l1i = __ffs(masked_l1);

			l2 = active_evtchns(l1i);
			l2i = 0; /* usually scan entire word from start */
			if (l1i == start_l1i) {
				/* We scan the starting word in two parts. */
				if (i == 0)
					/* 1st time: start in the middle */
					l2i = start_l2i;
				else
					/* 2nd time: mask bits done already */
					l2 &= (1ul << start_l2i) - 1;
			}

			do {
				masked_l2 = l2 & ((~0UL) << l2i);
				if (masked_l2 == 0)
					break;
				l2i = __ffs(masked_l2);

				/* process port */
				port = (l1i * BITS_PER_LONG) + l2i;
				if (unlikely((irq = evtchn_to_irq[port]) == -1))
					evtchn_device_upcall(port);
				else if (!handle_irq(irq, regs) && printk_ratelimit())
					printk(KERN_EMERG "%s(%d): No handler for irq %d\n",
					       __func__, smp_processor_id(), irq);

				l2i = (l2i + 1) % BITS_PER_LONG;

				/* Next caller starts at last processed + 1 */
				percpu_write(current_l1i,
					l2i ? l1i : (l1i + 1) % BITS_PER_LONG);
				percpu_write(current_l2i, l2i);

			} while (l2i != 0);

			/* Scan start_l1i twice; all others once. */
			if ((l1i != start_l1i) || (i != 0))
				l1 &= ~(1UL << l1i);

			l1i = (l1i + 1) % BITS_PER_LONG;
		}

		/* If there were nested callbacks then we have more to do. */
		count = percpu_read(upcall_count);
		percpu_write(upcall_count, 0);
	} while (unlikely(count != 1));

	irq_exit();
	set_irq_regs(old_regs);
}

static struct irq_chip dynirq_chip;

static int find_unbound_irq(unsigned int cpu, bool percpu)
{
	static int warned;
	int irq;

	for (irq = DYNIRQ_BASE; irq < (DYNIRQ_BASE + NR_DYNIRQS); irq++) {
		struct irq_desc *desc = irq_to_desc_alloc_node(irq, cpu_to_node(cpu));
		struct irq_cfg *cfg = desc->chip_data;

		if (!cfg->bindcount) {
			irq_flow_handler_t handle;
			const char *name;

			desc->status |= IRQ_NOPROBE;
			if (!percpu) {
				handle = handle_level_irq;
				name = "level";
			} else {
				handle = handle_percpu_irq;
				name = "percpu";
			}
			set_irq_chip_and_handler_name(irq, &dynirq_chip,
						      handle, name);
			return irq;
		}
	}

	if (!warned) {
		warned = 1;
		printk(KERN_WARNING "No available IRQ to bind to: "
		       "increase NR_DYNIRQS.\n");
	}

	return -ENOSPC;
}

static int bind_caller_port_to_irq(unsigned int caller_port)
{
	int irq;

	spin_lock(&irq_mapping_update_lock);

	if ((irq = evtchn_to_irq[caller_port]) == -1) {
		if ((irq = find_unbound_irq(smp_processor_id(), false)) < 0)
			goto out;

		evtchn_to_irq[caller_port] = irq;
		irq_cfg(irq)->info = mk_irq_info(IRQT_CALLER_PORT,
						  0, caller_port);
	}

	irq_cfg(irq)->bindcount++;

 out:
	spin_unlock(&irq_mapping_update_lock);
	return irq;
}

static int bind_local_port_to_irq(unsigned int local_port)
{
	int irq;

	spin_lock(&irq_mapping_update_lock);

	BUG_ON(evtchn_to_irq[local_port] != -1);

	if ((irq = find_unbound_irq(smp_processor_id(), false)) < 0) {
		struct evtchn_close close = { .port = local_port };
		if (HYPERVISOR_event_channel_op(EVTCHNOP_close, &close))
			BUG();
		goto out;
	}

	evtchn_to_irq[local_port] = irq;
	irq_cfg(irq)->info = mk_irq_info(IRQT_LOCAL_PORT, 0, local_port);
	irq_cfg(irq)->bindcount++;

 out:
	spin_unlock(&irq_mapping_update_lock);
	return irq;
}

static int bind_listening_port_to_irq(unsigned int remote_domain)
{
	struct evtchn_alloc_unbound alloc_unbound;
	int err;

	alloc_unbound.dom        = DOMID_SELF;
	alloc_unbound.remote_dom = remote_domain;

	err = HYPERVISOR_event_channel_op(EVTCHNOP_alloc_unbound,
					  &alloc_unbound);

	return err ? : bind_local_port_to_irq(alloc_unbound.port);
}

static int bind_interdomain_evtchn_to_irq(unsigned int remote_domain,
					  unsigned int remote_port)
{
	struct evtchn_bind_interdomain bind_interdomain;
	int err;

	bind_interdomain.remote_dom  = remote_domain;
	bind_interdomain.remote_port = remote_port;

	err = HYPERVISOR_event_channel_op(EVTCHNOP_bind_interdomain,
					  &bind_interdomain);

	return err ? : bind_local_port_to_irq(bind_interdomain.local_port);
}

static int bind_virq_to_irq(unsigned int virq, unsigned int cpu)
{
	struct evtchn_bind_virq bind_virq;
	int evtchn, irq;

	spin_lock(&irq_mapping_update_lock);

	if ((irq = per_cpu(virq_to_irq, cpu)[virq]) == -1) {
		if ((irq = find_unbound_irq(cpu, false)) < 0)
			goto out;

		bind_virq.virq = virq;
		bind_virq.vcpu = cpu;
		if (HYPERVISOR_event_channel_op(EVTCHNOP_bind_virq,
						&bind_virq) != 0)
			BUG();
		evtchn = bind_virq.port;

		evtchn_to_irq[evtchn] = irq;
#ifndef PER_CPU_VIRQ_IRQ
		{
			unsigned int cpu;

			for_each_possible_cpu(cpu)
				per_cpu(virq_to_evtchn, cpu)[virq] = evtchn;
		}
#endif
		irq_cfg(irq)->info = mk_irq_info(IRQT_VIRQ, virq, evtchn);

		per_cpu(virq_to_irq, cpu)[virq] = irq;

		bind_evtchn_to_cpu(evtchn, cpu);
	}

	irq_cfg(irq)->bindcount++;

 out:
	spin_unlock(&irq_mapping_update_lock);
	return irq;
}

#if defined(CONFIG_SMP) && defined(PER_CPU_IPI_IRQ)
static int bind_ipi_to_irq(unsigned int ipi, unsigned int cpu)
{
	struct evtchn_bind_ipi bind_ipi;
	int evtchn, irq;

	spin_lock(&irq_mapping_update_lock);

	if ((irq = per_cpu(ipi_to_irq, cpu)[ipi]) == -1) {
		if ((irq = find_unbound_irq(cpu, false)) < 0)
			goto out;

		bind_ipi.vcpu = cpu;
		if (HYPERVISOR_event_channel_op(EVTCHNOP_bind_ipi,
						&bind_ipi) != 0)
			BUG();
		evtchn = bind_ipi.port;

		evtchn_to_irq[evtchn] = irq;
		irq_cfg(irq)->info = mk_irq_info(IRQT_IPI, ipi, evtchn);

		per_cpu(ipi_to_irq, cpu)[ipi] = irq;

		bind_evtchn_to_cpu(evtchn, cpu);
	}

	irq_cfg(irq)->bindcount++;

 out:
	spin_unlock(&irq_mapping_update_lock);
	return irq;
}
#endif

static void unbind_from_irq(unsigned int irq)
{
	struct evtchn_close close;
	unsigned int cpu;
	int evtchn = evtchn_from_irq(irq);

	BUG_IF_VIRQ_PER_CPU(irq);
	BUG_IF_IPI(irq);

	spin_lock(&irq_mapping_update_lock);

	if (!--irq_cfg(irq)->bindcount && VALID_EVTCHN(evtchn)) {
		close.port = evtchn;
		if ((type_from_irq(irq) != IRQT_CALLER_PORT) &&
		    HYPERVISOR_event_channel_op(EVTCHNOP_close, &close))
			BUG();

		switch (type_from_irq(irq)) {
		case IRQT_VIRQ:
			per_cpu(virq_to_irq, cpu_from_evtchn(evtchn))
				[index_from_irq(irq)] = -1;
#ifndef PER_CPU_VIRQ_IRQ
			for_each_possible_cpu(cpu)
				per_cpu(virq_to_evtchn, cpu)
					[index_from_irq(irq)] = 0;
#endif
			break;
#if defined(CONFIG_SMP) && defined(PER_CPU_IPI_IRQ)
		case IRQT_IPI:
			per_cpu(ipi_to_irq, cpu_from_evtchn(evtchn))
				[index_from_irq(irq)] = -1;
			break;
#endif
		default:
			break;
		}

		/* Closed ports are implicitly re-bound to VCPU0. */
		bind_evtchn_to_cpu(evtchn, 0);

		evtchn_to_irq[evtchn] = -1;
		irq_cfg(irq)->info = IRQ_UNBOUND;

		/* Zap stats across IRQ changes of use. */
		for_each_possible_cpu(cpu)
#ifdef CONFIG_GENERIC_HARDIRQS
			irq_to_desc(irq)->kstat_irqs[cpu] = 0;
#else
			kstat_cpu(cpu).irqs[irq] = 0;
#endif
	}

	spin_unlock(&irq_mapping_update_lock);
}

#if defined(CONFIG_SMP) && (!defined(PER_CPU_IPI_IRQ) || !defined(PER_CPU_VIRQ_IRQ))
void unbind_from_per_cpu_irq(unsigned int irq, unsigned int cpu,
			     struct irqaction *action)
{
	struct evtchn_close close;
	int evtchn = evtchn_from_per_cpu_irq(irq, cpu);
	struct irqaction *free_action = NULL;

	spin_lock(&irq_mapping_update_lock);

	if (VALID_EVTCHN(evtchn)) {
		struct irq_desc *desc = irq_to_desc(irq);

		mask_evtchn(evtchn);

		BUG_ON(irq_cfg(irq)->bindcount <= 1);
		irq_cfg(irq)->bindcount--;

#ifndef PER_CPU_VIRQ_IRQ
		if (type_from_irq(irq) == IRQT_VIRQ) {
			unsigned int virq = index_from_irq(irq);
			struct per_cpu_irqaction *cur, *prev = NULL;

			cur = virq_actions[virq];
			while (cur) {
				if (cur->action.dev_id == action) {
					cpu_clear(cpu, cur->cpus);
					if (cpus_empty(cur->cpus)) {
						if (prev)
							prev->next = cur->next;
						else
							virq_actions[virq] = cur->next;
						free_action = action;
					}
				} else if (cpu_isset(cpu, cur->cpus))
					evtchn = 0;
				cur = (prev = cur)->next;
			}
			if (!VALID_EVTCHN(evtchn))
				goto done;
		}
#endif

		cpumask_clear_cpu(cpu, desc->affinity);

		close.port = evtchn;
		if (HYPERVISOR_event_channel_op(EVTCHNOP_close, &close))
			BUG();

		switch (type_from_irq(irq)) {
#ifndef PER_CPU_VIRQ_IRQ
		case IRQT_VIRQ:
			per_cpu(virq_to_evtchn, cpu)[index_from_irq(irq)] = 0;
			break;
#endif
#ifndef PER_CPU_IPI_IRQ
		case IRQT_IPI:
			per_cpu(ipi_to_evtchn, cpu)[index_from_irq(irq)] = 0;
			break;
#endif
		default:
			BUG();
			break;
		}

		/* Closed ports are implicitly re-bound to VCPU0. */
		bind_evtchn_to_cpu(evtchn, 0);

		evtchn_to_irq[evtchn] = -1;
	}

#ifndef PER_CPU_VIRQ_IRQ
done:
#endif
	spin_unlock(&irq_mapping_update_lock);

	if (free_action)
		free_irq(irq, free_action);
}
EXPORT_SYMBOL_GPL(unbind_from_per_cpu_irq);
#endif /* CONFIG_SMP && (!PER_CPU_IPI_IRQ || !PER_CPU_VIRQ_IRQ) */

int bind_caller_port_to_irqhandler(
	unsigned int caller_port,
	irq_handler_t handler,
	unsigned long irqflags,
	const char *devname,
	void *dev_id)
{
	int irq, retval;

	irq = bind_caller_port_to_irq(caller_port);
	if (irq < 0)
		return irq;

	retval = request_irq(irq, handler, irqflags, devname, dev_id);
	if (retval != 0) {
		unbind_from_irq(irq);
		return retval;
	}

	return irq;
}
EXPORT_SYMBOL_GPL(bind_caller_port_to_irqhandler);

int bind_listening_port_to_irqhandler(
	unsigned int remote_domain,
	irq_handler_t handler,
	unsigned long irqflags,
	const char *devname,
	void *dev_id)
{
	int irq, retval;

	irq = bind_listening_port_to_irq(remote_domain);
	if (irq < 0)
		return irq;

	retval = request_irq(irq, handler, irqflags, devname, dev_id);
	if (retval != 0) {
		unbind_from_irq(irq);
		return retval;
	}

	return irq;
}
EXPORT_SYMBOL_GPL(bind_listening_port_to_irqhandler);

int bind_interdomain_evtchn_to_irqhandler(
	unsigned int remote_domain,
	unsigned int remote_port,
	irq_handler_t handler,
	unsigned long irqflags,
	const char *devname,
	void *dev_id)
{
	int irq, retval;

	irq = bind_interdomain_evtchn_to_irq(remote_domain, remote_port);
	if (irq < 0)
		return irq;

	retval = request_irq(irq, handler, irqflags, devname, dev_id);
	if (retval != 0) {
		unbind_from_irq(irq);
		return retval;
	}

	return irq;
}
EXPORT_SYMBOL_GPL(bind_interdomain_evtchn_to_irqhandler);

int bind_virq_to_irqhandler(
	unsigned int virq,
	unsigned int cpu,
	irq_handler_t handler,
	unsigned long irqflags,
	const char *devname,
	void *dev_id)
{
	int irq, retval;

	BUG_IF_VIRQ_PER_CPU(virq);

	irq = bind_virq_to_irq(virq, cpu);
	if (irq < 0)
		return irq;

	retval = request_irq(irq, handler, irqflags, devname, dev_id);
	if (retval != 0) {
		unbind_from_irq(irq);
		return retval;
	}

	return irq;
}
EXPORT_SYMBOL_GPL(bind_virq_to_irqhandler);

#ifdef CONFIG_SMP
#ifndef PER_CPU_VIRQ_IRQ
int bind_virq_to_irqaction(
	unsigned int virq,
	unsigned int cpu,
	struct irqaction *action)
{
	struct evtchn_bind_virq bind_virq;
	int evtchn, irq, retval = 0;
	struct per_cpu_irqaction *cur = NULL, *new;

	BUG_ON(!test_bit(virq, virq_per_cpu));

	if (action->dev_id)
		return -EINVAL;

	new = kzalloc(sizeof(*new), GFP_ATOMIC);
	if (new) {
		new->action = *action;
		new->action.dev_id = action;
	}

	spin_lock(&irq_mapping_update_lock);

	for (cur = virq_actions[virq]; cur; cur = cur->next)
		if (cur->action.dev_id == action)
			break;
	if (!cur) {
		if (!new) {
			spin_unlock(&irq_mapping_update_lock);
			return -ENOMEM;
		}
		new->next = virq_actions[virq];
		virq_actions[virq] = cur = new;
		retval = 1;
	}
	cpu_set(cpu, cur->cpus);
	action = &cur->action;

	if ((irq = per_cpu(virq_to_irq, cpu)[virq]) == -1) {
		unsigned int nr;

		BUG_ON(!retval);

		if ((irq = find_unbound_irq(cpu, true)) < 0) {
			if (cur)
				virq_actions[virq] = cur->next;
			spin_unlock(&irq_mapping_update_lock);
			if (cur != new)
				kfree(new);
			return irq;
		}

		/* Extra reference so count will never drop to zero. */
		irq_cfg(irq)->bindcount++;

		for_each_possible_cpu(nr)
			per_cpu(virq_to_irq, nr)[virq] = irq;
		irq_cfg(irq)->info = mk_irq_info(IRQT_VIRQ, virq, 0);
	}

	evtchn = per_cpu(virq_to_evtchn, cpu)[virq];
	if (!VALID_EVTCHN(evtchn)) {
		bind_virq.virq = virq;
		bind_virq.vcpu = cpu;
		if (HYPERVISOR_event_channel_op(EVTCHNOP_bind_virq,
						&bind_virq) != 0)
			BUG();
		evtchn = bind_virq.port;
		evtchn_to_irq[evtchn] = irq;
		per_cpu(virq_to_evtchn, cpu)[virq] = evtchn;

		bind_evtchn_to_cpu(evtchn, cpu);
	}

	irq_cfg(irq)->bindcount++;

	spin_unlock(&irq_mapping_update_lock);

	if (cur != new)
		kfree(new);

	if (retval == 0) {
		unsigned long flags;

		local_irq_save(flags);
		unmask_evtchn(evtchn);
		local_irq_restore(flags);
	} else {
		action->flags |= IRQF_PERCPU;
		retval = setup_irq(irq, action);
		if (retval) {
			unbind_from_per_cpu_irq(irq, cpu, cur->action.dev_id);
			BUG_ON(retval > 0);
			irq = retval;
		}
	}

	return irq;
}
EXPORT_SYMBOL_GPL(bind_virq_to_irqaction);
#endif

#ifdef PER_CPU_IPI_IRQ
int bind_ipi_to_irqhandler(
	unsigned int ipi,
	unsigned int cpu,
	irq_handler_t handler,
	unsigned long irqflags,
	const char *devname,
	void *dev_id)
{
	int irq, retval;

	irq = bind_ipi_to_irq(ipi, cpu);
	if (irq < 0)
		return irq;

	retval = request_irq(irq, handler, irqflags | IRQF_NO_SUSPEND,
			     devname, dev_id);
	if (retval != 0) {
		unbind_from_irq(irq);
		return retval;
	}

	return irq;
}
#else
int __cpuinit bind_ipi_to_irqaction(
	unsigned int ipi,
	unsigned int cpu,
	struct irqaction *action)
{
	struct evtchn_bind_ipi bind_ipi;
	int evtchn, irq, retval = 0;

	spin_lock(&irq_mapping_update_lock);

	if (VALID_EVTCHN(per_cpu(ipi_to_evtchn, cpu)[ipi])) {
		spin_unlock(&irq_mapping_update_lock);
		return -EBUSY;
	}

	if ((irq = ipi_to_irq[ipi]) == -1) {
		if ((irq = find_unbound_irq(cpu, true)) < 0) {
			spin_unlock(&irq_mapping_update_lock);
			return irq;
		}

		/* Extra reference so count will never drop to zero. */
		irq_cfg(irq)->bindcount++;

		ipi_to_irq[ipi] = irq;
		irq_cfg(irq)->info = mk_irq_info(IRQT_IPI, ipi, 0);
		retval = 1;
	}

	bind_ipi.vcpu = cpu;
	if (HYPERVISOR_event_channel_op(EVTCHNOP_bind_ipi,
					&bind_ipi) != 0)
		BUG();

	evtchn = bind_ipi.port;
	evtchn_to_irq[evtchn] = irq;
	per_cpu(ipi_to_evtchn, cpu)[ipi] = evtchn;

	bind_evtchn_to_cpu(evtchn, cpu);

	irq_cfg(irq)->bindcount++;

	spin_unlock(&irq_mapping_update_lock);

	if (retval == 0) {
		unsigned long flags;

		local_irq_save(flags);
		unmask_evtchn(evtchn);
		local_irq_restore(flags);
	} else {
		action->flags |= IRQF_PERCPU | IRQF_NO_SUSPEND;
		retval = setup_irq(irq, action);
		if (retval) {
			unbind_from_per_cpu_irq(irq, cpu, NULL);
			BUG_ON(retval > 0);
			irq = retval;
		}
	}

	return irq;
}
#endif /* PER_CPU_IPI_IRQ */
#endif /* CONFIG_SMP */

void unbind_from_irqhandler(unsigned int irq, void *dev_id)
{
	free_irq(irq, dev_id);
	unbind_from_irq(irq);
}
EXPORT_SYMBOL_GPL(unbind_from_irqhandler);

#ifdef CONFIG_SMP
void rebind_evtchn_to_cpu(int port, unsigned int cpu)
{
	struct evtchn_bind_vcpu ebv = { .port = port, .vcpu = cpu };
	int masked;

	masked = test_and_set_evtchn_mask(port);
	if (HYPERVISOR_event_channel_op(EVTCHNOP_bind_vcpu, &ebv) == 0)
		bind_evtchn_to_cpu(port, cpu);
	if (!masked)
		unmask_evtchn(port);
}

static void rebind_irq_to_cpu(unsigned int irq, unsigned int tcpu)
{
	int evtchn = evtchn_from_irq(irq);

	BUG_IF_VIRQ_PER_CPU(irq);
	BUG_IF_IPI(irq);

	if (VALID_EVTCHN(evtchn))
		rebind_evtchn_to_cpu(evtchn, tcpu);
}

static int set_affinity_irq(unsigned int irq, const struct cpumask *dest)
{
	rebind_irq_to_cpu(irq, cpumask_first(dest));

	return 0;
}
#endif

int resend_irq_on_evtchn(unsigned int irq)
{
	int masked, evtchn = evtchn_from_irq(irq);

	if (!VALID_EVTCHN(evtchn))
		return 1;

	masked = test_and_set_evtchn_mask(evtchn);
	set_evtchn(evtchn);
	if (!masked)
		unmask_evtchn(evtchn);

	return 1;
}

/*
 * Interface to generic handling in irq.c
 */

static unsigned int startup_dynirq(unsigned int irq)
{
	int evtchn = evtchn_from_irq(irq);

	if (VALID_EVTCHN(evtchn))
		unmask_evtchn(evtchn);
	return 0;
}

static void unmask_dynirq(unsigned int irq)
{
	int evtchn = evtchn_from_irq(irq);

	if (VALID_EVTCHN(evtchn))
		unmask_evtchn(evtchn);
}

static void mask_dynirq(unsigned int irq)
{
	int evtchn = evtchn_from_irq(irq);

	if (VALID_EVTCHN(evtchn))
		mask_evtchn(evtchn);
}

static void ack_dynirq(unsigned int irq)
{
	int evtchn = evtchn_from_irq(irq);

	move_native_irq(irq);

	if (VALID_EVTCHN(evtchn)) {
		mask_evtchn(evtchn);
		clear_evtchn(evtchn);
	}
}

static void end_dynirq(unsigned int irq)
{
	int evtchn = evtchn_from_irq(irq);

	if (VALID_EVTCHN(evtchn) && !(irq_to_desc(irq)->status & IRQ_DISABLED))
		unmask_evtchn(evtchn);
}

static struct irq_chip dynirq_chip = {
	.name     = "Dynamic",
	.startup  = startup_dynirq,
	.shutdown = mask_dynirq,
	.disable  = mask_dynirq,
	.mask     = mask_dynirq,
	.unmask   = unmask_dynirq,
	.mask_ack = ack_dynirq,
	.ack      = ack_dynirq,
	.eoi      = end_dynirq,
	.end      = end_dynirq,
#ifdef CONFIG_SMP
	.set_affinity = set_affinity_irq,
#endif
	.retrigger = resend_irq_on_evtchn,
};

/* Bitmap indicating which PIRQs require Xen to be notified on unmask. */
static bool pirq_eoi_does_unmask;
static unsigned long *pirq_needs_eoi;
static DECLARE_BITMAP(probing_pirq, NR_PIRQS);

static void pirq_unmask_and_notify(unsigned int evtchn, unsigned int irq)
{
	struct physdev_eoi eoi = { .irq = evtchn_get_xen_pirq(irq) };

	if (pirq_eoi_does_unmask) {
		if (test_bit(eoi.irq, pirq_needs_eoi))
			VOID(HYPERVISOR_physdev_op(PHYSDEVOP_eoi, &eoi));
		else
			unmask_evtchn(evtchn);
	} else if (test_bit(irq - PIRQ_BASE, pirq_needs_eoi)) {
		if (smp_processor_id() != cpu_from_evtchn(evtchn)) {
			struct evtchn_unmask unmask = { .port = evtchn };
			struct multicall_entry mcl[2];

			mcl[0].op = __HYPERVISOR_event_channel_op;
			mcl[0].args[0] = EVTCHNOP_unmask;
			mcl[0].args[1] = (unsigned long)&unmask;
			mcl[1].op = __HYPERVISOR_physdev_op;
			mcl[1].args[0] = PHYSDEVOP_eoi;
			mcl[1].args[1] = (unsigned long)&eoi;

			if (HYPERVISOR_multicall(mcl, 2))
				BUG();
		} else {
			unmask_evtchn(evtchn);
			VOID(HYPERVISOR_physdev_op(PHYSDEVOP_eoi, &eoi));
		}
	} else
		unmask_evtchn(evtchn);
}

static inline void pirq_query_unmask(int irq)
{
	struct physdev_irq_status_query irq_status;

	if (pirq_eoi_does_unmask)
		return;
	irq_status.irq = evtchn_get_xen_pirq(irq);
	if (HYPERVISOR_physdev_op(PHYSDEVOP_irq_status_query, &irq_status))
		irq_status.flags = 0;
	clear_bit(irq - PIRQ_BASE, pirq_needs_eoi);
	if (irq_status.flags & XENIRQSTAT_needs_eoi)
		set_bit(irq - PIRQ_BASE, pirq_needs_eoi);
}

static int set_type_pirq(unsigned int irq, unsigned int type)
{
	if (type != IRQ_TYPE_PROBE)
		return -EINVAL;
	set_bit(irq - PIRQ_BASE, probing_pirq);
	return 0;
}

static unsigned int startup_pirq(unsigned int irq)
{
	struct evtchn_bind_pirq bind_pirq;
	int evtchn = evtchn_from_irq(irq);

	if (VALID_EVTCHN(evtchn)) {
		clear_bit(irq - PIRQ_BASE, probing_pirq);
		goto out;
	}

	bind_pirq.pirq = evtchn_get_xen_pirq(irq);
	/* NB. We are happy to share unless we are probing. */
	bind_pirq.flags = test_and_clear_bit(irq - PIRQ_BASE, probing_pirq)
			  || (irq_to_desc(irq)->status & IRQ_AUTODETECT)
			  ? 0 : BIND_PIRQ__WILL_SHARE;
	if (HYPERVISOR_event_channel_op(EVTCHNOP_bind_pirq, &bind_pirq) != 0) {
		if (bind_pirq.flags)
			printk(KERN_INFO "Failed to obtain physical IRQ %d\n",
			       irq);
		return 0;
	}
	evtchn = bind_pirq.port;

	pirq_query_unmask(irq);

	evtchn_to_irq[evtchn] = irq;
	bind_evtchn_to_cpu(evtchn, 0);
	irq_cfg(irq)->info = mk_irq_info(IRQT_PIRQ, bind_pirq.pirq, evtchn);

 out:
	pirq_unmask_and_notify(evtchn, irq);

	return 0;
}

static void shutdown_pirq(unsigned int irq)
{
	struct evtchn_close close;
	int evtchn = evtchn_from_irq(irq);

	if (!VALID_EVTCHN(evtchn))
		return;

	mask_evtchn(evtchn);

	close.port = evtchn;
	if (HYPERVISOR_event_channel_op(EVTCHNOP_close, &close) != 0)
		BUG();

	bind_evtchn_to_cpu(evtchn, 0);
	evtchn_to_irq[evtchn] = -1;
	irq_cfg(irq)->info = mk_irq_info(IRQT_PIRQ, index_from_irq(irq), 0);
}

static void unmask_pirq(unsigned int irq)
{
	startup_pirq(irq);
}

static void mask_pirq(unsigned int irq)
{
}

static void ack_pirq(unsigned int irq)
{
	int evtchn = evtchn_from_irq(irq);

	move_native_irq(irq);

	if (VALID_EVTCHN(evtchn)) {
		mask_evtchn(evtchn);
		clear_evtchn(evtchn);
	}
}

static void end_pirq(unsigned int irq)
{
	int evtchn = evtchn_from_irq(irq);

	if ((irq_to_desc(irq)->status & (IRQ_DISABLED|IRQ_PENDING)) ==
	    (IRQ_DISABLED|IRQ_PENDING)) {
		shutdown_pirq(irq);
	} else if (VALID_EVTCHN(evtchn))
		pirq_unmask_and_notify(evtchn, irq);
}

static struct irq_chip pirq_chip = {
	.name     = "Phys",
	.startup  = startup_pirq,
	.shutdown = shutdown_pirq,
	.mask     = mask_pirq,
	.unmask   = unmask_pirq,
	.mask_ack = ack_pirq,
	.ack      = ack_pirq,
	.end      = end_pirq,
	.set_type = set_type_pirq,
#ifdef CONFIG_SMP
	.set_affinity = set_affinity_irq,
#endif
	.retrigger = resend_irq_on_evtchn,
};

int irq_ignore_unhandled(unsigned int irq)
{
	struct physdev_irq_status_query irq_status = { .irq = irq };

	if (!is_running_on_xen())
		return 0;

	if (HYPERVISOR_physdev_op(PHYSDEVOP_irq_status_query, &irq_status))
		return 0;
	return !!(irq_status.flags & XENIRQSTAT_shared);
}

#if defined(CONFIG_SMP) && !defined(PER_CPU_IPI_IRQ)
void notify_remote_via_ipi(unsigned int ipi, unsigned int cpu)
{
	int evtchn = evtchn_from_per_cpu_irq(ipi_to_irq[ipi], cpu);

	if (VALID_EVTCHN(evtchn))
		notify_remote_via_evtchn(evtchn);
}
#endif

void notify_remote_via_irq(int irq)
{
	int evtchn = evtchn_from_irq(irq);

	BUG_ON(type_from_irq(irq) == IRQT_VIRQ);
	BUG_IF_IPI(irq);

	if (VALID_EVTCHN(evtchn))
		notify_remote_via_evtchn(evtchn);
}
EXPORT_SYMBOL_GPL(notify_remote_via_irq);

int multi_notify_remote_via_irq(multicall_entry_t *mcl, int irq)
{
	int evtchn = evtchn_from_irq(irq);

	BUG_ON(type_from_irq(irq) == IRQT_VIRQ);
	BUG_IF_IPI(irq);

	if (!VALID_EVTCHN(evtchn))
		return -EINVAL;

	multi_notify_remote_via_evtchn(mcl, evtchn);
	return 0;
}
EXPORT_SYMBOL_GPL(multi_notify_remote_via_irq);

int irq_to_evtchn_port(int irq)
{
	BUG_IF_VIRQ_PER_CPU(irq);
	BUG_IF_IPI(irq);
	return evtchn_from_irq(irq);
}
EXPORT_SYMBOL_GPL(irq_to_evtchn_port);

void mask_evtchn(int port)
{
	shared_info_t *s = HYPERVISOR_shared_info;
	synch_set_bit(port, s->evtchn_mask);
}
EXPORT_SYMBOL_GPL(mask_evtchn);

void unmask_evtchn(int port)
{
	shared_info_t *s = HYPERVISOR_shared_info;
	unsigned int cpu = smp_processor_id();

	BUG_ON(!irqs_disabled());

	/* Slow path (hypercall) if this is a non-local port. */
	if (unlikely(cpu != cpu_from_evtchn(port))) {
		struct evtchn_unmask unmask = { .port = port };
		VOID(HYPERVISOR_event_channel_op(EVTCHNOP_unmask, &unmask));
		return;
	}

	synch_clear_bit(port, s->evtchn_mask);

	/* Did we miss an interrupt 'edge'? Re-fire if so. */
	if (synch_test_bit(port, s->evtchn_pending)) {
		vcpu_info_t *vcpu_info = current_vcpu_info();

		if (!synch_test_and_set_bit(port / BITS_PER_LONG,
					    &vcpu_info->evtchn_pending_sel))
			vcpu_info->evtchn_upcall_pending = 1;
	}
}
EXPORT_SYMBOL_GPL(unmask_evtchn);

void disable_all_local_evtchn(void)
{
	unsigned i, cpu = smp_processor_id();
	shared_info_t *s = HYPERVISOR_shared_info;

	for (i = 0; i < NR_EVENT_CHANNELS; ++i)
		if (cpu_from_evtchn(i) == cpu)
			synch_set_bit(i, &s->evtchn_mask[0]);
}

/* Clear an irq's pending state, in preparation for polling on it. */
void xen_clear_irq_pending(int irq)
{
	int evtchn = evtchn_from_irq(irq);

	if (VALID_EVTCHN(evtchn))
		clear_evtchn(evtchn);
}

/* Set an irq's pending state, to avoid blocking on it. */
void xen_set_irq_pending(int irq)
{
	int evtchn = evtchn_from_irq(irq);

	if (VALID_EVTCHN(evtchn))
		set_evtchn(evtchn);
}

/* Test an irq's pending state. */
int xen_test_irq_pending(int irq)
{
	int evtchn = evtchn_from_irq(irq);

	return VALID_EVTCHN(evtchn) && test_evtchn(evtchn);
}

/* Poll waiting for an irq to become pending.  In the usual case, the
   irq will be disabled so it won't deliver an interrupt. */
void xen_poll_irq(int irq)
{
	evtchn_port_t evtchn = evtchn_from_irq(irq);

	if (VALID_EVTCHN(evtchn)
	    && HYPERVISOR_poll_no_timeout(&evtchn, 1))
		BUG();
}

#ifdef CONFIG_PM_SLEEP
static void restore_cpu_virqs(unsigned int cpu)
{
	struct evtchn_bind_virq bind_virq;
	int virq, irq, evtchn;

	for (virq = 0; virq < NR_VIRQS; virq++) {
		if ((irq = per_cpu(virq_to_irq, cpu)[virq]) == -1)
			continue;

#ifndef PER_CPU_VIRQ_IRQ
		if (test_bit(virq, virq_per_cpu)
		    && !VALID_EVTCHN(per_cpu(virq_to_evtchn, cpu)[virq]))
			continue;
#endif

		BUG_ON(irq_cfg(irq)->info != mk_irq_info(IRQT_VIRQ, virq, 0));

		/* Get a new binding from Xen. */
		bind_virq.virq = virq;
		bind_virq.vcpu = cpu;
		if (HYPERVISOR_event_channel_op(EVTCHNOP_bind_virq,
						&bind_virq) != 0)
			BUG();
		evtchn = bind_virq.port;

		/* Record the new mapping. */
		evtchn_to_irq[evtchn] = irq;
#ifdef PER_CPU_VIRQ_IRQ
		irq_cfg(irq)->info = mk_irq_info(IRQT_VIRQ, virq, evtchn);
#else
		if (test_bit(virq, virq_per_cpu))
			per_cpu(virq_to_evtchn, cpu)[virq] = evtchn;
		else {
			unsigned int cpu;

			irq_cfg(irq)->info = mk_irq_info(IRQT_VIRQ, virq,
							 evtchn);
			for_each_possible_cpu(cpu)
				per_cpu(virq_to_evtchn, cpu)[virq] = evtchn;
		}
#endif
		bind_evtchn_to_cpu(evtchn, cpu);

		/* Ready for use. */
		unmask_evtchn(evtchn);
	}
}

static void restore_cpu_ipis(unsigned int cpu)
{
#ifdef CONFIG_SMP
	struct evtchn_bind_ipi bind_ipi;
	int ipi, irq, evtchn;

	for (ipi = 0; ipi < NR_IPIS; ipi++) {
#ifdef PER_CPU_IPI_IRQ
		if ((irq = per_cpu(ipi_to_irq, cpu)[ipi]) == -1)
#else
		if ((irq = ipi_to_irq[ipi]) == -1
		    || !VALID_EVTCHN(per_cpu(ipi_to_evtchn, cpu)[ipi]))
#endif
			continue;

		BUG_ON(irq_cfg(irq)->info != mk_irq_info(IRQT_IPI, ipi, 0));

		/* Get a new binding from Xen. */
		bind_ipi.vcpu = cpu;
		if (HYPERVISOR_event_channel_op(EVTCHNOP_bind_ipi,
						&bind_ipi) != 0)
			BUG();
		evtchn = bind_ipi.port;

		/* Record the new mapping. */
		evtchn_to_irq[evtchn] = irq;
#ifdef PER_CPU_IPI_IRQ
		irq_cfg(irq)->info = mk_irq_info(IRQT_IPI, ipi, evtchn);
#else
		per_cpu(ipi_to_evtchn, cpu)[ipi] = evtchn;
#endif
		bind_evtchn_to_cpu(evtchn, cpu);

		/* Ready for use. */
		if (!(irq_to_desc(irq)->status & IRQ_DISABLED))
			unmask_evtchn(evtchn);
	}
#endif
}

static int evtchn_resume(struct sys_device *dev)
{
	unsigned int cpu, irq, evtchn;
	struct irq_cfg *cfg;
	struct evtchn_status status;

	/* Avoid doing anything in the 'suspend cancelled' case. */
	status.dom = DOMID_SELF;
#ifdef PER_CPU_VIRQ_IRQ
	status.port = evtchn_from_irq(percpu_read(virq_to_irq[VIRQ_TIMER]));
#else
	status.port = percpu_read(virq_to_evtchn[VIRQ_TIMER]);
#endif
	if (HYPERVISOR_event_channel_op(EVTCHNOP_status, &status))
		BUG();
	if (status.status == EVTCHNSTAT_virq
	    && status.vcpu == smp_processor_id()
	    && status.u.virq == VIRQ_TIMER)
		return 0;

	init_evtchn_cpu_bindings();

	if (pirq_eoi_does_unmask) {
		struct physdev_pirq_eoi_gmfn eoi_gmfn;

		eoi_gmfn.gmfn = virt_to_machine(pirq_needs_eoi) >> PAGE_SHIFT;
		if (HYPERVISOR_physdev_op(PHYSDEVOP_pirq_eoi_gmfn, &eoi_gmfn))
			BUG();
	}

	/* New event-channel space is not 'live' yet. */
	for (evtchn = 0; evtchn < NR_EVENT_CHANNELS; evtchn++)
		mask_evtchn(evtchn);

	/* Check that no PIRQs are still bound. */
	for (irq = PIRQ_BASE; irq < (PIRQ_BASE + nr_pirqs); irq++) {
		cfg = irq_cfg(irq);
		BUG_ON(cfg && cfg->info != IRQ_UNBOUND);
	}

	/* No IRQ <-> event-channel mappings. */
	for (irq = 0; irq < nr_irqs; irq++) {
		cfg = irq_cfg(irq);
		if (cfg)
			cfg->info &= ~((1U << _EVTCHN_BITS) - 1);
	}
	for (evtchn = 0; evtchn < NR_EVENT_CHANNELS; evtchn++)
		evtchn_to_irq[evtchn] = -1;

	for_each_possible_cpu(cpu) {
		restore_cpu_virqs(cpu);
		restore_cpu_ipis(cpu);
	}

	return 0;
}

static struct sysdev_class evtchn_sysclass = {
	.name		= "evtchn",
	.resume		= evtchn_resume,
};

static struct sys_device device_evtchn = {
	.id		= 0,
	.cls		= &evtchn_sysclass,
};

static int __init evtchn_register(void)
{
	int err;

	if (is_initial_xendomain())
		return 0;

	err = sysdev_class_register(&evtchn_sysclass);
	if (!err)
		err = sysdev_register(&device_evtchn);
	return err;
}
core_initcall(evtchn_register);
#endif

int __init arch_early_irq_init(void)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(_irq_cfg); i++)
		irq_to_desc(i)->chip_data = _irq_cfg + i;

	return 0;
}

#ifdef CONFIG_SPARSE_IRQ
int arch_init_chip_data(struct irq_desc *desc, int cpu)
{
	if (!desc->chip_data) {
		/* By default all event channels notify CPU#0. */
		cpumask_copy(desc->affinity, cpumask_of(0));

		desc->chip_data = kzalloc(sizeof(struct irq_cfg), GFP_ATOMIC);
	}
	if (!desc->chip_data) {
		printk(KERN_ERR "cannot alloc irq_cfg\n");
		BUG();
	}

	return 0;
}
#endif

#if defined(CONFIG_X86_IO_APIC)
#ifdef CONFIG_SPARSE_IRQ
int nr_pirqs = NR_PIRQS;
EXPORT_SYMBOL_GPL(nr_pirqs);

int __init arch_probe_nr_irqs(void)
{
	int nr_irqs_gsi, nr = acpi_probe_gsi();

	if (nr <= NR_IRQS_LEGACY) {
		/* for acpi=off or acpi not compiled in */
		int idx;

		for (nr = idx = 0; idx < nr_ioapics; idx++)
			nr += io_apic_get_redir_entries(idx) + 1;
	}
	nr_irqs_gsi = max(nr, NR_IRQS_LEGACY);

	nr = nr_irqs_gsi + 8 * nr_cpu_ids;
#ifdef CONFIG_PCI_MSI
	nr += nr_irqs_gsi * 16;
#endif
	if (nr_pirqs > nr) {
		nr_pirqs = nr;
		nr_irqs = nr + NR_DYNIRQS;
	}

	printk(KERN_DEBUG "nr_irqs_gsi=%d nr_pirqs=%d\n",
	       nr_irqs_gsi, nr_pirqs);

	return 0;
}
#endif

int assign_irq_vector(int irq, struct irq_cfg *cfg, const struct cpumask *mask)
{
	struct physdev_irq irq_op;

	if (irq < PIRQ_BASE || irq - PIRQ_BASE >= nr_pirqs)
		return -EINVAL;

	if (cfg->vector)
		return 0;

	irq_op.irq = irq;
	if (HYPERVISOR_physdev_op(PHYSDEVOP_alloc_irq_vector, &irq_op))
		return -ENOSPC;

	cfg->vector = irq_op.vector;

	return 0;
}
#define identity_mapped_irq(irq) (!IO_APIC_IRQ((irq) - PIRQ_BASE))
#elif defined(CONFIG_X86)
#define identity_mapped_irq(irq) (((irq) - PIRQ_BASE) < NR_IRQS_LEGACY)
#else
#define identity_mapped_irq(irq) (1)
#endif

void evtchn_register_pirq(int irq)
{
	BUG_ON(irq < PIRQ_BASE || irq - PIRQ_BASE >= nr_pirqs);
	if (identity_mapped_irq(irq) || type_from_irq(irq) != IRQT_UNBOUND)
		return;
	irq_cfg(irq)->info = mk_irq_info(IRQT_PIRQ, irq, 0);
	set_irq_chip_and_handler_name(irq, &pirq_chip, handle_level_irq,
				      "level");
}

int evtchn_map_pirq(int irq, int xen_pirq)
{
	if (irq < 0) {
		static DEFINE_SPINLOCK(irq_alloc_lock);

		irq = PIRQ_BASE + nr_pirqs - 1;
		spin_lock(&irq_alloc_lock);
		do {
			struct irq_desc *desc;
			struct irq_cfg *cfg;

			if (identity_mapped_irq(irq))
				continue;
			desc = irq_to_desc_alloc_node(irq, numa_node_id());
			cfg = desc->chip_data;
			if (!index_from_irq(irq)) {
				BUG_ON(type_from_irq(irq) != IRQT_UNBOUND);
				cfg->info = mk_irq_info(IRQT_PIRQ,
							xen_pirq, 0);
				break;
			}
		} while (--irq >= PIRQ_BASE);
		spin_unlock(&irq_alloc_lock);
		if (irq < PIRQ_BASE)
			return -ENOSPC;
		set_irq_chip_and_handler_name(irq, &pirq_chip,
					      handle_level_irq, "level");
	} else if (!xen_pirq) {
		if (unlikely(type_from_irq(irq) != IRQT_PIRQ))
			return -EINVAL;
		/*
		 * dynamic_irq_cleanup(irq) would seem to be the correct thing
		 * here, but cannot be used as we get here also during shutdown
		 * when a driver didn't free_irq() its MSI(-X) IRQ(s), which
		 * then causes a warning in dynamic_irq_cleanup().
		 */
		set_irq_chip_and_handler(irq, NULL, NULL);
		irq_cfg(irq)->info = IRQ_UNBOUND;
		return 0;
	} else if (type_from_irq(irq) != IRQT_PIRQ
		   || index_from_irq(irq) != xen_pirq) {
		printk(KERN_ERR "IRQ#%d is already mapped to %d:%u - "
				"cannot map to PIRQ#%u\n",
		       irq, type_from_irq(irq), index_from_irq(irq), xen_pirq);
		return -EINVAL;
	}
	return index_from_irq(irq) ? irq : -EINVAL;
}

int evtchn_get_xen_pirq(int irq)
{
	if (identity_mapped_irq(irq))
		return irq;
	BUG_ON(type_from_irq(irq) != IRQT_PIRQ);
	return index_from_irq(irq);
}

void __init xen_init_IRQ(void)
{
	unsigned int i;
	struct physdev_pirq_eoi_gmfn eoi_gmfn;

#ifndef PER_CPU_VIRQ_IRQ
	__set_bit(VIRQ_TIMER, virq_per_cpu);
	__set_bit(VIRQ_DEBUG, virq_per_cpu);
	__set_bit(VIRQ_XENOPROF, virq_per_cpu);
#ifdef CONFIG_IA64
	__set_bit(VIRQ_ITC, virq_per_cpu);
#endif
#endif

	init_evtchn_cpu_bindings();

	i = get_order(sizeof(unsigned long) * BITS_TO_LONGS(nr_pirqs));
	pirq_needs_eoi = (void *)__get_free_pages(GFP_KERNEL|__GFP_ZERO, i);
	BUILD_BUG_ON(NR_PIRQS > PAGE_SIZE * 8);
 	eoi_gmfn.gmfn = virt_to_machine(pirq_needs_eoi) >> PAGE_SHIFT;
	if (HYPERVISOR_physdev_op(PHYSDEVOP_pirq_eoi_gmfn, &eoi_gmfn) == 0)
		pirq_eoi_does_unmask = true;

	/* No event channels are 'live' right now. */
	for (i = 0; i < NR_EVENT_CHANNELS; i++)
		mask_evtchn(i);

#ifndef CONFIG_SPARSE_IRQ
	for (i = DYNIRQ_BASE; i < (DYNIRQ_BASE + NR_DYNIRQS); i++) {
		irq_to_desc(i)->status |= IRQ_NOPROBE;
		set_irq_chip_and_handler_name(i, &dynirq_chip,
					      handle_level_irq, "level");
	}

	for (i = PIRQ_BASE; i < (PIRQ_BASE + nr_pirqs); i++) {
#else
	for (i = PIRQ_BASE; i < (PIRQ_BASE + NR_IRQS_LEGACY); i++) {
#endif
		if (!identity_mapped_irq(i))
			continue;

#ifdef RTC_IRQ
		/* If not domain 0, force our RTC driver to fail its probe. */
		if (i - PIRQ_BASE == RTC_IRQ && !is_initial_xendomain())
			continue;
#endif

		set_irq_chip_and_handler_name(i, &pirq_chip,
					      handle_level_irq, "level");
	}
}
