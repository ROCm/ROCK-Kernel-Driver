/*
 * Copyright (c) 2004 Topspin Communications.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * $Id: mthca_profile.h 1349 2004-12-16 21:09:43Z roland $
 */

#ifndef MTHCA_PROFILE_H
#define MTHCA_PROFILE_H

#include "mthca_dev.h"
#include "mthca_cmd.h"

enum {
	MTHCA_RES_QP,
	MTHCA_RES_EEC,
	MTHCA_RES_SRQ,
	MTHCA_RES_CQ,
	MTHCA_RES_EQP,
	MTHCA_RES_EEEC,
	MTHCA_RES_EQ,
	MTHCA_RES_RDB,
	MTHCA_RES_MCG,
	MTHCA_RES_MPT,
	MTHCA_RES_MTT,
	MTHCA_RES_UAR,
	MTHCA_RES_UDAV,
	MTHCA_RES_NUM
};

int mthca_make_profile(struct mthca_dev *mdev,
		       struct mthca_dev_lim *dev_lim,
		       struct mthca_init_hca_param *init_hca);

#endif /* MTHCA_PROFILE_H */
