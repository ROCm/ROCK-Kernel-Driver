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

  $Id: cm_main.c,v 1.11 2004/02/25 00:55:10 roland Exp $
*/

#include "cm_priv.h"
#include "ts_ib_core.h"
#include "ts_ib_mad.h"

#include "ts_kernel_trace.h"

#ifndef W2K_OS
#include <linux/config.h>
#include <linux/version.h>
#include <linux/module.h>

#include <linux/init.h>
#include <linux/errno.h>
#include <linux/random.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <asm/byteorder.h>

#else /* W2K_OS */
#include <os_dep/win/linux/module.h>
#include <os_dep/win/linux/string.h>
#endif /* W2K_OS */

MODULE_AUTHOR("Roland Dreier");
MODULE_DESCRIPTION("IB Communication Manager");
MODULE_LICENSE("Dual BSD/GPL");

/* XXX this #if must be removed */
/* XXX where is seed_lock initialized for Windows?? */
#ifdef W2K_OS
static spinlock_t  seed_lock;
#else
static spinlock_t  seed_lock = SPIN_LOCK_UNLOCKED;
#endif

static uint32_t    psn_seed;
static uint64_t    tid_seed;

static int         ticks_to_jiffies[32];

static tTS_IB_MAD_FILTER_HANDLE mad_handle;

/* =============================================================== */
/*.._tsIbCmTicksToJiffiesInit - fill out lookup table              */
static void _tsIbCmTicksToJiffiesInit(
                                      void
                                      ) {

  int i;
  /* 1 IB "tick" is 4.096 microseconds = 2^12 nanoseconds. */
  int ns = 1 << 12;
  int ns_per_jiffy = 1000000000 / HZ;

  for (i = 0; i < 32; ++i) {
    ticks_to_jiffies[i] = ns / ns_per_jiffy;

    /*
      We should double ns for the next loop iteration.  However, when
      ns reaches 2^31, we start dividing ns_per_jiffy by 2 instead.
      At that point ns and ns_per_jiffy don't exactly equal what their
      names imply but their ratio stays correct.
    */
    if (ns < (1 << 30)) {
      ns <<= 1;
    } else {
      ns_per_jiffy >>= 1;
    }
  }
}

/* =============================================================== */
/*..tsIbCmTimeoutToJiffies - convert log 2 timeout to jiffies      */
int tsIbCmTimeoutToJiffies(
                           int timeout
                           ) {
  return ticks_to_jiffies[timeout & 0x1f];
}

/* =============================================================== */
/*..tsIbCmPsnGenerate - generate a pseudo-random PSN               */
tTS_IB_PSN tsIbCmPsnGenerate(
                             void
                             ) {
  tTS_IB_PSN psn;
  TS_WINDOWS_SPINLOCK_FLAGS

  spin_lock(&seed_lock);

  /* 3-shift-register generator with period 2^32-1 */
  psn_seed ^= psn_seed << 13;
  psn_seed ^= psn_seed >> 17;
  psn_seed ^= psn_seed << 5;

  psn = psn_seed & 0xffffff;

  spin_unlock(&seed_lock);

  TS_TRACE(MOD_IB_CM, T_VERY_VERBOSE, TRACE_IB_CM_GEN,
           "Using initial PSN 0x%06x",
           psn);

  return psn;
}

/* =============================================================== */
/*..tsIbCmTidGenerate - generate a pseudo-random transaction id    */
uint64_t tsIbCmTidGenerate(
                           void
                           ) {
  uint64_t tid;
  TS_WINDOWS_SPINLOCK_FLAGS

  spin_lock(&seed_lock);

  /* 3-shift-register generator with period 2^64-1 */
  tid_seed ^= tid_seed << 17;
  tid_seed ^= tid_seed >> 37;
  tid_seed ^= tid_seed << 3;

  tid = tid_seed;

  spin_unlock(&seed_lock);

  TS_TRACE(MOD_IB_CM, T_VERY_VERBOSE, TRACE_IB_CM_GEN,
           "Using TID 0x%016" TS_U64_FMT "x",
           tid);

  return tid;
}

static void _tsIbCmMadHandler(
                              tTS_IB_MAD packet,
                              void *arg
                              ) {
  /* XXX this #if must be removed */
#ifndef W2K_OS
  static const struct {
    void (*function)(tTS_IB_MAD);
    char  *name;
  } dispatch_table[] = {
    [TS_IB_COM_MGT_REQ]  = { .function = tsIbCmReqHandler,  .name = "REQ"  },
    [TS_IB_COM_MGT_REJ]  = { .function = tsIbCmRejHandler,  .name = "REJ"  },
    [TS_IB_COM_MGT_REP]  = { .function = tsIbCmRepHandler,  .name = "REP"  },
    [TS_IB_COM_MGT_RTU]  = { .function = tsIbCmRtuHandler,  .name = "RTU"  },
    [TS_IB_COM_MGT_REJ]  = { .function = tsIbCmRejHandler,  .name = "REJ"  },
    [TS_IB_COM_MGT_DREQ] = { .function = tsIbCmDreqHandler, .name = "DREQ" },
    [TS_IB_COM_MGT_DREP] = { .function = tsIbCmDrepHandler, .name = "DREP" },
    [TS_IB_COM_MGT_LAP]  = { .function = tsIbCmLapHandler,  .name = "LAP"  },
    [TS_IB_COM_MGT_APR]  = { .function = tsIbCmAprHandler,  .name = "APR"  },
    [TS_IB_COM_MGT_MRA]  = { .function = tsIbCmMraHandler,  .name = "MRA"  },

    [TS_IB_COM_MGT_CLASS_PORT_INFO] = { .name = "PORT_INFO" },
    [TS_IB_COM_MGT_SIDR_REQ]        = { .name = "SIDR_REQ"  },
    [TS_IB_COM_MGT_SIDR_REP]        = { .name = "SIDR_REP"  }
  };
  static const int max_id = sizeof dispatch_table / sizeof dispatch_table[0];

#else  /* W2K_OS */

  static struct {
    void (*function)(tTS_IB_MAD);
    char  *name;
  } dispatch_table[0x001f];
  static const int max_id =sizeof(dispatch_table) / sizeof(dispatch_table[0]);

  dispatch_table[TS_IB_COM_MGT_REQ].function = tsIbCmReqHandler;
  dispatch_table[TS_IB_COM_MGT_REQ].name = "REQ";

  dispatch_table[TS_IB_COM_MGT_REJ].function = tsIbCmRejHandler;
  dispatch_table[TS_IB_COM_MGT_REJ].name = "REJ";

  dispatch_table[TS_IB_COM_MGT_REP].function = tsIbCmRepHandler;
  dispatch_table[TS_IB_COM_MGT_REP].name = "REP";

  dispatch_table[TS_IB_COM_MGT_RTU].function = tsIbCmRtuHandler;
  dispatch_table[TS_IB_COM_MGT_RTU].name = "RTU";

  dispatch_table[TS_IB_COM_MGT_REJ].function = tsIbCmRejHandler;
  dispatch_table[TS_IB_COM_MGT_REJ].name = "REJ";

  dispatch_table[TS_IB_COM_MGT_DREQ].function = tsIbCmDreqHandler;
  dispatch_table[TS_IB_COM_MGT_DREQ].name = "DREQ";

  dispatch_table[TS_IB_COM_MGT_DREP].function = tsIbCmDrepHandler;
  dispatch_table[TS_IB_COM_MGT_DREP].name = "DREP";

  dispatch_table[TS_IB_COM_MGT_LAP].function = tsIbCmLapHandler;
  dispatch_table[TS_IB_COM_MGT_LAP].name = "LAP";

  dispatch_table[TS_IB_COM_MGT_APR].function = tsIbCmAprHandler;
  dispatch_table[TS_IB_COM_MGT_APR].name = "APR";

  dispatch_table[TS_IB_COM_MGT_MRA].name = "MRA";
  dispatch_table[TS_IB_COM_MGT_CLASS_PORT_INFO].name = "PORT_INFO";
  dispatch_table[TS_IB_COM_MGT_SIDR_REQ].name = "SIDR_REQ";
  dispatch_table[TS_IB_COM_MGT_SIDR_REP].name = "SIDR_REP";

#endif /* W2K_OS */

  uint16_t attribute_id;

  tsIbCmCountReceive(packet);

  attribute_id = be16_to_cpu(packet->attribute_id);

  if (attribute_id >= max_id) {
    TS_REPORT_WARN(MOD_IB_CM,
                   "received CM MAD with unknown attribute id 0x%04x",
                   attribute_id);
    return;
  }

  if (!dispatch_table[attribute_id].function) {
    if (dispatch_table[attribute_id].name) {
      TS_REPORT_WARN(MOD_IB_CM,
                     "received unhandled CM MAD (%s)",
                     dispatch_table[attribute_id].name);
    } else {
      TS_REPORT_WARN(MOD_IB_CM,
                     "received CM MAD with unknown attribute id 0x%04x",
                     attribute_id);
    }
    return;
  }

  dispatch_table[attribute_id].function(packet);
}

  /* XXX this #if must be removed */
#ifndef W2K_OS
static int __init tsIbCmInitModule(void) {
#else
int tsIbCmInitModule(void) {
#endif
  int ret;

  TS_REPORT_INIT(MOD_IB_CM,
                 "Initializing IB Communication Manager");

  /* XXX this #if must be removed */
#ifndef W2K_OS
  if (tsIbCmProcInit()) {
    return -ENOMEM;
  }
#endif

  get_random_bytes(&psn_seed, sizeof psn_seed);
  get_random_bytes(&tid_seed, sizeof tid_seed);
  _tsIbCmTicksToJiffiesInit();

  tsIbCmConnectionTableInit();
  tsIbCmServiceTableInit();

  {
    tTS_IB_MAD_FILTER_STRUCT filter = { 0 };

    filter.qpn        = 1;
    filter.mgmt_class = TS_IB_MGMT_CLASS_COMM_MGT;
    filter.direction  = TS_IB_MAD_DIRECTION_IN;
    filter.mask       = (TS_IB_MAD_FILTER_QPN        |
                         TS_IB_MAD_FILTER_MGMT_CLASS |
                         TS_IB_MAD_FILTER_DIRECTION);
    strcpy(filter.name, "communication manager");

    if (tsIbMadHandlerRegister(&filter, _tsIbCmMadHandler, NULL, &mad_handle)) {
      ret = -EINVAL;
      goto out_table_cleanup;
    }
  }

  /* XXX set CM cap bit for each device */

  TS_REPORT_INIT(MOD_IB_CM,
                 "IB Communications Manager initialized");

  return 0;

 out_table_cleanup:
  tsIbCmServiceTableCleanup();
  tsIbCmConnectionTableCleanup();

  /* XXX this #if must be removed */
#ifndef W2K_OS
  tsIbCmProcCleanup();
#endif

  return ret;
}

  /* XXX this #if must be removed */
#ifndef W2K_OS
static void __exit tsIbCmCleanupModule(void) {
#else
void tsIbCmCleanupModule(void) {
#endif
  TS_REPORT_CLEANUP(MOD_IB_CM,
                    "Unloading IB Communication Manager");

  /* XXX remove CM cap bit for each device */

  tsIbMadHandlerDeregister(mad_handle);

  tsIbCmServiceTableCleanup();
  tsIbCmConnectionTableCleanup();

  /* XXX this #if must be removed */
#ifndef W2K_OS
  tsIbCmProcCleanup();
#endif

  TS_REPORT_CLEANUP(MOD_IB_CM,
           "IB Communication Manager unloaded");
}

module_init(tsIbCmInitModule);
module_exit(tsIbCmCleanupModule);
