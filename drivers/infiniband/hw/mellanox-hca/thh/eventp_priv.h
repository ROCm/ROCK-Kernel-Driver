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


#ifndef H_EVENTP_PRIV_H
#define H_EVENTP_PRIV_H



#include <mosal.h>
#include <eventp.h>
#include <mtl_common.h>
#include <vapi_types.h>
#include <hh.h>
#include <thh.h>

/*================ macro definitions ===============================================*/
#define EQP_MAX_EQS 32 /* this if for now since we only use 3 EQs and its save us reading two ECR registers */
#define EQP_ECR_USED 1 /* should be changed to 2 when > 32 EQs are used */
#define EQP_CATAS_ERR_EQN 4 /* this is reserved only for catastrophic error notification */
#define EQP_CMD_IF_EQN 1 /* this is reserved only for command interface */
#define EQP_CQ_COMP_EQN 0 /* this is reserved only for CQ completion events */
#define EQP_ASYNCH_EQN 3 /* this is reserved only for asynch events */
#define EQE_HW_OWNER 0x80
#define EQP_MIN_EQ_NUM  (EQP_CATAS_ERR_EQN+1) /* make sure we start only after reserved EQs */
#define EQE_SW_OWNER 0x00
#define EQE_OWNER_BYTE_OFFSET 31

#define EQE_DATA_BYTE_SIZE   (MT_BYTE_SIZE(tavorprm_event_queue_entry_st, event_data))  /* in bytes */
#define EQE_OWNER_OFFSET (MT_BIT_OFFSET(tavorprm_event_queue_entry_st, owner) /32 ) /* in DWORDS relay on owner field to be first in DWORD*/
#define EQE_EVENT_TYPE_OFFSET (MT_BIT_OFFSET(tavorprm_event_queue_entry_st, event_type) /32 ) /* in DWORDS relay on owner field to be first in DWORD*/
#define EQE_DATA_OFFSET (MT_BIT_OFFSET(tavorprm_event_queue_entry_st, event_data) /32 ) /* in DWORDS relay on owner field to be first in DWORD*/
#define EQE_DWORD_SIZE         (sizeof(struct tavorprm_event_queue_entry_st)/32)  /* in DWORDS */
#define EQ_ENTRY_SIZE    (sizeof(struct tavorprm_event_queue_entry_st) / 8) /* in bytes */


/* Values to put in eq_buff_entry_num to mark entry state. If not one of those - entry is valid */
#define SET_EQ_INIT(eventp,eq_num)  (eventp->eq_table[eq_num].res_state= EQP_EQ_INIT)
#define SET_EQ_VALID(eventp,eq_num) (eventp->eq_table[eq_num].res_state= EQP_EQ_VALID)
#define SET_EQ_CLEANUP(eventp,eq_num) (eventp->eq_table[eq_num].res_state= EQP_EQ_CLEANUP)
#define SET_EQ_FREE(eventp,eq_num)  (eventp->eq_table[eq_num].res_state= EQP_EQ_FREE)
/* Macros to test if entry is invalid/free */
#define IS_EQ_FREE(eventp,eq_num) (eventp->eq_table[eq_num].res_state == EQP_EQ_FREE)
#define IS_EQ_VALID(eventp,eq_num) (eventp->eq_table[eq_num].res_state == EQP_EQ_VALID)
#define IS_EQ_VALID_P(eq_p) (eq_p->res_state == EQP_EQ_VALID)


/*================ type definitions ================================================*/

typedef enum {
  EQP_CQ_COMP_EVENT,
  EQP_IB_EVENT,
  EQP_CMD_IF_EVENT,
  EQP_MLX_EVENT,
  EQP_CATAS_ERR_EVENT
}EQ_type_t;

/* States of the EQ resources (for EQs pool management) */
typedef enum {
  EQP_EQ_FREE,
  EQP_EQ_INIT,
  EQP_EQ_VALID,
  EQP_EQ_CLEANUP
}EQ_resource_state_t;

/* entry saved for each EQ */
typedef struct EQP_eq_entry_st {
  EQ_resource_state_t    res_state;       /* EQ resource (entry) state */
#ifdef EQS_CMD_IN_DDR
  MT_phys_addr_t         alloc_mem_addr_p; /* need to save the allocated pointer for free at destroy */
#else
  MT_virt_addr_t         alloc_mem_addr_p; /* need to save the allocated pointer for free at destroy */
#endif  
  VAPI_size_t            alloc_mem_bytes_size;
  void*                  eq_buff;
  VAPI_size_t            eq_buff_entry_num; /* if ==0 entry is invalid */
  VAPI_pd_hndl_t         pd;
  VAPI_lkey_t            mem_lkey;
  void                   *priv_context;
  THH_eventp_mtmask_t    events_mask;
  EQ_type_t              eq_type;
  THH_eqn_t              eqn;          /* eq number - needed by DPC */
  THH_eventp_handler_t   handler;
  THH_eventp_t           eventp_p;
  MOSAL_DPC_t            polling_dpc;
  volatile u_int32_t      dpc_cntr;    /* Outstanding DPCs counter (possible values:0,1,2)*/
  u_int32_t              cons_indx;
  MT_virt_addr_t         clr_ecr_addr;
  u_int32_t              clr_ecr_mask;
  MT_bool                virtual_eq;
#ifdef IMPROVE_EVENT_HANDLING
	u_int32_t						dpc_lock;
#else
  MOSAL_spinlock_t       dpc_lock;       /* ensure that only one DPC is running - WINDOWS allows 
                                            more than one DPC to run at the same time */
#endif                                            	
  MOSAL_spinlock_t       state_lock;     /* protect on the eq state - 
                                            must be used for every access to this entry */
}EQP_eq_entry_t;


/* The main EVENT processor structure */
typedef struct THH_eventp_st {
  THH_hob_t hob;
  THH_ver_info_t version; 
  THH_eventp_res_t event_resources; 
  THH_cmd_t cmd_if; 
  THH_uar_t kar; 
  THH_uar_index_t kar_index;
  THH_mrwm_t mrwm_internal;
  THH_ddrmm_t ddrmm;
  EQP_eq_entry_t eq_table[EQP_MAX_EQS]; /* static table of eqs */
  MOSAL_mutex_t    mtx;     /* used internally */
  MOSAL_protection_ctx_t ctx_internal;
  MOSAL_ISR_t            isr_obj;
  HH_hca_hndl_t          hh_hca_hndl;
  volatile MT_virt_addr_t   ecr_h_base;
  volatile MT_virt_addr_t   ecr_l_base;
  volatile MT_virt_addr_t   clr_ecr_h_base;
  volatile MT_virt_addr_t   clr_ecr_l_base;
  volatile MT_virt_addr_t   intr_clr_reg;   /* can be the high or the low but not both */
  u_int32_t                 intr_clr_mask;
  volatile u_int8_t         max_eq_num_used;
  volatile MT_bool          have_fatal;
  volatile THH_fatal_err_t  fatal_type; /* need to distingush between master abort and catast error */
  MOSAL_DPC_t               fatal_error_dpc;
}THH_eventp_internal_t;


/*================ external functions ================================================*/



#endif /* H_EVENTP_PRIV_H  */
