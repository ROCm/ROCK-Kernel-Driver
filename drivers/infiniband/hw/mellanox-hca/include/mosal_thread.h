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

#ifndef H_MOSAL_THREAD_H
#define H_MOSAL_THREAD_H

#ifdef  __cplusplus
 extern "C" {
#endif

#define MOSAL_THREAD_FLAGS_DETACHED 1  /* Create thread as detached - valid for user-space only */


/******************************************************************************
 *  Function (kernel-mode only):
 *    MOSAL_thread_start 
 *
 *  Description:
 *    create a thread and run a t-function in its context
 *    The thread is created as JOINABLE unless the flag MOSAL_THREAD_FLAGS_DETACHED is used.
 *
 *  Parameters: 
 *		mto_p(IN) 		pointer to MOSAL thread object
 *		flags(IN)			flags for thread creation	(MOSAL_THREAD_FLAGS_DETACHED)
 *		mtf(IN) 		t-function  
 *		mtf_ctx(IN) 	t-function context
 *
 *  Returns:
 *		MT_OK		- thread started; for blocking mode - t-function is running;
 *		MT_EAGAIN	- for blocking mode - timeout; thread hasn't started yet; 
 *		other		- error;
 *
 ******************************************************************************/
call_result_t MOSAL_thread_start( 
	MOSAL_thread_t *mto_p,			/*  pointer to MOSAL thread object */
	u_int32_t	flags,				/*  flags for thread creation	 */
	MOSAL_thread_func_t mtf,		/*  t-function name  */
	void *mtf_ctx					/*  t-function context (optionally)  */
 );

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
	MOSAL_thread_t *mto_p			/*  pointer to MOSAL thread object */
 );


#ifdef  __cplusplus
 }
#endif


#endif

