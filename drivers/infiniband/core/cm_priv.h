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

  $Id: cm_priv.h 32 2004-04-09 03:57:42Z roland $
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
enum {
	IB_COM_MGT_GET     = 0x01,
	IB_COM_MGT_SET     = 0x02,
	IB_COM_MGT_GETRESP = 0x81,
	IB_COM_MGT_SEND    = 0x03
};

/* 16.7.3 */
enum {
	IB_COM_MGT_CLASS_PORT_INFO = 0x0001,
	IB_COM_MGT_REQ             = 0x0010,
	IB_COM_MGT_MRA             = 0x0011,
	IB_COM_MGT_REJ             = 0x0012,
	IB_COM_MGT_REP             = 0x0013,
	IB_COM_MGT_RTU             = 0x0014,
	IB_COM_MGT_DREQ            = 0x0015,
	IB_COM_MGT_DREP            = 0x0016,
	IB_COM_MGT_SIDR_REQ        = 0x0017,
	IB_COM_MGT_SIDR_REP        = 0x0018,
	IB_COM_MGT_LAP             = 0x0019,
	IB_COM_MGT_APR             = 0x001a,
};

/* 12.6.6 */
enum {
	IB_MRA_REQ = 0,
	IB_MRA_REP = 1,
	IB_MRA_LAP = 2
};

/* 12.6.7 */
enum {
	IB_REJ_REQ        = 0,
	IB_REJ_REP        = 1,
	IB_REJ_NO_MESSAGE = 2
};

/* 12.9.5/12.9.6 */
enum {
	IB_CM_STATE_IDLE,
	IB_CM_STATE_REQ_SENT,
	IB_CM_STATE_REP_RECEIVED,
	IB_CM_STATE_MRA_REP_SENT,
	IB_CM_STATE_ESTABLISHED,
	IB_CM_STATE_LISTEN,
	IB_CM_STATE_REQ_RECEIVED,
	IB_CM_STATE_MRA_SENT,
	IB_CM_STATE_REP_SENT,
	IB_CM_STATE_DREQ_SENT,
	IB_CM_STATE_DREQ_RECEIVED,
	IB_CM_STATE_TIME_WAIT
};

/* 12.11.2 */
enum {
	IB_SIDR_STATUS_VALID = 0,
	IB_SIDR_STATUS_SERVICE_ID_NOT_SUPPORTED = 1,
	IB_SIDR_STATUS_REJECTED = 2,
	IB_SIDR_STATUS_NO_QP = 3
};

/*
 * size and offset constants.
 */
enum {
	IB_CM_REP_MAX_PRIVATE_DATA = 204,
	IB_CM_REJ_MAX_PRIVATE_DATA = 204
};

struct ib_cm_connection {
	tTS_IB_CM_COMM_ID            local_comm_id;
	tTS_IB_CM_COMM_ID            remote_comm_id;
	int                          state;
	uint64_t                     transaction_id;

	tTS_IB_QP_HANDLE             local_qp;
	tTS_IB_QPN                   local_qpn;
	tTS_IB_QPN                   remote_qpn;

	struct ib_path_record        primary_path;
	struct ib_path_record        alternate_path;
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
	void                        *cm_arg;

	int                          cm_retry_count;
	int                          max_cm_retries;
	int                          cm_response_timeout;
	unsigned long                establish_jiffies;
	tTS_KERNEL_TIMER_STRUCT      timer;
	struct work_struct           work;

	struct ib_mad                mad;

	struct ib_cm_service        *peer_to_peer_service;

	tTS_LOCKED_HASH_ENTRY_STRUCT entry;
	tTS_HASH_NODE_STRUCT         remote_comm_id_node;
	tTS_HASH_NODE_STRUCT         remote_qp_node;

	int                          callbacks_running;

	int                          active:1;
	int                          lap_pending:1;
	int                          establish_pending:1;
};

struct ib_cm_service {
	tTS_IB_CM_CALLBACK_FUNCTION cm_function;
	void *                      cm_arg;

	tTS_IB_CM_COMM_ID           peer_to_peer_comm_id;

	atomic_t                    waiters;
	int                         freeing;
	struct semaphore            mutex;
	struct ib_cm_tree_node     *node;
};

struct ib_cm_delayed_establish {
	struct work_struct work;
	tTS_IB_CM_COMM_ID  comm_id;
};

static inline int ib_cm_qp_modify(tTS_IB_QP_HANDLE        qp,
				  struct ib_qp_attribute *attr)
{
	return qp != TS_IB_HANDLE_INVALID ? ib_qp_modify(qp, attr) : 0;
}

int ib_cm_timeout_to_jiffies(int timeout);

tTS_IB_PSN ib_cm_psn_generate(void);
uint64_t   ib_cm_tid_generate(void);

void ib_mad_build_header(struct ib_mad *packet);
void ib_cm_wait_for_callbacks(struct ib_cm_connection **connection);

tTS_IB_CM_CALLBACK_RETURN ib_cm_consumer_callback(struct ib_cm_connection **connection,
						  tTS_IB_CM_EVENT           event,
						  void                     *params);
tTS_IB_CM_CALLBACK_RETURN ib_cm_consumer_free_callback(struct ib_cm_connection **connection,
						       tTS_IB_CM_EVENT           event,
						       void                     *params);

void ib_cm_qp_to_error(tTS_IB_QP_HANDLE qp);
void ib_cm_connect_timeout(void *conn_ptr);
void ib_cm_timewait(struct ib_cm_connection      **connection,
                    tTS_IB_CM_DISCONNECTED_REASON  reason);

struct ib_cm_service *ib_cm_service_find(uint64_t service_id);
int ib_cm_service_create(tTS_IB_SERVICE_ID      service_id,
			 tTS_IB_SERVICE_ID      service_mask,
			 struct ib_cm_service **service);

void ib_cm_service_free(struct ib_cm_service *service);

static inline void ib_cm_service_put(struct ib_cm_service *service)
{
	up(&service->mutex);
}

struct ib_cm_connection *ib_cm_connection_find(tTS_IB_CM_COMM_ID local_comm_id);
struct ib_cm_connection *ib_cm_connection_find_interruptible(tTS_IB_CM_COMM_ID local_comm_id,
							     int              *status);


struct ib_cm_connection *ib_cm_connection_find_remote_qp(tTS_IB_GID port_gid,
							 tTS_IB_QPN qpn);
struct ib_cm_connection *ib_cm_connection_find_remote_id(tTS_IB_CM_COMM_ID comm_id);
static inline void ib_cm_connection_put(struct ib_cm_connection *connection)
{
	if (connection)
		tsKernelLockedHashRelease(&connection->entry);
}

struct ib_cm_connection *ib_cm_connection_new(void);
void ib_cm_connection_free(struct ib_cm_connection *connection);

void ib_cm_connection_insert_remote(struct ib_cm_connection *connection);

void ib_cm_connection_table_init(void);
void ib_cm_connection_table_cleanup(void);
void ib_cm_service_table_init(void);
void ib_cm_service_table_cleanup(void);

int ib_cm_drop_consumer_internal(struct ib_cm_connection *connection);
void ib_cm_delayed_establish(void *est_ptr);
int ib_cm_passive_rts(struct ib_cm_connection *connection);
int ib_cm_passive_param_store(struct ib_cm_connection    *connection,
			      struct ib_cm_passive_param *param);

void ib_cm_req_handler(struct ib_mad *packet);
void ib_cm_mra_handler(struct ib_mad *packet);
void ib_cm_rej_handler(struct ib_mad *packet);
void ib_cm_rep_handler(struct ib_mad *packet);
void ib_cm_rtu_handler(struct ib_mad *packet);
void ib_cm_dreq_handler(struct ib_mad *packet);
void ib_cm_drep_handler(struct ib_mad *packet);
void ib_cm_lap_handler(struct ib_mad *packet);
void ib_cm_apr_handler(struct ib_mad *packet);

int ib_cm_req_send(struct ib_cm_connection *connection,
		   tTS_IB_SERVICE_ID  	    service_id,
		   void                	   *req_private_data,
		   int                 	    req_private_data_len);
int ib_cm_dreq_send(struct ib_cm_connection *connection);
int ib_cm_lap_send(struct ib_cm_connection *connection,
		   struct ib_path_record   *alternate_path);

int ib_cm_req_qp_setup(struct ib_cm_connection *connection,
		       void                    *reply_data,
		       int                      reply_size);

int ib_cm_mra_send(struct ib_cm_connection *connection,
		   int                 	    service_timeout,
		   void                	   *mra_private_data,
		   int                 	    mra_private_data_len);

int ib_cm_rej_send(tTS_IB_DEVICE_HANDLE local_cm_device,
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
		   int                  reply_size);

int ib_cm_rtu_send(struct ib_cm_connection *connection);
void ib_cm_rtu_done(void *conn_ptr);

/* /proc count functions */

void ib_cm_count_receive(struct ib_mad *packet);
void ib_cm_count_send(struct ib_mad *packet);
void ib_cm_count_resend(struct ib_mad *packet);
int  ib_cm_proc_init(void);
void ib_cm_proc_cleanup(void);

#endif /* _CM_PRIV_H */

/*
  Local Variables:
  c-file-style: "linux"
  indent-tabs-mode: t
  End:
*/
