// SPDX-License-Identifier: GPL-2.0-only
/*
 *  (c) 2005-2016 Advanced Micro Devices, Inc.
 *
 *  Written by Jacob Shin - AMD, Inc.
 *  Maintained by: Borislav Petkov <bp@alien8.de>
 *
 *  All MC4_MISCi registers are shared between cores on a node.
 */


#ifdef CONFIG_X86_MCE_AMD
#include <asm/mce.h>

#if defined(HAVE_SMCA_GET_BANK_TYPE_WITH_TWO_ARGUMENTS)
enum smca_bank_types kcl_smca_get_bank_type(unsigned int cpu, unsigned int bank)
{
	return smca_get_bank_type(cpu, bank);
}
#elif defined(HAVE_SMCA_GET_BANK_TYPE_WITH_ONE_ARGUMENT)
enum smca_bank_types kcl_smca_get_bank_type(unsigned int cpu, unsigned int bank)
{
        return smca_get_bank_type(bank);
}

/* Copied from v5.15-rc2-452-gf38ce910d8df:arch/x86/kernel/cpu/mce/amd.c and modified for KCL */
#elif defined(HAVE_STRUCT_SMCA_BANK)
enum smca_bank_types smca_get_bank_type(unsigned int bank)
{
        struct smca_bank *b;

        if (bank >= MAX_NR_BANKS)
                return N_SMCA_BANK_TYPES;

        b = &smca_banks[bank];
        if (!b->hwid)
                return N_SMCA_BANK_TYPES;

        return b->hwid->bank_type;
}
enum smca_bank_types kcl_smca_get_bank_type(unsigned int cpu, unsigned int bank)
{
        return smca_get_bank_type(bank);
}

#else
int smca_get_bank_type(unsigned int bank)
{
	pr_warn_once("smca_get_bank_type is not supported\n");
	return 0;
}

int kcl_smca_get_bank_type(unsigned int cpu, unsigned int bank)
{
        return smca_get_bank_type(bank);
}

#endif 
EXPORT_SYMBOL_GPL(kcl_smca_get_bank_type);

#endif /* CONFIG_X86_MCE_AMD */
