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

  $Id: ts_ib_core.h,v 1.9 2004/02/25 00:32:31 roland Exp $
*/

#ifndef _TS_IB_CORE_H
#define _TS_IB_CORE_H

#if defined(MODVERSIONS) && !defined(__GENKSYMS__) && !defined(TS_KERNEL_2_6)
#  include "ts_kernel_version.h"
#  include TS_VER_FILE(../core,core_export.ver)
#endif

#include "ts_ib_core_types.h"

static inline int tsIbMtuEnumToInt(
                                   tTS_IB_MTU mtu
                                   ) {
  switch (mtu) {
  case TS_IB_MTU_256:
    return 256;
  case TS_IB_MTU_512:
    return 512;
  case TS_IB_MTU_1024:
    return 1024;
  case TS_IB_MTU_2048:
    return 2048;
  case TS_IB_MTU_4096:
    return 4096;
  default:
    return -1;
  }
}

tTS_IB_DEVICE_HANDLE tsIbDeviceGetByName(
                                         const char *name
                                         );

tTS_IB_DEVICE_HANDLE tsIbDeviceGetByIndex(
                                          int index
                                          );

int tsIbDevicePropertiesGet(
                            tTS_IB_DEVICE_HANDLE     device,
                            tTS_IB_DEVICE_PROPERTIES properties
                            );

int tsIbDevicePropertiesSet(
                            tTS_IB_DEVICE_HANDLE         device,
                            tTS_IB_DEVICE_PROPERTIES_SET properties
                            );

int tsIbPortPropertiesGet(
                          tTS_IB_DEVICE_HANDLE     device,
                          tTS_IB_PORT              port,
                          tTS_IB_PORT_PROPERTIES   properties
                          );

int tsIbPortPropertiesSet(
                          tTS_IB_DEVICE_HANDLE       device,
                          tTS_IB_PORT                port,
                          tTS_IB_PORT_PROPERTIES_SET properties
                          );

int tsIbPkeyEntryGet(
                     tTS_IB_DEVICE_HANDLE device,
                     tTS_IB_PORT          port,
                     int                  index,
                     tTS_IB_PKEY         *pkey
                     );

int tsIbGidEntryGet(
                    tTS_IB_DEVICE_HANDLE device,
                    tTS_IB_PORT          port,
                    int                  index,
                    tTS_IB_GID           gid
                    );

int tsIbPdCreate(
                 tTS_IB_DEVICE_HANDLE device,
                 void                *device_specific,
                 tTS_IB_PD_HANDLE    *pd
                 );

int tsIbPdDestroy(
                  tTS_IB_PD_HANDLE pd
                  );

int tsIbAddressCreate(
                      tTS_IB_PD_HANDLE       pd,
                      tTS_IB_ADDRESS_VECTOR  address,
                      tTS_IB_ADDRESS_HANDLE *address_handle
                      );

int tsIbAddressQuery(
                     tTS_IB_ADDRESS_HANDLE address_handle,
                     tTS_IB_ADDRESS_VECTOR address
                     );

int tsIbAddressDestroy(
                       tTS_IB_ADDRESS_HANDLE address_handle
                       );

int tsIbQpCreate(
                 tTS_IB_QP_CREATE_PARAM param,
                 tTS_IB_QP_HANDLE      *qp,
                 tTS_IB_QPN            *qpn
                 );

int tsIbSpecialQpCreate(
                        tTS_IB_QP_CREATE_PARAM param,
                        tTS_IB_PORT            port,
                        tTS_IB_SPECIAL_QP_TYPE qp_type,
                        tTS_IB_QP_HANDLE      *qp
                        );

int tsIbQpModify(
                 tTS_IB_QP_HANDLE    qp,
                 tTS_IB_QP_ATTRIBUTE attr
                 );

int tsIbQpQuery(
                tTS_IB_QP_HANDLE    qp,
                tTS_IB_QP_ATTRIBUTE attr
                );

int tsIbQpQueryQpn(
                   tTS_IB_QP_HANDLE qp,
                   tTS_IB_QPN      *qpn
                   );

int tsIbQpDestroy(
                  tTS_IB_QP_HANDLE qp
                  );

int tsIbSend(
             tTS_IB_QP_HANDLE  qp,
             tTS_IB_SEND_PARAM param,
             int               num_work_requests
             );

int tsIbReceive(
                tTS_IB_QP_HANDLE     qp,
                tTS_IB_RECEIVE_PARAM param,
                int                  num_work_requests
                );

int tsIbCqCreate(
                 tTS_IB_DEVICE_HANDLE       device,
                 int                       *entries,
                 tTS_IB_CQ_CALLBACK         callback,
                 void                      *device_specific,
                 tTS_IB_CQ_HANDLE          *cq
                 );

int tsIbCqDestroy(
                  tTS_IB_CQ_HANDLE cq
                  );

int tsIbCqResize(
                 tTS_IB_CQ_HANDLE cq,
                 int             *entries
                 );

int tsIbCqPoll(
               tTS_IB_CQ_HANDLE cq,
               tTS_IB_CQ_ENTRY  entry
               );

int tsIbCqRequestNotification(
                              tTS_IB_CQ_HANDLE cq,
                              int solicited
                              );

int tsIbMemoryRegister(
                       tTS_IB_PD_HANDLE     pd,
                       void                *start_address,
                       uint64_t             buffer_size,
                       tTS_IB_MEMORY_ACCESS access,
                       tTS_IB_MR_HANDLE    *memory,
                       tTS_IB_LKEY         *lkey,
                       tTS_IB_RKEY         *rkey
                       );

int tsIbMemoryRegisterPhysical(
                               tTS_IB_PD_HANDLE       pd,
                               tTS_IB_PHYSICAL_BUFFER buffer_list,
                               int                    list_len,
                               uint64_t              *io_virtual_address,
                               uint64_t               buffer_size,
                               uint64_t               iova_offset,
                               tTS_IB_MEMORY_ACCESS   access,
                               tTS_IB_MR_HANDLE      *mr,
                               tTS_IB_LKEY           *lkey,
                               tTS_IB_RKEY           *rkey
                               );

int tsIbMemoryDeregister(
                         tTS_IB_MR_HANDLE memory
                         );

int tsIbFmrPoolCreate(
                      tTS_IB_PD_HANDLE          pd,
		      tTS_IB_FMR_POOL_PARAM     params,
                      tTS_IB_FMR_POOL_HANDLE   *pool
                      );

int tsIbFmrPoolDestroy(
                       tTS_IB_FMR_POOL_HANDLE pool
                       );

int tsIbFmrRegisterPhysical(
                            tTS_IB_FMR_POOL_HANDLE  pool,
                            uint64_t               *page_list,
                            int                     list_len,
                            uint64_t               *io_virtual_address,
                            uint64_t                iova_offset,
                            tTS_IB_FMR_HANDLE      *fmr,
                            tTS_IB_LKEY            *lkey,
                            tTS_IB_RKEY            *rkey
                            );

int tsIbFmrDeregister(
                      tTS_IB_FMR_HANDLE fmr
                      );

int tsIbMulticastAttach(
                        tTS_IB_LID       multicast_lid,
                        tTS_IB_GID       multicast_gid,
                        tTS_IB_QP_HANDLE qp
                        );

int tsIbMulticastDetach(
                        tTS_IB_LID       multicast_lid,
                        tTS_IB_GID       multicast_gid,
                        tTS_IB_QP_HANDLE qp
                        );

int tsIbAsyncEventHandlerRegister(
                                  tTS_IB_ASYNC_EVENT_RECORD           record,
                                  tTS_IB_ASYNC_EVENT_HANDLER_FUNCTION function,
                                  void                               *arg,
                                  tTS_IB_ASYNC_EVENT_HANDLER_HANDLE  *handle
                                  );

int tsIbAsyncEventHandlerDeregister(
                                    tTS_IB_ASYNC_EVENT_HANDLER_HANDLE handler
                                    );

int tsIbCachedNodeGuidGet(
                          tTS_IB_DEVICE_HANDLE device,
                          tTS_IB_GUID          node_guid
                          );

int tsIbCachedPortPropertiesGet(
                                tTS_IB_DEVICE_HANDLE   device,
                                tTS_IB_PORT            port,
                                tTS_IB_PORT_PROPERTIES properties
                                );

int tsIbCachedSmPathGet(
                        tTS_IB_DEVICE_HANDLE device,
                        tTS_IB_PORT          port,
                        tTS_IB_SM_PATH       sm_path
                        );

int tsIbCachedLidGet(
                     tTS_IB_DEVICE_HANDLE device,
                     tTS_IB_PORT          port,
                     tTS_IB_PORT_LID      port_lid
                     );

int tsIbCachedGidGet(
                     tTS_IB_DEVICE_HANDLE device,
                     tTS_IB_PORT          port,
                     int                  index,
                     tTS_IB_GID           gid
                     );

int tsIbCachedGidFind(
                      tTS_IB_GID            gid,
                      tTS_IB_DEVICE_HANDLE *device,
                      tTS_IB_PORT          *port,
                      int                  *index
                      );

int tsIbCachedPkeyGet(
                      tTS_IB_DEVICE_HANDLE device_handle,
                      tTS_IB_PORT          port,
                      int                  index,
                      tTS_IB_PKEY         *pkey
                      );

int tsIbCachedPkeyFind(
                       tTS_IB_DEVICE_HANDLE device,
                       tTS_IB_PORT          port,
                       tTS_IB_PKEY          pkey,
                       int                 *index
                       );

#endif /* _TS_IB_CORE_H */
