/*
 *  drivers/s390/cio/requestirq.c
 *   S/390 common I/O routines -- 
 *   $Revision: 1.7 $
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

#include <linux/module.h>
#include <linux/config.h>
#include <asm/irq.h>

#include <asm/delay.h>
#include <asm/debug.h>
#include <asm/cpcmd.h>

#include "cio.h"
#include "cio_debug.h"
#include "ioinfo.h"
#include "s390io.h"

static int enable_subchannel (unsigned int irq);
static int disable_subchannel (unsigned int irq);

/*
 * Note : internal use of irqflags SA_PROBE for NOT path grouping 
 *
 */
int
s390_request_irq_special (int irq,
			  io_handler_func_t io_handler,
			  not_oper_handler_func_t not_oper_handler,
			  unsigned long irqflags,
			  const char *devname, void *dev_id)
{
	int retval = 0;
	unsigned long flags;
	char dbf_txt[15];
	int retry;

	if (irq >= __MAX_SUBCHANNELS)
		return -EINVAL;

	if (!io_handler || !dev_id)
		return -EINVAL;

	if (ioinfo[irq] == INVALID_STORAGE_AREA)
		return -ENODEV;
	
	if (ioinfo[irq]->st)
		return -ENODEV;

	sprintf (dbf_txt, "reqsp%x", irq);
	CIO_TRACE_EVENT (4, dbf_txt);

	/*
	 * The following block of code has to be executed atomically
	 */
	s390irq_spin_lock_irqsave (irq, flags);

	if (!ioinfo[irq]->ui.flags.ready) {
		retry = 5;

		ioinfo[irq]->irq_desc.handler = io_handler;
		ioinfo[irq]->irq_desc.name = devname;
		ioinfo[irq]->irq_desc.dev_id = dev_id;
		ioinfo[irq]->ui.flags.ready = 1;

		do {
			retval = enable_subchannel (irq);
			if (retval) {
				ioinfo[irq]->ui.flags.ready = 0;
				break;
			}

			stsch (irq, &ioinfo[irq]->schib);
			if (ioinfo[irq]->schib.pmcw.ena)
				retry = 0;
			else
				retry--;

		} while (retry);
	} else {
		/*
		 *  interrupt already owned, and shared interrupts
		 *   aren't supported on S/390.
		 */
		retval = -EBUSY;

	}

	s390irq_spin_unlock_irqrestore (irq, flags);

	if (retval == 0) {
		if (!(irqflags & SA_PROBE)) 
			s390_DevicePathVerification (irq, 0);

		ioinfo[irq]->nopfunc = not_oper_handler;
	}

	if (cio_debug_initialized)
		debug_int_event (cio_debug_trace_id, 4, retval);

	return retval;
}

int
s390_request_irq (unsigned int irq,
		  void (*handler) (int, void *, struct pt_regs *),
		  unsigned long irqflags, const char *devname, void *dev_id)
{

	return s390_request_irq_special (irq,
					 (io_handler_func_t) handler,
					 NULL, irqflags, devname, dev_id);
}

/*
 * request_irq wrapper
 */
int
request_irq(unsigned int irq, void (*handler)(int, void *, struct pt_regs *),
	    unsigned long irqflags, const char *devname, void *dev_id)
{
	return s390_request_irq(irq, handler, irqflags, devname, dev_id);
}

void
s390_free_irq (unsigned int irq, void *dev_id)
{
	unsigned long flags;
	int ret;

	char dbf_txt[15];

	if (irq >= __MAX_SUBCHANNELS || ioinfo[irq] == INVALID_STORAGE_AREA)
		return;

	if (ioinfo[irq]->st)
		return;

	sprintf (dbf_txt, "free%x", irq);
	CIO_TRACE_EVENT (2, dbf_txt);

	s390irq_spin_lock_irqsave (irq, flags);

	CIO_DEBUG_NOCONS(irq,KERN_DEBUG, printk, 2, 
			 "Trying to free IRQ %d\n", 
			 irq);

	/*
	 * disable the device and reset all IRQ info if
	 *  the IRQ is actually owned by the handler ...
	 */
	if (ioinfo[irq]->ui.flags.ready) {
		if (dev_id == ioinfo[irq]->irq_desc.dev_id) {
			/* start deregister */
			ioinfo[irq]->ui.flags.unready = 1;

			ret = disable_subchannel (irq);

			if (ret == -EBUSY) {

				/*
				 * kill it !
				 * We try to terminate the I/O by halt_IO first,
				 * then clear_IO.
				 * Because the device may be gone (machine 
				 * check handling), we can't use sync I/O.
				 */

				halt_IO (irq, 0xC8C1D3E3, 0);
				s390irq_spin_unlock_irqrestore (irq, flags);
				udelay (200000);	/* 200 ms */
				s390irq_spin_lock_irqsave (irq, flags);

				ret = disable_subchannel (irq);

				if (ret == -EBUSY) {

					clear_IO (irq, 0x40C3D3D9, 0);
					s390irq_spin_unlock_irqrestore (irq,
									flags);
					udelay (1000000);	/* 1000 ms */
					s390irq_spin_lock_irqsave (irq, flags);

					/* give it a very last try ... */
					disable_subchannel (irq);

					if (ioinfo[irq]->ui.flags.busy) {
						CIO_DEBUG_ALWAYS(KERN_CRIT, 0,
								 "free_irq(%04X) - "
								 "device %04X busy, "
								 "retry count exceeded\n",
								 irq,
								 ioinfo[irq]->
								 devstat.devno);
						
					}
				}
			}

			ioinfo[irq]->ui.flags.ready = 0;
			ioinfo[irq]->ui.flags.unready = 0;	/* deregister ended */

			ioinfo[irq]->nopfunc = NULL;

			s390irq_spin_unlock_irqrestore (irq, flags);
		} else {
			s390irq_spin_unlock_irqrestore (irq, flags);

			CIO_DEBUG_ALWAYS(KERN_ERR, 0,
					 "free_irq(%04X) : error, "
					 "dev_id does not match !\n",
					 irq);

		}
	} else {
		s390irq_spin_unlock_irqrestore (irq, flags);

		CIO_DEBUG_ALWAYS(KERN_ERR, 0,
				 "free_irq(%04X) : error, "
				 "no action block ... !\n", 
				 irq);

	}
}

/*
 * free_irq wrapper.
 */
void
free_irq(unsigned int irq, void *dev_id)
{
	s390_free_irq(irq, dev_id);
}

/*
 * Enable IRQ by modifying the subchannel
 */
static int
enable_subchannel (unsigned int irq)
{
	int ret = 0;
	int ccode;
	int retry = 5;
	char dbf_txt[15];

	sprintf (dbf_txt, "ensch%x", irq);
	CIO_TRACE_EVENT (2, dbf_txt);

	/*
	 * If a previous disable request is pending we reset it. However, this
	 *  status implies that the device may (still) be not-operational.
	 */
	if (ioinfo[irq]->ui.flags.d_disable) {
		ioinfo[irq]->ui.flags.d_disable = 0;
		ret = 0;
	} else {
		ccode = stsch (irq, &(ioinfo[irq]->schib));

		if (ccode) {
			ret = -ENODEV;
		} else {
			ioinfo[irq]->schib.pmcw.ena = 1;

			if (irq == cons_dev) {
				ioinfo[irq]->schib.pmcw.isc = 7;
			} else {
				ioinfo[irq]->schib.pmcw.isc = 3;

			}

			do {
				ccode = msch (irq, &(ioinfo[irq]->schib));

				switch (ccode) {
				case 0:	/* ok */
					ret = 0;
					retry = 0;
					break;

				case 1:	/* status pending */

					ioinfo[irq]->ui.flags.s_pend = 1;
					s390_process_IRQ (irq);
					ioinfo[irq]->ui.flags.s_pend = 0;

					ret = -EIO;
					/* 
					 * might be overwritten on re-driving 
					 * the msch()       
					 */
					retry--;
					break;

				case 2:	/* busy */
					udelay (100);	/* allow for recovery */
					ret = -EBUSY;
					retry--;
					break;

				case 3:	/* not oper */
					ioinfo[irq]->ui.flags.oper = 0;
					retry = 0;
					ret = -ENODEV;
					break;
				}

			} while (retry);

		}
	}

	sprintf (dbf_txt, "ret:%d", ret);
	CIO_TRACE_EVENT (2, dbf_txt);

	return ret;
}

/*
 * Disable IRQ by modifying the subchannel
 */
static int
disable_subchannel (unsigned int irq)
{
	int cc;			/* condition code */
	int ret = 0;		/* function return value */
	int retry = 5;
	char dbf_txt[15];

	sprintf (dbf_txt, "dissch%x", irq);
	CIO_TRACE_EVENT (2, dbf_txt);

	if (ioinfo[irq]->ui.flags.busy) {
		/*
		 * the disable function must not be called while there are
		 *  requests pending for completion !
		 */
		ret = -EBUSY;
	} else {

		/*
		 * If device isn't operational we have to perform delayed
		 *  disabling when the next interrupt occurs - unless the
		 *  irq is re-requested prior to the interrupt to occur.
		 */
		cc = stsch (irq, &(ioinfo[irq]->schib));

		if (cc == 3) {
			ioinfo[irq]->ui.flags.oper = 0;
			ioinfo[irq]->ui.flags.d_disable = 1;

			ret = 0;
		} else {	/* cc == 0 */

			ioinfo[irq]->schib.pmcw.ena = 0;

			do {
				cc = msch (irq, &(ioinfo[irq]->schib));

				switch (cc) {
				case 0:	/* ok */
					retry = 0;
					ret = 0;
					break;

				case 1:	/* status pending */
					ioinfo[irq]->ui.flags.s_pend = 1;
					s390_process_IRQ (irq);
					ioinfo[irq]->ui.flags.s_pend = 0;

					ret = -EIO;
					/* 
					 * might be overwritten on re-driving 
					 * the msch() call       
					 */
					retry--;
					break;

				case 2:	/* busy; this should not happen! */
					CIO_DEBUG_ALWAYS(KERN_CRIT, 0,
							 "disable_subchannel"
							 "(%04X) - unexpected "
							 "busy condition for "
							 "device %04X received!\n",
							 irq,
							 ioinfo[irq]->devstat.
							 devno);
					retry = 0;
					ret = -EBUSY;
					break;

				case 3:	/* not oper */
					/*
					 * should hardly occur ?!
					 */
					ioinfo[irq]->ui.flags.oper = 0;
					ioinfo[irq]->ui.flags.d_disable = 1;
					retry = 0;

					ret = 0;
					/* 
					 * if the device has gone, we don't need 
					 * to disable it anymore !          
					 */
					break;

				}

			} while (retry);

		}
	}

	sprintf (dbf_txt, "ret:%d", ret);
	CIO_TRACE_EVENT (2, dbf_txt);

	return ret;
}

/* FIXME: there must be a cleaner way to express what happens */
extern void do_reipl (int);
void
reipl (int sch)
{
	int i;
	s390_dev_info_t dev_info;

	for (i = 0; i <= highest_subchannel; i++) {
		if (get_dev_info_by_irq (i, &dev_info) == 0
		    && (dev_info.status & DEVSTAT_DEVICE_OWNED)) {
			free_irq (i, ioinfo[i]->irq_desc.dev_id);
		}
	}
	if (MACHINE_IS_VM)
		cpcmd ("IPL", NULL, 0);
	else
		do_reipl (0x10000 | sch);
}

EXPORT_SYMBOL (s390_request_irq_special);
