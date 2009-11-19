/*
 * cpuidle.c - core cpuidle infrastructure
 *
 * (C) 2006-2007 Venkatesh Pallipadi <venkatesh.pallipadi@intel.com>
 *               Shaohua Li <shaohua.li@intel.com>
 *               Adam Belay <abelay@novell.com>
 *
 * This code is licenced under the GPL.
 */

#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/sched.h>
#include <linux/notifier.h>
#include <linux/pm_qos_params.h>
#include <linux/cpu.h>
#include <linux/cpuidle.h>
#include <linux/ktime.h>
#include <linux/hrtimer.h>
#include <trace/events/power.h>

#include "cpuidle.h"

DEFINE_PER_CPU(struct cpuidle_device *, cpuidle_devices);
DEFINE_PER_CPU(struct list_head, cpuidle_devices_list);

DEFINE_MUTEX(cpuidle_lock);

#if defined(CONFIG_ARCH_HAS_CPU_IDLE_WAIT)
static void cpuidle_kick_cpus(void)
{
	cpu_idle_wait();
}
#elif defined(CONFIG_SMP)
# error "Arch needs cpu_idle_wait() equivalent here"
#else /* !CONFIG_ARCH_HAS_CPU_IDLE_WAIT && !CONFIG_SMP */
static void cpuidle_kick_cpus(void) {}
#endif

static int __cpuidle_register_device(struct cpuidle_device *dev);

/**
 * cpuidle_idle_call - the main idle loop
 *
 * NOTE: no locks or semaphores should be used here
 */
void cpuidle_idle_call(void)
{
	struct cpuidle_device *dev = __get_cpu_var(cpuidle_devices);
	struct cpuidle_state *target_state;
	int next_state;
	ktime_t	t1, t2;
	s64 diff;

	/* check if the device is ready */
	if (!dev || !dev->enabled) {
#if defined(CONFIG_ARCH_HAS_DEFAULT_IDLE)
		default_idle();
#else
		local_irq_enable();
#endif
		return;
	}

#if 0
	/* shows regressions, re-enable for 2.6.29 */
	/*
	 * run any timers that can be run now, at this point
	 * before calculating the idle duration etc.
	 */
	hrtimer_peek_ahead_timers();
#endif
	/* ask the governor for the next state */
	if (dev->state_count > 1)
		next_state = cpuidle_curr_governor->select(dev);
	else
		next_state = 0;

	if (need_resched()) {
		local_irq_enable();
		return;
	}

	target_state = &dev->states[next_state];

	/* enter the state and update stats */
	dev->last_state = target_state;

	t1 = ktime_get();

	target_state->enter(dev, target_state);

	t2 = ktime_get();
	diff = ktime_to_us(ktime_sub(t2, t1));
	if (diff > INT_MAX)
		diff = INT_MAX;

	dev->last_residency = (int) diff;

	if (dev->last_state)
		target_state = dev->last_state;

	target_state->time += (unsigned long long)dev->last_residency;
	target_state->usage++;

	/* give the governor an opportunity to reflect on the outcome */
	if (cpuidle_curr_governor->reflect)
		cpuidle_curr_governor->reflect(dev);
	trace_power_end(0);
}

/**
 * cpuidle_pause_and_lock - temporarily disables CPUIDLE
 */
void cpuidle_pause_and_lock(void)
{
	mutex_lock(&cpuidle_lock);
	cpuidle_kick_cpus();
}

EXPORT_SYMBOL_GPL(cpuidle_pause_and_lock);

/**
 * cpuidle_resume_and_unlock - resumes CPUIDLE operation
 */
void cpuidle_resume_and_unlock(void)
{
	mutex_unlock(&cpuidle_lock);
}

EXPORT_SYMBOL_GPL(cpuidle_resume_and_unlock);

int cpuidle_add_to_list(struct cpuidle_device *dev)
{
	int ret, cpu = dev->cpu;
	struct cpuidle_device *old_dev;

	if (!list_empty(&per_cpu(cpuidle_devices_list, cpu))) {
		old_dev = list_first_entry(&per_cpu(cpuidle_devices_list, cpu),
				struct cpuidle_device, idle_list);
		cpuidle_remove_state_sysfs(old_dev);
	}

	list_add(&dev->idle_list, &per_cpu(cpuidle_devices_list, cpu));
	ret = cpuidle_add_state_sysfs(dev);
	return ret;
}

void cpuidle_remove_from_list(struct cpuidle_device *dev)
{
	struct cpuidle_device *temp_dev;
	struct list_head *pos;
	int ret, cpu = dev->cpu;

	list_for_each(pos, &per_cpu(cpuidle_devices_list, cpu)) {
		temp_dev = container_of(pos, struct cpuidle_device, idle_list);
		if (dev == temp_dev) {
			list_del(&temp_dev->idle_list);
			cpuidle_remove_state_sysfs(temp_dev);
			break;
		}
	}

	if (!list_empty(&per_cpu(cpuidle_devices_list, cpu))) {
		temp_dev = list_first_entry(&per_cpu(cpuidle_devices_list, cpu),
					struct cpuidle_device, idle_list);
		ret = cpuidle_add_state_sysfs(temp_dev);
	}
	cpuidle_kick_cpus();
}

/**
 * cpuidle_enable_device - enables idle PM for a CPU
 * @dev: the CPU
 *
 * This function must be called between cpuidle_pause_and_lock and
 * cpuidle_resume_and_unlock when used externally.
 */
int cpuidle_enable_device(struct cpuidle_device *dev)
{
	int ret, i;

	if (dev->enabled)
		return 0;
	if (!cpuidle_curr_driver || !cpuidle_curr_governor)
		return -EIO;
	if (!dev->state_count)
		return -EINVAL;

	if (dev->registered == 0) {
		ret = __cpuidle_register_device(dev);
		if (ret)
			return ret;
	}

	if (cpuidle_curr_governor->enable &&
	    (ret = cpuidle_curr_governor->enable(dev)))
		goto fail_sysfs;

	for (i = 0; i < dev->state_count; i++) {
		dev->states[i].usage = 0;
		dev->states[i].time = 0;
	}
	dev->last_residency = 0;
	dev->last_state = NULL;

	smp_wmb();

	dev->enabled = 1;

	return 0;

fail_sysfs:
	cpuidle_remove_from_list(dev);

	return ret;
}

EXPORT_SYMBOL_GPL(cpuidle_enable_device);

/**
 * cpuidle_disable_device - disables idle PM for a CPU
 * @dev: the CPU
 *
 * This function must be called between cpuidle_pause_and_lock and
 * cpuidle_resume_and_unlock when used externally.
 */
void cpuidle_disable_device(struct cpuidle_device *dev)
{
	if (!dev->enabled)
		return;
	if (!cpuidle_curr_driver || !cpuidle_curr_governor)
		return;

	dev->enabled = 0;

	if (cpuidle_curr_governor->disable)
		cpuidle_curr_governor->disable(dev);
}

EXPORT_SYMBOL_GPL(cpuidle_disable_device);

#ifdef CONFIG_ARCH_HAS_CPU_RELAX
static void poll_idle(struct cpuidle_device *dev, struct cpuidle_state *st)
{
	local_irq_enable();
	while (!need_resched())
		cpu_relax();
}

static void poll_idle_init(struct cpuidle_device *dev)
{
	struct cpuidle_state *state = &dev->states[0];

	cpuidle_set_statedata(state, NULL);

	snprintf(state->name, CPUIDLE_NAME_LEN, "C0");
	snprintf(state->desc, CPUIDLE_DESC_LEN, "CPUIDLE CORE POLL IDLE");
	state->exit_latency = 0;
	state->target_residency = 0;
	state->power_usage = -1;
	state->flags = CPUIDLE_FLAG_POLL;
	state->enter = poll_idle;
}
#else
static void poll_idle_init(struct cpuidle_device *dev) {}
#endif /* CONFIG_ARCH_HAS_CPU_RELAX */

/**
 * __cpuidle_register_device - internal register function called before register
 * and enable routines
 * @dev: the cpu
 *
 * cpuidle_lock mutex must be held before this is called
 */
static int __cpuidle_register_device(struct cpuidle_device *dev)
{
	struct sys_device *sys_dev = get_cpu_sysdev((unsigned long)dev->cpu);

	if (!sys_dev)
		return -EINVAL;
	if (!try_module_get(cpuidle_curr_driver->owner))
		return -EINVAL;

	poll_idle_init(dev);

	per_cpu(cpuidle_devices, dev->cpu) = dev;

	dev->registered = 1;
	return 0;
}

/**
 * cpuidle_register_device - registers a CPU's idle PM feature
 * @dev: the cpu
 */
int cpuidle_register_device(struct cpuidle_device *dev)
{
	int ret;

	mutex_lock(&cpuidle_lock);

	if ((ret = __cpuidle_register_device(dev))) {
		mutex_unlock(&cpuidle_lock);
		return ret;
	}

	cpuidle_enable_device(dev);
	cpuidle_add_to_list(dev);

	mutex_unlock(&cpuidle_lock);

	return 0;

}

EXPORT_SYMBOL_GPL(cpuidle_register_device);

/**
 * cpuidle_unregister_device - unregisters a CPU's idle PM feature
 * @dev: the cpu
 */
void cpuidle_unregister_device(struct cpuidle_device *dev)
{
	if (dev->registered == 0)
		return;

	cpuidle_pause_and_lock();

	cpuidle_disable_device(dev);
	cpuidle_remove_from_list(dev);

	per_cpu(cpuidle_devices, dev->cpu) = NULL;

	cpuidle_resume_and_unlock();

	module_put(cpuidle_curr_driver->owner);
}

EXPORT_SYMBOL_GPL(cpuidle_unregister_device);

#ifdef CONFIG_SMP

static void smp_callback(void *v)
{
	/* we already woke the CPU up, nothing more to do */
}

/*
 * This function gets called when a part of the kernel has a new latency
 * requirement.  This means we need to get all processors out of their C-state,
 * and then recalculate a new suitable C-state. Just do a cross-cpu IPI; that
 * wakes them all right up.
 */
static int cpuidle_latency_notify(struct notifier_block *b,
		unsigned long l, void *v)
{
	smp_call_function(smp_callback, NULL, 1);
	return NOTIFY_OK;
}

static struct notifier_block cpuidle_latency_notifier = {
	.notifier_call = cpuidle_latency_notify,
};

static inline void latency_notifier_init(struct notifier_block *n)
{
	pm_qos_add_notifier(PM_QOS_CPU_DMA_LATENCY, n);
}

#else /* CONFIG_SMP */

#define latency_notifier_init(x) do { } while (0)

#endif /* CONFIG_SMP */

/**
 * cpuidle_init - core initializer
 */
static int __init cpuidle_init(void)
{
	int ret, cpu;

	ret = cpuidle_add_class_sysfs(&cpu_sysdev_class);
	if (ret)
		return ret;

	for_each_possible_cpu(cpu)
		INIT_LIST_HEAD(&per_cpu(cpuidle_devices_list, cpu));

	latency_notifier_init(&cpuidle_latency_notifier);

	return 0;
}

core_initcall(cpuidle_init);
