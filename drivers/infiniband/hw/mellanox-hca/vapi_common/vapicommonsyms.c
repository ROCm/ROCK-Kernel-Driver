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

#include <vip_array.h>
#include <vip_hash.h>
#include <vip_hashp.h>
#include <vip_hashp2p.h>
#include <vip_hash64p.h>
#include <vapi_common.h>
#include <vip_cirq.h>
#include <vip_delay_unlock.h>

MODULE_LICENSE("GPL");

EXPORT_SYMBOL(VAPI_atomic_cap_sym);
EXPORT_SYMBOL(VAPI_comp_type_sym);
EXPORT_SYMBOL(VAPI_cqe_opcode_sym);
EXPORT_SYMBOL(VAPI_cq_notif_sym);
EXPORT_SYMBOL(VAPI_event_record_sym);
EXPORT_SYMBOL(VAPI_event_syndrome_sym);
EXPORT_SYMBOL(VAPI_hca_attr_mask_sym);
EXPORT_SYMBOL(VAPI_hca_cap_sym);
EXPORT_SYMBOL(VAPI_mig_state_sym);
EXPORT_SYMBOL(VAPI_mr_change_mask_sym);
EXPORT_SYMBOL(VAPI_mrw_acl_mask_sym);
EXPORT_SYMBOL(VAPI_mrw_type_sym);
EXPORT_SYMBOL(VAPI_qp_attr_mask_sym);
EXPORT_SYMBOL(VAPI_qp_state_sym);
EXPORT_SYMBOL(VAPI_rdma_atom_acl_mask_sym);
EXPORT_SYMBOL(VAPI_remote_node_addr_sym);
EXPORT_SYMBOL(VAPI_sig_type_sym);
EXPORT_SYMBOL(VAPI_special_qp_sym);
EXPORT_SYMBOL(VAPI_strerror);
EXPORT_SYMBOL(VAPI_strerror_sym);
EXPORT_SYMBOL(VAPI_ts_type_sym);
EXPORT_SYMBOL(VAPI_wc_status_sym);
EXPORT_SYMBOL(VAPI_wr_opcode_sym);
EXPORT_SYMBOL(VIP_array_create);
EXPORT_SYMBOL(VIP_array_create_maxsize);
EXPORT_SYMBOL(VIP_array_destroy);
EXPORT_SYMBOL(VIP_array_erase);
EXPORT_SYMBOL(VIP_array_erase_prepare);
EXPORT_SYMBOL(VIP_array_erase_undo);
EXPORT_SYMBOL(VIP_array_erase_done);
EXPORT_SYMBOL(VIP_array_find);
EXPORT_SYMBOL(VIP_array_find_hold);
EXPORT_SYMBOL(VIP_array_find_release);
EXPORT_SYMBOL(VIP_array_find_release_erase);
EXPORT_SYMBOL(VIP_array_find_release_erase_prepare);
EXPORT_SYMBOL(VIP_array_get_first_handle);
EXPORT_SYMBOL(VIP_array_get_next_handle);
EXPORT_SYMBOL(VIP_array_get_first_handle_hold);
EXPORT_SYMBOL(VIP_array_get_next_handle_hold);
EXPORT_SYMBOL(VIP_array_get_num_of_objects);
EXPORT_SYMBOL(VIP_array_insert);
EXPORT_SYMBOL(VIP_array_insert2hndl);
EXPORT_SYMBOL(VIP_array_insert_ptr);
EXPORT_SYMBOL(VIP_cirq_add);
EXPORT_SYMBOL(VIP_cirq_create);
EXPORT_SYMBOL(VIP_cirq_destroy);
EXPORT_SYMBOL(VIP_cirq_empty);
EXPORT_SYMBOL(VIP_cirq_peek);
EXPORT_SYMBOL(VIP_cirq_peek_ptr);
EXPORT_SYMBOL(VIP_cirq_remove);
EXPORT_SYMBOL(VIP_cirq_stats_print);
EXPORT_SYMBOL(VIP_delay_unlock_create);
EXPORT_SYMBOL(VIP_delay_unlock_insert);
EXPORT_SYMBOL(VIP_delay_unlock_destroy);
EXPORT_SYMBOL(VIP_hash64p_create);
EXPORT_SYMBOL(VIP_hash64p_create_maxsize);
EXPORT_SYMBOL(VIP_hash64p_destroy);
EXPORT_SYMBOL(VIP_hash64p_erase);
EXPORT_SYMBOL(VIP_hash64p_find);
EXPORT_SYMBOL(VIP_hash64p_find_ptr);
EXPORT_SYMBOL(VIP_hash64p_get_num_of_objects);
EXPORT_SYMBOL(VIP_hash64p_get_num_of_buckets);
EXPORT_SYMBOL(VIP_hash64p_insert);
EXPORT_SYMBOL(VIP_hash64p_insert_ptr);
EXPORT_SYMBOL(VIP_hash64p_may_grow);
EXPORT_SYMBOL(VIP_hash64p_traverse);
EXPORT_SYMBOL(VIP_hash_create);
EXPORT_SYMBOL(VIP_hash_create_maxsize);
EXPORT_SYMBOL(VIP_hash_destroy);
EXPORT_SYMBOL(VIP_hash_erase);
EXPORT_SYMBOL(VIP_hash_find);
EXPORT_SYMBOL(VIP_hash_find_ptr);
EXPORT_SYMBOL(VIP_hash_get_num_of_objects);
EXPORT_SYMBOL(VIP_hash_get_num_of_buckets);
EXPORT_SYMBOL(VIP_hash_insert);
EXPORT_SYMBOL(VIP_hash_insert_ptr);
EXPORT_SYMBOL(VIP_hash_may_grow);
EXPORT_SYMBOL(VIP_hashp2p_create);
EXPORT_SYMBOL(VIP_hashp2p_create_maxsize);
EXPORT_SYMBOL(VIP_hashp2p_destroy);
EXPORT_SYMBOL(VIP_hashp2p_erase);
EXPORT_SYMBOL(VIP_hashp2p_find);
EXPORT_SYMBOL(VIP_hashp2p_find_ptr);
EXPORT_SYMBOL(VIP_hashp2p_get_num_of_objects);
EXPORT_SYMBOL(VIP_hashp2p_get_num_of_buckets);
EXPORT_SYMBOL(VIP_hashp2p_insert);
EXPORT_SYMBOL(VIP_hashp2p_insert_ptr);
EXPORT_SYMBOL(VIP_hashp2p_may_grow);
EXPORT_SYMBOL(VIP_hashp2p_traverse);
EXPORT_SYMBOL(VIP_hashp_create);
EXPORT_SYMBOL(VIP_hashp_create_maxsize);
EXPORT_SYMBOL(VIP_hashp_destroy);
EXPORT_SYMBOL(VIP_hashp_erase);
EXPORT_SYMBOL(VIP_hashp_find);
EXPORT_SYMBOL(VIP_hashp_find_ptr);
EXPORT_SYMBOL(VIP_hashp_get_num_of_objects);
EXPORT_SYMBOL(VIP_hashp_get_num_of_buckets);
EXPORT_SYMBOL(VIP_hashp_insert);
EXPORT_SYMBOL(VIP_hashp_insert_ptr);
EXPORT_SYMBOL(VIP_hashp_may_grow);
EXPORT_SYMBOL(VIP_hashp_traverse);
EXPORT_SYMBOL(VIP_hash_traverse);

