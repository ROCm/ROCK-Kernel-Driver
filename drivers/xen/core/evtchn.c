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

/* Packed IRQ information: binding type, sub-type index, and event channel. */
static u32 irq_info[NR_IRQS];

/* Binding types. */
enum {
	IRQT_UNBOUND,
	IRQT_PIRQ,
	IRQT_VIRQ,
	IRQT_IPI,
	IRQT_LOCAL_PORT,
	IRQT_CALLER_PORT
};

/* Constructor for packed IRQ information. */
static inline u32 mk_irq_info(u32 type, u32 index, u32 evtchn)
{
	return ((type << 24) | (index << 16) | evtchn);
}

/* Convenient shorthand for packed representation of an unbound IRQ. */
#define IRQ_UNBOUND	mk_irq_info(IRQT_UNBOUND, 0, 0)

/*
 * Accessors for packed IRQ information.
 */

static inline unsigned int evtchn_from_irq(int irq)
{
	return (u16)(irq_info[irq]);
}

static inline unsigned int index_from_irq(int irq)
{
	return (u8)(irq_info[irq] >> 16);
}

static inline unsigned int type_from_irq(int irq)
{
	return (u8)(irq_info[irq] >> 24);
}

/* IRQ <-> VIRQ mapping. */
DEFINE_PER_CPU(int, virq_to_irq[NR_VIRQS]) = {[0 ... NR_VIRQS-1] = -1};

/* IRQ <-> IPI mapping. */
#ifndef NR_IPIS
#define NR_IPIS 1
#endif
DEFINE_PER_CPU(int, ipi_to_irq[NR_IPIS]) = {[0 ... NR_IPIS-1] = -1};

/* Reference counts for bindings to IRQs. */
static int irq_bindcount[NR_IRQS];

/* Bitmap indicating which PIRQs require Xen to be notified on unmask. */
static DECLARE_BITMAP(pirq_needs_eoi, NR_PIRQS);

#ifdef CONFIG_SMP

static u8 cpu_evtchn[NR_EVENT_CHANNELS];
static unsigned long cpu_evtchn_mask[NR_CPUS][NR_EVENT_CHANNELS/BITS_PER_LONG];

static inline unsigned long active_evtchns(unsigned int cpu, shared_info_t *sh,
					   unsigned int idx)
{
	return (sh->evtchn_pending[idx] &
		cpu_evtchn_mask[cpu][idx] &
		~sh->evtchn_mask[idx]);
}

static void bind_evtchn_to_cpu(unsigned int chn, unsigned int cpu)
{
	shared_info_t *s = HYPERVISOR_shared_info;
	int irq = evtchn_to_irq[chn];

	BUG_ON(!test_bit(chn, s->evtchn_mask));

	if (irq != -1)
		irq_desc[irq].affinity = cpumask_of_cpu(cpu);

	clear_bit(chn, (unsigned long *)cpu_evtchn_mask[cpu_evtchn[chn]]);
	set_bit(chn, (unsigned long *)cpu_evtchn_mask[cpu]);
	cpu_evtchn[chn] = cpu;
}

static void init_evtchn_cpu_bindings(void)
{
	int i;

	/* By default all event channels notify CPU#0. */
	for (i = 0; i < NR_IRQS; i++)
		irq_desc[i].affinity = cpumask_of_cpu(0);

	memset(cpu_evtchn, 0, sizeof(cpu_evtchn));
	memset(cpu_evtchn_mask[0], ~0, sizeof(cpu_evtchn_mask[0]));
}

static inline unsigned int cpu_from_evtchn(unsigned int evtchn)
{
	return cpu_evtchn[evtchn];
}

#else

static inline unsigned long active_evtchns(unsigned int cpu, shared_info_t *sh,
					   unsigned int idx)
{
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

/* Upcall to generic IRQ layer. */
#ifdef CONFIG_X86
extern unsigned int do_IRQ(struct pt_regs *regs);
void __init xen_init_IRQ(void);
void __init init_IRQ(void)
{
	irq_ctx_init(0);
	xen_init_IRQ();
}
#if defined (__i386__)
static inline void exit_idle(void) {}
#elif defined (__x86_64__)
#include <asm/idle.h>
#endif
#define do_IRQ(irq, regs) do {		\
	(regs)->orig_ax = ~(irq);	\
	do_IRQ((regs));			\
} while (0)
#elif defined (__powerpc__)
#define do_IRQ(irq, regs)	__do_IRQ(irq, regs)
static inline void exit_idle(void) {}
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

static DEFINE_PER_CPU(unsigned int, upcall_count) = { 0 };
static DEFINE_PER_CPU(unsigned int, last_processed_l1i) = { BITS_PER_LONG - 1 };
static DEFINE_PER_CPU(unsigned int, last_processed_l2i) = { BITS_PER_LONG - 1 };

/* NB. Interrupts are disabled on entry. */
asmlinkage void evtchn_do_upcall(struct pt_regs *regs)
{
	unsigned long       l1, l2;
	unsigned long       masked_l1, masked_l2;
	unsigned int        l1i, l2i, port, count;
	int                 irq;
	unsigned int        cpu = smp_processor_id();
	shared_info_t      *s = HYPERVISOR_shared_info;
	vcpu_info_t        *vcpu_info = &s->vcpu_info[cpu];


	do {
		/* Avoid a callback storm when we reenable delivery. */
		vcpu_info->evtchn_upcall_pending = 0;

		/* Nested invocations bail immediately. */
		if (unlikely(per_cpu(upcall_count, cpu)++))
			return;

#ifndef CONFIG_X86 /* No need for a barrier -- XCHG is a barrier on x86. */
		/* Clear master flag /before/ clearing selector flag. */
		rmb();
#endif
		l1 = xchg(&vcpu_info->evtchn_pending_sel, 0);

		l1i = per_cpu(last_processed_l1i, cpu);
		l2i = per_cpu(last_processed_l2i, cpu);

		while (l1 != 0) {

			l1i = (l1i + 1) % BITS_PER_LONG;
			masked_l1 = l1 & ((~0UL) << l1i);

			if (masked_l1 == 0) { /* if we masked out all events, wrap around to the beginning */
				l1i = BITS_PER_LONG - 1;
				l2i = BITS_PER_LONG - 1;
				continue;
			}
			l1i = __ffs(masked_l1);

			do {
				l2 = active_evtchns(cpu, s, l1i);

				l2i = (l2i + 1) % BITS_PER_LONG;
				masked_l2 = l2 & ((~0UL) << l2i);

				if (masked_l2 == 0) { /* if we masked out all events, move on */
					l2i = BITS_PER_LONG - 1;
					break;
				}

				l2i = __ffs(masked_l2);

				/* process port */
				port = (l1i * BITS_PER_LONG) + l2i;
				if ((irq = evtchn_to_irq[port]) != -1)
					do_IRQ(irq, regs);
				else {
					exit_idle();
					evtchn_device_upcall(port);
				}

				/* if this is the final port processed, we'll pick up here+1 next time */
				per_cpu(last_processed_l1i, cpu) = l1i;
				per_cpu(last_processed_l2i, cpu) = l2i;

			} while (l2i != BITS_PER_LONG - 1);

			l2 = active_evtchns(cpu, s, l1i);
			if (l2 == 0) /* we handled all ports, so we can clear the selector bit */
				l1 &= ~(1UL << l1i);

		}

		/* If there were nested callbacks then we have more to do. */
		count = per_cpu(upcall_count, cpu);
		per_cpu(upcall_count, cpu) = 0;
	} while (unlikely(count != 1));
}

static int find_unbound_irq(void)
{
	static int warned;
	int dynirq, irq;

	for (dynirq = 0; dynirq < NR_DYNIRQS; dynirq++) {
		irq = dynirq_to_irq(dynirq);
		if (irq_bindcount[irq] == 0)
			return irq;
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
		if ((irq = find_unbound_irq()) < 0)
			goto out;

		evtchn_to_irq[caller_port] = irq;
		irq_info[irq] = mk_irq_info(IRQT_CALLER_PORT, 0, caller_port);
	}

	irq_bindcount[irq]++;

 out:
	spin_unlock(&irq_mapping_update_lock);
	return irq;
}

static int bind_local_port_to_irq(unsigned int local_port)
{
	int irq;

	spin_lock(&irq_mapping_update_lock);

	BUG_ON(evtchn_to_irq[local_port] != -1);

	if ((irq = find_unbound_irq()) < 0) {
		struct evtchn_close close = { .port = local_port };
		if (HYPERVISOR_event_channel_op(EVTCHNOP_close, &close))
			BUG();
		goto out;
	}

	evtchn_to_irq[local_port] = irq;
	irq_info[irq] = mk_irq_info(IRQT_LOCAL_PORT, 0, local_port);
	irq_bindcount[irq]++;

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
		if ((irq = find_unbound_irq()) < 0)
			goto out;

		bind_virq.virq = virq;
		bind_virq.vcpu = cpu;
		if (HYPERVISOR_event_channel_op(EVTCHNOP_bind_virq,
						&bind_virq) != 0)
			BUG();
		evtchn = bind_virq.port;

		evtchn_to_irq[evtchn] = irq;
		irq_info[irq] = mk_irq_info(IRQT_VIRQ, virq, evtchn);

		per_cpu(virq_to_irq, cpu)[virq] = irq;

		bind_evtchn_to_cpu(evtchn, cpu);
	}

	irq_bindcount[irq]++;

 out:
	spin_unlock(&irq_mapping_update_lock);
	return irq;
}

static int bind_ipi_to_irq(unsigned int ipi, unsigned int cpu)
{
	struct evtchn_bind_ipi bind_ipi;
	int evtchn, irq;

	spin_lock(&irq_mapping_update_lock);

	if ((irq = per_cpu(ipi_to_irq, cpu)[ipi]) == -1) {
		if ((irq = find_unbound_irq()) < 0)
			goto out;

		bind_ipi.vcpu = cpu;
		if (HYPERVISOR_event_channel_op(EVTCHNOP_bind_ipi,
						&bind_ipi) != 0)
			BUG();
		evtchn = bind_ipi.port;

		evtchn_to_irq[evtchn] = irq;
		irq_info[irq] = mk_irq_info(IRQT_IPI, ipi, evtchn);

		per_cpu(ipi_to_irq, cpu)[ipi] = irq;

		bind_evtchn_to_cpu(evtchn, cpu);
	}

	irq_bindcount[irq]++;

 out:
	spin_unlock(&irq_mapping_update_lock);
	return irq;
}

static void unbind_from_irq(unsigned int irq)
{
	struct evtchn_close close;
	unsigned int cpu;
	int evtchn = evtchn_from_irq(irq);

	spin_lock(&irq_mapping_update_lock);

	if ((--irq_bindcount[irq] == 0) && VALID_EVTCHN(evtchn)) {
		close.port = evtchn;
		if ((type_from_irq(irq) != IRQT_CALLER_PORT) &&
		    HYPERVISOR_event_channel_op(EVTCHNOP_close, &close))
			BUG();

		switch (type_from_irq(irq)) {
		case IRQT_VIRQ:
			per_cpu(virq_to_irq, cpu_from_evtchn(evtchn))
				[index_from_irq(irq)] = -1;
			break;
		case IRQT_IPI:
			per_cpu(ipi_to_irq, cpu_from_evtchn(evtchn))
				[index_from_irq(irq)] = -1;
			break;
		default:
			break;
		}

		/* Closed ports are implicitly re-bound to VCPU0. */
		bind_evtchn_to_cpu(evtchn, 0);

		evtchn_to_irq[evtchn] = -1;
		irq_info[irq] = IRQ_UNBOUND;

		/* Zap stats across IRQ changes of use. */
		for_each_possible_cpu(cpu)
			kstat_cpu(cpu).irqs[irq] = 0;
	}

	spin_unlock(&irq_mapping_update_lock);
}

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

	retval = request_irq(irq, handler, irqflags, devname, dev_id);
	if (retval != 0) {
		unbind_from_irq(irq);
		return retval;
	}

	return irq;
}
EXPORT_SYMBOL_GPL(bind_ipi_to_irqhandler);

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

	if (VALID_EVTCHN(evtchn))
		rebind_evtchn_to_cpu(evtchn, tcpu);
}

static void set_affinity_irq(unsigned int irq, cpumask_t dest)
{
	unsigned tcpu = first_cpu(dest);
	rebind_irq_to_cpu(irq, tcpu);
}
#endif

int resend_irq_on_evtchn(unsigned int irq)
{
	int masked, evtchn = evtchn_from_irq(irq);
	shared_info_t *s = HYPERVISOR_shared_info;

	if (!VALID_EVTCHN(evtchn))
		return 1;

	masked = test_and_set_evtchn_mask(evtchn);
	synch_set_bit(evtchn, s->evtchn_pending);
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

	if (VALID_EVTCHN(evtchn) && !(irq_desc[irq].status & IRQ_DISABLED))
		unmask_evtchn(evtchn);
}

static struct irq_chip dynirq_chip = {
	.name     = "Dynamic-irq",
	.startup  = startup_dynirq,
	.mask     = mask_dynirq,
	.unmask   = unmask_dynirq,
	.mask_ack = ack_dynirq,
	.ack      = ack_dynirq,
	.end      = end_dynirq,
#ifdef CONFIG_SMP
	.set_affinity = set_affinity_irq,
#endif
	.retrigger = resend_irq_on_evtchn,
};

static inline void pirq_unmask_notify(int pirq)
{
	struct physdev_eoi eoi = { .irq = pirq };
	if (unlikely(test_bit(pirq, pirq_needs_eoi)))
		VOID(HYPERVISOR_physdev_op(PHYSDEVOP_eoi, &eoi));
}

static inline void pirq_query_unmask(int pirq)
{
	struct physdev_irq_status_query irq_status;
	irq_status.irq = pirq;
	if (HYPERVISOR_physdev_op(PHYSDEVOP_irq_status_query, &irq_status))
		irq_status.flags = 0;
	clear_bit(pirq, pirq_needs_eoi);
	if (irq_status.flags & XENIRQSTAT_needs_eoi)
		set_bit(pirq, pirq_needs_eoi);
}

/*
 * On startup, if there is no action associated with the IRQ then we are
 * probing. In this case we should not share with others as it will confuse us.
 */
#define probing_irq(_irq) (irq_desc[(_irq)].action == NULL)

static unsigned int startup_pirq(unsigned int irq)
{
	struct evtchn_bind_pirq bind_pirq;
	int evtchn = evtchn_from_irq(irq);

	if (VALID_EVTCHN(evtchn))
		goto out;

	bind_pirq.pirq  = irq;
	/* NB. We are happy to share unless we are probing. */
	bind_pirq.flags = probing_irq(irq) ? 0 : BIND_PIRQ__WILL_SHARE;
	if (HYPERVISOR_event_channel_op(EVTCHNOP_bind_pirq, &bind_pirq) != 0) {
		if (!probing_irq(irq))
			printk(KERN_INFO "Failed to obtain physical IRQ %d\n",
			       irq);
		return 0;
	}
	evtchn = bind_pirq.port;

	pirq_query_unmask(irq_to_pirq(irq));

	evtchn_to_irq[evtchn] = irq;
	bind_evtchn_to_cpu(evtchn, 0);
	irq_info[irq] = mk_irq_info(IRQT_PIRQ, irq, evtchn);

 out:
	unmask_evtchn(evtchn);
	pirq_unmask_notify(irq_to_pirq(irq));

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
	irq_info[irq] = IRQ_UNBOUND;
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

	if ((irq_desc[irq].status & (IRQ_DISABLED|IRQ_PENDING)) ==
	    (IRQ_DISABLED|IRQ_PENDING)) {
		shutdown_pirq(irq);
	} else if (VALID_EVTCHN(evtchn)) {
		unmask_evtchn(evtchn);
		pirq_unmask_notify(irq_to_pirq(irq));
	}
}

static struct irq_chip pirq_chip = {
	.name     = "Phys-irq",
	.typename = "Phys-irq",
	.startup  = startup_pirq,
	.shutdown = shutdown_pirq,
	.mask     = mask_pirq,
	.unmask   = unmask_pirq,
	.mask_ack = ack_pirq,
	.ack      = ack_pirq,
	.end      = end_pirq,
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

void notify_remote_via_irq(int irq)
{
	int evtchn = evtchn_from_irq(irq);

	if (VALID_EVTCHN(evtchn))
		notify_remote_via_evtchn(evtchn);
}
EXPORT_SYMBOL_GPL(notify_remote_via_irq);

int irq_to_evtchn_port(int irq)
{
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
	vcpu_info_t *vcpu_info = &s->vcpu_info[cpu];

	BUG_ON(!irqs_disabled());

	/* Slow path (hypercall) if this is a non-local port. */
	if (unlikely(cpu != cpu_from_evtchn(port))) {
		struct evtchn_unmask unmask = { .port = port };
		VOID(HYPERVISOR_event_channel_op(EVTCHNOP_unmask, &unmask));
		return;
	}

	synch_clear_bit(port, s->evtchn_mask);

	/* Did we miss an interrupt 'edge'? Re-fire if so. */
	if (synch_test_bit(port, s->evtchn_pending) &&
	    !synch_test_and_set_bit(port / BITS_PER_LONG,
				    &vcpu_info->evtchn_pending_sel))
		vcpu_info->evtchn_upcall_pending = 1;
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

static void restore_cpu_virqs(unsigned int cpu)
{
	struct evtchn_bind_virq bind_virq;
	int virq, irq, evtchn;

	for (virq = 0; virq < NR_VIRQS; virq++) {
		if ((irq = per_cpu(virq_to_irq, cpu)[virq]) == -1)
			continue;

		BUG_ON(irq_info[irq] != mk_irq_info(IRQT_VIRQ, virq, 0));

		/* Get a new binding from Xen. */
		bind_virq.virq = virq;
		bind_virq.vcpu = cpu;
		if (HYPERVISOR_event_channel_op(EVTCHNOP_bind_virq,
						&bind_virq) != 0)
			BUG();
		evtchn = bind_virq.port;

		/* Record the new mapping. */
		evtchn_to_irq[evtchn] = irq;
		irq_info[irq] = mk_irq_info(IRQT_VIRQ, virq, evtchn);
		bind_evtchn_to_cpu(evtchn, cpu);

		/* Ready for use. */
		unmask_evtchn(evtchn);
	}
}

static void restore_cpu_ipis(unsigned int cpu)
{
	struct evtchn_bind_ipi bind_ipi;
	int ipi, irq, evtchn;

	for (ipi = 0; ipi < NR_IPIS; ipi++) {
		if ((irq = per_cpu(ipi_to_irq, cpu)[ipi]) == -1)
			continue;

		BUG_ON(irq_info[irq] != mk_irq_info(IRQT_IPI, ipi, 0));

		/* Get a new binding from Xen. */
		bind_ipi.vcpu = cpu;
		if (HYPERVISOR_event_channel_op(EVTCHNOP_bind_ipi,
						&bind_ipi) != 0)
			BUG();
		evtchn = bind_ipi.port;

		/* Record the new mapping. */
		evtchn_to_irq[evtchn] = irq;
		irq_info[irq] = mk_irq_info(IRQT_IPI, ipi, evtchn);
		bind_evtchn_to_cpu(evtchn, cpu);

		/* Ready for use. */
		unmask_evtchn(evtchn);

	}
}

void irq_resume(void)
{
	unsigned int cpu, pirq, irq, evtchn;

	init_evtchn_cpu_bindings();

	/* New event-channel space is not 'live' yet. */
	for (evtchn = 0; evtchn < NR_EVENT_CHANNELS; evtchn++)
		mask_evtchn(evtchn);

	/* Check that no PIRQs are still bound. */
	for (pirq = 0; pirq < NR_PIRQS; pirq++)
		BUG_ON(irq_info[pirq_to_irq(pirq)] != IRQ_UNBOUND);

	/* No IRQ <-> event-channel mappings. */
	for (irq = 0; irq < NR_IRQS; irq++)
		irq_info[irq] &= ~0xFFFF; /* zap event-channel binding */
	for (evtchn = 0; evtchn < NR_EVENT_CHANNELS; evtchn++)
		evtchn_to_irq[evtchn] = -1;

	for_each_possible_cpu(cpu) {
		restore_cpu_virqs(cpu);
		restore_cpu_ipis(cpu);
	}

}

void __init xen_init_IRQ(void)
{
	unsigned int i;

	init_evtchn_cpu_bindings();

	/* No event channels are 'live' right now. */
	for (i = 0; i < NR_EVENT_CHANNELS; i++)
		mask_evtchn(i);

	/* No IRQ -> event-channel mappings. */
	for (i = 0; i < NR_IRQS; i++)
		irq_info[i] = IRQ_UNBOUND;

	/* Dynamic IRQ space is currently unbound. Zero the refcnts. */
	for (i = 0; i < NR_DYNIRQS; i++) {
		irq_bindcount[dynirq_to_irq(i)] = 0;

		irq_desc[dynirq_to_irq(i)].status = IRQ_DISABLED;
		irq_desc[dynirq_to_irq(i)].action = NULL;
		irq_desc[dynirq_to_irq(i)].depth = 1;
		set_irq_chip_and_handler_name(dynirq_to_irq(i), &dynirq_chip,
		                              handle_level_irq, "level");
	}

	/* Phys IRQ space is statically bound (1:1 mapping). Nail refcnts. */
	for (i = 0; i < NR_PIRQS; i++) {
		irq_bindcount[pirq_to_irq(i)] = 1;

#ifdef RTC_IRQ
		/* If not domain 0, force our RTC driver to fail its probe. */
		if ((i == RTC_IRQ) && !is_initial_xendomain())
			continue;
#endif

		irq_desc[pirq_to_irq(i)].status = IRQ_DISABLED;
		irq_desc[pirq_to_irq(i)].action = NULL;
		irq_desc[pirq_to_irq(i)].depth = 1;
		set_irq_chip_and_handler_name(pirq_to_irq(i), &pirq_chip,
		                              handle_level_irq, "level");
	}
}
