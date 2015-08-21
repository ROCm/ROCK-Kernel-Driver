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
 * Copyright (C) 2013 SuSE Labs
 *	Borislav Petkov <bp@suse.de> - runtime services VA mapping
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
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/time.h>

#include <asm/setup.h>
#include <asm/efi.h>
#include <asm/time.h>
#include <asm/cacheflush.h>
#include <asm/tlbflush.h>
#include <asm/x86_init.h>
#include <asm/rtc.h>

#include <xen/interface/platform.h>

#define EFI_DEBUG

static efi_config_table_type_t arch_tables[] __initdata = {
#ifdef CONFIG_X86_UV
	{UV_SYSTEM_TABLE_GUID, "UVsystab", &efi.uv_systab},
#endif
	{NULL_GUID, NULL, NULL},
};

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

	return call.status;
}

static efi_status_t xen_efi_set_variable(efi_char16_t *name,
					 efi_guid_t *vendor,
					 u32 attr,
					 unsigned long data_size,
					 void *data)
{
	DECLARE_CALL(set_variable);

	set_xen_guest_handle(call.u.set_variable.name, name);
	call.misc = attr;
	BUILD_BUG_ON(sizeof(*vendor) !=
		     sizeof(call.u.set_variable.vendor_guid));
	memcpy(&call.u.set_variable.vendor_guid, vendor, sizeof(*vendor));
	call.u.set_variable.size = data_size;
	set_xen_guest_handle(call.u.set_variable.data, data);

	return HYPERVISOR_platform_op(&op) ? EFI_UNSUPPORTED : call.status;
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

	call.u.query_variable_info.attr = attr;

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

int efi_set_rtc_mmss(const struct timespec *now)
{
	unsigned long nowtime = now->tv_sec;
	efi_status_t	status;
	efi_time_t	eft;
	efi_time_cap_t	cap;
	struct rtc_time	tm;

	status = efi.get_time(&eft, &cap);
	if (status != EFI_SUCCESS) {
		pr_err("Oops: efitime: can't read time!\n");
		return -1;
	}

	rtc_time_to_tm(nowtime, &tm);
	if (!rtc_valid_tm(&tm)) {
		eft.year = tm.tm_year + 1900;
		eft.month = tm.tm_mon + 1;
		eft.day = tm.tm_mday;
		eft.minute = tm.tm_min;
		eft.second = tm.tm_sec;
		eft.nanosecond = 0;
	} else {
		pr_err("%s: Invalid EFI RTC value: write of %lx to EFI RTC failed\n",
		       __func__, nowtime);
		return -1;
	}

	status = efi.set_time(&eft);
	if (status != EFI_SUCCESS) {
		pr_err("Oops: efitime: can't write time!\n");
		return -1;
	}
	return 0;
}

void efi_get_time(struct timespec *now)
{
	efi_status_t status;
	efi_time_t eft;
	efi_time_cap_t cap;

	status = efi.get_time(&eft, &cap);
	if (status != EFI_SUCCESS) {
		pr_err("Oops: efitime: can't read time!\n");
		mach_get_cmos_time(now);
		return;
	}

	now->tv_sec = mktime(eft.year, eft.month, eft.day, eft.hour,
			     eft.minute, eft.second);
	now->tv_nsec = 0;
}

void __init efi_find_mirror(void)
{
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
		efi.get_time                 = xen_efi_get_time;
		efi.set_time                 = xen_efi_set_time;
		efi.get_wakeup_time          = xen_efi_get_wakeup_time;
		efi.set_wakeup_time          = xen_efi_set_wakeup_time;
		efi.get_variable             = xen_efi_get_variable;
		efi.get_next_variable        = xen_efi_get_next_variable;
		efi.set_variable             = xen_efi_set_variable;
		efi.get_next_high_mono_count = xen_efi_get_next_high_mono_count;
		efi.query_variable_info      = xen_efi_query_variable_info;
		efi.update_capsule           = xen_efi_update_capsule;
		efi.query_capsule_caps       = xen_efi_query_capsule_caps;

		__set_bit(EFI_BOOT, &efi.flags);
#ifdef CONFIG_64BIT
		__set_bit(EFI_64BIT, &efi.flags);
#endif
		__set_bit(EFI_SYSTEM_TABLES, &efi.flags);
		__set_bit(EFI_RUNTIME_SERVICES, &efi.flags);
		__set_bit(EFI_MEMMAP, &efi.flags);
	}
}

void __init efi_init(void)
{
	efi_char16_t c16[100];
	char vendor[ARRAY_SIZE(c16)] = "unknown";
	int ret, i;
	struct xen_platform_op op;
	union xenpf_efi_info *info = &op.u.firmware_info.u.efi_info;
	void *cfgtab;

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
	else if(__test_and_clear_bit(EFI_RUNTIME_SERVICES, &efi.flags))
		pr_warn("Could not get runtime services revision.\n");

	x86_platform.get_wallclock = efi_get_time;

	op.u.firmware_info.index = XEN_FW_EFI_CONFIG_TABLE;
	if (HYPERVISOR_platform_op(&op))
		BUG();

	cfgtab = early_ioremap(info->cfg.addr,
			       info->cfg.nent * sizeof(efi_config_table_64_t));
	if (!cfgtab) {
		pr_err("Couldn't map configuration table!\n");
		return;
	}
	efi_config_parse_tables(cfgtab, info->cfg.nent,
				sizeof(efi_config_table_64_t), arch_tables);
	early_iounmap(cfgtab, info->cfg.nent * sizeof(efi_config_table_64_t));

	efi_esrt_init();
}

#undef DECLARE_CALL
#undef call

void __init efi_late_init(void)
{
	efi_bgrt_init();
}

void __init efi_enter_virtual_mode(void)
{
	/* clean DUMMY object */
	efi_delete_dummy_variable();
}

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

	if (!efi_enabled(EFI_MEMMAP))
		return 0;

	op.cmd = XENPF_firmware_info;
	op.u.firmware_info.type = XEN_FW_EFI_INFO;
	op.u.firmware_info.index = XEN_FW_EFI_MEM_INFO;
	info->mem.addr = phys_addr;
	info->mem.size = 0;
	return HYPERVISOR_platform_op(&op) ? 0 : info->mem.attr;
}

static int __init arch_parse_efi_cmdline(char *str)
{
	if (!str) {
		pr_warn("need at least one option\n");
		return -EINVAL;
	}

	if (parse_option_str(str, "debug"))
		set_bit(EFI_DBG, &efi.flags);

	return 0;
}
early_param("efi", arch_parse_efi_cmdline);
