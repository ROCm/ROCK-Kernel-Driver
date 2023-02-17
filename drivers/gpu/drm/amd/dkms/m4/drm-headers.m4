AC_DEFUN([AC_AMDGPU_DRM_HEADERS], [
	dnl #
	dnl # RHEL 7.x wrapper
	dnl #
	AC_KERNEL_CHECK_HEADERS([drm/drm_backport.h])

	dnl #
	dnl # Optional devices ID for amdgpu driver
	dnl #
	AC_KERNEL_CHECK_HEADERS([drm/amdgpu_pciid.h])

	dnl #
	dnl # commit v4.15-rc4-1351-g495eb7f877ab
	dnl # drm: Add some HDCP related #defines
	dnl #
	AC_KERNEL_CHECK_HEADERS([drm/drm_hdcp.h])

	dnl #
	dnl # commit v5.0-rc1-342-gfcd70cd36b9b
	dnl # drm: Split out drm_probe_helper.h
	dnl #
	AC_KERNEL_CHECK_HEADERS([drm/drm_probe_helper.h])

	dnl #
	dnl # v5.4-rc1-214-g4e98f871bcff
	dnl # drm: delete drmP.h + drm_os_linux.h
	dnl #
	AC_KERNEL_CHECK_HEADERS([drm/drmP.h])

	dnl #
	dnl # commit v5.5-rc2-783-g368fd0aad1be
	dnl # drm: Add Reusable task barrier.
	dnl #
	AC_KERNEL_CHECK_HEADERS([drm/task_barrier.h])

	dnl #
	dnl # v5.6-rc5-1258-gc6603c740e0e
	dnl # drm: add managed resources tied to drm_device
	dnl #
	AC_KERNEL_CHECK_HEADERS([drm/drm_managed.h])

	dnl #
	dnl # Required by AC_KERNEL_SUPPORTED_AMD_CHIPS macro
	dnl #
	AC_KERNEL_CHECK_HEADERS([drm/amd_asic_type.h])

	dnl #
	dnl # v5.12-rc3-330-g2916059147ea
	dnl # drm/aperture: Add infrastructure for aperture ownership
	dnl #
	AC_KERNEL_CHECK_HEADERS([drm/drm_aperture.h])

	dnl #
	dnl # v5.16-rc5-872-g5b529e8d9c38
	dnl # drm/dp: Move public DisplayPort headers into dp/
	dnl #
	AC_KERNEL_CHECK_HEADERS([drm/dp/drm_dp_helper.h])

	dnl #
	dnl # v5.16-rc5-872-g5b529e8d9c38
	dnl # drm/dp: Move public DisplayPort headers into dp/
	dnl #
	AC_KERNEL_CHECK_HEADERS([drm/dp/drm_dp_mst_helper.h])
	
	dnl #
	dnl # v5.18-rc2-594-gda68386d9edb
	dnl # drm: Rename dp/ to display/
	dnl #
	AC_KERNEL_CHECK_HEADERS([drm/display/drm_dp_helper.h])

	dnl #
	dnl # v5.18-rc2-594-gda68386d9edb
	dnl # drm: Rename dp/ to display/
	dnl #
	AC_KERNEL_CHECK_HEADERS([drm/display/drm_dp_mst_helper.h])

	AC_KERNEL_CHECK_HEADERS([drm/display/drm_dsc.h])
	AC_KERNEL_CHECK_HEADERS([drm/display/drm_dsc_helper.h])
	AC_KERNEL_CHECK_HEADERS([drm/display/drm_hdmi_helper.h])
	AC_KERNEL_CHECK_HEADERS([drm/display/drm_hdcp_helper.h])
	AC_KERNEL_CHECK_HEADERS([drm/display/drm_hdcp.h])
	AC_KERNEL_CHECK_HEADERS([drm/display/drm_dp.h])

	AC_KERNEL_CHECK_HEADERS([drm/drm_dsc.h])

	dnl #
	dnl # v5.7-13141-gca5999fde0a1
	dnl # mm: introduce include/linux/pgtable.h
	dnl #
	AC_KERNEL_CHECK_HEADERS([linux/pgtable.h])

	dnl #
	dnl # v5.19-rc1- c9cad937c0
	dnl # drm/amdgpu: add drm buddy support to amdgpu
	dnl #
	AC_KERNEL_CHECK_HEADERS([drm/drm_buddy.h])

        dnl #
        dnl # v6.1-rc2-542-g8ab59da26bc0
        dnl # drm/fb-helper: Move generic fbdev emulation into separate source file
        dnl #
        AC_KERNEL_CHECK_HEADERS([drm/drm_fbdev_generic.h])

])
