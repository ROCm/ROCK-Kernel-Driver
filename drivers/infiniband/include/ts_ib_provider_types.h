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

  Copyright (c) 2004 Topspin Communications.  All rights reserved.

  $Id: ts_ib_provider_types.h 32 2004-04-09 03:57:42Z roland $
*/

#ifndef _TS_IB_PROVIDER_TYPES_H
#define _TS_IB_PROVIDER_TYPES_H

#if defined(__KERNEL__)
#  ifndef W2K_OS
#    include <linux/types.h>
#  endif
#else
#  ifndef W2K_OS
#    include <stdint.h>
#  endif
#endif

#ifdef W2K_OS
#include <all/common/include/w2k.h>
#endif

#include "ts_ib_magic.h"
#include "ts_ib_core_types.h"
#include "ts_ib_mad_types.h"

#include "ts_kernel_hash.h"

#include <linux/spinlock.h>
#include <linux/list.h>

/*
  Compatibility typedefs.  Just use the "struct ib_xxx *" name in new
  code, leave these typedefs for legacy code.
*/
typedef struct ib_device tTS_IB_DEVICE_STRUCT, *tTS_IB_DEVICE;
typedef struct ib_pd tTS_IB_PD_STRUCT, *tTS_IB_PD;
typedef struct ib_address tTS_IB_ADDRESS_STRUCT, *tTS_IB_ADDRESS;
typedef struct ib_qp tTS_IB_QP_STRUCT, *tTS_IB_QP;
typedef struct ib_mr tTS_IB_MR_STRUCT, *tTS_IB_MR;
typedef struct ib_cq tTS_IB_CQ_STRUCT, *tTS_IB_CQ;
typedef struct ib_fmr tTS_IB_FMR_STRUCT, *tTS_IB_FMR;
typedef struct ib_switch_ops tTS_IB_SWITCH_OPS_STRUCT, *tTS_IB_SWITCH_OPS;

typedef int (*ib_device_query_func)(struct ib_device            *device,
				    struct ib_device_properties *properties);
typedef int (*ib_device_modify_func)(struct ib_device         *device,
				     struct ib_device_changes *properties);
typedef int (*ib_port_query_func)(struct ib_device          *device,
				  tTS_IB_PORT                port,
				  struct ib_port_properties *properties);
typedef int (*ib_port_modify_func)(struct ib_device       *device,
				   tTS_IB_PORT             port,
				   struct ib_port_changes *properties);
typedef int (*ib_pkey_query_func)(struct ib_device *device,
				  tTS_IB_PORT  	    port,
				  int          	    index,
				  tTS_IB_PKEY  	   *pkey);
typedef int (*ib_gid_query_func)(struct ib_device *device,
				 tTS_IB_PORT       port,
				 int           	   index,
				 tTS_IB_GID    	   gid);
typedef int (*ib_pd_create_func)(struct ib_device *device,
				 void             *device_specific,
				 struct ib_pd     *pd);
typedef int (*ib_pd_destroy_func)(struct ib_pd *pd);
typedef int (*ib_address_create_func)(struct ib_pd             *pd,
				      struct ib_address_vector *address_vector,
				      struct ib_address        *address);
typedef int (*ib_address_query_func)(struct ib_address        *address,
				     struct ib_address_vector *address_vector);
typedef int (*ib_address_destroy_func)(struct ib_address *address);
typedef int (*ib_qp_create_func)(struct ib_pd              *pd,
				 struct ib_qp_create_param *param,
				 struct ib_qp              *qp);
typedef int (*ib_special_qp_create_func)(struct ib_pd              *pd,
					 struct ib_qp_create_param *param,
					 tTS_IB_PORT                port,
					 tTS_IB_SPECIAL_QP_TYPE     qp_type,
					 struct ib_qp              *qp);
typedef int (*ib_qp_modify_func)(struct ib_qp           *qp,
				 struct ib_qp_attribute *attr);
typedef int (*ib_qp_query_func)(struct ib_qp           *qp,
				struct ib_qp_attribute *attr);
typedef int (*ib_qp_destroy_func)(struct ib_qp *qp);
typedef int (*ib_send_post_func)(struct ib_qp         *qp,
				 struct ib_send_param *param,
				 int                   num_work_requests);
typedef int (*ib_receive_post_func)(struct ib_qp            *qp,
				    struct ib_receive_param *param,
				    int                      num_work_requests);
typedef int (*ib_cq_create_func)(struct ib_device *device,
				 int          	  *entries,
				 void         	  *device_specific,
				 struct ib_cq     *cq);
typedef int (*ib_cq_destroy_func)(struct ib_cq *cq);
typedef int (*ib_cq_resize_func)(struct ib_cq *cq,
				 int          *entries);
typedef int (*ib_cq_poll_func)(struct ib_cq       *cq,
			       struct ib_cq_entry *entry);
typedef int (*ib_cq_arm_func)(struct ib_cq *cq,
			      int           solicited);
typedef int (*ib_mr_register_func)(struct ib_pd        *pd,
				   void                *start_address,
				   uint64_t             buffer_size,
				   tTS_IB_MEMORY_ACCESS access,
				   struct ib_mr        *mr);
typedef int (*ib_mr_register_physical_func)(struct ib_pd              *pd,
					    struct ib_physical_buffer *buffer_list,
					    int                        list_len,
					    uint64_t                  *io_virtual_address,
					    uint64_t                   buffer_size,
					    uint64_t                   iova_offset,
					    tTS_IB_MEMORY_ACCESS       access,
					    struct ib_mr              *mr);
typedef int (*ib_mr_deregister_func)(struct ib_mr *mr);
typedef int (*ib_fmr_create_func)(struct ib_pd        *pd,
				  tTS_IB_MEMORY_ACCESS access,
				  int                  max_pages,
				  int                  max_remaps,
				  struct ib_fmr       *fmr);
typedef int (*ib_fmr_destroy_func)(struct ib_fmr *fmr);
typedef int (*ib_fmr_map_func)(struct ib_fmr *fmr,
			       uint64_t      *page_list,
			       int            list_len,
			       uint64_t      *io_virtual_address,
			       uint64_t       iova_offset,
			       tTS_IB_LKEY   *lkey,
			       tTS_IB_RKEY   *rkey);
typedef int (*ib_fmr_unmap_func)(struct ib_device *device,
				 struct list_head *fmr_list);
typedef int (*ib_multicast_attach_func)(struct ib_qp *qp,
					tTS_IB_LID lid,
					tTS_IB_GID gid);
typedef int (*ib_multicast_detach_func)(struct ib_qp *qp,
					tTS_IB_LID lid,
					tTS_IB_GID gid);
typedef tTS_IB_MAD_RESULT (*ib_mad_process_func)(struct ib_device *device,
						 int           	   ignore_mkey,
						 struct ib_mad    *in_mad,
						 struct ib_mad    *response_mad);

/* structures */

struct ib_device {
	TS_IB_DECLARE_MAGIC

	struct module                *owner;

	char                          name[TS_IB_DEVICE_NAME_MAX];
	char                         *provider;
	void                         *private;
	struct list_head              core_list;
	void                         *core;
	void                         *mad;

	ib_device_query_func         device_query;
	ib_device_modify_func        device_modify;
	ib_port_query_func           port_query;
	ib_port_modify_func          port_modify;
	ib_pkey_query_func           pkey_query;
	ib_gid_query_func            gid_query;
	ib_pd_create_func            pd_create;
	ib_pd_destroy_func           pd_destroy;
	ib_address_create_func       address_create;
	ib_address_query_func        address_query;
	ib_address_destroy_func      address_destroy;
	ib_qp_create_func            qp_create;
	ib_special_qp_create_func    special_qp_create;
	ib_qp_modify_func            qp_modify;
	ib_qp_query_func             qp_query;
	ib_qp_destroy_func           qp_destroy;
	ib_send_post_func            send_post;
	ib_receive_post_func         receive_post;
	ib_cq_create_func            cq_create;
	ib_cq_destroy_func           cq_destroy;
	ib_cq_resize_func            cq_resize;
	ib_cq_poll_func              cq_poll;
	ib_cq_arm_func               cq_arm;
	ib_mr_register_func          mr_register;
	ib_mr_register_physical_func mr_register_physical;
	ib_mr_deregister_func        mr_deregister;
	ib_fmr_create_func           fmr_create;
	ib_fmr_destroy_func          fmr_destroy;
	ib_fmr_map_func              fmr_map;
	ib_fmr_unmap_func            fmr_unmap;
	ib_multicast_attach_func     multicast_attach;
	ib_multicast_detach_func     multicast_detach;
	ib_mad_process_func          mad_process;

	struct ib_switch_ops        *switch_ops;
};

struct ib_pd {
	TS_IB_DECLARE_MAGIC
	struct ib_device *device;
	void             *private;
};

struct ib_address {
	TS_IB_DECLARE_MAGIC
	struct ib_device *device;
	void             *private;
};

struct ib_qp {
	TS_IB_DECLARE_MAGIC
	struct ib_device *device;
	tTS_IB_QPN        qpn;
	struct list_head  async_handler_list;
	spinlock_t        async_handler_lock;
	void             *private;
};

struct ib_mr {
	TS_IB_DECLARE_MAGIC
	struct ib_device *device;
	tTS_IB_LKEY       lkey;
	tTS_IB_RKEY       rkey;
	void             *private;
};

struct ib_cq {
	TS_IB_DECLARE_MAGIC
	struct ib_device     *device;
	struct ib_cq_callback callback;
	struct list_head      async_handler_list;
	spinlock_t            async_handler_lock;
	void                 *private;
};

struct ib_fmr {
	TS_IB_DECLARE_MAGIC
	struct ib_device      *device;
	void                  *private;
	tTS_IB_FMR_POOL_HANDLE pool;
	tTS_IB_LKEY            lkey;
	tTS_IB_RKEY            rkey;
	int                    ref_count;
	int                    remap_count;
	struct list_head       list;
	tTS_HASH_NODE_STRUCT   cache_node;
	uint64_t               io_virtual_address;
	uint64_t               iova_offset;
	int                    page_list_len;
	uint64_t               page_list[0];
};

#endif /* _TS_IB_PROVIDER_TYPES_H */

/*
  Local Variables:
  c-file-style: "linux"
  indent-tabs-mode: t
  End:
*/
