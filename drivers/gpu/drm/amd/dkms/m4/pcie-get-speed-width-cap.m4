dnl #
dnl # commit 576c7218a1546e0153480b208b125509cec71470
dnl # PCI: Export pcie_get_speed_cap and pcie_get_width_cap
dnl #
AC_DEFUN([AC_AMDGPU_PCIE_GET_SPEED_AND_WIDTH_CAP],
	[AC_MSG_CHECKING([whether pcie_get_speed_cap() and pcie_get_width_cap() exist])
	AC_KERNEL_CHECK_SYMBOL_EXPORT([pcie_get_speed_cap pcie_get_width_cap], [drivers/pci/pci.c], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_PCIE_GET_SPEED_AND_WIDTH_CAP, 1, [pcie_get_speed_cap() and pcie_get_width_cap() exist])
	], [
		AC_MSG_RESULT(no)
	])
])
