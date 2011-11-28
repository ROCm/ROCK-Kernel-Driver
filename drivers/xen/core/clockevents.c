/*
 *	Xen clockevent functions
 *
 *	See arch/x86/xen/time.c for copyright and credits for derived
 *	portions of this file.
 *
 * Xen clockevent implementation
 *
 * Xen has two clockevent implementations:
 *
 * The old timer_op one works with all released versions of Xen prior
 * to version 3.0.4.  This version of the hypervisor provides a
 * single-shot timer with nanosecond resolution.  However, sharing the
 * same event channel is a 100Hz tick which is delivered while the
 * vcpu is running.  We don't care about or use this tick, but it will
 * cause the core time code to think the timer fired too soon, and
 * will end up resetting it each time.  It could be filtered, but
 * doing so has complications when the ktime clocksource is not yet
 * the xen clocksource (ie, at boot time).
 *
 * The new vcpu_op-based timer interface allows the tick timer period
 * to be changed or turned off.  The tick timer is not useful as a
 * periodic timer because events are only delivered to running vcpus.
 * The one-shot timer can report when a timeout is in the past, so
 * set_next_event is capable of returning -ETIME when appropriate.
 * This interface is used when available.
 */
#include <linux/clockchips.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/kernel_stat.h>
#include <linux/math64.h>
#include <asm/hypervisor.h>
#include <xen/clock.h>
#include <xen/evtchn.h>
#include <xen/interface/vcpu.h>

#define XEN_SHIFT 22

/* Xen may fire a timer up to this many ns early */
#define TIMER_SLOP	100000
#define NS_PER_TICK	(1000000000LL / HZ)

/*
 * Get a hypervisor absolute time.  In theory we could maintain an
 * offset between the kernel's time and the hypervisor's time, and
 * apply that to a kernel's absolute timeout.  Unfortunately the
 * hypervisor and kernel times can drift even if the kernel is using
 * the Xen clocksource, because ntp can warp the kernel's clocksource.
 */
static u64 get_abs_timeout(unsigned long delta)
{
	return xen_local_clock() + delta;
}

#if CONFIG_XEN_COMPAT <= 0x030004
static void timerop_set_mode(enum clock_event_mode mode,
			     struct clock_event_device *evt)
{
	switch (mode) {
	case CLOCK_EVT_MODE_PERIODIC:
		WARN_ON(1); /* unsupported */
		break;

	case CLOCK_EVT_MODE_ONESHOT:
	case CLOCK_EVT_MODE_RESUME:
		break;

	case CLOCK_EVT_MODE_UNUSED:
	case CLOCK_EVT_MODE_SHUTDOWN:
		if (HYPERVISOR_set_timer_op(0)) /* cancel timeout */
			BUG();
		break;
	}
}

static int timerop_set_next_event(unsigned long delta,
				  struct clock_event_device *evt)
{
	WARN_ON(evt->mode != CLOCK_EVT_MODE_ONESHOT);

	if (HYPERVISOR_set_timer_op(get_abs_timeout(delta)) < 0)
		BUG();

	/*
	 * We may have missed the deadline, but there's no real way of
	 * knowing for sure.  If the event was in the past, then we'll
	 * get an immediate interrupt.
	 */

	return 0;
}
#endif

static void vcpuop_set_mode(enum clock_event_mode mode,
			    struct clock_event_device *evt)
{
	switch (mode) {
	case CLOCK_EVT_MODE_PERIODIC:
		WARN_ON(1); /* unsupported */
		break;

	case CLOCK_EVT_MODE_UNUSED:
	case CLOCK_EVT_MODE_SHUTDOWN:
		if (HYPERVISOR_vcpu_op(VCPUOP_stop_singleshot_timer,
				       smp_processor_id(), NULL))
			BUG();
		/* fall through */
	case CLOCK_EVT_MODE_ONESHOT:
		if (HYPERVISOR_vcpu_op(VCPUOP_stop_periodic_timer,
				       smp_processor_id(), NULL))
			BUG();
		break;

	case CLOCK_EVT_MODE_RESUME:
		break;
	}
}

static int vcpuop_set_next_event(unsigned long delta,
				 struct clock_event_device *evt)
{
	struct vcpu_set_singleshot_timer single;
	int ret;

	WARN_ON(evt->mode != CLOCK_EVT_MODE_ONESHOT);

	single.timeout_abs_ns = get_abs_timeout(delta);
	single.flags = VCPU_SSHOTTMR_future;

	ret = HYPERVISOR_vcpu_op(VCPUOP_set_singleshot_timer,
				 smp_processor_id(), &single);

	BUG_ON(ret != 0 && ret != -ETIME);

	return ret;
}

static DEFINE_PER_CPU(struct clock_event_device, xen_clock_event) = {
	.name		= "xen",
	.features	= CLOCK_EVT_FEAT_ONESHOT,

	.max_delta_ns	= 0xffffffff,
	.min_delta_ns	= TIMER_SLOP,

	.mult		= 1,
	.shift		= 0,
	.rating		= 500,

	.irq		= -1,
};

/* snapshots of runstate info */
static DEFINE_PER_CPU(struct vcpu_runstate_info, xen_runstate_snapshot);

/* unused ns of stolen and blocked time */
static DEFINE_PER_CPU(unsigned int, xen_residual_stolen);
static DEFINE_PER_CPU(unsigned int, xen_residual_blocked);

static void init_missing_ticks_accounting(unsigned int cpu)
{
	per_cpu(xen_runstate_snapshot, cpu) = *setup_runstate_area(cpu);
	if (cpu == smp_processor_id())
		get_runstate_snapshot(&__get_cpu_var(xen_runstate_snapshot));
	per_cpu(xen_residual_stolen, cpu) = 0;
	per_cpu(xen_residual_blocked, cpu) = 0;
}

static irqreturn_t timer_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *evt = &__get_cpu_var(xen_clock_event);
	struct vcpu_runstate_info state, *snap;
	s64 blocked, stolen;
	irqreturn_t ret = IRQ_NONE;

	if (evt->event_handler) {
		evt->event_handler(evt);
		ret = IRQ_HANDLED;
	}

	xen_check_wallclock_update();

	get_runstate_snapshot(&state);
	snap = &__get_cpu_var(xen_runstate_snapshot);

	stolen = state.time[RUNSTATE_runnable] - snap->time[RUNSTATE_runnable]
		+ state.time[RUNSTATE_offline] - snap->time[RUNSTATE_offline]
		+ percpu_read(xen_residual_stolen);

	if (stolen >= NS_PER_TICK)
		account_steal_ticks(div_u64_rem(stolen, NS_PER_TICK,
				    &__get_cpu_var(xen_residual_stolen)));
	else
		percpu_write(xen_residual_stolen, stolen > 0 ? stolen : 0);

	blocked = state.time[RUNSTATE_blocked] - snap->time[RUNSTATE_blocked]
		+ percpu_read(xen_residual_blocked);

	if (blocked >= NS_PER_TICK)
		account_idle_ticks(div_u64_rem(blocked, NS_PER_TICK,
				   &__get_cpu_var(xen_residual_blocked)));
	else
		percpu_write(xen_residual_blocked, blocked > 0 ? blocked : 0);

	*snap = state;

	return ret;
}

static struct irqaction timer_action = {
	.handler = timer_interrupt,
	.flags   = IRQF_DISABLED|IRQF_TIMER,
	.name    = "timer"
};

void __cpuinit xen_setup_cpu_clockevents(void)
{
	unsigned int cpu = smp_processor_id();
	struct clock_event_device *evt = &per_cpu(xen_clock_event, cpu);

	init_missing_ticks_accounting(cpu);

	evt->cpumask = cpumask_of(cpu);
	clockevents_register_device(evt);
}

#ifdef CONFIG_SMP
int __cpuinit local_setup_timer(unsigned int cpu)
{
	struct clock_event_device *evt = &per_cpu(xen_clock_event, cpu);

	BUG_ON(cpu == smp_processor_id());

	evt->irq = bind_virq_to_irqaction(VIRQ_TIMER, cpu, &timer_action);
	if (evt->irq < 0)
		return evt->irq;
	BUG_ON(per_cpu(xen_clock_event.irq, 0) != evt->irq);

	evt->set_mode = percpu_read(xen_clock_event.set_mode);
	evt->set_next_event = percpu_read(xen_clock_event.set_next_event);

	return 0;
}

void __cpuinit local_teardown_timer(unsigned int cpu)
{
	struct clock_event_device *evt = &per_cpu(xen_clock_event, cpu);

	BUG_ON(cpu == 0);
	unbind_from_per_cpu_irq(evt->irq, cpu, &timer_action);
}
#endif

void xen_clockevents_resume(void)
{
	unsigned int cpu;

	if (percpu_read(xen_clock_event.set_mode) != vcpuop_set_mode)
		return;

	for_each_online_cpu(cpu) {
		init_missing_ticks_accounting(cpu);
		if (HYPERVISOR_vcpu_op(VCPUOP_stop_periodic_timer, cpu, NULL))
			BUG();
	}
}

void __init xen_clockevents_init(void)
{
	unsigned int cpu = smp_processor_id();
	struct clock_event_device *evt = &__get_cpu_var(xen_clock_event);

	switch (HYPERVISOR_vcpu_op(VCPUOP_stop_periodic_timer,
				   cpu, NULL)) {
	case 0:
		/*
		 * Successfully turned off 100Hz tick, so we have the
		 * vcpuop-based timer interface
		 */
		evt->set_mode = vcpuop_set_mode;
		evt->set_next_event = vcpuop_set_next_event;
		break;
#if CONFIG_XEN_COMPAT <= 0x030004
	case -ENOSYS:
		printk(KERN_DEBUG "Xen: using timerop interface\n");
		evt->set_mode = timerop_set_mode;
		evt->set_next_event = timerop_set_next_event;
		break;
#endif
	default:
		BUG();
	}

	evt->irq = bind_virq_to_irqaction(VIRQ_TIMER, cpu, &timer_action);
	BUG_ON(evt->irq < 0);

	xen_setup_cpu_clockevents();
}
