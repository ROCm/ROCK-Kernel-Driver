/*
 * drivers/base/cpu.c - basic CPU class support
 */

#include <linux/device.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/cpu.h>

#include <asm/topology.h>


static int cpu_add_device(struct device * dev)
{
	return 0;
}
struct device_class cpu_devclass = {
	.name		= "cpu",
	.add_device	= cpu_add_device,
};


struct device_driver cpu_driver = {
	.name		= "cpu",
	.bus		= &system_bus_type,
	.devclass	= &cpu_devclass,
};


/*
 * register_cpu - Setup a driverfs device for a CPU.
 * @num - CPU number to use when creating the device.
 *
 * Initialize and register the CPU device.
 */
int __init register_cpu(struct cpu *cpu, int num, struct node *root)
{
	cpu->node_id = cpu_to_node(num);
	cpu->sysdev.name = "cpu";
	cpu->sysdev.id = num;
	if (root)
		cpu->sysdev.root = &root->sysroot;
	snprintf(cpu->sysdev.dev.name, DEVICE_NAME_SIZE, "CPU %u", num);
	cpu->sysdev.dev.driver = &cpu_driver;
	return sys_device_register(&cpu->sysdev);
}


int __init cpu_dev_init(void)
{
	int error;
	if (!(error = devclass_register(&cpu_devclass)))
		if (error = driver_register(&cpu_driver))
			devclass_unregister(&cpu_devclass);
	return error;
}
