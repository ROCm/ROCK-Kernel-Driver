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
#include <linux/slab.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/kconfig.h>
#include <linux/sched.h>
#include <linux/kernel_stat.h>
#include <linux/ftrace.h>
#include <linux/atomic.h>
#include <asm/barrier.h>
#include <asm/ptrace.h>
#include <xen/evtchn.h>
#include <xen/interface/event_channel.h>
#include <xen/interface/physdev.h>
#include <asm/hypervisor.h>
#include <linux/mc146818rtc.h> /* RTC_IRQ */
#include "../../../kernel/irq/internals.h" /* IRQS_AUTODETECT, IRQS_PENDING */

/*
 * This lock protects updates to the following mapping and reference-count
 * arrays. The lock does not need to be acquired to read the mapping tables.
 */
static DEFINE_SPINLOCK(irq_mapping_update_lock);

/* IRQ <-> event-channel mappings. */
static int evtchn_to_irq[EVTCHN_2L_NR_CHANNELS] = {
	[0 ...  EVTCHN_2L_NR_CHANNELS-1] = -1 };

#if defined(CONFIG_SMP) && defined(CONFIG_X86)
static struct percpu_irqaction {
	struct irqaction action; /* must be first */
	struct percpu_irqaction *next;
	cpumask_var_t cpus;
} *virq_actions[NR_VIRQS];
/* IRQ <-> VIRQ mapping. */
static DECLARE_BITMAP(virq_per_cpu, NR_VIRQS) __read_mostly;
static DEFINE_PER_CPU_READ_MOSTLY(int[NR_VIRQS], virq_to_evtchn);
#define BUG_IF_VIRQ_PER_CPU(irq_cfg) \
	BUG_ON(type_from_irq_cfg(irq_cfg) == IRQT_VIRQ \
	       && test_bit(index_from_irq_cfg(irq_cfg), virq_per_cpu))
#else
#define BUG_IF_VIRQ_PER_CPU(irq_cfg) ((void)0)
#define PER_CPU_VIRQ_IRQ
#endif

/* IRQ <-> IPI mapping. */
#if defined(CONFIG_SMP) && defined(CONFIG_X86)
static int __read_mostly ipi_irq = -1;
DEFINE_PER_CPU(DECLARE_BITMAP(, NR_IPIS), ipi_pending);
static DEFINE_PER_CPU_READ_MOSTLY(evtchn_port_t, ipi_evtchn);
#else
#define PER_CPU_IPI_IRQ
#endif
#if !defined(CONFIG_SMP) || !defined(PER_CPU_IPI_IRQ)
#define BUG_IF_IPI(irq_cfg) BUG_ON(type_from_irq_cfg(irq_cfg) == IRQT_IPI)
#else
#define BUG_IF_IPI(irq_cfg) ((void)0)
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
	return irq_get_chip_data(irq);
#else
	return irq < NR_IRQS ? _irq_cfg + irq : NULL;
#endif
}

static inline struct irq_cfg *__pure irq_data_cfg(struct irq_data *data)
{
	return irq_data_get_irq_chip_data(data);
}

/* Constructor for packed IRQ information. */
static inline u32 mk_irq_info(u32 type, u32 index, u32 evtchn)
{
	BUILD_BUG_ON(_IRQT_COUNT > (1U << _IRQT_BITS));

	BUILD_BUG_ON(NR_PIRQS > (1U << _INDEX_BITS));
	BUILD_BUG_ON(NR_VIRQS > (1U << _INDEX_BITS));
#if defined(PER_CPU_IPI_IRQ) && defined(NR_IPIS)
	BUILD_BUG_ON(NR_IPIS > (1U << _INDEX_BITS));
#endif
	BUG_ON(index >> _INDEX_BITS);

	BUILD_BUG_ON(EVTCHN_2L_NR_CHANNELS > (1U << _EVTCHN_BITS));

	return ((type << (32 - _IRQT_BITS)) | (index << _EVTCHN_BITS) | evtchn);
}

/*
 * Accessors for packed IRQ information.
 */

static inline unsigned int index_from_irq_cfg(const struct irq_cfg *cfg)
{
	return (cfg->info >> _EVTCHN_BITS) & ((1U << _INDEX_BITS) - 1);
}

static inline unsigned int index_from_irq(int irq)
{
	const struct irq_cfg *cfg = irq_cfg(irq);

	return cfg ? index_from_irq_cfg(cfg) : 0;
}

static inline unsigned int type_from_irq_cfg(const struct irq_cfg *cfg)
{
	return cfg->info >> (32 - _IRQT_BITS);
}

static inline unsigned int type_from_irq(int irq)
{
	const struct irq_cfg *cfg = irq_cfg(irq);

	return cfg ? type_from_irq_cfg(cfg) : IRQT_UNBOUND;
}

static inline unsigned int evtchn_from_per_cpu_irq(const struct irq_cfg *cfg,
						   unsigned int cpu)
{
	switch (type_from_irq_cfg(cfg)) {
#ifndef PER_CPU_VIRQ_IRQ
	case IRQT_VIRQ:
		return per_cpu(virq_to_evtchn, cpu)[index_from_irq_cfg(cfg)];
#endif
#ifndef PER_CPU_IPI_IRQ
	case IRQT_IPI:
		return per_cpu(ipi_evtchn, cpu);
#endif
	}
	BUG();
	return 0;
}

static inline unsigned int evtchn_from_irq_cfg(const struct irq_cfg *cfg)
{
	switch (type_from_irq_cfg(cfg)) {
#ifndef PER_CPU_VIRQ_IRQ
	case IRQT_VIRQ:
#endif
#ifndef PER_CPU_IPI_IRQ
	case IRQT_IPI:
#endif
		return evtchn_from_per_cpu_irq(cfg, smp_processor_id());
	}
	return cfg->info & ((1U << _EVTCHN_BITS) - 1);
}

static inline unsigned int evtchn_from_irq_data(struct irq_data *data)
{
	const struct irq_cfg *cfg = irq_data_cfg(data);

	return cfg ? evtchn_from_irq_cfg(cfg) : 0;
}

static inline unsigned int evtchn_from_irq(int irq)
{
	struct irq_data *data = irq_get_irq_data(irq);

	return data ? evtchn_from_irq_data(data) : 0;
}

unsigned int irq_from_evtchn(unsigned int port)
{
	return evtchn_to_irq[port];
}
EXPORT_SYMBOL_GPL(irq_from_evtchn);

/* IRQ <-> VIRQ mapping. */
DEFINE_PER_CPU(int[NR_VIRQS], virq_to_irq) = {[0 ... NR_VIRQS-1] = -1};

#if defined(CONFIG_SMP) && defined(PER_CPU_IPI_IRQ)
/* IRQ <-> IPI mapping. */
#ifndef NR_IPIS
#define NR_IPIS 1
#endif
DEFINE_PER_CPU(int[NR_IPIS], ipi_to_irq) = {[0 ... NR_IPIS-1] = -1};
#endif

#ifdef CONFIG_SMP

#if CONFIG_NR_CPUS <= 256
static u8 cpu_evtchn[EVTCHN_2L_NR_CHANNELS];
#else
static u16 cpu_evtchn[EVTCHN_2L_NR_CHANNELS];
#endif
static DEFINE_PER_CPU(unsigned long[BITS_TO_LONGS(EVTCHN_2L_NR_CHANNELS)],
		      cpu_evtchn_mask);

static inline unsigned long active_evtchns(unsigned int idx)
{
	shared_info_t *sh = HYPERVISOR_shared_info;

	return (sh->evtchn_pending[idx] &
		this_cpu_read(cpu_evtchn_mask[idx]) &
		~sh->evtchn_mask[idx]);
}

static void _bind_evtchn_to_cpu(unsigned int chn, unsigned int cpu,
				struct irq_data *data,
				const struct cpumask *cpumask)
{
	shared_info_t *s = HYPERVISOR_shared_info;

	BUG_ON(!test_bit(chn, s->evtchn_mask));

	if (data) {
		BUG_ON(!cpumask_test_cpu(cpu, cpumask));
		if (!irqd_is_per_cpu(data))
			cpumask_copy(data->affinity, cpumask);
		else
			cpumask_set_cpu(cpu, data->affinity);
	}

	clear_bit(chn, per_cpu(cpu_evtchn_mask, cpu_evtchn[chn]));
	set_bit(chn, per_cpu(cpu_evtchn_mask, cpu));
	cpu_evtchn[chn] = cpu;
}

static void bind_evtchn_to_cpu(unsigned int chn, unsigned int cpu)
{
	int irq = evtchn_to_irq[chn];

	_bind_evtchn_to_cpu(chn, cpu,
			    irq != -1 ? irq_get_irq_data(irq) : NULL,
			    cpumask_of(cpu));
}

static void init_evtchn_cpu_bindings(void)
{
	int i;

	/* By default all event channels notify CPU#0. */
	for (i = 0; i < nr_irqs; i++) {
		struct irq_data *data = irq_get_irq_data(i);

		if (data)
			cpumask_copy(data->affinity, cpumask_of(0));
	}

	memset(cpu_evtchn, 0, sizeof(cpu_evtchn));
	for_each_possible_cpu(i)
		memset(per_cpu(cpu_evtchn_mask, i), -!i,
		       sizeof(per_cpu(cpu_evtchn_mask, i)));
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

static void _bind_evtchn_to_cpu(unsigned int chn, unsigned int cpu,
				struct irq_data *data,
				const struct cpumask *cpumask)
{
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

#define UPC_INACTIVE 0
#define UPC_ACTIVE 1
#define UPC_NESTED_LATCH 2
#define UPC_RESTART (UPC_ACTIVE|UPC_NESTED_LATCH)
static DEFINE_PER_CPU(unsigned int, upcall_state);
static DEFINE_PER_CPU(unsigned int, current_l1i);
static DEFINE_PER_CPU(unsigned int, current_l2i);

#ifndef vcpu_info_xchg
#define vcpu_info_xchg(fld, val) xchg(&current_vcpu_info()->fld, val)
#endif

/* NB. Interrupts are disabled on entry. */
asmlinkage
#ifdef CONFIG_PREEMPT
void
#define return(x) return
#else
bool
#endif
__irq_entry evtchn_do_upcall(struct pt_regs *regs)
{
	unsigned long       l1, l2;
	unsigned long       masked_l1, masked_l2;
	unsigned int        l1i, l2i, start_l1i, start_l2i, port, i;
	int                 irq;
	struct pt_regs     *old_regs;

	/* Nested invocations bail immediately. */
	if (unlikely(__this_cpu_cmpxchg(upcall_state, UPC_INACTIVE,
					UPC_ACTIVE) != UPC_INACTIVE)) {
		__this_cpu_or(upcall_state, UPC_NESTED_LATCH);
		/* Avoid a callback storm when we reenable delivery. */
		vcpu_info_write(evtchn_upcall_pending, 0);
		return(false);
	}

	old_regs = set_irq_regs(regs);
	xen_spin_irq_enter();
	irq_enter();
	exit_idle();

	do {
		vcpu_info_write(evtchn_upcall_pending, 0);

#ifndef CONFIG_X86 /* No need for a barrier -- XCHG is a barrier on x86. */
		/* Clear master flag /before/ clearing selector flag. */
		wmb();
#else
		barrier();
#endif

#ifndef CONFIG_NO_HZ
		/*
		 * Handle timer interrupts before all others, so that all
		 * hardirq handlers see an up-to-date system time even if we
		 * have just woken from a long idle period.
		 */
#ifdef PER_CPU_VIRQ_IRQ
		if ((irq = __this_cpu_read(virq_to_irq[VIRQ_TIMER])) != -1) {
			port = evtchn_from_irq(irq);
#else
		port = __this_cpu_read(virq_to_evtchn[VIRQ_TIMER]);
		if (VALID_EVTCHN(port)) {
#endif
			l1i = port / BITS_PER_LONG;
			l2i = port % BITS_PER_LONG;
			if (active_evtchns(l1i) & (1ul<<l2i)) {
				mask_evtchn(port);
				clear_evtchn(port);
#ifndef PER_CPU_VIRQ_IRQ
				irq = evtchn_to_irq[port];
				BUG_ON(irq == -1);
#endif
				if (!handle_irq(irq, regs))
					BUG();
			}
		}
#endif /* CONFIG_NO_HZ */

		l1 = vcpu_info_xchg(evtchn_pending_sel, 0);

		start_l1i = l1i = __this_cpu_read(current_l1i);
		start_l2i = __this_cpu_read(current_l2i);

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
				bool handled = false;

				masked_l2 = l2 & ((~0UL) << l2i);
				if (masked_l2 == 0)
					break;
				l2i = __ffs(masked_l2);

				/* process port */
				port = (l1i * BITS_PER_LONG) + l2i;
				mask_evtchn(port);
				if ((irq = evtchn_to_irq[port]) != -1) {
#ifndef PER_CPU_IPI_IRQ
					if (port != __this_cpu_read(ipi_evtchn))
#endif
						clear_evtchn(port);
					handled = handle_irq(irq, regs);
				}
				if (!handled)
					pr_emerg_ratelimited("No handler for irq %d (port %u)\n",
							     irq, port);

				l2i = (l2i + 1) % BITS_PER_LONG;

				/* Next caller starts at last processed + 1 */
				__this_cpu_write(current_l1i,
					l2i ? l1i : (l1i + 1) % BITS_PER_LONG);
				__this_cpu_write(current_l2i, l2i);

			} while (l2i != 0);

			/* Scan start_l1i twice; all others once. */
			if ((l1i != start_l1i) || (i != 0))
				l1 &= ~(1UL << l1i);

			l1i = (l1i + 1) % BITS_PER_LONG;
		}

		/* If there were nested callbacks then we have more to do. */
	} while (unlikely(__this_cpu_cmpxchg(upcall_state, UPC_RESTART,
					     UPC_ACTIVE) == UPC_RESTART));

	__this_cpu_write(upcall_state, UPC_INACTIVE);
	irq_exit();
	xen_spin_irq_exit();
	set_irq_regs(old_regs);

	return(__this_cpu_read(privcmd_hcall) && in_hypercall(regs));
#undef return
}

static int find_unbound_irq(unsigned int node, struct irq_cfg **pcfg,
			    struct irq_chip *chip, unsigned int nr)
{
	static int warned;
	unsigned int count = 0;
	int irq, result = -ENOSPC;

	for (irq = DYNIRQ_BASE; irq < nr_irqs; irq++) {
		struct irq_cfg *cfg = alloc_irq_and_cfg_at(irq, node);
		struct irq_data *data = irq_get_irq_data(irq);

		if (unlikely(!cfg))
			return -ENOMEM;

		if ((data->chip == &no_irq_chip || data->chip == chip)
		    && !cfg->bindcount) {
			irq_flow_handler_t handle;
			const char *name;

			if (nr > 1) {
				if (!count)
					result = irq;
				if (++count == nr)
					break;
				continue;
			}

			*pcfg = cfg;
			irq_set_noprobe(irq);
			if (nr) {
				handle = handle_fasteoi_irq;
				name = "fasteoi";
			} else {
				handle = handle_percpu_irq;
				name = "percpu";
			}
			irq_set_chip_and_handler_name(irq, chip,
						      handle, name);
			return irq;
		}
		count = 0;
		result = -ENOSPC;
	}

	if (nr > 1 && count == nr) {
		BUG_ON(pcfg);
		for (irq = result; count--; ++irq) {
			irq_set_noprobe(irq);
			irq_set_chip_and_handler_name(irq, chip,
						      handle_fasteoi_irq, "fasteoi");
		}
		return result;
	}

	if (!warned) {
		warned = 1;
		pr_warning("No available IRQ to bind to: "
			   "increase NR_DYNIRQS.\n");
	}

	return -ENOSPC;
}

static struct irq_chip dynirq_chip;

static int bind_caller_port_to_irq(unsigned int caller_port)
{
	struct irq_cfg *cfg;
	int irq;

	spin_lock(&irq_mapping_update_lock);

	if ((irq = evtchn_to_irq[caller_port]) == -1) {
		if ((irq = find_unbound_irq(numa_node_id(), &cfg,
					    &dynirq_chip, 1)) < 0)
			goto out;

		evtchn_to_irq[caller_port] = irq;
		cfg->info = mk_irq_info(IRQT_CALLER_PORT, 0, caller_port);
	} else
		cfg = irq_cfg(irq);

	cfg->bindcount++;

 out:
	spin_unlock(&irq_mapping_update_lock);
	return irq;
}

static int bind_local_port_to_irq(unsigned int local_port)
{
	struct irq_cfg *cfg;
	int irq;

	spin_lock(&irq_mapping_update_lock);

	BUG_ON(evtchn_to_irq[local_port] != -1);

	if ((irq = find_unbound_irq(numa_node_id(), &cfg, &dynirq_chip,
				    1)) < 0) {
		if (close_evtchn(local_port))
			BUG();
		goto out;
	}

	evtchn_to_irq[local_port] = irq;
	cfg->info = mk_irq_info(IRQT_LOCAL_PORT, 0, local_port);
	cfg->bindcount++;

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
	struct irq_cfg *cfg;
	int evtchn, irq;

	spin_lock(&irq_mapping_update_lock);

	if ((irq = per_cpu(virq_to_irq, cpu)[virq]) == -1) {
		if ((irq = find_unbound_irq(cpu_to_node(cpu), &cfg,
					    &dynirq_chip, 1)) < 0)
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
		cfg->info = mk_irq_info(IRQT_VIRQ, virq, evtchn);

		per_cpu(virq_to_irq, cpu)[virq] = irq;

		bind_evtchn_to_cpu(evtchn, cpu);
	} else
		cfg = irq_cfg(irq);

	cfg->bindcount++;

 out:
	spin_unlock(&irq_mapping_update_lock);
	return irq;
}

#if defined(CONFIG_SMP) && defined(PER_CPU_IPI_IRQ)
static int bind_ipi_to_irq(unsigned int ipi, unsigned int cpu)
{
	struct evtchn_bind_ipi bind_ipi;
	struct irq_cfg *cfg;
	int evtchn, irq;

	spin_lock(&irq_mapping_update_lock);

	if ((irq = per_cpu(ipi_to_irq, cpu)[ipi]) == -1) {
		if ((irq = find_unbound_irq(cpu_to_node(cpu), &cfg,
					    &dynirq_chip, 1)) < 0)
			goto out;

		bind_ipi.vcpu = cpu;
		if (HYPERVISOR_event_channel_op(EVTCHNOP_bind_ipi,
						&bind_ipi) != 0)
			BUG();
		evtchn = bind_ipi.port;

		evtchn_to_irq[evtchn] = irq;
		cfg->info = mk_irq_info(IRQT_IPI, ipi, evtchn);

		per_cpu(ipi_to_irq, cpu)[ipi] = irq;

		bind_evtchn_to_cpu(evtchn, cpu);
	} else
		cfg = irq_cfg(irq);

	cfg->bindcount++;

 out:
	spin_unlock(&irq_mapping_update_lock);
	return irq;
}
#endif

static void unbind_from_irq(unsigned int irq)
{
	struct irq_cfg *cfg = irq_cfg(irq);
	unsigned int evtchn = evtchn_from_irq_cfg(cfg);

	BUG_IF_VIRQ_PER_CPU(cfg);
	BUG_IF_IPI(cfg);

	spin_lock(&irq_mapping_update_lock);

	if (!--cfg->bindcount && VALID_EVTCHN(evtchn)) {
		if ((type_from_irq_cfg(cfg) != IRQT_CALLER_PORT) &&
		    close_evtchn(evtchn))
			BUG();

		switch (type_from_irq_cfg(cfg)) {
		case IRQT_VIRQ:
			per_cpu(virq_to_irq, cpu_from_evtchn(evtchn))
				[index_from_irq_cfg(cfg)] = -1;
#ifndef PER_CPU_VIRQ_IRQ
			{
				unsigned int cpu;

				for_each_possible_cpu(cpu)
					per_cpu(virq_to_evtchn, cpu)
						[index_from_irq_cfg(cfg)] = 0;
			}
#endif
			break;
#if defined(CONFIG_SMP) && defined(PER_CPU_IPI_IRQ)
		case IRQT_IPI:
			per_cpu(ipi_to_irq, cpu_from_evtchn(evtchn))
				[index_from_irq_cfg(cfg)] = -1;
			break;
#endif
		default:
			break;
		}

		/* Closed ports are implicitly re-bound to VCPU0. */
		bind_evtchn_to_cpu(evtchn, 0);

		evtchn_to_irq[evtchn] = -1;
		cfg->info = IRQ_UNBOUND;

		dynamic_irq_cleanup(irq);
	}

	spin_unlock(&irq_mapping_update_lock);
}

#if !defined(PER_CPU_IPI_IRQ) || !defined(PER_CPU_VIRQ_IRQ)
static inline struct percpu_irqaction *alloc_percpu_irqaction(gfp_t gfp)
{
	struct percpu_irqaction *new = kzalloc(sizeof(*new), GFP_ATOMIC);

	if (new && !zalloc_cpumask_var(&new->cpus, gfp)) {
		kfree(new);
		new = NULL;
	}
	return new;
}

static inline void free_percpu_irqaction(struct percpu_irqaction *action)
{
	if (!action)
		return;
	free_cpumask_var(action->cpus);
	kfree(action);
}

void unbind_from_per_cpu_irq(unsigned int irq, unsigned int cpu,
			     struct irqaction *action)
{
	struct evtchn_close close;
	struct irq_data *data = irq_get_irq_data(irq);
	struct irq_cfg *cfg = irq_data_cfg(data);
	unsigned int evtchn = evtchn_from_per_cpu_irq(cfg, cpu);
	struct percpu_irqaction *free_action = NULL;

	spin_lock(&irq_mapping_update_lock);

	if (VALID_EVTCHN(evtchn)) {
		mask_evtchn(evtchn);

		BUG_ON(cfg->bindcount <= 1);
		cfg->bindcount--;

#ifndef PER_CPU_VIRQ_IRQ
		if (type_from_irq_cfg(cfg) == IRQT_VIRQ) {
			unsigned int virq = index_from_irq_cfg(cfg);
			struct percpu_irqaction *cur, *prev = NULL;

			cur = virq_actions[virq];
			while (cur) {
				if (cur->action.dev_id == action) {
					cpumask_clear_cpu(cpu, cur->cpus);
					if (cpumask_empty(cur->cpus)) {
						WARN_ON(free_action);
						if (prev)
							prev->next = cur->next;
						else
							virq_actions[virq]
								= cur->next;
						free_action = cur;
					}
				} else if (cpumask_test_cpu(cpu, cur->cpus))
					evtchn = 0;
				cur = (prev = cur)->next;
			}
			if (!VALID_EVTCHN(evtchn))
				goto done;
		}
#endif

		cpumask_clear_cpu(cpu, data->affinity);

		close.port = evtchn;
		if (HYPERVISOR_event_channel_op(EVTCHNOP_close, &close))
			BUG();

		switch (type_from_irq_cfg(cfg)) {
#ifndef PER_CPU_VIRQ_IRQ
		case IRQT_VIRQ:
			per_cpu(virq_to_evtchn, cpu)
				[index_from_irq_cfg(cfg)] = 0;
			break;
#endif
#ifndef PER_CPU_IPI_IRQ
		case IRQT_IPI:
			per_cpu(ipi_evtchn, cpu) = 0;
			break;
#endif
		default:
			BUG();
			break;
		}

		/* Closed ports are implicitly re-bound to VCPU0. */
		_bind_evtchn_to_cpu(evtchn, 0, NULL, NULL);

		evtchn_to_irq[evtchn] = -1;
	}

#ifndef PER_CPU_VIRQ_IRQ
done:
#endif
	spin_unlock(&irq_mapping_update_lock);

	if (free_action) {
		cpumask_t *cpus = free_action->cpus;

		free_irq(irq, free_action->action.dev_id);
		free_cpumask_var(cpus);
	}
}
EXPORT_SYMBOL_GPL(unbind_from_per_cpu_irq);
#endif /* !PER_CPU_IPI_IRQ || !PER_CPU_VIRQ_IRQ */

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

#ifndef PER_CPU_VIRQ_IRQ
	BUG_ON(test_bit(virq, virq_per_cpu));
#endif

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
	struct irq_cfg *cfg;
	unsigned int evtchn;
	int irq, retval = 0;
	struct percpu_irqaction *cur = NULL, *new;

	BUG_ON(!test_bit(virq, virq_per_cpu));

	if (action->dev_id)
		return -EINVAL;

	new = alloc_percpu_irqaction(GFP_ATOMIC);
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
		new = NULL;
		retval = 1;
	}
	cpumask_set_cpu(cpu, cur->cpus);
	action = &cur->action;

	if ((irq = per_cpu(virq_to_irq, cpu)[virq]) == -1) {
		unsigned int nr;

		BUG_ON(!retval);

		if ((irq = find_unbound_irq(cpu_to_node(cpu), &cfg,
					    &dynirq_chip, 0)) < 0) {
			virq_actions[virq] = cur->next;
			spin_unlock(&irq_mapping_update_lock);
			free_percpu_irqaction(new);
			return irq;
		}

		/* Extra reference so count will never drop to zero. */
		cfg->bindcount++;

		for_each_possible_cpu(nr)
			per_cpu(virq_to_irq, nr)[virq] = irq;
		cfg->info = mk_irq_info(IRQT_VIRQ, virq, 0);
	} else
		cfg = irq_cfg(irq);

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

	cfg->bindcount++;

	spin_unlock(&irq_mapping_update_lock);

	free_percpu_irqaction(new);

	if (retval == 0) {
		unsigned long flags;

		local_irq_save(flags);
		unmask_evtchn(evtchn);
		local_irq_restore(flags);
	} else {
		action->flags |= IRQF_PERCPU;
		retval = setup_irq(irq, action);
		if (retval) {
			unbind_from_per_cpu_irq(irq, cpu, action);
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
int bind_ipi_to_irqaction(
	unsigned int cpu,
	struct irqaction *action)
{
	struct evtchn_bind_ipi bind_ipi;
	struct irq_cfg *cfg;
	unsigned int evtchn;
	int retval = 0;

	spin_lock(&irq_mapping_update_lock);

	if (VALID_EVTCHN(per_cpu(ipi_evtchn, cpu))) {
		spin_unlock(&irq_mapping_update_lock);
		return -EBUSY;
	}

	if (ipi_irq < 0) {
		if ((ipi_irq = find_unbound_irq(cpu_to_node(cpu), &cfg,
						&dynirq_chip, 0)) < 0) {
			spin_unlock(&irq_mapping_update_lock);
			return ipi_irq;
		}

		/* Extra reference so count will never drop to zero. */
		cfg->bindcount++;

		cfg->info = mk_irq_info(IRQT_IPI, 0, 0);
		retval = 1;
	} else
		cfg = irq_cfg(ipi_irq);

	bind_ipi.vcpu = cpu;
	if (HYPERVISOR_event_channel_op(EVTCHNOP_bind_ipi, &bind_ipi))
		BUG();

	evtchn = bind_ipi.port;
	evtchn_to_irq[evtchn] = ipi_irq;
	per_cpu(ipi_evtchn, cpu) = evtchn;

	bind_evtchn_to_cpu(evtchn, cpu);

	cfg->bindcount++;

	spin_unlock(&irq_mapping_update_lock);

	if (retval == 0) {
		unsigned long flags;

		local_irq_save(flags);
		unmask_evtchn(evtchn);
		local_irq_restore(flags);
	} else {
		action->flags |= IRQF_PERCPU | IRQF_NO_SUSPEND;
		retval = setup_irq(ipi_irq, action);
		if (retval) {
			unbind_from_per_cpu_irq(ipi_irq, cpu, NULL);
			BUG_ON(retval > 0);
			ipi_irq = retval;
		}
	}

	return ipi_irq;
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
static int set_affinity_irq(struct irq_data *data,
			    const struct cpumask *dest, bool force)
{
	const struct irq_cfg *cfg = irq_data_cfg(data);
	unsigned int port = evtchn_from_irq_cfg(cfg);
	unsigned int cpu = cpumask_any(dest);
	struct evtchn_bind_vcpu ebv = { .port = port, .vcpu = cpu };
	bool masked;
	int rc;

	BUG_IF_VIRQ_PER_CPU(cfg);
	BUG_IF_IPI(cfg);

	if (!VALID_EVTCHN(port))
		return -ENXIO;

	masked = test_and_set_evtchn_mask(port);
	rc = HYPERVISOR_event_channel_op(EVTCHNOP_bind_vcpu, &ebv);
	if (rc == 0) {
		_bind_evtchn_to_cpu(port, cpu, data, dest);
		rc = IRQ_SET_MASK_OK_NOCOPY;
	}
	if (!masked)
		unmask_evtchn(port);

	return rc;
}
#endif

int resend_irq_on_evtchn(struct irq_data *data)
{
	unsigned int evtchn = evtchn_from_irq_data(data);
	bool masked;

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

static void unmask_dynirq(struct irq_data *data)
{
	unsigned int evtchn = evtchn_from_irq_data(data);

	if (VALID_EVTCHN(evtchn))
		unmask_evtchn(evtchn);
}

static void mask_dynirq(struct irq_data *data)
{
	unsigned int evtchn = evtchn_from_irq_data(data);

	if (VALID_EVTCHN(evtchn))
		mask_evtchn(evtchn);
}

static unsigned int startup_dynirq(struct irq_data *data)
{
	unmask_dynirq(data);
	return 0;
}

#define shutdown_dynirq mask_dynirq

static void end_dynirq(struct irq_data *data)
{
	if (!irqd_irq_disabled(data)) {
		irq_move_masked_irq(data);
		unmask_dynirq(data);
	}
}

static struct irq_chip dynirq_chip = {
	.name             = "Dynamic",
	.irq_startup      = startup_dynirq,
	.irq_shutdown     = shutdown_dynirq,
	.irq_enable       = unmask_dynirq,
	.irq_disable      = mask_dynirq,
	.irq_mask         = mask_dynirq,
	.irq_unmask       = unmask_dynirq,
	.irq_eoi          = end_dynirq,
#ifdef CONFIG_SMP
	.irq_set_affinity = set_affinity_irq,
#endif
	.irq_retrigger    = resend_irq_on_evtchn,
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

static int set_type_pirq(struct irq_data *data, unsigned int type)
{
	if (type != IRQ_TYPE_PROBE)
		return -EINVAL;
	set_bit(data->irq - PIRQ_BASE, probing_pirq);
	return 0;
}

static void enable_pirq(struct irq_data *data)
{
	struct evtchn_bind_pirq bind_pirq;
	struct irq_cfg *cfg = irq_data_cfg(data);
	unsigned int evtchn = evtchn_from_irq_cfg(cfg);
	unsigned int irq = data->irq, pirq = irq - PIRQ_BASE;

	if (VALID_EVTCHN(evtchn)) {
		if (pirq < nr_pirqs)
			clear_bit(pirq, probing_pirq);
		goto out;
	}

	bind_pirq.pirq = evtchn_get_xen_pirq(irq);
	/* NB. We are happy to share unless we are probing. */
	bind_pirq.flags = (pirq < nr_pirqs
			   && test_and_clear_bit(pirq, probing_pirq))
			  || (irq_to_desc(irq)->istate & IRQS_AUTODETECT)
			  ? 0 : BIND_PIRQ__WILL_SHARE;
	if (HYPERVISOR_event_channel_op(EVTCHNOP_bind_pirq, &bind_pirq) != 0) {
		if (bind_pirq.flags)
			pr_info("Failed to obtain physical IRQ %d\n", irq);
		return;
	}
	evtchn = bind_pirq.port;

	pirq_query_unmask(irq);

	evtchn_to_irq[evtchn] = irq;
	_bind_evtchn_to_cpu(evtchn, 0, NULL, NULL);
	cfg->info = mk_irq_info(IRQT_PIRQ, bind_pirq.pirq, evtchn);

 out:
	pirq_unmask_and_notify(evtchn, irq);
}

#define disable_pirq mask_pirq

static unsigned int startup_pirq(struct irq_data *data)
{
	enable_pirq(data);
	return 0;
}

static void shutdown_pirq(struct irq_data *data)
{
	struct irq_cfg *cfg = irq_data_cfg(data);
	unsigned int evtchn = evtchn_from_irq_cfg(cfg);

	if (!VALID_EVTCHN(evtchn))
		return;

	mask_evtchn(evtchn);

	if (close_evtchn(evtchn))
		BUG();

	bind_evtchn_to_cpu(evtchn, 0);
	evtchn_to_irq[evtchn] = -1;
	cfg->info = mk_irq_info(IRQT_PIRQ, index_from_irq_cfg(cfg), 0);
}

static void unmask_pirq(struct irq_data *data)
{
	unsigned int evtchn = evtchn_from_irq_data(data);

	if (VALID_EVTCHN(evtchn))
		pirq_unmask_and_notify(evtchn, data->irq);
}

#define mask_pirq mask_dynirq

static void end_pirq(struct irq_data *data)
{
	bool disabled = irqd_irq_disabled(data);

	if (disabled && (irq_to_desc(data->irq)->istate & IRQS_PENDING))
		shutdown_pirq(data);
	else {
		if (!disabled)
			irq_move_masked_irq(data);
		unmask_pirq(data);
	}
}

static struct irq_chip pirq_chip = {
	.name             = "Phys",
	.irq_startup      = startup_pirq,
	.irq_shutdown     = shutdown_pirq,
	.irq_enable       = enable_pirq,
	.irq_disable      = disable_pirq,
	.irq_mask         = mask_pirq,
	.irq_unmask       = unmask_pirq,
	.irq_eoi          = end_pirq,
	.irq_set_type     = set_type_pirq,
#ifdef CONFIG_SMP
	.irq_set_affinity = set_affinity_irq,
#endif
	.irq_retrigger    = resend_irq_on_evtchn,
};

int irq_ignore_unhandled(unsigned int irq)
{
	struct physdev_irq_status_query irq_status = { .irq = irq };

	if (!is_running_on_xen() || irq >= nr_pirqs)
		return 0;

	if (HYPERVISOR_physdev_op(PHYSDEVOP_irq_status_query, &irq_status))
		return 0;
	return !!(irq_status.flags & XENIRQSTAT_shared);
}

#if defined(CONFIG_SMP) && !defined(PER_CPU_IPI_IRQ)
void notify_remote_via_ipi(unsigned int ipi, unsigned int cpu)
{
	unsigned int evtchn = per_cpu(ipi_evtchn, cpu);

#ifdef NMI_VECTOR
	if (ipi == NMI_VECTOR) {
		int rc = HYPERVISOR_vcpu_op(VCPUOP_send_nmi, cpu, NULL);

		if (rc)
			pr_warn_once("Unable (%d) to send NMI to CPU#%u\n",
				     rc, cpu);
		return;
	}
#endif

	if (VALID_EVTCHN(evtchn)
	    && !test_and_set_bit(ipi, per_cpu(ipi_pending, cpu))
	    && !test_evtchn(evtchn))
		notify_remote_via_evtchn(evtchn);
}

void clear_ipi_evtchn(void)
{
	unsigned int evtchn = this_cpu_read(ipi_evtchn);

	BUG_ON(!VALID_EVTCHN(evtchn));
	clear_evtchn(evtchn);
}
#endif

void notify_remote_via_irq(int irq)
{
	const struct irq_cfg *cfg = irq_cfg(irq);
	unsigned int evtchn;

	if (WARN_ON_ONCE(!cfg))
		return;
	BUG_ON(type_from_irq_cfg(cfg) == IRQT_VIRQ);
	BUG_IF_IPI(cfg);

	evtchn = evtchn_from_irq_cfg(cfg);
	if (VALID_EVTCHN(evtchn))
		notify_remote_via_evtchn(evtchn);
}
EXPORT_SYMBOL_GPL(notify_remote_via_irq);

#if IS_ENABLED(CONFIG_XEN_BACKEND)
int multi_notify_remote_via_irq(multicall_entry_t *mcl, int irq)
{
	const struct irq_cfg *cfg = irq_cfg(irq);
	unsigned int evtchn;

	if (WARN_ON_ONCE(!cfg))
		return -EINVAL;
	BUG_ON(type_from_irq_cfg(cfg) == IRQT_VIRQ);
	BUG_IF_IPI(cfg);

	evtchn = evtchn_from_irq_cfg(cfg);
	if (!VALID_EVTCHN(evtchn))
		return -EINVAL;

	multi_notify_remote_via_evtchn(mcl, evtchn);
	return 0;
}
EXPORT_SYMBOL_GPL(multi_notify_remote_via_irq);
#endif

int irq_to_evtchn_port(int irq)
{
	const struct irq_cfg *cfg = irq_cfg(irq);

	if (!cfg)
		return 0;
	BUG_IF_VIRQ_PER_CPU(cfg);
	BUG_IF_IPI(cfg);
	return evtchn_from_irq_cfg(cfg);
}
EXPORT_SYMBOL_GPL(irq_to_evtchn_port);

void mask_evtchn(int port)
{
	shared_info_t *s = HYPERVISOR_shared_info;
	sync_set_bit(port, s->evtchn_mask);
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

	sync_clear_bit(port, s->evtchn_mask);

	/* Did we miss an interrupt 'edge'? Re-fire if so. */
	if (sync_test_bit(port, s->evtchn_pending)) {
		vcpu_info_t *v = current_vcpu_info();

		if (!sync_test_and_set_bit(port / BITS_PER_LONG,
					   &v->evtchn_pending_sel))
			v->evtchn_upcall_pending = 1;
	}
}
EXPORT_SYMBOL_GPL(unmask_evtchn);

void disable_all_local_evtchn(void)
{
	unsigned i, cpu = smp_processor_id();
	shared_info_t *s = HYPERVISOR_shared_info;

	for (i = 0; i < EVTCHN_2L_NR_CHANNELS; ++i)
		if (cpu_from_evtchn(i) == cpu)
			sync_set_bit(i, &s->evtchn_mask[0]);
}

/* Test an irq's pending state. */
int xen_test_irq_pending(int irq)
{
	unsigned int evtchn = evtchn_from_irq(irq);

	return VALID_EVTCHN(evtchn) && test_evtchn(evtchn);
}

#ifdef CONFIG_PM_SLEEP
#include <linux/syscore_ops.h>

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
		_bind_evtchn_to_cpu(evtchn, cpu, NULL, NULL);

		/* Ready for use. */
		unmask_evtchn(evtchn);
	}
}

static void restore_cpu_ipis(unsigned int cpu)
{
#ifdef CONFIG_SMP
	struct evtchn_bind_ipi bind_ipi;
	struct irq_data *data;
	unsigned int evtchn;
#ifdef PER_CPU_IPI_IRQ
	int ipi, irq;

	for (ipi = 0; ipi < NR_IPIS; ipi++) {
		if ((irq = per_cpu(ipi_to_irq, cpu)[ipi]) == -1)
			continue;
#else
#define ipi 0
#define irq ipi_irq
		if (irq == -1
		    || !VALID_EVTCHN(per_cpu(ipi_evtchn, cpu)))
			return;

		bitmap_zero(per_cpu(ipi_pending, cpu), NR_IPIS);
#endif

		data = irq_get_irq_data(irq);
		BUG_ON(irq_data_cfg(data)->info != mk_irq_info(IRQT_IPI, ipi, 0));

		/* Get a new binding from Xen. */
		bind_ipi.vcpu = cpu;
		if (HYPERVISOR_event_channel_op(EVTCHNOP_bind_ipi,
						&bind_ipi) != 0)
			BUG();
		evtchn = bind_ipi.port;

		/* Record the new mapping. */
		evtchn_to_irq[evtchn] = irq;
#ifdef PER_CPU_IPI_IRQ
		irq_data_cfg(data)->info = mk_irq_info(IRQT_IPI, ipi, evtchn);
#else
		per_cpu(ipi_evtchn, cpu) = evtchn;
#endif
		_bind_evtchn_to_cpu(evtchn, cpu, NULL, NULL);

		/* Ready for use. */
		if (!irqd_irq_disabled(data))
			unmask_evtchn(evtchn);
#ifdef PER_CPU_IPI_IRQ
	}
#else
#undef irq
#undef ipi
#endif
#endif /* CONFIG_SMP */
}

static void evtchn_resume(void)
{
	unsigned int cpu, irq, evtchn;
	struct evtchn_status status;

	/* Avoid doing anything in the 'suspend cancelled' case. */
	status.dom = DOMID_SELF;
#ifdef PER_CPU_VIRQ_IRQ
	status.port = evtchn_from_irq(__this_cpu_read(virq_to_irq[VIRQ_TIMER]));
#else
	status.port = __this_cpu_read(virq_to_evtchn[VIRQ_TIMER]);
#endif
	if (HYPERVISOR_event_channel_op(EVTCHNOP_status, &status))
		BUG();
	if (status.status == EVTCHNSTAT_virq
	    && status.vcpu == smp_processor_id()
	    && status.u.virq == VIRQ_TIMER)
		return;

	init_evtchn_cpu_bindings();

	if (pirq_eoi_does_unmask) {
		struct physdev_pirq_eoi_gmfn eoi_gmfn;

		eoi_gmfn.gmfn = virt_to_machine(pirq_needs_eoi) >> PAGE_SHIFT;
		if (HYPERVISOR_physdev_op(PHYSDEVOP_pirq_eoi_gmfn_v1,
					  &eoi_gmfn))
			BUG();
	}

	/* New event-channel space is not 'live' yet. */
	for (evtchn = 0; evtchn < EVTCHN_2L_NR_CHANNELS; evtchn++)
		mask_evtchn(evtchn);

	/* No IRQ <-> event-channel mappings. */
	for (irq = 0; irq < nr_irqs; irq++) {
		struct irq_cfg *cfg = irq_cfg(irq);

		if (!cfg)
			continue;

		/* Check that no PIRQs are still bound. */
#ifdef CONFIG_SPARSE_IRQ
		if (irq < PIRQ_BASE || irq >= PIRQ_BASE + nr_pirqs)
			BUG_ON(type_from_irq_cfg(cfg) == IRQT_PIRQ);
		else
#endif
			BUG_ON(cfg->info != IRQ_UNBOUND);

		cfg->info &= ~((1U << _EVTCHN_BITS) - 1);
	}
	for (evtchn = 0; evtchn < EVTCHN_2L_NR_CHANNELS; evtchn++)
		evtchn_to_irq[evtchn] = -1;

	for_each_possible_cpu(cpu) {
		restore_cpu_virqs(cpu);
		restore_cpu_ipis(cpu);
	}
}

static struct syscore_ops evtchn_syscore_ops = {
	.resume	= evtchn_resume,
};

static int __init evtchn_register(void)
{
	if (!is_initial_xendomain())
		register_syscore_ops(&evtchn_syscore_ops);
	return 0;
}
core_initcall(evtchn_register);
#endif

int __init arch_early_irq_init(void)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(_irq_cfg); i++)
		irq_set_chip_data(i, _irq_cfg + i);

	return 0;
}

struct irq_cfg *alloc_irq_and_cfg_at(unsigned int at, int node)
{
	int res = irq_alloc_desc_at(at, node);
	struct irq_cfg *cfg = NULL;

	if (res < 0) {
		if (res != -EEXIST)
			return NULL;
		cfg = irq_get_chip_data(at);
		if (cfg)
			return cfg;
	}

#ifdef CONFIG_SPARSE_IRQ
#ifdef CONFIG_SMP
	/* By default all event channels notify CPU#0. */
	cpumask_copy(irq_get_irq_data(at)->affinity, cpumask_of(0));
#endif

	cfg = kzalloc(sizeof(*cfg), GFP_KERNEL);
	if (cfg)
		irq_set_chip_data(at, cfg);
	else
		irq_free_desc(at);

	return cfg;
#else
	return irq_cfg(at);
#endif
}

#ifdef CONFIG_SPARSE_IRQ
#ifdef CONFIG_X86_IO_APIC
#include <asm/io_apic.h>
#endif

int nr_pirqs = NR_PIRQS;
EXPORT_SYMBOL_GPL(nr_pirqs);

int __init arch_probe_nr_irqs(void)
{
	int nr = 64 + CONFIG_XEN_NR_GUEST_DEVICES, nr_irqs_gsi;

	if (is_initial_xendomain()) {
		nr_irqs_gsi = NR_IRQS_LEGACY;
#ifdef CONFIG_X86_IO_APIC
		nr_irqs_gsi += gsi_top;
#endif
#ifdef CONFIG_PCI_MSI
		nr += max(nr_irqs_gsi * 16, nr_cpu_ids * 8);
#endif
	} else {
		nr_irqs_gsi = NR_VECTORS;
#ifdef CONFIG_PCI_MSI
		nr += max(NR_IRQS_LEGACY * 16, nr_cpu_ids * 8);
#endif
	}

	if (nr_pirqs > nr_irqs_gsi)
		nr_pirqs = nr_irqs_gsi;
	if (nr > min_t(int, NR_DYNIRQS, EVTCHN_2L_NR_CHANNELS))
		nr = min_t(int, NR_DYNIRQS, EVTCHN_2L_NR_CHANNELS);
	nr_irqs = min_t(int, nr_pirqs + nr, PAGE_SIZE * 8);

	printk(KERN_DEBUG "nr_pirqs: %d\n", nr_pirqs);

	return ARRAY_SIZE(_irq_cfg);
}
#endif

#if defined(CONFIG_X86_IO_APIC)
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
	struct irq_cfg *cfg = irq_cfg(irq);

	BUG_ON(irq < PIRQ_BASE || irq - PIRQ_BASE >= nr_pirqs);
	if (identity_mapped_irq(irq) || type_from_irq_cfg(cfg) != IRQT_UNBOUND)
		return;
	cfg->info = mk_irq_info(IRQT_PIRQ, irq, 0);
	irq_set_chip_and_handler_name(irq, &pirq_chip, handle_fasteoi_irq,
				      "fasteoi");
}

#ifdef CONFIG_PCI_MSI
int evtchn_map_pirq(int irq, unsigned int xen_pirq, unsigned int nr)
{
	if (irq < 0) {
#ifdef CONFIG_SPARSE_IRQ
		struct irq_cfg *cfg = NULL;

		if (nr <= 0)
			return -EINVAL;
		spin_lock(&irq_mapping_update_lock);
		irq = find_unbound_irq(numa_node_id(), nr == 1 ? &cfg : NULL,
				       &pirq_chip, nr);
		if (irq >= 0) {
			unsigned int i;

			for (i = 0; i < nr; ++i) {
				if (!cfg || i)
					cfg = irq_cfg(irq + i);
				BUG_ON(type_from_irq_cfg(cfg) != IRQT_UNBOUND);
				cfg->bindcount++;
				cfg->info = mk_irq_info(IRQT_PIRQ,
							xen_pirq + i, 0);
			}
		}
		spin_unlock(&irq_mapping_update_lock);
		if (irq < 0)
			return irq;
	} else if (irq >= PIRQ_BASE && irq < PIRQ_BASE + nr_pirqs) {
		WARN_ONCE(1, "Non-MSI IRQ#%d (Xen %d)\n", irq, xen_pirq);
		return -EINVAL;
#else
		static DEFINE_SPINLOCK(irq_alloc_lock);

		if (nr > 1)
			return -EOPNOTSUPP;
		irq = PIRQ_BASE + nr_pirqs - 1;
		spin_lock(&irq_alloc_lock);
		do {
			struct irq_cfg *cfg;

			if (identity_mapped_irq(irq))
				continue;
			cfg = alloc_irq_and_cfg_at(irq, numa_node_id());
			if (unlikely(!cfg)) {
				spin_unlock(&irq_alloc_lock);
				return -ENOMEM;
			}
			if (!index_from_irq_cfg(cfg)) {
				BUG_ON(type_from_irq_cfg(cfg) != IRQT_UNBOUND);
				cfg->info = mk_irq_info(IRQT_PIRQ,
							xen_pirq, 0);
				break;
			}
		} while (--irq >= PIRQ_BASE);
		spin_unlock(&irq_alloc_lock);
		if (irq < PIRQ_BASE)
			return -ENOSPC;
		irq_set_chip_and_handler_name(irq, &pirq_chip,
					      handle_fasteoi_irq, "fasteoi");
#endif
	} else if (!xen_pirq) {
		while (nr--) {
			struct irq_cfg *cfg = irq_cfg(irq + nr);

			if (!cfg
			    || unlikely(type_from_irq_cfg(cfg) != IRQT_PIRQ))
				return -EINVAL;
			/*
			 * dynamic_irq_cleanup(irq) would seem to be the
			 * correct thing here, but cannot be used as we get
			 * here also during shutdown when a driver didn't
			 * free_irq() its MSI(-X) IRQ(s), which then causes
			 * a warning in dynamic_irq_cleanup().
			 */
			irq_set_chip_and_handler(irq, NULL, NULL);
			cfg->info = IRQ_UNBOUND;
#ifdef CONFIG_SPARSE_IRQ
			cfg->bindcount--;
#endif
		}
		return 0;
	} else
		while (nr--) {
			if (type_from_irq(irq + nr) == IRQT_PIRQ
			    && index_from_irq(irq + nr) == xen_pirq + nr)
				continue;
			pr_err("IRQ#%u is already mapped to %d:%u - "
			       "cannot map to PIRQ#%u\n",
			       irq + nr, type_from_irq(irq + nr),
			       index_from_irq(irq + nr), xen_pirq + nr);
			return -EINVAL;
		}
	return index_from_irq(irq) ? irq : -EINVAL;
}
#endif

int evtchn_get_xen_pirq(int irq)
{
	struct irq_cfg *cfg = irq_cfg(irq);

	if (identity_mapped_irq(irq))
		return irq;
	BUG_ON(type_from_irq_cfg(cfg) != IRQT_PIRQ);
	return index_from_irq_cfg(cfg);
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

#ifdef CONFIG_SPARSE_IRQ
	i = nr_irqs;
#else
	i = nr_pirqs;
#endif
	i = get_order(sizeof(unsigned long) * BITS_TO_LONGS(i));
	pirq_needs_eoi = (void *)__get_free_pages(GFP_KERNEL|__GFP_ZERO, i);
	BUILD_BUG_ON(NR_PIRQS > PAGE_SIZE * 8);
 	eoi_gmfn.gmfn = virt_to_machine(pirq_needs_eoi) >> PAGE_SHIFT;
	if (HYPERVISOR_physdev_op(PHYSDEVOP_pirq_eoi_gmfn_v1, &eoi_gmfn) == 0)
		pirq_eoi_does_unmask = true;

	/* No event channels are 'live' right now. */
	for (i = 0; i < EVTCHN_2L_NR_CHANNELS; i++)
		mask_evtchn(i);

#ifndef CONFIG_SPARSE_IRQ
	for (i = DYNIRQ_BASE; i < (DYNIRQ_BASE + NR_DYNIRQS); i++) {
		irq_set_noprobe(i);
		irq_set_chip_and_handler_name(i, &dynirq_chip,
					      handle_fasteoi_irq, "fasteoi");
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

		irq_set_chip_and_handler_name(i, &pirq_chip,
					      handle_fasteoi_irq, "fasteoi");
	}
}
