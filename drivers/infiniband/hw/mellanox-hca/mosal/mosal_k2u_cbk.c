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

#define C_K2U_CBK_K_C

#include <linux/kernel.h>
#include <linux/autoconf.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/sched.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <linux/linkage.h>
#include <linux/smp_lock.h>
#include <mosal.h>
#include "mosal_priv.h"
#include "mosal_k2u_cbk_priv.h"

/* Maximum processes supported */
#define MAX_PROCS 256
/* Max outstanding calls per process - additional calls will be lost */
#define LOG2_MAX_OUTS_CALLS_PER_PROC 7
#define MAX_OUTS_CALLS_PER_PROC (1<<LOG2_MAX_OUTS_CALLS_PER_PROC)
#define MAX_OUTS_CALLS_MASK (MAX_OUTS_CALLS_PER_PROC-1)

/* message queue format */
typedef struct {
  k2u_cbk_id_t id;
  void *data_p;
  MT_size_t size;
}
cbk_msg_t;

/* Process callback data */
typedef struct {
  MOSAL_semaphore_t qsem;   /* semaphore to count outstanding messages in queue */
  cbk_msg_t msg_q[MAX_OUTS_CALLS_PER_PROC];  /* message queue cyclic buffer */
  volatile u_int32_t prod;        /* producer index for msg_q buffer */
  volatile u_int32_t cons;        /* consumer index for msg_q buffer */
  struct mm_struct *mm;     /* mm of the process that owns the object */
  unsigned int ref_cnt;     /* reference count of consumers of this data structure */
}
proc_cbk_dat_t;


static MOSAL_spinlock_t cbk_dat_lock;  /* Protect structured below */
/* Callback data per process (handles) */
static proc_cbk_dat_t* cbk_db[MAX_PROCS]={NULL};
k2u_cbk_hndl_t next_start_seek=0;


static void k2u_cbk_rsrc_cleanup(k2u_cbk_hndl_t k2u_cbk_h);

/* Initialization for this module */
call_result_t MOSAL_k2u_cbk_mod_init(void)
{
  MOSAL_spinlock_init(&cbk_dat_lock);
  return MT_OK;
}


call_result_t k2u_cbk_init(k2u_cbk_hndl_t *k2u_cbk_h_p)
{
  k2u_cbk_hndl_t free_hndl, i;
  proc_cbk_dat_t *new_proc;

  if ( !k2u_cbk_h_p ) return MT_EINVAL;
  /* Allocate and initialize new callback context entrry */
  new_proc= MALLOC(sizeof(proc_cbk_dat_t));
  if ( !new_proc ) {
    MTL_ERROR1(MT_FLFMT("%s: Cannot allocate memory for proc_cbk_dat_t"), __func__);
    return MT_EAGAIN;
  }
  new_proc->cons = new_proc->prod = 0; /* init consumer-producer buffer */
  new_proc->mm = current->mm;
  new_proc->ref_cnt = 1;
  MOSAL_sem_init(&new_proc->qsem, 0);

  /* Insert context to handles array */
  MOSAL_spinlock_irq_lock(&cbk_dat_lock);

  /* Find free handle */
  for (i=0, free_hndl= next_start_seek; i < MAX_PROCS; i++, free_hndl++) {
    free_hndl = free_hndl % MAX_PROCS;
    if (cbk_db[free_hndl] == NULL) break;
  }
  if (i == MAX_PROCS) { /* no free entry */
    MOSAL_spinlock_unlock(&cbk_dat_lock);
    FREE(new_proc);
    MTL_ERROR4(MT_FLFMT("No resources for registering additional processes (max.=%d)"), MAX_PROCS);
    return MT_EAGAIN;
  }
  
  cbk_db[free_hndl] = new_proc;
  
  MOSAL_spinlock_unlock(&cbk_dat_lock);

  MTL_DEBUG4(MT_FLFMT("%s: allocated cbk handle %d for pid=%d"), __func__, free_hndl, current->pid);
  
  *k2u_cbk_h_p = free_hndl;
  return MT_OK;
}


/*
 * k2u_cbk_cleanup
 */
call_result_t k2u_cbk_cleanup(k2u_cbk_hndl_t k2u_cbk_h)
{
  MOSAL_spinlock_irq_lock(&cbk_dat_lock);
  if ( (k2u_cbk_h>=MAX_PROCS) ||
       (k2u_cbk_h==INVALID_K2U_CBK_HNDL) ||
       (cbk_db[k2u_cbk_h]==NULL) ) {
    MOSAL_spinlock_unlock(&cbk_dat_lock);
    MTL_ERROR1(MT_FLFMT("%s: called with invalid handle"), __func__);
    return MT_EINVAL;
  }
  if ( current->mm ) {
    if ( current->mm != cbk_db[k2u_cbk_h]->mm ) {
      MTL_ERROR1(MT_FLFMT("%s: this proccess("MT_ULONG_PTR_FMT") has no permission for this operation"), __func__, MOSAL_getpid());
      return MT_EPERM;
    }
  }
  else {
    MTL_DEBUG1(MT_FLFMT("%s: ref_cnt=%d"), __func__, cbk_db[k2u_cbk_h]->ref_cnt);
  }
  MOSAL_spinlock_unlock(&cbk_dat_lock);

  if ( current->mm ) {
    k2u_cbk_invoke(k2u_cbk_h, K2U_CBK_CLEANUP_CBK_ID, NULL, 0);
  }

  k2u_cbk_rsrc_cleanup(k2u_cbk_h);
  return MT_OK;
}


/*
 *  k2u_cbk_rsrc_cleanup
 */
static void k2u_cbk_rsrc_cleanup(k2u_cbk_hndl_t k2u_cbk_h)
{
  proc_cbk_dat_t *cbk_p = NULL;

  MTL_TRACE3(MT_FLFMT("%s called with handle=%d"), __func__, k2u_cbk_h);
  MOSAL_spinlock_irq_lock(&cbk_dat_lock);
  cbk_db[k2u_cbk_h]->ref_cnt--;
  
  if ( cbk_db[k2u_cbk_h]->ref_cnt == 0 ) {
    cbk_p = cbk_db[k2u_cbk_h];
    cbk_db[k2u_cbk_h] = NULL;
  }
  MOSAL_spinlock_unlock(&cbk_dat_lock);
  if ( cbk_p ) {
    FREE(cbk_p);
    next_start_seek = (k2u_cbk_h+1) % MAX_PROCS;
  }
}


/*
 *  k2u_cbk_pollq
 */
call_result_t k2u_cbk_pollq(k2u_cbk_hndl_t k2u_cbk_h,
                            k2u_cbk_id_t *cbk_id_p,
                            void *data_p,
                            MT_size_t *size_p)
{
  cbk_msg_t *msg_p;


  if ( (k2u_cbk_h>MAX_PROCS) || (k2u_cbk_h==INVALID_K2U_CBK_HNDL) ) {
    return MT_EINVAL;
  }

  MOSAL_spinlock_irq_lock(&cbk_dat_lock);
  /* check validity of the handle */
  if ( cbk_db[k2u_cbk_h] == NULL ) {
    MOSAL_spinlock_unlock(&cbk_dat_lock);
    MTL_DEBUG4(MT_FLFMT("%s: handle does not exist(%d)"), __func__, k2u_cbk_h);
    return MT_ENORSC;
  }


  /* check permission */
  if (cbk_db[k2u_cbk_h]->mm != current->mm) {
    MTL_ERROR1(MT_FLFMT("%s: process("MT_ULONG_PTR_FMT") has no permission for polling"), __func__, MOSAL_getpid());
    MOSAL_spinlock_unlock(&cbk_dat_lock);
    return MT_EPERM;
  }

  /* increment reference count to guarantee the life of the object while we use it */
  cbk_db[k2u_cbk_h]->ref_cnt++;
  MTL_DEBUG3(MT_FLFMT("%s: ref_cnt=%d"), __func__, cbk_db[k2u_cbk_h]->ref_cnt);

  MOSAL_spinlock_unlock(&cbk_dat_lock);

  /* wait for event */
  if (MOSAL_sem_acq(&(cbk_db[k2u_cbk_h]->qsem), TRUE) != MT_OK) {
    MTL_DEBUG4(MT_FLFMT("MOSAL_sem_acq for qsem was interrupted."));
    k2u_cbk_rsrc_cleanup(k2u_cbk_h);
    return MT_EINTR;
  }

  MOSAL_spinlock_irq_lock(&cbk_dat_lock);

  MTL_DEBUG5(MT_FLFMT("%s: cbk_db[%u]->msg_q=%p cbk_db[%u]->cons=%u  cbk_db[%u]->prod=%u"), __func__, 
    k2u_cbk_h, cbk_db[k2u_cbk_h]->msg_q, 
    k2u_cbk_h, cbk_db[k2u_cbk_h]->cons, 
    k2u_cbk_h, cbk_db[k2u_cbk_h]->prod);

  if (cbk_db[k2u_cbk_h]->cons == cbk_db[k2u_cbk_h]->prod) {
    /* false alarm - queue is empty */
    MOSAL_spinlock_unlock(&cbk_dat_lock);
    MTL_ERROR2(MT_FLFMT("Passed queue semaphore but found no message (hndl=%d, pid=%d)"), k2u_cbk_h, current->pid);
    k2u_cbk_rsrc_cleanup(k2u_cbk_h);
    return MT_EAGAIN;
  }

  msg_p = cbk_db[k2u_cbk_h]->msg_q + cbk_db[k2u_cbk_h]->cons;

  cbk_db[k2u_cbk_h]->cons = (cbk_db[k2u_cbk_h]->cons+1) & MAX_OUTS_CALLS_MASK;
  MOSAL_spinlock_unlock(&cbk_dat_lock);

  *cbk_id_p = msg_p->id;
  *size_p = msg_p->size;
  if ( msg_p->size > 0 ) {
    memcpy(data_p, msg_p->data_p, msg_p->size);
    FREE(msg_p->data_p);
  }

  k2u_cbk_rsrc_cleanup(k2u_cbk_h);

  return MT_OK;  
}


/*
 *  k2u_cbk_invoke
 */
call_result_t k2u_cbk_invoke(k2u_cbk_hndl_t k2u_cbk_h, k2u_cbk_id_t cbk_id, void *data_p, MT_size_t size)
{
  cbk_msg_t *msg_p;
  u_int32_t next_prod; /* next producer index */

  if ( ((size>0)&&(data_p==NULL)) || (size>MAX_CBK_DATA_SZ) ) return MT_EINVAL;

  MOSAL_spinlock_irq_lock(&cbk_dat_lock);
  
  if ( (k2u_cbk_h>=MAX_PROCS) ||
       (k2u_cbk_h==INVALID_K2U_CBK_HNDL) ||
       (cbk_db[k2u_cbk_h]==NULL) ) {
    MOSAL_spinlock_unlock(&cbk_dat_lock);
    MTL_ERROR4(MT_FLFMT("%s: Invalid handle (%d)"), __func__, k2u_cbk_h);
    return MT_EINVAL;
  }

  MTL_TRACE4(MT_FLFMT("%s: called for cbk_hndl=%d cbk_id=%d data size=0x"SIZE_T_FMT), __func__, k2u_cbk_h, cbk_id,size);

  next_prod = (cbk_db[k2u_cbk_h]->prod+1) & MAX_OUTS_CALLS_MASK;
  if ( next_prod == cbk_db[k2u_cbk_h]->cons ) {
    MOSAL_spinlock_unlock(&cbk_dat_lock);
    MTL_ERROR4(MT_FLFMT("%s: Call queue is full - invocation is dropped"), __func__);
    return MT_EAGAIN;
  }
  msg_p = cbk_db[k2u_cbk_h]->msg_q+cbk_db[k2u_cbk_h]->prod;
  msg_p->id= cbk_id;
  msg_p->data_p= data_p;
  msg_p->size= size;
  cbk_db[k2u_cbk_h]->prod= next_prod;
  MOSAL_sem_rel(&(cbk_db[k2u_cbk_h]->qsem));   /* signal for new message in queue */
  MTL_TRACE1(MT_FLFMT("%s: signaled process for a new event: event id=%d"), __func__, cbk_id);
  
  MOSAL_spinlock_unlock(&cbk_dat_lock);
  
  return MT_OK;
}
