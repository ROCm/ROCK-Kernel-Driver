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

  $Id: ts_ib_core.h 58 2004-04-16 02:09:40Z roland $
*/

#ifndef _TS_IB_CORE_H
#define _TS_IB_CORE_H

#if defined(MODVERSIONS) && !defined(__GENKSYMS__) && !defined(TS_KERNEL_2_6)
#  include "ts_kernel_version.h"
#  include TS_VER_FILE(../core,core_export.ver)
#endif

#include "ts_ib_core_types.h"

static inline int ib_mtu_enum_to_int(tTS_IB_MTU mtu)
{
	switch (mtu) {
	case IB_MTU_256:  return  256;
	case IB_MTU_512:  return  512;
	case IB_MTU_1024: return 1024;
	case IB_MTU_2048: return 2048;
	case IB_MTU_4096: return 4096;
	default: 	  return -1;
	}
}

tTS_IB_DEVICE_HANDLE ib_device_get_by_name(const char *name);
tTS_IB_DEVICE_HANDLE ib_device_get_by_index(int index);
int ib_device_notifier_register(struct ib_device_notifier *notifier);
int ib_device_notifier_deregister(struct ib_device_notifier *notifier);

int ib_device_properties_get(tTS_IB_DEVICE_HANDLE         device,
			     struct ib_device_properties *properties);
int ib_device_properties_set(tTS_IB_DEVICE_HANDLE             device,
			     struct ib_device_changes        *properties);
int ib_port_properties_get(tTS_IB_DEVICE_HANDLE       device,
			   tTS_IB_PORT                port,
			   struct ib_port_properties     *properties);
int ib_port_properties_set(tTS_IB_DEVICE_HANDLE    device,
			   tTS_IB_PORT             port,
			   struct ib_port_changes *properties);

int ib_pkey_entry_get(tTS_IB_DEVICE_HANDLE device,
		      tTS_IB_PORT          port,
		      int                  index,
		      tTS_IB_PKEY         *pkey);
int ib_gid_entry_get(tTS_IB_DEVICE_HANDLE device,
		     tTS_IB_PORT          port,
		     int                  index,
		     tTS_IB_GID           gid);

int ib_pd_create(tTS_IB_DEVICE_HANDLE device,
                 void                *device_specific,
                 tTS_IB_PD_HANDLE    *pd);
int ib_pd_destroy(tTS_IB_PD_HANDLE pd);

int ib_address_create(tTS_IB_PD_HANDLE          pd,
                      struct ib_address_vector *address,
                      tTS_IB_ADDRESS_HANDLE    *address_handle);
int ib_address_query(tTS_IB_ADDRESS_HANDLE     address_handle,
		     struct ib_address_vector *address);
int ib_address_destroy(tTS_IB_ADDRESS_HANDLE address_handle);

int ib_qp_create(struct ib_qp_create_param *param,
                 tTS_IB_QP_HANDLE          *qp,
                 tTS_IB_QPN                *qpn);
int ib_special_qp_create(struct ib_qp_create_param *param,
			 tTS_IB_PORT                port,
			 tTS_IB_SPECIAL_QP_TYPE     qp_type,
			 tTS_IB_QP_HANDLE          *qp);
int ib_qp_modify(tTS_IB_QP_HANDLE        qp,
		 struct ib_qp_attribute *attr);
int ib_qp_query(tTS_IB_QP_HANDLE        qp,
		struct ib_qp_attribute *attr);
int ib_qp_query_qpn(tTS_IB_QP_HANDLE qp,
		    tTS_IB_QPN      *qpn);
int ib_qp_destroy(tTS_IB_QP_HANDLE qp);

int ib_send(tTS_IB_QP_HANDLE      qp,
	    struct ib_send_param *param,
	    int                   num_work_requests);
int ib_receive(tTS_IB_QP_HANDLE         qp,
	       struct ib_receive_param *param,
	       int                      num_work_requests);

int ib_cq_create(tTS_IB_DEVICE_HANDLE       device,
                 int                       *entries,
                 struct ib_cq_callback     *callback,
                 void                      *device_specific,
                 tTS_IB_CQ_HANDLE          *cq);
int ib_cq_destroy(tTS_IB_CQ_HANDLE cq);
int ib_cq_resize(tTS_IB_CQ_HANDLE cq,
                 int             *entries);
int ib_cq_poll(tTS_IB_CQ_HANDLE    cq,
               struct ib_cq_entry *entry);
int ib_cq_request_notification(tTS_IB_CQ_HANDLE cq,
			       int solicited);

int ib_memory_register(tTS_IB_PD_HANDLE     pd,
                       void                *start_address,
                       uint64_t             buffer_size,
                       tTS_IB_MEMORY_ACCESS access,
                       tTS_IB_MR_HANDLE    *memory,
                       tTS_IB_LKEY         *lkey,
                       tTS_IB_RKEY         *rkey);
int ib_memory_register_physical(tTS_IB_PD_HANDLE           pd,
				struct ib_physical_buffer *buffer_list,
				int                   	   list_len,
				uint64_t              	  *io_virtual_address,
				uint64_t              	   buffer_size,
				uint64_t              	   iova_offset,
				tTS_IB_MEMORY_ACCESS  	   access,
				tTS_IB_MR_HANDLE      	  *mr,
				tTS_IB_LKEY           	  *lkey,
				tTS_IB_RKEY           	  *rkey);
int ib_memory_deregister(tTS_IB_MR_HANDLE memory);

int ib_fmr_pool_create(tTS_IB_PD_HANDLE          pd,
		       struct ib_fmr_pool_param *params,
		       tTS_IB_FMR_POOL_HANDLE   *pool);
int ib_fmr_pool_destroy(tTS_IB_FMR_POOL_HANDLE pool);
int ib_fmr_register_physical(tTS_IB_FMR_POOL_HANDLE  pool,
			     uint64_t               *page_list,
			     int                     list_len,
			     uint64_t               *io_virtual_address,
			     uint64_t                iova_offset,
			     tTS_IB_FMR_HANDLE      *fmr,
			     tTS_IB_LKEY            *lkey,
			     tTS_IB_RKEY            *rkey);
int ib_fmr_deregister(tTS_IB_FMR_HANDLE fmr);

int ib_multicast_attach(tTS_IB_LID       multicast_lid,
			tTS_IB_GID       multicast_gid,
			tTS_IB_QP_HANDLE qp);
int ib_multicast_detach(tTS_IB_LID       multicast_lid,
			tTS_IB_GID       multicast_gid,
			tTS_IB_QP_HANDLE qp);

int ib_async_event_handler_register(struct ib_async_event_record       *record,
				    tTS_IB_ASYNC_EVENT_HANDLER_FUNCTION function,
				    void                               *arg,
				    tTS_IB_ASYNC_EVENT_HANDLER_HANDLE  *handle);
int ib_async_event_handler_deregister(tTS_IB_ASYNC_EVENT_HANDLER_HANDLE handler);

int ib_cached_node_guid_get(tTS_IB_DEVICE_HANDLE device,
			    tTS_IB_GUID          node_guid);
int ib_cached_port_properties_get(tTS_IB_DEVICE_HANDLE       device,
				  tTS_IB_PORT                port,
				  struct ib_port_properties *properties);
int ib_cached_sm_path_get(tTS_IB_DEVICE_HANDLE device,
			  tTS_IB_PORT          port,
			  struct ib_sm_path   *sm_path);
int ib_cached_lid_get(tTS_IB_DEVICE_HANDLE device,
		      tTS_IB_PORT          port,
		      struct ib_port_lid  *port_lid);
int ib_cached_gid_get(tTS_IB_DEVICE_HANDLE device,
		      tTS_IB_PORT          port,
		      int                  index,
		      tTS_IB_GID           gid);
int ib_cached_gid_find(tTS_IB_GID            gid,
		       tTS_IB_DEVICE_HANDLE *device,
		       tTS_IB_PORT          *port,
		       int                  *index);
int ib_cached_pkey_get(tTS_IB_DEVICE_HANDLE device_handle,
		       tTS_IB_PORT          port,
		       int                  index,
		       tTS_IB_PKEY         *pkey);
int ib_cached_pkey_find(tTS_IB_DEVICE_HANDLE device,
			tTS_IB_PORT          port,
			tTS_IB_PKEY          pkey,
			int                 *index);

/* Defines to support legacy code -- don't use the tsIb names in new code. */
#define tsIbMtuEnumToInt                 ib_mtu_enum_to_int
#define tsIbDeviceGetByName              ib_device_get_by_name
#define tsIbDeviceGetByIndex             ib_device_get_by_index
#define tsIbDevicePropertiesGet          ib_device_properties_get
#define tsIbDevicePropertiesSet          ib_device_properties_set
#define tsIbPortPropertiesGet            ib_port_properties_get
#define tsIbPortPropertiesSet            ib_port_properties_set
#define tsIbPkeyEntryGet                 ib_pkey_entry_get
#define tsIbGidEntryGet                  ib_gid_entry_get
#define tsIbPdCreate                     ib_pd_create
#define tsIbPdDestroy                    ib_pd_destroy
#define tsIbAddressCreate                ib_address_create
#define tsIbAddressQuery                 ib_address_query
#define tsIbAddressDestroy               ib_address_destroy
#define tsIbQpCreate                     ib_qp_create
#define tsIbSpecialQpCreate              ib_special_qp_create
#define tsIbQpModify                     ib_qp_modify
#define tsIbQpQuery                      ib_qp_query
#define tsIbQpQueryQpn                   ib_qp_query_qpn
#define tsIbQpDestroy                    ib_qp_destroy
#define tsIbSend                         ib_send
#define tsIbReceive                      ib_receive
#define tsIbCqCreate                     ib_cq_create
#define tsIbCqDestroy                    ib_cq_destroy
#define tsIbCqResize                     ib_cq_resize
#define tsIbCqPoll                       ib_cq_poll
#define tsIbCqRequestNotification        ib_cq_request_notification
#define tsIbMemoryRegister               ib_memory_register
#define tsIbMemoryRegisterPhysical       ib_memory_register_physical
#define tsIbMemoryDeregister             ib_memory_deregister
#define tsIbFmrPoolCreate                ib_fmr_pool_create
#define tsIbFmrPoolDestroy               ib_fmr_pool_destroy
#define tsIbFmrRegisterPhysical          ib_fmr_register_physical
#define tsIbFmrDeregister                ib_fmr_deregister
#define tsIbMulticastAttach              ib_multicast_attach
#define tsIbMulticastDetach              ib_multicast_detach
#define tsIbAsyncEventHandlerRegister    ib_async_event_handler_register
#define tsIbAsyncEventHandlerDeregister  ib_async_event_handler_deregister
#define tsIbCachedNodeGuidGet            ib_cached_node_guid_get
#define tsIbCachedPortPropertiesGet      ib_cached_port_properties_get
#define tsIbCachedSmPathGet              ib_cached_sm_path_get
#define tsIbCachedLidGet                 ib_cached_lid_get
#define tsIbCachedGidGet                 ib_cached_gid_get
#define tsIbCachedGidFind                ib_cached_gid_find
#define tsIbCachedPkeyGet                ib_cached_pkey_get
#define tsIbCachedPkeyFind               ib_cached_pkey_find

#endif /* _TS_IB_CORE_H */

/*
  Local Variables:
  c-file-style: "linux"
  indent-tabs-mode: t
  End:
*/
