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

#include "mosal_priv.h"



struct mosalq_st* qhandles[MOSAL_MAX_QHANDLES] = {0};   /* q db */

/******************************************************************************************
* Name:
*               MOSAL_qdestroy_internal
* Description:
*               Do actual destroying of MOSAL queue
* Parameters:
*               qh(IN)              MOSAL queue handler
* Return value:
*               call_result
* Remarks:
*               Don't perform any checks, assume that all these checks
*               already done by caller.
*
*******************************************************************************************/
static call_result_t MOSAL_qdestroy_internal(MOSAL_qhandle_t qh);



/*
 * Queues stuff
 */

/* +++++++ DEBUG STUFF
static void print_signals(void)
{
    int i;

    MTL_TRACE4("   --- signal printout:\n");
    for(i=0; i<_NSIG_WORDS; i++)
    {
        MTL_TRACE4("   --- %2d: 0x%08lx\n", i, current->signal.sig[i]);
    }
    MTL_TRACE4("   --- signal printout end\n");
}
*/

call_result_t MOSAL_qcreate(MOSAL_qhandle_t *qh, MOSAL_data_free_t qdestroy_free)
{
    call_result_t   rc = MT_OK;
    int i;

    MTL_TRACE1("-> MOSAL_qcreate(...)\n");


    for (i= 0; i < MOSAL_MAX_QHANDLES; i++)  /* look for empty handler */
      if (qhandles[i] == NULL)  break;  /* found free */
    if (i >= MOSAL_MAX_QHANDLES)  return MT_EAGAIN; /* no free q handle */
    qhandles[i] = MALLOC(sizeof(struct mosalq_st));
    if (qhandles[i] == NULL)  return MT_EAGAIN;
#if LINUX_KERNEL_2_4
    qhandles[i]->q = MALLOC(sizeof(WAIT_QUEUE));
    if ( qhandles[i]->q == NULL ) {
      FREE(qhandles[i]);
      qhandles[i] = NULL;
      return MT_EAGAIN;
    }
    else {
      qhandles[i]->q->lock = SPIN_LOCK_UNLOCKED;
      qhandles[i]->q->task_list.prev = &qhandles[i]->q->task_list;
      qhandles[i]->q->task_list.next = &qhandles[i]->q->task_list;
    }
#else
    qhandles[i]->q = NULL;
#endif
    qhandles[i]->head = NULL;
    qhandles[i]->tail = NULL;
    qhandles[i]->is_waiting = 0;
    qhandles[i]->is_wakedup = 0;
    qhandles[i]->is_destroyed = 0;
    qhandles[i]->qdestroy_free= qdestroy_free;
    *qh = i;

    MTL_TRACE1("<- MOSAL_qcreate qh=%d rc=%d (%s)\n\n",
               *qh, rc, mtl_strerror_sym(rc));
    return rc;
}

bool MOSAL_isqempty(MOSAL_qhandle_t qh)
{
  if ((qh >= MOSAL_MAX_QHANDLES) || (qhandles[qh] == NULL)) return TRUE;  /* 
no such queue */
  return qhandles[qh]->head ? FALSE : TRUE;
}

call_result_t MOSAL_qget(MOSAL_qhandle_t qh,  int *size, void **data, bool 
block)
{
  call_result_t    rc = MT_OK;
  struct mosalq_el *qh_current;

  MTL_TRACE1("-> MOSAL_qget(0x%x, ..., %d)\n", (u_int32_t)qh, block);

  if ((qh >= MOSAL_MAX_QHANDLES) || (qhandles[qh] == NULL)) {
    rc=MT_ENORSC;  /* no such queue */
    goto lbl_error;
  }

  if (qhandles[qh]->is_destroyed) {
    MTL_TRACE4("   MOSAL_qget(0x%x, ...) - destroy request. Do it.\n",
        (u_int32_t)qh);
    MOSAL_qdestroy_internal(qh);
    rc = MT_EINTR;
    goto lbl_error;
  }

  if (!qhandles[qh]->head) {

    int ret;

    if (!block) {
      rc = MT_EAGAIN;
      goto lbl_error;
    }

    qhandles[qh]->is_waiting = 1;
    MTL_TRACE4("   MOSAL_qget(0x%x, ...) - go to sleep\n",  (u_int32_t)qh);

    /* Wait until someone puts something on the Q,
     *   or until the Q is destroyed,
     *   or until we get a signal. */

    ret=wait_event_interruptible(GET_HANDLER_Q(qh),
        ( qhandles[qh]->is_wakedup || qhandles[qh]->is_destroyed));

    MTL_TRACE4("   MOSAL_qget(0x%x, ...) - awake(%d, %d)\n",
        (u_int32_t)qh, qhandles[qh]->is_wakedup,
        qhandles[qh]->is_destroyed);

    if (ret) {
      rc = MT_EINTR;
      goto lbl_error;

    } 

    if (qhandles[qh]->is_destroyed) {
      MTL_TRACE4("   MOSAL_qget(0x%x, ...) - destroy request when waiting. Do it.\n",
          (u_int32_t)qh);
      MOSAL_qdestroy_internal(qh);
      rc = MT_EINTR;
      return rc;
    }

    if (! qhandles[qh]->is_wakedup) {
      MTL_ERROR1("   MOSAL_qget(0x%x, ...) - internal error #1\n",
          (u_int32_t)qh);
      rc = MT_ERROR;
      goto lbl_error;
    }

    qhandles[qh]->is_wakedup = 0;
    qhandles[qh]->is_waiting = 0;

    if (! qhandles[qh]->head) {
      MTL_ERROR1("   MOSAL_qget(0x%x, ...) - internal error #2\n",
          (u_int32_t)qh);
      rc = MT_ERROR;
      goto lbl_error;
    }
  }

  qh_current = (struct mosalq_el *)qhandles[qh]->head;
  *size = qh_current->size;
  *data = qh_current->data;
  qhandles[qh]->head = qh_current->next;
  FREE(qh_current);

  MTL_TRACE1("<- MOSAL_qget, size=%d rc=0 (MT_OK)\n", *size);
  return MT_OK;

lbl_error:
  MTL_TRACE1("<- MOSAL_qget, rc=%d (%s)\n", rc, mtl_strerror_sym(rc));
  return rc;
}

call_result_t MOSAL_qput(MOSAL_qhandle_t qh, int size, void *data)
{
    call_result_t    rc = MT_OK;
    struct mosalq_el *qh_current;

    MTL_TRACE1("-> MOSAL_qput(%d,%d,<data>)\n", (u_int32_t)qh, size);
    if ((qh >= MOSAL_MAX_QHANDLES) || (qhandles[qh] == NULL)) return MT_ENORSC
;  /* no such queue */

    if (qhandles[qh]->is_destroyed)
    {
        MTL_TRACE4("   MOSAL_qput(0x%x, ...) - destroy request. Do it.\n",
                   (u_int32_t)qh);
        MOSAL_qdestroy_internal(qh);
        rc = MT_EINTR;
    }
    else
    {
        qh_current = INTR_MALLOC(sizeof(struct mosalq_el));
        qh_current->next = NULL;
        qh_current->size = size;
        qh_current->data = data;
        if (qhandles[qh]->head)
        {
            MTL_TRACE4("   MOSAL_qput(0x%x) - append to end of queue\n", 
                       (u_int32_t)qh);
            qhandles[qh]->tail->next = qh_current;
            qhandles[qh]->tail = qh_current;
        }
        else
        {
            MTL_TRACE4("   MOSAL_qput(0x%x) - was empty\n", (u_int32_t)qh);
            qhandles[qh]->head = qh_current;
            qhandles[qh]->tail = qh_current;
            if (qhandles[qh]->is_waiting)
            {
                MTL_TRACE4("   MOSAL_qput(0x%x) - try to wake up\n", 
                           (u_int32_t)qh);
                qhandles[qh]->is_wakedup = 1;
                wake_up_interruptible(GET_HANDLER(qh));
            }
        }
    }

    MTL_TRACE1("<- MOSAL_qput rc=%d (%s)\n\n", rc, mtl_strerror_sym(rc));
    return rc;
}

/*
 * Internal MOSAL_qdestroy
 *
 * Assumes that all sanity checks already performed.
 */
call_result_t MOSAL_qdestroy_internal(MOSAL_qhandle_t qh)
{
    struct mosalq_el *next_el, *cur_el;

    MTL_TRACE1("-> MOSAL_qdestroy_internal(0x%x)\n", (u_int32_t)qh);
    for (cur_el=(struct mosalq_el *)qhandles[qh]->head; cur_el; cur_el = 
next_el)  {
      if ((qhandles[qh]->qdestroy_free) && (cur_el->data))
          /* Free data of possible */
          qhandles[qh]->qdestroy_free(cur_el->data);
      next_el= cur_el->next;
      FREE(cur_el);
    }
    FREE(qhandles[qh]);
    qhandles[qh]= NULL;

    MTL_TRACE1("<- MOSAL_qdestroy_internal\n\n");
    return MT_OK;
}

call_result_t MOSAL_qdestroy(MOSAL_qhandle_t qh)
{
    call_result_t    rc = MT_OK;

    MTL_TRACE1("-> MOSAL_qdestroy(0x%x)\n", (u_int32_t)qh);
    if ((qh >= MOSAL_MAX_QHANDLES) || (qhandles[qh] == NULL))
        return MT_ENORSC;  /* no such queue */

    if (qhandles[qh]->is_waiting)
    {
        qhandles[qh]->is_destroyed = 1;
        MTL_TRACE4("   MOSAL_qdestroy(0x%x) - somebody is waiting on queue. Wake it up.\n",
                   (u_int32_t)qh);
        wake_up_interruptible(GET_HANDLER(qh));
    }
    else
        rc = MOSAL_qdestroy_internal(qh);

    MTL_TRACE1("<- MOSAL_qdestroy rc=%d (%s)\n\n", rc, mtl_strerror_sym(rc));
    return rc;
}

call_result_t MOSAL_qprint(MOSAL_qhandle_t qh)
{
    call_result_t    rc = MT_OK;
    struct mosalq_el *i;

    MTL_TRACE1("-> MOSAL_qprint(0x%x):\n", (u_int32_t)qh);
    if ((qh >= MOSAL_MAX_QHANDLES) || (qhandles[qh] == NULL)) return MT_ENORSC
;  /* no such queue */
    for (i=(struct mosalq_el *)qhandles[qh]->head; i; i = i->next)
    {
        MTL_TRACE1("    %5d : \"%s\"\n", i->size, (char *)i->data);
    }

    MTL_TRACE1("<- MOSAL_qprint\n");
    return rc;
}



