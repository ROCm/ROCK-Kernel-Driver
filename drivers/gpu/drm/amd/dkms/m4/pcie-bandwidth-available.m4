dnl #
dnl # commit 6db79a88c67e4679d9c1e4a3f05c6385e21f6e9a
dnl # PCI: Add pcie_bandwidth_available() to compute bandwidth available to device
dnl #
AC_DEFUN([AC_AMDGPU_PCIE_BANDWIDTH_AVAILABLE],
	[AC_MSG_CHECKING([whether pcie_bandwidth_available() is available])
	AC_KERNEL_TRY_COMPILE_SYMBOL([
		#include <linux/pci.h>
	], [
		pcie_bandwidth_available(NULL, NULL, NULL, NULL);
	], [pcie_bandwidth_available], [drivers/pci/pci.c], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_PCIE_BANDWIDTH_AVAILABLE, 1, [pcie_bandwidth_available() is available])
	], [
		AC_MSG_RESULT(no)
	])
])
