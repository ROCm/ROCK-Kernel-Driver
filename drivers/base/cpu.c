/*
 * drivers/base/cpu.c - basic CPU class support
 */

#include <linux/sysdev.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/cpu.h>
#include <linux/topology.h>


struct sysdev_class cpu_sysdev_class = {
	set_kset_name("cpu"),
};
EXPORT_SYMBOL(cpu_sysdev_class);


/*
 * register_cpu - Setup a driverfs device for a CPU.
 * @num - CPU number to use when creating the device.
 *
 * Initialize and register the CPU device.
 */
int __init register_cpu(struct cpu *cpu, int num, struct node *root)
{
	int error;

	cpu->node_id = cpu_to_node(num);
	cpu->sysdev.id = num;
	cpu->sysdev.cls = &cpu_sysdev_class;

	error = sys_device_register(&cpu->sysdev);
	if (!error && root)
		error = sysfs_create_link(&root->sysdev.kobj,
					  &cpu->sysdev.kobj,
					  kobject_name(&cpu->sysdev.kobj));
	return error;
}



int __init cpu_dev_init(void)
{
	return sysdev_class_register(&cpu_sysdev_class);
}
