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

#ifndef H_MOSAL_SYNC_H
#define H_MOSAL_SYNC_H

#ifdef  __cplusplus
 extern "C" {
#endif


typedef struct {
    u_int32_t sec;     /* Seconds */
    u_int32_t msec;    /* Milliseconds */
} MOSAL_time_t;



/* //////////////////////////////////// */

/* ////////////////////////////////////////////////////////////////////////////// */
/*  Synchronization object */
/* ////////////////////////////////////////////////////////////////////////////// */


/******************************************************************************
 *  Function (kernel-mode only):
 *    MOSAL_syncobj_init
 *
 *  Description:
 *    Init sync object
 *
 *  Parameters: 
 *		obj_p(IN) pointer to synch object
 *
 *  Returns:
 *    in kernel always returns MT_OK
 *    in user mode may return MT_ERROR
 *
 ******************************************************************************/
call_result_t MOSAL_syncobj_init(MOSAL_syncobj_t *obj_p);

/******************************************************************************
 *  Function (kernel-mode only):
 *    MOSAL_syncobj_free
 *
 *  Description:
 *    Destroy sync object
 *
 *  Parameters:
 *              obj_p(IN) pointer to synch object
 *
 *  Returns:
 *    MT_OK
 *    MT_ERR
 *
 ******************************************************************************/
call_result_t MOSAL_syncobj_free(MOSAL_syncobj_t *obj_p);

/******************************************************************************
 *  Function (kernel-mode only):
 *    MOSAL_syncobj_waiton
 *
 *  Description:
 *    cause process to sleep until synchonization object is signalled or time
 *    expires
 *
 *  Parameters: 
 *		obj_p(IN) pointer to synch object
 *    micro_sec(IN) max time to wait in microseconds.
 *                  MOSAL_SYNC_TIMEOUT_INFINITE = inifinite timeout
 *
 *  Returns:
 *    MT_OK - woke up by event
 *    MT_ETIMEDOUT - wokeup because of timeout
 *    MT_EINTR    - waiting for event interrupted due to a (SYSV) signal
 *
 ******************************************************************************/
call_result_t MOSAL_syncobj_waiton(MOSAL_syncobj_t *obj_p, MT_size_t micro_sec);


/******************************************************************************
 *  Function (kernel-mode only):
 *    MOSAL_syncobj_signal
 *
 *  Description:
 *    signal the synchronization object
 *
 *  Parameters: 
 *		obj_p(IN) pointer to synch object
 *
 *  Returns:
 *    N/A
 *
 ******************************************************************************/
void MOSAL_syncobj_signal(MOSAL_syncobj_t *obj_p);

/******************************************************************************
 *  Function (kernel-mode only):
 *    MOSAL_syncobj_clear
 *
 *  Description:
 *    reset sync object (i.e. bring it to init - not-signalled -state)
 *
 *  Parameters: 
 *		obj_p(IN) pointer to synch object
 *
 *  Returns:
 *
 ******************************************************************************/
void MOSAL_syncobj_clear(MOSAL_syncobj_t *obj_p);


/* ////////////////////////////////////////////////////////////////////////////// */
/*  Semaphores */
/* ////////////////////////////////////////////////////////////////////////////// */


/******************************************************************************
 *  Function (kernel-mode only):
 *    MOSAL_sem_init
 *
 *  Description:
 *    init semaphore
 *
 *  Parameters: 
 *		sem_p(OUT) pointer to semaphore to be initialized
 *    count(IN) max number of processes that can hold the semaphore at the same time
 *
 *  Returns:
 *  OK
 *  MT_EAGAIN
 *
 ******************************************************************************/
call_result_t MOSAL_sem_init(MOSAL_semaphore_t *sem_p, MT_size_t count);

/******************************************************************************
 *  Function (kernel-mode only):
 *    MOSAL_sem_free
 *
 *  Description:
 *    free semaphore
 *
 *  Parameters:
 *              sem_p(OUT) pointer to semaphore to be freed
 *
 *  Returns:
 *  OK
 *  MT_ERROR - internal error (double free)
 *
 ******************************************************************************/
call_result_t MOSAL_sem_free(MOSAL_semaphore_t *sem_p);

/******************************************************************************
 *  Function (kernel-mode only):
 *    MOSAL_sem_acq
 *
 *  Description:
 *    acquire the semaphore
 *
 *  Parameters: 
 *	  sem_p(IN) pointer to semaphore
 *    block(IN) if - FALSE, return immediately if could not acquire, otherwise block if necessary
 *
 *  Returns:
 *    MT_OK - semaphore acquired
 *    MT_EINTR - interrupted (in blocking mode)
 *    MT_EAGAIN - semaphore not acquired (only - in non-blocking mode)
 *
 *******************************************************************************/
call_result_t MOSAL_sem_acq(MOSAL_semaphore_t *sem_p, MT_bool block);


/******************************************************************************
 *  Function (kernel-mode only):
 *    MOSAL_sem_rel
 *
 *  Description:
 *    release the semaphore
 *
 *  Parameters: 
 *		sem_p(IN) pointer to semaphore
 *
 *  Returns:
 *    N/A
 *
 ******************************************************************************/
void MOSAL_sem_rel(MOSAL_semaphore_t *sem_p);


/* ////////////////////////////////////////////////////////////////////////////// */
/*  Mutexes */
/* ////////////////////////////////////////////////////////////////////////////// */


/******************************************************************************
 *  Function (kernel-mode only):
 *    MOSAL_mutex_init
 *
 *  Description:
 *    init mutex
 *
 *  Parameters: 
 *		mtx_p(OUT) pointer to mutex to be initialized
 *
 *  Returns:
 *  OK
 *  MT_EAGAIN
 *
 ******************************************************************************/
call_result_t MOSAL_mutex_init(MOSAL_mutex_t *mtx_p);

/******************************************************************************
 *  Function (kernel-mode only):
 *    MOSAL_mutex_free
 *
 *  Description:
 *    init mutex
 *
 *  Parameters: 
 *		mtx_p(OUT) pointer to mutex to be destroyed
 *
 *  Returns:
 *     OK
 *     MT_ERROR
 *
 ******************************************************************************/
call_result_t MOSAL_mutex_free(MOSAL_mutex_t *mtx_p);

/******************************************************************************
 *  Function (kernel-mode only):
 *    MOSAL_mutex_acq
 *
 *  Description:
 *    acquire the mutex
 *
 *  Parameters: 
 *	  mtx_p(IN) pointer to mutex
 *    block(IN) if - FALSE, return immediately if could not acquire, otherwise block if necessary
 *
 *  Returns:
 *    MT_OK - mutex acquired
 *    MT_EINTR - mutex not acquired (only - in non-blocking mode)
 *
 ******************************************************************************/
call_result_t MOSAL_mutex_acq(MOSAL_mutex_t *mtx_p, MT_bool block);


/******************************************************************************
 *  Function (kernel-mode only):
 *    MOSAL_mutex_rel
 *
 *  Description:
 *    release the mutex
 *
 *  Parameters: 
 *		mtx_p(IN) pointer to mutex
 *
 *  Returns:
 *    N/A
 *
 ******************************************************************************/
void MOSAL_mutex_rel(MOSAL_mutex_t *mtx_p);


/* ////////////////////////////////////////////////////////////////////////////// */
/*  Delay of execution */
/* ////////////////////////////////////////////////////////////////////////////// */

/******************************************************************************
 *  Function (kernel-mode only):
 *    MOSAL_delay_execution
 *
 *  Description:
 *    delay execution of this control path for a the specified time period. Note
 *    that in some implementaions it performs busy wait.
 *
 *  Parameters: 
 *		time_micro(IN) required delay time in microseconds
 *
 *  Returns:
 *    N/A
 *
 ******************************************************************************/
void MOSAL_delay_execution(u_int32_t time_micro);

/* ////////////////////////////////////////////////////////////////////////////// */
/*  Spinlocks */
/* ////////////////////////////////////////////////////////////////////////////// */


/**************************************************************************************************
 * Function (different for kernel and user space): MOSAL_spinlock_init
 *
 * Description: Creates a locking mechanism to allow synchronization between different processors.
 *              It is initialized to an unlocked state
 *
 * Parameters: spinlock: pointer to spinlock element.
 *
 *            
 * Returns: MT_OK
 *          MT_AGAIN: not enough resources.
 *          
 *************************************************************************************************/
call_result_t MOSAL_spinlock_init(MOSAL_spinlock_t  *sp);

/******************************************************************************
 *  Function (kernel-mode only):
 *    MOSAL_mutex_acq_ui
 *
 *  Description:
 *    acquire the mutex - uninterruptable
 *
 *  Parameters: 
 *	  mtx_p(IN) pointer to mutex
 *
 *
 ******************************************************************************/
void MOSAL_mutex_acq_ui(MOSAL_mutex_t *mtx_p);


/******************************************************************************
 *  Function (kernel-mode only):
 *    MOSAL_sem_acq_ui
 *
 *  Description:
 *    acquire the semaphore - uninterruptable
 *
 *  Parameters: 
 *	  sem_p(IN) pointer to semaphore
 *
 *******************************************************************************/
void MOSAL_sem_acq_ui(MOSAL_semaphore_t *sem_p);

/******************************************************************************
 *  Function (kernel-mode only):
 *    MOSAL_syncobj_waiton_ui
 *
 *  Description:
 *    cause process to sleep until synchonization object is signalled or time
 *    expires - uninterruptable
 *
 *  Parameters: 
 *		obj_p(IN) pointer to synch object
 *    micro_sec(IN) max time to wait in microseconds.
 *                  MOSAL_SYNC_TIMEOUT_INFINITE = inifinite timeout
 *
 *  Returns:
 *    MT_OK - woke up by event
 *    MT_ETIMEDOUT - wokeup because of timeout
 *
 ******************************************************************************/
call_result_t MOSAL_syncobj_waiton_ui(MOSAL_syncobj_t *obj_p, MT_size_t micro_sec);

/******************************************************************************
 *  Function 
 *    MOSAL_sleep:
 *
 *  Description:
 *    Suspends the execution of the current process (in Linux)/thread (in Wondows) 
 *  for the given number of seconds
 *
 *  Parameters: 
 *    sec(IN) u_int32_t - number of seconds to sleep
 *
 *  Returns:
 *	  0 - if sleep and parans were okay
 *    otherwise - if it wasn't
 *  Remarks:
 *    sec must be less that MAX_DWORD/1000
 *
 ******************************************************************************/
int MOSAL_sleep( u_int32_t sec );

/******************************************************************************
 *  Function 
 *    MOSAL_usleep:
 *
 *  Description:
 *    Suspends the execution of the current process for the given number of
 *    microseconds. The function guarantees to go to sleep for at least usec
 *    microseconds
 *  Parameters: 
 *    usec(IN) number of micro seconds to sleep
 *
 *  Returns:
 *	  MT_OK
 *    MT_EINTR signal received
 *
 ******************************************************************************/
call_result_t MOSAL_usleep(u_int32_t usec);

/******************************************************************************
 *  Function 
 *    MOSAL_usleep_ui:
 *
 *  Description:
 *    Suspends the execution of the current process for the given number of
 *    microseconds. The function guarantees to go to sleep for at least usec
 *    microseconds. The function is non interruptile
 *  Parameters: 
 *    usec(IN) number of micro seconds to sleep
 *
 *  Returns: void
 *
 ******************************************************************************/
void MOSAL_usleep_ui(u_int32_t usec);

/******************************************************************************
 *  Function 
 *    MOSAL_gettimeofday:
 *
 *  Description:
 *    retrns MOSAL_time_t struct defining the current time
 *  Parameters: 
 *    time_p(OUT) MOSAL_time_t * - current time
 *
 *
 ******************************************************************************/
void MOSAL_gettimeofday(MOSAL_time_t * time_p);






#ifdef  __cplusplus
 }
#endif


#endif
