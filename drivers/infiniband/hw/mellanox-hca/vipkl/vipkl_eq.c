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

#define C_VIPKL_EQ_C
#include <mosal.h>
#include <vapi.h>
#include <vapi_common.h>
#include <vipkl.h>
#include "vipkl_eq.h"
#include <VIP_rsct.h>
/* Maximum processes*HCAs supported */
#define MAX_EQS 2048
/* Max outstanding calls per process - additional calls will be lost */
#define LOG2_MAX_OUTS_CALLS_PER_PROC 9
#define MAX_OUTS_CALLS_PER_PROC (1<<LOG2_MAX_OUTS_CALLS_PER_PROC)
#define MAX_OUTS_CALLS_MASK (MAX_OUTS_CALLS_PER_PROC-1)
#define INVAL_ASYNC_HANDLER_HNDL VAPI_INVAL_HNDL


/* Registered callback info - to be used as private data for registered stub */
typedef struct VIP_EQ_cbk_st {
  union { /* Function to invoke */
    VAPI_completion_event_handler_t comp; 
    VAPI_async_event_handler_t async;
  } eventh; /* event handler */
  union { /* To be used on unbind of the handler */
    CQM_cq_hndl_t vipkl_cq; /* Used as handler handle for VIP_EQ_COMP_EVENTH only*/
    EVAPI_async_handler_hndl_t async_handler_hndl; /* For VIPKL_EQ_ASYNC_EVENTH only */
  } handler_handle;
  void* private_data;   /* As received from consumer level */
  struct VIP_EQ_ctx_st *eq_ctx;  /* For direct access from callback to message queue */
  struct VIP_EQ_cbk_st *next;  /* manage as linked list */
} VIP_EQ_cbk_t;

/* Process callback context (message queue and bounded callbacks) */
typedef struct VIP_EQ_ctx_st {
  VIPKL_EQ_cbk_type_t cbk_type; /* defines typed of callbacks and VIPKL_EQ_event_t entries */
  VAPI_hca_hndl_t hca; /* manage as a per HCA context -> no need to sync access to "cons" */
  VIPKL_EQ_hndl_t eq_hndl; /* Index in eq_db */
  EM_async_ctx_hndl_t async_ctx; /* For VIPKL_EQ_ASYNC_EVENTH only */
  VIP_EQ_cbk_t* cbks_list; /* For VIPKL_EQ_COMP_EVENTH_ONLY */
  MOSAL_mutex_t cbk_dat_lock;  /* protect list above */
  MOSAL_semaphore_t qsem;   /* semaphore to count outstanding messages in queue */
  VIPKL_EQ_event_t msg_q[MAX_OUTS_CALLS_PER_PROC];  /* message queue cyclic buffer */
  volatile u_int32_t prod;        /* producer index for msg_q buffer */
  volatile u_int32_t cons;        /* consumer index for msg_q buffer */
  MOSAL_pid_t pid;                /* Thread pid - to identify polling thread */
  int ref_cnt;                    /* protect resource while in use */
    /* ref_cnt should be initialized to 1 on VIPKL_EQ_new
     * On VIPKL_EQ_del it should be deceremented by 1 and removed from the table 
     * When it reaches 0 - resource should be freed using cleanup_eq_ctx() 
     */
  VIP_RSCT_rscinfo_t rsc_ctx;
} VIP_EQ_ctx_t;


/* A macro to mark the callbacks list as "closed" - i.e. avoid any further binds */
#define CBKS_LIST_CLOSED ((VIP_EQ_cbk_t*)(-1))
/* For easy "EQ_del" failure rollback */

static MOSAL_mutex_t free_entries_lock; /* Protect free entries in table below */
/* Callback data per EQ */
static VIP_EQ_ctx_t* eq_db[MAX_EQS]={NULL};
static MOSAL_spinlock_t eq_lock[MAX_EQS]; /* one per entry above */
static MT_bool entry_in_use[MAX_EQS]={FALSE}; /* one per entry above */

static void VIPKL_EQ_evapi_comp_eventh_stub(
                                                /*IN*/ VAPI_hca_hndl_t hca_hndl,
                                                /*IN*/ VAPI_cq_hndl_t cq_hndl,
                                                /*IN*/ void* private_data);

static void VIPKL_EQ_evapi_async_eventh_stub(
                                                /*IN*/ VAPI_hca_hndl_t hca_hndl,
                                                /*IN*/ VAPI_event_record_t *event_record_p, 
                                                /*IN*/ void* private_data);


static inline VIP_EQ_ctx_t* hold_eq(VIPKL_EQ_hndl_t vipkl_eq)
{
  VIP_EQ_ctx_t* held_eq_p;
  
  if (vipkl_eq > MAX_EQS)  return NULL;

  MOSAL_spinlock_lock(&(eq_lock[vipkl_eq]));
  if (eq_db[vipkl_eq] == NULL) {
    MOSAL_spinlock_unlock(&(eq_lock[vipkl_eq]));
    return NULL;
  }
  held_eq_p= eq_db[vipkl_eq]; /* save context in case removed from table after "unlock"*/
  held_eq_p->ref_cnt++;
  MOSAL_spinlock_unlock(&(eq_lock[vipkl_eq]));
  return held_eq_p;
}

#define HOLD_EQ(vipkl_eq,eq_p)                                               \
  if (!(eq_p= hold_eq(vipkl_eq))) {                                          \
    MTL_ERROR1(MT_FLFMT("%s: Invoked for invalid EQ (%d)"),__func__,vipkl_eq);   \
    return VIP_EINVAL_PARAM;                                                      \
  }                                                     

#define HOLD_EQ_SILENT(vipkl_eq,eq_p)                                        \
  if (!(eq_p= hold_eq(vipkl_eq))) {                                          \
    return VIP_EINVAL_PARAM;                                                 \
  }                                                     

static inline void release_eq(VIP_EQ_ctx_t *eq_p)
{
  int new_ref_cnt;

  MOSAL_spinlock_lock(&(eq_lock[eq_p->eq_hndl]));
  new_ref_cnt= --(eq_p->ref_cnt);
  MOSAL_spinlock_unlock(&(eq_lock[eq_p->eq_hndl]));
  if (new_ref_cnt == 0) { /* last to use */
    FREE(eq_p);
  }
}

/* Unbind and remove all associated callbacks (close cbks list before removing eq_ctx) */
/* This function is called once per eq_ctx by VIPKL_EQ_del */
static VIP_ret_t cleanup_all_cbks(VIP_EQ_ctx_t *rm_eq)
{
  VIP_EQ_cbk_t *cur_cbk_p,*next_cbk_p;
  VIP_ret_t rc= VIP_OK;

  MOSAL_mutex_acq_ui(&rm_eq->cbk_dat_lock);
  
  /*Free resources allocated in queue */
  for (cur_cbk_p= rm_eq->cbks_list; cur_cbk_p != NULL; cur_cbk_p= next_cbk_p) {
    next_cbk_p= cur_cbk_p->next;
    /* unbind handler before deleting the private context */
    switch (rm_eq->cbk_type) {
      case VIPKL_EQ_COMP_EVENTH:
        rc= VIPKL_bind_evapi_completion_event_handler(rm_eq->hca,cur_cbk_p->handler_handle.vipkl_cq,
                                                      NULL,NULL);  
        if (rc != VIP_OK) {
          MTL_ERROR1(MT_FLFMT("%s: Unexpected error: "
                          "Failed VIPKL_bind_evapi_completion_event_handler (vipkl_cq=0x%X) "
                          "during process cleanup (%s) - possible memory leak."),
                     __func__,cur_cbk_p->handler_handle.vipkl_cq,VAPI_strerror_sym(rc));
        }
        break;
      case VIPKL_EQ_ASYNC_EVENTH:
        rc= VIPKL_clear_async_event_handler(rm_eq->hca,rm_eq->async_ctx,
                                            cur_cbk_p->handler_handle.async_handler_hndl);
        if (rc != VIP_OK) {
          MTL_ERROR1(MT_FLFMT("%s: Unexpected error: "
                          "Failed VIPKL_clear_async_event_handler during process cleanup"
                          "(%s) - possible memory leak."),__func__,VAPI_strerror_sym(rc));
        }
        break;
    }
    
    if (rc == VIP_OK) FREE(cur_cbk_p);
  }

  rm_eq->cbks_list= CBKS_LIST_CLOSED; /* prevent further binds */
  MOSAL_mutex_rel(&(rm_eq->cbk_dat_lock));
  return VIP_OK;
}

/* Initialization for this module */
void VIPKL_EQ_init(void)
{
  int i;

  MOSAL_mutex_init(&free_entries_lock);
  for (i= 0; i< MAX_EQS; i++)  MOSAL_spinlock_init(&(eq_lock[i]));
}

void VIPKL_EQ_cleanup(void)
{
  VIPKL_EQ_hndl_t i;
  VIP_ret_t rc;
  call_result_t mt_rc;

  mt_rc = MOSAL_mutex_free(&free_entries_lock);    //TK - ???
  if (mt_rc != MT_OK) {
    MTL_ERROR2(MT_FLFMT("Failed MOSAL_mutex_free (%s)"),mtl_strerror_sym(mt_rc));
  }
  for (i= 0; i < MAX_EQS; i++) {
    if (eq_db[i] != NULL)  {
      if ((rc= cleanup_all_cbks(eq_db[i])) != VIP_OK) {
        MTL_ERROR1(MT_FLFMT("%s: Failed cleanup_all_cbks for EQ %d (%s)"),__func__,
                   i,VAPI_strerror_sym(rc));
      } else {
        FREE(eq_db[i]);
      }
    }
  }
}


/* Create context for a new polling thread/process */
VIP_ret_t VIPKL_EQ_new(VIP_RSCT_t usr_ctx,
                       VAPI_hca_hndl_t hca,
                       VIPKL_EQ_cbk_type_t cbk_type,
                       EM_async_ctx_hndl_t async_ctx, /* for async. event handler only */
                       VIPKL_EQ_hndl_t *vipkl_eq_h_p)
{
  VIPKL_EQ_hndl_t free_hndl= MAX_EQS;
  VIP_EQ_ctx_t *new_eq;
  VIP_RSCT_rschndl_t r_h;
  VIP_ret_t rc;
  call_result_t mt_rc;

  if ((cbk_type != VIPKL_EQ_COMP_EVENTH) && (cbk_type != VIPKL_EQ_ASYNC_EVENTH)) {
    MTL_ERROR2(MT_FLFMT("%s: Invalid EQ callback type (%d)"),__func__,cbk_type);
    return VIP_EINVAL_PARAM;
  }
  /* Allocate and initialize new callback context entrry */
  new_eq= (VIP_EQ_ctx_t *)MALLOC(sizeof(VIP_EQ_ctx_t));
  if (new_eq == NULL) {
    MTL_ERROR1("%s: Cannot allocate vmem for eq_cbk_dat_t\n", __func__);
    return VIP_EAGAIN;
  }
  new_eq->hca= hca;
  new_eq->cbk_type= cbk_type;
  if (cbk_type == VIPKL_EQ_ASYNC_EVENTH)  new_eq->async_ctx= async_ctx;
  new_eq->cbks_list= NULL;
  new_eq->cons= new_eq->prod= 0; /* init consumer-producer buffer */
  new_eq->pid= MOSAL_getpid();
  new_eq->ref_cnt= 1;
  MOSAL_sem_init(&(new_eq->qsem),0);
  MOSAL_mutex_init(&(new_eq->cbk_dat_lock));

  
  /* Insert context to handles array */

  if (MOSAL_mutex_acq(&free_entries_lock,TRUE) != MT_OK) {
    rc= VIP_EINTR;
    goto fail_mutex;
  }

  /* Find free handle */
  for (free_hndl= 0; free_hndl < MAX_EQS; free_hndl++) {
    if (!entry_in_use[free_hndl]) break;
  }
  if (free_hndl == MAX_EQS) { /* no free entry */
    MOSAL_mutex_rel(&free_entries_lock);
    MTL_ERROR4(MT_FLFMT("No resources for registering additional processes (max.=%d)"),
      MAX_EQS);
    rc= VIP_EAGAIN;
    goto fail_alloc_hndl;
  }
  
  entry_in_use[free_hndl]= TRUE; /* take entry but do not initialize yet */
  new_eq->eq_hndl= free_hndl;
  
  MOSAL_mutex_rel(&free_entries_lock);

  eq_db[free_hndl]= new_eq; /* make entry valid */
  MTL_DEBUG4("%s: allocated cbk handle %d for pid="MT_ULONG_PTR_FMT".\n",__func__,free_hndl,MOSAL_getpid());
  
  r_h.rsc_eq_hndl = free_hndl;
  rc= VIP_RSCT_register_rsc(usr_ctx,&new_eq->rsc_ctx,VIP_RSCT_EQ,r_h);
  if (rc != VIP_OK) {
    MTL_ERROR2(MT_FLFMT("%s: Failed VIP_RSCT_register_rsc (%s)"),__func__,VAPI_strerror_sym(rc));
    goto fail_rsct;
  }
  
  *vipkl_eq_h_p = free_hndl;
  return VIP_OK;

  fail_rsct:
    entry_in_use[free_hndl]= FALSE;
  fail_alloc_hndl:
  fail_mutex:
    mt_rc = MOSAL_sem_free(&(new_eq->qsem));
    if (mt_rc != MT_OK) {
      MTL_ERROR2(MT_FLFMT("Failed MOSAL_sem_free (%s)"),mtl_strerror_sym(mt_rc));
    }
    mt_rc = MOSAL_mutex_free(&(new_eq->cbk_dat_lock));
    if (mt_rc != MT_OK) {
      MTL_ERROR2(MT_FLFMT("Failed MOSAL_mutex_free (%s)"),mtl_strerror_sym(mt_rc));
    }
    FREE(new_eq);
    return rc;
}



/* This function is invoked from HOBUL_delete. Should be invoked by resources tracker. */
VIP_ret_t VIPKL_EQ_del(VIP_RSCT_t usr_ctx,VAPI_hca_hndl_t hca,VIPKL_EQ_hndl_t vipkl_eq)
{
  VIP_EQ_ctx_t *rm_eq;
  VIP_ret_t rc;
  call_result_t mt_rc;

  MTL_DEBUG4(MT_FLFMT("%s"),__func__);
  if (vipkl_eq > MAX_EQS)  return VIP_EINVAL_PARAM;
  /* We don't use HOLD_EQ() because the EQ is "held" already due to initial  *
   * ref_cnt value set by VIPKL_EQ_new                                       */
  MOSAL_spinlock_lock(&(eq_lock[vipkl_eq]));
  if (eq_db[vipkl_eq] == NULL) {
    MOSAL_spinlock_unlock(&(eq_lock[vipkl_eq]));
    MTL_ERROR2(MT_FLFMT("Invalid EQ handle (%d)"),vipkl_eq);
    return VIP_EINVAL_PARAM;
  }
  rm_eq= eq_db[vipkl_eq];
  rc = VIP_RSCT_check_usr_ctx(usr_ctx,&rm_eq->rsc_ctx);
  if (rc != VIP_OK) {
      MOSAL_spinlock_unlock(&(eq_lock[vipkl_eq]));
      if (rc == VIP_EPERM) {
          /* probably harmless -- due to process exit race condition */
          MTL_DEBUG1(MT_FLFMT("%s: invalid usr_ctx. eq handle=0x%x (%s)"),__func__,vipkl_eq,VAPI_strerror_sym(rc));
      } else {
          MTL_ERROR1(MT_FLFMT("%s: invalid usr_ctx. eq handle=0x%x (%s)"),__func__,vipkl_eq,VAPI_strerror_sym(rc));
      }
      return rc;
  } 

  eq_db[vipkl_eq]= NULL; /* prevent further accesses to this EQ context */
  
  MOSAL_spinlock_unlock(&(eq_lock[vipkl_eq]));
  
  if ((rc= cleanup_all_cbks(rm_eq)) != VIP_OK) {
    MTL_ERROR1(MT_FLFMT("%s: Failed cleanup_all_cbks for EQ %d (%s)"),__func__,
               vipkl_eq,VAPI_strerror_sym(rc));
    eq_db[vipkl_eq]= rm_eq; /* restore entry to allow deleting later */
    return rc;
  }
  
  VIP_RSCT_deregister_rsc(usr_ctx,&rm_eq->rsc_ctx,VIP_RSCT_EQ);
  /* In case the polling thread is the one holding the EQ, we should wake it up */
  MOSAL_sem_rel(&(rm_eq->qsem));   
  
  mt_rc = MOSAL_sem_free(&(rm_eq->qsem));
  if (mt_rc != MT_OK) {
    MTL_ERROR2(MT_FLFMT("Failed MOSAL_sem_free (%s)"),mtl_strerror_sym(mt_rc));
  }
  mt_rc = MOSAL_mutex_free(&(rm_eq->cbk_dat_lock));
  if (mt_rc != MT_OK) {
    MTL_ERROR2(MT_FLFMT("Failed MOSAL_mutex_free (%s)"),mtl_strerror_sym(mt_rc));
  }
  release_eq(rm_eq); /* This release is symetric to the initial value set by VIPKL_EQ_new */
  entry_in_use[vipkl_eq]= FALSE; /* Allow reuse of entry */
  
  return VIP_OK;
}

static VIP_ret_t VIPKL_EQ_set_eventh(
  /*IN*/ VIP_RSCT_t usr_ctx,VAPI_hca_hndl_t hca,
  /*IN*/ VIPKL_EQ_hndl_t                  vipkl_eq,
  /*IN*/ VIPKL_EQ_cbk_type_t                cbk_type,
  /*IN*/ VIP_EQ_cbk_t                     *cbk_info_p,
  /*OUT*/EVAPI_async_handler_hndl_t       *async_handler_hndl_p)
{
  VIP_EQ_ctx_t *eq_p;
  VIP_EQ_cbk_t *new_cbk_p;
  VIP_ret_t rc;

  HOLD_EQ(vipkl_eq,eq_p);
  switch (eq_p->cbk_type) {
    case VIPKL_EQ_COMP_EVENTH:
      if (cbk_info_p->eventh.comp == NULL)  {
        release_eq(eq_p);
        return VIP_EINVAL_PARAM;
      }
      break;
    case VIPKL_EQ_ASYNC_EVENTH:
      if (cbk_info_p->eventh.async == NULL)  {
        release_eq(eq_p);
        return VIP_EINVAL_PARAM;
      }
      break;
  }
  if (eq_p->cbk_type != cbk_type) {
    MTL_ERROR3(MT_FLFMT("%s: Invoked for the wrong callback type for given EQ"),__func__);
    return VIP_EINVAL_PARAM;
  }

  rc = VIP_RSCT_check_usr_ctx(usr_ctx,&eq_p->rsc_ctx);
  if (rc != VIP_OK) {
      release_eq(eq_p);
      MTL_ERROR1(MT_FLFMT("%s: invalid usr_ctx. eq handle=0x%x (%s)"),__func__,vipkl_eq,VAPI_strerror_sym(rc));
      return rc;
  } 

  if (MOSAL_mutex_acq(&eq_p->cbk_dat_lock,TRUE) != MT_OK) { /* protect bind from cleanup */
    release_eq(eq_p);
    return VIP_EINTR;
  }
  if (eq_p->cbks_list == CBKS_LIST_CLOSED) {
    MOSAL_mutex_rel(&(eq_p->cbk_dat_lock));
    release_eq(eq_p);
    MTL_ERROR2(MT_FLFMT("%s: Invoked for EQ %d when it was already shut down"),__func__,vipkl_eq);
    return VIP_EINVAL_PARAM;
  }

  new_cbk_p= TMALLOC(VIP_EQ_cbk_t);
  if (new_cbk_p == NULL) {
    MOSAL_mutex_rel(&(eq_p->cbk_dat_lock));
    release_eq(eq_p);
    MTL_ERROR4(MT_FLFMT("Failed allocation of VIP_EQ_cbk_t for new callback"));
    return VIP_EAGAIN;
  }
  
  memcpy(new_cbk_p,cbk_info_p,sizeof(VIP_EQ_cbk_t));
  new_cbk_p->eq_ctx= eq_p;

  switch (eq_p->cbk_type) {
    case VIPKL_EQ_COMP_EVENTH:
      rc= VIPKL_bind_evapi_completion_event_handler(eq_p->hca, new_cbk_p->handler_handle.vipkl_cq,
                                                    VIPKL_EQ_evapi_comp_eventh_stub,new_cbk_p);
      break;
    case VIPKL_EQ_ASYNC_EVENTH:
      rc= VIPKL_set_async_event_handler(eq_p->hca,eq_p->async_ctx,
                                        VIPKL_EQ_evapi_async_eventh_stub,new_cbk_p,
                                        &new_cbk_p->handler_handle.async_handler_hndl);
      if (rc == VIP_OK) *async_handler_hndl_p= new_cbk_p->handler_handle.async_handler_hndl;
      break;
  }

  if (rc != VIP_OK) {
    MOSAL_mutex_rel(&(eq_p->cbk_dat_lock));
    release_eq(eq_p);
    MTL_ERROR2(MT_FLFMT("%s: Failed %s (%s)"),__func__,
               eq_p->cbk_type == VIPKL_EQ_COMP_EVENTH ? "VIPKL_bind_evapi_completion_event_handler":
                                                      "VIPKL_set_async_event_handler", 
               VAPI_strerror_sym(rc));
    FREE(new_cbk_p);
    return rc;
  }
    
  /* Insert to list as first */
  new_cbk_p->next= eq_p->cbks_list;
  eq_p->cbks_list= new_cbk_p;
  MOSAL_mutex_rel(&(eq_p->cbk_dat_lock));
  release_eq(eq_p);

  return VIP_OK;
}

VIP_ret_t VIPKL_EQ_clear_eventh(
  VIP_RSCT_t usr_ctx,VAPI_hca_hndl_t hca,
  /*IN*/VIPKL_EQ_hndl_t                  vipkl_eq,
  /*IN*/CQM_cq_hndl_t                    vipkl_cq,  /* OR */
  /*IN*/EVAPI_async_handler_hndl_t       async_handler_hndl
)
{
  VIP_EQ_ctx_t *eq_p;
  VIP_EQ_cbk_t *cur_cbk_p,*prev_cbk_p,*rm_cbk_p;
  VIP_ret_t rc;
  
  HOLD_EQ(vipkl_eq,eq_p);

  rc = VIP_RSCT_check_usr_ctx(usr_ctx,&eq_p->rsc_ctx);
  if (rc != VIP_OK) {
      release_eq(eq_p);
      MTL_ERROR1(MT_FLFMT("%s: invalid usr_ctx. eq handle=0x%x (%s)"),__func__,vipkl_eq,VAPI_strerror_sym(rc));
      return rc;
  } 

  if (MOSAL_mutex_acq(&eq_p->cbk_dat_lock,TRUE) != MT_OK) { /* protect bind from cleanup */
    release_eq(eq_p);
    return VIP_EINTR;
  }
  for (prev_cbk_p= NULL, rm_cbk_p= NULL, cur_cbk_p= eq_p->cbks_list; 
       cur_cbk_p != NULL;   /* while not end of list and not found */
       prev_cbk_p=cur_cbk_p , cur_cbk_p= cur_cbk_p->next) {
    switch (eq_p->cbk_type) {
      case VIPKL_EQ_COMP_EVENTH:
        if (cur_cbk_p->handler_handle.vipkl_cq == vipkl_cq)  rm_cbk_p= cur_cbk_p; /* found */
        break;
      case VIPKL_EQ_ASYNC_EVENTH:
        if (cur_cbk_p->handler_handle.async_handler_hndl == async_handler_hndl) rm_cbk_p= cur_cbk_p;
        break;
    }
    if (rm_cbk_p != NULL)  break; /* we cannot "break" directly from a switch statement... */
    /*We cannot wait for loop conditiion evaluation because we want to retain current prev_cbk_p*/
  }
  if (rm_cbk_p == NULL) {
    MOSAL_mutex_rel(&(eq_p->cbk_dat_lock));
    release_eq(eq_p);
    MTL_ERROR4(MT_FLFMT("Completion handler not found"));
    return VIP_EINVAL_PARAM;
  }
  
  if (prev_cbk_p == NULL) { /* Removing first */
    eq_p->cbks_list= rm_cbk_p->next;
  } else {
    prev_cbk_p->next= rm_cbk_p->next;
  }
  
  switch (eq_p->cbk_type) {
    case VIPKL_EQ_COMP_EVENTH:
      rc= VIPKL_bind_evapi_completion_event_handler(eq_p->hca,vipkl_cq,NULL,NULL);  /* unbind */
      break;
    case VIPKL_EQ_ASYNC_EVENTH:
      rc= VIPKL_clear_async_event_handler(eq_p->hca,eq_p->async_ctx,async_handler_hndl);
      break;
  }
  MOSAL_mutex_rel(&(eq_p->cbk_dat_lock));
  release_eq(eq_p);
  if (rc != VIP_OK) { 
    MTL_ERROR2(MT_FLFMT("%s: Unexpected error: Failed unbinding event handler (%s)"
                        " --> possible memory leak !"),__func__,VAPI_strerror_sym(rc));
    return rc;
  } 
  
  /* After invoking VIPKL_bind_evapi_completion_event_handler/VIPKL_clear_async_event_handler
     we are sure no more invocations are made with removed context
   */
  FREE(rm_cbk_p);
  return VIP_OK;
}


VIP_ret_t VIPKL_EQ_evapi_set_comp_eventh(VIP_RSCT_t usr_ctx,VAPI_hca_hndl_t hca,
  /*IN*/VIPKL_EQ_hndl_t                  vipkl_eq,
  /*IN*/CQM_cq_hndl_t                    vipkl_cq,
  /*IN*/VAPI_completion_event_handler_t  completion_handler,
  /*IN*/void *                           private_data)
{
  VIP_EQ_cbk_t new_cbk_info;

  new_cbk_info.eventh.comp= completion_handler;
  new_cbk_info.handler_handle.vipkl_cq= vipkl_cq;
  new_cbk_info.private_data= private_data;
  
  return VIPKL_EQ_set_eventh(usr_ctx,hca,vipkl_eq,VIPKL_EQ_COMP_EVENTH, &new_cbk_info,NULL);
}

VIP_ret_t VIPKL_EQ_evapi_clear_comp_eventh(VIP_RSCT_t usr_ctx,VAPI_hca_hndl_t hca,
  /*IN*/VIPKL_EQ_hndl_t                  vipkl_eq,
  /*IN*/CQM_cq_hndl_t                    vipkl_cq)
{
  return VIPKL_EQ_clear_eventh(usr_ctx,hca,vipkl_eq,vipkl_cq,INVAL_ASYNC_HANDLER_HNDL);
}

VIP_ret_t VIPKL_EQ_set_async_event_handler(VIP_RSCT_t usr_ctx,VAPI_hca_hndl_t hca,
  /*IN*/VIPKL_EQ_hndl_t                  vipkl_eq,
  /*IN*/VAPI_async_event_handler_t       async_eventh,
  /*IN*/void *                           private_data,
  /*OUT*/EVAPI_async_handler_hndl_t      *async_handler_hndl_p)
{
  VIP_EQ_cbk_t new_cbk_info;

  new_cbk_info.eventh.async= async_eventh;
  new_cbk_info.private_data= private_data;

  return VIPKL_EQ_set_eventh(usr_ctx,hca,vipkl_eq,VIPKL_EQ_ASYNC_EVENTH,&new_cbk_info,
                             async_handler_hndl_p);
}

VIP_ret_t VIPKL_EQ_clear_async_event_handler(VIP_RSCT_t usr_ctx,VAPI_hca_hndl_t hca,
  /*IN*/VIPKL_EQ_hndl_t                  vipkl_eq,
  /*IN*/EVAPI_async_handler_hndl_t       async_handler_hndl
  )
{
  return VIPKL_EQ_clear_eventh(usr_ctx,hca,vipkl_eq,VAPI_INVAL_HNDL,async_handler_hndl);
}


/* This function is assumed to be invoked by a single (cbk_polling) thread, per VIPKL_EQ_hndl_t) */
VIP_ret_t VIPKL_EQ_poll(VIP_RSCT_t usr_ctx,VAPI_hca_hndl_t hca,VIPKL_EQ_hndl_t vipkl_eq,VIPKL_EQ_event_t *eqe_p)
{
  VIP_EQ_ctx_t *eq_p;
  call_result_t mt_rc;
  VIP_ret_t rc = VIP_OK;

  HOLD_EQ_SILENT(vipkl_eq,eq_p);

  rc = VIP_RSCT_check_usr_ctx(usr_ctx,&eq_p->rsc_ctx);
  if (rc != VIP_OK) {
    release_eq(eq_p);
    if (rc == VIP_EPERM) {
        /* probably harmless -- due to process exit race condition */
        MTL_DEBUG1(MT_FLFMT("%s: invalid usr_ctx. eq handle=0x%x (%s)"),__func__,vipkl_eq,VAPI_strerror_sym(rc));
    } else {
        MTL_ERROR1(MT_FLFMT("%s: invalid usr_ctx. eq handle=0x%x (%s)"),__func__,vipkl_eq,VAPI_strerror_sym(rc));
    }
    return rc;
  } 

  mt_rc= MOSAL_sem_acq(&(eq_p->qsem),TRUE);
  if (mt_rc != MT_OK) {
    release_eq(eq_p);
    MTL_DEBUG4(MT_FLFMT("MOSAL_sem_acq for qsem was interrupted."));
    return VIP_EINTR;
  }
  
  
  /* Empty queue ? Must be due to invocation of VIPKL_EQ_del_eq */
  if (eq_p->cons == eq_p->prod) {
    release_eq(eq_p);
    return VIP_EAGAIN;
  }

  memcpy(eqe_p, eq_p->msg_q + eq_p->cons, sizeof(VIPKL_EQ_event_t));
  eq_p->cons= (eq_p->cons+1) & MAX_OUTS_CALLS_MASK;
  release_eq(eq_p);
  return VIP_OK;  
}


/* Since only the DPC of a single HCA will invoke this function per message queue
 * we need no spinlock to protect producer index. 
 * (handles are already validated via private context)
 * (Callback context is protected by the handler unbind flow - 
 *                            see VIPKL_EQ_evapi_clear_comp_eventh)
 */
static void VIPKL_EQ_evapi_comp_eventh_stub(
                                                /*IN*/ VAPI_hca_hndl_t hca_hndl,
                                                /*IN*/ VAPI_cq_hndl_t cq_hndl,
                                                /*IN*/ void* private_data)
{
  VIP_EQ_cbk_t *cbk_p= (VIP_EQ_cbk_t*)private_data;
  VIP_EQ_ctx_t *eq_p= cbk_p->eq_ctx;
  u_int32_t next_prod; /* next producer index */
  VIPKL_EQ_event_t *eqe_p;

  next_prod= (eq_p->prod+1) & MAX_OUTS_CALLS_MASK;
  if (next_prod == eq_p->cons) {
    MTL_ERROR4("%s: Call queue of process "MT_ULONG_PTR_FMT" is full - invocation is dropped.\n", 
               __func__,eq_p->pid);
    return;
  }
  eqe_p= eq_p->msg_q + eq_p->prod;
  eqe_p->eventh.comp= cbk_p->eventh.comp;
  eqe_p->event_record.modifier.cq_hndl= cq_hndl;
  eqe_p->private_data= cbk_p->private_data;
  eq_p->prod= next_prod;
  MOSAL_sem_rel(&(eq_p->qsem));   /* signal for new message in queue */
}

/* same as above */
static void VIPKL_EQ_evapi_async_eventh_stub(
                                                /*IN*/ VAPI_hca_hndl_t hca_hndl,
                                                /*IN*/ VAPI_event_record_t *event_record_p, 
                                                /*IN*/ void* private_data)
{
  VIP_EQ_cbk_t *cbk_p= (VIP_EQ_cbk_t*)private_data;
  VIP_EQ_ctx_t *eq_p= cbk_p->eq_ctx;
  u_int32_t next_prod; /* next producer index */
  VIPKL_EQ_event_t *eqe_p;

  next_prod= (eq_p->prod+1) & MAX_OUTS_CALLS_MASK;
  if (next_prod == eq_p->cons) {
    MTL_ERROR4("%s: Call queue of process "MT_ULONG_PTR_FMT" is full - invocation is dropped.\n", 
               __func__,eq_p->pid);
    return;
  }
  eqe_p= eq_p->msg_q + eq_p->prod;
  eqe_p->eventh.async= cbk_p->eventh.async;
  memcpy(&eqe_p->event_record,event_record_p,sizeof(VAPI_event_record_t));
  eqe_p->private_data= cbk_p->private_data;
  eq_p->prod= next_prod;
  MOSAL_sem_rel(&(eq_p->qsem));   /* signal for new message in queue */
}

