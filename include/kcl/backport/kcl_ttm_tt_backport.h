/* SPDX-License-Identifier: GPL-2.0 */
#ifndef AMDKCL_BACKPORT_KCL_TTM_TT_BACKPORT_H
#define AMDKCL_BACKPORT_KCL_TTM_TT_BACKPORT_H

#include <drm/ttm/ttm_tt.h>

#ifndef HAVE_TTM_SG_TT_INIT
#define amdttm_sg_tt_init ttm_dma_tt_init
#endif

#endif
