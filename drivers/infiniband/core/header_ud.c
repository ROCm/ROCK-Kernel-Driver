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

  $Id: header_ud.c 32 2004-04-09 03:57:42Z roland $
*/

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

#include "ts_ib_header.h"
#include "header_table.h"

#include "ts_kernel_trace.h"
#include "ts_kernel_services.h"

#include <linux/errno.h>

void ib_ud_header_init(int     		    payload_bytes,
		       int    		    grh_present,
		       struct ib_ud_header *header)
{
	int header_len;

	header_len =
		TS_IB_LRH_BYTES  +
		TS_IB_BTH_BYTES  +
		TS_IB_DETH_BYTES;
	if (grh_present) {
		header_len += TS_IB_GRH_BYTES;
	}

	header->lrh.link_version             = 0;
	header->lrh.link_next_header         =
		grh_present ? IB_LNH_IBA_GLOBAL : IB_LNH_IBA_LOCAL;
	header->lrh.packet_length            = (TS_IB_LRH_BYTES  +
						TS_IB_BTH_BYTES  +
						TS_IB_DETH_BYTES +
						payload_bytes    +
						4                + /* ICRC     */
						3) / 4;            /* round up */

	header->grh_present                  = grh_present;
	if (grh_present) {
		header->lrh.packet_length         += TS_IB_GRH_BYTES / 4;

		header->grh.ip_version             = 6;
		header->grh.payload_length         = (TS_IB_BTH_BYTES  +
						      TS_IB_DETH_BYTES +
						      payload_bytes    +
						      4                + /* ICRC     */
						      3) & ~3;           /* round up */
		header->grh.next_header            = 0x1b;
	}

	if (header->immediate_present) {
		header->bth.opcode                   = IB_OPCODE_UD_SEND_ONLY_WITH_IMMEDIATE;
	} else {
		header->bth.opcode                   = IB_OPCODE_UD_SEND_ONLY;
	}
	header->bth.pad_count                = (4 - payload_bytes) & 3;
	header->bth.transport_header_version = 0;
}

int ib_ud_header_pack(struct ib_ud_header *header,
		      void                *buf)
{
	int len = 0;

	ib_header_pack(lrh_table,
		       sizeof lrh_table / sizeof lrh_table[0],
		       &header->lrh,
		       buf);
	len += TS_IB_LRH_BYTES;

	if (header->grh_present) {
		ib_header_pack(grh_table,
			       sizeof grh_table / sizeof grh_table[0],
			       &header->grh,
			       buf + len);
		len += TS_IB_GRH_BYTES;
	}

	ib_header_pack(bth_table,
		       sizeof bth_table / sizeof bth_table[0],
		       &header->bth,
		       buf + len);
	len += TS_IB_BTH_BYTES;

	ib_header_pack(deth_table,
		       sizeof deth_table / sizeof deth_table[0],
		       &header->deth,
		       buf + len);
	len += TS_IB_DETH_BYTES;

	if (header->immediate_present) {
		memcpy(buf + len, &header->immediate_data, sizeof header->immediate_data);
		len += sizeof header->immediate_data;
	}

	return len;
}

int ib_ud_header_unpack(void                *buf,
			struct ib_ud_header *header)
{
	ib_header_unpack(lrh_table,
			 sizeof lrh_table / sizeof lrh_table[0],
			 buf,
			 &header->lrh);
	buf += TS_IB_LRH_BYTES;

	if (header->lrh.link_version != 0) {
		TS_REPORT_WARN(MOD_KERNEL_IB,
			       "Invalid LRH.link_version %d", header->lrh.link_version);
		return -EINVAL;
	}

	switch (header->lrh.link_next_header) {
	case IB_LNH_IBA_LOCAL:
		header->grh_present = 0;
		break;

	case IB_LNH_IBA_GLOBAL:
		header->grh_present = 1;
		ib_header_unpack(grh_table,
				 sizeof grh_table / sizeof grh_table[0],
				 buf,
				 &header->grh);
		buf += TS_IB_GRH_BYTES;

		if (header->grh.ip_version != 6) {
			TS_REPORT_WARN(MOD_KERNEL_IB,
				       "Invalid GRH.ip_version %d", header->grh.ip_version);
			return -EINVAL;
		}
		if (header->grh.next_header != 0x1b) {
			TS_REPORT_WARN(MOD_KERNEL_IB,
				       "Invalid GRH.next_header 0x%02x", header->grh.next_header);
			return -EINVAL;
		}
		break;

	default:
		TS_REPORT_WARN(MOD_KERNEL_IB,
			       "Invalid LRH.link_next_header %d", header->lrh.link_next_header);
		return -EINVAL;
	}

	ib_header_unpack(bth_table,
			 sizeof bth_table / sizeof bth_table[0],
			 buf,
			 &header->bth);
	buf += TS_IB_BTH_BYTES;

	switch (header->bth.opcode) {
	case IB_OPCODE_UD_SEND_ONLY:
		header->immediate_present = 0;
		break;
	case IB_OPCODE_UD_SEND_ONLY_WITH_IMMEDIATE:
		header->immediate_present = 1;
		break;
	default:
		TS_REPORT_WARN(MOD_KERNEL_IB,
			       "Invalid BTH.opcode 0x%02x", header->bth.opcode);
		return -EINVAL;
	}

	if (header->bth.transport_header_version != 0) {
		TS_REPORT_WARN(MOD_KERNEL_IB,
			       "Invalid BTH.transport_header_version %d",
			       header->bth.transport_header_version);
		return -EINVAL;
	}

	ib_header_unpack(deth_table,
			 sizeof deth_table / sizeof deth_table[0],
			 buf,
			 &header->deth);
	buf += TS_IB_DETH_BYTES;

	if (header->immediate_present) {
		memcpy(&header->immediate_data, buf, sizeof header->immediate_data);
	}

	return 0;
}

/*
  Local Variables:
  c-file-style: "linux"
  indent-tabs-mode: t
  End:
*/
