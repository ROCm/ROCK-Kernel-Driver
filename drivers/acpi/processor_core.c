/*
 * Copyright (C) 2005 Intel Corporation
 * Copyright (C) 2009 Hewlett-Packard Development Company, L.P.
 *
 *	Alex Chiang <achiang@hp.com>
 *	- Unified x86/ia64 implementations
 */
#include <linux/export.h>
#include <linux/acpi.h>
#include <acpi/processor.h>

#define _COMPONENT		ACPI_PROCESSOR_COMPONENT
ACPI_MODULE_NAME("processor_core");

#ifdef CONFIG_PROCESSOR_EXTERNAL_CONTROL
/*
 * External processor control logic may register with its own set of
 * ops to get ACPI related notification. One example is like VMM.
 */
const struct processor_extcntl_ops *processor_extcntl_ops;
EXPORT_SYMBOL(processor_extcntl_ops);

int processor_notify_external(struct acpi_processor *pr, int event, int type)
{
	int ret = -EINVAL;

	if (!processor_cntl_external())
		return -EINVAL;

	switch (event) {
	case PROCESSOR_PM_INIT:
	case PROCESSOR_PM_CHANGE:
		if (type >= PM_TYPE_MAX
		    || !processor_extcntl_ops->pm_ops[type])
			break;

		ret = processor_extcntl_ops->pm_ops[type](pr, event);
		break;
#ifdef CONFIG_ACPI_HOTPLUG_CPU
	case PROCESSOR_HOTPLUG:
		if (processor_extcntl_ops->hotplug)
			ret = processor_extcntl_ops->hotplug(pr, type);
		xen_pcpu_hotplug(type);
		break;
#endif
	default:
		pr_err("Unsupported processor event %d.\n", event);
		break;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(processor_notify_external);
#endif

static int map_lapic_id(struct acpi_subtable_header *entry,
		 u32 acpi_id, int *apic_id)
{
	struct acpi_madt_local_apic *lapic =
		(struct acpi_madt_local_apic *)entry;

	if (!(lapic->lapic_flags & ACPI_MADT_ENABLED))
		return -ENODEV;

	if (lapic->processor_id != acpi_id)
		return -EINVAL;

	*apic_id = lapic->id;
	return 0;
}

static int map_x2apic_id(struct acpi_subtable_header *entry,
			 int device_declaration, u32 acpi_id, int *apic_id)
{
	struct acpi_madt_local_x2apic *apic =
		(struct acpi_madt_local_x2apic *)entry;

	if (!(apic->lapic_flags & ACPI_MADT_ENABLED))
		return -ENODEV;

	if (device_declaration && (apic->uid == acpi_id)) {
		*apic_id = apic->local_apic_id;
		return 0;
	}

	return -EINVAL;
}

static int map_lsapic_id(struct acpi_subtable_header *entry,
		int device_declaration, u32 acpi_id, int *apic_id)
{
	struct acpi_madt_local_sapic *lsapic =
		(struct acpi_madt_local_sapic *)entry;

	if (!(lsapic->lapic_flags & ACPI_MADT_ENABLED))
		return -ENODEV;

	if (device_declaration) {
		if ((entry->length < 16) || (lsapic->uid != acpi_id))
			return -EINVAL;
	} else if (lsapic->processor_id != acpi_id)
		return -EINVAL;

	*apic_id = (lsapic->id << 8) | lsapic->eid;
	return 0;
}

static int map_madt_entry(int type, u32 acpi_id)
{
	unsigned long madt_end, entry;
	static struct acpi_table_madt *madt;
	static int read_madt;
	int apic_id = -1;

	if (!read_madt) {
		if (ACPI_FAILURE(acpi_get_table(ACPI_SIG_MADT, 0,
					(struct acpi_table_header **)&madt)))
			madt = NULL;
		read_madt++;
	}

	if (!madt)
		return apic_id;

	entry = (unsigned long)madt;
	madt_end = entry + madt->header.length;

	/* Parse all entries looking for a match. */

	entry += sizeof(struct acpi_table_madt);
	while (entry + sizeof(struct acpi_subtable_header) < madt_end) {
		struct acpi_subtable_header *header =
			(struct acpi_subtable_header *)entry;
		if (header->type == ACPI_MADT_TYPE_LOCAL_APIC) {
			if (!map_lapic_id(header, acpi_id, &apic_id))
				break;
		} else if (header->type == ACPI_MADT_TYPE_LOCAL_X2APIC) {
			if (!map_x2apic_id(header, type, acpi_id, &apic_id))
				break;
		} else if (header->type == ACPI_MADT_TYPE_LOCAL_SAPIC) {
			if (!map_lsapic_id(header, type, acpi_id, &apic_id))
				break;
		}
		entry += header->length;
	}
	return apic_id;
}

static int map_mat_entry(acpi_handle handle, int type, u32 acpi_id)
{
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };
	union acpi_object *obj;
	struct acpi_subtable_header *header;
	int apic_id = -1;

	if (ACPI_FAILURE(acpi_evaluate_object(handle, "_MAT", NULL, &buffer)))
		goto exit;

	if (!buffer.length || !buffer.pointer)
		goto exit;

	obj = buffer.pointer;
	if (obj->type != ACPI_TYPE_BUFFER ||
	    obj->buffer.length < sizeof(struct acpi_subtable_header)) {
		goto exit;
	}

	header = (struct acpi_subtable_header *)obj->buffer.pointer;
	if (header->type == ACPI_MADT_TYPE_LOCAL_APIC) {
		map_lapic_id(header, acpi_id, &apic_id);
	} else if (header->type == ACPI_MADT_TYPE_LOCAL_SAPIC) {
		map_lsapic_id(header, type, acpi_id, &apic_id);
	} else if (header->type == ACPI_MADT_TYPE_LOCAL_X2APIC) {
		map_x2apic_id(header, type, acpi_id, &apic_id);
	}

exit:
	kfree(buffer.pointer);
	return apic_id;
}

int acpi_get_apicid(acpi_handle handle, int type, u32 acpi_id)
{
	int apic_id;

	apic_id = map_mat_entry(handle, type, acpi_id);
	if (apic_id == -1)
		apic_id = map_madt_entry(type, acpi_id);

	return apic_id;
}

int acpi_map_cpuid(int apic_id, u32 acpi_id)
{
#if defined(CONFIG_SMP) && !defined(CONFIG_PROCESSOR_EXTERNAL_CONTROL)
	int i;
#endif

	if (apic_id == -1) {
		/*
		 * On UP processor, there is no _MAT or MADT table.
		 * So above apic_id is always set to -1.
		 *
		 * BIOS may define multiple CPU handles even for UP processor.
		 * For example,
		 *
		 * Scope (_PR)
                 * {
		 *     Processor (CPU0, 0x00, 0x00000410, 0x06) {}
		 *     Processor (CPU1, 0x01, 0x00000410, 0x06) {}
		 *     Processor (CPU2, 0x02, 0x00000410, 0x06) {}
		 *     Processor (CPU3, 0x03, 0x00000410, 0x06) {}
		 * }
		 *
		 * Ignores apic_id and always returns 0 for the processor
		 * handle with acpi id 0 if nr_cpu_ids is 1.
		 * This should be the case if SMP tables are not found.
		 * Return -1 for other CPU's handle.
		 */
		if (nr_cpu_ids <= 1 && acpi_id == 0)
			return acpi_id;
		else
			return apic_id;
	}

#ifdef CONFIG_SMP
#ifndef CONFIG_PROCESSOR_EXTERNAL_CONTROL
	for_each_possible_cpu(i) {
		if (cpu_physical_id(i) == apic_id)
			return i;
	}
#else
	/*
	 * Use of cpu_physical_id() is bogus here. Rather than defining a
	 * stub enforcing a 1:1 mapping, we keep it undefined to catch bad
	 * uses. Return as if there was a 1:1 mapping.
	 */
	if (apic_id < nr_cpu_ids && cpu_possible(apic_id))
		return apic_id;
#endif
#else
	/* In UP kernel, only processor 0 is valid */
	if (apic_id == 0)
		return apic_id;
#endif
	return -1;
}

int acpi_get_cpuid(acpi_handle handle, int type, u32 acpi_id)
{
	int apic_id;

	apic_id = acpi_get_apicid(handle, type, acpi_id);

	return acpi_map_cpuid(apic_id, acpi_id);
}
EXPORT_SYMBOL_GPL(acpi_get_cpuid);
