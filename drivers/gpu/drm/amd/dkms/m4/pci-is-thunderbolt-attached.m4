dnl #
dnl # commit 8531e283bee66050734fb0e89d53e85fd5ce24a4
dnl # PCI: Recognize Thunderbolt devices
dnl #
AC_DEFUN([AC_AMDGPU_PCI_IS_THUNDERBOLD_ATTACHED],
	[AC_MSG_CHECKING([whether pci_is_thunderbolt_attached() is available])
	AC_KERNEL_TRY_COMPILE([
		#include <linux/pci.h>
	], [
		struct pci_dev *pdev = NULL;

		pci_is_thunderbolt_attached(pdev);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_PCI_IS_THUNDERBOLD_ATTACHED, 1, [pci_is_thunderbolt_attached() is available])

	],[
		AC_MSG_RESULT(no)
	])
])
