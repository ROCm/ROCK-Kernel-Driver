/*
 *
 * linux/drivers/s390/scsi/zfcp_stat.c
 *
 * FCP adapter driver for IBM eServer zSeries
 *
 * Statistics
 *
 * (C) Copyright IBM Corp. 2005
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#define ZFCP_STAT_REVISION "$Revision: 1.9.2.1 $"

#include <linux/statistic.h>
#include <linux/ctype.h>
#include "zfcp_ext.h"

#define ZFCP_LOG_AREA			ZFCP_LOG_AREA_OTHER

int zfcp_adapter_statistic_register(struct zfcp_adapter *adapter)
{
	int retval = 0;
	char name[14];

	sprintf(name, "zfcp-%s", zfcp_get_busid_by_adapter(adapter));
	statistic_interface_create(&adapter->stat_if, name);

	retval |=
	    statistic_create(&adapter->stat_qdio_outb_full, adapter->stat_if,
			     "occurrence_qdio_outb_full",
			     "sbals_left/incidents");
	statistic_define_value(adapter->stat_qdio_outb_full,
			       STATISTIC_RANGE_MIN, STATISTIC_RANGE_MAX,
			       STATISTIC_DEF_MODE_INC);

	retval |= statistic_create(&adapter->stat_qdio_outb, adapter->stat_if,
				   "util_qdio_outb",
				   "slots-occupied/incidents");
	statistic_define_range(adapter->stat_qdio_outb,
			       0, QDIO_MAX_BUFFERS_PER_Q - 1);

	retval |= statistic_create(&adapter->stat_qdio_inb, adapter->stat_if,
				   "util_qdio_inb", "slots-occupied/incidents");
	statistic_define_range(adapter->stat_qdio_inb,
			       0, QDIO_MAX_BUFFERS_PER_Q - 1);

	retval |=
	    statistic_create(&adapter->stat_low_mem_scsi, adapter->stat_if,
			     "occurrence_low_mem_scsi", "-/incidents");
	statistic_define_value(adapter->stat_low_mem_scsi, STATISTIC_RANGE_MIN,
			       STATISTIC_RANGE_MAX, STATISTIC_DEF_MODE_INC);

	retval |= statistic_create(&adapter->stat_erp, adapter->stat_if,
				   "occurrence_erp", "results/incidents");
	statistic_define_value(adapter->stat_erp,
			       STATISTIC_RANGE_MIN, STATISTIC_RANGE_MAX,
			       STATISTIC_DEF_MODE_INC);

	return retval;
}

int zfcp_adapter_statistic_unregister(struct zfcp_adapter *adapter)
{
	return statistic_interface_remove(&adapter->stat_if);
}

int zfcp_unit_statistic_register(struct zfcp_unit *unit)
{
	int retval = 0;
	char name[64];

	atomic_set(&unit->read_num, 0);
	atomic_set(&unit->write_num, 0);

	sprintf(name, "zfcp-%s-0x%016Lx-0x%016Lx",
		zfcp_get_busid_by_unit(unit), unit->port->wwpn, unit->fcp_lun);
	statistic_interface_create(&unit->stat_if, name);

	retval |= statistic_create(&unit->stat_sizes_scsi_write, unit->stat_if,
				   "request_sizes_scsi_write",
				   "bytes/incidents");
	statistic_define_list(unit->stat_sizes_scsi_write, 0,
			      STATISTIC_RANGE_MAX, 256);

	retval |= statistic_create(&unit->stat_sizes_scsi_read, unit->stat_if,
				   "request_sizes_scsi_read",
				   "bytes/incidents");
	statistic_define_list(unit->stat_sizes_scsi_read, 0,
			      STATISTIC_RANGE_MAX, 256);

	retval |= statistic_create(&unit->stat_sizes_scsi_nodata, unit->stat_if,
				   "request_sizes_scsi_nodata",
				   "bytes/incidents");
	statistic_define_value(unit->stat_sizes_scsi_nodata,
			       STATISTIC_RANGE_MIN, STATISTIC_RANGE_MAX,
			       STATISTIC_DEF_MODE_INC);

	retval |= statistic_create(&unit->stat_sizes_scsi_nofit, unit->stat_if,
				   "request_sizes_scsi_nofit",
				   "bytes/incidents");
	statistic_define_list(unit->stat_sizes_scsi_nofit, 0,
			      STATISTIC_RANGE_MAX, 256);

	retval |= statistic_create(&unit->stat_sizes_scsi_nomem, unit->stat_if,
				   "request_sizes_scsi_nomem",
				   "bytes/incidents");
	statistic_define_value(unit->stat_sizes_scsi_nomem, STATISTIC_RANGE_MIN,
			       STATISTIC_RANGE_MAX, STATISTIC_DEF_MODE_INC);

	retval |=
	    statistic_create(&unit->stat_sizes_timedout_write, unit->stat_if,
			     "request_sizes_timedout_write", "bytes/incidents");
	statistic_define_value(unit->stat_sizes_timedout_write,
			       STATISTIC_RANGE_MIN, STATISTIC_RANGE_MAX,
			       STATISTIC_DEF_MODE_INC);

	retval |=
	    statistic_create(&unit->stat_sizes_timedout_read, unit->stat_if,
			     "request_sizes_timedout_read", "bytes/incidents");
	statistic_define_value(unit->stat_sizes_timedout_read,
			       STATISTIC_RANGE_MIN, STATISTIC_RANGE_MAX,
			       STATISTIC_DEF_MODE_INC);

	retval |=
	    statistic_create(&unit->stat_sizes_timedout_nodata, unit->stat_if,
			     "request_sizes_timedout_nodata",
			     "bytes/incidents");
	statistic_define_value(unit->stat_sizes_timedout_nodata,
			       STATISTIC_RANGE_MIN, STATISTIC_RANGE_MAX,
			       STATISTIC_DEF_MODE_INC);

	retval |=
	    statistic_create(&unit->stat_latencies_scsi_write, unit->stat_if,
			     "latencies_scsi_write", "milliseconds/incidents");
	statistic_define_array(unit->stat_latencies_scsi_write, 0, 1024, 1,
			       STATISTIC_DEF_SCALE_LOG2);

	retval |=
	    statistic_create(&unit->stat_latencies_scsi_read, unit->stat_if,
			     "latencies_scsi_read", "milliseconds/incidents");
	statistic_define_array(unit->stat_latencies_scsi_read, 0, 1024, 1,
			       STATISTIC_DEF_SCALE_LOG2);

	retval |=
	    statistic_create(&unit->stat_latencies_scsi_nodata, unit->stat_if,
			     "latencies_scsi_nodata", "milliseconds/incidents");
	statistic_define_array(unit->stat_latencies_scsi_nodata, 0, 1024, 1,
			       STATISTIC_DEF_SCALE_LOG2);

	retval |=
	    statistic_create(&unit->stat_pending_scsi_write, unit->stat_if,
			     "pending_scsi_write", "commands/incidents");
	statistic_define_range(unit->stat_pending_scsi_write, 0,
			       STATISTIC_RANGE_MAX);

	retval |= statistic_create(&unit->stat_pending_scsi_read, unit->stat_if,
				   "pending_scsi_read", "commands/incidents");
	statistic_define_range(unit->stat_pending_scsi_read, 0,
			       STATISTIC_RANGE_MAX);

	retval |= statistic_create(&unit->stat_erp, unit->stat_if,
				   "occurrence_erp", "results/incidents");
	statistic_define_value(unit->stat_erp,
			       STATISTIC_RANGE_MIN, STATISTIC_RANGE_MAX,
			       STATISTIC_DEF_MODE_INC);

	retval |= statistic_create(&unit->stat_eh_reset, unit->stat_if,
				   "occurrence_eh_reset", "results/incidents");
	statistic_define_value(unit->stat_eh_reset,
			       STATISTIC_RANGE_MIN, STATISTIC_RANGE_MAX,
			       STATISTIC_DEF_MODE_INC);

	return retval;
}

int zfcp_unit_statistic_unregister(struct zfcp_unit *unit)
{
	return statistic_interface_remove(&unit->stat_if);
}

#undef ZFCP_LOG_AREA
