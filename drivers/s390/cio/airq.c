/*
 *  drivers/s390/cio/airq.c
 *   S/390 common I/O routines -- support for adapter interruptions
 *
 *   $Revision: 1.11 $
 *
 *    Copyright (C) 1999-2002 IBM Deutschland Entwicklung GmbH,
 *			      IBM Corporation
 *    Author(s): Ingo Adlung (adlung@de.ibm.com)
 *		 Cornelia Huck (cohuck@de.ibm.com)
 *		 Arnd Bergmann (arndb@de.ibm.com)
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>

#include "cio_debug.h"
#include "airq.h"

static spinlock_t adapter_lock = SPIN_LOCK_UNLOCKED;
static adapter_int_handler_t adapter_handler;

/*
 * register for adapter interrupts
 *
 * With HiperSockets the zSeries architecture provides for
 *  means of adapter interrups, pseudo I/O interrupts that are
 *  not tied to an I/O subchannel, but to an adapter. However,
 *  it doesn't disclose the info how to enable/disable them, but
 *  to recognize them only. Perhaps we should consider them
 *  being shared interrupts, and thus build a linked list
 *  of adapter handlers ... to be evaluated ...
 */
int
s390_register_adapter_interrupt (adapter_int_handler_t handler)
{
	int ret;
	char dbf_txt[15];

	CIO_TRACE_EVENT (4, "rgaint");

	spin_lock (&adapter_lock);

	if (handler == NULL)
		ret = -EINVAL;
	else if (adapter_handler)
		ret = -EBUSY;
	else {
		adapter_handler = handler;
		ret = 0;
	}

	spin_unlock (&adapter_lock);

	sprintf (dbf_txt, "ret:%d", ret);
	CIO_TRACE_EVENT (4, dbf_txt);

	return (ret);
}

int
s390_unregister_adapter_interrupt (adapter_int_handler_t handler)
{
	int ret;
	char dbf_txt[15];

	CIO_TRACE_EVENT (4, "urgaint");

	spin_lock (&adapter_lock);

	if (handler == NULL)
		ret = -EINVAL;
	else if (handler != adapter_handler)
		ret = -EINVAL;
	else {
		adapter_handler = NULL;
		ret = 0;
	}

	spin_unlock (&adapter_lock);

	sprintf (dbf_txt, "ret:%d", ret);
	CIO_TRACE_EVENT (4, dbf_txt);

	return (ret);
}

void
do_adapter_IO (void)
{
	CIO_TRACE_EVENT (4, "doaio");

	spin_lock (&adapter_lock);

	if (adapter_handler)
		(*adapter_handler) ();

	spin_unlock (&adapter_lock);

	return;
}

EXPORT_SYMBOL (s390_register_adapter_interrupt);
EXPORT_SYMBOL (s390_unregister_adapter_interrupt);
