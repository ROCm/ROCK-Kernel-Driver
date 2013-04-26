/*
 * Common EFI (Extensible Firmware Interface) support functions
 * Based on Extensible Firmware Interface Specification version 1.0
 *
 * Copyright (C) 1999 VA Linux Systems
 * Copyright (C) 1999 Walt Drummond <drummond@valinux.com>
 * Copyright (C) 1999-2002 Hewlett-Packard Co.
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 *	Stephane Eranian <eranian@hpl.hp.com>
 * Copyright (C) 2005-2008 Intel Co.
 *	Fenghua Yu <fenghua.yu@intel.com>
 *	Bibo Mao <bibo.mao@intel.com>
 *	Chandramouli Narayanan <mouli@linux.intel.com>
 *	Huang Ying <ying.huang@intel.com>
 *
 * Copied from efi_32.c to eliminate the duplicated code between EFI
 * 32/64 support code. --ying 2007-10-26
 *
 * All EFI Runtime Services are not implemented yet as EFI only
 * supports physical mode addressing on SoftSDV. This is to be fixed
 * in a future version.  --drummond 1999-07-20
 *
 * Implemented EFI runtime services and virtual mode calls.  --davidm
 *
 * Goutham Rao: <goutham.rao@intel.com>
 *	Skip non-WB memory and ignore empty memory ranges.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/efi.h>
#include <linux/efi-bgrt.h>
#include <linux/export.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>
#include <linux/time.h>
#include <linux/ucs2_string.h>

#include <asm/setup.h>
#include <asm/efi.h>
#include <asm/time.h>
#include <asm/cacheflush.h>
#include <asm/tlbflush.h>
#include <asm/x86_init.h>

#include <xen/interface/platform.h>

#define EFI_DEBUG	1

/*
 * There's some additional metadata associated with each
 * variable. Intel's reference implementation is 60 bytes - bump that
 * to account for potential alignment constraints
 */
#define VAR_METADATA_SIZE 64

static u64 __initdata efi_var_store_size;
static u64 __initdata efi_var_remaining_size;
static u64 __initdata efi_var_max_var_size;
static u64 boot_used_size;
static u64 boot_var_size;
static u64 active_size;

static unsigned long efi_facility;

/*
 * Returns 1 if 'facility' is enabled, 0 otherwise.
 */
int efi_enabled(int facility)
{
	return test_bit(facility, &efi_facility) != 0;
}
EXPORT_SYMBOL(efi_enabled);

static int __init setup_noefi(char *arg)
{
	__clear_bit(EFI_RUNTIME_SERVICES, &efi_facility);
	return 0;
}
early_param("noefi", setup_noefi);

static bool efi_no_storage_paranoia;

static int __init setup_storage_paranoia(char *arg)
{
	efi_no_storage_paranoia = true;
	return 0;
}
early_param("efi_no_storage_paranoia", setup_storage_paranoia);

#define call op.u.efi_runtime_call
#define DECLARE_CALL(what) \
	struct xen_platform_op op; \
	op.cmd = XENPF_efi_runtime_call; \
	call.function = XEN_EFI_##what; \
	call.misc = 0

static efi_status_t xen_efi_get_time(efi_time_t *tm, efi_time_cap_t *tc)
{
	int err;
	DECLARE_CALL(get_time);

	err = HYPERVISOR_platform_op(&op);
	if (err)
		return EFI_UNSUPPORTED;

	if (tm) {
		BUILD_BUG_ON(sizeof(*tm) != sizeof(call.u.get_time.time));
		memcpy(tm, &call.u.get_time.time, sizeof(*tm));
	}

	if (tc) {
		tc->resolution = call.u.get_time.resolution;
		tc->accuracy = call.u.get_time.accuracy;
		tc->sets_to_zero = !!(call.misc &
				      XEN_EFI_GET_TIME_SET_CLEARS_NS);
	}

	return call.status;
}

static efi_status_t xen_efi_set_time(efi_time_t *tm)
{
	DECLARE_CALL(set_time);

	BUILD_BUG_ON(sizeof(*tm) != sizeof(call.u.set_time));
	memcpy(&call.u.set_time, tm, sizeof(*tm));

	return HYPERVISOR_platform_op(&op) ? EFI_UNSUPPORTED : call.status;
}

static efi_status_t xen_efi_get_wakeup_time(efi_bool_t *enabled,
					    efi_bool_t *pending,
					    efi_time_t *tm)
{
	int err;
	DECLARE_CALL(get_wakeup_time);

	err = HYPERVISOR_platform_op(&op);
	if (err)
		return EFI_UNSUPPORTED;

	if (tm) {
		BUILD_BUG_ON(sizeof(*tm) != sizeof(call.u.get_wakeup_time));
		memcpy(tm, &call.u.get_wakeup_time, sizeof(*tm));
	}

	if (enabled)
		*enabled = !!(call.misc & XEN_EFI_GET_WAKEUP_TIME_ENABLED);

	if (pending)
		*pending = !!(call.misc & XEN_EFI_GET_WAKEUP_TIME_PENDING);

	return call.status;
}

static efi_status_t xen_efi_set_wakeup_time(efi_bool_t enabled, efi_time_t *tm)
{
	DECLARE_CALL(set_wakeup_time);

	BUILD_BUG_ON(sizeof(*tm) != sizeof(call.u.set_wakeup_time));
	if (enabled)
		call.misc = XEN_EFI_SET_WAKEUP_TIME_ENABLE;
	if (tm)
		memcpy(&call.u.set_wakeup_time, tm, sizeof(*tm));
	else
		call.misc |= XEN_EFI_SET_WAKEUP_TIME_ENABLE_ONLY;

	return HYPERVISOR_platform_op(&op) ? EFI_UNSUPPORTED : call.status;
}

static efi_status_t xen_efi_get_variable(efi_char16_t *name,
					 efi_guid_t *vendor,
					 u32 *attr,
					 unsigned long *data_size,
					 void *data)
{
	int err;
	DECLARE_CALL(get_variable);

	set_xen_guest_handle(call.u.get_variable.name, name);
	BUILD_BUG_ON(sizeof(*vendor) !=
		     sizeof(call.u.get_variable.vendor_guid));
	memcpy(&call.u.get_variable.vendor_guid, vendor, sizeof(*vendor));
	call.u.get_variable.size = *data_size;
	set_xen_guest_handle(call.u.get_variable.data, data);
	err = HYPERVISOR_platform_op(&op);
	if (err)
		return EFI_UNSUPPORTED;

	*data_size = call.u.get_variable.size;
	if (attr)
		*attr = call.misc;

	return call.status;
}

static efi_status_t xen_efi_get_next_variable(unsigned long *name_size,
					      efi_char16_t *name,
					      efi_guid_t *vendor)
{
	static bool finished = false;
	static u64 var_size;
	int err;
	DECLARE_CALL(get_next_variable_name);

	call.u.get_next_variable_name.size = *name_size;
	set_xen_guest_handle(call.u.get_next_variable_name.name, name);
	BUILD_BUG_ON(sizeof(*vendor) !=
		     sizeof(call.u.get_next_variable_name.vendor_guid));
	memcpy(&call.u.get_next_variable_name.vendor_guid, vendor,
	       sizeof(*vendor));
	err = HYPERVISOR_platform_op(&op);
	if (err)
		return EFI_UNSUPPORTED;

	*name_size = call.u.get_next_variable_name.size;
	memcpy(vendor, &call.u.get_next_variable_name.vendor_guid,
	       sizeof(*vendor));

	if (call.status == EFI_NOT_FOUND) {
		finished = true;
		if (var_size < boot_used_size) {
			boot_var_size = boot_used_size - var_size;
			active_size += boot_var_size;
		} else {
			pr_warn(FW_BUG "efi: Inconsistent initial sizes\n");
		}
	}

	if (boot_used_size && !finished) {
		unsigned long size;
		u32 attr;
		efi_status_t s;
		void *tmp;

		s = xen_efi_get_variable(name, vendor, &attr, &size, NULL);

		if (s != EFI_BUFFER_TOO_SMALL || !size)
			return call.status;

		tmp = kmalloc(size, GFP_ATOMIC);

		if (!tmp)
			return call.status;

		s = xen_efi_get_variable(name, vendor, &attr, &size, tmp);

		if (s == EFI_SUCCESS && (attr & EFI_VARIABLE_NON_VOLATILE)) {
			var_size += size;
			var_size += ucs2_strsize(name, 1024);
			active_size += size;
			active_size += VAR_METADATA_SIZE;
			active_size += ucs2_strsize(name, 1024);
		}

		kfree(tmp);
	}

	return call.status;
}

static efi_status_t xen_efi_set_variable(efi_char16_t *name,
					 efi_guid_t *vendor,
					 u32 attr,
					 unsigned long data_size,
					 void *data)
{
	efi_status_t status;
	u32 orig_attr = 0;
	unsigned long orig_size = 0;
	DECLARE_CALL(set_variable);

	status = xen_efi_get_variable(name, vendor, &orig_attr, &orig_size,
				      NULL);

	if (status != EFI_BUFFER_TOO_SMALL)
		orig_size = 0;

	set_xen_guest_handle(call.u.set_variable.name, name);
	call.misc = attr;
	BUILD_BUG_ON(sizeof(*vendor) !=
		     sizeof(call.u.set_variable.vendor_guid));
	memcpy(&call.u.set_variable.vendor_guid, vendor, sizeof(*vendor));
	call.u.set_variable.size = data_size;
	set_xen_guest_handle(call.u.set_variable.data, data);

	status = HYPERVISOR_platform_op(&op) ? EFI_UNSUPPORTED : call.status;

	if (status == EFI_SUCCESS) {
		if (orig_size) {
			active_size -= orig_size;
			active_size -= ucs2_strsize(name, 1024);
			active_size -= VAR_METADATA_SIZE;
		}
		if (data_size) {
			active_size += data_size;
			active_size += ucs2_strsize(name, 1024);
			active_size += VAR_METADATA_SIZE;
		}
	}

	return status;
}

static efi_status_t xen_efi_query_variable_info(u32 attr,
						u64 *storage_space,
						u64 *remaining_space,
						u64 *max_variable_size)
{
	int err;
	DECLARE_CALL(query_variable_info);

	if (efi.runtime_version < EFI_2_00_SYSTEM_TABLE_REVISION)
		return EFI_UNSUPPORTED;

	err = HYPERVISOR_platform_op(&op);
	if (err)
		return EFI_UNSUPPORTED;

	*storage_space = call.u.query_variable_info.max_store_size;
	*remaining_space = call.u.query_variable_info.remain_store_size;
	*max_variable_size = call.u.query_variable_info.max_size;

	return call.status;
}

static efi_status_t xen_efi_get_next_high_mono_count(u32 *count)
{
	int err;
	DECLARE_CALL(get_next_high_monotonic_count);

	err = HYPERVISOR_platform_op(&op);
	if (err)
		return EFI_UNSUPPORTED;

	*count = call.misc;

	return call.status;
}

static efi_status_t xen_efi_update_capsule(efi_capsule_header_t **capsules,
					   unsigned long count,
					   unsigned long sg_list)
{
	DECLARE_CALL(update_capsule);

	if (efi.runtime_version < EFI_2_00_SYSTEM_TABLE_REVISION)
		return EFI_UNSUPPORTED;

	set_xen_guest_handle(call.u.update_capsule.capsule_header_array,
			     capsules);
	call.u.update_capsule.capsule_count = count;
	call.u.update_capsule.sg_list = sg_list;

	return HYPERVISOR_platform_op(&op) ? EFI_UNSUPPORTED : call.status;
}

static efi_status_t xen_efi_query_capsule_caps(efi_capsule_header_t **capsules,
					       unsigned long count,
					       u64 *max_size,
					       int *reset_type)
{
	int err;
	DECLARE_CALL(query_capsule_capabilities);

	if (efi.runtime_version < EFI_2_00_SYSTEM_TABLE_REVISION)
		return EFI_UNSUPPORTED;

	set_xen_guest_handle(call.u.query_capsule_capabilities.capsule_header_array,
			     capsules);
	call.u.query_capsule_capabilities.capsule_count = count;

	err = HYPERVISOR_platform_op(&op);
	if (err)
		return EFI_UNSUPPORTED;

	*max_size = call.u.query_capsule_capabilities.max_capsule_size;
	*reset_type = call.u.query_capsule_capabilities.reset_type;

	return call.status;
}

struct efi __read_mostly efi = {
	.mps                      = EFI_INVALID_TABLE_ADDR,
	.acpi                     = EFI_INVALID_TABLE_ADDR,
	.acpi20                   = EFI_INVALID_TABLE_ADDR,
	.smbios                   = EFI_INVALID_TABLE_ADDR,
	.sal_systab               = EFI_INVALID_TABLE_ADDR,
	.boot_info                = EFI_INVALID_TABLE_ADDR,
	.hcdp                     = EFI_INVALID_TABLE_ADDR,
	.uga                      = EFI_INVALID_TABLE_ADDR,
	.uv_systab                = EFI_INVALID_TABLE_ADDR,
	.get_time                 = xen_efi_get_time,
	.set_time                 = xen_efi_set_time,
	.get_wakeup_time          = xen_efi_get_wakeup_time,
	.set_wakeup_time          = xen_efi_set_wakeup_time,
	.get_variable             = xen_efi_get_variable,
	.get_next_variable        = xen_efi_get_next_variable,
	.set_variable             = xen_efi_set_variable,
	.get_next_high_mono_count = xen_efi_get_next_high_mono_count,
	.query_variable_info      = xen_efi_query_variable_info,
	.update_capsule           = xen_efi_update_capsule,
	.query_capsule_caps       = xen_efi_query_capsule_caps,
};
EXPORT_SYMBOL(efi);

int efi_set_rtc_mmss(unsigned long nowtime)
{
	int real_seconds, real_minutes;
	efi_status_t 	status;
	efi_time_t 	eft;
	efi_time_cap_t 	cap;

	status = efi.get_time(&eft, &cap);
	if (status != EFI_SUCCESS) {
		pr_err("Oops: efitime: can't read time!\n");
		return -1;
	}

	real_seconds = nowtime % 60;
	real_minutes = nowtime / 60;
	if (((abs(real_minutes - eft.minute) + 15)/30) & 1)
		real_minutes += 30;
	real_minutes %= 60;
	eft.minute = real_minutes;
	eft.second = real_seconds;

	status = efi.set_time(&eft);
	if (status != EFI_SUCCESS) {
		pr_err("Oops: efitime: can't write time!\n");
		return -1;
	}
	return 0;
}

unsigned long efi_get_time(void)
{
	efi_status_t status;
	efi_time_t eft;
	efi_time_cap_t cap;

	status = efi.get_time(&eft, &cap);
	if (status != EFI_SUCCESS) {
		pr_err("Oops: efitime: can't read time!\n");
		return mach_get_cmos_time();
	}

	return mktime(eft.year, eft.month, eft.day, eft.hour,
		      eft.minute, eft.second);
}

void __init efi_probe(void)
{
	static struct xen_platform_op __initdata op = {
		.cmd = XENPF_firmware_info,
		.u.firmware_info = {
			.type = XEN_FW_EFI_INFO,
			.index = XEN_FW_EFI_CONFIG_TABLE
		}
	};

	if (HYPERVISOR_platform_op(&op) == 0) {
		__set_bit(EFI_BOOT, &efi_facility);
		__set_bit(EFI_64BIT, &efi_facility);
		__set_bit(EFI_SYSTEM_TABLES, &efi_facility);
		__set_bit(EFI_RUNTIME_SERVICES, &efi_facility);
		__set_bit(EFI_MEMMAP, &efi_facility);
	}
}

void __init efi_reserve_boot_services(void) { }
void __init efi_free_boot_services(void) { }

static int __init efi_config_init(u64 tables, unsigned int nr_tables)
{
	void *config_tables, *tablep;
	unsigned int i, sz = sizeof(efi_config_table_t);

	/*
	 * Let's see what config tables the firmware passed to us.
	 */
	config_tables = early_ioremap(tables, nr_tables * sz);
	if (config_tables == NULL) {
		pr_err("Could not map Configuration table!\n");
		return -ENOMEM;
	}

	tablep = config_tables;
	pr_info("");
	for (i = 0; i < nr_tables; i++) {
		efi_guid_t guid;
		unsigned long table;

		guid = ((efi_config_table_t *)tablep)->guid;
		table = ((efi_config_table_t *)tablep)->table;
		if (!efi_guidcmp(guid, MPS_TABLE_GUID)) {
			efi.mps = table;
			pr_cont(" MPS=0x%lx ", table);
		} else if (!efi_guidcmp(guid, ACPI_20_TABLE_GUID)) {
			efi.acpi20 = table;
			pr_cont(" ACPI 2.0=0x%lx ", table);
		} else if (!efi_guidcmp(guid, ACPI_TABLE_GUID)) {
			efi.acpi = table;
			pr_cont(" ACPI=0x%lx ", table);
		} else if (!efi_guidcmp(guid, SMBIOS_TABLE_GUID)) {
			efi.smbios = table;
			pr_cont(" SMBIOS=0x%lx ", table);
		} else if (!efi_guidcmp(guid, HCDP_TABLE_GUID)) {
			efi.hcdp = table;
			pr_cont(" HCDP=0x%lx ", table);
		} else if (!efi_guidcmp(guid, UGA_IO_PROTOCOL_GUID)) {
			efi.uga = table;
			pr_cont(" UGA=0x%lx ", table);
		}
		tablep += sz;
	}
	pr_cont("\n");
	early_iounmap(config_tables, nr_tables * sz);
	return 0;
}

void __init efi_init(void)
{
	efi_char16_t c16[100];
	char vendor[ARRAY_SIZE(c16)] = "unknown";
	int ret, i;
	struct xen_platform_op op;
	union xenpf_efi_info *info = &op.u.firmware_info.u.efi_info;

	op.cmd = XENPF_firmware_info;
	op.u.firmware_info.type = XEN_FW_EFI_INFO;

	/*
	 * Show what we know for posterity
	 */
	op.u.firmware_info.index = XEN_FW_EFI_VENDOR;
	info->vendor.bufsz = sizeof(c16);
	set_xen_guest_handle(info->vendor.name, c16);
	ret = HYPERVISOR_platform_op(&op);
	if (!ret) {
		for (i = 0; i < sizeof(vendor) - 1 && c16[i]; ++i)
			vendor[i] = c16[i];
		vendor[i] = '\0';
	} else
		pr_err("Could not get the firmware vendor!\n");

	op.u.firmware_info.index = XEN_FW_EFI_VERSION;
	ret = HYPERVISOR_platform_op(&op);
	if (!ret)
		pr_info("EFI v%u.%.02u by %s\n",
			info->version >> 16,
			info->version & 0xffff, vendor);
	else
		pr_err("Could not get EFI revision!\n");

	op.u.firmware_info.index = XEN_FW_EFI_RT_VERSION;
	ret = HYPERVISOR_platform_op(&op);
	if (!ret)
		efi.runtime_version = info->version;
	else
		pr_warn("Could not get runtime services revision.\n");

	if (efi.runtime_version >= EFI_2_00_SYSTEM_TABLE_REVISION) {
		DECLARE_CALL(query_variable_info);

		call.misc = XEN_EFI_VARINFO_BOOT_SNAPSHOT;
		call.u.query_variable_info.attr =
			EFI_VARIABLE_NON_VOLATILE |
			EFI_VARIABLE_BOOTSERVICE_ACCESS |
			EFI_VARIABLE_RUNTIME_ACCESS;
		ret = HYPERVISOR_platform_op(&op);
		if (!ret && call.status == EFI_SUCCESS) {
			efi_var_store_size =
				call.u.query_variable_info.max_store_size;
			efi_var_remaining_size =
				call.u.query_variable_info.remain_store_size;
			efi_var_max_var_size =
				call.u.query_variable_info.max_size;
		}
	}

	boot_used_size = efi_var_store_size - efi_var_remaining_size;

	x86_platform.get_wallclock = efi_get_time;
	x86_platform.set_wallclock = efi_set_rtc_mmss;

	op.u.firmware_info.index = XEN_FW_EFI_CONFIG_TABLE;
	if (HYPERVISOR_platform_op(&op))
		BUG();
	if (efi_config_init(info->cfg.addr, info->cfg.nent))
		return;

	__set_bit(EFI_CONFIG_TABLES, &efi_facility);
}

#undef DECLARE_CALL
#undef call

void __init efi_late_init(void)
{
	efi_bgrt_init();
}

void __iomem *efi_lookup_mapped_addr(u64 phys_addr) { return NULL; }

void __init efi_enter_virtual_mode(void) { }

static struct platform_device rtc_efi_dev = {
	.name = "rtc-efi",
	.id = -1,
};

static int __init rtc_init(void)
{
	if (efi_enabled(EFI_RUNTIME_SERVICES)
	    && platform_device_register(&rtc_efi_dev) < 0)
		pr_err("unable to register rtc device...\n");

	/* not necessarily an error */
	return 0;
}
arch_initcall(rtc_init);

/*
 * Convenience functions to obtain memory types and attributes
 */
u32 efi_mem_type(unsigned long phys_addr)
{
	struct xen_platform_op op;
	union xenpf_efi_info *info = &op.u.firmware_info.u.efi_info;

	if (!efi_enabled(EFI_MEMMAP))
		return 0;

	op.cmd = XENPF_firmware_info;
	op.u.firmware_info.type = XEN_FW_EFI_INFO;
	op.u.firmware_info.index = XEN_FW_EFI_MEM_INFO;
	info->mem.addr = phys_addr;
	info->mem.size = 0;
	return HYPERVISOR_platform_op(&op) ? 0 : info->mem.type;
}

u64 efi_mem_attributes(unsigned long phys_addr)
{
	struct xen_platform_op op;
	union xenpf_efi_info *info = &op.u.firmware_info.u.efi_info;

	op.cmd = XENPF_firmware_info;
	op.u.firmware_info.type = XEN_FW_EFI_INFO;
	op.u.firmware_info.index = XEN_FW_EFI_MEM_INFO;
	info->mem.addr = phys_addr;
	info->mem.size = 0;
	return HYPERVISOR_platform_op(&op) ? 0 : info->mem.attr;
}

/*
 * Some firmware has serious problems when using more than 50% of the EFI
 * variable store, i.e. it triggers bugs that can brick machines. Ensure that
 * we never use more than this safe limit.
 *
 * Return EFI_SUCCESS if it is safe to write 'size' bytes to the variable
 * store.
 */
efi_status_t efi_query_variable_store(u32 attributes, unsigned long size)
{
	efi_status_t status;
	u64 storage_size, remaining_size, max_size;

	status = xen_efi_query_variable_info(attributes, &storage_size,
					     &remaining_size, &max_size);
	if (status != EFI_SUCCESS)
		return status;

	if (!max_size && remaining_size > size)
		printk_once(KERN_ERR FW_BUG "Broken EFI implementation"
			    " is returning MaxVariableSize=0\n");
	/*
	 * Some firmware implementations refuse to boot if there's insufficient
	 * space in the variable store. We account for that by refusing the
	 * write if permitting it would reduce the available space to under
	 * 50%. However, some firmware won't reclaim variable space until
	 * after the used (not merely the actively used) space drops below
	 * a threshold. We can approximate that case with the value calculated
	 * above. If both the firmware and our calculations indicate that the
	 * available space would drop below 50%, refuse the write.
	 */

	if (!storage_size || size > remaining_size ||
	    (max_size && size > max_size))
		return EFI_OUT_OF_RESOURCES;

	if (!efi_no_storage_paranoia &&
	    ((active_size + size + VAR_METADATA_SIZE > storage_size / 2) &&
	     (remaining_size - size < storage_size / 2)))
		return EFI_OUT_OF_RESOURCES;

	return EFI_SUCCESS;
}
EXPORT_SYMBOL_GPL(efi_query_variable_store);
