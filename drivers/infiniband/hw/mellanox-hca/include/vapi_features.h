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
#ifndef H_VAPI_FEATURES_H
#define H_VAPI_FEATURES_H

/* VAPI_ features macros */
#define VAPI_FEATURE_APM          /* Automatic Path migration support */
#define VAPI_FEATURE_ETIMEOUT     /* defined new return code VAPI_ETIMEOUT */
#define VAPI_FEATURE_RESIZE_CQ
#define VAPI_FEATURE_RESOURCE_TRACKING  /* User level resource tracking supported */
#define VAPI_FEATURE_SMR_VALIDATION
#define VAPI_FEATURE_ALT_RETRY_OBSOLETE  /* retry count and rnr_retry are per QP, not per path */
#define VAPI_FEATURE_DESTR_QP_FAIL_IF_MCG /* destroy QP fails if QP is attached to a mcg */
#define VAPI_FEATURE_SRQ                /* SRQ (Shared Receive Queue) support */
#define VAPI_FEATURE_MODIFY_SRQ         /* VAPI_modify_srq supported */
#define VAPI_FEATURE_CQE_WITH_QP        /* QP number included in VAPI_wc_desc_t */
/* to be enabled when SQ Draining is entirely fixed*/
/* #define VAPI_FEATURE_SQD */

/* EVAPI features macros */
#define EVAPI_FEATURE_DP_HNDL_CHK    /* Data path handles validation */
#define EVAPI_FEATURE_FMR         /* Fast Memory Regions support */
#define EVAPI_FEATURE_INLINE_SR   /* Inline send */
#define EVAPI_FEATURE_CQBLK       /* EVAPI_poll_cq_block() */
#define EVAPI_FEATURE_PEEK_CQ     /* EVAPI_peek_cq() */
#define EVAPI_FEATURE_REQ_NCOMP_NOTIF  /* EVAPI_req_ncomp_notif() */
#define EVAPI_FEATURE_ALLOC_PD_AV  /* EVAPI_alloc_pd() */
#define EVAPI_FEATURE_PROC_MAD_OPTS /*change in EVAPI_process_local_mad() arglist */
#define EVAPI_FEATURE_APM
#define EVAPI_FEATURE_DEVMEM
#define EVAPI_FEATURE_DEVMEM2
#define EVAPI_FEATURE_LOCAL_MAD_BAD_PARAM
#define EVAPI_FEATURE_OPEN_CLOSE_HCA
#define EVAPI_FEATURE_ASYNC_EVENTH
#define EVAPI_FEATURE_LOCAL_MAD_SLID
#define EVAPI_FEATURE_USER_PROFILE  /* user profile.  Affects EVAPI_open_hca() arlist*/
#define EVAPI_FEATURE_VENDOR_ERR_SYNDROME /* EVAPI_vendor_err_syndrome_t in VAPI_wc_desc_t */
#define EVAPI_FEATURE_GSI_SEND_PKEY      /* EVAPI_post_gsi_sr with Pkey parameter */
#define EVAPI_FEATURE_ALLOC_PD_AV_SQP  /* EVAPI_alloc_pd_sqp() */
#define EVAPI_FEATURE_FORK_SUPPORT /* support fork in multithreaded apps */

/* Fixed bugs (FlowManager issue numbers) */
#define BUG_FIX_FM12831
#define BUG_FIX_FM12549
#define BUG_FIX_FM16939

#endif 
