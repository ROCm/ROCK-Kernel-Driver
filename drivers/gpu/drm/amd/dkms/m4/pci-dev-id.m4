dnl #
dnl # commit 4e544bac8267f65a0bf06aed1bde9964da4812ed
dnl # PCI: Add pci_dev_id() helper
dnl #
AC_DEFUN([AC_AMDGPU_PCI_DEV_ID], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <linux/pci.h>
		], [
			pci_dev_id(NULL);
		], [
			AC_DEFINE(HAVE_PCI_DEV_ID, 1,
				[pci_dev_id() is available])
		])
	])
])

dnl #
dnl # commit: v6.6-rc1-1-gd427da2323b0
dnl # PCI: Add pci_get_base_class() helper
dnl # 
AC_DEFUN([AC_AMDGPU_PCI_GET_BASE_CLASS], [
        AC_KERNEL_DO_BACKGROUND([
                AC_KERNEL_TRY_COMPILE_SYMBOL([
                        #include <linux/pci.h>
                ], [
                        pci_get_base_class(0, NULL);
                ], [pci_get_base_class], [drivers/pci/search.c], [
                        AC_DEFINE(HAVE_PCI_GET_BASE_CLASS, 1,
                                [pci_get_base_class() is available])
                ])
        ])
])

AC_DEFUN([AC_AMDGPU_PCI], [
		AC_AMDGPU_PCI_DEV_ID
		AC_AMDGPU_PCI_GET_BASE_CLASS
])
