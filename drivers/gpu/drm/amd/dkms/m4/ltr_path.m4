dnl #
dnl # commit c46fd358070f22ba68d6e74c22016a33b914c20a
dnl # PCI/ASPM: Enable Latency Tolerance Reporting when supported
dnl #
dnl #
AC_DEFUN([AC_AMDGPU_PCI_DEV_LTR_PATH], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <linux/pci.h>
		], [
			struct pci_dev *dev = NULL;
			dev->ltr_path = 0;
		], [
			AC_DEFINE(HAVE_PCI_DEV_LTR_PATH, 1,
				[strurct pci_dev->ltr_path is available])
		])
	])
])
