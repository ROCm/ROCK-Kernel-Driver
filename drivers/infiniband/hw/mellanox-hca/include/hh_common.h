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


#ifndef H_HH_COMMON_H
#define H_HH_COMMON_H

#include <vapi_types.h>

#define MAX_HCA_DEV_NUM 32


/*
 * Typedefs
 *
 */

typedef struct HH_hca_dev_st*  HH_hca_hndl_t;
typedef struct HHUL_sr_wqe_st  HHUL_sr_wqe_t;
typedef struct HHUL_rr_wqe_st  HHUL_rr_wqe_t;

typedef u_int32_t HH_pd_hndl_t;
typedef u_int32_t HH_rdd_hndl_t;
typedef MT_ulong_ptr_t HH_ud_av_hndl_t;
typedef u_int32_t HH_cq_hndl_t;
typedef u_int32_t HH_srq_hndl_t;

#define HH_INVAL_SRQ_HNDL 0xFFFFFFFF

typedef enum {PD_NO_FLAGS=0, PD_FOR_SQP=1} HH_pdm_pd_flags_t;


/*
 * Return error codes
 * First put all VAPI codes, then all codes not in VAPI
 *
 */
#if 0
 { /* left-brace for balance check */
#endif

#define HH_ERROR_LIST \
HH_ERROR_INFO(HH_OK,                 = VAPI_OK               , "Operation Succeeded")\
HH_ERROR_INFO(HH_ERR,                = VAPI_EGEN             , "General Layer Error")\
HH_ERROR_INFO(HH_EFATAL,             = VAPI_EFATAL           , "Fatal error") \
HH_ERROR_INFO(HH_EAGAIN,             = VAPI_EAGAIN           , "Not Enough Resources")\
HH_ERROR_INFO(HH_EINTR,              = VAPI_EINTR            , "Operation interrupted")\
HH_ERROR_INFO(HH_EBUSY,              = VAPI_EBUSY            , "Resource is busy/in-use")\
HH_ERROR_INFO(HH_EINVAL,             = VAPI_EINVAL_PARAM     , "Invalid parameter")\
HH_ERROR_INFO(HH_EINVAL_PD_HNDL,     = VAPI_EINVAL_PD_HNDL   , "Invalid PD handle")\
HH_ERROR_INFO(HH_EINVAL_AV_HNDL,     = VAPI_EINVAL_AV_HNDL   , "Invalid Address Vector handle")\
HH_ERROR_INFO(HH_EINVAL_QP_NUM,      = VAPI_EINVAL_QP_HNDL   , "Invalid Queue Pair Number")\
HH_ERROR_INFO(HH_EINVAL_SRQ_HNDL,    = VAPI_EINVAL_SRQ_HNDL  , "Invalid SRQ handle")\
HH_ERROR_INFO(HH_EINVAL_EEC_NUM,     = VAPI_EINVAL_EEC_HNDL  , "Invalid EE-Context Number")\
HH_ERROR_INFO(HH_EINVAL_CQ_HNDL,     = VAPI_EINVAL_CQ_HNDL   , "Invalid Completion Queue Handle")\
HH_ERROR_INFO(HH_EINVAL_QP_STATE,    = VAPI_EINVAL_QP_STATE  , "Invalid Queue Pair State")\
HH_ERROR_INFO(HH_EINVAL_HCA_ID,      = VAPI_EINVAL_HCA_ID    , "Wrong HCA ID")\
HH_ERROR_INFO(HH_EINVAL_CQ_NOT_TYPE, = VAPI_EINVAL_NOTIF_TYPE, "Invalid Completion Notification Type")\
HH_ERROR_INFO(HH_EINVAL_PARAM,       = VAPI_EINVAL_PARAM,      "Invalid Parameter")\
HH_ERROR_INFO(HH_EINVAL_HCA_HNDL,    = VAPI_EINVAL_HCA_HNDL  , "Bad HCA device Handle")\
HH_ERROR_INFO(HH_ENOSYS,             = VAPI_ENOSYS           , "Not Supported")\
HH_ERROR_INFO(HH_EINVAL_PORT,        = VAPI_EINVAL_PORT      , "Invalid Port Number")\
HH_ERROR_INFO(HH_EINVAL_OPCODE,      = VAPI_EINVAL_OP        , "Invalid Operation")\
HH_ERROR_INFO(HH_ENOMEM,             = VAPI_EAGAIN           , "Not Enough Memory")\
HH_ERROR_INFO(HH_E2BIG_SG_NUM,       = VAPI_E2BIG_SG_NUM     , "Max. SG size exceeds capabilities")\
HH_ERROR_INFO(HH_E2BIG_WR_NUM,       = VAPI_E2BIG_WR_NUM     , "Max. WR number exceeds capabilities")\
HH_ERROR_INFO(HH_EINVAL_WQE,         = VAPI_E2BIG_WR_NUM     , "Invalid WQE")\
HH_ERROR_INFO(HH_EINVAL_SG_NUM,      = VAPI_EINVAL_SG_NUM    , "Invalid scatter/gather list length") \
HH_ERROR_INFO(HH_EINVAL_SG_FMT,      = VAPI_EINVAL_SG_FMT    , "Invalid scatter/gather list format") \
HH_ERROR_INFO(HH_E2BIG_CQE_NUM,      = VAPI_E2BIG_CQ_NUM     , "CQE number exceeds CQ cap.") \
HH_ERROR_INFO(HH_CQ_EMPTY,           = VAPI_CQ_EMPTY	     , "CQ is empty")\
HH_ERROR_INFO(HH_EINVAL_VA,          = VAPI_EINVAL_VA	     , "Invalid virtual address")\
HH_ERROR_INFO(HH_EINVAL_MW,          = VAPI_EINVAL_MW_HNDL   , "Invalid memory window")\
HH_ERROR_INFO(HH_CQ_FULL,            = VAPI_EAGAIN           , "CQ is full")\
HH_ERROR_INFO(HH_EINVAL_MTU,         = VAPI_EINVAL_MTU       , "MTU violation")\
HH_ERROR_INFO(HH_2BIG_MCG_SIZE,      = VAPI_E2BIG_MCG_SIZE   ,"Number of QPs attached to multicast groups exceeded") \
HH_ERROR_INFO(HH_EINVAL_MCG_GID,     = VAPI_EINVAL_MCG_GID   ,"Invalid Multicast group GID") \
HH_ERROR_INFO(HH_EINVAL_SERVICE_TYPE,= VAPI_EINVAL_SERVICE_TYPE        , "Non supported transport service for QP.")\
HH_ERROR_INFO(HH_EINVAL_MIG_STATE,   = VAPI_EINVAL_MIG_STATE ,"Invalid Path Migration State") \
HH_ERROR_INFO(HH_ERROR_MIN,          = VAPI_ERROR_MAX        , "Dummy min error code : put all error codes after it")\
HH_ERROR_INFO(HH_NO_MCG,             EMPTY                   ,"No Multicast group was found") \
HH_ERROR_INFO(HH_MCG_FULL,           EMPTY                   ,"Multicast group is not empty") \
HH_ERROR_INFO(HH_MCG_EMPTY,          EMPTY                   ,"Multicast group is empty") \
HH_ERROR_INFO(HH_ENODEV,             EMPTY                   , "Unknown device")\
HH_ERROR_INFO(HH_DISCARD,            EMPTY                   ,"Data Discarded")\
HH_ERROR_INFO(HH_ERROR_MAX,          EMPTY                   ,"Dummy max error code : put all error codes before it")    
#if 0
 } /* right-brace for balance check */
#endif

enum
{
#define HH_ERROR_INFO(A, B, C) A B,
  HH_ERROR_LIST
#undef HH_ERROR_INFO
  HH_ERROR_DUMMY_CODE
}; 
typedef int32_t HH_ret_t;


extern const char*  HH_strerror(HH_ret_t errnum);
extern const char*  HH_strerror_sym(HH_ret_t errnum);


/************************************************************************
 ***                        Low level only                            ***
 ***/

typedef struct
{
  MT_virt_addr_t    addr;
  MT_virt_addr_t    size;
} HH_buff_entry_t;

#if 0
typedef void (*HH_async_event_t)(HH_hca_hndl_t, 
                                 HH_event_record_t *, 
                                 void* private_data);                      
typedef void (*HH_comp_event_t)(HH_hca_hndl_t, 
                                HH_cq_num_t, 
                                void* private_data);
#endif

#endif /* H_HH_COMMON_H */
