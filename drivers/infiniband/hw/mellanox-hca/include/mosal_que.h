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

#ifndef H_MOSAL_QUE_H
#define H_MOSAL_QUE_H


/* Message queue defs. */
typedef u_int32_t MOSAL_qhandle_t;
typedef void (*MOSAL_data_free_t)(void*);  /* prototype for queue data freeing */
#define MOSAL_MAX_QHANDLES 1024
#define NULL_MOSAL_QHANDLE 0xFFFFFFFF


/*********************************** Event queues ****************************/

/******************************************************************************
 *  Function (kernel-mode only): MOSAL_qcreate
 *
 *  Description: Create Event Queue
 *    This function creates new queue
 *
 *  Parameters:
 *    qh(OUT) MOSAL_qhandle_t  *
 *        The handle to be used in MOSAL_qget() and MOSAL_qput() and 
 *        MOSAL_qdelete
 *    qdestroy_free(IN) MOSAL_data_free_t
 *        This function is called to free all message queue left-overs
 *        when qdestroy is called while queue is not empty.
 *        Set to NULL if no free is needed (there is another mechanism for 
this data freeing)
 *
 *  Returns:
 *    call_result_t
 *      MT_OK or MT_ERROR.
 *
 *****************************************************************************/
call_result_t MOSAL_qcreate(MOSAL_qhandle_t *qh,MOSAL_data_free_t qdestroy_free);

/******************************************************************************
 *  Function (kernel-mode only): MOSAL_isqempty
 *
 *  Description:
 *      Check is it comething in queue
 *  Parameters:
 *    qh(IN) MOSAL_qhandle_t
 *        The event queue handle.
 *
 *  Returns:
 *    call_result_t
 *      TRUE  - the queue is empty
 *      FALSE - the queue isn't empty
 *
 *****************************************************************************/
MT_bool MOSAL_isqempty(MOSAL_qhandle_t qh);

/******************************************************************************
 *  Function (kernel-mode only): MOSAL_qget
 *
 *  Description:
 *      reads the next data portion from event_queue
 *  Parameters:
 *    qh(IN) MOSAL_qhandle_t
 *        The event queue handle.
 *    size(OUT) int *
 *        Actual data size
 *    maxsize(IN) int
 *        Length of the data buffer
 *    data(OUT) (LEN @maxsize) void *
 *        Data buffer to fill
 *    block(IN) MT_bool
 *        If true and queue empty, the call is blocked.
 * 
 *
 *  Returns:
 *    call_result_t
 *      MT_OK
 *      MT_EINTR
 *      MT_EAGAIN      on non-blocking access if the queue is empty
 *      MT_ERROR.
 *
 *****************************************************************************/
call_result_t MOSAL_qget(MOSAL_qhandle_t qh,  int *size, void **data, MT_bool block);

/******************************************************************************
 *  Function (kernel-mode only): MOSAL_qput
 *
 *  Description:
 *      puts the next data portion from event_queue
 *  Parameters:
 *    qh(IN) MOSAL_qhandle_t
 *        The event queue handle.
 *    size(IN) int
 *        Actual data size
 *    data(IN) (LEN @size) void *
 *        Data buffer
 * 
 *
 *  Returns:
 *    call_result_t
 *      MT_OK
 *      MT_ERROR.
 *
 *****************************************************************************/
call_result_t MOSAL_qput(MOSAL_qhandle_t qh, int size, void *data);


/******************************************************************************
 *  Function (kernel-mode only): MOSAL_qdestroy
 *
 *  Description:
 *      destroy the queue
 *
 *  Parameters:
 *    qh(IN) MOSAL_qhandle_t
 *        The event queue handle.
 *
 *  Returns:
 *    call_result_t
 *      MT_OK
 *      MT_ERROR.
 *
 *****************************************************************************/
call_result_t MOSAL_qdestroy(MOSAL_qhandle_t qh);

/******************************************************************************
 *  Function (kernel-mode only): MOSAL_qprint
 *
 *  Description:
 *      print the queue (data assumed to be a strings)
 *
 *  Parameters:
 *    qh(IN) MOSAL_qhandle_t
 *        The event queue handle.
 *
 *  Returns:
 *    call_result_t
 *      MT_OK
 *
 *****************************************************************************/
call_result_t MOSAL_qprint(MOSAL_qhandle_t qh);



#endif
