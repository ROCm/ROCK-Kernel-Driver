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

#include <linux/module.h>
MODULE_LICENSE("GPL");

#include <hhul.h>
#include <vapi.h>
#include <evapi.h>
#include <epool.h>

EXPORT_SYMBOL(epool_alloc);
EXPORT_SYMBOL(epool_cleanup);
EXPORT_SYMBOL(epool_free);
EXPORT_SYMBOL(epool_init);
EXPORT_SYMBOL(epool_reserve);
EXPORT_SYMBOL(epool_unreserve);
EXPORT_SYMBOL(EVAPI_alloc_map_devmem);
EXPORT_SYMBOL(EVAPI_alloc_fmr);
EXPORT_SYMBOL(EVAPI_alloc_pd);
EXPORT_SYMBOL(EVAPI_alloc_pd_sqp);
EXPORT_SYMBOL(EVAPI_clear_comp_eventh);
EXPORT_SYMBOL(EVAPI_free_unmap_devmem);
EXPORT_SYMBOL(EVAPI_free_fmr);
EXPORT_SYMBOL(EVAPI_get_hca_hndl);
EXPORT_SYMBOL(EVAPI_get_priv_context4cq);
EXPORT_SYMBOL(EVAPI_get_priv_context4qp);
EXPORT_SYMBOL(EVAPI_k_clear_comp_eventh);
EXPORT_SYMBOL(EVAPI_k_get_cq_hndl);
EXPORT_SYMBOL(EVAPI_k_get_qp_hndl);
EXPORT_SYMBOL(EVAPI_k_modify_qp);
EXPORT_SYMBOL(EVAPI_k_set_comp_eventh);
EXPORT_SYMBOL(EVAPI_k_sync_qp_state);
EXPORT_SYMBOL(EVAPI_list_hcas);
EXPORT_SYMBOL(EVAPI_map_fmr);
EXPORT_SYMBOL(EVAPI_peek_cq);
EXPORT_SYMBOL(EVAPI_poll_cq_block);
EXPORT_SYMBOL(EVAPI_poll_cq_unblock);
EXPORT_SYMBOL(EVAPI_post_inline_sr);
EXPORT_SYMBOL(EVAPI_post_gsi_sr);
EXPORT_SYMBOL(EVAPI_post_rr_list);
EXPORT_SYMBOL(EVAPI_post_sr_list);
EXPORT_SYMBOL(EVAPI_process_local_mad);
EXPORT_SYMBOL(EVAPI_release_hca_hndl);
EXPORT_SYMBOL(EVAPI_set_comp_eventh);
EXPORT_SYMBOL(EVAPI_set_priv_context4cq);
EXPORT_SYMBOL(EVAPI_set_priv_context4qp);
EXPORT_SYMBOL(EVAPI_unmap_fmr);
EXPORT_SYMBOL(HHUL_alloc_hca_hndl);
EXPORT_SYMBOL(HHUL_ifops_tbl_set_enosys);
EXPORT_SYMBOL(VAPI_alloc_mw);
EXPORT_SYMBOL(VAPI_alloc_pd);
EXPORT_SYMBOL(VAPI_attach_to_multicast);
EXPORT_SYMBOL(VAPI_bind_mw);
EXPORT_SYMBOL(VAPI_close_hca);
EXPORT_SYMBOL(EVAPI_close_hca);
EXPORT_SYMBOL(VAPI_create_addr_hndl);
EXPORT_SYMBOL(VAPI_create_cq);
EXPORT_SYMBOL(VAPI_create_srq);
EXPORT_SYMBOL(VAPI_create_qp);
EXPORT_SYMBOL(VAPI_create_qp_ext);
EXPORT_SYMBOL(VAPI_dealloc_mw);
EXPORT_SYMBOL(VAPI_dealloc_pd);
EXPORT_SYMBOL(VAPI_deregister_mr);
EXPORT_SYMBOL(VAPI_destroy_addr_hndl);
EXPORT_SYMBOL(VAPI_destroy_cq);
EXPORT_SYMBOL(VAPI_destroy_srq);
EXPORT_SYMBOL(VAPI_destroy_qp);
EXPORT_SYMBOL(VAPI_detach_from_multicast);
EXPORT_SYMBOL(VAPI_get_special_qp);
EXPORT_SYMBOL(VAPI_modify_addr_hndl);
EXPORT_SYMBOL(VAPI_modify_hca_attr);
EXPORT_SYMBOL(VAPI_modify_srq);
EXPORT_SYMBOL(VAPI_modify_qp);
EXPORT_SYMBOL(VAPI_open_hca);
EXPORT_SYMBOL(EVAPI_open_hca);
EXPORT_SYMBOL(VAPI_poll_cq);
EXPORT_SYMBOL(VAPI_post_srq);
EXPORT_SYMBOL(VAPI_post_rr);
EXPORT_SYMBOL(VAPI_post_sr);
EXPORT_SYMBOL(VAPI_query_addr_hndl);
EXPORT_SYMBOL(VAPI_query_cq);
EXPORT_SYMBOL(VAPI_query_hca_cap);
EXPORT_SYMBOL(VAPI_query_hca_gid_tbl);
EXPORT_SYMBOL(VAPI_query_hca_pkey_tbl);
EXPORT_SYMBOL(VAPI_query_hca_port_prop);
EXPORT_SYMBOL(VAPI_query_mr);
EXPORT_SYMBOL(VAPI_query_mw);
EXPORT_SYMBOL(VAPI_query_srq);
EXPORT_SYMBOL(VAPI_query_qp);
EXPORT_SYMBOL(VAPI_query_qp_ext);
EXPORT_SYMBOL(EVAPI_query_devmem);
EXPORT_SYMBOL(VAPI_register_mr);
EXPORT_SYMBOL(VAPI_register_smr);
EXPORT_SYMBOL(VAPI_req_comp_notif);
EXPORT_SYMBOL(EVAPI_req_ncomp_notif);
EXPORT_SYMBOL(VAPI_reregister_mr);
EXPORT_SYMBOL(VAPI_resize_cq);
EXPORT_SYMBOL(VAPI_set_async_event_handler);
EXPORT_SYMBOL(VAPI_set_comp_event_handler);
EXPORT_SYMBOL(EVAPI_set_async_event_handler);
EXPORT_SYMBOL(EVAPI_clear_async_event_handler);
EXPORT_SYMBOL(EVAPI_k_set_destroy_cq_cbk);
EXPORT_SYMBOL(EVAPI_k_clear_destroy_cq_cbk);
EXPORT_SYMBOL(EVAPI_k_set_destroy_qp_cbk);
EXPORT_SYMBOL(EVAPI_k_clear_destroy_qp_cbk);
#if defined(MT_SUSPEND_QP)
EXPORT_SYMBOL(EVAPI_suspend_qp);
EXPORT_SYMBOL(EVAPI_suspend_cq);
#endif

