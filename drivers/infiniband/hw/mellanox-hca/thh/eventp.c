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


#include <mosal.h>
#include <MT23108.h>
#include <eventp_priv.h>
#include <tavor_if_defs.h>
#include <cmdif.h>
#include <thh_hob.h>
#include <uar.h>
#include <tlog2.h>
#include <tmrwm.h>
//#define MTPERF
#include <mtperf.h>

#ifdef EQS_CMD_IN_DDR
#include <tddrmm.h>
#endif


/*================ macro definitions ===============================================*/


#define NOT_VALID_EQ_NUM(eq_num) (((eq_num) < 0) || ((eq_num) >= EQP_MAX_EQS))


/*================ type definitions ================================================*/

#define PHYS_EQ_MAX_SIZE 0x2000

/*================ global variables definitions ====================================*/


MTPERF_EXTERN_SEGMENT(interupt_segment);       
MTPERF_EXTERN_SEGMENT(dpc_segment);
MTPERF_EXTERN_SEGMENT(inter2dpc_segment);
MTPERF_EXTERN_SEGMENT(part_of_DPC_segment);


/*================ static functions prototypes =====================================*/

#ifdef MAX_DEBUG
static char * const eq_type_str(EQ_type_t eq_type)
{
  switch ( eq_type ) {
    case EQP_CQ_COMP_EVENT:
      return "EQP_CQ_COMP_EVENT";
    case EQP_IB_EVENT:
      return "EQP_IB_EVENT";
    case EQP_CMD_IF_EVENT:
      return "EQP_CMD_IF_EVENT";
    case EQP_MLX_EVENT:
      return "EQP_MLX_EVENT";
    case EQP_CATAS_ERR_EVENT:
      return "EQP_CATAS_ERR_EVENT";
    default:
      return "***UNKNOWNN***";
  }
}
#endif

#ifdef __WIN__
#ifndef MAX_DEBUG
static char * const eq_type_str(EQ_type_t eq_type) { return NULL; }
#endif
#endif

static THH_eqn_t insert_new_eq(THH_eventp_t     eventp,
                               void*            eventh, 
                               void             *priv_context, 
                               MT_size_t        max_outs_eqe, 
                               EQ_type_t        eq_type);

static HH_ret_t map_eq(THH_eventp_t        eventp,
                        THH_eqn_t           eqn,
                        THH_eventp_mtmask_t tavor_mask);

static void remove_eq(THH_eventp_t        eventp,
                 THH_eqn_t           eqn);


static HH_ret_t prepare_intr_resources(THH_eventp_t eventp);

static HH_ret_t remove_intr_resources(THH_eventp_t eventp);

static HH_ret_t add_catast_err_eq(THH_eventp_t eventp);

/*================ external functions definitions ====================================*/



extern void eq_polling_dpc(DPC_CONTEXT_t *func_ctx);

extern void fatal_error_dpc(DPC_CONTEXT_t *func_ctx);

extern irqreturn_t thh_intr_handler(MT_ulong_ptr_t eventp, void* isr_ctx1, void* isr_ctx2);




/************************************************************************
 *  Function: THH_eventp_create
 *  
 
    Arguments:
    version_p - Version information 
    event_res_p - See 7.2.1 THH_eventp_res_t - Event processing resources on page 63 
    cmd_if - Command interface object to use for EQ setup commands 
    kar - KAR object to use for EQ doorbells 
    eventp_p - Returned object handle 
    
    Returns:
    HH_OK 
    HH_EINVAL -Invalid parameters 
    HH_EAGAIN -Not enough resources to create object 
    HH_ERR - internal error
    
    Description: 
    Create THH_eventp object context. No EQs are set up until an event consumer registers 
    using one of the functions below. 
    
    
************************************************************************/
 
HH_ret_t THH_eventp_create ( /*IN */ THH_hob_t hob,
                             /*IN */ THH_eventp_res_t *event_res_p, 
                             /*IN */ THH_uar_t kar, 
                             /*OUT*/ THH_eventp_t *eventp_p )
{

  THH_eventp_t new_eventp_p = NULL;
  u_int32_t i;
  HH_ret_t ret;

  FUNC_IN;
                         
  
  /* allocation of object structure */
  new_eventp_p = (THH_eventp_t)VMALLOC(sizeof(THH_eventp_internal_t));
  if (!new_eventp_p) {
    MTL_ERROR4("%s: Cannot allocate EVENTP object.\n", __func__);
    MT_RETURN(HH_EAGAIN);
  }
  
  memset(new_eventp_p,0,sizeof(THH_eventp_internal_t));
  MOSAL_mutex_init(&new_eventp_p->mtx);
  for (i= 0; i < EQP_MAX_EQS; i++) {
    SET_EQ_FREE(new_eventp_p,i);
    new_eventp_p->eq_table[i].eq_buff_entry_num= 0;
    new_eventp_p->eq_table[i].alloc_mem_addr_p= 0;
    if (MOSAL_spinlock_init(&(new_eventp_p->eq_table[i].state_lock)) != MT_OK){
      MTL_ERROR4("%s: Failed to initializing spinlocks.\n", __func__);
      ret= HH_ERR;
      goto err_free_mem;
    } 
#ifdef SIMULTANUOUS_DPC
#ifdef IMPROVE_EVENT_HANDLING
	new_eventp_p->eq_table[i].dpc_lock = 0;
#else
    if (MOSAL_spinlock_init(&(new_eventp_p->eq_table[i].dpc_lock)) != MT_OK){
      MTL_ERROR4("%s: Failed to initializing dpc_lock.\n", __func__);
      ret= HH_ERR;
      goto err_free_mem;
    } 
#endif    
#endif    
    
  }

  /* filling eventp structure */
  memcpy(&new_eventp_p->event_resources, event_res_p, sizeof(THH_eventp_res_t));
  new_eventp_p->max_eq_num_used = EQP_MIN_EQ_NUM;
  new_eventp_p->fatal_type = THH_FATAL_NONE;
  new_eventp_p->have_fatal = FALSE;
  new_eventp_p->hob = hob;
  if (THH_hob_get_cmd_if (hob, &new_eventp_p->cmd_if) != HH_OK){
    MTL_ERROR4("%s: Cannot get cmd_if object handle.\n", __func__);
    ret = HH_ERR;
    goto err_free_mem;
  }
  new_eventp_p->kar = kar; 
  if ((ret = THH_uar_get_index(new_eventp_p->kar, &new_eventp_p->kar_index)) != HH_OK){ /* the KAR */
    MTL_ERROR4("%s: Failed to THH_uar_get_index. ret=%d.\n", __func__,ret);
    ret = HH_ERR;
    goto err_free_mem;
  }
  if (THH_hob_get_ver_info(hob, &new_eventp_p->version) != HH_OK){
    MTL_ERROR4("%s: Cannot get version.\n", __func__);
    ret = HH_ERR;
    goto err_free_mem;
  }
  if (THH_hob_get_mrwm(hob, &new_eventp_p->mrwm_internal) != HH_OK){
    MTL_ERROR4("%s: Cannot get mrwm_internal.\n", __func__);
    ret = HH_ERR;
    goto err_free_mem;
  }
#ifdef EQS_CMD_IN_DDR
  if (THH_hob_get_ddrmm(hob, &new_eventp_p->ddrmm) != HH_OK){
    MTL_ERROR4("%s: Cannot get ddrmm.\n", __func__);
    ret = HH_ERR;
    goto err_free_mem;
  }
#endif
  if (THH_hob_get_hca_hndl(hob, &new_eventp_p->hh_hca_hndl) != HH_OK){
    MTL_ERROR4("%s: Cannot get HH_HCA_hndl.\n", __func__);
    ret = HH_ERR;
    goto err_free_mem;
  }

  new_eventp_p->ctx_internal = MOSAL_get_kernel_prot_ctx();

  /* init DPC of master abort & catastrophic error*/
  MOSAL_DPC_init(&new_eventp_p->fatal_error_dpc, fatal_error_dpc, (MT_ulong_ptr_t)new_eventp_p,  
                 MOSAL_SINGLE_CTX);

  MTL_TRACE4("%s: SUCCESS to MOSAL_DPC_init the master_abort_dpc. \n", __func__);

  if (prepare_intr_resources(new_eventp_p)  != HH_OK){
    MTL_ERROR4("%s: Cannot set interrupt resources.\n", __func__);
    ret = HH_ERR;
    goto err_free_mem;
  }

  /* initialize value of CLR_ECR for all EQs */
  for ( i=EQP_MIN_EQ_NUM; i<EQP_MAX_EQS; ++i ) {
    if (i < 32) {
      new_eventp_p->eq_table[i].clr_ecr_addr = new_eventp_p->clr_ecr_l_base;
    }
    else {
      new_eventp_p->eq_table[i].clr_ecr_addr = new_eventp_p->clr_ecr_h_base;
    }
    new_eventp_p->eq_table[i].clr_ecr_mask = MOSAL_cpu_to_be32(1 << (i % 32));
  }

  /* setup the catastrophic error EQ - must be a separete EQ initialzed at the begining */
  add_catast_err_eq(new_eventp_p);

  *eventp_p = new_eventp_p;
  MT_RETURN(HH_OK);


  /* error handling cleanup */
err_free_mem:
  VFREE(new_eventp_p);
  MT_RETURN(ret);

}

/************************************************************************
 *  Function: THH_eventp_destroy
 *  
 
   Arguments:
   eventp -The THH_eventp object to destroy 
   
   Returns:
   HH_OK 
   HH_EINVAL -Invalid event object handle 
   
   Description: 
   Destroy object context. If any EQs are still set they are torn-down when this 
   function is called Those EQs should generate an HCA catastrophic error ((i.e.callbacks 
   for IB compliant, proprietary and debug events are invoked) since this call implies 
   abnormal HCA closure (in a normal HCA closure all EQs should be torn-down before a 
   call to this function). 
   
   
 ************************************************************************/

HH_ret_t THH_eventp_destroy( /*IN */ THH_eventp_t eventp)
{
  THH_eqn_t eqn;

  FUNC_IN;
  
  if (eventp == NULL) {
    MTL_ERROR4("%s: eventp is NULL.\n", __func__);
    MT_RETURN( HH_EINVAL);
  }

  //MTPERF_REPORT_PRINTF(interupt_segment);
  //MTPERF_REPORT_PRINTF(inter2dpc_segment);
  //MTPERF_REPORT_PRINTF(dpc_segment);
  //MTPERF_REPORT_PRINTF(part_of_DPC_segment);
  
  
  /* Teardown any EQ still up */
  for (eqn=0; eqn<EQP_MAX_EQS; eqn++) {
      THH_eventp_teardown_eq(eventp, eqn);
  } /*for loop */
  
  MOSAL_mutex_acq_ui(&eventp->mtx);
  remove_intr_resources(eventp);
  MOSAL_mutex_rel(&eventp->mtx);
  MOSAL_mutex_free(&eventp->mtx);
  VFREE(eventp);
  MT_RETURN( HH_OK);
  

}
/************************************************************************
 *  Function: THH_eventp_setup_comp_eq
 *  
   Arguments:
   eventp -The THH_eventp object handle 
   eventh -The callback handle for events over this EQ 
   priv_context -Private context to be used in callback invocation 
   max_outs_eqe -Maximum outstanding EQEs in EQ created 
   eqn_p -Allocated EQ index 
   
   Returns:
   HH_OK
   HH_EINVAL -Invalid handle 
   HH_EAGAIN -Not enough resources available (e.g.EQC,mem- ory,etc.). 
   
   Description: 
   Set up an EQ for completion events and register given handler as a callback for 
   such events. 
   Note that the created EQ is not mapped to any event at this stage since completion 
   events are mapped using the CQC set up on CQ creation.
   
 
 ************************************************************************/

         
HH_ret_t THH_eventp_setup_comp_eq(/*IN */ THH_eventp_t eventp, 
                                  /*IN */ HH_comp_eventh_t eventh, 
                                  /*IN */ void *priv_context, 
                                  /*IN */ MT_size_t max_outs_eqe, 
                                  /*OUT*/ THH_eqn_t *eqn_p )

{
  THH_eqn_t new_eq;
  
  FUNC_IN;

  if (eventp == NULL || eventh == NULL || max_outs_eqe == 0) {
    MTL_ERROR4("%s: NULL parameter. eventp=%p, eventh=%p, max_outs_eqe=%d\n", __func__,
               eventp, eventh, (u_int32_t)max_outs_eqe);
    MT_RETURN(HH_EINVAL);
  }
  if (MOSAL_mutex_acq(&eventp->mtx, TRUE) != MT_OK){

    MTL_ERROR4("%s: MOSAL_mutex_acq failed\n", __func__);
    MT_RETURN(HH_EINTR);
  }
  new_eq = insert_new_eq(eventp, (void*)eventh, priv_context, max_outs_eqe, 
                         EQP_CQ_COMP_EVENT);
  
  if (new_eq == EQP_MAX_EQS) { /* All EQs are occupied */
    MTL_ERROR4("%s: Fail in adding new EQ.\n", __func__);
    MOSAL_mutex_rel(&eventp->mtx);
    MT_RETURN( HH_EAGAIN);
  }
  MTL_DEBUG1("%s success: eqn=%d\n", __func__, new_eq);

  *eqn_p = new_eq;
  MOSAL_mutex_rel(&eventp->mtx);

  MT_RETURN( HH_OK);
}

/************************************************************************
 *  Function: 
 *  
    Arguments:
    eventp -The THH_eventp object handle 
    eventh -The callback handle for events over this EQ 
    priv_context -Private context to be used in callback invocation 
    max_outs_eqe -Maximum outstanding EQEs in EQ created 
    eqn_p -Allocated EQ index 
    
    Returns:
    HH_OK 
    HH_EINVAL -Invalid handle 
    HH_EAGAIN -Not enough resources available (e.g.EQC,mem- ory,etc.). 
    HH_ERR    - Internal error
    
    Description: 
    Set up an EQ and map events given in mask to it. Events over new EQ are reported to 
    given handler. 
    
    
 ************************************************************************/

HH_ret_t THH_eventp_setup_ib_eq(/*IN */ THH_eventp_t eventp, 
                                /*IN */ HH_async_eventh_t eventh, 
                                /*IN */ void *priv_context, 
                                /*IN */ MT_size_t max_outs_eqe, 
                                /*OUT*/ THH_eqn_t *eqn_p )

{
  THH_eqn_t new_eq;
  THH_eventp_mtmask_t tavor_mask=0;

  FUNC_IN;

  if (eventp == NULL || eventh == NULL || max_outs_eqe == 0) {
    MTL_ERROR4("%s: NULL parameter. eventp=%p, eventh=%p, max_outs_eqe=%d\n", __func__,
               eventp, eventh, (u_int32_t)max_outs_eqe);
    MT_RETURN( HH_EINVAL);
  }
  
  if (MOSAL_mutex_acq(&eventp->mtx, TRUE) != MT_OK){

    MTL_ERROR4("%s: MOSAL_mutex_acq failed\n", __func__);
    MT_RETURN(HH_EINTR);
  }
  
  new_eq = insert_new_eq(eventp, (void*)eventh, priv_context, max_outs_eqe, 
                         EQP_IB_EVENT);
  
  if (new_eq == EQP_MAX_EQS) { /* All EQs are occupied */
    MTL_ERROR4("%s: Fail in adding new EQ.\n", __func__);
    MOSAL_mutex_rel(&eventp->mtx);
    MT_RETURN( HH_EAGAIN);
  }

  /* map the EQ to events */
  /* prepare mask for all IB events */
  TAVOR_IF_EV_MASK_SET(tavor_mask,TAVOR_IF_EV_MASK_PATH_MIG);
  TAVOR_IF_EV_MASK_SET(tavor_mask,TAVOR_IF_EV_MASK_COMM_EST);
  TAVOR_IF_EV_MASK_SET(tavor_mask,TAVOR_IF_EV_MASK_SEND_Q_DRAINED);
  TAVOR_IF_EV_MASK_SET(tavor_mask,TAVOR_IF_EV_MASK_CQ_ERR);
  TAVOR_IF_EV_MASK_SET(tavor_mask,TAVOR_IF_EV_MASK_LOCAL_WQ_CATAS_ERR);
  TAVOR_IF_EV_MASK_SET(tavor_mask,TAVOR_IF_EV_MASK_LOCAL_EE_CATAS_ERR);
  TAVOR_IF_EV_MASK_SET(tavor_mask,TAVOR_IF_EV_MASK_PATH_MIG_FAIL );
  TAVOR_IF_EV_MASK_SET(tavor_mask,TAVOR_IF_EV_MASK_PORT_ERR);
  TAVOR_IF_EV_MASK_SET(tavor_mask,TAVOR_IF_EV_MASK_LOCAL_WQ_INVALID_REQ_ERR);
  TAVOR_IF_EV_MASK_SET(tavor_mask,TAVOR_IF_EV_MASK_LOCAL_WQ_ACCESS_VIOL_ERR);
  if (eventp->event_resources.is_srq_enable) {
    TAVOR_IF_EV_MASK_SET(tavor_mask,TAVOR_IF_EV_MASK_LOCAL_SRQ_CATAS_ERR);
    TAVOR_IF_EV_MASK_SET(tavor_mask,TAVOR_IF_EV_MASK_SRQ_QP_LAST_WQE_REACHED);
  }

  if (map_eq(eventp, new_eq, tavor_mask) != HH_OK){
    MTL_ERROR4("%s: Failed to map EQ.\n", __func__);
    MOSAL_mutex_rel(&eventp->mtx);
    MT_RETURN( HH_ERR);
  }
  MTL_DEBUG1("%s: Succeeded to map EQ=%d. mask="U64_FMT"\n", __func__, new_eq, (u_int64_t)tavor_mask);
  *eqn_p = new_eq;

  MOSAL_mutex_rel(&eventp->mtx);
  
  MT_RETURN( HH_OK);
}



/************************************************************************
 *  Function: 
 *  
    Arguments:
    eventp -The THH_eventp object handle 
    
    Arguments:
    HH_OK 
    HH_EINVAL -Invalid handle 
    HH_EAGAIN -Not enough resources available (e.g.EQC,mem- ory,etc.). 
    EE_ERR    - internal error
    
    Description: 
    This function setup an EQ and maps command interface events to it. It also takes 
    care of notifying the THH_cmd associated with it (as de  ned on the THH_eventp creation)of 
    this EQ availability using the THH_cmd_set_eq() (see page 38)function.This causes the THH_cmd 
    to set event generation to given EQ for all commands dispatched after this noti  cation. The 
    THH_eventp automatically sets noti  cation of events from this EQ to the THH_cmd_eventh() 
    (see page 39)callback of associated THH_cmd. The function should be invoked by the THH_hob 
    in order to cause the THH_cmd associated with this eventp to execute commands using events. 
    
    
 ************************************************************************/

HH_ret_t THH_eventp_setup_cmd_eq ( /*IN */ THH_eventp_t eventp,
                                   /*IN */ MT_size_t max_outs_eqe)

{
  
  THH_eqn_t new_eq;
  THH_eventp_mtmask_t tavor_mask = 0;
  HH_ret_t ret;
  
  
  FUNC_IN;
  
  
  if (eventp == NULL || max_outs_eqe == 0) {
    MTL_ERROR4("%s: NULL parameter. eventp=%p, max_outs_eqe=%d\n", __func__,
               eventp, (u_int32_t)max_outs_eqe);
    MT_RETURN( HH_EINVAL);
  }
  
  if (MOSAL_mutex_acq(&eventp->mtx, TRUE) != MT_OK){
    MTL_ERROR4("%s: MOSAL_mutex_acq failed\n", __func__);
    MT_RETURN(HH_EINTR);
  }
  
  new_eq = insert_new_eq(eventp, NULL, NULL, max_outs_eqe, EQP_CMD_IF_EVENT);
  
  if (new_eq == EQP_MAX_EQS) { /* All EQs are occupied */
    MTL_ERROR4("%s: Fail in adding new EQ.\n", __func__);
    MOSAL_mutex_rel(&eventp->mtx);
    MT_RETURN( HH_EAGAIN);
  }
  
  /* map the EQ to events */
  TAVOR_IF_EV_MASK_SET(tavor_mask, TAVOR_IF_EV_MASK_CMD_IF_COMP);
  if (map_eq(eventp,new_eq,tavor_mask) != HH_OK){
    MTL_ERROR4("%s: Failed to map EQ.\n", __func__);
    MOSAL_mutex_rel(&eventp->mtx);
    MT_RETURN( HH_ERR);
  }
#if 1
  if ((ret = THH_cmd_set_eq(eventp->cmd_if)) != HH_OK){
    MTL_ERROR4("%s: Failed to THH_cmd_set_eq. ret=%d\n", __func__, ret);
    MOSAL_mutex_rel(&eventp->mtx);
    MT_RETURN( HH_ERR);
  }
#else
  ret=HH_OK;
#endif
  MOSAL_mutex_rel(&eventp->mtx);

  MTL_DEBUG1("%s success: eqn=%d\n", __func__, new_eq);
  
  MT_RETURN( HH_OK);
}


/************************************************************************
 *  Function: THH_eventp_setup_mt_eq
 *  
    Arguments:
    eventp -The THH_eventp object handle 
    event_mask -Flags combination of events to map to this EQ 
    eventh -The callback handle for events over this EQ 
    priv_context -Private context to be used in callback invocation 
    max_outs_eqe -Maximum outstanding EQEs in EQ created eqn_p -Allocated EQ index 
    
    Returns:
    HH_OK 
    HH_EINVAL -Invalid handle 
    HH_EAGAIN -Not enough resources available (e.g.EQC,mem- ory,etc.). 
    HH_ERR    - internal error
    
    Description: 
    Set up an EQ for reporting events beyond IB-spec.(debug events and others). All events 
    given in the event_mask are mapped to the new EQ. 
    
    
    
 ************************************************************************/

HH_ret_t THH_eventp_setup_mt_eq(/*IN */ THH_eventp_t eventp, 
                                /*IN */ THH_eventp_mtmask_t event_mask, 
                                /*IN */ THH_mlx_eventh_t eventh, 
                                /*IN */ void *priv_context, 
                                /*IN */ MT_size_t max_outs_eqe, 
                                /*OUT*/ THH_eqn_t *eqn_p)

{
  THH_eqn_t new_eq;
  
  FUNC_IN;

  if (eventp == NULL || eventh == NULL || max_outs_eqe == 0 || event_mask == 0) {
    MTL_ERROR4("%s: NULL parameter. eventp=%p, eventh=%p, max_outs_eqe=%d, event_mask="U64_FMT"\n", __func__,
               eventp, eventh, (u_int32_t)max_outs_eqe, (u_int64_t)event_mask);
    MT_RETURN( HH_EINVAL);
  }
  
  if (MOSAL_mutex_acq(&eventp->mtx, TRUE) != MT_OK){
    MTL_ERROR4("%s: MOSAL_mutex_acq failed\n", __func__);
    MT_RETURN(HH_EINTR);
  }

  new_eq = insert_new_eq(eventp, (void*)eventh, priv_context, max_outs_eqe, EQP_MLX_EVENT);

  if (new_eq == EQP_MAX_EQS) { /* All EQs are occupied */
    MTL_ERROR4("%s: Fail in adding new EQ.\n", __func__);
    MOSAL_mutex_rel(&eventp->mtx);
    MT_RETURN( HH_EAGAIN);
  }
  
  /* map the EQ to events */
  if (map_eq(eventp,new_eq,event_mask) != HH_OK){
    MTL_ERROR4("%s: Failed to map EQ.\n", __func__);
    MOSAL_mutex_rel(&eventp->mtx);
    MT_RETURN( HH_ERR);
  }

  *eqn_p = new_eq;
  MOSAL_mutex_rel(&eventp->mtx);


  MT_RETURN( HH_OK);
}


/************************************************************************
 *  Function: 
 *  
    Arguments:
    eventp 
    eq -The EQ to replace handler for 
    eventh -The new handler 
    priv_context -Private context to be used with handler
    
    Returns:
    HH_OK 
    HH_EINVAL 
    HH_ENORSC -Given EQ is not set up 
    
    Description: 
    Replace the callback function of an EQ previously set up. This may be used 
    in order to change the handler without loosing events.It retains the EQ and 
    just replaces the callback function.All EQEs polled after a return from this 
    function will be reported to the new handler.
    
 ************************************************************************/

HH_ret_t THH_eventp_replace_handler(/*IN */ THH_eventp_t eventp, 
                                    /*IN */ THH_eqn_t eqn, 
                                    /*IN */ THH_eventp_handler_t eventh, 
                                    /*IN */ void *priv_context)

{
  HH_ret_t ret=HH_OK;
  
  FUNC_IN;

  if (eventp == NULL || NOT_VALID_EQ_NUM(eqn)){
    MTL_ERROR4("%s: Invalid parameter. eventp=%p, eq_num=%d\n", __func__,
               eventp, (u_int32_t)eqn);
    MT_RETURN( HH_EINVAL);
  }

  MOSAL_spinlock_irq_lock(&(eventp->eq_table[eqn].state_lock));
  
  if (!IS_EQ_VALID(eventp,eqn)){
    MOSAL_spinlock_unlock(&(eventp->eq_table[eqn].state_lock));
    MTL_ERROR4("%s: EQ %d is not in use.\n", __func__, (u_int32_t)eqn);
    MT_RETURN( HH_EINVAL);
  }

  switch (eventp->eq_table[eqn].eq_type)
  {
  case EQP_CQ_COMP_EVENT:
      if(eventh.comp_event_h == NULL) {
        MTL_ERROR4("%s: Invalid event handler is NULL\n", __func__);
        ret = HH_EINVAL;
      }
      else {
        eventp->eq_table[eqn].handler.comp_event_h= eventh.comp_event_h;
      }
      break;
    case EQP_IB_EVENT:
      if(eventh.ib_comp_event_h == NULL) {
        MTL_ERROR4("%s: Invalid event handler is NULL\n", __func__);
        ret = HH_EINVAL;
      }
      else {
        eventp->eq_table[eqn].handler.ib_comp_event_h= eventh.ib_comp_event_h;
      }
      break;
    case EQP_CMD_IF_EVENT:    /* no event handler in this case */
      break;
    case EQP_MLX_EVENT:
      if(eventh.mlx_event_h == NULL) {
        MTL_ERROR4("%s: Invalid event handler is NULL\n", __func__);
        ret = HH_EINVAL;
      }
      else {
        eventp->eq_table[eqn].handler.mlx_event_h= eventh.mlx_event_h;
      }
      break;
    case EQP_CATAS_ERR_EVENT:
      MTL_ERROR4("%s: Internal error: EQP_CATAS_ERR_EVENT should not get any handle\n", __func__);
      break;
    default:
      MTL_ERROR4("%s: Internal error: invalid event type.\n", __func__);
      ret= HH_ERR;
  }
  eventp->eq_table[eqn].priv_context = priv_context;
  MOSAL_spinlock_unlock(&(eventp->eq_table[eqn].state_lock));

  MT_RETURN(ret);
}

/************************************************************************
 *  Function: 
 *  
    Arguments:
    eventp -The THH_eventp object handle 
    eqn -The EQ to teardown 
    
    Returns:
    HH_OK 
    HH_EINVAL -Invalid handles (e.g.no such EQ set up) 
    
    Description: 
    This function tear down an EQ set up by one of the previous functions. Given eqn  
    which EQ to tear down. This teardown includes cleaning of any context relating to 
    the callback associated with it. 
    
    
 ************************************************************************/

HH_ret_t THH_eventp_teardown_eq(/*IN */ THH_eventp_t eventp, 
                                /*IN */ THH_eqn_t eqn )

{
  THH_cmd_status_t cmd_ret;
  HH_ret_t ret;
  unsigned long i=0;
  
  FUNC_IN;

  if (eventp == NULL || NOT_VALID_EQ_NUM(eqn)){
    MTL_ERROR4("%s: Invalid parameter. eventp=%p, eq_num=%d\n", __func__,
               eventp, (u_int32_t)eqn);
    MT_RETURN( HH_EINVAL);
  }

  
  if (MOSAL_mutex_acq(&eventp->mtx, TRUE) != MT_OK){
    MTL_ERROR4("%s: MOSAL_mutex_acq failed\n", __func__);
    MT_RETURN(HH_EINTR);
  }

  MOSAL_spinlock_irq_lock(&(eventp->eq_table[eqn].state_lock));
  if (!IS_EQ_VALID(eventp,eqn))  {  /* Given EQN is not in use ? */
    MOSAL_spinlock_unlock(&(eventp->eq_table[eqn].state_lock));  
    if (eventp->max_eq_num_used == eqn) { /* the highest EQ is removed */
      eventp->max_eq_num_used--;
    }
    MOSAL_mutex_rel(&(eventp->mtx));
    return HH_EINVAL;
  }
  SET_EQ_CLEANUP(eventp,eqn); /* Mark EQ while in the cleanup stage (disable DPC sched. from ISR) */
  MOSAL_spinlock_unlock(&(eventp->eq_table[eqn].state_lock));  
  
  /* Wait for all outstanding DPCs */
  while (eventp->eq_table[eqn].dpc_cntr)
  {
    /* this must be done for Linux only since there is no preemption in Linux */
    /* (DPC/tasklet cannot run while this context hold the CPU)               */
#ifdef __LINUX__
    schedule();
#endif
    i++;
	if (i==0xffffffff) {
      MTL_DEBUG4("%s: dpc_cntr was not zero after %lu iterations for eq_num=%d, dpc_cntr=%d\n",
                 __func__, i, eqn, eventp->eq_table[eqn].dpc_cntr);
      i=0;
      //break;
    }
  }
  
  /* for CMD_IF event need to notify the cmd_if object */
  if (eventp->eq_table[eqn].eq_type == EQP_CMD_IF_EVENT){
    if ((ret = THH_cmd_clr_eq(eventp->cmd_if)) != HH_OK){
      MTL_ERROR4("%s: Failed to THH_cmd_clr_eq. ret=%d\n", __func__, ret);
    }
  }

  /* unmap events of EQ by putting special EQ number */
  /* in case of fatal error - do not call CMD_IF */
  if (eventp->have_fatal == FALSE) {
    if ((cmd_ret = THH_cmd_MAP_EQ(eventp->cmd_if, TAVOR_IF_UNMAP_QP_BIT, eventp->eq_table[eqn].events_mask))
        != THH_CMD_STAT_OK){
      MTL_ERROR4("%s: Failed to unmap EQ events. CMD_IF error:%d.\n", __func__,cmd_ret);
    }
  }
  remove_eq(eventp,eqn);
  
  MOSAL_mutex_rel(&(eventp->mtx));
  MT_RETURN( HH_OK);

}


/************************************************************************
 *  Function: THH_eventp_notify_fatal
 *  
    Arguments:
    eventp -The THH_eventp object handle
    fatal_err - the error code of the fatal error 
    
    Return:
    HH_OK 
    HH_EINVAL -Invalid handle 
    
    Description: 
    This function is invoked by THH_hob_fatal_error to notify eventp when a fatal error
    has occurred.
    
 ************************************************************************/

HH_ret_t THH_eventp_notify_fatal ( /*IN */ THH_eventp_t eventp,
                                   /*IN */ THH_fatal_err_t fatal_err)
{
  
  FUNC_IN;
  
  if (eventp == NULL ) {
    MTL_ERROR4("%s: NULL parameter. eventp=%p\n",__func__,eventp);
    MT_RETURN( HH_EINVAL);
  }

  eventp->have_fatal = TRUE;
  MT_RETURN(HH_OK);
}

/************************************************************************
 *  Function: THH_eventp_handle_fatal
 *  
    Arguments:
    eventp -The THH_eventp object handle
    
    Return:
    HH_OK 
    HH_EINVAL -Invalid handle 
    
    Description: 
    This function is invoked by THH_hob_fatal_error t
    
    
    
    o handle the fatal error
    has occurred.
    
 ************************************************************************/

HH_ret_t THH_eventp_handle_fatal ( /*IN */ THH_eventp_t eventp)
{
  
  FUNC_IN;
  
  if (eventp == NULL ) {
    MTL_ERROR4("%s: NULL parameter. eventp=%p\n", __func__, eventp);
    MT_RETURN( HH_EINVAL);
  }

  MT_RETURN(HH_OK);
}


/********************* STATIC FUNCTIONS ************************************************/

/* This function must be invoked with eventp's mutex locked */
static THH_eqn_t insert_new_eq(THH_eventp_t     eventp,
                               void*            eventh, 
                               void             *priv_context, 
                               MT_size_t        max_outs_eqe, 
                               EQ_type_t        eq_type)
{
  unsigned int new_eq=EQP_MAX_EQS+1;
  EQP_eq_entry_t *new_entry;
  THH_internal_mr_t  params;
  THH_eqc_t eq_context;
  MT_size_t entries_num;
  THH_cmd_status_t cmd_ret;
  HH_ret_t ret=HH_OK;
  MT_bool virtual_eq = TRUE;
  VAPI_size_t alloc_mem_bytes_size;
  call_result_t rc;

  
  FUNC_IN;
  /* TK NOTE: this will not work if we have more then one EQ from the same type
     In this case we will need to change this huristic */
  switch (eq_type) {
    case EQP_CATAS_ERR_EVENT:
      new_eq = EQP_CATAS_ERR_EQN;
      break;
    case EQP_CQ_COMP_EVENT:
      new_eq = EQP_CQ_COMP_EQN;
      break;
    case EQP_IB_EVENT:
      new_eq = EQP_ASYNCH_EQN;
      break;
    case EQP_CMD_IF_EVENT:
      new_eq = EQP_CMD_IF_EQN;
      break;
    case EQP_MLX_EVENT:    /* currently not supported */
      break;
    default:
      MTL_ERROR4("%s: Internal error: invalid event queue type.\n", __func__);
      return EQP_MAX_EQS;
  }

  MTL_DEBUG3("%s:eq_type=%s; eq_num=%d \n",  __func__, eq_type_str(eq_type), new_eq);
  
  if (new_eq != EQP_MAX_EQS+1) { /* one of the above types */
    SET_EQ_INIT(eventp,new_eq);   /* reserve EQ while initializing */
  }
  else {   /* Not one of the above types */
    /* TK: this code is not realy working now since no other types of EQs are exposed */
    for (new_eq= EQP_MIN_EQ_NUM; new_eq < EQP_MAX_EQS; new_eq++) {/* Find free EQ */
      /* reserve EQ while initializing */
      MOSAL_spinlock_irq_lock(&(eventp->eq_table[new_eq].state_lock));
      if (IS_EQ_FREE(eventp,new_eq)) { /* find free entry */
        SET_EQ_INIT(eventp,new_eq);   /* reserve EQ while initializing */
        if (new_eq+1 > eventp->max_eq_num_used) {
          eventp->max_eq_num_used=new_eq+1;    /* should be done under spinlock since the intr handler use this */
        }
        MOSAL_spinlock_unlock(&(eventp->eq_table[new_eq].state_lock));
        break;
      }
      MOSAL_spinlock_unlock(&(eventp->eq_table[new_eq].state_lock));
    }
  }
  
  /* no free EQ */
  if (new_eq == EQP_MAX_EQS) {
    MTL_ERROR4("%s: All EQs are busy.\n", __func__);
    MT_RETURN( EQP_MAX_EQS);
  }
  
  MTL_DEBUG3("%s: in params: max_outs_eqe = "SIZE_T_FMT", eq_type=%s; got EQ num = %d\n", 
             __func__, max_outs_eqe, eq_type_str(eq_type), new_eq);
  new_entry = &(eventp->eq_table[new_eq]);
  /* number of outs_eqes must be power of 2 */
  entries_num = THH_CYCLIC_BUFF_SIZE(max_outs_eqe); /* already take care for the empty entry in cyclic buff */
  
#ifdef EQS_CMD_IN_DDR
  /* when EQEs in DDR we can get the alignment we need */
  alloc_mem_bytes_size = entries_num*EQ_ENTRY_SIZE;
  virtual_eq=FALSE;
  ret = THH_ddrmm_alloc(eventp->ddrmm, alloc_mem_bytes_size, floor_log2(EQ_ENTRY_SIZE), 
                       &new_entry->alloc_mem_addr_p);
  if ( ret != HH_OK ) {
    MTL_ERROR4("%s: failed to allocate ddr memory\n", __func__);
    goto err_free_eq;
  }
  new_entry->eq_buff = (void*)MOSAL_io_remap(new_entry->alloc_mem_addr_p, alloc_mem_bytes_size); 
  if ( !new_entry->eq_buff ) {
    goto err_free_alloc;
  }
  memset((void*)new_entry->eq_buff,0xff,alloc_mem_bytes_size);
#else
  /* EQS are in main memory */
  /* need to add 1 for the alignment */
  alloc_mem_bytes_size = (entries_num+1)*EQ_ENTRY_SIZE;
  /* if this is a small EQ then work with physically contiguous memory */
  if (alloc_mem_bytes_size <= PHYS_EQ_MAX_SIZE) {
    new_entry->alloc_mem_addr_p = (MT_virt_addr_t)MOSAL_pci_phys_alloc_consistent(alloc_mem_bytes_size, floor_log2(EQ_ENTRY_SIZE));
    if (new_entry->alloc_mem_addr_p != 0) {
      virtual_eq=FALSE;
      MTL_TRACE5("%s: EQ %d is going to be with physical memory. \n", __func__,new_eq);
    }
  }

  if (virtual_eq) {
    new_entry->alloc_mem_addr_p = (MT_virt_addr_t)MOSAL_pci_virt_alloc_consistent(alloc_mem_bytes_size, floor_log2(EQ_ENTRY_SIZE));
    if (!new_entry->alloc_mem_addr_p) {
      MTL_ERROR4("%s: Cannot allocate EQE buffer.\n", __func__);
      goto err_free_eq;
    }
  }
  
  /* EQEs cyclic buffer should be aligned to entry size */
  new_entry->eq_buff = (void *)MT_UP_ALIGNX_VIRT(new_entry->alloc_mem_addr_p, floor_log2(EQ_ENTRY_SIZE)); 
  memset((void*)new_entry->alloc_mem_addr_p,0xff,alloc_mem_bytes_size);
#endif


  MTL_DEBUG3("%s: real entries_num = "SIZE_T_FMT", table size= "SIZE_T_FMT"\n", __func__, entries_num,(entries_num * EQ_ENTRY_SIZE) );
  new_entry->pd = THH_RESERVED_PD;
  new_entry->priv_context = priv_context;
  new_entry->dpc_cntr= 0;
  new_entry->eventp_p = eventp;
  new_entry->virtual_eq = virtual_eq;
  new_entry->eq_type = eq_type;
  new_entry->eqn = new_eq;
  new_entry->cons_indx= 0;
  switch (eq_type)
  {
    case EQP_CQ_COMP_EVENT:
      new_entry->handler.comp_event_h = (HH_comp_eventh_t)eventh;
      break;
    case EQP_IB_EVENT:
      new_entry->handler.ib_comp_event_h = (HH_async_eventh_t)eventh;
      break;
    case EQP_CMD_IF_EVENT:    /* no event handler in this case */
      break;
    case EQP_MLX_EVENT:
      new_entry->handler.mlx_event_h = (THH_mlx_eventh_t)eventh;
      break;
    case EQP_CATAS_ERR_EVENT:
      new_entry->handler.mlx_event_h = NULL; /* no handler for catast error */
      break;
    default:
      MTL_ERROR4("%s: Internal error: invalid event queue type.\n", __func__);
      goto err_free_mem;
  }
  
  if (virtual_eq) {
    /* registering memory region for this buffer */
    memset(&params, 0, sizeof(params));
    params.start        = (IB_virt_addr_t)(MT_virt_addr_t)(new_entry->eq_buff);
    params.size         = (VAPI_size_t)entries_num * EQ_ENTRY_SIZE;
    params.pd           = THH_RESERVED_PD;
    params.vm_ctx       = MOSAL_get_kernel_prot_ctx(); //eventp->ctx_internal;
    params.force_memkey = FALSE;

    MTL_DEBUG4("%s: registering mem region. start addr="U64_FMT", size="U64_FMT"\n", __func__,
               params.start, params.size);
    if((ret = THH_mrwm_register_internal(eventp->mrwm_internal, &params, &new_entry->mem_lkey)) != HH_OK){
      MTL_ERROR4("%s: Failed to register EQ buffer in memory. ret=%d\n", __func__, ret);
      goto err_free_mem;
    }
    MTL_TRACE4("%s: SUCCESS to register EQ buffer in memory. \n", __func__);
  }
  else {
    new_entry->mem_lkey = 0;
  }
  
  /* prepare EQ for HW ownership */
  memset(&eq_context,0,sizeof(THH_eqc_t));
  eq_context.st = EQ_STATE_ARMED;

  if (eq_type == EQP_CATAS_ERR_EVENT) {
    eq_context.oi = TRUE;	/* Overrun detection ignore */
  }
  else {
    eq_context.oi = FALSE;	/* Overrun detection ignore */
  }
  
  eq_context.tr = virtual_eq;	/* Translation Required. If set - EQ access undergo address translation. */
  eq_context.owner = THH_OWNER_HW;	/* SW/HW ownership */
  eq_context.status = EQ_STATUS_OK;	/* EQ status:\;0000 - OK\;1001 - EQ overflow\;1010 - EQ write failure */
  if (virtual_eq) {
    eq_context.start_address = (u_int64_t)(MT_virt_addr_t)new_entry->eq_buff;	/* Start Address of Event Queue. Must be aligned on 32-byte boundary */
  }
  else {
#ifdef EQS_CMD_IN_DDR
    eq_context.start_address = (u_int64_t)(new_entry->alloc_mem_addr_p);
#else    
    MT_phys_addr_t pa;

    rc = MOSAL_virt_to_phys(MOSAL_get_kernel_prot_ctx(), (MT_virt_addr_t)new_entry->eq_buff, &pa);
    if ( rc != MT_OK ) {
      MTL_ERROR4(MT_FLFMT("%s: failed va=%p"), __func__, new_entry->eq_buff);
      goto err_unreg_mem;
    }
    else {
      eq_context.start_address = (u_int64_t)pa;
    }
#endif 
  }
  
  if ((ret = THH_uar_get_index(eventp->kar, &eq_context.usr_page)) != HH_OK){ /* the KAR */
    MTL_ERROR4("%s: Failed to THH_uar_get_index. ret=%d.\n", __func__,ret);
    goto err_unreg_mem;
  }
  eq_context.log_eq_size = floor_log2(entries_num);	/* Log2 of the amount of entries in the EQ */
  eq_context.intr = eventp->event_resources.intr_clr_bit;	/* Interrupt (message) to be generated to report event to INT layer.
                                                  \;0000iiii - specifies GPIO pin to be asserted
                                                  \;1jjjjjjj - specificies type of interrupt message to be generated (total 128 different messages supported). */
  eq_context.lkey = new_entry->mem_lkey;	/* Memory key (L-Key) to be used to access EQ */
  eq_context.consumer_indx = 0;	/* Contains next entry to be read upon poll for completion. Must be initialized to '0 while opening EQ */
  eq_context.producer_indx = 0;	/* Contains next entry in EQ to be written by the HCA. Must be initialized to '1 while opening EQ. */
  eq_context.pd = THH_RESERVED_PD; // TK - need to add to structure
  
  if ((cmd_ret = THH_cmd_SW2HW_EQ(eventp->cmd_if, new_eq, &eq_context)) != THH_CMD_STAT_OK){
    MTL_ERROR4("%s: Failed to move EQ to HW ownership. CMD_IF error:%d.\n", __func__,cmd_ret);
    goto err_unreg_mem;
  }
  
  MTL_TRACE4("%s: SUCCESS to THH_cmd_SW2HW_EQ EQ=%d. \n", __func__,new_eq);
  /* init DPC of this EQ */
  if (eq_type != EQP_CATAS_ERR_EVENT) {
    MOSAL_DPC_init(&new_entry->polling_dpc, eq_polling_dpc, (MT_ulong_ptr_t)new_entry,  MOSAL_SINGLE_CTX);
  }
  /* each eq use only one word (high or low) of the clr_ecr_reg 
     decide on this if EQ number is bigger then 32. 
     also prepare the mask to be used when polling the EQ in the DPC */
  
  MTL_TRACE4("%s: SUCCESS to MOSAL_DPC_init. \n", __func__);
  if (new_eq < 32) {
    new_entry->clr_ecr_addr = eventp->clr_ecr_l_base;
  }
  else {
    new_entry->clr_ecr_addr = eventp->clr_ecr_h_base;
  }
  new_entry->clr_ecr_mask = MOSAL_cpu_to_be32(1 << (new_eq % 32));

  /* this must be done last since this is the indication that the entry is valid */
  new_entry->alloc_mem_bytes_size = alloc_mem_bytes_size;
  new_entry->eq_buff_entry_num = entries_num;
  SET_EQ_VALID(eventp,new_eq);

  MT_RETURN( (THH_eqn_t)new_eq);

  /* error handling */
err_unreg_mem:
  if (virtual_eq) {
    if ((ret = THH_mrwm_deregister_mr(eventp->mrwm_internal, new_entry->mem_lkey)) != HH_OK){
      MTL_ERROR4("%s: Failed to deregister memory region. ret=%d\n", __func__,ret);
    }
  }
err_free_mem:  
#ifdef EQS_CMD_IN_DDR
  MOSAL_io_unmap((MT_virt_addr_t)new_entry->eq_buff);
err_free_alloc:
  ret = THH_ddrmm_free(eventp->ddrmm, new_entry->alloc_mem_addr_p, alloc_mem_bytes_size);
  if ( ret != HH_OK ) {
    MTL_ERROR4("%s: failed to THH_ddrmm_free\n", __func__);
  }
#else    
  if (virtual_eq) {
    MOSAL_pci_virt_free_consistent((void *)new_entry->alloc_mem_addr_p, alloc_mem_bytes_size);
  }
  else {
    MOSAL_pci_phys_free_consistent((void *)(MT_ulong_ptr_t)new_entry->alloc_mem_addr_p, alloc_mem_bytes_size);
  }
#endif
err_free_eq:
  SET_EQ_FREE(eventp,new_eq);   /* return EQ to free-pool */
  MT_RETURN(EQP_MAX_EQS);
} /* insert_new_eq */
  

/************************************************************************/


static HH_ret_t map_eq(THH_eventp_t         eventp,
                       THH_eqn_t            eqn,
                       THH_eventp_mtmask_t  tavor_mask)
{
  THH_cmd_status_t cmd_ret;

  
  FUNC_IN;
  /* save mask for later unmapping when EQ is teared down */
  eventp->eq_table[eqn].events_mask = tavor_mask;
  MTL_TRACE4("%s:  EQ=%d mask="U64_FMT" \n", __func__, eqn, (u_int64_t)tavor_mask);

  if ((cmd_ret = THH_cmd_MAP_EQ(eventp->cmd_if, eqn, tavor_mask))!= THH_CMD_STAT_OK){
    MTL_ERROR4("%s: Failed to map EQ events. CMD_IF error:%d EQN=%d.\n", __func__,cmd_ret,eqn);
    /* due to this error need to remove EQ */
    remove_eq(eventp, eqn);
    MT_RETURN( HH_ERR);
  }
  MTL_TRACE4("%s: SUCCESS to THH_cmd_MAP_EQ. EQ=%d mask="U64_FMT" \n", __func__, eqn, (u_int64_t)tavor_mask);
  MT_RETURN( HH_OK);
}


/************************************************************************/


static void remove_eq(THH_eventp_t        eventp,
                      THH_eqn_t           eqn)
{
  THH_cmd_status_t cmd_ret;
  THH_eqc_t eq_context;
  VAPI_size_t buf_sz;
#ifdef EQS_CMD_IN_DDR
  MT_phys_addr_t buf_addr;
#else
  void* buf_addr;
#endif
  MT_bool virt_buf;
  HH_ret_t ret;

  
  FUNC_IN;
  
  /* clear EQC (in SW ownership) */
  memset(&eq_context,0,sizeof(THH_eqc_t));
  
  /* in case of fatal error - do not call CMD_IF */
  if (eventp->have_fatal == FALSE) {
    if ((cmd_ret = THH_cmd_HW2SW_EQ(eventp->cmd_if, eqn, &eq_context)) != THH_CMD_STAT_OK){
      MTL_ERROR4("%s: Failed to move EQ to SW ownership. CMD_IF error:%d.\n", __func__,cmd_ret);
      /* TK - maybe we need to exit in this case */
    }
  }
  /* unregister MR of buffer and free the eq_buff */
  if (eventp->eq_table[eqn].virtual_eq) {
    ret = THH_mrwm_deregister_mr(eventp->mrwm_internal, eventp->eq_table[eqn].mem_lkey);
    if (ret != HH_OK){ 
        MTL_ERROR4("%s: Failed to deregister memory region. ret=%d\n", __func__,ret);
    }
  }
  MOSAL_spinlock_irq_lock(&(eventp->eq_table[eqn].state_lock));
  virt_buf= eventp->eq_table[eqn].virtual_eq;
#ifdef EQS_CMD_IN_DDR
  buf_addr= eventp->eq_table[eqn].alloc_mem_addr_p;
#else
  buf_addr= (void*)eventp->eq_table[eqn].alloc_mem_addr_p;
#endif
  buf_sz= eventp->eq_table[eqn].alloc_mem_bytes_size;

  eventp->eq_table[eqn].alloc_mem_addr_p= 0;
  eventp->eq_table[eqn].eq_buff_entry_num = 0; 
  eventp->eq_table[eqn].alloc_mem_bytes_size = 0;
  SET_EQ_FREE(eventp,eqn);
  MOSAL_spinlock_unlock(&(eventp->eq_table[eqn].state_lock));  

  /* Free buffer */
  
#ifdef EQS_CMD_IN_DDR
  MOSAL_io_unmap((MT_virt_addr_t)eventp->eq_table[eqn].eq_buff);
  ret = THH_ddrmm_free(eventp->ddrmm, buf_addr, buf_sz);
  if ( ret != HH_OK ) {
    MTL_ERROR4("%s: failed to THH_ddrmm_free\n", __func__);
  }
#else    
  if (virt_buf) {
    MOSAL_pci_virt_free_consistent(buf_addr, buf_sz);
  }
  else {
    MOSAL_pci_phys_free_consistent(buf_addr, buf_sz);
  }
#endif
  
  FUNC_OUT;
  return;
}


/************************************************************************************************/

HH_ret_t prepare_intr_resources(THH_eventp_t eventp)
{
  call_result_t msl_ret;

  MT_phys_addr_t cr_base = eventp->event_resources.cr_base;
  
  FUNC_IN;
  /* map both ecr & clr_ecr registers (4 words) */
  // TK: need to use the structure and do separate io remap for each register
  if ((eventp->ecr_h_base = MOSAL_io_remap(cr_base+TAVOR_ECR_H_OFFSET_FROM_CR_BASE, 4*sizeof(u_int32_t))) 
      == 0){
    MTL_ERROR1("%s: Failed to MOSAL_io_remap for ECR\n", __func__);
    MT_RETURN( HH_ERR);
  }
  eventp->ecr_l_base = eventp->ecr_h_base + 4;
  eventp->clr_ecr_h_base = eventp->ecr_h_base + 8;
  eventp->clr_ecr_l_base = eventp->ecr_h_base + 12;


  MTL_DEBUG1("%s: ECR register="VIRT_ADDR_FMT"\n", __func__, eventp->ecr_h_base );

  
  /* if interrupt bit < 32: we use the low word and otherwise the high word of the clr_int register */
  if ((eventp->intr_clr_reg = MOSAL_io_remap(cr_base + (eventp->event_resources.intr_clr_bit<32 ? 
                                     TAVOR_CLR_INT_L_OFFSET_FROM_CR_BASE : TAVOR_CLR_INT_H_OFFSET_FROM_CR_BASE),
                                     sizeof(u_int32_t))) == 0)
  {
     MTL_ERROR1("%s: Failed to MOSAL_io_remap for INTR_CLR_REG\n", __func__);
     MT_RETURN( HH_ERR);
  }
  eventp->intr_clr_mask = MOSAL_be32_to_cpu(1 << (eventp->event_resources.intr_clr_bit % 32));
  
  //MTL_DEBUG1("%s: TAVOR IRQ=%d\n", __func__, eventp->event_resources.irq);
  if ((msl_ret = MOSAL_ISR_set(&eventp->isr_obj, thh_intr_handler, eventp->event_resources.irq,
                               "InfiniHost", (MT_ulong_ptr_t)eventp)) != MT_OK){
    MTL_ERROR1("%s: Failed to MOSAL_ISR_set MOSAL ret=%d\n", __func__, msl_ret);
    MT_RETURN( HH_ERR);
  }
  MT_RETURN( HH_OK);
}


/*
***********************************************************************************************/

HH_ret_t remove_intr_resources(THH_eventp_t eventp)
{
  call_result_t msl_ret;


  FUNC_IN;
  MOSAL_io_unmap(eventp->ecr_h_base);
  MOSAL_io_unmap(eventp->intr_clr_reg);

  if ((msl_ret = MOSAL_ISR_unset(&eventp->isr_obj)) != MT_OK){
    MTL_ERROR1("%s: Failed to MOSAL_ISR_unset MOSAL ret=%d\n", __func__, msl_ret);
    MT_RETURN( HH_ERR);
  }
  MT_RETURN( HH_OK);
}

/************************************************************************************************/


static HH_ret_t add_catast_err_eq(THH_eventp_t eventp)
{
  THH_eventp_mtmask_t tavor_mask=0;
  
  FUNC_IN;
  
  if (MOSAL_mutex_acq(&eventp->mtx, TRUE) != MT_OK){

    MTL_ERROR4("%s: MOSAL_mutex_acq failed\n", __func__);
    MT_RETURN(HH_EINTR);
  }
  
  if (insert_new_eq(eventp, (void*)NULL, NULL, 4, EQP_CATAS_ERR_EVENT) == EQP_MAX_EQS) {
    MTL_ERROR4("%s: Failed to add the catastrophic event EQ.\n", __func__);
    MT_RETURN (HH_ERR);
  }

  TAVOR_IF_EV_MASK_SET(tavor_mask, TAVOR_IF_EV_MASK_LOCAL_CATAS_ERR);

  if (map_eq(eventp, EQP_CATAS_ERR_EQN, tavor_mask) != HH_OK){
    MTL_ERROR4("%s: Failed to map catastrophic error EQ.\n", __func__);
    MOSAL_mutex_rel(&eventp->mtx);
    MT_RETURN( HH_ERR);
  }
  MOSAL_mutex_rel(&eventp->mtx);
  MTL_DEBUG1("%s: Succeeded to map EQ=%d. mask="U64_FMT"\n", __func__, 
             EQP_CATAS_ERR_EQN, (u_int64_t)tavor_mask);
  MT_RETURN(HH_OK);

}

