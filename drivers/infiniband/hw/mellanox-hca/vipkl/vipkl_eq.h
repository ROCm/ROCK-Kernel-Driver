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

#ifndef H_VIPKL_EQ_H
#define H_VIPKL_EQ_H
#include <vapi.h>
#include <vip.h>


typedef u_int32_t VIPKL_EQ_hndl_t;

typedef enum {
  VIPKL_EQ_COMP_EVENTH, VIPKL_EQ_ASYNC_EVENTH
} VIPKL_EQ_cbk_type_t;

/* Item in events queue (field in union is selected based on cbk_type of the EQ_ctx) */
typedef struct {
  union {
    VAPI_completion_event_handler_t comp; /* Function to invoke */
    VAPI_async_event_handler_t async;
  } eventh; /* event handler */
  VAPI_event_record_t event_record; /* For completion event handler - only CQ modifier is valid */
  void* private_data;
} VIPKL_EQ_event_t;

/* Initialization for this module - to be invoked only once by VIPKL_init() */
void VIPKL_EQ_init(void);

void VIPKL_EQ_cleanup(void);

/* Create context for a new polling thread/process (to be invoked for each EVAPI_get_hca_hndl) */
VIP_ret_t VIPKL_EQ_new(VIP_RSCT_t usr_ctx,VAPI_hca_hndl_t hca,
                       VIPKL_EQ_cbk_type_t cbk_type,EM_async_ctx_hndl_t async_ctx, 
                       VIPKL_EQ_hndl_t *vipkl_eq_h_p);


VIP_ret_t VIPKL_EQ_del(VIP_RSCT_t usr_ctx,VAPI_hca_hndl_t hca,VIPKL_EQ_hndl_t vipkl_eq);


VIP_ret_t VIPKL_EQ_evapi_set_comp_eventh(VIP_RSCT_t usr_ctx, VAPI_hca_hndl_t hca,
  /*IN*/VIPKL_EQ_hndl_t                  vipkl_eq,
  /*IN*/CQM_cq_hndl_t                    vipkl_cq,
  /*IN*/VAPI_completion_event_handler_t  completion_handler,
  /*IN*/void *                           private_data);

VIP_ret_t VIPKL_EQ_evapi_clear_comp_eventh(VIP_RSCT_t usr_ctx,VAPI_hca_hndl_t hca,
  /*IN*/VIPKL_EQ_hndl_t                  vipkl_eq,
  /*IN*/CQM_cq_hndl_t                    vipkl_cq);

VIP_ret_t VIPKL_EQ_set_async_event_handler(VIP_RSCT_t usr_ctx,VAPI_hca_hndl_t hca,
  /*IN*/VIPKL_EQ_hndl_t                  vipkl_eq,
  /*IN*/VAPI_async_event_handler_t       async_eventh,
  /*IN*/void *                           private_data,
  /*OUT*/EVAPI_async_handler_hndl_t      *async_handler_hndl_p);

VIP_ret_t VIPKL_EQ_clear_async_event_handler(VIP_RSCT_t usr_ctx,VAPI_hca_hndl_t hca,
  /*IN*/VIPKL_EQ_hndl_t                  vipkl_eq,
  /*IN*/EVAPI_async_handler_hndl_t       async_handler_hndl);


VIP_ret_t VIPKL_EQ_poll(
    VIP_RSCT_t usr_ctx,VAPI_hca_hndl_t hca,
  /*IN*/VIPKL_EQ_hndl_t vipkl_eq,
  /*OUT*/VIPKL_EQ_event_t *eqe_p);

#endif
