/*
 *  drivers/s390/cio/cio_debug.c
 *   S/390 common I/O routines -- message ids for debugging
 *   $Revision: 1.5 $
 *
 *    Copyright (C) 1999-2002 IBM Deutschland Entwicklung GmbH,
 *                            IBM Corporation
 *    Author(s): Ingo Adlung (adlung@de.ibm.com)
 *               Cornelia Huck (cohuck@de.ibm.com) 
 *		 Arnd Bergmann (arndb@de.ibm.com)
 *    ChangeLog: 11/04/2002 Arnd Bergmann Split s390io.c into multiple files,
 *					  see s390io.c for complete list of
 * 					  changes.
 */


#include <linux/init.h>
#include <linux/slab.h>

#include <asm/debug.h>

#include "cio_debug.h"

debug_info_t *cio_debug_msg_id;
debug_info_t *cio_debug_trace_id;
debug_info_t *cio_debug_crw_id;
int cio_debug_initialized;

/*
 * Function: cio_debug_init
 * Initializes three debug logs (under /proc/s390dbf) for common I/O:
 * - cio_msg logs the messages which are printk'ed when CONFIG_DEBUG_IO is on
 * - cio_trace logs the calling of different functions
 * - cio_crw logs the messages which are printk'ed when CONFIG_DEBUG_CRW is on
 * debug levels depend on CONFIG_DEBUG_IO resp. CONFIG_DEBUG_CRW
 */
static int __init
cio_debug_init (void)
{
	int ret = 0;

	cio_debug_msg_id = debug_register ("cio_msg", 4, 4, 16 * sizeof (long));
	if (cio_debug_msg_id != NULL) {
		debug_register_view (cio_debug_msg_id, &debug_sprintf_view);
		debug_set_level (cio_debug_msg_id, 6);
	} else {
		ret = -1;
	}
	cio_debug_trace_id = debug_register ("cio_trace", 4, 4, 8);
	if (cio_debug_trace_id != NULL) {
		debug_register_view (cio_debug_trace_id, &debug_hex_ascii_view);
		debug_set_level (cio_debug_trace_id, 6);
	} else {
		ret = -1;
	}
	cio_debug_crw_id = debug_register ("cio_crw", 2, 4, 16 * sizeof (long));
	if (cio_debug_crw_id != NULL) {
		debug_register_view (cio_debug_crw_id, &debug_sprintf_view);
		debug_set_level (cio_debug_crw_id, 6);
	} else {
		ret = -1;
	}
	if (ret){
		printk ("could not initialize debugging\n");
	} else {
		printk ("debugging initialized\n");
		cio_debug_initialized = 1;
	}
	return ret;
}

arch_initcall (cio_debug_init);
