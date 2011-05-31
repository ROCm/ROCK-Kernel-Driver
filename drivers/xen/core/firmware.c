#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/edd.h>
#include <video/edid.h>
#include <xen/interface/platform.h>
#include <asm/hypervisor.h>

#if defined(CONFIG_EDD) || defined(CONFIG_EDD_MODULE)
void __init copy_edd(void)
{
	int ret;
	struct xen_platform_op op;

	if (!is_initial_xendomain())
		return;

	op.cmd = XENPF_firmware_info;

	op.u.firmware_info.type = XEN_FW_DISK_INFO;
	for (op.u.firmware_info.index = 0;
	     edd.edd_info_nr < EDDMAXNR;
	     op.u.firmware_info.index++) {
		struct edd_info *info = edd.edd_info + edd.edd_info_nr;

		info->params.length = sizeof(info->params);
		set_xen_guest_handle(op.u.firmware_info.u.disk_info.edd_params,
				     &info->params);
		ret = HYPERVISOR_platform_op(&op);
		if (ret)
			break;

#define C(x) info->x = op.u.firmware_info.u.disk_info.x
		C(device);
		C(version);
		C(interface_support);
		C(legacy_max_cylinder);
		C(legacy_max_head);
		C(legacy_sectors_per_track);
#undef C

		edd.edd_info_nr++;
	}

	op.u.firmware_info.type = XEN_FW_DISK_MBR_SIGNATURE;
	for (op.u.firmware_info.index = 0;
	     edd.mbr_signature_nr < EDD_MBR_SIG_MAX;
	     op.u.firmware_info.index++) {
		ret = HYPERVISOR_platform_op(&op);
		if (ret)
			break;
		edd.mbr_signature[edd.mbr_signature_nr++] =
			op.u.firmware_info.u.disk_mbr_signature.mbr_signature;
	}
}
#endif

void __init copy_edid(void)
{
#if defined(CONFIG_FIRMWARE_EDID) && defined(CONFIG_X86)
	struct xen_platform_op op;

	if (!is_initial_xendomain())
		return;

	op.cmd = XENPF_firmware_info;
	op.u.firmware_info.index = 0;
	op.u.firmware_info.type = XEN_FW_VBEDDC_INFO;
	set_xen_guest_handle(op.u.firmware_info.u.vbeddc_info.edid,
			     edid_info.dummy);
	if (HYPERVISOR_platform_op(&op) != 0)
		memset(edid_info.dummy, 0x13, sizeof(edid_info.dummy));
#endif
}
