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

#ifndef OBUILD_FLAG
#include "../../thh_hob/thh_hob_priv.h"
#include "../../cmdif/cmdif.h"
#else 
#include "thh_hob_priv.h"
#include "cmdif.h"
#endif



EXPORT_SYMBOL(THH_hob_get_qp1_pkey);
EXPORT_SYMBOL(THH_hob_get_num_ports);
EXPORT_SYMBOL(THH_hob_get_pkey);
EXPORT_SYMBOL(THH_hob_get_sgid);
EXPORT_SYMBOL(THH_get_debug_info);

#if defined(MAX_DEBUG) && 4 <= MAX_DEBUG
/*export these symbols only if debugging */

EXPORT_SYMBOL(THH_cmd_MODIFY_QP);
EXPORT_SYMBOL(THH_cmd_MODIFY_EE);
EXPORT_SYMBOL(THH_cmd_CONF_SPECIAL_QP);
EXPORT_SYMBOL(THH_cmd_MAD_IFC);
EXPORT_SYMBOL(THH_cmd_READ_MTT);
EXPORT_SYMBOL(THH_cmd_WRITE_MTT);

#endif

#ifdef IVAPI_THH
/* Export these symbols only for IVAPI */
EXPORT_SYMBOL(THH_hob_open_hca);
EXPORT_SYMBOL(THH_hob_close_hca);
EXPORT_SYMBOL(THH_hob_set_async_eventh);
EXPORT_SYMBOL(THH_hob_set_comp_eventh);
EXPORT_SYMBOL(THH_hob_get_gid_tbl);
EXPORT_SYMBOL(THH_hob_get_pkey_tbl);
EXPORT_SYMBOL(THH_hob_query_port_prop);
EXPORT_SYMBOL(THH_hob_query);
EXPORT_SYMBOL(THH_hob_modify);
EXPORT_SYMBOL(THH_hob_alloc_pd);
EXPORT_SYMBOL(THH_hob_free_pd);
EXPORT_SYMBOL(THH_hob_create_cq);
EXPORT_SYMBOL(THH_hob_query_cq);
EXPORT_SYMBOL(THH_hob_destroy_cq);
EXPORT_SYMBOL(THH_hob_resize_cq);
EXPORT_SYMBOL(THH_hob_create_qp);
EXPORT_SYMBOL(THH_hob_query_qp);
EXPORT_SYMBOL(THH_hob_modify_qp);
EXPORT_SYMBOL(THH_hob_destroy_qp);
EXPORT_SYMBOL(THH_hob_get_special_qp);
EXPORT_SYMBOL(THH_hob_process_local_mad);
EXPORT_SYMBOL(THH_hob_register_mr);
EXPORT_SYMBOL(THH_hob_query_mr);
EXPORT_SYMBOL(THH_hob_reregister_mr);
EXPORT_SYMBOL(THH_hob_deregister_mr);
EXPORT_SYMBOL(THH_hob_alloc_mw);
EXPORT_SYMBOL(THH_hob_free_mw);
EXPORT_SYMBOL(THH_hob_register_smr);
EXPORT_SYMBOL(THH_hob_attach_to_multicast);
EXPORT_SYMBOL(THH_hob_detach_from_multicast);
EXPORT_SYMBOL(THH_hob_free_ul_res);
EXPORT_SYMBOL(THH_hob_alloc_ul_res);
#endif
 


