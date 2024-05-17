/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _KCL_BACKPORT_KCL_MCE_H
#define _KCL_BACKPORT_KCL_MCE_H

#include <kcl/kcl_mce.h>

#ifdef CONFIG_X86_MCE_AMD
#ifndef HAVE_SMCA_GET_BANK_TYPE_WITH_TWO_ARGUMENTS
#define smca_get_bank_type _kcl_smca_get_bank_type
#endif /* HAVE_SMCA_GET_BANK_TYPE_WITH_TWO_ARGUMENTS */
#endif /* CONFIG_X86_MCE_AMD */

#endif /* _KCL_BACKPORT_KCL_MCE_H */
