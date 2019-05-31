dnl #
dnl # commit 08a543ad33fc188650801bd36eed4ffe272643e1
dnl # Grant Likely <grant.likely@secretlab.ca>
dnl # Tue Jul 26 03:19:06 2011 -0600
dnl # irq: add irq_domain translation infrastructure
dnl #
AC_DEFUN([AC_AMDGPU_IRQ_DOMAIN], [
	AC_KERNEL_DO_BACKGROUND([
		AC_KERNEL_TRY_COMPILE([
			#include <linux/irqdomain.h>
		], [
			struct irq_domain *domain = NULL;
			irq_domain_add_linear(NULL, 0, NULL, NULL);
			irq_domain_remove(domain);
			irq_create_mapping(domain, 0);
			irq_find_mapping(domain, 0);
		],[
			AC_DEFINE(HAVE_IRQ_DOMAIN, 1,
				[IRQ translation domains exist])
		])
	])
])
