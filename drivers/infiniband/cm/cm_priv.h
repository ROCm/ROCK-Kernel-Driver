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

  $Id: cm_priv.h,v 1.14 2004/02/25 00:35:12 roland Exp $
*/

#ifndef _CM_PRIV_H
#define _CM_PRIV_H

#ifndef __KERNEL__
#  define __KERNEL__
#endif
#ifndef MODULE
#  define MODULE
#endif

#ifndef W2K_OS
#include <linux/config.h>
#if defined(CONFIG_MODVERSIONS) && !defined(MODVERSIONS)
#  define MODVERSIONS
#endif

#if defined(MODVERSIONS) && !defined(__GENKSYMS__) && !defined(TS_KERNEL_2_6)
#  include <linux/modversions.h>
#endif
#endif

#ifdef W2K_OS
#include <ntddk.h>
#else
#include <asm/param.h>
#include <asm/semaphore.h>
#endif

/* XXX Move this compatibility code to a better place */
#ifdef W2K_OS
#  define schedule() do { } while (0)
#  define TS_WINDOWS_SPINLOCK_FLAGS unsigned long flags;
#  define spin_lock(s)       spin_lock_irqsave((s), flags)
#  define spin_unlock(s)     spin_unlock_irqrestore((s), flags)
#  define spin_lock_irq(s)   spin_lock_irqsave((s), flags)
#  define spin_unlock_irq(s) spin_unlock_irqrestore((s), flags)
#else
#  define TS_WINDOWS_SPINLOCK_FLAGS
#endif

#include "ts_ib_cm.h"
#include "ts_ib_core.h"
#include "ts_ib_mad_types.h"

#include "ts_kernel_hash.h"
#include "ts_kernel_timer.h"

#if !defined(TS_KERNEL_2_6)
#define work_struct tq_struct
#define INIT_WORK(x,y,z) INIT_TQUEUE(x,y,z)
#define schedule_work schedule_task
#endif

/* 16.7.2 */
enum tTS_IB_COM_MGT_METHOD {
  TS_IB_COM_MGT_GET     = 0x01,
  TS_IB_COM_MGT_SET     = 0x02,
  TS_IB_COM_MGT_GETRESP = 0x81,
  TS_IB_COM_MGT_SEND    = 0x03
};

/* 16.7.3 */
enum tTS_IB_COM_MGT_ATTRIBUTE_ID {
  TS_IB_COM_MGT_CLASS_PORT_INFO = 0x0001,
  TS_IB_COM_MGT_REQ             = 0x0010,
  TS_IB_COM_MGT_MRA             = 0x0011,
  TS_IB_COM_MGT_REJ             = 0x0012,
  TS_IB_COM_MGT_REP             = 0x0013,
  TS_IB_COM_MGT_RTU             = 0x0014,
  TS_IB_COM_MGT_DREQ            = 0x0015,
  TS_IB_COM_MGT_DREP            = 0x0016,
  TS_IB_COM_MGT_SIDR_REQ        = 0x0017,
  TS_IB_COM_MGT_SIDR_REP        = 0x0018,
  TS_IB_COM_MGT_LAP             = 0x0019,
  TS_IB_COM_MGT_APR             = 0x001a,
};

/* 12.6.6 */
enum tTS_IB_CM_MRA_MESSAGE {
  TS_IB_MRA_REQ = 0,
  TS_IB_MRA_REP = 1,
  TS_IB_MRA_LAP = 2
};

/* 12.6.7 */
enum tTS_IB_CM_REJ_MESSAGE {
  TS_IB_REJ_REQ        = 0,
  TS_IB_REJ_REP        = 1,
  TS_IB_REJ_NO_MESSAGE = 2
};

/* 12.9.5/12.9.6 */
typedef enum {
  TS_IB_CM_STATE_IDLE,
  TS_IB_CM_STATE_REQ_SENT,
  TS_IB_CM_STATE_REP_RECEIVED,
  TS_IB_CM_STATE_MRA_REP_SENT,
  TS_IB_CM_STATE_ESTABLISHED,
  TS_IB_CM_STATE_LISTEN,
  TS_IB_CM_STATE_REQ_RECEIVED,
  TS_IB_CM_STATE_MRA_SENT,
  TS_IB_CM_STATE_REP_SENT,
  TS_IB_CM_STATE_DREQ_SENT,
  TS_IB_CM_STATE_DREQ_RECEIVED,
  TS_IB_CM_STATE_TIME_WAIT
} tTS_IB_CM_STATE;

/* 12.11.2 */
enum tTS_IB_CM_SIDR_STATUS {
  TS_IB_SIDR_STATUS_VALID = 0,
  TS_IB_SIDR_STATUS_SERVICE_ID_NOT_SUPPORTED = 1,
  TS_IB_SIDR_STATUS_REJECTED = 2,
  TS_IB_SIDR_STATUS_NO_QP = 3
};

typedef struct tTS_IB_CM_CONNECTION_STRUCT tTS_IB_CM_CONNECTION_STRUCT,
  *tTS_IB_CM_CONNECTION;
typedef struct tTS_IB_CM_SERVICE_STRUCT tTS_IB_CM_SERVICE_STRUCT,
  *tTS_IB_CM_SERVICE;
typedef struct tTS_IB_CM_TREE_NODE_STRUCT tTS_IB_CM_TREE_NODE_STRUCT,
  *tTS_IB_CM_TREE_NODE;

/*
 * size and offset constants.
 */
enum {
  TS_IB_CM_REP_MAX_PRIVATE_DATA = 204,
  TS_IB_CM_REJ_MAX_PRIVATE_DATA = 204
};

struct tTS_IB_CM_CONNECTION_STRUCT {
  tTS_IB_CM_COMM_ID            local_comm_id;
  tTS_IB_CM_COMM_ID            remote_comm_id;
  tTS_IB_CM_STATE              state;
  uint64_t                     transaction_id;

  tTS_IB_QP_HANDLE             local_qp;
  tTS_IB_QPN                   local_qpn;
  tTS_IB_QPN                   remote_qpn;

  tTS_IB_PATH_RECORD_STRUCT    primary_path;
  tTS_IB_PATH_RECORD_STRUCT    alternate_path;
  tTS_IB_PSN                   receive_psn;
  tTS_IB_PSN                   send_psn;
  uint8_t                      retry_count;
  uint8_t                      rnr_retry_count;
  uint8_t                      responder_resources;
  uint8_t                      initiator_depth;

  tTS_IB_DEVICE_HANDLE         local_cm_device;
  tTS_IB_PORT                  local_cm_port;
  int                          local_cm_pkey_index;
  tTS_IB_LID                   remote_cm_lid;
  tTS_IB_QPN                   remote_cm_qpn;
  tTS_IB_LID                   alternate_remote_cm_lid;

  tTS_IB_CM_CALLBACK_FUNCTION  cm_function;
  void *                       cm_arg;

  int                          cm_retry_count;
  int                          max_cm_retries;
  int                          cm_response_timeout;
  unsigned long                establish_jiffies;
  tTS_KERNEL_TIMER_STRUCT      timer;
  struct work_struct           work;

  tTS_IB_MAD_STRUCT            mad;

  tTS_IB_CM_SERVICE            peer_to_peer_service;

  tTS_LOCKED_HASH_ENTRY_STRUCT entry;
  tTS_HASH_NODE_STRUCT         remote_comm_id_node;
  tTS_HASH_NODE_STRUCT         remote_qp_node;

  int                          callbacks_running;

  int                          active:1;
  int                          lap_pending:1;
  int                          establish_pending:1;
};

struct tTS_IB_CM_SERVICE_STRUCT {
  tTS_IB_CM_CALLBACK_FUNCTION cm_function;
  void *                      cm_arg;

  tTS_IB_CM_COMM_ID           peer_to_peer_comm_id;

  atomic_t                    waiters;
  int                         freeing;
  /* XXX this #if must be removed */
#ifndef W2K_OS
  struct semaphore            mutex;
#else
  KSEMAPHORE                  mutex;
#endif
  tTS_IB_CM_TREE_NODE         node;
};

struct tTS_IB_CM_DELAYED_ESTABLISH_STRUCT {
  struct work_struct work;
  tTS_IB_CM_COMM_ID  comm_id;
};

static inline int tsIbCmQpModify(
                                 tTS_IB_QP_HANDLE    qp,
                                 tTS_IB_QP_ATTRIBUTE attr
                                 ) {
  return qp != TS_IB_HANDLE_INVALID ? tsIbQpModify(qp, attr) : 0;
}

int tsIbCmTimeoutToJiffies(
                           int timeout
                           );

tTS_IB_PSN tsIbCmPsnGenerate(
                             void
                             );

uint64_t tsIbCmTidGenerate(
                           void
                           );

void tsIbMadBuildHeader(
                        tTS_IB_MAD packet
                        );

void tsIbCmWaitForCallbacks(
                            tTS_IB_CM_CONNECTION *connection
                            );

tTS_IB_CM_CALLBACK_RETURN tsIbCmConsumerCallback(
                                                 tTS_IB_CM_CONNECTION *connection,
                                                 tTS_IB_CM_STATE       state,
                                                 void                 *params
                                                 );

tTS_IB_CM_CALLBACK_RETURN tsIbCmConsumerFreeCallback(
                                                     tTS_IB_CM_CONNECTION *connection,
                                                     tTS_IB_CM_STATE       state,
                                                     void                 *params
                                                     );

void tsIbCmQpToError(
                     tTS_IB_QP_HANDLE qp
                     );

void tsIbCmConnectTimeout(
                          void *conn_ptr
                          );

void tsIbCmTimeWait(
                    tTS_IB_CM_CONNECTION         *connection,
                    tTS_IB_CM_DISCONNECTED_REASON reason
                    );

tTS_IB_CM_SERVICE tsIbCmServiceFind(
                                    uint64_t service_id
                                    );

int tsIbCmServiceCreate(
                        tTS_IB_SERVICE_ID  service_id,
                        tTS_IB_SERVICE_ID  service_mask,
                        tTS_IB_CM_SERVICE *service
                        );

void tsIbCmServiceFree(
                       tTS_IB_CM_SERVICE service
                       );

static inline void tsIbCmServicePut(
                                    tTS_IB_CM_SERVICE service
                                    ) {
  up(&service->mutex);
}

tTS_IB_CM_CONNECTION tsIbCmConnectionFind(
                                          tTS_IB_CM_COMM_ID local_comm_id
                                          );

tTS_IB_CM_CONNECTION tsIbCmConnectionFindInterruptible(
                                                       tTS_IB_CM_COMM_ID local_comm_id,
                                                       int              *status
                                                       );

tTS_IB_CM_CONNECTION tsIbCmConnectionFindRemoteQp(
                                                  tTS_IB_GID port_gid,
                                                  tTS_IB_QPN qpn
                                                  );

tTS_IB_CM_CONNECTION tsIbCmConnectionFindRemoteId(
                                                  tTS_IB_CM_COMM_ID comm_id
                                                  );

static inline void tsIbCmConnectionPut(
                                       tTS_IB_CM_CONNECTION connection
                                       ) {
  if (connection) {
    tsKernelLockedHashRelease(&connection->entry);
  }
}

tTS_IB_CM_CONNECTION tsIbCmConnectionNew(
                                         void
                                         );

void tsIbCmConnectionFree(
                          tTS_IB_CM_CONNECTION connection
                          );

void tsIbCmConnectionInsertRemote(
                                  tTS_IB_CM_CONNECTION connection
                                  );

void tsIbCmConnectionTableInit(
                               void
                               );

void tsIbCmConnectionTableCleanup(
                                  void
                                  );

void tsIbCmServiceTableInit(
                            void
                            );

void tsIbCmServiceTableCleanup(
                               void
                               );

int tsIbCmDropConsumerInternal(
                               tTS_IB_CM_CONNECTION connection
                               );

void tsIbCmDelayedEstablish(
                            void *est_ptr
                            );

int tsIbCmPassiveRts(
                     tTS_IB_CM_CONNECTION connection
                     );

int tsIbCmPassiveParamStore(
                            tTS_IB_CM_CONNECTION    connection,
                            tTS_IB_CM_PASSIVE_PARAM param
                            );

void tsIbCmReqHandler(
                      tTS_IB_MAD packet
                      );

void tsIbCmMraHandler(
                      tTS_IB_MAD packet
                      );

void tsIbCmRejHandler(
                      tTS_IB_MAD packet
                      );

void tsIbCmRepHandler(
                      tTS_IB_MAD packet
                      );

void tsIbCmRtuHandler(
                      tTS_IB_MAD packet
                      );

void tsIbCmDreqHandler(
                       tTS_IB_MAD packet
                       );

void tsIbCmDrepHandler(
                       tTS_IB_MAD packet
                       );

void tsIbCmLapHandler(
                      tTS_IB_MAD packet
                      );

void tsIbCmAprHandler(
                      tTS_IB_MAD packet
                      );

int tsIbCmReqSend(
                  tTS_IB_CM_CONNECTION connection,
                  tTS_IB_SERVICE_ID    service_id,
                  void                *req_private_data,
                  int                  req_private_data_len
                  );

int tsIbCmDreqSend(
                   tTS_IB_CM_CONNECTION connection
                   );

int tsIbCmLapSend(
                  tTS_IB_CM_CONNECTION connection,
                  tTS_IB_PATH_RECORD   alternate_path
                  );

int tsIbCmReqQpSetup(
                     tTS_IB_CM_CONNECTION connection,
                     void   *reply_data,
                     int     reply_size
                     );

int tsIbCmMraSend(
                  tTS_IB_CM_CONNECTION connection,
                  int                  service_timeout,
                  void                *mra_private_data,
                  int                  mra_private_data_len
                  );

int tsIbCmRejSend(
                  tTS_IB_DEVICE_HANDLE local_cm_device,
                  tTS_IB_PORT          local_cm_port,
                  int                  pkey_index,
		  tTS_IB_LID           remote_cm_lid,
		  uint32_t             remote_cm_qpn,
		  uint64_t             transaction_id,
                  tTS_IB_CM_COMM_ID    local_comm_id,
                  tTS_IB_CM_COMM_ID    remote_comm_id,
		  int                  type,
                  int                  reason,
		  void                *reply_data,
		  int                  reply_size
		  );

int tsIbCmRtuSend(
		  tTS_IB_CM_CONNECTION connection
		  );

void tsIbCmRtuDone(
                   void *conn_ptr
                   );

/* /proc count functions */

void tsIbCmCountReceive(
                        tTS_IB_MAD packet
                        );

void tsIbCmCountSend(
                     tTS_IB_MAD packet
                     );

void tsIbCmCountResend(
                       tTS_IB_MAD packet
                       );

int tsIbCmProcInit(
                   void
                   );

void tsIbCmProcCleanup(
                       void
                       );

#endif /* _CM_PRIV_H */
