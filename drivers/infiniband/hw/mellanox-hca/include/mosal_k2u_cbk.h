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

#ifndef H_MOSAL_K2U_CBK_H
#define H_MOSAL_K2U_CBK_H

#include <mtl_common.h>

/* Max. num. of bytes for callback data */
#define MAX_CBK_DATA_SZ 512

/* Per process resources handle */
typedef int k2u_cbk_hndl_t;

#define INVALID_K2U_CBK_HNDL ((k2u_cbk_hndl_t)(-1))

/* Callback ID for demultiplexing of different callbacks in the same process */
typedef unsigned int k2u_cbk_id_t;

/* Generic callback */
typedef void (*k2u_cbk_t)(k2u_cbk_id_t cbk_id, void* data_p, MT_size_t size);


/******************************************************************************
 *  Function (user-space only): k2u_cbk_register
 *
 *  Description: Register a handler in the user-level generic callback agent.
 *
 *  Parameters:
 *    cbk       (IN) - Function pointer of the generic callback.
 *    cbk_hndl_p(OUT)- Callback handle for this process (per process, to be used by kernel caller)
 *    cbk_id_p  (OUT)- ID to be used when calling this callback from kernel (per this process).
 *
 *  Returns: MT_OK,
 *    MT_EAGAIN on problems with resources allocation. 
 *
 *****************************************************************************/
call_result_t k2u_cbk_register(k2u_cbk_t cbk, k2u_cbk_hndl_t *cbk_hndl_p, k2u_cbk_id_t *cbk_id_p);


/******************************************************************************
 *  Function (user-space only): k2u_cbk_deregister
 *
 *  Description: Deregister a handler in the user-level generic callback agent.
 *
 *  Parameters:
 *    cbk_id    (IN)- ID of callback to deregister.
 *
 *  Returns: MT_OK,
 *    MT_EAGAIN on problems with resources allocation. 
 *
 *****************************************************************************/
call_result_t k2u_cbk_deregister(k2u_cbk_id_t cbk_id);


/******************************************************************************
 *  Function (kernel-space only): k2u_cbk_invoke
 *
 *  Description: Invoke from kernel a registered handler in user-level.
 *
 *  Parameters:
 *    k2u_cbk_h(IN) - handle of cbk resources for process of callback to invoke.
 *    cbk_id(IN) - ID to be used when calling this callback from kernel (per this process).
 *    data_p(IN) (LEN MAX_CBK_DATA_SZ) - Pointer in kernel-space for data buffer (of up to MAX_CBK_DATA_SZ bytes).
 *                     This data must be "vmalloc"ed.
 *    size(IN) - Number of valid bytes copies to data buffer.
 *
 *  Returns: MT_OK,
 *    MT_EINVAL on invalid handle or size of data more than MAX_CBK_DATA_SZ,
 *    MT_EAGAIN on problems with resources allocation. 
 *
 *  Note: The data is not copied but given data_p is saved in the queue and vfreed 
 *        when delivered (copied) to the user-level process.
 *
 *****************************************************************************/
call_result_t k2u_cbk_invoke(k2u_cbk_hndl_t k2u_cbk_h, k2u_cbk_id_t cbk_id,
                                void *data_p, MT_size_t size);
#endif
