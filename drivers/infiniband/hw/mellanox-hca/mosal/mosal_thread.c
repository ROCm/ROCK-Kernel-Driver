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

#define THREAD_WAIT_FOR_EXIT		0x80000000

/******************************************************************************
 *  Function (kernel-mode only):
 *    MOSAL_thread_start 
 *
 *  Description:
 *    create a tread and run a t-function in its context
 *
 *  Parameters: 
 *		mto_p(IN) 		pointer to MOSAL thread object
 *		flags(IN)				// flags for thread creation	
 *		mtf(IN) 		t-function  
 *		mtf_ctx(IN) 	t-function context
 *
 *  Returns:
 *		MT_OK		- thread started; for blocking mode - t-function is running;
 *		MT_EAGAIN	- for blocking mode - timeout; thread hasn't started yet; 
 *		other		- error;
 *
 ******************************************************************************/
static int ThreadProc(void * lpParameter )
{
	MOSAL_thread_t *mto_p = (MOSAL_thread_t *)lpParameter; 

#if LINUX_KERNEL_2_6
  daemonize("ThreadProc");
#else
	daemonize();
#endif
	mto_p->thread = current;
	mto_p->th = MOSAL_THREAD_VALID_HANDLE;
	mto_p->res = mto_p->func(mto_p->func_ctx);
	MOSAL_syncobj_signal( &mto_p->sync );
	mto_p->th = MOSAL_THREAD_INVALID_HANDLE;
	mto_p->thread = NULL;
	return 0;
}

call_result_t MOSAL_thread_start( 
	MOSAL_thread_t *mto_p,			// pointer to MOSAL thread object
	u_int32_t	flags,				// flags for thread creation	
	MOSAL_thread_func_t mtf,		// t-function name 
	void *mtf_ctx					// t-function context (optionally) 
 )
{
	
	// sanity checks
	if (mtf == NULL)
		return MT_EINVAL;

	// init thread object
	mto_p->func 		= mtf;
	mto_p->func_ctx 	= mtf_ctx;
	mto_p->flags 		= 0;
	MOSAL_syncobj_init( &mto_p->sync );
	mto_p->th = MOSAL_THREAD_INVALID_HANDLE;

	// create and run the thread
	kernel_thread(ThreadProc, mto_p, flags );
	
	return MT_OK;
}	


/******************************************************************************
 *  Function (kernel-mode only):
 *    MOSAL_thread_kill 
 *
 *  Description:
 *    terminate the tread brutally
 *
 *  Parameters: 
 *		mto_p(IN) 		pointer to MOSAL thread object
 *
 *  Returns:
 *		MT_OK		- thread terminated
 *		MT_ERROR	- a failure on thread termination
 *
 ******************************************************************************/
call_result_t MOSAL_thread_kill( 
	MOSAL_thread_t *mto_p			// pointer to MOSAL thread object
 )
 {
	return MT_ERROR;
 }

/******************************************************************************
 *  Function (kernel-mode only):
 *    MOSAL_thread_wait_for_exit 
 *
 *  Description:
 *    create a tread and run a t-function in its context
 *
 *  Parameters: 
 *		mto_p(IN) 		pointer to MOSAL thread object
 *		micro_sec(IN)	timeout in mcs; MOSAL_THREAD_WAIT_FOREVER means ENDLESS
 *		exit_code(OUT)	return code of the thread
 *
 *  Returns:
 *		MT_OK		- thread started; for blocking mode - t-function is running;
 *		MT_EAGAIN	- for blocking mode - timeout; thread hasn't started yet; 
 *		other		- error;
 *
 ******************************************************************************/
call_result_t MOSAL_thread_wait_for_exit( 
	MOSAL_thread_t *mto_p,			// pointer to MOSAL thread object
	MT_size_t micro_sec,			// timeout in mcs; MOSAL_THREAD_WAIT_FOREVER means ENDLESS  
	u_int32_t	*exit_code			// return code of the thread
	)
 {
	call_result_t status;
	mto_p->flags |= THREAD_WAIT_FOR_EXIT;
	status = MOSAL_syncobj_waiton(&mto_p->sync, micro_sec);
	if (exit_code != NULL) {
		if (status == MT_OK )
			*exit_code = mto_p->res;
		else
			*exit_code = status;
	}
	return status;
 }

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
 	MOSAL_thread_t *mto_p,			// pointer to MOSAL thread object
	char *name						// thread name
	)
{
	if (MOSAL_thread_is_in_work(mto_p))	{
		strcpy(mto_p->thread->comm, name);
	}
}


