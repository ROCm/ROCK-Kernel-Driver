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
 
 
/* DEFINES WHICH MAY BE USED TO CHANGE THE NUMBER AND PROPORTIONS */
/* OF ALLOCATED RESOURCES */

#ifndef H_THH_REQUESTED_PROFILE_H
#define H_THH_REQUESTED_PROFILE_H


#if 1
    /*standard default profile.  Subtracts out the reserved resources from a power-of-2 */
    /*These values are used in THH_calculate_profile, in file thh_hob.c */
    /*The reserved resources are expressed in internal terms to guard against incompatibilities. */
#define THH_PROF_MAX_QPS              ((1<<16) - ((1<<hob->dev_lims.log2_rsvd_qps) + THH_NUM_RSVD_QP))
#define THH_PROF_MAX_CQS              ((1<<14) - (1<<hob->dev_lims.log2_rsvd_cqs))
#define THH_PROF_MAX_PDS              ((1<<14) - THH_NUM_RSVD_PD)
#define THH_PROF_MAX_REGIONS          ((1<<17) - (1 << hob->dev_lims.log2_rsvd_mtts))
#define THH_PROF_MAX_WINDOWS          ((1<<18) - (1 << hob->dev_lims.log2_rsvd_mrws))

#define THH_PROF_MIN_QPS              ((1<<14) - ((1<<hob->dev_lims.log2_rsvd_qps) + THH_NUM_RSVD_QP))
#define THH_PROF_MIN_CQS              ((1<<12) - (1<<hob->dev_lims.log2_rsvd_cqs))
#define THH_PROF_MIN_PDS              ((1<<12) - THH_NUM_RSVD_PD)
#define THH_PROF_MIN_REGIONS          ((1<<15) - (1 << hob->dev_lims.log2_rsvd_mtts))
#define THH_PROF_MIN_WINDOWS          ((1<<16) - (1 << hob->dev_lims.log2_rsvd_mrws))

#else
    /* profile which will enable a maximum of
     * 1 million QPs, when the Tavor on-board memory
     * is 1 Gigabyte, at the expense of fewer CQs,
     * memory regions, and memory windows .  To activate
     * this profile, change the "if 1" above to "if 0"
     * and recompile and reinstall the driver
     */
     
#define THH_PROF_MAX_QPS              ((1<<20) - 24)
#define THH_PROF_MAX_CQS              ((1<<18) - 128)
#define THH_PROF_MAX_PDS              ((1<<18) - 2)
#define THH_PROF_MAX_REGIONS          ((1<<18) - 16)
#define THH_PROF_MAX_WINDOWS          ((1<<19) - 16)

#define THH_PROF_MIN_QPS              ((1<<14) - 24)
#define THH_PROF_MIN_CQS              ((1<<17) - 128)
#define THH_PROF_MIN_PDS              ((1<<12) - 2)
#define THH_PROF_MIN_REGIONS          ((1<<18) - 16)
#define THH_PROF_MIN_WINDOWS          ((1<<19) - 16)

#endif


#define THH_PROF_PCNT_REDUCTION_QPS       (50)
#define THH_PROF_PCNT_REDUCTION_CQS       (50)
#define THH_PROF_PCNT_REDUCTION_PDS       (50)
#define THH_PROF_PCNT_REDUCTION_REGIONS   (50)
#define THH_PROF_PCNT_REDUCTION_WINDOWS   (50)

#endif
