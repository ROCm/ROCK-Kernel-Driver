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

  $Id: header_table.h 32 2004-04-09 03:57:42Z roland $
*/

#ifndef _HEADER_TABLE_H
#define _HEADER_TABLE_H

#include "ts_ib_header_types.h"

#define STRUCT_FIELD(header, field) \
	.struct_offset_bytes = offsetof(struct ib_ ## header, field),      \
	.struct_size_bytes   = sizeof ((struct ib_ ## header *) 0)->field, \
	.field_name          = #header ":" #field
#define RESERVED \
	.field_name          = "reserved"

static const struct ib_header_field lrh_table[]  = {
	{ STRUCT_FIELD(lrh, virtual_lane),
	  .header_offset_words = 0,
	  .header_offset_bits  = 0,
	  .header_size_bits    = 4 },
	{ STRUCT_FIELD(lrh, link_version),
	  .header_offset_words = 0,
	  .header_offset_bits  = 4,
	  .header_size_bits    = 4 },
	{ STRUCT_FIELD(lrh, service_level),
	  .header_offset_words = 0,
	  .header_offset_bits  = 8,
	  .header_size_bits    = 4 },
	{ RESERVED,
	  .header_offset_words = 0,
	  .header_offset_bits  = 12,
	  .header_size_bits    = 2 },
	{ STRUCT_FIELD(lrh, link_next_header),
	  .header_offset_words = 0,
	  .header_offset_bits  = 14,
	  .header_size_bits    = 2 },
	{ STRUCT_FIELD(lrh, destination_lid),
	  .header_offset_words = 0,
	  .header_offset_bits  = 16,
	  .header_size_bits    = 16 },
	{ RESERVED,
	  .header_offset_words = 1,
	  .header_offset_bits  = 0,
	  .header_size_bits    = 5 },
	{ STRUCT_FIELD(lrh, packet_length),
	  .header_offset_words = 1,
	  .header_offset_bits  = 5,
	  .header_size_bits    = 11 },
	{ STRUCT_FIELD(lrh, source_lid),
	  .header_offset_words = 1,
	  .header_offset_bits  = 16,
	  .header_size_bits    = 16 }
};

static const struct ib_header_field grh_table[]  = {
	{ STRUCT_FIELD(grh, ip_version),
	  .header_offset_words = 0,
	  .header_offset_bits  = 0,
	  .header_size_bits    = 4 },
	{ STRUCT_FIELD(grh, traffic_class),
	  .header_offset_words = 0,
	  .header_offset_bits  = 4,
	  .header_size_bits    = 8 },
	{ STRUCT_FIELD(grh, flow_label),
	  .header_offset_words = 0,
	  .header_offset_bits  = 12,
	  .header_size_bits    = 20 },
	{ STRUCT_FIELD(grh, payload_length),
	  .header_offset_words = 1,
	  .header_offset_bits  = 0,
	  .header_size_bits    = 16 },
	{ STRUCT_FIELD(grh, next_header),
	  .header_offset_words = 1,
	  .header_offset_bits  = 16,
	  .header_size_bits    = 8 },
	{ STRUCT_FIELD(grh, hop_limit),
	  .header_offset_words = 1,
	  .header_offset_bits  = 24,
	  .header_size_bits    = 8 },
	{ STRUCT_FIELD(grh, source_gid),
	  .header_offset_words = 2,
	  .header_offset_bits  = 0,
	  .header_size_bits    = 128 },
	{ STRUCT_FIELD(grh, destination_gid),
	  .header_offset_words = 6,
	  .header_offset_bits  = 0,
	  .header_size_bits    = 128 }
};

static const struct ib_header_field bth_table[]  = {
	{ STRUCT_FIELD(bth, opcode),
	  .header_offset_words = 0,
	  .header_offset_bits  = 0,
	  .header_size_bits    = 8 },
	{ STRUCT_FIELD(bth, solicited_event),
	  .header_offset_words = 0,
	  .header_offset_bits  = 8,
	  .header_size_bits    = 1 },
	{ STRUCT_FIELD(bth, mig_req),
	  .header_offset_words = 0,
	  .header_offset_bits  = 9,
	  .header_size_bits    = 1 },
	{ STRUCT_FIELD(bth, pad_count),
	  .header_offset_words = 0,
	  .header_offset_bits  = 10,
	  .header_size_bits    = 2 },
	{ STRUCT_FIELD(bth, transport_header_version),
	  .header_offset_words = 0,
	  .header_offset_bits  = 12,
	  .header_size_bits    = 4 },
	{ STRUCT_FIELD(bth, pkey),
	  .header_offset_words = 0,
	  .header_offset_bits  = 16,
	  .header_size_bits    = 16 },
	{ RESERVED,
	  .header_offset_words = 1,
	  .header_offset_bits  = 0,
	  .header_size_bits    = 8 },
	{ STRUCT_FIELD(bth, destination_qpn),
	  .header_offset_words = 1,
	  .header_offset_bits  = 8,
	  .header_size_bits    = 24 },
	{ STRUCT_FIELD(bth, ack_req),
	  .header_offset_words = 2,
	  .header_offset_bits  = 0,
	  .header_size_bits    = 1 },
	{ RESERVED,
	  .header_offset_words = 2,
	  .header_offset_bits  = 1,
	  .header_size_bits    = 7 },
	{ STRUCT_FIELD(bth, psn),
	  .header_offset_words = 2,
	  .header_offset_bits  = 8,
	  .header_size_bits    = 24 }
};

static const struct ib_header_field deth_table[] = {
	{ STRUCT_FIELD(deth, qkey),
	  .header_offset_words = 0,
	  .header_offset_bits  = 0,
	  .header_size_bits    = 32 },
	{ RESERVED,
	  .header_offset_words = 1,
	  .header_offset_bits  = 0,
	  .header_size_bits    = 8 },
	{ STRUCT_FIELD(deth, source_qpn),
	  .header_offset_words = 1,
	  .header_offset_bits  = 8,
	  .header_size_bits    = 24 }
};

#endif /* _HEADER_TABLE_H */

/*
  Local Variables:
  c-file-style: "linux"
  indent-tabs-mode: t
  End:
*/
