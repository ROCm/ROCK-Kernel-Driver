/*
  This software is available to you under a choice of one of two
  licenses.  You may choose to be licensed under the terms of the GNU
  General Public License (GPL) Version 2, available at
  <http://www.fsf.org/copyleft/gpl.html>, or the OpenIB.org BSD
  license, available in the LICENSE.TXT file accompanying this
  software.  These details are also available at
  <http://openib.org/license.html>.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
  NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
  BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
  ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
  CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.

  Copyright (c) 2004 Mellanox Technologies Ltd.  All rights reserved.
*/

#ifndef H_THH_DEFAULT_PROFILE_H
#define H_THH_DEFAULT_PROFILE_H

#include <thh_requested_profile.h>

/* WQE IN DDR DEFINES */
/* CHANGE THE DEFINE BELOW TO BE DEFINED TO ZERO TO DISABLE WQEs IN DDR */
#define THH_LOG2_WQE_DDR_SPACE_PER_QP    0        /* was 4096 */

#define THH_DDR_LOG2_MTT_ENTRIES_PER_SEG  (3)
#define THH_DDR_LOG2_MTT_SEGS_PER_REGION  (1)

#define THH_DDR_LOG2_INFLIGHT_RDMA_PER_QP  (3)
#define THH_DDR_LOG2_MIN_QP_PER_MCG  (3)        /* minimum QPs per MCG.  May be increased by calculations */
#define THH_DDR_LOG2_MAX_MCG         (13)       /* log2 max MCG entries */
#define THH_DDR_LOG2_MCG_HASH_PROPORTION (-1)   /* log2 of proportion of MCG entries in mcg hash table*/
#define THH_DDR_LOG2_MAX_EQ          (6)
#define THH_DDR_MAX_PRIV_UDAVS       (1<<16)
#define THH_USE_PRIV_UDAV            (FALSE)
#define THH_MAX_ASYNC_EQ_SIZE        (1<<14)    /* max number of outstanding async events */

typedef struct THH_profile_input_st {
    u_int32_t   max_qps;       /* max number of QPs to configure */
    u_int32_t   max_cqs;
    u_int32_t   max_pds;
    u_int32_t   max_regions;
    u_int32_t   max_windows;

    u_int32_t   min_qps;       /* min number of QPs to configure */
    u_int32_t   min_cqs;
    u_int32_t   min_pds;
    u_int32_t   min_regions;
    u_int32_t   min_windows;

    u_int32_t   reduction_pct_qps;  /* percent by which to reduce QPs if need reduction */
    u_int32_t   reduction_pct_cqs;  /* percent by which to reduce CQs if need reduction */
    u_int32_t   reduction_pct_pds;  /* percent by which to reduce PDs if need reduction */
    u_int32_t   reduction_pct_regions;
    u_int32_t   reduction_pct_windows;

    u_int32_t   log2_max_eq;
    u_int32_t   log2_mtt_entries_per_seg;
    u_int32_t   log2_mtt_segs_per_region;
    u_int32_t   log2_inflight_rdma_per_qp;
    u_int32_t   log2_max_mcg;
    u_int32_t   log2_min_qp_per_mcg;
    int         log2_mcg_hash_proportion;
    u_int32_t   max_priv_udavs;
    MT_bool     use_priv_udav;
    u_int32_t   log2_wqe_ddr_space_per_qp;
} THH_profile_input_t;

/* NOTE:  In the case of NON-privileged UDAV, we need one internal region per allocated PD.  The number of PDs  */
/*        by default is #QPs/4.  This means that the number of internal regions in the MPT is not properly calculated. */
/*        However, there is a problem in that the MTT segment size MUST be a power of 2 (so that MTT entry addresses */
/*        are composed of a segment address and an entry offset in the segment).  Using a segment size of 16 requires */
/*        reducing the number of supported QPs.  For now, we are ignoring this issue, since users will mostly run */
/*        in UDAV protected mode */

/* INIT_IB: No provision for overriding GUID0 on the chip is provided for now.  some customers may wish to override the 
 * default GUIDs burned into the chip.  A define will not do the job, since each chip in a network 
 * must have a different GUID0.  When we provide default-override capability, we need to think about allowing
 * the administrator of a network to specify GUIDs per card on a host */



/* DEFINES WHICH SHOULD REALLY COME FROM THE FIRMWARE. */

#define THH_DDR_LOG2_SEG_SIZE_PER_REGION (3)
#define THH_DDR_MCG_ENTRY_SIZE       (64)
#define THH_DDR_MCG_BYTES_PER_QP     (4)
#define THH_DDR_MCG_ENTRY_HEADER_SIZE (32)
#define THH_DDR_LOG2_RDB_ENTRY_SIZE  (5)
#define THH_DDR_LOG2_EQC_ENTRY_SIZE  (ceil_log2(hob->dev_lims.eqc_entry_sz))
#define THH_DDR_LOG2_EEC_ENTRY_SIZE  (ceil_log2(hob->dev_lims.eec_entry_sz))
#define THH_DDR_LOG2_EEEC_ENTRY_SIZE  (ceil_log2(hob->dev_lims.eeec_entry_sz))
#define THH_DDR_LOG2_QPC_ENTRY_SIZE  (ceil_log2(hob->dev_lims.qpc_entry_sz))
#define THH_DDR_LOG2_EQPC_ENTRY_SIZE  (ceil_log2(hob->dev_lims.eqpc_entry_sz))
#define THH_DDR_LOG2_SRQC_ENTRY_SIZE  (ceil_log2(hob->dev_lims.srq_entry_sz))
#define THH_DDR_LOG2_MTT_ENTRY_SIZE  (3)
#define THH_DDR_LOG2_MIN_MTT_SEG_SIZE (6)
#define THH_DDR_LOG2_MPT_ENTRY_SIZE  (6)
#define THH_DDR_LOG2_CQC_ENTRY_SIZE  (ceil_log2(hob->dev_lims.cqc_entry_sz))
#define THH_DDR_LOG2_UAR_SCR_ENTRY_SIZE  (ceil_log2(hob->dev_lims.uar_scratch_entry_sz))
#define THH_DDR_ADDR_VEC_SIZE            (32)

/* END firmware defines */


#endif
