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

  $Id: useraccess_cm.c 40 2004-04-10 19:24:27Z roland $
*/

/* FIXME: We should validate the QP handle given back from userspace */

#define TS_IB_QP_STATE_INVALID (-1)
#define TS_IB_UCM_MRA_TIMEOUT 27     /* around 9 minutes */

#include "useraccess_cm.h"

#include "ts_kernel_trace.h"
#include "ts_kernel_services.h"
#include "ts_ib_tavor_provider.h"

#ifndef W2K_OS
/* for path record lookup code */
#include "ip2pr_export.h"
#include <linux/config.h>
#include <linux/version.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/errno.h>
#if defined(TS_KERNEL_2_6)
#include <linux/cdev.h>
#endif
#else
#include "host_win/sdp/proxy/sdp_inet.h"
#include <w2k.h>
#include <os_dep/win/linux/module.h>
#include "useraccess_main_w2k.h"

enum {
  TS_USERACCESS_NUM_DEVICE = 16
};

PDEVICE_OBJECT pdoArray[TS_USERACCESS_NUM_DEVICE];
#endif

MODULE_AUTHOR("Johannes Erdfelt");
MODULE_DESCRIPTION("kernel IB CM userspace access");
MODULE_LICENSE("Dual BSD/GPL");

/* This is used to queue up completions out to userspace */
struct tTS_IB_CM_USER_COMPLETION {
  struct list_head         list;

  tTS_IB_QP_HANDLE         qp;
  tTS_IB_CM_COMM_ID        comm_id;
  tTS_IB_LISTEN_HANDLE     listen_handle;
  unsigned long            cm_arg;
  tTS_IB_CM_EVENT          event;

  void                    *params;
  ssize_t                  params_size;

  tTS_IB_QP_STATE          qp_state;
};

typedef struct tTS_IB_CM_USER_COMPLETION tTS_IB_CM_USER_COMPLETION_STRUCT,
  *tTS_IB_CM_USER_COMPLETION;

enum {
  TS_USERACCESS_CM_NUM_DEVICE = 16
};

#if defined(TS_KERNEL_2_6)
static dev_t          useraccess_cm_devnum;
static struct cdev    useraccess_cm_cdev;
#else
static int            useraccess_cm_major;
static devfs_handle_t useraccess_cm_devfs_dir;
#endif

static const char     TS_USERACCESS_CM_NAME[] = "ts_ib_useraccess_cm";

static tTS_IB_USERACCESS_CM_DEVICE_STRUCT useraccess_cm_dev_list[TS_USERACCESS_CM_NUM_DEVICE];

/* FIXME: We need to handle this cleaner */
int tsIbTavorDeviceQueryVapiHandle(
                                   tTS_IB_DEVICE_HANDLE     device,
                                   VAPI_hca_hndl_t         *vapi_handle
                                   );

#ifndef W2K_OS
static tTS_IB_CM_USER_CONNECTION tsIbCmUserConnectionAlloc(
                                                           tTS_IB_USERACCESS_CM_PRIVATE priv,
                                                           int flags
                                                           )
#else
static tTS_IB_CM_USER_CONNECTION tsIbCmUserConnectionAlloc(
                                                           tTS_IB_USERACCESS_CM_PRIVATE priv
                                                           )
#endif
{
  tTS_IB_CM_USER_CONNECTION cm_conn;

  /* Get any memory allocate errors out of the way first */
#ifndef W2K_OS
  cm_conn = kmalloc(sizeof(*cm_conn), flags);
#else
  cm_conn = kmalloc(sizeof(*cm_conn), GFP_KERNEL);
#endif
  if (NULL == cm_conn) {
    return NULL;
  }

  atomic_set(&cm_conn->refcnt, 2);	/* One for UCM, one for calling func */
  INIT_LIST_HEAD(&cm_conn->list);

  cm_conn->priv = priv;
  cm_conn->comm_id = TS_IB_CM_COMM_ID_INVALID;
  cm_conn->listen_handle = NULL;
  cm_conn->cm_arg = 0;
  cm_conn->v_device = VAPI_INVAL_HNDL;
  cm_conn->qp = TS_IB_HANDLE_INVALID;
  cm_conn->vk_qp = VAPI_INVAL_HNDL;

  return cm_conn;
}

static void tsIbCmUserConnectionGet(
                                    tTS_IB_CM_USER_CONNECTION cm_conn
                                    )
{
  atomic_inc(&cm_conn->refcnt);
}

static void tsIbCmUserConnectionPut(
                                    tTS_IB_CM_USER_CONNECTION cm_conn
                                    )
{
  if (atomic_dec_and_test(&cm_conn->refcnt)) {
    cm_conn->priv = (void *)0x5a5a5a5a;
    kfree(cm_conn);
  }
}

static tTS_IB_CM_USER_CONNECTION tsIbCmUserConnectionFind(
                                                          tTS_IB_USERACCESS_CM_PRIVATE priv,
                                                          tTS_IB_CM_COMM_ID comm_id
                                                          )
{
  struct list_head *ptr;

  down(&priv->cm_conn_sem);
  list_for_each(ptr, &priv->cm_conn_list) {
    tTS_IB_CM_USER_CONNECTION cm_conn;

    cm_conn = list_entry(ptr, struct tTS_IB_CM_USER_CONNECTION, list);

    if (cm_conn->comm_id == comm_id) {
      tsIbCmUserConnectionGet(cm_conn);
      up(&priv->cm_conn_sem);

      return cm_conn;
    }
  }
  up(&priv->cm_conn_sem);

  return NULL;
}

static void _tsIbCmUserConnectionDestroy(
                                         tTS_IB_CM_USER_CONNECTION cm_conn
                                         )
{
  tTS_IB_USERACCESS_CM_PRIVATE priv = cm_conn->priv;
  unsigned long flags;
  struct list_head *ptr, *tmp;

  if (cm_conn->vk_qp != VAPI_INVAL_HNDL) {
#ifndef W2K_OS // Temporary as Mellanox 006 code doesn't provide this fn()
    EVAPI_k_clear_destroy_qp_cbk(cm_conn->v_device, cm_conn->vk_qp);
#endif
    cm_conn->vk_qp = VAPI_INVAL_HNDL;
  }

  if (cm_conn->listen_handle != TS_IB_HANDLE_INVALID) {
    tsIbCmListenStop(cm_conn->listen_handle);
    cm_conn->listen_handle = TS_IB_HANDLE_INVALID;
  }

  if (cm_conn->comm_id != TS_IB_CM_COMM_ID_INVALID) {
    /* Free up any completions */
    spin_lock_irqsave(&priv->cm_comp_lock, flags);
    list_for_each_safe(ptr, tmp, &priv->cm_comp_list) {
      tTS_IB_CM_USER_COMPLETION cm_comp;

      cm_comp = list_entry(ptr, struct tTS_IB_CM_USER_COMPLETION, list);

      if (cm_comp->comm_id == cm_conn->comm_id) {
        list_del_init(&cm_comp->list);
        kfree(cm_comp);
      }
    }
    spin_unlock_irqrestore(&priv->cm_comp_lock, flags);

    /* This will drop all state for this connection */
    tsIbCmKill(cm_conn->comm_id);
  }

  tsIbCmUserConnectionPut(cm_conn);		/* drop the structure */
}

static void tsIbCmUserConnectionDestroy(
                                        tTS_IB_CM_USER_CONNECTION cm_conn
                                        )
{
  tTS_IB_USERACCESS_CM_PRIVATE priv = cm_conn->priv;

  down(&priv->cm_conn_sem);
  list_del_init(&cm_conn->list);

  _tsIbCmUserConnectionDestroy(cm_conn);
  up(&priv->cm_conn_sem);
}

/* User Mode CM Filter Routines */
int tsIbCmUserGetServiceId(
                           unsigned long arg
                           )
{
  tTS_IB_SERVICE_ID service_id;

  service_id = (tTS_IB_SERVICE_ID)tsIbCmServiceAssign();

  return copy_to_user((tTS_IB_SERVICE_ID *) arg,
                      &service_id,
                      sizeof service_id) ? -EFAULT : 0;
}

static tTS_IB_CM_CALLBACK_RETURN tsIbCmUserCompletionHandler(
                                                             tTS_IB_CM_EVENT event,
                                                             tTS_IB_CM_COMM_ID comm_id,
                                                             void *params,
                                                             void *arg
                                                             )
{
  tTS_IB_CM_USER_CONNECTION cm_conn = (tTS_IB_CM_USER_CONNECTION)arg;
  tTS_IB_CM_USER_COMPLETION cm_comp;
  unsigned long flags;
  tTS_IB_CM_CALLBACK_RETURN ret = TS_IB_CM_CALLBACK_PROCEED;
  int rc;

  if (!cm_conn) {
    return TS_IB_CM_CALLBACK_ABORT;
  }

  tsIbCmUserConnectionGet(cm_conn);	/* since we can possibly sleep */

  /* If we've destroyed the conn, don't worry about completions to userspace */
  if (list_empty(&cm_conn->list)) {
    goto out;
  }

  /* The CM will be destroying the connection anyway, so clean up our state */
  if (event == TS_IB_CM_IDLE) {
    down(&cm_conn->priv->cm_conn_sem);
    if (!list_empty(&cm_conn->list)) {
      list_del_init(&cm_conn->list);
      up(&cm_conn->priv->cm_conn_sem);

      /* Don't free up the completions. They need to go down to userspace */

#ifndef W2K_OS // Temporary as Mellanox 006 code doesn't provide this fn()
      EVAPI_k_clear_destroy_qp_cbk(cm_conn->v_device, cm_conn->vk_qp);
#endif

      /* Cleanup finished below */
    } else if (cm_conn->vk_qp != VAPI_INVAL_HNDL) {
      /* FIXME: Can we clean this code up a little bit? */
      up(&cm_conn->priv->cm_conn_sem);

      /* Drop this completion on the floor, we don't exist anymore */
      goto out;
    }
  }

  cm_comp = kmalloc(sizeof(*cm_comp), GFP_KERNEL);
  if (!cm_comp) {
    TS_REPORT_WARN(MOD_KERNEL_IB,
                   "Unable to allocate memory for CM completion buffer");
    goto out;
  }

  cm_comp->qp = cm_conn->qp;
  cm_comp->comm_id = comm_id;
  cm_comp->listen_handle = cm_conn->listen_handle;
  cm_comp->cm_arg = cm_conn->cm_arg;
  cm_comp->event = event;

  if (cm_conn->qp != TS_IB_HANDLE_INVALID) {
    struct ib_qp_attribute qp_attr;

    qp_attr.valid_fields = TS_IB_QP_ATTRIBUTE_STATE;
    rc = tsIbQpQuery(cm_conn->qp, &qp_attr);
    if (rc == 0) {
      cm_comp->qp_state = qp_attr.state;
    } else {
      cm_comp->qp_state = TS_IB_QP_STATE_INVALID;
    }
  } else {
    cm_comp->qp_state = TS_IB_QP_STATE_INVALID;
  }

  switch (event) {
  case TS_IB_CM_REQ_RECEIVED:
    {
      tTS_IB_CM_REQ_RECEIVED_PARAM req_param =
        (tTS_IB_CM_REQ_RECEIVED_PARAM)params;
      tTS_IB_U_CM_REQ_RECEIVED_PARAM u_req_param;
      tTS_IB_CM_USER_CONNECTION ncm_conn;

      cm_comp->params_size = sizeof(*u_req_param) +
                             req_param->remote_private_data_len;
      cm_comp->params = kmalloc(cm_comp->params_size, GFP_KERNEL);
      if (!cm_comp->params) {
        TS_REPORT_WARN(MOD_KERNEL_IB,
                       "Unable to allocate memory for REQ_RECV param struct");
        kfree(cm_comp);
        goto out;
      }

      u_req_param = (tTS_IB_U_CM_REQ_RECEIVED_PARAM)cm_comp->params;

      u_req_param->listen_handle = req_param->listen_handle;
      u_req_param->service_id = req_param->service_id;
      u_req_param->local_qpn = req_param->local_qpn;
      u_req_param->remote_qpn = req_param->remote_qpn;

      memcpy(u_req_param->remote_guid, req_param->remote_guid, sizeof(u_req_param->remote_guid));
      memcpy(u_req_param->dgid, req_param->dgid, sizeof(u_req_param->dgid));
      memcpy(u_req_param->sgid, req_param->sgid, sizeof(u_req_param->sgid));

      u_req_param->dlid = req_param->dlid;
      u_req_param->slid = req_param->slid;
      u_req_param->port = req_param->port;

      u_req_param->remote_private_data_len = req_param->remote_private_data_len;

#ifndef W2K_OS
      memcpy(cm_comp->params + sizeof(*u_req_param),
             req_param->remote_private_data,
             req_param->remote_private_data_len);
#else
      memcpy((uint8_t *)cm_comp->params + sizeof(*u_req_param),
             req_param->remote_private_data,
             req_param->remote_private_data_len);
#endif
      /*
       * We also need to create a new connection structure and update the
       * callback argument.
       */
#ifndef W2K_OS
      ncm_conn = tsIbCmUserConnectionAlloc(cm_conn->priv, GFP_KERNEL);
#else
      ncm_conn = tsIbCmUserConnectionAlloc(cm_conn->priv);
#endif

      /*
       * Send an MRA
       */
      rc = tsIbCmDelayResponse(comm_id, TS_IB_UCM_MRA_TIMEOUT, NULL, 0);
      if (rc)
	  TS_REPORT_WARN(MOD_KERNEL_IB, "tsIbCmDelayResponse() failed: %d",
			 rc);

      tsIbCmCallbackModify(comm_id, tsIbCmUserCompletionHandler, ncm_conn);
      if (NULL != ncm_conn) {
        tTS_IB_USERACCESS_CM_PRIVATE priv = ncm_conn->priv;

        ncm_conn->comm_id = comm_id;

        down(&priv->cm_conn_sem);
        list_add(&ncm_conn->list, &priv->cm_conn_list);
        up(&priv->cm_conn_sem);

        tsIbCmUserConnectionPut(ncm_conn);	/* for the Alloc() */
      }

      ret = TS_IB_CM_CALLBACK_DEFER;
    }
    break;
  case TS_IB_CM_IDLE:
    {
      tTS_IB_USERACCESS_CM_PRIVATE priv = cm_conn->priv;
      tTS_IB_CM_IDLE_PARAM idle_param =
        (tTS_IB_CM_IDLE_PARAM)params;
      tTS_IB_U_CM_IDLE_PARAM u_idle_param;

      cm_comp->params_size = sizeof(*u_idle_param) +
                             idle_param->rej_info_len;
      cm_comp->params = kmalloc(cm_comp->params_size, GFP_KERNEL);
      if (!cm_comp->params) {
        TS_REPORT_WARN(MOD_KERNEL_IB,
                       "Unable to allocate memory for IDLE param struct");
        kfree(cm_comp);
        goto out;
      }

      u_idle_param = (tTS_IB_U_CM_IDLE_PARAM)cm_comp->params;

      u_idle_param->reason = idle_param->reason;
      u_idle_param->rej_reason = idle_param->rej_reason;

      u_idle_param->rej_info_len = idle_param->rej_info_len;

#ifndef W2K_OS
      memcpy(cm_comp->params + sizeof(*u_idle_param),
             idle_param->rej_info,
             idle_param->rej_info_len);
#else
      memcpy((uint8_t *)cm_comp->params + sizeof(*u_idle_param),
             idle_param->rej_info,
             idle_param->rej_info_len);
#endif

      /* Final cleanup of connection */
      cm_conn->qp = TS_IB_HANDLE_INVALID;

#ifndef W2K_OS // Temporary as Mellanox 006 code doesn't provide this fn()
      EVAPI_k_clear_destroy_qp_cbk(cm_conn->v_device, cm_conn->vk_qp);
#endif
      cm_conn->vk_qp = VAPI_INVAL_HNDL;

      /*
       * We don't use ConnectionDestroy() since it will delete the completion
       * for the IDLE state change that we want to be delivered to userspace
       */
      down(&priv->cm_conn_sem);
      list_del_init(&cm_conn->list);
      up(&priv->cm_conn_sem);

      /* We shouldn't get anymore callbacks, but just in case... */
      /* No more just in case, since we can deadlock here */
#if 0
      tsIbCmCallbackModify(cm_conn->comm_id, NULL, NULL);
#endif

      tsIbCmUserConnectionPut(cm_conn);		/* drop the structure */
    }
    break;
  case TS_IB_CM_REP_RECEIVED:
    {
      tTS_IB_CM_REP_RECEIVED_PARAM rep_param =
        (tTS_IB_CM_REP_RECEIVED_PARAM)params;
      tTS_IB_U_CM_REP_RECEIVED_PARAM u_rep_param;

      cm_comp->params_size = sizeof(*u_rep_param) +
                             rep_param->remote_private_data_len;
      cm_comp->params = kmalloc(cm_comp->params_size, GFP_KERNEL);
      if (!cm_comp->params) {
        TS_REPORT_WARN(MOD_KERNEL_IB,
                       "Unable to allocate memory for REP_RECV param struct");
        kfree(cm_comp);
        goto out;
      }

      u_rep_param = (tTS_IB_U_CM_REP_RECEIVED_PARAM)cm_comp->params;

      u_rep_param->local_qpn = rep_param->local_qpn;
      u_rep_param->remote_qpn = rep_param->remote_qpn;

      u_rep_param->remote_private_data_len = rep_param->remote_private_data_len;

#ifndef W2K_OS
      memcpy(cm_comp->params + sizeof(*u_rep_param),
             rep_param->remote_private_data,
             rep_param->remote_private_data_len);
#else
      memcpy((uint8_t *)cm_comp->params + sizeof(*u_rep_param),
             rep_param->remote_private_data,
             rep_param->remote_private_data_len);
#endif
      ret = TS_IB_CM_CALLBACK_DEFER;
    }
    break;
  case TS_IB_CM_ESTABLISHED:
    /* tTS_IB_CM_ESTABLISHED_PARAM is 0 bytes */
    cm_comp->params = NULL;
    cm_comp->params_size = 0;
    break;
  case TS_IB_CM_DISCONNECTED:
    {
      tTS_IB_CM_DISCONNECTED_PARAM disconnected_param =
        (tTS_IB_CM_DISCONNECTED_PARAM)params;
      tTS_IB_U_CM_DISCONNECTED_PARAM u_disconnected_param;

      cm_comp->params_size = sizeof(*u_disconnected_param);
      cm_comp->params = kmalloc(cm_comp->params_size, GFP_KERNEL);
      if (!cm_comp->params) {
        TS_REPORT_WARN(MOD_KERNEL_IB,
                       "Unable to allocate memory for DISCONNECTED param struct");
        kfree(cm_comp);
        goto out;
      }

      u_disconnected_param = (tTS_IB_U_CM_DISCONNECTED_PARAM)cm_comp->params;

      u_disconnected_param->reason = disconnected_param->reason;
    }
    break;
  /* FIXME: Probably should do something on receiving these messages */
  case TS_IB_CM_LAP_RECEIVED:
  case TS_IB_CM_APR_RECEIVED:
    cm_comp->params = NULL;
    cm_comp->params_size = 0;
    break;
  }

  spin_lock_irqsave(&cm_conn->priv->cm_comp_lock, flags);
  list_add_tail(&cm_comp->list, &cm_conn->priv->cm_comp_list);
  spin_unlock_irqrestore(&cm_conn->priv->cm_comp_lock, flags);

  wake_up_interruptible(&cm_conn->priv->cm_comp_wait);

out:
  tsIbCmUserConnectionPut(cm_conn);	/* for the Get() in the beginning */

  return ret;
}

#ifndef W2K_OS
struct pathrecordlookup {
  struct semaphore      sem;
  int                   status;
  struct ib_path_record path_record;
};

static int tsIbUserPathRecordCompletion(
		                        tIP2PR_PATH_LOOKUP_ID plid,
                                        int32_t status,
                                        uint32_t src_addr,
                                        uint32_t dst_addr,
                                        tTS_IB_PORT hw_port,
					tTS_IB_DEVICE_HANDLE ca,
                                        tTS_IB_PATH_RECORD path,
                                        tPTR usr_arg
					)
{
  struct pathrecordlookup *prl = (struct pathrecordlookup *)usr_arg;

  prl->status = status;
  if (!status) {
    memcpy(&prl->path_record, path, sizeof(prl->path_record));
  }

  up(&prl->sem);

  return 0;
}

int tsIbUserPathRecord(
                       tTS_IB_USERACCESS_CM_PRIVATE priv,
                       unsigned long arg
                       )
{
  struct tTS_IB_PATH_RECORD_IOCTL pathrecord_ioctl;
  tIP2PR_PATH_LOOKUP_ID plid;
  struct pathrecordlookup prl;
  int rc;

  if (copy_from_user(&pathrecord_ioctl,
                     (tTS_IB_PATH_RECORD_IOCTL) arg,
                     sizeof pathrecord_ioctl)) {
    return -EFAULT;
  }

  sema_init(&prl.sem, 0);

  rc = tsIp2prPathRecordLookup(pathrecord_ioctl.dst_addr,
                             0, 0, 0,
                             tsIbUserPathRecordCompletion,
                             &prl,
                             &plid);
  if (rc < 0) {
    return rc;
  }

  rc = down_interruptible(&prl.sem);
  if (rc) {
    tsIp2prPathRecordCancel(plid);
  }

  if (prl.status) {
    TS_REPORT_WARN(MOD_KERNEL_IB, "Path record lookup completion status: %d",
                   prl.status);
    return prl.status;
  }

  return copy_to_user(pathrecord_ioctl.path_record,
                      &prl.path_record,
                      sizeof prl.path_record) ? -EFAULT : 0;
}
#endif

void tsIbUserQpDestroyCallback(
                               VAPI_hca_hndl_t hca_hndl,
                               VAPI_k_qp_hndl_t k_qp_hndl,
                               void *private_data
                               )
{
  tTS_IB_CM_USER_CONNECTION cm_conn = (tTS_IB_CM_USER_CONNECTION)private_data;

  /* Destroying the callback is implicit when we received it */
  cm_conn->qp = TS_IB_HANDLE_INVALID;
  cm_conn->vk_qp = VAPI_INVAL_HNDL;

  tsIbCmUserConnectionDestroy(cm_conn);
}

int tsIbCmUserConnect(
                      tTS_IB_USERACCESS_CM_PRIVATE priv,
                      unsigned long arg
                      )
{
  tTS_IB_CM_CONNECT_IOCTL_STRUCT connect_ioctl;
  void *req_private_data;
  tTS_IB_CM_COMM_ID comm_id;
  tTS_IB_CM_USER_CONNECTION cm_conn;
  struct ib_path_record primary_path, alternate_path;
  struct ib_qp_attribute qp_attr;
  int rc, ret;
  VAPI_ret_t status;

  if (copy_from_user(&connect_ioctl,
                     (tTS_IB_CM_CONNECT_IOCTL) arg,
                     sizeof connect_ioctl)) {
    return -EFAULT;
  }

  if (connect_ioctl.primary_path) {
    if (copy_from_user(&primary_path,
                       (tTS_IB_PATH_RECORD) connect_ioctl.primary_path,
                       sizeof primary_path)) {
      return -EFAULT;
    }
  }

  if (connect_ioctl.alternate_path) {
    if (copy_from_user(&alternate_path,
                       (tTS_IB_PATH_RECORD) connect_ioctl.alternate_path,
                       sizeof alternate_path)) {
      return -EFAULT;
    }
  }

  if (connect_ioctl.req_private_data_len < 0) {
    return -EINVAL;
  }

  /* FIXME: Arbitrary limit */
  if (connect_ioctl.req_private_data_len > PAGE_SIZE) {
    return -E2BIG;
  }

  if (connect_ioctl.req_private_data_len != 0) {
    /* Allocate memory and copy it */
    req_private_data = kmalloc(connect_ioctl.req_private_data_len, GFP_KERNEL);
    if (req_private_data == NULL) {
      return -ENOMEM;
    }

    if (copy_from_user(req_private_data,
                       connect_ioctl.req_private_data,
                       connect_ioctl.req_private_data_len)) {
      rc = -EFAULT;
      goto out_free;
    }
  } else {
    req_private_data = NULL;
  }

  /* Get any memory allocate errors out of the way first */
#ifndef W2K_OS
  cm_conn = tsIbCmUserConnectionAlloc(priv, GFP_KERNEL);
#else
  cm_conn = tsIbCmUserConnectionAlloc(priv);
#endif
  if (NULL == cm_conn) {
    rc = -ENOMEM;
    goto out_free;
  }

  tsIbTavorDeviceQueryVapiHandle(priv->device->ib_device,
                                 &cm_conn->v_device);
  cm_conn->cm_arg = connect_ioctl.cm_arg;

#ifndef W2K_OS // TEMPORARY-----
  status = EVAPI_k_set_destroy_qp_cbk(cm_conn->v_device,
                                      connect_ioctl.k_qp_handle,
                                      tsIbUserQpDestroyCallback,
                                      cm_conn);

  switch (status) {
  case VAPI_OK:
    break;
  default:
    TS_REPORT_WARN(MOD_KERNEL_IB,
                   "EVAPI_k_set_destroy_qp_cbk, return code = %d (%s)",
                   status, VAPI_strerror(status));
    rc = -EINVAL;

    goto out_destroy;
  }
#endif
  cm_conn->qp = connect_ioctl.qp_handle;
  cm_conn->vk_qp = connect_ioctl.k_qp_handle;

  /* set RC timeout to a value of 16 */
  primary_path.packet_life = 15;

  {
    tTS_IB_CM_ACTIVE_PARAM_STRUCT active_param = {
      .qp                   = connect_ioctl.qp_handle,
      .req_private_data     = req_private_data,
      .req_private_data_len = connect_ioctl.req_private_data_len,
      .responder_resources  = connect_ioctl.responder_resources,
      .initiator_depth      = connect_ioctl.initiator_depth,
      .retry_count          = connect_ioctl.retry_count,
      .rnr_retry_count      = connect_ioctl.rnr_retry_count,
      .cm_response_timeout  = connect_ioctl.cm_response_timeout,
      .max_cm_retries       = connect_ioctl.max_cm_retries
    };

    rc = tsIbCmConnect(&active_param,
                       connect_ioctl.primary_path ? &primary_path : NULL,
                       connect_ioctl.alternate_path ? &alternate_path : NULL,
                       connect_ioctl.service_id,
                       0,
                       tsIbCmUserCompletionHandler,
                       cm_conn,
                       &comm_id);
  }

  if (0 == rc) {
    connect_ioctl.comm_id = comm_id;

    /* We can only add it to the list after we get the comm_id */
    cm_conn->comm_id = comm_id;

    down(&priv->cm_conn_sem);
    list_add(&cm_conn->list, &priv->cm_conn_list);
    up(&priv->cm_conn_sem);
  }

  /* use ret so we don't trash rc */
  qp_attr.valid_fields = TS_IB_QP_ATTRIBUTE_STATE;
  ret = tsIbQpQuery(cm_conn->qp, &qp_attr);
  if (ret == 0 && qp_attr.state >= 0) {
    connect_ioctl.qp_state = qp_attr.state;
  } else {
    connect_ioctl.qp_state = TS_IB_QP_STATE_INVALID;
  }

  if (copy_to_user((tTS_IB_CM_CONNECT_IOCTL) arg,
                      &connect_ioctl,
                      sizeof connect_ioctl)) {
    rc = -EFAULT;
  }

out_destroy:
  if (rc) {
    tsIbCmUserConnectionDestroy(cm_conn);	/* drop the structure */
  }

  tsIbCmUserConnectionPut(cm_conn);		/* for the Alloc() */

out_free:
  kfree(req_private_data);

  return rc;
}


int tsIbCmUserListen(
                     tTS_IB_USERACCESS_CM_PRIVATE priv,
                     unsigned long arg
                     )
{
  tTS_IB_CM_LISTEN_IOCTL_STRUCT listen_ioctl;
  tTS_IB_LISTEN_HANDLE listen_handle;
  tTS_IB_CM_USER_CONNECTION cm_conn;
  int rc;

  if (copy_from_user(&listen_ioctl,
                     (tTS_IB_CM_LISTEN_IOCTL) arg,
                     sizeof listen_ioctl)) {
    return -EFAULT;
  }

  /* Get any memory allocate errors out of the way first */
#ifndef W2K_OS
  cm_conn = tsIbCmUserConnectionAlloc(priv, GFP_KERNEL);
#else
  cm_conn = tsIbCmUserConnectionAlloc(priv);
#endif
  if (NULL == cm_conn) {
    return -ENOMEM;
  }

  cm_conn->cm_arg = listen_ioctl.cm_arg;

  rc = tsIbCmListen(listen_ioctl.service_id,
                    listen_ioctl.service_mask,
                    tsIbCmUserCompletionHandler,
                    cm_conn,
                    &listen_handle);
  if (0 == rc) {
    listen_ioctl.listen_handle = listen_handle;

    cm_conn->listen_handle = listen_handle;

    /* We can only add it to the list after we get the comm_id */
    down(&priv->cm_conn_sem);
    list_add(&cm_conn->list, &priv->cm_conn_list);
    up(&priv->cm_conn_sem);
  }

  if (copy_to_user((tTS_IB_CM_LISTEN_IOCTL) arg,
                      &listen_ioctl,
                      sizeof listen_ioctl)) {
    rc = -EFAULT;
  }

  if (rc) {
    tsIbCmUserConnectionDestroy(cm_conn);	/* drop the structure */
  }

  tsIbCmUserConnectionPut(cm_conn);		/* for the Alloc() */

  return rc;
}

int tsIbCmUserListenStop(
                         tTS_IB_USERACCESS_CM_PRIVATE priv,
                         unsigned long arg
                         )
{
  tTS_IB_LISTEN_HANDLE listen_handle = (tTS_IB_LISTEN_HANDLE)arg;
  struct list_head *ptr, *tmp;

  if (listen_handle == TS_IB_HANDLE_INVALID) {
    return -EINVAL;
  }

  down(&priv->cm_conn_sem);
  list_for_each_safe(ptr, tmp, &priv->cm_conn_list) {
    tTS_IB_CM_USER_CONNECTION cm_conn;
    int ret;

    cm_conn = list_entry(ptr, struct tTS_IB_CM_USER_CONNECTION, list);

    if (cm_conn->listen_handle == listen_handle) {
      cm_conn->listen_handle = TS_IB_HANDLE_INVALID;
      list_del_init(&cm_conn->list);

      up(&priv->cm_conn_sem);

      ret = tsIbCmListenStop(listen_handle);

      tsIbCmUserConnectionPut(cm_conn);		/* drop the structure */

      return ret;
    }
  }
  up(&priv->cm_conn_sem);

  return -ENOENT;
}

int tsIbCmUserAccept(
                     tTS_IB_USERACCESS_CM_PRIVATE priv,
                     unsigned long arg
                     )
{
  tTS_IB_CM_ACCEPT_IOCTL_STRUCT accept_ioctl;
  void *reply_data;
  tTS_IB_CM_USER_CONNECTION cm_conn;
  struct ib_qp_attribute qp_attr;
  int rc, ret;
  VAPI_ret_t status;

  if (copy_from_user(&accept_ioctl,
                     (tTS_IB_CM_ACCEPT_IOCTL) arg,
                     sizeof accept_ioctl)) {
    return -EFAULT;
  }

  if (accept_ioctl.reply_size < 0) {
    return -EINVAL;
  }

  /* FIXME: Arbitrary limit */
  if (accept_ioctl.reply_size > PAGE_SIZE) {
    return -E2BIG;
  }

  if (accept_ioctl.reply_size != 0) {
    /* Allocate memory and copy it */
    reply_data = kmalloc(accept_ioctl.reply_size, GFP_KERNEL);
    if (reply_data == NULL) {
      return -ENOMEM;
    }

    if (copy_from_user(reply_data,
                       accept_ioctl.reply_data,
                       accept_ioctl.reply_size)) {
      rc = -EFAULT;
      goto out_free;
    }
  } else {
    reply_data = NULL;
  }

  cm_conn = tsIbCmUserConnectionFind(priv, accept_ioctl.comm_id);
  if (!cm_conn) {
    rc = -ENOENT;
    goto out_free;
  }

  tsIbTavorDeviceQueryVapiHandle(priv->device->ib_device,
                                 &cm_conn->v_device);
  cm_conn->cm_arg = accept_ioctl.cm_arg;

#ifndef W2K_OS // TEMPORARY ---
  status = EVAPI_k_set_destroy_qp_cbk(cm_conn->v_device,
                                      accept_ioctl.k_qp_handle,
                                      tsIbUserQpDestroyCallback,
                                      cm_conn);
  switch (status) {
  case VAPI_OK:
    break;
  default:
    TS_REPORT_WARN(MOD_KERNEL_IB,
                   "EVAPI_k_set_destroy_qp_cbk, return code = %d (%s)",
                   status, VAPI_strerror(status));
    rc = -EINVAL;

    goto out_free_conn;
  }
#endif
  cm_conn->qp = accept_ioctl.qp_handle;
  cm_conn->vk_qp = accept_ioctl.k_qp_handle;

  {
    tTS_IB_CM_PASSIVE_PARAM_STRUCT passive_params = {
      .qp                     = accept_ioctl.qp_handle,
      .reply_private_data     = reply_data,
      .reply_private_data_len = accept_ioctl.reply_size,
    };

    rc = tsIbCmAccept(cm_conn->comm_id, &passive_params);
  }

  /* use ret so we don't trash rc */
  qp_attr.valid_fields = TS_IB_QP_ATTRIBUTE_STATE;
  ret = tsIbQpQuery(cm_conn->qp, &qp_attr);
  if (ret == 0 && qp_attr.state >= 0) {
    accept_ioctl.qp_state = qp_attr.state;
  } else {
    accept_ioctl.qp_state = TS_IB_QP_STATE_INVALID;
  }

  if (copy_to_user((tTS_IB_CM_ACCEPT_IOCTL) arg,
                   &accept_ioctl,
                   sizeof accept_ioctl)) {
    rc = -EFAULT;
  }

  if (rc) {
    tsIbCmUserConnectionDestroy(cm_conn);	/* drop the structure */
  }

out_free_conn:
  tsIbCmUserConnectionPut(cm_conn);		/* for the Find() */

out_free:
  kfree(reply_data);

  return rc;
}

int tsIbCmUserReject(
                     tTS_IB_USERACCESS_CM_PRIVATE priv,
                     unsigned long arg
                     )
{
  struct tTS_IB_CM_REJECT_IOCTL reject_ioctl;
  tTS_IB_CM_USER_CONNECTION cm_conn;
  void *reply_data;
  int rc;

  if (copy_from_user(&reject_ioctl,
                     (tTS_IB_CM_REJECT_IOCTL) arg,
                     sizeof reject_ioctl)) {
    return -EFAULT;
  }

  if (reject_ioctl.reply_size < 0) {
    return -EINVAL;
  }

  /* FIXME: Arbitrary limit */
  if (reject_ioctl.reply_size > PAGE_SIZE) {
    return -E2BIG;
  }

  if (reject_ioctl.reply_size != 0) {
    /* Allocate memory and copy it */
    reply_data = kmalloc(reject_ioctl.reply_size, GFP_KERNEL);
    if (reply_data == NULL) {
      return -ENOMEM;
    }

    if (copy_from_user(reply_data,
                       reject_ioctl.reply_data,
                       reject_ioctl.reply_size)) {
      rc = -EFAULT;
      goto out_free;
    }
  } else {
    reply_data = NULL;
  }

  cm_conn = tsIbCmUserConnectionFind(priv, reject_ioctl.comm_id);
  if (!cm_conn) {
    rc = -ENOENT;
    goto out_free;
  }

  rc = tsIbCmReject(cm_conn->comm_id,
                    reply_data,
                    reject_ioctl.reply_size);

  tsIbCmCallbackModify(cm_conn->comm_id, NULL, NULL);
  cm_conn->comm_id = TS_IB_CM_COMM_ID_INVALID;

  tsIbCmUserConnectionDestroy(cm_conn);
  tsIbCmUserConnectionPut(cm_conn);		/* for the Find() */

out_free:
  kfree(reply_data);

  return rc;
}

int tsIbCmUserConfirm(
                      tTS_IB_USERACCESS_CM_PRIVATE priv,
                      unsigned long arg
                      )
{
  struct tTS_IB_CM_CONFIRM_IOCTL confirm_ioctl;
  struct ib_qp_attribute qp_attr;
  tTS_IB_CM_USER_CONNECTION cm_conn;
  void *reply_data;
  int rc, ret;

  if (copy_from_user(&confirm_ioctl,
                     (tTS_IB_CM_CONFIRM_IOCTL) arg,
                     sizeof confirm_ioctl)) {
    return -EFAULT;
  }

  if (confirm_ioctl.reply_size < 0) {
    return -EINVAL;
  }

  /* FIXME: Arbitrary limit */
  if (confirm_ioctl.reply_size > PAGE_SIZE) {
    return -E2BIG;
  }

  if (confirm_ioctl.reply_size != 0) {
    /* Allocate memory and copy it */
    reply_data = kmalloc(confirm_ioctl.reply_size, GFP_KERNEL);
    if (reply_data == NULL) {
      return -ENOMEM;
    }

    if (copy_from_user(reply_data,
                       confirm_ioctl.reply_data,
                       confirm_ioctl.reply_size)) {
      rc = -EFAULT;
      goto out_free;
    }
  } else {
    reply_data = NULL;
  }

  cm_conn = tsIbCmUserConnectionFind(priv, confirm_ioctl.comm_id);
  if (!cm_conn) {
    rc = -ENOENT;
    goto out_free;
  }

  rc = tsIbCmConfirm(cm_conn->comm_id,
                     reply_data,
                     confirm_ioctl.reply_size);

  /* use ret so we don't trash rc */
  qp_attr.valid_fields = TS_IB_QP_ATTRIBUTE_STATE;
  ret = tsIbQpQuery(cm_conn->qp, &qp_attr);
  if (ret == 0 && qp_attr.state >= 0) {
    confirm_ioctl.qp_state = qp_attr.state;
  } else {
    confirm_ioctl.qp_state = TS_IB_QP_STATE_INVALID;
  }

  tsIbCmUserConnectionPut(cm_conn);		/* for the Find() */

  if (copy_to_user((tTS_IB_CM_CONFIRM_IOCTL) arg,
                   &confirm_ioctl,
                   sizeof confirm_ioctl)) {
    rc = -EFAULT;
  }

out_free:
  kfree(reply_data);

  return rc;
}

int tsIbCmUserEstablish(
                        tTS_IB_USERACCESS_CM_PRIVATE priv,
                        unsigned long arg
                        )
{
  struct tTS_IB_CM_ESTABLISH_IOCTL establish_ioctl;
  tTS_IB_CM_USER_CONNECTION cm_conn;
  struct ib_qp_attribute qp_attr;
  int rc, ret;

  if (copy_from_user(&establish_ioctl,
                     (tTS_IB_CM_ESTABLISH_IOCTL) arg,
                     sizeof establish_ioctl)) {
    return -EFAULT;
  }

  cm_conn = tsIbCmUserConnectionFind(priv, establish_ioctl.comm_id);
  if (!cm_conn) {
    return -ENOENT;
  }

  /* Always immediate for now atleast */
  rc = tsIbCmEstablish(cm_conn->comm_id, 1);

  /* use ret so we don't trash rc */
  qp_attr.valid_fields = TS_IB_QP_ATTRIBUTE_STATE;
  ret = tsIbQpQuery(cm_conn->qp, &qp_attr);
  if (ret == 0 && qp_attr.state >= 0) {
    establish_ioctl.qp_state = qp_attr.state;
  } else {
    establish_ioctl.qp_state = TS_IB_QP_STATE_INVALID;
  }

  tsIbCmUserConnectionPut(cm_conn);		/* for the Find() */

  if (copy_to_user((tTS_IB_CM_ESTABLISH_IOCTL) arg,
                   &establish_ioctl,
                   sizeof establish_ioctl)) {
    rc = -EFAULT;
  }

  return rc;
}

int tsIbCmUserDelayResponse(
                            tTS_IB_USERACCESS_CM_PRIVATE priv,
                            unsigned long arg
                            )
{
  struct tTS_IB_CM_DELAYRESPONSE_IOCTL delay_ioctl;
  tTS_IB_CM_USER_CONNECTION cm_conn;
  void *mra_data;
  int rc;

  if (copy_from_user(&delay_ioctl,
                     (tTS_IB_CM_DELAYRESPONSE_IOCTL) arg,
                     sizeof delay_ioctl)) {
    return -EFAULT;
  }

  if (delay_ioctl.mra_data_size < 0) {
    return -EINVAL;
  }

  /* FIXME: Arbitrary limit */
  if (delay_ioctl.mra_data_size > PAGE_SIZE) {
    return -E2BIG;
  }

  if (delay_ioctl.mra_data_size != 0) {
    /* Allocate memory and copy it */
    mra_data = kmalloc(delay_ioctl.mra_data_size, GFP_KERNEL);
    if (mra_data == NULL) {
      return -ENOMEM;
    }

    if (copy_from_user(mra_data,
                       delay_ioctl.mra_data,
                       delay_ioctl.mra_data_size)) {
      rc = -EFAULT;
      goto out_free;
    }
  } else {
    mra_data = NULL;
  }

  cm_conn = tsIbCmUserConnectionFind(priv, delay_ioctl.comm_id);
  if (!cm_conn) {
    rc = -ENOENT;
    goto out_free;
  }

  rc = tsIbCmDelayResponse(cm_conn->comm_id,
                           delay_ioctl.service_timeout,
                           mra_data,
                           delay_ioctl.mra_data_size);

  tsIbCmUserConnectionPut(cm_conn);		/* for the Find() */

out_free:
  kfree(mra_data);

  return rc;
}

int tsIbCmUserDropConsumer(
                           tTS_IB_USERACCESS_CM_PRIVATE priv,
                           unsigned long arg
                           )
{
  tTS_IB_CM_COMM_ID comm_id = (tTS_IB_CM_COMM_ID)arg;
  tTS_IB_CM_USER_CONNECTION cm_conn;

  cm_conn = tsIbCmUserConnectionFind(priv, comm_id);
  if (!cm_conn) {
    return -ENOENT;
  }

  tsIbCmUserConnectionDestroy(cm_conn);
  tsIbCmUserConnectionPut(cm_conn);		/* for the Find() */

  return 0;
}

int tsIbCmUserGetCompletion(
                            tTS_IB_USERACCESS_CM_PRIVATE priv,
                            unsigned long arg
                            )
{
  struct tTS_IB_U_CM_GET_COMPLETION_IOCTL getcomp_ioctl;
  tTS_IB_CM_USER_COMPLETION cm_comp;
  unsigned long flags;

  if (copy_from_user(&getcomp_ioctl,
                     (tTS_IB_U_CM_GET_COMPLETION_IOCTL) arg,
                     sizeof getcomp_ioctl)) {
    return -EFAULT;
  }

  spin_lock_irqsave(&priv->cm_comp_lock, flags);
  while (list_empty(&priv->cm_comp_list)) {
    spin_unlock_irqrestore(&priv->cm_comp_lock, flags);

#ifndef W2K_OS
    if (wait_event_interruptible(priv->cm_comp_wait,
                                 !list_empty(&priv->cm_comp_list))) {
#else
    if (wait_event_interruptible(&priv->cm_comp_wait,
                                 !list_empty(&priv->cm_comp_list))) {
#endif
      return -ERESTARTSYS;
    }

    spin_lock_irqsave(&priv->cm_comp_lock, flags);
  }

  cm_comp = list_entry(priv->cm_comp_list.next, struct tTS_IB_CM_USER_COMPLETION, list);
  list_del_init(&cm_comp->list);

  spin_unlock_irqrestore(&priv->cm_comp_lock, flags);

  getcomp_ioctl.qp_handle = cm_comp->qp;
  getcomp_ioctl.comm_id = cm_comp->comm_id;
  getcomp_ioctl.listen_handle = cm_comp->listen_handle;
  getcomp_ioctl.event = cm_comp->event;
  getcomp_ioctl.qp_state = cm_comp->qp_state;
  getcomp_ioctl.cm_arg = cm_comp->cm_arg;

  if (getcomp_ioctl.params_size < cm_comp->params_size) {
    /* FIXME: Should we throw away the buffer here? */
    kfree(cm_comp->params);
    kfree(cm_comp);
    return -ENOSPC;
  }

  if (cm_comp->params_size > 0) {
    if (copy_to_user(getcomp_ioctl.params,
                     cm_comp->params,
                     cm_comp->params_size)) {
      kfree(cm_comp->params);
      kfree(cm_comp);
      return -EFAULT;
    }
  }

  getcomp_ioctl.params_size = cm_comp->params_size;

  kfree(cm_comp->params);
  kfree(cm_comp);

  return copy_to_user((tTS_IB_U_CM_GET_COMPLETION_IOCTL) arg,
                      &getcomp_ioctl,
                      sizeof getcomp_ioctl) ? -EFAULT : 0;
}

int tsIbCmUserClose(
                    tTS_IB_USERACCESS_CM_PRIVATE priv
                    )
{
  struct list_head *ptr, *tmp;
  unsigned long flags;

  spin_lock_irqsave(&priv->cm_comp_lock, flags);
  list_for_each_safe(ptr, tmp, &priv->cm_comp_list) {
    tTS_IB_CM_USER_COMPLETION cm_comp;

    cm_comp = list_entry(ptr, struct tTS_IB_CM_USER_COMPLETION, list);
    list_del_init(&cm_comp->list);
    kfree(cm_comp);
  }
  spin_unlock_irqrestore(&priv->cm_comp_lock, flags);

  /*
   * We need to drop priv->com_conn_sem because we can deadlock with the
   * completion handler (locks are obtained in opposite order from each
   * other, a "deadly embrace").
   */
  down(&priv->cm_conn_sem);
  while (!list_empty(&priv->cm_conn_list)) {
    tTS_IB_CM_USER_CONNECTION cm_conn;

    cm_conn = list_entry(priv->cm_conn_list.next, struct tTS_IB_CM_USER_CONNECTION, list);

    list_del_init(&cm_conn->list);

    /* Drop and reacquire lock since destroying the connection may callback */
    up(&priv->cm_conn_sem);

    _tsIbCmUserConnectionDestroy(cm_conn);

    down(&priv->cm_conn_sem);
  }
  up(&priv->cm_conn_sem);

  return 0;
}

/* =============================================================== */
/*..tsIbCmUserIoctl -                                              */
#ifndef W2K_OS // Vipul
int tsIbCmUserIoctl(
                    struct inode *inode,
                    struct file *filp,
                    unsigned int cmd,
                    unsigned long arg
                    )
#else
int tsIbCmUserIoctl(
                  PFILE_OBJECT pFileObject,
                  unsigned  int  cmd,
                  unsigned long  arg
                 )
#endif
{
#ifndef W2K_OS // Vipul
  tTS_IB_USERACCESS_CM_PRIVATE priv =
    (tTS_IB_USERACCESS_CM_PRIVATE) filp->private_data;
#else
  tTS_IB_USERACCESS_CM_PRIVATE priv =
    (tTS_IB_USERACCESS_CM_PRIVATE) pFileObject->FsContext;
#endif

  switch (cmd) {
  case TS_IB_IOCCMCONNECT:
    TS_TRACE(MOD_KERNEL_IB, T_VERBOSE, TRACE_KERNEL_IB_GEN,
             "initiate connection ioctl");
    return tsIbCmUserConnect(priv, arg);

  case TS_IB_IOCCMLISTEN:
    TS_TRACE(MOD_KERNEL_IB, T_VERBOSE, TRACE_KERNEL_IB_GEN,
             "connection listen ioctl");
    return tsIbCmUserListen(priv, arg);

  case TS_IB_IOCCMLISTENSTOP:
    TS_TRACE(MOD_KERNEL_IB, T_VERBOSE, TRACE_KERNEL_IB_GEN,
             "stop listening connection ioctl");
    return tsIbCmUserListenStop(priv, arg);

  case TS_IB_IOCCMDISCONNECT:
    TS_TRACE(MOD_KERNEL_IB, T_VERBOSE, TRACE_KERNEL_IB_GEN,
             "disconnect CM connection ioctl");
    /* We'll cleanup everything when see the connection go to IDLE */
    /* FIXME: We should validate the connection id from userspace */
    return tsIbCmDisconnect((tTS_IB_CM_COMM_ID)arg);

  case TS_IB_IOCCMALTPATHLOAD:
    TS_TRACE(MOD_KERNEL_IB, T_VERBOSE, TRACE_KERNEL_IB_GEN,
             "load alternate path ioctl");
    {
      struct tTS_IB_CM_ALTERNATE_PATH_LOAD_IOCTL altpathload_ioctl;
      struct ib_path_record path_record;

      if (copy_from_user(&altpathload_ioctl,
                         (tTS_IB_CM_ALTERNATE_PATH_LOAD_IOCTL) arg,
                         sizeof altpathload_ioctl)) {
        return -EFAULT;
      }

      if (copy_from_user(&path_record,
                         (tTS_IB_PATH_RECORD) altpathload_ioctl.alternate_path,
                         sizeof path_record)) {
        return -EFAULT;
      }

      /* FIXME: We should validate the connection id from userspace */
      return tsIbCmAlternatePathLoad(altpathload_ioctl.comm_id,
                                     &path_record);
    }

  case TS_IB_IOCCMACCEPT:
    TS_TRACE(MOD_KERNEL_IB, T_VERBOSE, TRACE_KERNEL_IB_GEN,
             "accept CM connection ioctl");
    return tsIbCmUserAccept(priv, arg);

  case TS_IB_IOCCMREJECT:
    TS_TRACE(MOD_KERNEL_IB, T_VERBOSE, TRACE_KERNEL_IB_GEN,
             "reject CM connection ioctl");
    return tsIbCmUserReject(priv, arg);

  case TS_IB_IOCCMCONFIRM:
    TS_TRACE(MOD_KERNEL_IB, T_VERBOSE, TRACE_KERNEL_IB_GEN,
             "confirm CM connection ioctl");
    return tsIbCmUserConfirm(priv, arg);

  case TS_IB_IOCCMESTABLISH:
    TS_TRACE(MOD_KERNEL_IB, T_VERBOSE, TRACE_KERNEL_IB_GEN,
             "notify CM connection is establed ioctl");
    return tsIbCmUserEstablish(priv, arg);

  case TS_IB_IOCCMDELAYRESPONSE:
    TS_TRACE(MOD_KERNEL_IB, T_VERBOSE, TRACE_KERNEL_IB_GEN,
             "delay response ioctl");
    return tsIbCmUserDelayResponse(priv, arg);

  case TS_IB_IOCCMDROPCONSUMER:
    TS_TRACE(MOD_KERNEL_IB, T_VERBOSE, TRACE_KERNEL_IB_GEN,
             "drop CM consumer ioctl");
    return tsIbCmUserDropConsumer(priv, arg);

  case TS_IB_IOCCMGETCOMPLETION:
    TS_TRACE(MOD_KERNEL_IB, T_VERBOSE, TRACE_KERNEL_IB_GEN,
             "get completion ioctl");
    return tsIbCmUserGetCompletion(priv, arg);

  case TS_IB_IOCQPREGISTER:
    TS_TRACE(MOD_KERNEL_IB, T_VERBOSE, TRACE_KERNEL_IB_GEN,
             "register QP with kernel ioctl");
    {
      tTS_IB_QP_REGISTER_IOCTL_STRUCT qpregister_ioctl;
      tTS_IB_TAVOR_QP_CREATE_PARAM_STRUCT tavor_qp_create;
      struct ib_qp_create_param qp_create = {
        .pd              = priv->device->pd,
        .transport       = qpregister_ioctl.transport,
        .device_specific = &tavor_qp_create,
      };
      tTS_IB_QP_HANDLE qp_handle;
      tTS_IB_QPN qpn;
      int rc;

      if (copy_from_user(&qpregister_ioctl,
                         (tTS_IB_QP_REGISTER_IOCTL) arg,
                         sizeof qpregister_ioctl)) {
        return -EFAULT;
      }

      tavor_qp_create.vapi_k_handle = (void *)(unsigned long)qpregister_ioctl.vk_qp_handle;
      tavor_qp_create.qpn           = qpregister_ioctl.qpn;
      tavor_qp_create.qp_state      = qpregister_ioctl.qp_state;

      rc = tsIbQpCreate(&qp_create, &qp_handle, &qpn);
      if (rc) {
        return rc;
      }

      qpregister_ioctl.qp_handle = qp_handle;

      return copy_to_user((tTS_IB_QP_REGISTER_IOCTL) arg,
                          &qpregister_ioctl,
                          sizeof qpregister_ioctl) ? -EFAULT : 0;
    }

  case TS_IB_IOCGPATHRECORD:
    TS_TRACE(MOD_KERNEL_IB, T_VERBOSE, TRACE_KERNEL_IB_GEN,
             "perform path record lookup ioctl");
#ifndef W2K_OS
    return tsIbUserPathRecord(priv, arg);
#else
    return -EFAULT;
#endif

  case TS_IB_IOCCMSERVIDASSIGN:
    TS_TRACE(MOD_KERNEL_IB, T_VERBOSE, TRACE_KERNEL_IB_GEN,
             "assign a service id ioctl");
    {
      tTS_IB_SERVICE_ID servid = tsIbCmServiceAssign();

      return copy_to_user((tTS_IB_SERVICE_ID *) arg,
                          &servid,
                          sizeof servid) ? -EFAULT : 0;
    }

  default:
    TS_REPORT_WARN(MOD_KERNEL_IB,
                   "Unimplemented ioctl %d",
                   cmd);
    return -ENOIOCTLCMD;
  }

  return 0;
}

/* =============================================================== */
/*.._tsIbCmUserOpen -                                              */
#ifndef W2K_OS
static int _tsIbCmUserOpen(
                           struct inode *inode,
                           struct file *filp
                           )
#else
int _tsIbCmUserOpen( PFILE_OBJECT pFileObject )
#endif
{
#ifdef W2K_OS
  PDEVICE_EXTENSION pDevExt = pFileObject->DeviceObject->DeviceExtension;
#endif

  tTS_IB_USERACCESS_CM_DEVICE dev;
  tTS_IB_USERACCESS_CM_PRIVATE priv;
  int dev_num;

#ifdef W2K_OS
  dev_num = pDevExt->nDeviceNum;

  if (pFileObject->FsContext)
    dev = (tTS_IB_USERACCESS_CM_DEVICE) pFileObject->FsContext;
  else {
#else
  if (filp->private_data) {
    dev = (tTS_IB_USERACCESS_CM_DEVICE) filp->private_data;
  } else {
    dev_num = MINOR(inode->i_rdev);
#endif

    TS_TRACE(MOD_KERNEL_IB, T_VERBOSE, TRACE_KERNEL_IB_GEN,
             "device number %d", dev_num);

    /* not using devfs, use minor number */
    dev = &useraccess_cm_dev_list[dev_num];
  }

  if (dev->ib_device == TS_IB_HANDLE_INVALID) {
    return -ENODEV;
  }

  priv = kmalloc(sizeof(*priv), GFP_KERNEL);
  if (!priv) {
    return -ENOMEM;
  }

  priv->device               = dev;

  INIT_LIST_HEAD(&priv->cm_comp_list);
  init_waitqueue_head(&priv->cm_comp_wait);
  spin_lock_init(&priv->cm_comp_lock);

  INIT_LIST_HEAD(&priv->cm_conn_list);
  sema_init(&priv->cm_conn_sem, 1);

#ifdef W2K_OS // Vipul
  sema_init(&priv->irp_sem, 1);
  priv->pIrp = NULL;
  pFileObject->FsContext = priv;
#else
  filp->private_data = priv;
#endif

  return 0;
}

/* =============================================================== */
/*.._tsIbCmUserClose -                                             */
#ifndef W2K_OS
static int _tsIbCmUserClose(
                            struct inode *inode,
                            struct file *filp
                            )
{
  tTS_IB_USERACCESS_CM_PRIVATE priv =
   (tTS_IB_USERACCESS_CM_PRIVATE) filp->private_data;

  tsIbCmUserClose(priv);
  kfree(priv);

  return 0;
}
#else
int _tsIbCmUserClose( PFILE_OBJECT pFileObject )
{
  tsIbCmUserClose((tTS_IB_USERACCESS_CM_PRIVATE) pFileObject->FsContext);
  kfree(pFileObject->FsContext);

  return 0;
}
#endif

#ifndef W2K_OS
static struct file_operations useraccess_cm_fops = {
  .owner   = THIS_MODULE,
  .ioctl   = tsIbCmUserIoctl,
  .open    = _tsIbCmUserOpen,
  .release = _tsIbCmUserClose,
};

#if defined(TS_KERNEL_2_6)

/* Create devices for kernel 2.4 */
static int __init _tsIbCmUserCreateDevices(
                                           void
                                           ) {
  int ret;

  ret = alloc_chrdev_region(&useraccess_cm_devnum,
                            0,
                            TS_USERACCESS_CM_NUM_DEVICE,
                            (char *) TS_USERACCESS_CM_NAME);
  if (ret) {
    TS_REPORT_FATAL(MOD_KERNEL_IB,
                    "Couldn't allocate device numbers for useraccess_cm module");
    return ret;
  }

  cdev_init(&useraccess_cm_cdev, &useraccess_cm_fops);
  useraccess_cm_cdev.owner = THIS_MODULE;
  kobject_set_name(&useraccess_cm_cdev.kobj, TS_USERACCESS_CM_NAME);
  ret = cdev_add(&useraccess_cm_cdev,
                 useraccess_cm_devnum,
                 TS_USERACCESS_CM_NUM_DEVICE);
  if (ret) {
    kobject_put(&useraccess_cm_cdev.kobj);
    unregister_chrdev_region(useraccess_cm_devnum, TS_USERACCESS_CM_NUM_DEVICE);
  }

  return ret;
}

#else /* TS_KERNEL_2_6 */

/* Create devices for kernel 2.4 */
static int __init _tsIbCmUserCreateDevices(
                                         void
                                         ) {
  useraccess_cm_major = devfs_register_chrdev(0,
                                              TS_USERACCESS_CM_NAME,
                                              &useraccess_cm_fops);
  if (useraccess_cm_major < 0) {
    TS_REPORT_FATAL(MOD_KERNEL_IB,
                    "Failed to register device");
    return useraccess_cm_major;
  }

  TS_TRACE(MOD_KERNEL_IB, T_VERBOSE, TRACE_KERNEL_IB_GEN,
           "TS IB user access major %d",
           useraccess_cm_major);

  useraccess_cm_devfs_dir = devfs_mk_dir(NULL, "ts_ua", NULL);

  {
    int i;
    char name[4];

    for (i = 0; i < TS_USERACCESS_CM_NUM_DEVICE; ++i) {
      snprintf(name, sizeof name, "%02d", i);

      useraccess_cm_dev_list[i].devfs_handle =
        devfs_register(useraccess_cm_devfs_dir,
                       name,
                       DEVFS_FL_DEFAULT,
                       useraccess_cm_major,
                       i,
                       S_IFCHR | S_IRUSR | S_IWUSR,
                       &useraccess_cm_fops,
                       &useraccess_cm_dev_list[i]);

      TS_TRACE(MOD_KERNEL_IB, T_VERBOSE, TRACE_KERNEL_IB_GEN,
               "TS IB add using major %d, minor %d",
               useraccess_cm_major, i);
    }
  }

  return 0;
}

#endif /* TS_KERNEL_2_6 */

static int __init _tsIbCmUserInitModule(
                                        void
                                        )
{
  int i;
  int ret;

  for (i = 0; i < TS_USERACCESS_CM_NUM_DEVICE; ++i) {
    useraccess_cm_dev_list[i].ib_device = tsIbDeviceGetByIndex(i);
    if (useraccess_cm_dev_list[i].ib_device != TS_IB_HANDLE_INVALID) {
      ret = tsIbPdCreate(useraccess_cm_dev_list[i].ib_device, NULL,
                         &useraccess_cm_dev_list[i].pd);
      if (ret) {
        TS_REPORT_WARN(MOD_KERNEL_IB,
                       "couldn't create PD (ret = %d)", ret);
        useraccess_cm_dev_list[i].ib_device = TS_IB_HANDLE_INVALID;
        continue;
      }
    }
  }

  ret = _tsIbCmUserCreateDevices();
  if (ret) {
    for (i = 0; i < TS_USERACCESS_CM_NUM_DEVICE; ++i) {
      if (useraccess_cm_dev_list[i].pd != TS_IB_HANDLE_INVALID) {
        tsIbPdDestroy(useraccess_cm_dev_list[i].pd);
      }
    }
  }

  return ret;
}

#if defined(TS_KERNEL_2_6)

/* Delete devices for kernel 2.6 */
static void __exit _tsIbCmUserDeleteDevices(
                                            void
                                            ) {
  unregister_chrdev_region(useraccess_cm_devnum, TS_USERACCESS_CM_NUM_DEVICE);
  cdev_del(&useraccess_cm_cdev);
}

#else /* TS_KERNEL_2_6 */

/* Delete devices for kernel 2.4 */
static void __exit _tsIbCmUserDeleteDevices(
                                            void
                                            ) {
  devfs_unregister_chrdev(useraccess_cm_major, TS_USERACCESS_CM_NAME);

  {
    int i;

    for (i = 0; i < TS_USERACCESS_CM_NUM_DEVICE; ++i) {
      if (useraccess_cm_dev_list[i].devfs_handle) {
        devfs_unregister(useraccess_cm_dev_list[i].devfs_handle);
      }
    }
  }

  devfs_unregister(useraccess_cm_devfs_dir);
}

#endif /* TS_KERNEL_2_6 */

static void __exit _tsIbCmUserCleanupModule(
                                            void
                                            )
{
  int i;

  TS_REPORT_CLEANUP(MOD_KERNEL_IB,
                    "Unloading IB userspace access");

  _tsIbCmUserDeleteDevices();


  for (i = 0; i < TS_USERACCESS_CM_NUM_DEVICE; ++i) {
    if (useraccess_cm_dev_list[i].pd != TS_IB_HANDLE_INVALID) {
      tsIbPdDestroy(useraccess_cm_dev_list[i].pd);
    }
  }

  TS_REPORT_CLEANUP(MOD_KERNEL_IB,
                    "IB userspace access unloaded");
}
#else
/* Below are the entrypoints that are specific to Windows 2000 */

NTSTATUS
_tsIbCmUserInitModule( IN PDRIVER_OBJECT pDriverObject )
{
    int                     i;
    int                     a;
    tTS_IB_DEVICE_HANDLE    h;
    char                    name[4];

    UNICODE_STRING		    sDeviceName;
	UNICODE_STRING		    sWin32Name;
    ANSI_STRING             sAnsiString;
    NTSTATUS			    status;
    PDEVICE_OBJECT		    pDevObj;
    PDEVICE_EXTENSION       pDevExt;
    UCHAR                   szTempString[64];

    status = STATUS_UNSUCCESSFUL;

    /* Clear the device object pointer array */
    RtlZeroMemory( pdoArray, sizeof(PDEVICE_OBJECT)*TS_USERACCESS_CM_NUM_DEVICE );

    /* for each device, create a new device object */
    for( i = 0; i < TS_USERACCESS_CM_NUM_DEVICE; ++i )
    {
        useraccess_cm_dev_list[i].ib_device = TS_IB_HANDLE_INVALID;

        /* Get a handle to the device */
        if( !tsIbDeviceGet( &h, i, 1, &a ) && a )
        {
            useraccess_cm_dev_list[i].ib_device = h;

            sprintf( szTempString, "%s%d", DRIVER_NT_CM_DEV_NAME, i );
            RtlInitAnsiString( &sAnsiString, szTempString );
            RtlAnsiStringToUnicodeString( &sDeviceName, &sAnsiString, TRUE );

            sprintf( szTempString, "%s%d", DRIVER_DOS_CM_DEV_NAME, i );
            RtlInitAnsiString( &sAnsiString, szTempString );
            RtlAnsiStringToUnicodeString( &sWin32Name, &sAnsiString, TRUE );

            /* Create the device object */
	        status = IoCreateDevice( pDriverObject,
				        sizeof(DEVICE_EXTENSION),
				        &sDeviceName,
				        FILE_DEVICE_UNKNOWN,
				        0,
				        FALSE,
				        &pDevObj );

            /* check the status */
	        if(	!NT_SUCCESS(status) )
	        {
		        return( status );
	        }

            /* Create the symbolic link for the driver */
	        status = IoCreateSymbolicLink( &sWin32Name, &sDeviceName );

            /* Initialize the Device Extension */
            pDevExt                         = (PDEVICE_EXTENSION)pDevObj->DeviceExtension;
            pDevExt->pDevice                = pDevObj;	/* back pointer */
            pDevExt->nDeviceNum             = i;

            /* Use buffered I/O */
	        pDevObj->Flags |= DO_BUFFERED_IO;

            /* check the status */
	        if( !NT_SUCCESS(status) )
	        {
		        IoDeleteDevice( pDevObj );
		        return( status );
	        }

            /* Save off the device object pointer */
            pdoArray[i] = pDevObj;
        }
    }

    return( STATUS_SUCCESS );
}

void
_tsIbCmUserCleanupModule( PDRIVER_OBJECT pDriverObject )
{
    int             i;
    UNICODE_STRING  sWin32Name;
    ANSI_STRING     sAnsiString;
    UCHAR           szTempString[64];

    TS_REPORT_CLEANUP(MOD_KERNEL_IB,
                    "Unloading IB userspace access" );

    /* delete each device object */
    for( i=0; i < TS_USERACCESS_CM_NUM_DEVICE; ++i )
    {
        if( useraccess_cm_dev_list[i].devfs_handle )
        {
	        /* Create the DeviceName string */
            sprintf( szTempString, "%s%d", DRIVER_DOS_DEV_NAME, i );
            RtlInitAnsiString( &sAnsiString, szTempString );
            RtlAnsiStringToUnicodeString( &sWin32Name, &sAnsiString, TRUE );

            /* Delete the symbolic link and the device object */
	        IoDeleteSymbolicLink( &sWin32Name );

            if( pdoArray[i] )
            {
                IoDeleteDevice( pdoArray[i] );
            }
        }
    }
}
#endif /* W2K_OS */

module_init(_tsIbCmUserInitModule);
module_exit(_tsIbCmUserCleanupModule);
