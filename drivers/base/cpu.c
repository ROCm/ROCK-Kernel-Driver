/*
 * drivers/base/cpu.c - basic CPU class support
 */

#include <linux/device.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/cpu.h>

#include <asm/topology.h>


struct class cpu_class = {
	.name		= "cpu",
};


struct device_driver cpu_driver = {
	.name		= "cpu",
	.bus		= &system_bus_type,
};

/*
 * register_cpu - Setup a driverfs device for a CPU.
 * @num - CPU number to use when creating the device.
 *
 * Initialize and register the CPU device.
 */
int __init register_cpu(struct cpu *cpu, int num, struct node *root)
{
	int retval;

	cpu->node_id = cpu_to_node(num);
	cpu->sysdev.name = "cpu";
	cpu->sysdev.id = num;
	if (root)
		cpu->sysdev.root = &root->sysroot;
	snprintf(cpu->sysdev.dev.name, DEVICE_NAME_SIZE, "CPU %u", num);
	cpu->sysdev.dev.driver = &cpu_driver;
	retval = sys_device_register(&cpu->sysdev);
	if (retval)
		return retval;
	memset(&cpu->sysdev.class_dev, 0x00, sizeof(struct class_device));
	cpu->sysdev.class_dev.dev = &cpu->sysdev.dev;
	cpu->sysdev.class_dev.class = &cpu_class;
	snprintf(cpu->sysdev.class_dev.class_id, BUS_ID_SIZE, "cpu%d", num);
	retval = class_device_register(&cpu->sysdev.class_dev);
	if (retval) {
		// FIXME cleanup sys_device_register
		return retval;
	}
	return 0;
}


int __init cpu_dev_init(void)
{
	int error;

	error = class_register(&cpu_class);
	if (!error) {
		error = driver_register(&cpu_driver);
		if (error)
			class_unregister(&cpu_class);
	}
	return error;
}
