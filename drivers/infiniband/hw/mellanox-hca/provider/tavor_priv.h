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

  $Id: tavor_priv.h,v 1.10 2004/03/04 02:10:05 roland Exp $
*/

#ifndef _TAVOR_PRIV_H
#define _TAVOR_PRIV_H

#ifndef __KERNEL__
#  define __KERNEL__
#endif
#ifndef MODULE
#  define MODULE
#endif

#ifndef W2K_OS
#  include <linux/config.h>
#endif
#if defined(CONFIG_MODVERSIONS) && !defined(MODVERSIONS)
#  define MODVERSIONS
#endif

#if defined(MODVERSIONS) && !defined(__GENKSYMS__) && !defined(TS_KERNEL_2_6)
#  ifndef W2K_OS
#    include <linux/modversions.h>
#  endif
#endif

#include "ts_ib_tavor_provider.h"
#include "ts_ib_provider_types.h"

#include <evapi.h>
#include <vapi_common.h>

/* Should we override node description? */
#if defined(TS_HOST_DRIVER)
#  define TS_IB_TAVOR_OVERRIDE_NODE_DESCRIPTION 1
#else
#  define TS_IB_TAVOR_OVERRIDE_NODE_DESCRIPTION 0
#endif

#define TS_IB_TAVOR_FMR_UNMAP_CHUNK_SIZE 256

typedef struct tTS_IB_TAVOR_PRIVATE_STRUCT tTS_IB_TAVOR_PRIVATE_STRUCT,
  *tTS_IB_TAVOR_PRIVATE;
typedef struct tTS_IB_TAVOR_CQ_STRUCT tTS_IB_TAVOR_CQ_STRUCT,
  *tTS_IB_TAVOR_CQ;
typedef struct tTS_IB_TAVOR_QP_STRUCT tTS_IB_TAVOR_QP_STRUCT,
  *tTS_IB_TAVOR_QP;

struct tTS_IB_TAVOR_PRIVATE_STRUCT {
  VAPI_hca_hndl_t  vapi_handle;
  tTS_IB_NODE_DESC node_desc;
  EVAPI_fmr_hndl_t fmr_unmap[TS_IB_TAVOR_FMR_UNMAP_CHUNK_SIZE];
  struct semaphore fmr_unmap_mutex;
};

struct tTS_IB_TAVOR_CQ_STRUCT {
  VAPI_cq_hndl_t             cq_handle;
  EVAPI_compl_handler_hndl_t handler;
};

struct tTS_IB_TAVOR_QP_STRUCT {
  union {
    VAPI_qp_hndl_t   kernel_handle;
    VAPI_k_qp_hndl_t user_handle;
  }                  qp_handle;
  tTS_IB_QP_STATE    cached_state;
  int                is_user:1;
};

#define TS_IB_MAX_TAVOR 32

void tsIbTavorAsyncHandler(
                           VAPI_hca_hndl_t      hca,
                           VAPI_event_record_t *event_record,
                           void                *device_ptr
                           );

int tsIbTavorDeviceQuery(
                         tTS_IB_DEVICE            device,
                         tTS_IB_DEVICE_PROPERTIES properties
                         );

int tsIbTavorPortQuery(
                       tTS_IB_DEVICE          device,
                       tTS_IB_PORT            port,
                       tTS_IB_PORT_PROPERTIES properties
                       );

int tsIbTavorPortModify(
                        tTS_IB_DEVICE              device,
                        tTS_IB_PORT                port,
                        tTS_IB_PORT_PROPERTIES_SET properties
                        );

int tsIbTavorPkeyQuery(
                       tTS_IB_DEVICE device,
                       tTS_IB_PORT   port,
                       int           index,
                       tTS_IB_PKEY  *pkey
                       );

int tsIbTavorGidQuery(
                      tTS_IB_DEVICE device,
                      tTS_IB_PORT   port,
                      int           index,
                      tTS_IB_GID    gid
                      );

int tsIbTavorPdCreate(
                      tTS_IB_DEVICE device,
                      void         *device_specific,
                      tTS_IB_PD     pd
                      );

int tsIbTavorPdDestroy(
                       tTS_IB_PD pd
                       );

int tsIbTavorAddressCreate(
                           tTS_IB_PD             pd,
                           tTS_IB_ADDRESS_VECTOR address_vector,
                           tTS_IB_ADDRESS        address
                           );

int tsIbTavorAddressDestroy(
                            tTS_IB_ADDRESS address
                            );

int tsIbTavorQpCreate(
                      tTS_IB_PD              pd,
                      tTS_IB_QP_CREATE_PARAM param,
                      tTS_IB_QP              qp
                      );

int tsIbTavorSpecialQpCreate(
                             tTS_IB_PD              pd,
                             tTS_IB_QP_CREATE_PARAM param,
                             tTS_IB_PORT            port,
                             tTS_IB_SPECIAL_QP_TYPE qp_type,
                             tTS_IB_QP              qp
                             );

int tsIbTavorQpModify(
                      tTS_IB_QP           qp,
                      tTS_IB_QP_ATTRIBUTE attr
                      );

int tsIbTavorQpQuery(
                     tTS_IB_QP           qp,
                     tTS_IB_QP_ATTRIBUTE attr
                     );

int tsIbTavorQpDestroy(
                       tTS_IB_QP qp
                       );

int tsIbTavorSendPost(
                      tTS_IB_QP         qp,
                      tTS_IB_SEND_PARAM param,
                      int               num_work_requests
                      );

int tsIbTavorReceivePost(
                         tTS_IB_QP            qp,
                         tTS_IB_RECEIVE_PARAM param,
                         int                  num_work_requests
                         );

int tsIbTavorCqCreate(
                      tTS_IB_DEVICE device,
                      int          *entries,
                      void         *device_specific,
                      tTS_IB_CQ     cq
                      );

int tsIbTavorCqDestroy(
                       tTS_IB_CQ cq
                       );

int tsIbTavorCqResize(
                      tTS_IB_CQ cq,
                      int      *entries
                      );

int tsIbTavorCqPoll(
                    tTS_IB_CQ       cq,
                    tTS_IB_CQ_ENTRY entry
                    );

int tsIbTavorCqArm(
                   tTS_IB_CQ cq,
                   int       solicited
                   );

int tsIbTavorMemoryRegister(
                            tTS_IB_PD            pd,
                            void                *start_address,
                            uint64_t             buffer_size,
                            tTS_IB_MEMORY_ACCESS access,
                            tTS_IB_MR            mr
                            );

int tsIbTavorMemoryRegisterPhysical(
                                    tTS_IB_PD              pd,
                                    tTS_IB_PHYSICAL_BUFFER buffer_list,
                                    int                    list_len,
                                    uint64_t              *io_virtual_address,
                                    uint64_t               buffer_size,
                                    uint64_t               iova_offset,
                                    tTS_IB_MEMORY_ACCESS   access,
                                    tTS_IB_MR              mr
                                    );

int tsIbTavorMemoryDeregister(
                              tTS_IB_MR mr
                              );

int tsIbTavorFmrCreate(
                       tTS_IB_PD            pd,
                       tTS_IB_MEMORY_ACCESS access,
                       int                  max_pages,
                       int                  max_remaps,
                       tTS_IB_FMR           fmr
                       );

int tsIbTavorFmrDestroy(
                        tTS_IB_FMR fmr
                        );

int tsIbTavorFmrMap(
                    tTS_IB_FMR   fmr,
                    uint64_t    *page_list,
                    int          list_len,
                    uint64_t    *io_virtual_address,
                    uint64_t     iova_offset,
                    tTS_IB_LKEY *lkey,
                    tTS_IB_RKEY *rkey
                    );

int tsIbTavorFmrUnmap(
                      tTS_IB_DEVICE     device,
                      struct list_head *fmr_list
                      );

int tsIbTavorMulticastAttach(
                             tTS_IB_QP  qp,
                             tTS_IB_LID lid,
                             tTS_IB_GID gid
                             );

int tsIbTavorMulticastDetach(
                             tTS_IB_QP  qp,
                             tTS_IB_LID lid,
                             tTS_IB_GID gid
                             );

tTS_IB_MAD_RESULT tsIbTavorMadProcess(
                                      tTS_IB_DEVICE device,
                                      int           ignore_mkey,
                                      tTS_IB_MAD    in_mad,
                                      tTS_IB_MAD    response_mad
                                      );

void tsIbTavorSetNodeDesc(
                          tTS_IB_TAVOR_PRIVATE priv,
                          int                  index
                          );

static inline VAPI_mrw_acl_t tsIbTavorAccessTranslate(
                                                      tTS_IB_MEMORY_ACCESS access
                                                      ) {
  return (!!(access & TS_IB_ACCESS_LOCAL_WRITE)   * VAPI_EN_LOCAL_WRITE)  |
         (!!(access & TS_IB_ACCESS_REMOTE_WRITE)  * VAPI_EN_REMOTE_WRITE) |
         (!!(access & TS_IB_ACCESS_REMOTE_READ)   * VAPI_EN_REMOTE_READ)  |
         (!!(access & TS_IB_ACCESS_REMOTE_ATOMIC) * VAPI_EN_REMOTE_ATOM)  |
         (!!(access & TS_IB_ACCESS_ENABLE_WINDOW) * VAPI_EN_MEMREG_BIND);
}

#include "tavor_compat.h"

#endif /* _TAVOR_PRIV_H */
