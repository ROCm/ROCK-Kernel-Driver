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


#include <cmdif.h>
#include <mosal.h>
#include <eventp_priv.h>
#include <uar.h>
#include <thh_hob.h>
#include <vapi_common.h>

#include <cr_types.h>
#include <MT23108.h>

//#define MTPERF
#include <mtperf.h>

#ifdef THH_CMD_TIME_TRACK
u_int64_t THH_eventp_last_cmdif_interrupt;
#endif

/*================ macro definitions ===============================================*/

/*================ type definitions ================================================*/


typedef struct eq_entry_st {
  u_int8_t event_type;
  tavor_if_port_event_subtype_t event_sub_type;
  u_int32_t event_data[6];      /* Delivers auxiliary data to handle event. - 24 bytes */
}eq_entry_t;


/*================ global variables definitions ====================================*/

MTPERF_NEW_SEGMENT(interupt_segment,100000);
MTPERF_NEW_SEGMENT(inter2dpc_segment,100000);
MTPERF_NEW_SEGMENT(dpc_segment,100000);
MTPERF_NEW_SEGMENT(part_of_DPC_segment,100000);

/*================ static functions prototypes =====================================*/

inline static MT_bool read_eq_entry(u_int32_t  *buff_p,
                          EQ_type_t  eq_type,
                          eq_entry_t *eqe_p);



inline static HH_ret_t move_eqe_to_hw(EQP_eq_entry_t *eqe_p, 
                               u_int32_t      *eqe_buff_p);     /* pointer to start of EQE buffer */

static void handle_eqe_ib_events(EQP_eq_entry_t *eqe_p,
                                 eq_entry_t *eq_data_p);
/*================ global functions definitions ====================================*/



/************************************************************************
 *  Function: thh_intr_handler
 *  
 
    Arguments:
    eventp - handler to eventp object
    isr_ctx1 & isr_ctx2 - ignored
    
    
    Description:
    
    Clear_INT(); // De-assert INT pin (write to Clr_INT register) 
    ECR = Read_ECR(); // read ECR register 
    foreach (EQN set in ECR) { 
      Schedule_EQ_handler(EQN);      // schedule appropriate handler 
      Clear_ECR; // Clear ECR bits that were taken care of 
    }
    
    
    
************************************************************************/


irqreturn_t thh_intr_handler(MT_ulong_ptr_t eventp_dpc, void* isr_ctx1, void* isr_ctx2)
{
  THH_eventp_t eventp = (THH_eventp_t)eventp_dpc;
  u_int32_t ecr[2]; /* 0 - low 1- high */
  int i,j, eq_num;
  EQP_eq_entry_t *event_entry;


  /* start measurements */

  //MTPERF_TIME_START(interupt_segment);
  
  /* De-assert INT pin (write to Clr_INT register) */
  MOSAL_MMAP_IO_WRITE_DWORD(eventp->intr_clr_reg, eventp->intr_clr_mask);

  /*MTL_DEBUG1("%s: ECR register=0x%08x\n", __func__, eventp->ecr_h_base );*/

  /* read ECR register */
  ecr[0] = MOSAL_be32_to_cpu(MOSAL_MMAP_IO_READ_DWORD(eventp->ecr_l_base));
#if EQP_ECR_USED>1
  ecr[1] = MOSAL_be32_to_cpu(MOSAL_MMAP_IO_READ_DWORD(eventp->ecr_h_base));
  MTL_ERROR1("%s: READ ECR HIGH\n", __func__);
#endif
  
  /* No need to check anything of all bits are zero */
  if (ecr[0] == 0) {
    return IRQ_HANDLED;
  }
  if (ecr[0] == 0xffffffff) {
    /* master abort */
    /* verify that it indeed master abort by reading ECR high too */
    ecr[1] = MOSAL_be32_to_cpu(MOSAL_MMAP_IO_READ_DWORD(eventp->ecr_h_base));
    if (ecr[1] == 0xffffffff) {
      /* notify hob for fatal error - must be done from DPC level */
      MTL_ERROR1("%s: THH_FATAL_MASTER_ABORT: on ECR[0 & 1] \n", __func__);
      if (eventp->have_fatal) {
        /* no need to do anything since we already in fatal state */
        MTL_ERROR1("%s: THH_FATAL_MASTER_ABORT recieved when we are in FATAL state - NOP \n", __func__);
      }
      else { /* a new fatal error */
        eventp->have_fatal = TRUE;
        eventp->fatal_type = THH_FATAL_MASTER_ABORT;
        MOSAL_DPC_schedule(&(eventp->fatal_error_dpc)); /* schedule DPC to notify hob on master abort */
      }
      return IRQ_HANDLED;
    }
    MTL_ERROR1("%s: THH_FATAL_MASTER_ABORT: on ECR[0] only - ignoring \n", __func__);
  }
  
  /* work on both words */
  for(i=0; i<EQP_ECR_USED; i++) {      /* foreach dword */
    for(j=0; j<eventp->max_eq_num_used; j++) {    /* foreach bit   */
      if((ecr[i] & BITS32(j,1))) {  /* if the j-th bit is set */
        eq_num = (i << 5) + j;
        event_entry = &(eventp->eq_table[eq_num]);
        MOSAL_spinlock_irq_lock(&(event_entry->state_lock));
        if (IS_EQ_VALID_P(event_entry)) {
          MTL_DEBUG1("%s: ECR_LOW=0x%08x ECR_HIGH=not read, eq_num = %d \n", __func__, ecr[0], eq_num);
          /* check if this is catastrophic error */
          if (eq_num == EQP_CATAS_ERR_EQN) {
            MTL_ERROR1("%s: THH_FATAL_ERROR recieved on EQP_CATAS_ERR_EQN (%d) \n", 
                       __func__, EQP_CATAS_ERR_EQN);
            if (eventp->have_fatal) {
              /* no need to do anything since we already in fatal state */
              MTL_ERROR1("%s: THH_FATAL_ERROR recieved when we are in FATAL state - NOP \n", __func__);
            }
            else { /* a new fatal error */
              eventp->have_fatal = TRUE;
              eventp->fatal_type = THH_FATAL_EVENT;
              MOSAL_DPC_schedule(&(eventp->fatal_error_dpc)); /* schedule DPC to notify hob on fatal error */
            }
          } 
          else {
            if (MOSAL_DPC_schedule(&event_entry->polling_dpc)){  /* schedule DPC to poll EQ */
        		#if defined(IMPROVE_EVENT_HANDLING) && defined(__i386__)
        			  MOSAL_inc_bit32(&event_entry->dpc_cntr);
          		#else
                      event_entry->dpc_cntr++;  /* Monitor number of outstanding DPCs */
                #endif    
#ifdef THH_CMD_TIME_TRACK
                      if (eq_num == EQP_CMD_IF_EQN) {
                        THH_eventp_last_cmdif_interrupt= MOSAL_get_time_counter();
                      }
#endif
            }
          }
        } 
        else {
          if (eventp->have_fatal) {
            MTL_DEBUG1("%s: in fatal state: got event to EQ=%d but it is not setup; eq state=%d\n", __func__, 
                      i*32 + j, event_entry->res_state);
          }
          else {
            MTL_ERROR1("%s: Internal error: got event to EQ=%d but it is not setup; eq state=%d\n", __func__, 
                      i*32 + j, event_entry->res_state);
          }
          
        }
        MOSAL_spinlock_unlock(&(event_entry->state_lock));
        
        /* clear ecr bits that were taken care of - already in big-endian */
        if (event_entry->clr_ecr_addr) {
          MOSAL_MMAP_IO_WRITE_DWORD(event_entry->clr_ecr_addr, event_entry->clr_ecr_mask); 
        }
        else {
          MTL_ERROR1(MT_FLFMT("event_entry->clr_ecr_addr=null\n"));
        }

        if (eq_num == EQP_CATAS_ERR_EQN) {
          goto end_intr; 
        }
      } /* the j-th bit is set */
    } /* foreach bit */
  } /* foreach dword */
  
end_intr:
  return IRQ_HANDLED;
} /* thh_intr_handler() */

/* inline functions must be placed BEFORE their call */
/************************************************************************************************/
inline static MT_bool read_eq_entry(u_int32_t  *eqe_buff_p,
                          EQ_type_t  eq_type,
                          eq_entry_t *eqe_p)
{
  u_int32_t tmp_eq_data[8] = {0,0,0,0,0,0,0,0}; /* entry size is 32 bytes */
  unsigned int i;


  MTL_DEBUG1("%s: eqe_buff= %x %x %x %x %x %x %x %x \n", __func__, *eqe_buff_p,
	  *(eqe_buff_p+1), *(eqe_buff_p+2), *(eqe_buff_p+3),*(eqe_buff_p+4),
	  *(eqe_buff_p+5), *(eqe_buff_p+6), *(eqe_buff_p+7));
  /* first extract the owner & event_type only*/
  
#ifdef EQS_CMD_IN_DDR
  tmp_eq_data[EQE_OWNER_OFFSET] = MOSAL_be32_to_cpu(MOSAL_MMAP_IO_READ_DWORD((eqe_buff_p+EQE_OWNER_OFFSET)));
  /* owner */
  if ( EQE_SW_OWNER != MT_EXTRACT_ARRAY32(tmp_eq_data, MT_BIT_OFFSET(tavorprm_event_queue_entry_st, owner),
                                 MT_BIT_SIZE(tavorprm_event_queue_entry_st, owner))) {
    MTL_DEBUG1("%s: EQE_HW_OWNER\n", __func__);
    return FALSE;
  }
#else
  /* if EQE not in SW ownership - end of EQEs to handle */
  if ((((u_int8_t*)eqe_buff_p)[EQE_OWNER_BYTE_OFFSET] & EQE_HW_OWNER) != EQE_SW_OWNER) {
    MTL_DEBUG1("%s: EQE_HW_OWNER\n", __func__);
    return FALSE;
  }
#endif  

  /* event_type & event_sub_type */
#ifdef EQS_CMD_IN_DDR
  tmp_eq_data[EQE_EVENT_TYPE_OFFSET] = MOSAL_be32_to_cpu(MOSAL_MMAP_IO_READ_DWORD((eqe_buff_p+EQE_EVENT_TYPE_OFFSET)));
#else
  tmp_eq_data[EQE_EVENT_TYPE_OFFSET] = MOSAL_be32_to_cpu(*(eqe_buff_p+EQE_EVENT_TYPE_OFFSET));
#endif
  eqe_p->event_type = MT_EXTRACT_ARRAY32(tmp_eq_data, MT_BIT_OFFSET(tavorprm_event_queue_entry_st, event_type),
                                      MT_BIT_SIZE(tavorprm_event_queue_entry_st, event_type));

  eqe_p->event_sub_type = (tavor_if_port_event_subtype_t)
        MT_EXTRACT_ARRAY32(tmp_eq_data, MT_BIT_OFFSET(tavorprm_event_queue_entry_st, event_sub_type),
                                      MT_BIT_SIZE(tavorprm_event_queue_entry_st, event_sub_type));

  /* extract of data will be done according to the event type  */
  
  if(eq_type != EQP_CMD_IF_EVENT) {
    for(i=EQE_DATA_OFFSET; i< EQE_OWNER_OFFSET; i++) {
#ifdef EQS_CMD_IN_DDR
      eqe_p->event_data[i-EQE_DATA_OFFSET] = MOSAL_be32_to_cpu(MOSAL_MMAP_IO_READ_DWORD((eqe_buff_p + i)));
#else
      eqe_p->event_data[i-EQE_DATA_OFFSET] = MOSAL_be32_to_cpu(*(eqe_buff_p + i));
#endif
    }
  }
  else {   /* need to leave as big-endien for the cmd_if callback */
#ifdef EQS_CMD_IN_DDR
    MOSAL_MMAP_IO_READ_BUF_DWORD(eqe_buff_p+EQE_DATA_OFFSET,eqe_p->event_data,EQE_DATA_BYTE_SIZE/4);
#else
    memcpy(eqe_p->event_data, eqe_buff_p+EQE_DATA_OFFSET, EQE_DATA_BYTE_SIZE);
#endif
  }
  FUNC_OUT;
  return TRUE;
}

/************************************************************************************************/

inline static HH_ret_t move_eqe_to_hw(EQP_eq_entry_t *eqe_p, 
                               u_int32_t      *eqe_buff_p)     /* pointer to start of EQE buffer */
{

  HH_ret_t ret = HH_OK;

  /* move EQE to HW ownership */
  
#ifdef EQS_CMD_IN_DDR
  MOSAL_MMAP_IO_WRITE_DWORD((eqe_buff_p+EQE_OWNER_OFFSET), 0xffffffff);
#else
    *(eqe_buff_p+EQE_OWNER_OFFSET) = 0xffffffff; /* no need for cpu to be32 sine all is ff */
#endif

  /* update consumer index the FW take care to the cyclic buffer update */
  ret = THH_uar_eq_cmd(eqe_p->eventp_p->kar, TAVOR_IF_UAR_EQ_INC_CI, eqe_p->eqn, 0);
  if(ret != HH_OK) {
    MTL_ERROR1("%s: Internal error: THH_uar_eq_cmd failed ret=%d.\n", __func__,ret);
  }
  MT_RETURN( ret);
}


/************************************************************************
 *  Function: eq_polling_dpc
 *  
 
    Arguments:
    
    Returns:
    HH_OK 
    HH_EINVAL -Invalid parameters 
    
    Description: 
    
    lock spinlock of EQ
    clear ECR of this EQ
    While (EQE[Consumer_indx].Owner == SW) { 
      consume_entry - call EQ handler; // remove entry from the queue 
      EQE[Consumer_indx++].Owner = HW; // mark entry to the Hardware ownership for next time around 
      consumer_indx &= MASK; // wrap around index 
    } 
    update_consumer_index(EQ,consumer_indx); // update consumer index in HCA via UAR 
    subscribe_for_event(EQ); // subscribe for event for next time
    unlock EQ spinlock
    
************************************************************************/

void eq_polling_dpc(DPC_CONTEXT_t *dpc_obj_p)
{
  EQP_eq_entry_t *eqe_p = (EQP_eq_entry_t *)dpc_obj_p->func_ctx; /* EQ context entry (not the EQE) */ 
  u_int32_t   cons_indx;
  eq_entry_t  eqe;
  u_int32_t   *eqe_buff_p;
  void* cyc_buff;
  HH_ret_t ret;

  
  //MTPERF_TIME_END(inter2dpc_segment);
  //MTPERF_TIME_START(dpc_segment);

  FUNC_IN;


  if (eqe_p->eventp_p->have_fatal){
    goto dpc_done1;
  }
  
#if !defined(DPC_IS_DIRECT_CALL) && !defined(IMPROVE_EVENT_HANDLING)
  // the following fragment is unnecessary, because THH_eventp_teardown_eq() will wait for all DPCs to exit.
  /* In Darwin, the DPC is a direct function call, so this is guaranteed to be
   * checked already */
  MOSAL_spinlock_irq_lock(&eqe_p->state_lock);
  if (!IS_EQ_VALID_P(eqe_p)) {  /* Make sure the EQ is still in use (was not torn down) */
    MOSAL_spinlock_unlock(&eqe_p->state_lock);
    MTL_ERROR1(MT_FLFMT("DPC invoked for an EQ not in use.\n"));
    goto dpc_done1;
  }
  MOSAL_spinlock_unlock(&eqe_p->state_lock);
#endif
  
  
  /* CLEAR ECR bit of this EQ since it might be set again between ISR & DPC */
  /* moved to comment since any write to the Tavor registers harm the performance */
  /* MOSAL_MMAP_IO_WRITE_DWORD(eqe_p->clr_ecr_addr, eqe_p->clr_ecr_mask); */

  cyc_buff = eqe_p->eq_buff;
  /* EQEs polling loop */
/* need to protect only in case simultanuous DPC can be called at the same time 
   currently will be set only for windows */
#ifdef SIMULTANUOUS_DPC 
#ifdef IMPROVE_EVENT_HANDLING
  if (MOSAL_test_set_bit32(0, (volatile void * )&eqe_p->dpc_lock)) {
    goto dpc_done1;
  }
#else
  MOSAL_spinlock_irq_lock(&eqe_p->dpc_lock); /* protect consumer index access */
#endif
#endif

  for(cons_indx = eqe_p->cons_indx;/* break done inside the loop*/ ;
                  cons_indx = (cons_indx == (eqe_p->eq_buff_entry_num - 1)) ? 0 : cons_indx+1) {

    MTL_DEBUG1("%s: cons_indx=%d cyc_buff=%p\n", __func__,cons_indx, cyc_buff);
    eqe_buff_p = ((u_int32_t*)cyc_buff) + EQE_DWORD_SIZE*cons_indx ;   /* TK: can improve with << of EQE logsize later */
    /* read the EQE and change to machine endianess */
    if (read_eq_entry(eqe_buff_p, eqe_p->eq_type, &eqe) == FALSE){
      MTL_DEBUG1("%s: entry not in SW ownership, cons_indx=%d cyc_buff=%p\n", __func__,cons_indx, cyc_buff);
      break; /* no more EQEs to poll */
    }

    /* if we are here then we have EQE in SW ownership */

    /* first return EQE to HW ownership and update consumer index */
    if((ret = move_eqe_to_hw(eqe_p, eqe_buff_p)) != HH_OK) {
      MTL_ERROR1("%s: failed moving EQE to HW \n", __func__);
      goto dpc_done2;
    }
    /* TK: maybe this should be moved before the move to hw?? 
       now check that we don't have overrun */

    if(eqe.event_type == TAVOR_IF_EV_TYPE_OVERRUN) {
      MTL_ERROR1("%s: EQ OVERRUN eqn=%d\n", __func__, eqe_p->eqn);
      /* need to notify the hob for the fatal error */
      eqe_p->eventp_p->have_fatal = TRUE;
      THH_hob_fatal_error(eqe_p->eventp_p->hob, THH_FATAL_EQ_OVF, VAPI_CATAS_ERR_EQ_OVERFLOW);
      goto dpc_done2;
    }
    
    MTL_DEBUG1("%s: EQ type =%d\n", __func__, eqe_p->eq_type);
    switch (eqe_p->eq_type) {
      /* handle CMD_IF EQ */
      case EQP_CMD_IF_EVENT:
        if(eqe.event_type != TAVOR_IF_EV_TYPE_CMD_IF_COMP) {
            MTL_ERROR1("%s: Internal error: wrong EQ type to CMD_IF EQ. EQ type =%d\n", __func__, 
                       eqe.event_type);
        }
        else {
          /* notify cmd_if module */
          THH_cmd_eventh(eqe_p->eventp_p->cmd_if, (u_int32_t*)eqe.event_data);
        }
        break;
      /* IB events */
      case EQP_IB_EVENT:
        handle_eqe_ib_events(eqe_p,&eqe);
        break;
      
      /* Completion Events */     
      case EQP_CQ_COMP_EVENT: 
        /* sanity check */
        if(eqe.event_type != TAVOR_IF_EV_TYPE_CQ_COMP) {
          MTL_ERROR1("%s: Internal error: wrong EQ type to COMPLETION EVENTS EQ. EQ type =%d\n", __func__,
                         eqe.event_type);
        }
        else {
          u_int32_t   cqnum=0;
          cqnum = MT_EXTRACT_ARRAY32(eqe.event_data, MT_BIT_OFFSET(tavorprm_completion_event_st, cqn),
                                  MT_BIT_SIZE(tavorprm_completion_event_st, cqn));
          MTL_DEBUG2("%s: Got completion event on CQN=%d. Going to DIS-ARM CQ\n", __func__,cqnum);
          /* disarm CQ */
          ret = THH_uar_eq_cmd(eqe_p->eventp_p->kar, TAVOR_IF_UAR_EQ_DISARM_CQ, eqe_p->eqn, cqnum);

          /* call handler */
          
          eqe_p->handler.comp_event_h(eqe_p->eventp_p->hh_hca_hndl, (HH_cq_hndl_t)cqnum, 
                                      eqe_p->priv_context);
        }
        break;

      case EQP_CATAS_ERR_EVENT:
        MTL_ERROR1("%s: Internal error: got to EQP_CATAS_ERR_EVENT in regular DPC \n", __func__);
        goto dpc_done2;
        break;
      case EQP_MLX_EVENT:
        break;
    }
  } /* polling EQE for loop */

#ifdef SIMULTANUOUS_DPC
#ifdef IMPROVE_EVENT_HANDLING
	MOSAL_test_clear_bit32(0, (volatile void * )&eqe_p->dpc_lock);
#else
	MOSAL_spinlock_unlock(&eqe_p->dpc_lock);
#endif
#endif

  /* re-arm EQ for all EQs */
  ret = THH_uar_eq_cmd(eqe_p->eventp_p->kar, TAVOR_IF_UAR_EQ_INT_ARM, eqe_p->eqn, 0);
  if(ret != HH_OK) {
    MTL_ERROR1("%s: Internal error: THH_uar_eq_cmd failed ret=%d.\n", __func__,ret);
  }

  /* update consumer index of EQ */
  eqe_p->cons_indx = cons_indx;
  goto dpc_done1;

dpc_done2:
#ifdef SIMULTANUOUS_DPC
#ifdef IMPROVE_EVENT_HANDLING
	MOSAL_test_clear_bit32(0, (volatile void * )&eqe_p->dpc_lock);
#else
	MOSAL_spinlock_unlock(&eqe_p->dpc_lock);
#endif
#endif

dpc_done1:
#if defined(IMPROVE_EVENT_HANDLING) && defined(__i386__)
    /* Currently defined only in WIN version */
	MOSAL_dec_bit32(&eqe_p->dpc_cntr);
#elif defined(DPC_IS_DIRECT_CALL)
  /* In darwin, this spinlock is locked all throughout the intr_handler which
   * directly calls this function */
    eqe_p->dpc_cntr--;  /* signal DPC done (for cleanup) */
#else  
    MOSAL_spinlock_irq_lock(&eqe_p->state_lock);
    eqe_p->dpc_cntr--;  /* signal DPC done (for cleanup) */
    MOSAL_spinlock_unlock(&eqe_p->state_lock);
#endif  

  FUNC_OUT;

#ifdef MTPERF
  if (eqe_p->eq_type == EQP_CQ_COMP_EVENT) {
    /*MTPERF_TIME_END(dpc_segment)*/;
  }
#endif
}

/************************************************************************************************/


static inline VAPI_event_record_type_t tavor2vapi_qp_error_type(u_int8_t event_type, MT_bool is_qp)
{
  
  VAPI_event_record_type_t vapi_event_type;
  switch(event_type) {
    case TAVOR_IF_EV_TYPE_PATH_MIG: 
      vapi_event_type = is_qp ? VAPI_QP_PATH_MIGRATED : VAPI_EEC_PATH_MIGRATED;
      break;
    case TAVOR_IF_EV_TYPE_SEND_Q_DRAINED: 
      vapi_event_type = VAPI_SEND_QUEUE_DRAINED;
      break;
    case TAVOR_IF_EV_TYPE_PATH_MIG_FAIL: 
      vapi_event_type = VAPI_PATH_MIG_REQ_ERROR;
      break;
    case TAVOR_IF_EV_TYPE_COMM_EST:
      vapi_event_type =  is_qp ? VAPI_QP_COMM_ESTABLISHED : VAPI_EEC_COMM_ESTABLISHED;
      break;
    case TAVOR_IF_EV_TYPE_LOCAL_WQ_CATAS_ERR:
      vapi_event_type = is_qp ? VAPI_LOCAL_WQ_CATASTROPHIC_ERROR : VAPI_LOCAL_EEC_CATASTROPHIC_ERROR;
      break;
    case TAVOR_IF_EV_TYPE_LOCAL_WQ_INVALID_REQ_ERR: 
      vapi_event_type = VAPI_LOCAL_WQ_INV_REQUEST_ERROR;
      break;
    case TAVOR_IF_EV_TYPE_LOCAL_WQ_ACCESS_VIOL_ERR: 
      vapi_event_type = VAPI_LOCAL_WQ_ACCESS_VIOL_ERROR;
      break;
    case TAVOR_IF_EV_TYPE_SRQ_QP_LAST_WQE_REACHED:
      vapi_event_type = VAPI_RECEIVE_QUEUE_DRAINED;
      break;
    case TAVOR_IF_EV_TYPE_LOCAL_SRQ_CATAS_ERR:
      vapi_event_type = VAPI_SRQ_CATASTROPHIC_ERROR;
      break;
      
    default:
      MTL_ERROR1("%s: Unknown event type = %d\n", __func__,event_type);
      vapi_event_type = VAPI_LOCAL_CATASTROPHIC_ERROR;
  }
  return vapi_event_type;
}
         

  
/************************************************************************************************/
static void handle_eqe_ib_events(EQP_eq_entry_t *eqe_p,
                                 eq_entry_t     *eq_data_p)
{
  HH_event_record_t event_record;
  u_int8_t syndrome=0;
  VAPI_event_syndrome_t vapi_syndrome = VAPI_EV_SYNDROME_NONE;
  event_record.syndrome = VAPI_EV_SYNDROME_NONE;

  FUNC_IN;
  switch(eq_data_p->event_type) {
#if 0    /* TK: currently not supported by Tavor */
    case TAVOR_IF_EV_TYPE_LOCAL_EE_CATAS_ERR:                  
#endif
    /* QP/EEC errors */
    case TAVOR_IF_EV_TYPE_PATH_MIG_FAIL:            
	case TAVOR_IF_EV_TYPE_PATH_MIG:                 
    case TAVOR_IF_EV_TYPE_COMM_EST:
    case TAVOR_IF_EV_TYPE_LOCAL_WQ_CATAS_ERR:
    case TAVOR_IF_EV_TYPE_LOCAL_WQ_INVALID_REQ_ERR:
    case TAVOR_IF_EV_TYPE_LOCAL_WQ_ACCESS_VIOL_ERR:
    case TAVOR_IF_EV_TYPE_SEND_Q_DRAINED: 
    case TAVOR_IF_EV_TYPE_SRQ_QP_LAST_WQE_REACHED:  /* this is QP event */
      {
        u_int32_t qpn=0;
        u_int32_t is_qp=1;

        qpn = MT_EXTRACT_ARRAY32(eq_data_p->event_data, MT_BIT_OFFSET(tavorprm_qp_ee_event_st, qpn_een),
                              MT_BIT_SIZE(tavorprm_qp_ee_event_st, qpn_een));
        
        is_qp = !(MT_EXTRACT_ARRAY32(eq_data_p->event_data, MT_BIT_OFFSET(tavorprm_qp_ee_event_st, e_q),
                               MT_BIT_SIZE(tavorprm_qp_ee_event_st, e_q)));

        if (is_qp) {
          event_record.event_modifier.qpn = qpn;
          /* need to translate to VAPI_event_record_type_t */ 
          event_record.etype = tavor2vapi_qp_error_type(eq_data_p->event_type,is_qp);
        }
        else { /* EEC is not supported now */
          MTL_ERROR1("%s: Internal error: is_eq = 0 but EEC not supported. eqn=%d\n", __func__,
                     eqe_p->eqn);
        }
        break;
      }
    
    case TAVOR_IF_EV_TYPE_LOCAL_SRQ_CATAS_ERR:
      {
        event_record.event_modifier.srq= 
          MT_EXTRACT_ARRAY32(eq_data_p->event_data, 
                             MT_BIT_OFFSET(tavorprm_qp_ee_event_st, qpn_een),
                             MT_BIT_SIZE(tavorprm_qp_ee_event_st, qpn_een));
          event_record.etype = tavor2vapi_qp_error_type(eq_data_p->event_type,FALSE);
          break;
      }

    /* IB - affiliated errors CQ  */
    case TAVOR_IF_EV_TYPE_CQ_ERR:                           
      {
        u_int32_t   cqnum=0;
        
        cqnum = MT_EXTRACT_ARRAY32(eq_data_p->event_data, MT_BIT_OFFSET(tavorprm_completion_queue_error_st, cqn),
                                MT_BIT_SIZE(tavorprm_completion_queue_error_st, cqn));
        syndrome = MT_EXTRACT_ARRAY32(eq_data_p->event_data, MT_BIT_OFFSET(tavorprm_completion_queue_error_st, syndrome),
                                   MT_BIT_SIZE(tavorprm_completion_queue_error_st, syndrome));
        event_record.etype = VAPI_CQ_ERROR;
        event_record.event_modifier.cq = cqnum;
        event_record.syndrome = (syndrome == TAVOR_IF_CQ_OVERRUN) ? VAPI_CQ_ERR_OVERRUN : 
                       ((syndrome == TAVOR_IF_CQ_ACCSS_VIOL_ERR) ? VAPI_CQ_ERR_ACCESS_VIOL : 
                         VAPI_EV_SYNDROME_NONE);
        MTL_ERROR1("%s: CQ error on CQ number= %d syndrome is %s (%d)\n", __func__,cqnum,
                   VAPI_event_syndrome_sym(event_record.syndrome), event_record.syndrome);
        break;
      }
    /* Unaffiliated errors */
    case TAVOR_IF_EV_TYPE_LOCAL_CATAS_ERR:
      {
        MTL_ERROR1("%s: CATASTROPHIC ERROR - should not be in this EQ: \n", __func__);
        MTL_ERROR1("CATASTROPHIC ERROR: data: %x %x %x %x %x %x \n", *(eq_data_p->event_data),
                   *(eq_data_p->event_data+1), *(eq_data_p->event_data+2), *(eq_data_p->event_data+3),
                   *(eq_data_p->event_data+4), *(eq_data_p->event_data+5));
        break;                 
      }
    case TAVOR_IF_EV_TYPE_PORT_ERR:                        
      { 
        IB_port_t port;
        port = MT_EXTRACT_ARRAY32(eq_data_p->event_data,MT_BIT_OFFSET(tavorprm_port_state_change_st, p),
                               MT_BIT_SIZE(tavorprm_port_state_change_st, p));

        if (eq_data_p->event_sub_type == TAVOR_IF_SUB_EV_PORT_DOWN) {
          event_record.etype = VAPI_PORT_ERROR;
        }
        else if (eq_data_p->event_sub_type == TAVOR_IF_SUB_EV_PORT_UP) {
          event_record.etype = VAPI_PORT_ACTIVE;
        }
        else {
          MTL_ERROR1("%s: Wrong sub-type for Port event on port=%d sub_type=%d\n", __func__,port,eq_data_p->event_sub_type);
        }

        event_record.event_modifier.port = port;
        MTL_DEBUG1("%s: Port change event on port=%d sub_type=%d\n", __func__,port,eq_data_p->event_sub_type);
        break;
      }
    default:
      MTL_ERROR1("%s: Unsupported event type = %d\n", __func__,eq_data_p->event_type);
      /* in case of catastrophic error - no call to upper layer but notify HOB to handle */
      MTL_ERROR1("CATASTROPHIC ERROR: data: %x %x %x %x %x %x \n", *(eq_data_p->event_data),
                 *(eq_data_p->event_data+1), *(eq_data_p->event_data+2), *(eq_data_p->event_data+3),
                 *(eq_data_p->event_data+4), *(eq_data_p->event_data+5));

      THH_hob_fatal_error(eqe_p->eventp_p->hob, THH_FATAL_EVENT, vapi_syndrome);
      /**** !!!! FUNCTION returns HERE !!!! */
      FUNC_OUT;
      return;
  } /* switch(eq_data_p->event_type)  */
  
  /* call the event callback */
  eqe_p->handler.ib_comp_event_h(eqe_p->eventp_p->hh_hca_hndl, &event_record, eqe_p->priv_context);

  FUNC_OUT;
  
}


/************************************************************************************************/
          
void fatal_error_dpc(DPC_CONTEXT_t *dpc_obj_p)
{
  VAPI_event_syndrome_t  syndrome;
  
  THH_eventp_t eventp = (THH_eventp_t)dpc_obj_p->func_ctx; /* eventp of this hob */ 
  FUNC_IN;

  if (eventp->fatal_type == THH_FATAL_MASTER_ABORT) {
    syndrome = VAPI_CATAS_ERR_MASTER_ABORT;
    MTL_DEBUG1("%s: VAPI_CATAS_ERR_MASTER_ABORT\n", __func__);
  }
  else {
    syndrome = VAPI_CATAS_ERR_GENERAL;
    MTL_DEBUG1("%s: VAPI_CATAS_ERR_GENERAL\n", __func__);
  }

  THH_hob_fatal_error(eventp->hob, eventp->fatal_type, syndrome);
  
  FUNC_OUT;
}
