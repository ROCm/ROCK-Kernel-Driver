/*
 *  drivers/s390/s390io.c
 *   S/390 common I/O routines
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
 *               05/03/2002 Cornelia Huck  debug cleanup
 */
#include <linux/module.h>
#include <linux/config.h>
#ifdef CONFIG_PROC_FS
#include <linux/proc_fs.h>
#endif
#include <asm/delay.h>
#include <asm/irq.h>
#include <asm/uaccess.h>

#include <asm/s390io.h>
#include <asm/s390dyn.h>
#include <asm/s390mach.h>
#include <asm/debug.h>

#include "s390io.h"
#include "cio_debug.h"
#include "ioinfo.h"
#include "proc.h"
#include "chsc.h"

#ifdef CONFIG_DEBUG_CRW
#define DBGW printk
#else
#define DBGW(args,...) do {} while (0)
#endif

#define CRW_DEBUG(printk_level,event_level,msg...) ({\
	DBGW(printk_level msg); \
	CIO_CRW_EVENT (event_level, msg); \
})

static void
s390_process_subchannel_source (int irq)
{
	int dev_oper = 0;
	int dev_no = -1;
	int lock = 0;
	int is_owned = 0;

	/*
	 * If the device isn't known yet
	 *   we can't lock it ...
	 */
	if (ioinfo[irq] != INVALID_STORAGE_AREA) {
		s390irq_spin_lock (irq);
		lock = 1;

		if (!ioinfo[irq]->st) {
			dev_oper = ioinfo[irq]->ui.flags.oper;
			
			if (ioinfo[irq]->ui.flags.dval)
				dev_no = ioinfo[irq]->devno;
			
			is_owned = ioinfo[irq]->ui.flags.ready;
		}

	}
	CRW_DEBUG(KERN_DEBUG, 4, 
		  "subchannel validation - start\n");
	s390_validate_subchannel (irq, is_owned);

	if (irq > highest_subchannel)
		highest_subchannel = irq;

	CRW_DEBUG(KERN_DEBUG, 4, 
		  "subchannel validation - done\n");
	/*
	 * After the validate processing
	 *   the ioinfo control block
	 *   should be allocated ...
	 */
	if (lock) {
		s390irq_spin_unlock (irq);
	}

	if (ioinfo[irq] != INVALID_STORAGE_AREA) {

#ifdef CONFIG_ARCH_S390X
		CRW_DEBUG(KERN_DEBUG, 4, 
			  "ioinfo at %08lX\n", 
			  (unsigned long)ioinfo[irq]);
#else				/* CONFIG_ARCH_S390X */
		CRW_DEBUG(KERN_DEBUG, 4, 
			  "ioinfo at %08X\n", 
			  (unsigned)ioinfo[irq]);
#endif				/* CONFIG_ARCH_S390X */

		if (ioinfo[irq]->st)
			return;

		if (ioinfo[irq]->ui.flags.oper == 0) {
			not_oper_handler_func_t nopfunc = ioinfo[irq]->nopfunc;

			/*
			 * If the device has gone
			 *  call not oper handler               
			 */
			if ((dev_oper == 1)
			    && (nopfunc != NULL)) {

				free_irq (irq, ioinfo[irq]->irq_desc.dev_id);
				nopfunc (irq, DEVSTAT_DEVICE_GONE);

			}
		} else {
			CRW_DEBUG(KERN_DEBUG, 4,
				  "device recognition - start\n");
	
			s390_device_recognition_irq (irq);

			CRW_DEBUG(KERN_DEBUG, 4,
				  "device recognition - done\n");
			/*
			 * the device became operational
			 */
			if (dev_oper == 0) {
				devreg_t *pdevreg;

				pdevreg = s390_search_devreg (ioinfo[irq]);

				if (pdevreg != NULL) {
					if (pdevreg->oper_func != NULL)
						pdevreg->
						    oper_func (irq, pdevreg);

				}

			}
			/*
			 * ... it is and was operational, but
			 *      the devno may have changed
			 */
			else if ((ioinfo[irq]->devno != dev_no)
				 && (ioinfo[irq]->nopfunc != NULL)) {

				ioinfo[irq]->nopfunc (irq, DEVSTAT_REVALIDATE);

			}
		}
	}
}

/*
 * s390_do_crw_pending
 *
 * Called by the machine check handler to process CRW pending
 *  conditions. It may be a single CRW, or CRWs may be chained.
 *
 * Note : we currently process CRWs for subchannel source only
 */
void
s390_do_crw_pending (crwe_t * pcrwe)
{
	int irq;
	int chpid;
	
	CRW_DEBUG(KERN_DEBUG, 2, 
		  "do_crw_pending: starting\n");

	while (pcrwe != NULL) {

		switch (pcrwe->crw.rsc) {
		case CRW_RSC_SCH:
			
			irq = pcrwe->crw.rsid;
			
			CRW_DEBUG(KERN_NOTICE, 2, 
				  "source is subchannel %04X\n",
				  irq);

			s390_process_subchannel_source (irq);

			break;

		case CRW_RSC_MONITOR:

			CRW_DEBUG(KERN_NOTICE, 2, 
				  "source is monitoring facility\n");
			break;

		case CRW_RSC_CPATH:

			chpid = pcrwe->crw.rsid;

			CRW_DEBUG(KERN_NOTICE, 2, 
				  "source is channel path %02X\n",
				  chpid);
			break;

		case CRW_RSC_CONFIG:

			CRW_DEBUG(KERN_NOTICE, 2, 
				  "source is configuration-alert facility\n");
			break;

		case CRW_RSC_CSS:

			CRW_DEBUG(KERN_NOTICE, 2, 
				  "source is channel subsystem\n");
			s390_process_css();
			break;

		default:

			CRW_DEBUG(KERN_NOTICE, 2, 
				  "unknown source\n");
			break;

		}

		pcrwe = pcrwe->crwe_next;

	}

	CRW_DEBUG(KERN_DEBUG, 2, 
		  "do_crw_pending: done\n");
	return;
}


