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
#include <linux/delay.h>

////////////////////////////////////////////////////////////////////////////////
// Synchronization object
////////////////////////////////////////////////////////////////////////////////

/*
 *  MOSAL_syncobj_init
 */
call_result_t MOSAL_syncobj_init(MOSAL_syncobj_t *obj_p)
{
  init_waitqueue_head(&obj_p->wq);
  obj_p->signalled = FALSE;
  return MT_OK;
}


/*
 *  MOSAL_syncobj_waiton
 */
call_result_t MOSAL_syncobj_waiton(MOSAL_syncobj_t *obj_p, MT_size_t micro_sec)
{
  long os_ticks, remain;
  
  if ( micro_sec == MOSAL_SYNC_TIMEOUT_INFINITE ) {
    if (wait_event_interruptible(obj_p->wq, obj_p->signalled) == 0)  return MT_OK;
    return MT_EINTR;
  }

  os_ticks = micro_sec / (1000000/HZ) + 1;
  remain = os_ticks;
	if (!obj_p->signalled ) {         /* If not already signalled */
    wait_queue_t __wait;
    init_waitqueue_entry(&__wait, current);
    add_wait_queue(&obj_p->wq, &__wait);
    set_current_state(TASK_INTERRUPTIBLE);
    if ( (! obj_p->signalled ) && (!signal_pending(current)) ) {
      remain = schedule_timeout(os_ticks);
    }
    current->state = TASK_RUNNING;
    remove_wait_queue(&obj_p->wq, &__wait);
  }

  if ( remain == 0 ) return MT_ETIMEDOUT;
 

  if ( obj_p->signalled ) {
    return MT_OK;
  }
  else {
    return MT_EINTR;
  }
}



/*
 *  MOSAL_syncobj_waiton_ui
 */
call_result_t MOSAL_syncobj_waiton_ui(MOSAL_syncobj_t *obj_p, MT_size_t micro_sec)
{
  long os_ticks;
  
  if ( micro_sec == MOSAL_SYNC_TIMEOUT_INFINITE ) {
    wait_event(obj_p->wq, obj_p->signalled);
    return MT_OK;
  }

  os_ticks = micro_sec / (1000000/HZ) + 1;
	if (!obj_p->signalled ) {         /* If not already signalled */
    wait_queue_t __wait;
    init_waitqueue_entry(&__wait, current);
    add_wait_queue(&obj_p->wq, &__wait);
    set_current_state(TASK_UNINTERRUPTIBLE);
    if ( !obj_p->signalled ) {
      schedule_timeout(os_ticks);
    }
    else {
      current->state = TASK_RUNNING;
    }
    remove_wait_queue(&obj_p->wq, &__wait);
    if ( obj_p->signalled ) {
      return MT_OK;
    }
    return MT_ETIMEDOUT;
  }
  else {
    return MT_OK;
  }
}


/*
 *  MOSAL_syncobj_signal
 */
void MOSAL_syncobj_signal(MOSAL_syncobj_t *obj_p)
{
  obj_p->signalled = TRUE;
  wake_up_all(&obj_p->wq);
}


/*
 *  MOSAL_syncobj_clear
 */
void MOSAL_syncobj_clear(MOSAL_syncobj_t *obj_p)
{
  obj_p->signalled = FALSE;
}



////////////////////////////////////////////////////////////////////////////////
// Semaphores
////////////////////////////////////////////////////////////////////////////////

/*
 *  MOSAL_sem_init
 */
call_result_t MOSAL_sem_init(MOSAL_semaphore_t *sem_p, MT_size_t count)
{
  sema_init(sem_p, count);
  return MT_OK;
}


/*
 *  MOSAL_sem_acq
 */
call_result_t MOSAL_sem_acq(MOSAL_semaphore_t *sem_p, MT_bool block)
{
  int rc;

  if ( block ) {
    rc = down_interruptible(sem_p);
    return rc==0 ? MT_OK : MT_EINTR;
  }
  else {
    rc = down_trylock(sem_p);
    return rc==0 ? MT_OK : MT_EAGAIN;
  }
}



/*
 *  MOSAL_sem_acq_ui
 */
void MOSAL_sem_acq_ui(MOSAL_semaphore_t *sem_p)
{
  down(sem_p);
}

/*
 *  MOSAL_sem_rel
 */
void MOSAL_sem_rel(MOSAL_semaphore_t *sem_p)
{
  up(sem_p);
}

////////////////////////////////////////////////////////////////////////////////
// Mutexes
////////////////////////////////////////////////////////////////////////////////
/*
 *  MOSAL_mutex_init
 */
call_result_t MOSAL_mutex_init(MOSAL_mutex_t *mtx_p)
{
  sema_init(mtx_p, 1);
  return MT_OK;
}


/*
 *  MOSAL_mutex_acq
 */
call_result_t MOSAL_mutex_acq(MOSAL_mutex_t *mtx_p, MT_bool block)
{
  int rc;

  if ( block ) {
    rc = down_interruptible(mtx_p);
  }
  else {
    rc = down_trylock(mtx_p);
  }
  return rc==0 ? MT_OK : MT_EINTR;
}


/*
 *  MOSAL_mutex_acq_ui
 */
void MOSAL_mutex_acq_ui(MOSAL_mutex_t *mtx_p)
{
  down(mtx_p);
}


/*
 *  MOSAL_mutex_rel
 */
void MOSAL_mutex_rel(MOSAL_mutex_t *mtx_p)
{
  up(mtx_p);
}


////////////////////////////////////////////////////////////////////////////////
// Delay of execution
////////////////////////////////////////////////////////////////////////////////

/*
 *  MOSAL_delay_execution
 */
void MOSAL_delay_execution(u_int32_t time_micro)
{
  if ( time_micro <= 1000 ) {
    udelay(time_micro);
    return;
  }
  if ( time_micro < 10000 ) {
    u_int32_t mili, micro;

    mili = time_micro/1000;
    micro = time_micro - mili*1000;
    mdelay(mili);
    udelay(micro);
    return;
  }
  {
    u_int32_t ticks;

    ticks = time_micro/(1000000/HZ);
    set_current_state(TASK_UNINTERRUPTIBLE);
    schedule_timeout(ticks);
    return;
  }
}


/*
 *  MOSAL_usleep
 */
call_result_t MOSAL_usleep(u_int32_t usec)
{
  unsigned int ticks, usec_per_tick;
  int rc;

  if ( usec == 0 ) {
    usec = 1; /* make sure we wait at lease 1 jiffies */
  }
  usec_per_tick = 1000000/HZ;

  ticks = usec / usec_per_tick;
  if ( usec % usec_per_tick ) {
    ticks++;
  }

  set_current_state(TASK_INTERRUPTIBLE);
  rc = schedule_timeout(ticks);
  if ( rc == 0 ) {
    return MT_OK;
  }
  return MT_EINTR;
}


/*
 *  MOSAL_usleep_ui
 */
void MOSAL_usleep_ui(u_int32_t usec)
{
  unsigned int ticks, usec_per_tick;

  if ( usec == 0 ) {
    usec = 1; /* make sure we wait at lease 1 jiffies */
  }
  usec_per_tick = 1000000/HZ;

  ticks = usec / usec_per_tick;
  if ( usec % usec_per_tick ) {
    ticks++;
  }

  set_current_state(TASK_UNINTERRUPTIBLE);
  schedule_timeout(ticks);
}

////////////////////////////////////////////////////////////////////////////////
// Spinlocks
////////////////////////////////////////////////////////////////////////////////

call_result_t MOSAL_spinlock_init(MOSAL_spinlock_t  *sp)
{
    spin_lock_init(&(sp->lock));
    return MT_OK;
}




