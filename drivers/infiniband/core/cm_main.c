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

  $Id: cm_main.c 32 2004-04-09 03:57:42Z roland $
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

static spinlock_t  seed_lock = SPIN_LOCK_UNLOCKED;
static uint32_t    psn_seed;
static uint64_t    tid_seed;

static int         ticks_to_jiffies[32];

static tTS_IB_MAD_FILTER_HANDLE mad_handle;

static void ib_ticks_to_jiffies(void)
{
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

int ib_cm_timeout_to_jiffies(int timeout)
{
	return ticks_to_jiffies[timeout & 0x1f];
}

tTS_IB_PSN ib_cm_psn_generate(void)
{
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

uint64_t ib_cm_tid_generate(void)
{
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

static void ib_cm_mad_handler(struct ib_mad *packet,
                              void *arg)
{
	static const struct {
		void (*function)(struct ib_mad *);
		char  *name;
	} dispatch_table[] = {
		[IB_COM_MGT_REQ]  = { .function = ib_cm_req_handler,  .name = "REQ"  },
		[IB_COM_MGT_REJ]  = { .function = ib_cm_rej_handler,  .name = "REJ"  },
		[IB_COM_MGT_REP]  = { .function = ib_cm_rep_handler,  .name = "REP"  },
		[IB_COM_MGT_RTU]  = { .function = ib_cm_rtu_handler,  .name = "RTU"  },
		[IB_COM_MGT_REJ]  = { .function = ib_cm_rej_handler,  .name = "REJ"  },
		[IB_COM_MGT_DREQ] = { .function = ib_cm_dreq_handler, .name = "DREQ" },
		[IB_COM_MGT_DREP] = { .function = ib_cm_drep_handler, .name = "DREP" },
		[IB_COM_MGT_LAP]  = { .function = ib_cm_lap_handler,  .name = "LAP"  },
		[IB_COM_MGT_APR]  = { .function = ib_cm_apr_handler,  .name = "APR"  },
		[IB_COM_MGT_MRA]  = { .function = ib_cm_mra_handler,  .name = "MRA"  },

		[IB_COM_MGT_CLASS_PORT_INFO] = { .name = "PORT_INFO" },
		[IB_COM_MGT_SIDR_REQ]        = { .name = "SIDR_REQ"  },
		[IB_COM_MGT_SIDR_REP]        = { .name = "SIDR_REP"  }
	};

	uint16_t attribute_id;

	ib_cm_count_receive(packet);

	attribute_id = be16_to_cpu(packet->attribute_id);

	if (attribute_id >= ARRAY_SIZE(dispatch_table)) {
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

static int __init ib_cm_init(void)
{
	int ret;

	TS_REPORT_INIT(MOD_IB_CM,
		       "Initializing IB Communication Manager");

	if (ib_cm_proc_init()) {
		return -ENOMEM;
	}

	get_random_bytes(&psn_seed, sizeof psn_seed);
	get_random_bytes(&tid_seed, sizeof tid_seed);
	ib_ticks_to_jiffies();

	ib_cm_connection_table_init();
	ib_cm_service_table_init();

	{
		struct ib_mad_filter filter = { 0 };

		filter.qpn        = 1;
		filter.mgmt_class = TS_IB_MGMT_CLASS_COMM_MGT;
		filter.direction  = TS_IB_MAD_DIRECTION_IN;
		filter.mask       = (TS_IB_MAD_FILTER_QPN        |
				     TS_IB_MAD_FILTER_MGMT_CLASS |
				     TS_IB_MAD_FILTER_DIRECTION);
		strcpy(filter.name, "communication manager");

		if (ib_mad_handler_register(&filter, ib_cm_mad_handler, NULL, &mad_handle)) {
			ret = -EINVAL;
			goto out_table_cleanup;
		}
	}

	/* XXX set CM cap bit for each device */

	TS_REPORT_INIT(MOD_IB_CM,
		       "IB Communications Manager initialized");

	return 0;

 out_table_cleanup:
	ib_cm_service_table_cleanup();
	ib_cm_connection_table_cleanup();
	ib_cm_proc_cleanup();

	return ret;
}

static void __exit ib_cm_cleanup(void) {
	TS_REPORT_CLEANUP(MOD_IB_CM,
			  "Unloading IB Communication Manager");

	/* XXX remove CM cap bit for each device */

	ib_mad_handler_deregister(mad_handle);

	ib_cm_service_table_cleanup();
	ib_cm_connection_table_cleanup();
	ib_cm_proc_cleanup();

	TS_REPORT_CLEANUP(MOD_IB_CM,
			  "IB Communication Manager unloaded");
}

module_init(ib_cm_init);
module_exit(ib_cm_cleanup);

/*
  Local Variables:
  c-file-style: "linux"
  indent-tabs-mode: t
  End:
*/
