/*
 * pcpu.c - management physical cpu in dom0 environment
 */
#include <linux/acpi.h>
#include <linux/cpu.h>
#include <linux/interrupt.h>
#include <linux/kobject.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/sysdev.h>
#include <asm/hypervisor.h>
#include <xen/interface/platform.h>
#include <xen/evtchn.h>
#include <xen/pcpu.h>
#include <acpi/processor.h>

struct pcpu {
	struct list_head pcpu_list;
	struct sys_device sysdev;
	uint32_t xen_id;
	uint32_t apic_id;
	uint32_t acpi_id;
	uint32_t flags;
};

static inline int xen_pcpu_online(uint32_t flags)
{
	return !!(flags & XEN_PCPU_FLAGS_ONLINE);
}

static DEFINE_MUTEX(xen_pcpu_lock);

/* No need for irq disable since hotplug notify is in workqueue context */
#define get_pcpu_lock() mutex_lock(&xen_pcpu_lock);
#define put_pcpu_lock() mutex_unlock(&xen_pcpu_lock);

static LIST_HEAD(xen_pcpus);

static BLOCKING_NOTIFIER_HEAD(pcpu_chain);

static inline void *notifier_param(const struct pcpu *pcpu)
{
	return (void *)(unsigned long)pcpu->xen_id;
}

int register_pcpu_notifier(struct notifier_block *nb)
{
	int err;

	get_pcpu_lock();

	err = blocking_notifier_chain_register(&pcpu_chain, nb);

	if (!err) {
		struct pcpu *pcpu;

		list_for_each_entry(pcpu, &xen_pcpus, pcpu_list)
			if (xen_pcpu_online(pcpu->flags))
				nb->notifier_call(nb, CPU_ONLINE,
						  notifier_param(pcpu));
	}

	put_pcpu_lock();

	return err;
}
EXPORT_SYMBOL_GPL(register_pcpu_notifier);

void unregister_pcpu_notifier(struct notifier_block *nb)
{
	get_pcpu_lock();
	blocking_notifier_chain_unregister(&pcpu_chain, nb);
	put_pcpu_lock();
}
EXPORT_SYMBOL_GPL(unregister_pcpu_notifier);

static int xen_pcpu_down(uint32_t xen_id)
{
	xen_platform_op_t op;

	op.cmd = XENPF_cpu_offline;
	op.u.cpu_ol.cpuid = xen_id;
	return HYPERVISOR_platform_op(&op);
}

static int xen_pcpu_up(uint32_t xen_id)
{
	xen_platform_op_t op;

	op.cmd = XENPF_cpu_online;
	op.u.cpu_ol.cpuid = xen_id;
	return HYPERVISOR_platform_op(&op);
}

static ssize_t show_online(struct sys_device *dev,
			   struct sysdev_attribute *attr,
			   char *buf)
{
	struct pcpu *cpu = container_of(dev, struct pcpu, sysdev);

	return sprintf(buf, "%d\n", xen_pcpu_online(cpu->flags));
}

static ssize_t store_online(struct sys_device *dev,
			    struct sysdev_attribute *attr,
			    const char *buf, size_t count)
{
	struct pcpu *cpu = container_of(dev, struct pcpu, sysdev);
	ssize_t ret;

	switch (buf[0]) {
	case '0':
		ret = xen_pcpu_down(cpu->xen_id);
		break;
	case '1':
		ret = xen_pcpu_up(cpu->xen_id);
		break;
	default:
		ret = -EINVAL;
	}

	if (ret >= 0)
		ret = count;
	return ret;
}

static SYSDEV_ATTR(online, 0644, show_online, store_online);

static ssize_t show_apicid(struct sys_device *dev,
			   struct sysdev_attribute *attr,
			   char *buf)
{
	struct pcpu *cpu = container_of(dev, struct pcpu, sysdev);

	return sprintf(buf, "%#x\n", cpu->apic_id);
}
static SYSDEV_ATTR(apic_id, 0444, show_apicid, NULL);

static ssize_t show_acpiid(struct sys_device *dev,
			   struct sysdev_attribute *attr,
			   char *buf)
{
	struct pcpu *cpu = container_of(dev, struct pcpu, sysdev);

	return sprintf(buf, "%#x\n", cpu->acpi_id);
}
static SYSDEV_ATTR(acpi_id, 0444, show_acpiid, NULL);

static struct sysdev_class xen_pcpu_sysdev_class = {
	.name = "xen_pcpu",
};

static int xen_pcpu_free(struct pcpu *pcpu)
{
	if (!pcpu)
		return 0;

	sysdev_remove_file(&pcpu->sysdev, &attr_online);
	sysdev_remove_file(&pcpu->sysdev, &attr_apic_id);
	sysdev_remove_file(&pcpu->sysdev, &attr_acpi_id);
	sysdev_unregister(&pcpu->sysdev);
	list_del(&pcpu->pcpu_list);
	kfree(pcpu);

	return 0;
}

static inline int same_pcpu(struct xenpf_pcpuinfo *info,
			    struct pcpu *pcpu)
{
	return (pcpu->apic_id == info->apic_id) &&
		(pcpu->xen_id == info->xen_cpuid);
}

/*
 * Return 1 if online status changed
 */
static int xen_pcpu_online_check(struct xenpf_pcpuinfo *info,
				 struct pcpu *pcpu)
{
	int result = 0;

	if (info->xen_cpuid != pcpu->xen_id)
		return 0;

	if (xen_pcpu_online(info->flags) && !xen_pcpu_online(pcpu->flags)) {
		/* the pcpu is onlined */
		pcpu->flags |= XEN_PCPU_FLAGS_ONLINE;
		blocking_notifier_call_chain(&pcpu_chain, CPU_ONLINE,
					     notifier_param(pcpu));
		kobject_uevent(&pcpu->sysdev.kobj, KOBJ_ONLINE);
		result = 1;
	} else if (!xen_pcpu_online(info->flags) &&
		   xen_pcpu_online(pcpu->flags))  {
		/* The pcpu is offlined now */
		pcpu->flags &= ~XEN_PCPU_FLAGS_ONLINE;
		blocking_notifier_call_chain(&pcpu_chain, CPU_DEAD,
					     notifier_param(pcpu));
		kobject_uevent(&pcpu->sysdev.kobj, KOBJ_OFFLINE);
		result = 1;
	}

	return result;
}

static int pcpu_sysdev_init(struct pcpu *cpu)
{
	int error;

	error = sysdev_register(&cpu->sysdev);
	if (error) {
		pr_warn("xen_pcpu_add: Failed to register pcpu\n");
		kfree(cpu);
		return -1;
	}
	sysdev_create_file(&cpu->sysdev, &attr_online);
	sysdev_create_file(&cpu->sysdev, &attr_apic_id);
	sysdev_create_file(&cpu->sysdev, &attr_acpi_id);
	return 0;
}

static struct pcpu *get_pcpu(unsigned int xen_id)
{
	struct pcpu *pcpu;

	list_for_each_entry(pcpu, &xen_pcpus, pcpu_list)
		if (pcpu->xen_id == xen_id)
			return pcpu;

	return NULL;
}

static struct pcpu *init_pcpu(struct xenpf_pcpuinfo *info)
{
	struct pcpu *pcpu;

	if (info->flags & XEN_PCPU_FLAGS_INVALID)
		return NULL;

	/* The PCPU is just added */
	pcpu = kzalloc(sizeof(struct pcpu), GFP_KERNEL);
	if (!pcpu)
		return NULL;

	INIT_LIST_HEAD(&pcpu->pcpu_list);
	pcpu->xen_id = info->xen_cpuid;
	pcpu->apic_id = info->apic_id;
	pcpu->acpi_id = info->acpi_id;
	pcpu->flags = info->flags;

	pcpu->sysdev.cls = &xen_pcpu_sysdev_class;
	pcpu->sysdev.id = info->xen_cpuid;

	if (pcpu_sysdev_init(pcpu)) {
		kfree(pcpu);
		return NULL;
	}

	list_add_tail(&pcpu->pcpu_list, &xen_pcpus);
	return pcpu;
}

#define PCPU_NO_CHANGE			0
#define PCPU_ADDED			1
#define PCPU_ONLINE_OFFLINE		2
#define PCPU_REMOVED			3
/*
 * Caller should hold the pcpu lock
 * < 0: Something wrong
 * 0: No changes
 * > 0: State changed
 */
static struct pcpu *_sync_pcpu(unsigned int cpu_num, unsigned int *max_id,
			       int *result)
{
	struct pcpu *pcpu;
	struct xenpf_pcpuinfo *info;
	xen_platform_op_t op;
	int ret;

	*result = -1;

	op.cmd = XENPF_get_cpuinfo;
	info = &op.u.pcpu_info;
	info->xen_cpuid = cpu_num;

	do {
		ret = HYPERVISOR_platform_op(&op);
	} while (ret == -EBUSY);
	if (ret)
		return NULL;

	if (max_id)
		*max_id = op.u.pcpu_info.max_present;

	pcpu = get_pcpu(cpu_num);

	if (info->flags & XEN_PCPU_FLAGS_INVALID) {
		/* The pcpu has been removed */
		*result = PCPU_NO_CHANGE;
		if (pcpu) {
			xen_pcpu_free(pcpu);
			*result = PCPU_REMOVED;
		}
		return NULL;
	}


	if (!pcpu) {
		*result = PCPU_ADDED;
		pcpu = init_pcpu(info);
		if (pcpu == NULL) {
			pr_warn("Failed to init pcpu %x\n", info->xen_cpuid);
			*result = -1;
		}
	} else {
		*result = PCPU_NO_CHANGE;
		/*
		 * Old PCPU is replaced with a new pcpu, this means
		 * several virq is missed, will it happen?
		 */
		if (!same_pcpu(info, pcpu)) {
			pr_warn("Pcpu %x changed!\n", pcpu->xen_id);
			pcpu->apic_id = info->apic_id;
			pcpu->acpi_id = info->acpi_id;
		}
		if (xen_pcpu_online_check(info, pcpu))
			*result = PCPU_ONLINE_OFFLINE;
	}
	return pcpu;
}

/*
 * Sync dom0's pcpu information with xen hypervisor's
 */
static int xen_sync_pcpus(void)
{
	/*
	 * Boot cpu always have cpu_id 0 in xen
	 */
	unsigned int cpu_num = 0, max_id = 0;
	int result = 0;
	struct pcpu *pcpu;

	get_pcpu_lock();

	while ((result >= 0) && (cpu_num <= max_id)) {
		pcpu = _sync_pcpu(cpu_num, &max_id, &result);

		switch (result)	{
		case PCPU_NO_CHANGE:
		case PCPU_ADDED:
		case PCPU_ONLINE_OFFLINE:
		case PCPU_REMOVED:
			break;
		default:
			pr_warn("Failed to sync pcpu %x\n", cpu_num);
			break;
		}
		cpu_num++;
	}

	if (result < 0) {
		struct pcpu *tmp;

		list_for_each_entry_safe(pcpu, tmp, &xen_pcpus, pcpu_list)
			xen_pcpu_free(pcpu);
	}

	put_pcpu_lock();

	return 0;
}

static void xen_pcpu_dpc(struct work_struct *work)
{
	if (xen_sync_pcpus() < 0)
		pr_warn("xen_pcpu_dpc: Failed to sync pcpu information\n");
}
static DECLARE_WORK(xen_pcpu_work, xen_pcpu_dpc);

static irqreturn_t xen_pcpu_interrupt(int irq, void *dev_id)
{
	schedule_work(&xen_pcpu_work);

	return IRQ_HANDLED;
}

#ifdef CONFIG_ACPI_HOTPLUG_CPU

int xen_pcpu_hotplug(int type)
{
	schedule_work(&xen_pcpu_work);

	return 0;
}
EXPORT_SYMBOL_GPL(xen_pcpu_hotplug);

int xen_pcpu_index(uint32_t id, bool is_acpiid)
{
	unsigned int cpu_num, max_id;
	xen_platform_op_t op;
	struct xenpf_pcpuinfo *info = &op.u.pcpu_info;

	op.cmd = XENPF_get_cpuinfo;
	for (max_id = cpu_num = 0; cpu_num <= max_id; ++cpu_num) {
		int ret;

		info->xen_cpuid = cpu_num;
		do {
			ret = HYPERVISOR_platform_op(&op);
		} while (ret == -EBUSY);
		if (ret)
			continue;

		if (info->max_present > max_id)
			max_id = info->max_present;
		if (id == (is_acpiid ? info->acpi_id : info->apic_id))
			return cpu_num;
	}

	return -1;
}
EXPORT_SYMBOL_GPL(xen_pcpu_index);

#endif /* CONFIG_ACPI_HOTPLUG_CPU */

static int __init xen_pcpu_init(void)
{
	int err;

	if (!is_initial_xendomain())
		return 0;

	err = sysdev_class_register(&xen_pcpu_sysdev_class);
	if (err) {
		pr_warn("xen_pcpu_init: "
			"Failed to register sysdev class (%d)\n", err);
		return err;
	}

	xen_sync_pcpus();

	if (!list_empty(&xen_pcpus))
		err = bind_virq_to_irqhandler(VIRQ_PCPU_STATE, 0,
					      xen_pcpu_interrupt, 0,
					      "pcpu", NULL);
	if (err < 0)
		pr_warn("xen_pcpu_init: "
			"Failed to bind pcpu_state virq (%d)\n", err);

	return err;
}
subsys_initcall(xen_pcpu_init);
