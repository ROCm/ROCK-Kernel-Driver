/*
 * Support for dynamic reconfiguration (including PCI, Memory, and CPU
 * Hotplug and Dynamic Logical Partitioning on PAPR platforms).
 *
 * Copyright (C) 2009 Nathan Fontenot
 * Copyright (C) 2009 IBM Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/kref.h>
#include <linux/notifier.h>
#include <linux/proc_fs.h>
#include <linux/spinlock.h>
#include <linux/memory_hotplug.h>
#include <linux/sysdev.h>
#include <linux/sysfs.h>
#include <linux/cpu.h>
#include "offline_states.h"

#include <asm/prom.h>
#include <asm/machdep.h>
#include <asm/uaccess.h>
#include <asm/rtas.h>
#include <asm/pSeries_reconfig.h>

#define CFG_CONN_WORK_SIZE	4096
static char workarea[CFG_CONN_WORK_SIZE];
static DEFINE_SPINLOCK(workarea_lock);

struct cc_workarea {
	u32	drc_index;
	u32	zero;
	u32	name_offset;
	u32	prop_length;
	u32	prop_offset;
};

static struct property *parse_cc_property(char *workarea)
{
	struct property *prop;
	struct cc_workarea *ccwa;
	char *name;
	char *value;

	prop = kzalloc(sizeof(*prop), GFP_KERNEL);
	if (!prop)
		return NULL;

	ccwa = (struct cc_workarea *)workarea;
	name = workarea + ccwa->name_offset;
	prop->name = kzalloc(strlen(name) + 1, GFP_KERNEL);
	if (!prop->name) {
		kfree(prop);
		return NULL;
	}

	strcpy(prop->name, name);

	prop->length = ccwa->prop_length;
	value = workarea + ccwa->prop_offset;
	prop->value = kzalloc(prop->length, GFP_KERNEL);
	if (!prop->value) {
		kfree(prop->name);
		kfree(prop);
		return NULL;
	}

	memcpy(prop->value, value, prop->length);
	return prop;
}

static void free_property(struct property *prop)
{
	kfree(prop->name);
	kfree(prop->value);
	kfree(prop);
}

static struct device_node *parse_cc_node(char *work_area)
{
	struct device_node *dn;
	struct cc_workarea *ccwa;
	char *name;

	dn = kzalloc(sizeof(*dn), GFP_KERNEL);
	if (!dn)
		return NULL;

	ccwa = (struct cc_workarea *)work_area;
	name = work_area + ccwa->name_offset;
	dn->full_name = kzalloc(strlen(name) + 1, GFP_KERNEL);
	if (!dn->full_name) {
		kfree(dn);
		return NULL;
	}

	strcpy(dn->full_name, name);
	return dn;
}

static void free_one_cc_node(struct device_node *dn)
{
	struct property *prop;

	while (dn->properties) {
		prop = dn->properties;
		dn->properties = prop->next;
		free_property(prop);
	}

	kfree(dn->full_name);
	kfree(dn);
}

static void free_cc_nodes(struct device_node *dn)
{
	if (dn->child)
		free_cc_nodes(dn->child);

	if (dn->sibling)
		free_cc_nodes(dn->sibling);

	free_one_cc_node(dn);
}

#define NEXT_SIBLING    1
#define NEXT_CHILD      2
#define NEXT_PROPERTY   3
#define PREV_PARENT     4
#define MORE_MEMORY     5
#define CALL_AGAIN	-2
#define ERR_CFG_USE     -9003

struct device_node *configure_connector(u32 drc_index)
{
	struct device_node *dn;
	struct device_node *first_dn = NULL;
	struct device_node *last_dn = NULL;
	struct property *property;
	struct property *last_property = NULL;
	struct cc_workarea *ccwa;
	int cc_token;
	int rc;

	cc_token = rtas_token("ibm,configure-connector");
	if (cc_token == RTAS_UNKNOWN_SERVICE)
		return NULL;

	spin_lock(&workarea_lock);

	ccwa = (struct cc_workarea *)&workarea[0];
	ccwa->drc_index = drc_index;
	ccwa->zero = 0;

	rc = rtas_call(cc_token, 2, 1, NULL, workarea, NULL);
	while (rc) {
		switch (rc) {
		case NEXT_SIBLING:
			dn = parse_cc_node(workarea);
			if (!dn)
				goto cc_error;

			dn->parent = last_dn->parent;
			last_dn->sibling = dn;
			last_dn = dn;
			break;

		case NEXT_CHILD:
			dn = parse_cc_node(workarea);
			if (!dn)
				goto cc_error;

			if (!first_dn)
				first_dn = dn;
			else {
				dn->parent = last_dn;
				if (last_dn)
					last_dn->child = dn;
			}

			last_dn = dn;
			break;

		case NEXT_PROPERTY:
			property = parse_cc_property(workarea);
			if (!property)
				goto cc_error;

			if (!last_dn->properties)
				last_dn->properties = property;
			else
				last_property->next = property;

			last_property = property;
			break;

		case PREV_PARENT:
			last_dn = last_dn->parent;
			break;

		case CALL_AGAIN:
			break;

		case MORE_MEMORY:
		case ERR_CFG_USE:
		default:
			printk(KERN_ERR "Unexpected Error (%d) "
			       "returned from configure-connector\n", rc);
			goto cc_error;
		}

		rc = rtas_call(cc_token, 2, 1, NULL, workarea, NULL);
	}

	spin_unlock(&workarea_lock);
	return first_dn;

cc_error:
	spin_unlock(&workarea_lock);

	if (first_dn)
		free_cc_nodes(first_dn);

	return NULL;
}

static struct device_node *derive_parent(const char *path)
{
	struct device_node *parent;
	char parent_path[128];
	int parent_path_len;

	parent_path_len = strrchr(path, '/') - path + 1;
	strlcpy(parent_path, path, parent_path_len);

	parent = of_find_node_by_path(parent_path);

	return parent;
}

static int add_one_node(struct device_node *dn)
{
	struct proc_dir_entry *ent;
	int rc;

	of_node_set_flag(dn, OF_DYNAMIC);
	kref_init(&dn->kref);
	dn->parent = derive_parent(dn->full_name);

	rc = blocking_notifier_call_chain(&pSeries_reconfig_chain,
					  PSERIES_RECONFIG_ADD, dn);
	if (rc == NOTIFY_BAD) {
		printk(KERN_ERR "Failed to add device node %s\n",
		       dn->full_name);
		return -ENOMEM; /* For now, safe to assume kmalloc failure */
	}

	of_attach_node(dn);

#ifdef CONFIG_PROC_DEVICETREE
	ent = proc_mkdir(strrchr(dn->full_name, '/') + 1, dn->parent->pde);
	if (ent)
		proc_device_tree_add_node(dn, ent);
#endif

	of_node_put(dn->parent);
	return 0;
}

int add_device_tree_nodes(struct device_node *dn)
{
	struct device_node *child = dn->child;
	struct device_node *sibling = dn->sibling;
	int rc;

	dn->child = NULL;
	dn->sibling = NULL;
	dn->parent = NULL;

	rc = add_one_node(dn);
	if (rc)
		return rc;

	if (child) {
		rc = add_device_tree_nodes(child);
		if (rc)
			return rc;
	}

	if (sibling)
		rc = add_device_tree_nodes(sibling);

	return rc;
}

static int remove_one_node(struct device_node *dn)
{
	struct device_node *parent = dn->parent;
	struct property *prop = dn->properties;

#ifdef CONFIG_PROC_DEVICETREE
	while (prop) {
		remove_proc_entry(prop->name, dn->pde);
		prop = prop->next;
	}

	if (dn->pde)
		remove_proc_entry(dn->pde->name, parent->pde);
#endif

	blocking_notifier_call_chain(&pSeries_reconfig_chain,
			    PSERIES_RECONFIG_REMOVE, dn);
	of_detach_node(dn);
	of_node_put(dn); /* Must decrement the refcount */

	return 0;
}

static int _remove_device_tree_nodes(struct device_node *dn)
{
	int rc;

	if (dn->child) {
		rc = _remove_device_tree_nodes(dn->child);
		if (rc)
			return rc;
	}

	if (dn->sibling) {
		rc = _remove_device_tree_nodes(dn->sibling);
		if (rc)
			return rc;
	}

	rc = remove_one_node(dn);
	return rc;
}

int remove_device_tree_nodes(struct device_node *dn)
{
	int rc;

	if (dn->child) {
		rc = _remove_device_tree_nodes(dn->child);
		if (rc)
			return rc;
	}

	rc = remove_one_node(dn);
	return rc;
}

int online_node_cpus(struct device_node *dn)
{
	int rc = 0;
	unsigned int cpu;
	int len, nthreads, i;
	const u32 *intserv;

	intserv = of_get_property(dn, "ibm,ppc-interrupt-server#s", &len);
	if (!intserv)
		return -EINVAL;

	nthreads = len / sizeof(u32);

	cpu_maps_update_begin();
	for (i = 0; i < nthreads; i++) {
		for_each_present_cpu(cpu) {
			if (get_hard_smp_processor_id(cpu) != intserv[i])
				continue;
			BUG_ON(get_cpu_current_state(cpu)
					!= CPU_STATE_OFFLINE);
			cpu_maps_update_done();
			rc = cpu_up(cpu);
			if (rc)
				goto out;
			cpu_maps_update_begin();

			break;
		}
		if (cpu == num_possible_cpus())
			printk(KERN_WARNING "Could not find cpu to online "
			       "with physical id 0x%x\n", intserv[i]);
	}
	cpu_maps_update_done();

out:
	return rc;

}

int offline_node_cpus(struct device_node *dn)
{
	int rc = 0;
	unsigned int cpu;
	int len, nthreads, i;
	const u32 *intserv;

	intserv = of_get_property(dn, "ibm,ppc-interrupt-server#s", &len);
	if (!intserv)
		return -EINVAL;

	nthreads = len / sizeof(u32);

	cpu_maps_update_begin();
	for (i = 0; i < nthreads; i++) {
		for_each_present_cpu(cpu) {
			if (get_hard_smp_processor_id(cpu) != intserv[i])
				continue;

			if (get_cpu_current_state(cpu) == CPU_STATE_OFFLINE)
				break;

			if (get_cpu_current_state(cpu) == CPU_STATE_ONLINE) {
				cpu_maps_update_done();
				rc = cpu_down(cpu);
				if (rc)
					goto out;
				cpu_maps_update_begin();
				break;

			}

			/*
			 * The cpu is in CPU_STATE_INACTIVE.
			 * Upgrade it's state to CPU_STATE_OFFLINE.
			 */
			set_preferred_offline_state(cpu, CPU_STATE_OFFLINE);
			BUG_ON(plpar_hcall_norets(H_PROD, intserv[i])
								!= H_SUCCESS);
			__cpu_die(cpu);
			break;
		}
		if (cpu == num_possible_cpus())
			printk(KERN_WARNING "Could not find cpu to offline "
			       "with physical id 0x%x\n", intserv[i]);
	}
	cpu_maps_update_done();

out:
	return rc;

}

#define DR_ENTITY_SENSE		9003
#define DR_ENTITY_PRESENT	1
#define DR_ENTITY_UNUSABLE	2
#define ALLOCATION_STATE	9003
#define ALLOC_UNUSABLE		0
#define ALLOC_USABLE		1
#define ISOLATION_STATE		9001
#define ISOLATE			0
#define UNISOLATE		1

int acquire_drc(u32 drc_index)
{
	int dr_status, rc;

	rc = rtas_call(rtas_token("get-sensor-state"), 2, 2, &dr_status,
		       DR_ENTITY_SENSE, drc_index);
	if (rc || dr_status != DR_ENTITY_UNUSABLE)
		return -1;

	rc = rtas_set_indicator(ALLOCATION_STATE, drc_index, ALLOC_USABLE);
	if (rc)
		return rc;

	rc = rtas_set_indicator(ISOLATION_STATE, drc_index, UNISOLATE);
	if (rc) {
		rtas_set_indicator(ALLOCATION_STATE, drc_index, ALLOC_UNUSABLE);
		return rc;
	}

	return 0;
}

int release_drc(u32 drc_index)
{
	int dr_status, rc;

	rc = rtas_call(rtas_token("get-sensor-state"), 2, 2, &dr_status,
		       DR_ENTITY_SENSE, drc_index);
	if (rc || dr_status != DR_ENTITY_PRESENT)
		return -1;

	rc = rtas_set_indicator(ISOLATION_STATE, drc_index, ISOLATE);
	if (rc)
		return rc;

	rc = rtas_set_indicator(ALLOCATION_STATE, drc_index, ALLOC_UNUSABLE);
	if (rc) {
		rtas_set_indicator(ISOLATION_STATE, drc_index, UNISOLATE);
		return rc;
	}

	return 0;
}

#ifdef CONFIG_HOTPLUG_CPU
static DEFINE_MUTEX(pseries_cpu_hotplug_mutex);

void cpu_hotplug_driver_lock()
{
	mutex_lock(&pseries_cpu_hotplug_mutex);
}

void cpu_hotplug_driver_unlock()
{
	mutex_unlock(&pseries_cpu_hotplug_mutex);
}

static ssize_t cpu_probe_store(struct class *class, const char *buf,
			       size_t count)
{
	struct device_node *dn;
	unsigned long drc_index;
	char *cpu_name;
	int rc;

	cpu_hotplug_driver_lock();
	rc = strict_strtoul(buf, 0, &drc_index);
	if (rc)
		goto out;

	rc = acquire_drc(drc_index);
	if (rc)
		goto out;

	dn = configure_connector(drc_index);
	if (!dn) {
		release_drc(drc_index);
		goto out;
	}

	/* fixup dn name */
	cpu_name = kzalloc(strlen(dn->full_name) + strlen("/cpus/") + 1,
			   GFP_KERNEL);
	if (!cpu_name) {
		free_cc_nodes(dn);
		release_drc(drc_index);
		rc = -ENOMEM;
		goto out;
	}

	sprintf(cpu_name, "/cpus/%s", dn->full_name);
	kfree(dn->full_name);
	dn->full_name = cpu_name;

	rc = add_device_tree_nodes(dn);
	if (rc)
		release_drc(drc_index);

	rc = online_node_cpus(dn);
out:
	cpu_hotplug_driver_unlock();

	return rc ? -EINVAL : count;
}

static ssize_t cpu_release_store(struct class *class, const char *buf,
				 size_t count)
{
	struct device_node *dn;
	const u32 *drc_index;
	int rc;

	dn = of_find_node_by_path(buf);
	if (!dn)
		return -EINVAL;

	drc_index = of_get_property(dn, "ibm,my-drc-index", NULL);
	if (!drc_index) {
		of_node_put(dn);
		return -EINVAL;
	}

	cpu_hotplug_driver_lock();
	rc = offline_node_cpus(dn);

	if (rc)
		goto out;

	rc = release_drc(*drc_index);
	if (rc) {
		of_node_put(dn);
		goto out;
	}

	rc = remove_device_tree_nodes(dn);
	if (rc)
		acquire_drc(*drc_index);

	of_node_put(dn);
out:
	cpu_hotplug_driver_unlock();
	return rc ? -EINVAL : count;
}

#endif /* CONFIG_HOTPLUG_CPU */

#ifdef CONFIG_MEMORY_HOTPLUG

static struct property *clone_property(struct property *old_prop)
{
	struct property *new_prop;

	new_prop = kzalloc((sizeof *new_prop), GFP_KERNEL);
	if (!new_prop)
		return NULL;

	new_prop->name = kstrdup(old_prop->name, GFP_KERNEL);
	new_prop->value = kzalloc(old_prop->length + 1, GFP_KERNEL);
	if (!new_prop->name || !new_prop->value) {
		free_property(new_prop);
		return NULL;
	}

	memcpy(new_prop->value, old_prop->value, old_prop->length);
	new_prop->length = old_prop->length;

	return new_prop;
}

int platform_probe_memory(u64 phys_addr)
{
	struct device_node *dn = NULL;
	struct property *new_prop;
	struct property *old_prop;
	struct of_drconf_cell *drmem;
	const u64 *lmb_size;
	int num_entries, i;
	int rc = -EINVAL;

	if (!phys_addr)
		goto memory_probe_exit;

	dn = of_find_node_by_path("/ibm,dynamic-reconfiguration-memory");
	if (!dn)
		goto memory_probe_exit;

	lmb_size = of_get_property(dn, "ibm,lmb-size", NULL);
	if (!lmb_size)
		goto memory_probe_exit;

	old_prop = of_find_property(dn, "ibm,dynamic-memory", NULL);
	if (!old_prop)
		goto memory_probe_exit;

	num_entries = *(u32 *)old_prop->value;
	drmem = (struct of_drconf_cell *)
				((char *)old_prop->value + sizeof(u32));

	for (i = 0; i < num_entries; i++) {
		u64 lmb_end_addr = drmem[i].base_addr + *lmb_size;
		if (phys_addr >= drmem[i].base_addr
		    && phys_addr < lmb_end_addr)
			break;
	}

	if (i >= num_entries)
		goto memory_probe_exit;

	if (drmem[i].flags & DRCONF_MEM_ASSIGNED) {
		/* This lmb is already adssigned to the system, nothing to do */
		rc = 0;
		goto memory_probe_exit;
	}

	rc = acquire_drc(drmem[i].drc_index);
	if (rc) {
		rc = -EINVAL;
		goto memory_probe_exit;
	}

	new_prop = clone_property(old_prop);
	drmem = (struct of_drconf_cell *)
				((char *)new_prop->value + sizeof(u32));

	drmem[i].flags |= DRCONF_MEM_ASSIGNED;
	rc = prom_update_property(dn, new_prop, old_prop);
	if (rc) {
		free_property(new_prop);
		rc = -EINVAL;
		goto memory_probe_exit;
	}

	rc = blocking_notifier_call_chain(&pSeries_reconfig_chain,
					  PSERIES_DRCONF_MEM_ADD,
					  &drmem[i].base_addr);
	if (rc == NOTIFY_BAD) {
		prom_update_property(dn, old_prop, new_prop);
		release_drc(drmem[i].drc_index);
		rc = -EINVAL;
	} else
		rc = 0;

memory_probe_exit:
	of_node_put(dn);
	return rc;
}

static ssize_t memory_release_store(struct class *class, const char *buf,
				    size_t count)
{
	unsigned long drc_index;
	struct device_node *dn;
	struct property *new_prop, *old_prop;
	struct of_drconf_cell *drmem;
	int num_entries;
	int i;
	int rc = -EINVAL;

	rc = strict_strtoul(buf, 0, &drc_index);
	if (rc)
		return rc;

	dn = of_find_node_by_path("/ibm,dynamic-reconfiguration-memory");
	if (!dn)
		return rc;

	old_prop = of_find_property(dn, "ibm,dynamic-memory", NULL);
	if (!old_prop)
		goto memory_release_exit;

	num_entries = *(u32 *)old_prop->value;
	drmem = (struct of_drconf_cell *)
				((char *)old_prop->value + sizeof(u32));

	for (i = 0; i < num_entries; i++) {
		if (drmem[i].drc_index == drc_index)
			break;
	}

	if (i >= num_entries)
		goto memory_release_exit;

	new_prop = clone_property(old_prop);
	drmem = (struct of_drconf_cell *)
				((char *)new_prop->value + sizeof(u32));

	drmem[i].flags &= ~DRCONF_MEM_ASSIGNED;
	rc = prom_update_property(dn, new_prop, old_prop);
	if (rc) {
		free_property(new_prop);
		rc = -EINVAL;
		goto memory_release_exit;
	}

	rc = blocking_notifier_call_chain(&pSeries_reconfig_chain,
					  PSERIES_DRCONF_MEM_REMOVE,
					  &drmem[i].base_addr);
	if (rc != NOTIFY_BAD)
		rc = release_drc(drc_index);

	if (rc) {
		prom_update_property(dn, old_prop, new_prop);
		rc = -EINVAL;
	}

memory_release_exit:
	of_node_put(dn);
	return rc ? rc : count;
}

static struct class_attribute class_attr_mem_release =
			__ATTR(release, S_IWUSR, NULL, memory_release_store);
#endif /* CONFIG_MEMORY_HOTPLUG */

#ifdef CONFIG_HOTPLUG_CPU
static struct class_attribute class_attr_cpu_probe =
			__ATTR(probe, S_IWUSR, NULL, cpu_probe_store);
static struct class_attribute class_attr_cpu_release =
			__ATTR(release, S_IWUSR, NULL, cpu_release_store);
#endif

static int pseries_dlpar_init(void)
{
	if (!machine_is(pseries))
		return 0;

#ifdef CONFIG_MEMORY_HOTPLUG
	if (sysfs_create_file(&memory_sysdev_class.kset.kobj,
			      &class_attr_mem_release.attr))
		printk(KERN_INFO "DLPAR: Could not create sysfs memory "
		       "release file\n");
#endif

#ifdef CONFIG_HOTPLUG_CPU
	if (sysfs_create_file(&cpu_sysdev_class.kset.kobj,
			      &class_attr_cpu_probe.attr))
		printk(KERN_INFO "DLPAR: Could not create sysfs cpu "
		       "probe file\n");

	if (sysfs_create_file(&cpu_sysdev_class.kset.kobj,
			      &class_attr_cpu_release.attr))
		printk(KERN_INFO "DLPAR: Could not create sysfs cpu "
		       "release file\n");
#endif

	return 0;
}
device_initcall(pseries_dlpar_init);
