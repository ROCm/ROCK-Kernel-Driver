/* SPDX-License-Identifier: GPL-2.0 */
#ifndef AMDKCL_HYPERVISOR_H
#define AMDKCL_HYPERVISOR_H

#include <asm/hypervisor.h>

#ifdef CONFIG_X86
#if !defined(HAVE_X86_HYPERVISOR_TYPE)
enum x86_hypervisor_type {
	X86_HYPER_NATIVE = 0,
	X86_HYPER_VMWARE,
	X86_HYPER_MS_HYPERV,
	X86_HYPER_XEN_PV,
	X86_HYPER_XEN_HVM,
	X86_HYPER_KVM,
	X86_HYPER_JAILHOUSE,
	X86_HYPER_ACRN,
};
#endif

#ifndef HAVE_HYPERVISOR_IS_TYPE
static inline bool hypervisor_is_type(enum x86_hypervisor_type type)
{
	return false;
}
#endif

#endif /* CONFIG_X86 */
#endif /* AMDKCL_HYPERVISOR_H */
