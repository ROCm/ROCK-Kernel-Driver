/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef AMDKCL_IRQDESC_H
#define AMDKCL_IRQDESC_H

#ifndef HAVE_GENERIC_HANDLE_DOMAIN_IRQ
int kcl_generic_handle_domain_irq(struct irq_domain *domain, unsigned int hwirq);
#define generic_handle_domain_irq kcl_generic_handle_domain_irq
#endif /* HAVE_GENERIC_HANDLE_DOMAIN_IRQ */

#endif
