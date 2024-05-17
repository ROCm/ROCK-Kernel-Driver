/* SPDX-License-Identifier: GPL-2.0 */
#ifndef AMDKCL_MCE_H
#define AMDKCL_MCE_H

#ifdef CONFIG_X86_MCE_AMD

#include <asm/mce.h>

/* Copied from asm/mce.h */
#ifndef XEC
#define XEC(x, mask)			(((x) >> 16) & mask)
#endif

#ifndef HAVE_MCE_PRIO_UC
#define MCE_PRIO_UC  MCE_PRIO_SRAO
#endif

#if !defined(HAVE_SMCA_GET_BANK_TYPE_WITH_TWO_ARGUMENTS)
#if defined(HAVE_SMCA_GET_BANK_TYPE_WITH_ONE_ARGUMENT) || defined(HAVE_STRUCT_SMCA_BANK)
#if defined(HAVE_STRUCT_SMCA_BANK)
enum smca_bank_types smca_get_bank_type(unsigned int bank);
#endif
static inline
enum smca_bank_types _kcl_smca_get_bank_type(unsigned int cpu, unsigned int bank)
{
        return smca_get_bank_type(bank);
}
#else
int smca_get_bank_type(unsigned int bank);
static inline
int _kcl_smca_get_bank_type(unsigned int cpu, unsigned int bank)
{
        return smca_get_bank_type(bank);
}
#endif
#endif

#endif /* CONFIG_X86_MCE_AMD */
#endif
