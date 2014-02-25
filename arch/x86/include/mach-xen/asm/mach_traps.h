/*
 *  include/asm-xen/asm-i386/mach-xen/mach_traps.h
 *
 *  Machine specific NMI handling for Xen
 */
#ifndef _MACH_TRAPS_H
#define _MACH_TRAPS_H

#include <linux/bitops.h>
#include <xen/interface/nmi.h>

#define NMI_REASON_SERR		0x80
#define NMI_REASON_IOCHK	0x40
#define NMI_REASON_MASK		(NMI_REASON_SERR | NMI_REASON_IOCHK)

static inline void clear_serr_error(unsigned char reason) {}
static inline void clear_io_check_error(unsigned char reason) {}

static inline unsigned char xen_get_nmi_reason(void)
{
	shared_info_t *s = HYPERVISOR_shared_info;
	unsigned char reason = 0;

	/* construct a value which looks like it came from
	 * port 0x61.
	 */
	if (test_bit(_XEN_NMIREASON_io_error, &s->arch.nmi_reason))
		reason |= NMI_REASON_IOCHK;
	if (test_bit(_XEN_NMIREASON_pci_serr, &s->arch.nmi_reason))
		reason |= NMI_REASON_SERR;

        return reason;
}

static inline void reassert_nmi(void) {}

#endif /* !_MACH_TRAPS_H */
