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

  $Id: ts_ib_cm.h 32 2004-04-09 03:57:42Z roland $
*/

#ifndef _TS_IB_CM_H
#define _TS_IB_CM_H

#if defined(MODVERSIONS) && !defined(__GENKSYMS__) && !defined(TS_KERNEL_2_6)
#  include "ts_kernel_version.h"
#  include TS_VER_FILE(../cm,cm_export.ver)
#endif

#include "ts_ib_cm_types.h"

tTS_IB_SERVICE_ID ib_cm_service_assign(void);

int ib_cm_connect(struct ib_cm_active_param  *param,
                  struct ib_path_record      *primary_path,
                  struct ib_path_record      *alternate_path,
                  tTS_IB_SERVICE_ID           service_id,
                  int                         peer_to_peer,
                  tTS_IB_CM_CALLBACK_FUNCTION function,
                  void                       *arg,
                  tTS_IB_CM_COMM_ID          *comm_id);

int ib_cm_disconnect(tTS_IB_CM_COMM_ID comm_id);
int ib_cm_kill(tTS_IB_CM_COMM_ID comm_id);

int ib_cm_listen(tTS_IB_SERVICE_ID           service_id,
                 tTS_IB_SERVICE_ID           service_mask,
                 tTS_IB_CM_CALLBACK_FUNCTION function,
                 void                       *arg,
                 tTS_IB_LISTEN_HANDLE       *listen_handle);

int ib_cm_listen_stop(tTS_IB_LISTEN_HANDLE listen_handle);

int ib_cm_alternate_path_load(tTS_IB_CM_COMM_ID      comm_id,
                            struct ib_path_record *alternate_path);

int ib_cm_delay_response(tTS_IB_CM_COMM_ID comm_id,
                        int               service_timeout,
                        void             *mra_private_data,
                        int               mra_private_data_len);

int ib_cm_callback_modify(tTS_IB_CM_COMM_ID           comm_id,
                         tTS_IB_CM_CALLBACK_FUNCTION function,
                         void                       *arg);

int ib_cm_establish(tTS_IB_CM_COMM_ID comm_id,
                    int               immediate);

int ib_cm_path_migrate(tTS_IB_CM_COMM_ID comm_id);

int ib_cm_accept(tTS_IB_CM_COMM_ID           comm_id,
                 struct ib_cm_passive_param *param);

int ib_cm_reject(tTS_IB_CM_COMM_ID comm_id,
                 void             *rej_private_data,
                 int               rej_private_data_len);

int ib_cm_confirm(tTS_IB_CM_COMM_ID comm_id,
                  void             *rtu_private_data,
                  int               rtu_private_data_len);

/* Defines to support legacy code -- don't use the tsIb names in new code. */
#define tsIbCmServiceAssign              ib_cm_service_assign
#define tsIbCmConnect                    ib_cm_connect
#define tsIbCmDisconnect                 ib_cm_disconnect
#define tsIbCmKill                       ib_cm_kill
#define tsIbCmListen                     ib_cm_listen
#define tsIbCmListenStop                 ib_cm_listen_stop
#define tsIbCmAlternatePathLoad          ib_cm_alternate_path_load
#define tsIbCmDelayResponse              ib_cm_delay_response
#define tsIbCmCallbackModify             ib_cm_callback_modify
#define tsIbCmEstablish                  ib_cm_establish
#define tsIbCmPathMigrate                ib_cm_path_migrate
#define tsIbCmAccept                     ib_cm_accept
#define tsIbCmReject                     ib_cm_reject
#define tsIbCmConfirm                    ib_cm_confirm

#endif /* _TS_IB_CM_H */

/*
  Local Variables:
  c-file-style: "linux"
  indent-tabs-mode: t
  End:
*/
