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

  $Id: ts_ib_cm.h,v 1.7 2004/02/25 00:32:30 roland Exp $
*/

#ifndef _TS_IB_CM_H
#define _TS_IB_CM_H

#if defined(MODVERSIONS) && !defined(__GENKSYMS__) && !defined(TS_KERNEL_2_6)
#  include "ts_kernel_version.h"
#  include TS_VER_FILE(../cm,cm_export.ver)
#endif

#include "ts_ib_cm_types.h"

tTS_IB_SERVICE_ID tsIbCmServiceAssign(
                                      void
                                      );

int tsIbCmConnect(
                  tTS_IB_CM_ACTIVE_PARAM      param,
                  tTS_IB_PATH_RECORD          primary_path,
                  tTS_IB_PATH_RECORD          alternate_path,
                  tTS_IB_SERVICE_ID           service_id,
                  int                         peer_to_peer,
                  tTS_IB_CM_CALLBACK_FUNCTION function,
                  void                       *arg,
                  tTS_IB_CM_COMM_ID          *comm_id
                  );

int tsIbCmDisconnect(
                     tTS_IB_CM_COMM_ID comm_id
                     );

int tsIbCmKill(
               tTS_IB_CM_COMM_ID comm_id
               );

int tsIbCmListen(
                 tTS_IB_SERVICE_ID           service_id,
                 tTS_IB_SERVICE_ID           service_mask,
                 tTS_IB_CM_CALLBACK_FUNCTION function,
                 void                       *arg,
                 tTS_IB_LISTEN_HANDLE       *listen_handle
                 );

int tsIbCmListenStop(
                     tTS_IB_LISTEN_HANDLE listen_handle
                     );

int tsIbCmAlternatePathLoad(
                            tTS_IB_CM_COMM_ID  comm_id,
                            tTS_IB_PATH_RECORD alternate_path
                            );

int tsIbCmDelayResponse(
                        tTS_IB_CM_COMM_ID comm_id,
                        int               service_timeout,
                        void             *mra_private_data,
                        int               mra_private_data_len
                        );

int tsIbCmCallbackModify(
                         tTS_IB_CM_COMM_ID           comm_id,
                         tTS_IB_CM_CALLBACK_FUNCTION function,
                         void                       *arg
                         );

int tsIbCmEstablish(
                    tTS_IB_CM_COMM_ID comm_id,
                    int               immediate
                    );

int tsIbCmPathMigrate(
                      tTS_IB_CM_COMM_ID comm_id
                      );

int tsIbCmAccept(
                 tTS_IB_CM_COMM_ID       comm_id,
                 tTS_IB_CM_PASSIVE_PARAM param
                 );

int tsIbCmReject(
                 tTS_IB_CM_COMM_ID comm_id,
                 void             *rej_private_data,
                 int               rej_private_data_len
                 );

int tsIbCmConfirm(
                  tTS_IB_CM_COMM_ID comm_id,
                  void             *rtu_private_data,
                  int               rtu_private_data_len
                  );

#endif /* _TS_IB_CM_H */
