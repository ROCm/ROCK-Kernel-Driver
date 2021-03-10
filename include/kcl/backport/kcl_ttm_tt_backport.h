/* SPDX-License-Identifier: GPL-2.0 */
#ifndef AMDKCL_BACKPORT_KCL_TTM_TT_BACKPORT_H
#define AMDKCL_BACKPORT_KCL_TTM_TT_BACKPORT_H

#include <drm/ttm/ttm_tt.h>

#ifndef HAVE_TTM_SG_TT_INIT
#define amdttm_sg_tt_init ttm_dma_tt_init
#endif

/*
 * adapt to kernel 5.11 ttm structure changes
 * this kcl patch only exists in dkms-5.9 branch, don't
 * be promoted to dkms-5.11 branch
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 11, 0)
#define HAVE_TTM_BUS_PLACEMENT_CACHING
#endif


#endif
