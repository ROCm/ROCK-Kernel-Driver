dnl #
dnl # v4.7-rc6-10-gaff171641d18
dnl # PCI: Provide sensible IRQ vector alloc/free routines
dnl #
AC_DEFUN([AC_AMDGPU_PCI_IRQ_VECTOR], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <linux/pci.h>
		], [
			pci_irq_vector(NULL, 0);
		], [
			AC_DEFINE(HAVE_PCI_IRQ_VECTOR, 1,
				[pci_irq_vector() is available])
		])
	])
])
