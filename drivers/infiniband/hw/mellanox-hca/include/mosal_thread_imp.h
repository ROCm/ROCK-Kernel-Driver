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

#ifndef H_MOSAL_THREAD_IMP_H
#define H_MOSAL_THREAD_IMP_H

/* MOSAL t-function */
struct MOSAL_thread;
typedef struct MOSAL_thread MOSAL_thread_t;

#ifndef __KERNEL__

#include <pthread.h>

typedef void* (*MOSAL_thread_func_t)( void * );

struct MOSAL_thread {
	pthread_t  thread;     
	MOSAL_thread_func_t		func;			/* t-function */
	void *					func_ctx;		/* t-function context */
    u_int32_t               flags;
};

/******************************************************************************
 *  Function (kernel-mode only):
 *    MOSAL_thread_wait_for_term 
 *
 *  Description:
 *    wait till the target thread exits
 *
 *  Parameters: 
 *		mto_p(IN) 		pointer to MOSAL thread object
 *		exit_code(OUT)	return code of the thread
 *
 *  Returns:
 *		MT_OK		- thread started; for blocking mode - t-function is running;
 *		MT_EAGAIN	- thread hasn't started yet; 
 *		other		- error;
 *
 ******************************************************************************/
call_result_t MOSAL_thread_wait_for_term( 
	MOSAL_thread_t *mto_p,			/*  pointer to MOSAL thread object */
	u_int32_t	*exit_code			/*  return code of the thread */
	);

#else

typedef int (*MOSAL_thread_func_t)( void * );

#define MOSAL_THREAD_VALID_HANDLE		1
#define MOSAL_THREAD_INVALID_HANDLE		0

#include <linux/sched.h>
#include <linux/kernel.h>



#ifdef CSIGNAL
  #define MOSAL_KTHREAD_CSIGNAL     CSIGNAL      /* signal mask to be sent at exit */
#endif

#ifdef CLONE_VM
#define MOSAL_KTHREAD_CLONE_VM      CLONE_VM      /* set if VM shared between processes */
#endif

#ifdef CLONE_FS
#define MOSAL_KTHREAD_CLONE_FS      CLONE_FS      /* set if fs info shared between processes */
#endif

#ifdef CLONE_FILES
#define MOSAL_KTHREAD_CLONE_FILES   CLONE_FILES      /* set if open files shared between processes */
#endif

#ifdef CLONE_SIGHAND
#define MOSAL_KTHREAD_CLONE_SIGHAND CLONE_SIGHAND      /* set if signal handlers and blocked signals shared */
#endif

#ifdef CLONE_PID
#define MOSAL_KTHREAD_CLONE_PID     CLONE_PID      /* set if pid shared */
#endif

#ifdef CLONE_PTRACE
#define MOSAL_KTHREAD_CLONE_PTRACE  CLONE_PTRACE      /* set if we want to let tracing continue on the child too */
#endif

#ifdef CLONE_VFORK
#define MOSAL_KTHREAD_CLONE_VFORK   CLONE_VFORK      /* set if the parent wants the child to wake it up on mm_release */
#endif

#ifdef CLONE_PARENT
#define MOSAL_KTHREAD_CLONE_PARENT  CLONE_PARENT      /* set if we want to have the same parent as the cloner */
#endif

#ifdef CLONE_THREAD
#define MOSAL_KTHREAD_CLONE_THREAD  CLONE_THREAD      /* Same thread group? */
#endif

#ifdef CLONE_NEWNS
#define MOSAL_KTHREAD_CLONE_NEWNS   CLONE_NEWNS      /* New namespace group? */
#endif

#ifdef CLONE_SIGNAL
#define MOSAL_KTHREAD_CLONE_SIGNAL  CLONE_SIGNAL
#endif


#ifdef CLONE_KERNEL
  #define MOSAL_KTHREAD_CLONE_FLAGS CLONE_KERNEL
#else 
  #define MOSAL_KTHREAD_CLONE_FLAGS (CLONE_VM | CLONE_FS | CLONE_FILES | CLONE_SIGNAL)
#endif

/* MOSAL thread object implementation */
#define THREAD_NAME_LEN   16

struct MOSAL_thread {
	int					th;				/*  thread handle */
	MOSAL_thread_func_t		func;			/*  t-function */
	void *					func_ctx;		/*  t-function context */
	u_int32_t				flags;			/*  flags for thread creation */
	u_int32_t				res;			/*  return code */
	MOSAL_syncobj_t			sync;			/*  sync object */
	struct task_struct *thread;
};

/******************************************************************************
 *  Function (kernel-mode only):
 *    MOSAL_thread_set_name 
 *
 *  Description:
 *    set thread name
 *
 *  Parameters: 
 *		mto_p(IN) 		pointer to MOSAL thread object
 *		name(IN)			thread name
 *
 *  Returns:
 *
 ******************************************************************************/
 void MOSAL_thread_set_name(
 	MOSAL_thread_t *mto_p,			/*  pointer to MOSAL thread object */
	char *name						/*  thread name */
	);

/******************************************************************************
 *  Function (kernel-mode only):
 *    MOSAL_thread_is_in_work 
 *
 *  Description:
 *    check, whether thread is working (i.e hasn't exited yet)
 *
 *  Parameters: 
 *		mto_p(IN) 		pointer to MOSAL thread object
 *
 *  Returns:
 *		TRUE		- in work
 *		FALSE		- exited 
 *
 ******************************************************************************/
static inline MT_bool MOSAL_thread_is_in_work( 
	MOSAL_thread_t *mto_p			/*  pointer to MOSAL thread object */
	)
{
	return (mto_p->th != MOSAL_THREAD_INVALID_HANDLE);
}

/******************************************************************************
 *  Function:
 *    MOSAL_thread_wait_for_exit 
 *
 *  Description:
 *    wait for a target thread to exit
 *
 *  Parameters: 
 *		mto_p(IN) 		pointer to MOSAL thread object
 *		micro_sec(IN)	timeout in mcs; MOSAL_SYNC_TIMEOUT_INFINITE means ENDLESS
 *		exit_code(OUT)	return code of the thread
 *
 *  Returns:
 *		MT_OK		- thread started; for blocking mode - t-function is running;
 *		MT_EAGAIN	- for blocking mode - timeout; thread hasn't started yet; 
 *		other		- error;
 *
 ******************************************************************************/
call_result_t MOSAL_thread_wait_for_exit( 
	MOSAL_thread_t *mto_p,			/*  pointer to MOSAL thread object */
	MT_size_t micro_sec,			/*  timeout in mcs; MOSAL_THREAD_WAIT_FOREVER means ENDLESS   */
	u_int32_t	*exit_code			/*  return code of the thread */
	);

/******************************************************************************
 *  Function (kernel-mode only):
 *    MOSAL_thread_wait_for_term 
 *
 *  Description:
 *    wait till the target thread exits
 *
 *  Parameters: 
 *		mto_p(IN) 		pointer to MOSAL thread object
 *		exit_code(OUT)	return code of the thread
 *
 *  Returns:
 *		MT_OK		- thread started; for blocking mode - t-function is running;
 *		MT_EAGAIN	- thread hasn't started yet; 
 *		other		- error;
 *
 ******************************************************************************/
#define MOSAL_thread_wait_for_term(mto_p,exit_code)  MOSAL_thread_wait_for_exit(mto_p,MOSAL_SYNC_TIMEOUT_INFINITE,exit_code)



#endif


#endif /* H_MOSAL_THREAD_IMP_H */
