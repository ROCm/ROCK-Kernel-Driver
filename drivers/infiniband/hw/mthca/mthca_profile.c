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
 * $Id: mthca_profile.c 1349 2004-12-16 21:09:43Z roland $
 */

#include <linux/module.h>
#include <linux/moduleparam.h>

#include "mthca_profile.h"

static int default_profile[MTHCA_RES_NUM] = {
	[MTHCA_RES_QP]    = 1 << 16,
	[MTHCA_RES_EQP]   = 1 << 16,
	[MTHCA_RES_CQ]    = 1 << 16,
	[MTHCA_RES_EQ]    = 32,
	[MTHCA_RES_RDB]   = 1 << 18,
	[MTHCA_RES_MCG]   = 1 << 13,
	[MTHCA_RES_MPT]   = 1 << 17,
	[MTHCA_RES_MTT]   = 1 << 20,
	[MTHCA_RES_UDAV]  = 1 << 15
};

enum {
	MTHCA_RDB_ENTRY_SIZE = 32,
	MTHCA_MTT_SEG_SIZE   = 64
};

enum {
	MTHCA_NUM_PDS = 1 << 15
};

int mthca_make_profile(struct mthca_dev *dev,
		       struct mthca_dev_lim *dev_lim,
		       struct mthca_init_hca_param *init_hca)
{
	/* just use default profile for now */
	struct mthca_resource {
		u64 size;
		u64 start;
		int type;
		int num;
		int log_num;
	};

	u64 total_size = 0;
	struct mthca_resource *profile;
	struct mthca_resource tmp;
	int i, j;

	default_profile[MTHCA_RES_UAR] = dev_lim->uar_size / PAGE_SIZE;

	profile = kmalloc(MTHCA_RES_NUM * sizeof *profile, GFP_KERNEL);
	if (!profile)
		return -ENOMEM;

	profile[MTHCA_RES_QP].size   = dev_lim->qpc_entry_sz;
	profile[MTHCA_RES_EEC].size  = dev_lim->eec_entry_sz;
	profile[MTHCA_RES_SRQ].size  = dev_lim->srq_entry_sz;
	profile[MTHCA_RES_CQ].size   = dev_lim->cqc_entry_sz;
	profile[MTHCA_RES_EQP].size  = dev_lim->eqpc_entry_sz;
	profile[MTHCA_RES_EEEC].size = dev_lim->eeec_entry_sz;
	profile[MTHCA_RES_EQ].size   = dev_lim->eqc_entry_sz;
	profile[MTHCA_RES_RDB].size  = MTHCA_RDB_ENTRY_SIZE;
	profile[MTHCA_RES_MCG].size  = MTHCA_MGM_ENTRY_SIZE;
	profile[MTHCA_RES_MPT].size  = MTHCA_MPT_ENTRY_SIZE;
	profile[MTHCA_RES_MTT].size  = MTHCA_MTT_SEG_SIZE;
	profile[MTHCA_RES_UAR].size  = dev_lim->uar_scratch_entry_sz;
	profile[MTHCA_RES_UDAV].size = MTHCA_AV_SIZE;

	for (i = 0; i < MTHCA_RES_NUM; ++i) {
		profile[i].type     = i;
		profile[i].num      = default_profile[i];
		profile[i].log_num  = max(ffs(default_profile[i]) - 1, 0);
		profile[i].size    *= default_profile[i];
	}

	/*
	 * Sort the resources in decreasing order of size.  Since they
	 * all have sizes that are powers of 2, we'll be able to keep
	 * resources aligned to their size and pack them without gaps
	 * using the sorted order.
	 */
	for (i = MTHCA_RES_NUM; i > 0; --i)
		for (j = 1; j < i; ++j) {
			if (profile[j].size > profile[j - 1].size) {
				tmp            = profile[j];
				profile[j]     = profile[j - 1];
				profile[j - 1] = tmp;
			}
		}

	for (i = 0; i < MTHCA_RES_NUM; ++i) {
		if (profile[i].size) {
			profile[i].start = dev->ddr_start + total_size;
			total_size      += profile[i].size;
		}
		if (total_size > dev->fw.tavor.fw_start - dev->ddr_start) {
			mthca_err(dev, "Profile requires 0x%llx bytes; "
				  "won't fit between DDR start at 0x%016llx "
				  "and FW start at 0x%016llx.\n",
				  (unsigned long long) total_size,
				  (unsigned long long) dev->ddr_start,
				  (unsigned long long) dev->fw.tavor.fw_start);
			kfree(profile);
			return -ENOMEM;
		}

		if (profile[i].size)
			mthca_dbg(dev, "profile[%2d]--%2d/%2d @ 0x%16llx "
				  "(size 0x%8llx)\n",
				  i, profile[i].type, profile[i].log_num,
				  (unsigned long long) profile[i].start,
				  (unsigned long long) profile[i].size);
	}

	mthca_dbg(dev, "HCA memory: allocated %d KB/%d KB (%d KB free)\n",
		  (int) (total_size >> 10),
		  (int) ((dev->fw.tavor.fw_start - dev->ddr_start) >> 10),
		  (int) ((dev->fw.tavor.fw_start - dev->ddr_start - total_size) >> 10));

	for (i = 0; i < MTHCA_RES_NUM; ++i) {
		switch (profile[i].type) {
		case MTHCA_RES_QP:
			dev->limits.num_qps   = profile[i].num;
			init_hca->qpc_base    = profile[i].start;
			init_hca->log_num_qps = profile[i].log_num;
			break;
		case MTHCA_RES_EEC:
			dev->limits.num_eecs   = profile[i].num;
			init_hca->eec_base     = profile[i].start;
			init_hca->log_num_eecs = profile[i].log_num;
			break;
		case MTHCA_RES_SRQ:
			dev->limits.num_srqs   = profile[i].num;
			init_hca->srqc_base    = profile[i].start;
			init_hca->log_num_srqs = profile[i].log_num;
			break;
		case MTHCA_RES_CQ:
			dev->limits.num_cqs   = profile[i].num;
			init_hca->cqc_base    = profile[i].start;
			init_hca->log_num_cqs = profile[i].log_num;
			break;
		case MTHCA_RES_EQP:
			init_hca->eqpc_base = profile[i].start;
			break;
		case MTHCA_RES_EEEC:
			init_hca->eeec_base = profile[i].start;
			break;
		case MTHCA_RES_EQ:
			dev->limits.num_eqs   = profile[i].num;
			init_hca->eqc_base    = profile[i].start;
			init_hca->log_num_eqs = profile[i].log_num;
			break;
		case MTHCA_RES_RDB:
			dev->limits.num_rdbs = profile[i].num;
			init_hca->rdb_base   = profile[i].start;
			break;
		case MTHCA_RES_MCG:
			dev->limits.num_mgms      = profile[i].num >> 1;
			dev->limits.num_amgms     = profile[i].num >> 1;
			init_hca->mc_base         = profile[i].start;
			init_hca->log_mc_entry_sz = ffs(MTHCA_MGM_ENTRY_SIZE) - 1;
			init_hca->log_mc_table_sz = profile[i].log_num;
			init_hca->mc_hash_sz      = 1 << (profile[i].log_num - 1);
			break;
		case MTHCA_RES_MPT:
			dev->limits.num_mpts = profile[i].num;
			init_hca->mpt_base   = profile[i].start;
			init_hca->log_mpt_sz = profile[i].log_num;
			break;
		case MTHCA_RES_MTT:
			dev->limits.num_mtt_segs = profile[i].num;
			dev->limits.mtt_seg_size = MTHCA_MTT_SEG_SIZE;
			dev->mr_table.mtt_base   = profile[i].start;
			init_hca->mtt_base       = profile[i].start;
			init_hca->mtt_seg_sz     = ffs(MTHCA_MTT_SEG_SIZE) - 7;
			break;
		case MTHCA_RES_UAR:
			init_hca->uar_scratch_base = profile[i].start;
			break;
		case MTHCA_RES_UDAV:
			dev->av_table.ddr_av_base = profile[i].start;
			dev->av_table.num_ddr_avs = profile[i].num;
		default:
			break;
		}
	}

	/*
	 * PDs don't take any HCA memory, but we assign them as part
	 * of the HCA profile anyway.
	 */
	dev->limits.num_pds = MTHCA_NUM_PDS;

	kfree(profile);
	return 0;
}
