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

  $Id: ts_ib_provider_types.h,v 1.11 2004/02/25 00:32:33 roland Exp $
*/

#ifndef _TS_IB_PROVIDER_TYPES_H
#define _TS_IB_PROVIDER_TYPES_H

#if defined(__KERNEL__)
#  ifndef W2K_OS
#    include <linux/types.h>
#    include "ts_kernel_uintptr.h"
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

/* type definitions */

typedef struct tTS_IB_DEVICE_STRUCT tTS_IB_DEVICE_STRUCT,
  *tTS_IB_DEVICE;
typedef struct tTS_IB_PD_STRUCT tTS_IB_PD_STRUCT,
  *tTS_IB_PD;
typedef struct tTS_IB_ADDRESS_STRUCT tTS_IB_ADDRESS_STRUCT,
  *tTS_IB_ADDRESS;
typedef struct tTS_IB_QP_STRUCT tTS_IB_QP_STRUCT,
  *tTS_IB_QP;
typedef struct tTS_IB_MR_STRUCT tTS_IB_MR_STRUCT,
  *tTS_IB_MR;
typedef struct tTS_IB_CQ_STRUCT tTS_IB_CQ_STRUCT,
  *tTS_IB_CQ;
typedef struct tTS_IB_FMR_STRUCT tTS_IB_FMR_STRUCT,
  *tTS_IB_FMR;
typedef struct tTS_IB_SWITCH_OPS_STRUCT tTS_IB_SWITCH_OPS_STRUCT,
  *tTS_IB_SWITCH_OPS;

typedef int (*tTS_IB_DEVICE_QUERY_FUNCTION)(tTS_IB_DEVICE            device,
                                            tTS_IB_DEVICE_PROPERTIES properties);
typedef int (*tTS_IB_DEVICE_MODIFY_FUNCTION)(tTS_IB_DEVICE                device,
                                             tTS_IB_DEVICE_PROPERTIES_SET properties);
typedef int (*tTS_IB_PORT_QUERY_FUNCTION)(tTS_IB_DEVICE          device,
                                          tTS_IB_PORT            port,
                                          tTS_IB_PORT_PROPERTIES properties);
typedef int (*tTS_IB_PORT_MODIFY_FUNCTION)(tTS_IB_DEVICE              device,
                                           tTS_IB_PORT                port,
                                           tTS_IB_PORT_PROPERTIES_SET properties);
typedef int (*tTS_IB_PKEY_QUERY_FUNCTION)(tTS_IB_DEVICE device,
                                          tTS_IB_PORT   port,
                                          int           index,
                                          tTS_IB_PKEY  *pkey);
typedef int (*tTS_IB_GID_QUERY_FUNCTION)(tTS_IB_DEVICE device,
                                         tTS_IB_PORT   port,
                                         int           index,
                                         tTS_IB_GID    gid);
typedef int (*tTS_IB_PD_CREATE_FUNCTION)(tTS_IB_DEVICE device,
                                         void         *device_specific,
                                         tTS_IB_PD     pd);
typedef int (*tTS_IB_PD_DESTROY_FUNCTION)(tTS_IB_PD     pd);
typedef int (*tTS_IB_ADDRESS_CREATE_FUNCTION)(tTS_IB_PD             pd,
                                              tTS_IB_ADDRESS_VECTOR address_vector,
                                              tTS_IB_ADDRESS        address);
typedef int (*tTS_IB_ADDRESS_QUERY_FUNCTION)(tTS_IB_ADDRESS        address,
                                             tTS_IB_ADDRESS_VECTOR address_vector);
typedef int (*tTS_IB_ADDRESS_DESTROY_FUNCTION)(tTS_IB_ADDRESS address);
typedef int (*tTS_IB_QP_CREATE_FUNCTION)(tTS_IB_PD              pd,
                                         tTS_IB_QP_CREATE_PARAM param,
                                         tTS_IB_QP              qp);
typedef int (*tTS_IB_SPECIAL_QP_CREATE_FUNCTION)(tTS_IB_PD              pd,
                                                 tTS_IB_QP_CREATE_PARAM param,
                                                 tTS_IB_PORT            port,
                                                 tTS_IB_SPECIAL_QP_TYPE qp_type,
                                                 tTS_IB_QP              qp);
typedef int (*tTS_IB_QP_MODIFY_FUNCTION)(tTS_IB_QP           qp,
                                         tTS_IB_QP_ATTRIBUTE attr);
typedef int (*tTS_IB_QP_QUERY_FUNCTION)(tTS_IB_QP           qp,
                                        tTS_IB_QP_ATTRIBUTE attr);
typedef int (*tTS_IB_QP_DESTROY_FUNCTION)(tTS_IB_QP qp);
typedef int (*tTS_IB_SEND_POST_FUNCTION)(tTS_IB_QP         qp,
                                         tTS_IB_SEND_PARAM param,
                                         int               num_work_requests);
typedef int (*tTS_IB_RECEIVE_POST_FUNCTION)(tTS_IB_QP            qp,
                                            tTS_IB_RECEIVE_PARAM param,
                                            int                  num_work_requests);
typedef int (*tTS_IB_CQ_CREATE_FUNCTION)(tTS_IB_DEVICE device,
                                         int          *entries,
                                         void         *device_specific,
                                         tTS_IB_CQ     cq);
typedef int (*tTS_IB_CQ_DESTROY_FUNCTION)(tTS_IB_CQ cq);
typedef int (*tTS_IB_CQ_RESIZE_FUNCTION)(tTS_IB_CQ cq,
                                         int      *entries);
typedef int (*tTS_IB_CQ_POLL_FUNCTION)(tTS_IB_CQ       cq,
                                       tTS_IB_CQ_ENTRY entry);
typedef int (*tTS_IB_CQ_ARM_FUNCTION)(tTS_IB_CQ cq,
                                      int       solicited);
typedef int (*tTS_IB_MR_REGISTER_FUNCTION)(tTS_IB_PD            pd,
                                           void                *start_address,
                                           uint64_t             buffer_size,
                                           tTS_IB_MEMORY_ACCESS access,
                                           tTS_IB_MR            mr);
typedef int (*tTS_IB_MR_REGISTER_PHYSICAL_FUNCTION)(tTS_IB_PD              pd,
                                                    tTS_IB_PHYSICAL_BUFFER buffer_list,
                                                    int                    list_len,
                                                    uint64_t              *io_virtual_address,
                                                    uint64_t               buffer_size,
                                                    uint64_t               iova_offset,
                                                    tTS_IB_MEMORY_ACCESS   access,
                                                    tTS_IB_MR              mr);
typedef int (*tTS_IB_MR_DEREGISTER_FUNCTION)(tTS_IB_MR mr);
typedef int (*tTS_IB_FMR_CREATE_FUNCTION)(tTS_IB_PD            pd,
                                          tTS_IB_MEMORY_ACCESS access,
                                          int                  max_pages,
                                          int                  max_remaps,
                                          tTS_IB_FMR           fmr);
typedef int (*tTS_IB_FMR_DESTROY_FUNCTION)(tTS_IB_FMR fmr);
typedef int (*tTS_IB_FMR_MAP_FUNCTION)(tTS_IB_FMR   fmr,
                                       uint64_t    *page_list,
                                       int          list_len,
                                       uint64_t    *io_virtual_address,
                                       uint64_t     iova_offset,
                                       tTS_IB_LKEY *lkey,
                                       tTS_IB_RKEY *rkey);
typedef int (*tTS_IB_FMR_UNMAP_FUNCTION)(tTS_IB_DEVICE     device,
                                         struct list_head *fmr_list);
typedef int (*tTS_IB_MULTICAST_ATTACH_FUNCTION)(tTS_IB_QP  qp,
                                                tTS_IB_LID lid,
                                                tTS_IB_GID gid);
typedef int (*tTS_IB_MULTICAST_DETACH_FUNCTION)(tTS_IB_QP  qp,
                                                tTS_IB_LID lid,
                                                tTS_IB_GID gid);
typedef tTS_IB_MAD_RESULT (*tTS_IB_MAD_PROCESS_FUNCTION)(
                                                         tTS_IB_DEVICE device,
                                                         int           ignore_mkey,
                                                         tTS_IB_MAD    in_mad,
                                                         tTS_IB_MAD    response_mad);

/* structures */

struct tTS_IB_DEVICE_STRUCT {
  TS_IB_DECLARE_MAGIC

  char                                 name[TS_IB_DEVICE_NAME_MAX];
  char                                *provider;
  void                                *private;
  struct list_head                     core_list;
  void                                *core;
  void                                *mad;

  tTS_IB_DEVICE_QUERY_FUNCTION         device_query;
  tTS_IB_DEVICE_MODIFY_FUNCTION        device_modify;
  tTS_IB_PORT_QUERY_FUNCTION           port_query;
  tTS_IB_PORT_MODIFY_FUNCTION          port_modify;
  tTS_IB_PKEY_QUERY_FUNCTION           pkey_query;
  tTS_IB_GID_QUERY_FUNCTION            gid_query;
  tTS_IB_PD_CREATE_FUNCTION            pd_create;
  tTS_IB_PD_DESTROY_FUNCTION           pd_destroy;
  tTS_IB_ADDRESS_CREATE_FUNCTION       address_create;
  tTS_IB_ADDRESS_QUERY_FUNCTION        address_query;
  tTS_IB_ADDRESS_DESTROY_FUNCTION      address_destroy;
  tTS_IB_QP_CREATE_FUNCTION            qp_create;
  tTS_IB_SPECIAL_QP_CREATE_FUNCTION    special_qp_create;
  tTS_IB_QP_MODIFY_FUNCTION            qp_modify;
  tTS_IB_QP_QUERY_FUNCTION             qp_query;
  tTS_IB_QP_DESTROY_FUNCTION           qp_destroy;
  tTS_IB_SEND_POST_FUNCTION            send_post;
  tTS_IB_RECEIVE_POST_FUNCTION         receive_post;
  tTS_IB_CQ_CREATE_FUNCTION            cq_create;
  tTS_IB_CQ_DESTROY_FUNCTION           cq_destroy;
  tTS_IB_CQ_RESIZE_FUNCTION            cq_resize;
  tTS_IB_CQ_POLL_FUNCTION              cq_poll;
  tTS_IB_CQ_ARM_FUNCTION               cq_arm;
  tTS_IB_MR_REGISTER_FUNCTION          mr_register;
  tTS_IB_MR_REGISTER_PHYSICAL_FUNCTION mr_register_physical;
  tTS_IB_MR_DEREGISTER_FUNCTION        mr_deregister;
  tTS_IB_FMR_CREATE_FUNCTION           fmr_create;
  tTS_IB_FMR_DESTROY_FUNCTION          fmr_destroy;
  tTS_IB_FMR_MAP_FUNCTION              fmr_map;
  tTS_IB_FMR_UNMAP_FUNCTION            fmr_unmap;
  tTS_IB_MULTICAST_ATTACH_FUNCTION     multicast_attach;
  tTS_IB_MULTICAST_DETACH_FUNCTION     multicast_detach;
  tTS_IB_MAD_PROCESS_FUNCTION          mad_process;

  tTS_IB_SWITCH_OPS                    switch_ops;
};

struct tTS_IB_PD_STRUCT {
  TS_IB_DECLARE_MAGIC
  tTS_IB_DEVICE device;
  void         *private;
};

struct tTS_IB_ADDRESS_STRUCT {
  TS_IB_DECLARE_MAGIC
  tTS_IB_DEVICE device;
  void         *private;
};

struct tTS_IB_QP_STRUCT {
  TS_IB_DECLARE_MAGIC
  tTS_IB_DEVICE    device;
  tTS_IB_QPN       qpn;
  struct list_head async_handler_list;
  spinlock_t       async_handler_lock;
  void            *private;
};

struct tTS_IB_MR_STRUCT {
  TS_IB_DECLARE_MAGIC
  tTS_IB_DEVICE device;
  tTS_IB_LKEY   lkey;
  tTS_IB_RKEY   rkey;
  void         *private;
};

struct tTS_IB_CQ_STRUCT {
  TS_IB_DECLARE_MAGIC
  tTS_IB_DEVICE             device;
  tTS_IB_CQ_CALLBACK_STRUCT callback;
  struct list_head          async_handler_list;
  spinlock_t                async_handler_lock;
  void                     *private;
};

struct tTS_IB_FMR_STRUCT {
  TS_IB_DECLARE_MAGIC
  tTS_IB_DEVICE          device;
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
