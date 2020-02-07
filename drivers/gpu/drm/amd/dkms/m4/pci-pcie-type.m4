dnl #
dnl # commit 786e22885d9959fda0473ace5a61cb11620fba9b
dnl # Author: Yijing Wang <wangyijing@huawei.com>
dnl # Date:   Tue Jul 24 17:20:02 2012 +0800
dnl # PCI: Add pcie_flags_reg to cache PCIe capabilities register
dnl #
AC_DEFUN([AC_AMDGPU_PCI_PCIE_TYPE], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <linux/pci.h>
		], [
			pci_pcie_type(NULL);
		], [
			AC_DEFINE(HAVE_PCI_PCIE_TYPE, 1, [pci_pcie_type() exist])
		])
	])
])
