/*
 *
 * linux/drivers/s390/net/ctcdbug.c ($Revision: 1.1 $)
 *
 * Linux on zSeries OSA Express and HiperSockets support
 *
 * Copyright 2000,2003 IBM Corporation
 *
 *    Author(s): Original Code written by
 *			  Peter Tiedemann (ptiedem@de.ibm.com)
 *
 *    $Revision: 1.1 $	 $Date: 2004/07/02 16:31:22 $
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include "ctcdbug.h"

/**
 * Debug Facility Stuff
 */
debug_info_t *dbf_setup = NULL;
debug_info_t *dbf_data = NULL;
debug_info_t *dbf_trace = NULL;

DEFINE_PER_CPU(char[256], dbf_txt_buf);

void
unregister_dbf_views(void)
{
	if (dbf_setup)
		debug_unregister(dbf_setup);
	if (dbf_data)
		debug_unregister(dbf_data);
	if (dbf_trace)
		debug_unregister(dbf_trace);
}
int
register_dbf_views(void)
{
	dbf_setup = debug_register(CTC_DBF_SETUP_NAME,
					CTC_DBF_SETUP_INDEX,
					CTC_DBF_SETUP_NR_AREAS,
					CTC_DBF_SETUP_LEN);
	dbf_data = debug_register(CTC_DBF_DATA_NAME,
				       CTC_DBF_DATA_INDEX,
				       CTC_DBF_DATA_NR_AREAS,
				       CTC_DBF_DATA_LEN);
	dbf_trace = debug_register(CTC_DBF_TRACE_NAME,
					CTC_DBF_TRACE_INDEX,
					CTC_DBF_TRACE_NR_AREAS,
					CTC_DBF_TRACE_LEN);

	if ((dbf_setup == NULL) || (dbf_data == NULL) ||
	    (dbf_trace == NULL)) {
		unregister_dbf_views();
		return -ENOMEM;
	}
	debug_register_view(dbf_setup, &debug_hex_ascii_view);
	debug_set_level(dbf_setup, CTC_DBF_SETUP_LEVEL);

	debug_register_view(dbf_data, &debug_hex_ascii_view);
	debug_set_level(dbf_data, CTC_DBF_DATA_LEVEL);

	debug_register_view(dbf_trace, &debug_hex_ascii_view);
	debug_set_level(dbf_trace, CTC_DBF_TRACE_LEVEL);

	return 0;
}


