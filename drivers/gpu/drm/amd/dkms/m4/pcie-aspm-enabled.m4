dnl #
dnl # v5.3-rc4-1-gaccd2dd72c8f
dnl # PCI/ASPM: Add pcie_aspm_enabled()
dnl #
AC_DEFUN([AC_AMDGPU_PCIE_ASPM_ENABLED], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <linux/pci.h>
		], [
			pcie_aspm_enabled(NULL);
		], [
			AC_DEFINE(HAVE_PCIE_ASPM_ENABLED, 1,
				[pcie_aspm_enabled() is available])
		])
	])
])
