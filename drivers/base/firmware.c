/*
 * firmware.c - firmware subsystem hoohaw.
 */

#include <linux/kobject.h>
#include <linux/module.h>
#include <linux/init.h>

static decl_subsys(firmware,NULL);

int firmware_register(struct subsystem * s)
{
	kset_set_kset_s(s,firmware_subsys);
	return subsystem_register(s);
}

void firmware_unregister(struct subsystem * s)
{
	subsystem_unregister(s);
}

static int __init firmware_init(void)
{
	return subsystem_register(&firmware_subsys);
}

core_initcall(firmware_init);

EXPORT_SYMBOL(firmware_register);
EXPORT_SYMBOL(firmware_unregister);
