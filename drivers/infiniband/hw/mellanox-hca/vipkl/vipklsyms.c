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

#include <vipkl.h>
#include "vipkl_wrap.h"
#include <vipkl_cqblk.h>
#include <vipkl_eq.h>

EXPORT_SYMBOL(VIPKL_alloc_fmr);
EXPORT_SYMBOL(VIPKL_alloc_ul_resources);
EXPORT_SYMBOL(VIPKL_attach_to_multicast);
EXPORT_SYMBOL(VIPKL_bind_async_error_handler);
EXPORT_SYMBOL(VIPKL_bind_completion_event_handler);
EXPORT_SYMBOL(VIPKL_bind_evapi_completion_event_handler);
EXPORT_SYMBOL(VIPKL_set_async_event_handler);
EXPORT_SYMBOL(VIPKL_clear_async_event_handler);
EXPORT_SYMBOL(VIPKL_set_destroy_cq_cbk);
EXPORT_SYMBOL(VIPKL_clear_destroy_cq_cbk);
EXPORT_SYMBOL(VIPKL_set_destroy_qp_cbk);
EXPORT_SYMBOL(VIPKL_clear_destroy_qp_cbk);
EXPORT_SYMBOL(VIPKL_cleanup);
EXPORT_SYMBOL(VIPKL_close_hca);
EXPORT_SYMBOL(VIPKL_create_cq);
EXPORT_SYMBOL(VIPKL_create_mr);
EXPORT_SYMBOL(VIPKL_reregister_mr);
EXPORT_SYMBOL(VIPKL_create_mw);
EXPORT_SYMBOL(VIPKL_create_pd);
EXPORT_SYMBOL(VIPKL_create_srq);
EXPORT_SYMBOL(VIPKL_create_qp);
EXPORT_SYMBOL(VIPKL_create_smr);
EXPORT_SYMBOL(VIPKL_destroy_cq);
EXPORT_SYMBOL(VIPKL_destroy_mr);
EXPORT_SYMBOL(VIPKL_destroy_mw);
EXPORT_SYMBOL(VIPKL_destroy_pd);
EXPORT_SYMBOL(VIPKL_destroy_srq);
EXPORT_SYMBOL(VIPKL_destroy_qp);
EXPORT_SYMBOL(VIPKL_detach_from_multicast);
EXPORT_SYMBOL(VIPKL_free_fmr);
EXPORT_SYMBOL(VIPKL_free_ul_resources);
EXPORT_SYMBOL(VIPKL_get_cq_props);
EXPORT_SYMBOL(VIPKL_get_hca_hndl);
EXPORT_SYMBOL(VIPKL_get_hca_id);
EXPORT_SYMBOL(VIPKL_get_hca_ul_info);
EXPORT_SYMBOL(VIPKL_get_hh_hndl);
EXPORT_SYMBOL(VIPKL_get_special_qp);
EXPORT_SYMBOL(VIPKL_list_hcas);
EXPORT_SYMBOL(VIPKL_map_fmr);
EXPORT_SYMBOL(VIPKL_resize_cq);
EXPORT_SYMBOL(VIPKL_modify_hca_attr);
EXPORT_SYMBOL(VIPKL_modify_qp);
EXPORT_SYMBOL(VIPKL_open_hca);
EXPORT_SYMBOL(VIPKL_process_local_mad);
EXPORT_SYMBOL(VIPKL_query_hca_cap);
EXPORT_SYMBOL(VIPKL_query_mr);
EXPORT_SYMBOL(VIPKL_query_mw);
EXPORT_SYMBOL(VIPKL_query_devmem);
EXPORT_SYMBOL(VIPKL_query_port_gid_tbl);
EXPORT_SYMBOL(VIPKL_query_port_pkey_tbl);
EXPORT_SYMBOL(VIPKL_query_port_prop);
EXPORT_SYMBOL(VIPKL_query_srq);
EXPORT_SYMBOL(VIPKL_query_qp);
EXPORT_SYMBOL(VIPKL_ioctl);
EXPORT_SYMBOL(VIPKL_unmap_fmr);
EXPORT_SYMBOL(VIPKL_cqblk_alloc_ctx);
EXPORT_SYMBOL(VIPKL_cqblk_free_ctx);
EXPORT_SYMBOL(VIPKL_cqblk_wait);
EXPORT_SYMBOL(VIPKL_cqblk_signal);
EXPORT_SYMBOL(VIPKL_EQ_new);
EXPORT_SYMBOL(VIPKL_EQ_del);
EXPORT_SYMBOL(VIPKL_EQ_evapi_set_comp_eventh);
EXPORT_SYMBOL(VIPKL_EQ_evapi_clear_comp_eventh);
EXPORT_SYMBOL(VIPKL_EQ_set_async_event_handler);
EXPORT_SYMBOL(VIPKL_EQ_clear_async_event_handler);
EXPORT_SYMBOL(VIPKL_EQ_poll);
EXPORT_SYMBOL(VIPKL_alloc_map_devmem);
EXPORT_SYMBOL(VIPKL_free_unmap_devmem);
EXPORT_SYMBOL(VIPKL_get_debug_info);
EXPORT_SYMBOL(VIPKL_get_rsrc_info);
EXPORT_SYMBOL(VIPKL_get_qp_rsrc_str);
EXPORT_SYMBOL(VIPKL_rel_qp_rsrc_info);
#if defined(MT_SUSPEND_QP)
EXPORT_SYMBOL(VIPKL_suspend_qp);
EXPORT_SYMBOL(VIPKL_suspend_cq);
#endif
