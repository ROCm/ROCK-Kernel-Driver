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

#ifndef H_MOSAL_K2U_CBK_PRIV_H
#define H_MOSAL_K2U_CBK_PRIV_H

/* This "callback ID" should be used to notify polling thread of cleanup request */
#define K2U_CBK_CLEANUP_CBK_ID ((k2u_cbk_id_t)(-1))

/******************************************************************************
 *  Function(No automatic wrapper): k2u_cbk_init 
 *
 *  Description: Init private resources for k2u callback for calling process (e.g. message q).
 *
 *  Parameters:
 *    k2u_cbk_h_p (OUT) - returned handle (pointer in user-space)
 *
 *  Returns: MT_OK,
 *    MT_EAGAIN on problems with resources allocation,
 *    MT_EBUSY  if a handle is already allocated for this process (returned in k2u_cbk_h_p). 
 *
 *****************************************************************************/
call_result_t k2u_cbk_init(k2u_cbk_hndl_t *k2u_cbk_h_p);


/******************************************************************************
 *  Function(No automatic wrapper): k2u_cbk_cleanup
 *
 *  Description: Clean private resources for k2u callback for calling process.
 *
 *  Parameters:
 *    k2u_cbk_h (IN) - handle of cbk resources for this process
 *
 *
 *  Returns: MT_OK,
 *    MT_EINVAL for invalid handle (e.g. this handle is not of this process). 
 *
 *****************************************************************************/
call_result_t k2u_cbk_cleanup(k2u_cbk_hndl_t k2u_cbk_h);


/******************************************************************************
 *  Function: k2u_cbk_pollq
 *
 *  Description: Poll the message queue (will block if no message in the queue).
 *
 *  Parameters:
 *    k2u_cbk_h (IN) - handle of cbk resources for this process
 *    cbk_id_p (OUT) - Id for given callback data.
 *    data_p   (OUT) (LEN MAX_CBK_DATA_SZ) - Pointer in user-space for data buffer (of MAX_CBK_DATA_SZ bytes).
 *    size_p   (OUT) - Number of valid bytes copies to data buffer.
 *
 *  Returns: MT_OK,
 *    MT_EINVAL for invalid handle (e.g. this handle is not of this process) or NULL ptrs,
 *    MT_EAGAIN if q is empty
 *
 *****************************************************************************/
call_result_t k2u_cbk_pollq(k2u_cbk_hndl_t k2u_cbk_h,
                            k2u_cbk_id_t *cbk_id_p,void *data_p, MT_size_t *size_p);


#endif
