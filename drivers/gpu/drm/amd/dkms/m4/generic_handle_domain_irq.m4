dnl #
dnl # v5.13-rc4-24-g8240ef50d486
dnl # genirq: Add generic_handle_domain_irq() helper
dnl #
AC_DEFUN([AC_AMDGPU_GENERIC_HANDLE_DOMAIN_IRQ], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <linux/irq.h>
		], [
			generic_handle_domain_irq(NULL, 0);
		], [
			AC_DEFINE(HAVE_GENERIC_HANDLE_DOMAIN_IRQ, 1,
				[generic_handle_domain_irq() is available])
		])
	])
])
