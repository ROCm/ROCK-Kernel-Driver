/*
 *  drivers/s390/cio/cio.c
 *   S/390 common I/O routines -- low level i/o calls
 *   $Revision: 1.26 $
 *
 *    Copyright (C) 1999-2002 IBM Deutschland Entwicklung GmbH,
 *                            IBM Corporation
 *    Author(s): Ingo Adlung (adlung@de.ibm.com)
 *               Cornelia Huck (cohuck@de.ibm.com) 
 *		 Arnd Bergmann (arndb@de.ibm.com)
 *    ChangeLog: 11/04/2002 Arnd Bergmann Split s390io.c into multiple files,
 *					  see s390io.c for complete list of
 * 					  changes.
 *               05/06/2002 Cornelia Huck  some cleanups
 */

#include <linux/module.h>
#include <linux/config.h>
#include <linux/slab.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/delay.h>
#include <asm/debug.h>

#include "airq.h"
#include "cio.h"
#include "cio_debug.h"
#include "ioinfo.h"
#include "s390io.h" /* FIXME: try to do without this */

static atomic_t sync_isc = ATOMIC_INIT (-1);
static int sync_isc_cnt;	/* synchronous irq processing lock */

int cons_dev = -1;	/* identify console device */

/* FIXME: no signed counters here */
static void
s390_displayhex (void *ptr, s32 cnt, int level)
{
	s32 cnt1, cnt2, maxcnt2;
	u32 *currptr = (__u32 *) ptr;
	char buffer[cnt * 12]; /* FIXME: possible stack overflow? */

	for (cnt1 = 0; cnt1 < cnt; cnt1 += 16) {
		sprintf (buffer, "%08lX ", (unsigned long) currptr);
		maxcnt2 = cnt - cnt1;
		if (maxcnt2 > 16)
			maxcnt2 = 16;
		for (cnt2 = 0; cnt2 < maxcnt2; cnt2 += 4)
			sprintf (buffer, "%08X ", *currptr++);
	}
	DBG ("%s\n", buffer);
	if (cio_debug_initialized) 
		debug_text_event (cio_debug_msg_id, level, buffer);
}


/*
 * used by s390_start_IO
 */
static inline void
s390_build_orb(int irq, ccw1_t * cpa,
	       unsigned long flag, __u8 lpm)
{
	ioinfo[irq]->orb.intparm = (__u32) (long) &ioinfo[irq]->u_intparm;
	ioinfo[irq]->orb.fmt = 1;
	ioinfo[irq]->orb.sync = 1;

	ioinfo[irq]->orb.pfch = !(flag & DOIO_DENY_PREFETCH);
	ioinfo[irq]->orb.spnd = (flag & DOIO_ALLOW_SUSPEND ? 1:0);
	ioinfo[irq]->orb.ssic = ((flag & DOIO_ALLOW_SUSPEND)
				 && (flag & DOIO_SUPPRESS_INTER));

	if (flag & DOIO_VALID_LPM) {
		ioinfo[irq]->orb.lpm = lpm;
	} else {
		ioinfo[irq]->orb.lpm = ioinfo[irq]->opm;

	}

#ifdef CONFIG_ARCH_S390X
	/* 
	 * for 64 bit we always support 64 bit IDAWs with 4k page size only
	 */
	ioinfo[irq]->orb.c64 = 1;
	ioinfo[irq]->orb.i2k = 0;
#endif

	ioinfo[irq]->orb.cpa = (__u32) virt_to_phys (cpa);

}

/*
 * used by s390_start_IO to wait on sync. requests
 */
static inline int
s390_do_sync_wait(int irq, int do_tpi)
{
	unsigned long psw_mask;
	uint64_t time_start;
	uint64_t time_curr;
	
	int ready = 0;
	int io_sub = -1;
	int do_retry = 1;

	int ret = 0;

	/*
	 * We shouldn't perform a TPI loop, waiting for an
	 *  interrupt to occur, but should load a WAIT PSW
	 *  instead. Otherwise we may keep the channel subsystem
	 *  busy, not able to present the interrupt. When our
	 *  sync. interrupt arrived we reset the I/O old PSW to
	 *  its original value.
	 */
	psw_mask = PSW_KERNEL_BITS | PSW_MASK_IO | PSW_MASK_WAIT;
	
	/*
	 * Martin didn't like modifying the new PSW, now we take
	 *  a fast exit in do_IRQ() instead
	 */
	*(__u32 *) __LC_SYNC_IO_WORD = 1;

	asm volatile ("STCK %0":"=m" (time_start));
	
	time_start = time_start >> 32;
	
	do {
		if (do_tpi) {
			tpi_info_t tpi_info = { 0, };

			do {
				if (tpi (&tpi_info) == 1) {
					io_sub = tpi_info.irq;
					break;
				}

				udelay (100);	/* usecs */
				asm volatile("STCK %0":"=m" (time_curr));
				
				if (((time_curr >> 32) - time_start) >= 3)
					do_retry = 0;
				
			} while (do_retry);
		} else {
			__load_psw_mask (psw_mask);
			
			io_sub = (__u32) * (__u16 *) __LC_SUBCHANNEL_NR;

		}

		if (do_retry)
			ready = s390_process_IRQ (io_sub);
		
		/*
		 * surrender when retry count's exceeded ...
		 */
	} while (!((io_sub == irq) && (ready == 1)) && do_retry);

	*(__u32 *) __LC_SYNC_IO_WORD = 0;

	if (!do_retry)
		ret = -ETIMEDOUT;
	
	return ret;
}

/*
 * Used by halt_IO and clear_IO to wait on sync. requests
 */
static void
s390_do_sync_wait_haltclear(int irq, int halt)
{
	int io_sub;
	__u32 io_parm;
	unsigned long psw_mask;
	
	int ready = 0;
	
	/*
	 * For hsch and csch, we don't do a tpi loop as for ssch,
	 * but load a wait psw if sync processing is requested.
	 *
	 * FIXME: Are there case where we can't rely on an interrupt
	 *        to occurr? Need to check...
	 */
	psw_mask = PSW_KERNEL_BITS | PSW_MASK_IO | PSW_MASK_WAIT;
	
	/*
	 * Martin didn't like modifying the new PSW, now we take
	 *  a fast exit in do_IRQ() instead
	 */
	*(__u32 *) __LC_SYNC_IO_WORD = 1;
	
	do {
		__load_psw_mask (psw_mask);
		
		io_parm = *(__u32 *) __LC_IO_INT_PARM;
		io_sub = (__u32) * (__u16 *) __LC_SUBCHANNEL_NR;
		
		ready = s390_process_IRQ (io_sub);
		
	} while (!((io_sub == irq) && (ready == 1)));
	
	*(__u32 *) __LC_SYNC_IO_WORD = 0;
	
}

/*
 * called from start_IO after deferred cc=3 conditions
 * to print some debug data
 */
static inline void
s390_cc3_print_data(int irq)
{

	stsch (irq, &(ioinfo[irq]->schib));
	
	CIO_DEBUG(KERN_INFO, 4,
			 "s390_start_IO(%04X) - irb for "
			 "device %04X, after status pending:\n",
			 irq, ioinfo[irq]->devstat.devno);
	
	s390_displayhex (&(ioinfo[irq]->devstat.ii.irb),
			 sizeof (irb_t), 4);
	
	CIO_DEBUG(KERN_INFO, 4,	
			 "s390_start_IO(%04X) - schib for "
			 "device %04X, after status pending:\n",
			 irq, ioinfo[irq]->devstat.devno);
	
	s390_displayhex (&(ioinfo[irq]->schib),
			 sizeof (schib_t), 4);
	
	if (ioinfo[irq]->devstat.flag & DEVSTAT_FLAG_SENSE_AVAIL) {
	CIO_DEBUG(KERN_INFO, 4,	
			 "s390_start_IO(%04X) "
			 "- sense data for device %04X,"
			 " after status pending:\n",
			 irq,
			 ioinfo[irq]->devstat.devno);
		
		s390_displayhex (ioinfo[irq]->irq_desc.
				 dev_id->ii.sense.data,
				 ioinfo[irq]->irq_desc.
				 dev_id->rescnt, 4);
	}

}

static inline int
s390_IO_handle_pending(int irq, int valid_lpm, int halt)
{
	int ret;
	
	if (halt) {
		ioinfo[irq]->devstat.flag |= DEVSTAT_STATUS_PENDING;
	} else {
		ioinfo[irq]->devstat.flag = DEVSTAT_START_FUNCTION
			| DEVSTAT_STATUS_PENDING;
	}
	
	/*
	 * initialize the device driver specific devstat irb area
	 */
	memset (&((devstat_t *) ioinfo[irq]->irq_desc.dev_id)->ii.irb,
		'\0', sizeof (irb_t));
	
	/*
	 * Let the common interrupt handler process the pending status.
	 *  However, we must avoid calling the user action handler, as
	 *  it won't be prepared to handle a pending status during
	 *  do_IO() processing inline. This also implies that process_IRQ
	 *  must terminate synchronously - especially if device sensing
	 *  is required.
	 */
	ioinfo[irq]->ui.flags.s_pend = 1;
	ioinfo[irq]->ui.flags.busy = 1;
	ioinfo[irq]->ui.flags.doio = 1;
	
	s390_process_IRQ (irq);
	
	ioinfo[irq]->ui.flags.s_pend = 0;
	ioinfo[irq]->ui.flags.busy = 0;
	ioinfo[irq]->ui.flags.doio = 0;
	
	ioinfo[irq]->ui.flags.repall = 0;
	ioinfo[irq]->ui.flags.w4final = 0;
	
	ioinfo[irq]->devstat.flag |= DEVSTAT_FINAL_STATUS;
	
	/*
	 * In multipath mode a condition code 3 implies the last path
	 *  has gone, except we have previously restricted the I/O to
	 *  a particular path. A condition code 1 (0 won't occur)
	 *  results in return code EIO as well as 3 with another path
	 *  than the one used (i.e. path available mask is non-zero).
	 */
	if (ioinfo[irq]->devstat.ii.irb.scsw.cc == 3) {
		if (!halt) {
			if (valid_lpm) {
				ioinfo[irq]->opm &=
					~(ioinfo[irq]->
					  devstat.ii.irb.esw.esw1.lpum);
			} else {
				ioinfo[irq]->opm = 0;
				
			}
		}
		
		if ((halt) || (ioinfo[irq]->opm == 0)) {
			ret = -ENODEV;
			ioinfo[irq]->ui.flags.oper = 0;
		} else {
			ret = -EIO;
			
		}
		
		ioinfo[irq]->devstat.flag |= DEVSTAT_NOT_OPER;
		
		if (!halt) 
			s390_cc3_print_data(irq);
		
	} else {
		ret = -EIO;
		ioinfo[irq]->devstat.flag &= ~DEVSTAT_NOT_OPER;
		ioinfo[irq]->ui.flags.oper = 1;
		
	}
	
	return ret;
}

static inline int
s390_start_IO_handle_notoper(int irq, 
			     int valid_lpm,
			     __u8 lpm)
{
	int ret;

	if (valid_lpm) {
		ioinfo[irq]->opm &= ~lpm;
		switch_off_chpids(irq, lpm);
	} else {
		ioinfo[irq]->opm = 0;
		
	}
	
	if (ioinfo[irq]->opm == 0) {
		ioinfo[irq]->ui.flags.oper = 0;
		ioinfo[irq]->devstat.flag |= DEVSTAT_NOT_OPER;
		
	}
	
	ret = -ENODEV;
	
	memcpy (ioinfo[irq]->irq_desc.dev_id,
		&(ioinfo[irq]->devstat), sizeof (devstat_t));
	
	stsch (irq, &(ioinfo[irq]->schib));
	
	CIO_DEBUG(KERN_WARNING, 0, 
			 "s390_start_IO(%04X) - schib for "
			 "device %04X, after 'not oper' status:\n",
			 irq, ioinfo[irq]->devstat.devno);
	
	s390_displayhex (&(ioinfo[irq]->schib), 
			 sizeof (schib_t), 0);
	
	return ret;
}

int
s390_start_IO (int irq,		/* IRQ */
	       ccw1_t * cpa,	/* logical channel prog addr */
	       unsigned long user_intparm,	/* interruption parameter */
	       __u8 lpm,	/* logical path mask */
	       unsigned long flag)
{				/* flags */
	int ccode;
	int ret = 0;
	char dbf_txt[15];

	/*
	 * The flag usage is mutal exclusive ...
	 */
	if ((flag & DOIO_EARLY_NOTIFICATION)
	    && (flag & DOIO_REPORT_ALL)) 
		return -EINVAL;

	sprintf (dbf_txt, "stIO%x", irq);
	CIO_TRACE_EVENT (4, dbf_txt);

	s390_build_orb(irq, cpa, flag, lpm);

	/*
	 * If sync processing was requested we lock the sync ISC, modify the
	 *  device to present interrupts for this ISC only and switch the
	 *  CPU to handle this ISC exclusively.
	 */
	if (flag & DOIO_WAIT_FOR_INTERRUPT) {
		ret = enable_cpu_sync_isc (irq);

		if (ret) 
			return ret;

	}

	if (flag & DOIO_DONT_CALL_INTHDLR) 
		ioinfo[irq]->ui.flags.repnone = 1;

	/*
	 * Issue "Start subchannel" and process condition code
	 */
	ccode = ssch (irq, &(ioinfo[irq]->orb));

	sprintf (dbf_txt, "ccode:%d", ccode);
	CIO_TRACE_EVENT (4, dbf_txt);

	switch (ccode) {
	case 0:

		if (!ioinfo[irq]->ui.flags.w4sense) {
			/*
			 * init the device driver specific devstat irb area
			 *
			 * Note : don´t clear saved irb info in case of sense !
			 */
			memset (&((devstat_t *) ioinfo[irq]->irq_desc.dev_id)->
				ii.irb, '\0', sizeof (irb_t));
		}

		memset (&ioinfo[irq]->devstat.ii.irb, '\0', sizeof (irb_t));

		/*
		 * initialize device status information
		 */
		ioinfo[irq]->ui.flags.busy = 1;
		ioinfo[irq]->ui.flags.doio = 1;

		ioinfo[irq]->u_intparm = user_intparm;
		ioinfo[irq]->devstat.cstat = 0;
		ioinfo[irq]->devstat.dstat = 0;
		ioinfo[irq]->devstat.lpum = 0;
		ioinfo[irq]->devstat.flag = DEVSTAT_START_FUNCTION;
		ioinfo[irq]->devstat.scnt = 0;

		ioinfo[irq]->ui.flags.fast = 0;
		ioinfo[irq]->ui.flags.repall = 0;

		/*
		 * Check for either early (FAST) notification requests
		 *  or if we are to return all interrupt info.
		 * Default is to call IRQ handler at secondary status only
		 */
		if (flag & DOIO_EARLY_NOTIFICATION) {
			ioinfo[irq]->ui.flags.fast = 1;
		} else if (flag & DOIO_REPORT_ALL) {
			ioinfo[irq]->ui.flags.repall = 1;

		}

		ioinfo[irq]->ulpm = ioinfo[irq]->orb.lpm;

		/*
		 * If synchronous I/O processing is requested, we have
		 *  to wait for the corresponding interrupt to occur by
		 *  polling the interrupt condition. However, as multiple
		 *  interrupts may be outstanding, we must not just wait
		 *  for the first interrupt, but must poll until ours
		 *  pops up.
		 */
		if (flag & DOIO_WAIT_FOR_INTERRUPT) 
			ret = s390_do_sync_wait(irq, flag&DOIO_TIMEOUT);

		break;

	case 1:		/* status pending */

		ret = s390_IO_handle_pending(irq, 
					     flag & DOIO_VALID_LPM,
					     0);
		break;

	case 2:		/* busy */

		ret = -EBUSY;
		break;

	default:		/* device/path not operational */

		ret = s390_start_IO_handle_notoper(irq, 
						   flag & DOIO_VALID_LPM,
						   lpm);
		break;

	}

	if (flag & DOIO_WAIT_FOR_INTERRUPT) 
		disable_cpu_sync_isc (irq);

	if (flag & DOIO_DONT_CALL_INTHDLR) 
		ioinfo[irq]->ui.flags.repnone = 0;

	return ret;
}

int
do_IO (int irq,			/* IRQ */
       ccw1_t * cpa,		/* channel program address */
       unsigned long user_intparm,	/* interruption parameter */
       __u8 lpm,		/* logical path mask */
       unsigned long flag)
{				/* flags : see above */
	int ret = 0;
	char dbf_txt[15];

	SANITY_CHECK (irq);

	/* handler registered ? or free_irq() in process already ? */
	if (!ioinfo[irq]->ui.flags.ready || ioinfo[irq]->ui.flags.unready) 
		return -ENODEV;

	sprintf (dbf_txt, "doIO%x", irq);
	CIO_TRACE_EVENT (4, dbf_txt);

	/*
	 * Note: We ignore the device operational status - if not operational,
	 *        the SSCH will lead to an -ENODEV condition ...
	 */
	if (!ioinfo[irq]->ui.flags.busy) {	/* last I/O completed ? */
		ret = s390_start_IO (irq, cpa, user_intparm, lpm, flag);
		if ((ret == -ETIMEDOUT) && (flag & DOIO_CANCEL_ON_TIMEOUT)) {
			/*
			 * We should better cancel the io request here
			 * or we might not be able to do io on this sch
			 * again
			 */
			cancel_IO (irq);
		}

	} else if (ioinfo[irq]->ui.flags.fast) {
		/*
		 * If primary status was received and ending status is missing,
		 *  the device driver won't be notified on the ending status
		 *  if early (fast) interrupt notification was requested.
		 *  Therefore we have to queue the next incoming request. If
		 *  halt_IO() is issued while there is a request queued, a HSCH
		 *  needs to be issued and the queued request must be deleted
		 *  but its intparm must be returned (see halt_IO() processing)
		 */
		if (ioinfo[irq]->ui.flags.w4final
		    && !ioinfo[irq]->ui.flags.doio_q) {
			ioinfo[irq]->qflag = flag;
			ioinfo[irq]->qcpa = cpa;
			ioinfo[irq]->qintparm = user_intparm;
			ioinfo[irq]->qlpm = lpm;
		} else {
			ret = -EBUSY;

		}
	} else {
		ret = -EBUSY;

	}

	return ret;

}

/*
 * resume suspended I/O operation
 */
int
resume_IO (int irq)
{
	int ret = 0;
	char dbf_txt[15];

	SANITY_CHECK (irq);

	sprintf (dbf_txt, "resIO%x", irq);
	CIO_TRACE_EVENT (4, dbf_txt);

	/*
	 * We allow for 'resume' requests only for active I/O operations
	 */
	if (ioinfo[irq]->ui.flags.busy) {
		int ccode;

		ccode = rsch (irq);

		sprintf (dbf_txt, "ccode:%d", ccode);
		CIO_TRACE_EVENT (4, dbf_txt);

		switch (ccode) {
		case 0:
			break;

		case 1:
			s390_process_IRQ (irq);
			ret = -EBUSY;
			break;

		case 2:
			ret = -EINVAL;
			break;

		case 3:
			/*
			 * useless to wait for request completion
			 *  as device is no longer operational !
			 */
			ioinfo[irq]->ui.flags.oper = 0;
			ioinfo[irq]->ui.flags.busy = 0;
			ret = -ENODEV;
			break;

		}

	} else {
		ret = -ENOTCONN;

	}

	return ret;
}

/*
 * Note: The "intparm" parameter is not used by the halt_IO() function
 *       itself, as no ORB is built for the HSCH instruction. However,
 *       it allows the device interrupt handler to associate the upcoming
 *       interrupt with the halt_IO() request.
 */
int
halt_IO (int irq, unsigned long user_intparm, unsigned long flag)
{				/* possible DOIO_WAIT_FOR_INTERRUPT */
	int ret;
	int ccode;
	char dbf_txt[15];

	SANITY_CHECK (irq);

	/*
	 * we only allow for halt_IO if the device has an I/O handler associated
	 */
	if (!ioinfo[irq]->ui.flags.ready) 
		return -ENODEV;

	/*
	 * we ignore the halt_io() request if ending_status was received but
	 *  a SENSE operation is waiting for completion.
	 */
	if (ioinfo[irq]->ui.flags.w4sense) 
		return 0;

	sprintf (dbf_txt, "haltIO%x", irq);
	CIO_TRACE_EVENT (2, dbf_txt);

	/*
	 * If sync processing was requested we lock the sync ISC,
	 *  modify the device to present interrupts for this ISC only
	 *  and switch the CPU to handle this ISC exclusively.
	 */
	if (flag & DOIO_WAIT_FOR_INTERRUPT) {
		ret = enable_cpu_sync_isc (irq);

		if (ret)
			return ret;
	}

	/*
	 * Issue "Halt subchannel" and process condition code
	 */
	ccode = hsch (irq);

	sprintf (dbf_txt, "ccode:%d", ccode);
	CIO_TRACE_EVENT (2, dbf_txt);

	switch (ccode) {
	case 0:

		ioinfo[irq]->ui.flags.haltio = 1;

		if (!ioinfo[irq]->ui.flags.doio) {
			ioinfo[irq]->ui.flags.busy = 1;
			ioinfo[irq]->u_intparm = user_intparm;
			ioinfo[irq]->devstat.cstat = 0;
			ioinfo[irq]->devstat.dstat = 0;
			ioinfo[irq]->devstat.lpum = 0;
			ioinfo[irq]->devstat.flag = DEVSTAT_HALT_FUNCTION;
			ioinfo[irq]->devstat.scnt = 0;

		} else {
			ioinfo[irq]->devstat.flag |= DEVSTAT_HALT_FUNCTION;

		}

		/*
		 * If synchronous I/O processing is requested, we have
		 *  to wait for the corresponding interrupt to occur by
		 *  polling the interrupt condition. However, as multiple
		 *  interrupts may be outstanding, we must not just wait
		 *  for the first interrupt, but must poll until ours
		 *  pops up.
		 */
		if (flag & DOIO_WAIT_FOR_INTERRUPT) 
			s390_do_sync_wait_haltclear(irq, 1);

		ret = 0;
		break;

	case 1:		/* status pending */

		ret = s390_IO_handle_pending(irq, 0, 1);
		break;

	case 2:		/* busy */

		ret = -EBUSY;
		break;

	default:		/* device not operational */

		ret = -ENODEV;
		break;

	}

	if (flag & DOIO_WAIT_FOR_INTERRUPT) 
		disable_cpu_sync_isc (irq);

	return ret;
}

/*
 * Note: The "intparm" parameter is not used by the clear_IO() function
 *       itself, as no ORB is built for the CSCH instruction. However,
 *       it allows the device interrupt handler to associate the upcoming
 *       interrupt with the clear_IO() request.
 */
int
clear_IO (int irq, unsigned long user_intparm, unsigned long flag)
{				/* possible DOIO_WAIT_FOR_INTERRUPT */
	int ret = 0;
	int ccode;
	char dbf_txt[15];

	SANITY_CHECK (irq);

	if (ioinfo[irq] == INVALID_STORAGE_AREA)
		return -ENODEV;

	/*
	 * we only allow for clear_IO if the device has an I/O handler associated
	 */
	if (!ioinfo[irq]->ui.flags.ready)
		return -ENODEV;
	/*
	 * we ignore the clear_io() request if ending_status was received but
	 *  a SENSE operation is waiting for completion.
	 */
	if (ioinfo[irq]->ui.flags.w4sense)
		return 0;

	sprintf (dbf_txt, "clearIO%x", irq);
	CIO_TRACE_EVENT (2, dbf_txt);

	/*
	 * If sync processing was requested we lock the sync ISC,
	 *  modify the device to present interrupts for this ISC only
	 *  and switch the CPU to handle this ISC exclusively.
	 */
	if (flag & DOIO_WAIT_FOR_INTERRUPT) {
		ret = enable_cpu_sync_isc (irq);

		if (ret)
			return ret;
	}

	/*
	 * Issue "Clear subchannel" and process condition code
	 */
	ccode = csch (irq);

	sprintf (dbf_txt, "ccode:%d", ccode);
	CIO_TRACE_EVENT (2, dbf_txt);

	switch (ccode) {
	case 0:

		ioinfo[irq]->ui.flags.haltio = 1;

		if (!ioinfo[irq]->ui.flags.doio) {
			ioinfo[irq]->ui.flags.busy = 1;
			ioinfo[irq]->u_intparm = user_intparm;
			ioinfo[irq]->devstat.cstat = 0;
			ioinfo[irq]->devstat.dstat = 0;
			ioinfo[irq]->devstat.lpum = 0;
			ioinfo[irq]->devstat.flag = DEVSTAT_CLEAR_FUNCTION;
			ioinfo[irq]->devstat.scnt = 0;

		} else {
			ioinfo[irq]->devstat.flag |= DEVSTAT_CLEAR_FUNCTION;

		}

		/*
		 * If synchronous I/O processing is requested, we have
		 *  to wait for the corresponding interrupt to occur by
		 *  polling the interrupt condition. However, as multiple
		 *  interrupts may be outstanding, we must not just wait
		 *  for the first interrupt, but must poll until ours
		 *  pops up.
		 */
		if (flag & DOIO_WAIT_FOR_INTERRUPT) 
			s390_do_sync_wait_haltclear(irq, 0);

		ret = 0;
		break;

	case 1:		/* no status pending for csh */
		BUG ();
		break;

	case 2:		/* no busy for csh */
		BUG ();
		break;

	default:		/* device not operational */

		ret = -ENODEV;
		break;

	}

	if (flag & DOIO_WAIT_FOR_INTERRUPT) 
		disable_cpu_sync_isc (irq);

	return ret;
}

/*
 * Function: cancel_IO
 * Issues a "Cancel Subchannel" on the specified subchannel
 * Note: We don't need any fancy intparms and flags here
 *       since xsch is executed synchronously.
 * Only for common I/O internal use as for now.
 */
int
cancel_IO (int irq)
{
	int ccode;
	char dbf_txt[15];
	int ret = 0;

	sprintf (dbf_txt, "cancelIO%x", irq);
	CIO_TRACE_EVENT (2, dbf_txt);

	ccode = xsch (irq);

	sprintf (dbf_txt, "ccode:%d", ccode);
	CIO_TRACE_EVENT (2, dbf_txt);

	switch (ccode) {

	case 0:		/* success */
		ret = 0;
		break;

	case 1:		/* status pending */

		/* process the pending irq... */
		s390_process_IRQ (irq);
		ret = -EBUSY;
		break;

	case 2:		/* not applicable */
		ret = -EINVAL;
		break;

	default:		/* not oper */
		ret = -ENODEV;
	}

	return ret;
}

/*
 * do_IRQ() handles all normal I/O device IRQ's (the special
 *          SMP cross-CPU interrupts have their own specific
 *          handlers).
 *
 */
asmlinkage void
do_IRQ (struct pt_regs regs)
{
	/*
	 * Get interrupt info from lowcore
	 */
	volatile tpi_info_t *tpi_info = (tpi_info_t *) (__LC_SUBCHANNEL_ID);

	/*
	 * take fast exit if CPU is in sync. I/O state
	 *
	 * Note: we have to turn off the WAIT bit and re-disable
	 *       interrupts prior to return as this was the initial
	 *       entry condition to synchronous I/O.
	 */
	if (*(__u32 *) __LC_SYNC_IO_WORD) {
		regs.psw.mask &= ~(PSW_MASK_WAIT | PSW_MASK_IO);
		return;
	}
	/* endif */
	do {
		/*
		 * Non I/O-subchannel thin interrupts are processed differently
		 */
		if (tpi_info->adapter_IO == 1 &&
		    tpi_info->int_type == IO_INTERRUPT_TYPE) {
			irq_enter ();
			do_adapter_IO (tpi_info->intparm);
			irq_exit ();
		} else {
			unsigned int irq = tpi_info->irq;

			/*
			 * fix me !!!
			 *
			 * instead of boxing the device, we need to schedule device
			 * recognition, the interrupt stays pending. We need to
			 * dynamically allocate an ioinfo structure, etc..
			 */
			if (ioinfo[irq] == INVALID_STORAGE_AREA) 
				return;	/* this keeps the device boxed ... */

			if (ioinfo[irq]->st) {
				/* How can that be? */
				printk(KERN_WARNING "Received interrupt on "
				       "non-IO subchannel %x!\n", irq);
				return;
			}

			irq_enter ();
			s390irq_spin_lock (irq);
			s390_process_IRQ (irq);
			s390irq_spin_unlock (irq);
			irq_exit ();
		}

		/*
		 * Are more interrupts pending?
		 * If so, the tpi instruction will update the lowcore 
		 * to hold the info for the next interrupt.
		 * We don't do this for VM because a tpi drops the cpu
		 * out of the sie which costs more cycles than it saves.
		 */
	} while (!MACHINE_IS_VM && tpi (NULL) != 0);

	return;
}

/*
 * used by s390_process_irq to start queued channel programs
 */
static inline void
s390_start_queued_io(int irq, devstat_t *udp)
{
	int ret;
	int do_cancel = ioinfo[irq]->qflag & DOIO_CANCEL_ON_TIMEOUT;
	
	ret = s390_start_IO (irq,
			     ioinfo[irq]->qcpa,
			     ioinfo[irq]->qintparm,
			     ioinfo[irq]->qlpm,
			     ioinfo[irq]->qflag);
	
	ioinfo[irq]->ui.flags.doio_q = 0;
	
	/*
	 * If s390_start_IO() failed call the device's interrupt
	 *  handler, the IRQ related devstat area was setup by
	 *  s390_start_IO() accordingly already (status pending
	 *  condition).
	 */
	if (ret) 
		ioinfo[irq]->irq_desc.handler (irq, udp, NULL);
		
	/* 
	 * better cancel the io when we time out...
	 */
	if ((ret == -ETIMEDOUT) && do_cancel) 
		cancel_IO (irq);

}

/*
 * used by s390_process_irq to issue a sense ccw
 */
static inline void
s390_issue_sense(unsigned int irq, devstat_t *dp)
{
	int ret_io;
	ccw1_t *s_ccw = &ioinfo[irq]->senseccw;
	unsigned long s_flag = 0;

	s_ccw->cmd_code = CCW_CMD_BASIC_SENSE;
	s_ccw->cda = (__u32) virt_to_phys (ioinfo[irq]->sense_data);
	s_ccw->count = SENSE_MAX_COUNT;
	s_ccw->flags = CCW_FLAG_SLI;
	
	/*
	 * If free_irq() or a sync do_IO/s390_start_IO() is in
	 *  process we have to sense synchronously
	 */
	if (ioinfo[irq]->ui.flags.unready || ioinfo[irq]->ui.flags.syncio) 
		s_flag = DOIO_WAIT_FOR_INTERRUPT;
	
	/*
	 * Reset status info
	 *
	 * It does not matter whether this is a sync. or async.
	 *  SENSE request, but we have to assure we don't call
	 *  the irq handler now, but keep the irq in busy state.
	 *  In sync. mode s390_process_IRQ() is called recursively,
	 *  while in async. mode we re-enter do_IRQ() with the
	 *  next interrupt.
	 *
	 * Note : this may be a delayed sense request !
	 */
	ioinfo[irq]->ui.flags.fast = 0;
	ioinfo[irq]->ui.flags.repall = 0;
	ioinfo[irq]->ui.flags.w4final = 0;
	ioinfo[irq]->ui.flags.delsense = 0;
	
	dp->cstat = 0;
	dp->dstat = 0;
	dp->rescnt = SENSE_MAX_COUNT;
	
	ioinfo[irq]->ui.flags.w4sense = 1;
	
	ret_io = s390_start_IO (irq, s_ccw, 0xE2C5D5E2,	/* = SENSe */
				0,	/* n/a */
				s_flag);
	
}

static inline void
s390_reset_flags_after_ending_status(int irq)
{
	ioinfo[irq]->ui.flags.busy = 0;
	ioinfo[irq]->ui.flags.doio = 0;
	ioinfo[irq]->ui.flags.haltio = 0;
	ioinfo[irq]->ui.flags.fast = 0;
	ioinfo[irq]->ui.flags.repall = 0;
	ioinfo[irq]->ui.flags.w4final = 0;

	ioinfo[irq]->devstat.flag |= DEVSTAT_FINAL_STATUS;
	ioinfo[irq]->irq_desc.dev_id->flag |= DEVSTAT_FINAL_STATUS;
	
}

/*
 * for cc=0 and cc=1 after tsch
 */
static inline int
s390_process_IRQ_normal(unsigned int irq)
{
	int ending_status;
	unsigned int fctl;	/* function control */
	unsigned int stctl;	/* status   control */
	unsigned int actl;	/* activity control */
	scsw_t *scsw;
	int allow4handler = 1;
	devstat_t *dp;
	devstat_t *udp;
	
	dp = &ioinfo[irq]->devstat;
	udp = ioinfo[irq]->irq_desc.dev_id;

	scsw = &dp->ii.irb.scsw;
	
	fctl = scsw->fctl;
	stctl = scsw->stctl;
	actl = scsw->actl;
	
	ioinfo[irq]->stctl |= stctl;
	
	ending_status = (stctl & SCSW_STCTL_SEC_STATUS)
		|| (stctl == (SCSW_STCTL_ALERT_STATUS | SCSW_STCTL_STATUS_PEND))
		|| (stctl == SCSW_STCTL_STATUS_PEND);
	
	/*
	 * Check for unsolicited interrupts - for debug purposes only
	 *
	 * We only consider an interrupt as unsolicited, if the device was not
	 *  actively in use (busy) and an interrupt other than an ALERT status
	 *  was received.
	 *
	 * Note: We must not issue a message to the console, if the
	 *       unsolicited interrupt applies to the console device
	 *       itself !
	 */
	if (!(stctl & SCSW_STCTL_ALERT_STATUS)
	    && (ioinfo[irq]->ui.flags.busy == 0)) {
		
		CIO_DEBUG_NOCONS(irq, KERN_INFO, DBG, 2,
				 "Unsolicited interrupt "
				 "received for device %04X "
				 "on subchannel %04X\n"
				 " ... device status : %02X "
				 "subchannel status : %02X\n",
				 dp->devno,
				 irq, dp->dstat, dp->cstat);
		
		CIO_DEBUG_NOCONS(irq, KERN_INFO, DBG, 2,
				 "s390_process_IRQ(%04X) - irb for "
				 "device %04X, ending_status %d:\n", 
				 irq, dp->devno, ending_status);
		
		s390_displayhex (&(dp->ii.irb),
				 sizeof (irb_t), 2);
	}
	
	/*
	 * take fast exit if no handler is available
	 */
	if (!ioinfo[irq]->ui.flags.ready)
		return ending_status;
	
	/*
	 * Check whether we must issue a SENSE CCW ourselves if there is no
	 *  concurrent sense facility installed for the subchannel.
	 *
	 * Note: We should check for ioinfo[irq]->ui.flags.consns but VM
	 *       violates the ESA/390 architecture and doesn't present an
	 *       operand exception for virtual devices without concurrent
	 *       sense facility available/supported when enabling the
	 *       concurrent sense facility.
	 */
	if (((scsw->dstat & DEV_STAT_UNIT_CHECK) 
	     && (!dp->flag & DEVSTAT_FLAG_SENSE_AVAIL))
	    || (ioinfo[irq]->ui.flags.delsense && ending_status)) {
		
		allow4handler = 0;
		
		if (ending_status) {
			/*
			 * We copy the current status information into the device driver
			 *  status area. Then we can use the local devstat area for device
			 *  sensing. When finally calling the IRQ handler we must not overlay
			 *  the original device status but copy the sense data only.
			 */
			memcpy (udp, dp, sizeof (devstat_t));
			
			s390_issue_sense(irq, dp);
			
		} else {
			/*
			 * we received an Unit Check but we have no final
			 *  status yet, therefore we must delay the SENSE
			 *  processing. However, we must not report this
			 *  intermediate status to the device interrupt
			 *  handler.
			 */
			ioinfo[irq]->ui.flags.fast = 0;
			ioinfo[irq]->ui.flags.repall = 0;
			
			ioinfo[irq]->ui.flags.delsense = 1;
			
		}
		
	}
	
	/*
	 * we allow for the device action handler if .
	 *  - we received ending status
	 *  - the action handler requested to see all interrupts
	 *  - we received an intermediate status
	 *  - fast notification was requested (primary status)
	 *  - unsolicited interrupts
	 *
	 */
	if (allow4handler) {
		allow4handler = ending_status
			|| (ioinfo[irq]->ui.flags.repall)
			|| (stctl & SCSW_STCTL_INTER_STATUS)
			|| ((ioinfo[irq]->ui.flags.fast)
			    && (stctl & SCSW_STCTL_PRIM_STATUS))
			|| (ioinfo[irq]->ui.flags.oper == 0);
		
	}
	
	/*
	 * We used to copy the device status information right before
	 *  calling the device action handler. However, in status
	 *  pending situations during do_IO() or halt_IO(), as well as
	 *  enable_subchannel/disable_subchannel processing we must
	 *  synchronously return the status information and must not
	 *  call the device action handler.
	 *
	 */
	if (allow4handler) {
		/*
		 * if we were waiting for sense data we copy the sense
		 *  bytes only as the original status information was
		 *  saved prior to sense already.
		 */
		if (ioinfo[irq]->ui.flags.w4sense) {
			int sense_count =
				SENSE_MAX_COUNT - ioinfo[irq]->devstat.rescnt;
			
			CIO_DEBUG_NOCONS(irq, KERN_DEBUG, DBG, 4,
					 "s390_process_IRQ( %04X ): "
					 "BASIC SENSE bytes avail %d\n",
					 irq, sense_count);

			ioinfo[irq]->ui.flags.w4sense = 0;
			udp->flag |= DEVSTAT_FLAG_SENSE_AVAIL;
			udp->scnt = sense_count;

			if (sense_count >= 0) {
				memcpy (udp->ii.sense.data,
					ioinfo[irq]->sense_data,
					sense_count);
			} else {
				panic ("s390_process_IRQ(%04x) encountered "
				       "negative sense count\n", irq);

			}
		} else {
			memcpy (udp, dp, 
				sizeof(devstat_t) - 
				(dp->flag & DEVSTAT_FLAG_SENSE_AVAIL ?  
				 0 : SENSE_MAX_COUNT));
			
		}

	}

	/*
	 * for status pending situations other than deferred interrupt
	 *  conditions detected by s390_process_IRQ() itself we must not
	 *  call the handler. This will synchronously be reported back
	 *  to the caller instead, e.g. when detected during do_IO().
	 */
	if (ioinfo[irq]->ui.flags.s_pend
	    || ioinfo[irq]->ui.flags.unready
	    || ioinfo[irq]->ui.flags.repnone) {

		if (ending_status) 
			s390_reset_flags_after_ending_status(irq);

		allow4handler = 0;

	}

	/*
	 * Call device action handler if applicable
	 */
	if (allow4handler) {

		/*
		 *  We only reset the busy condition when we are sure that no further
		 *   interrupt is pending for the current I/O request (ending_status).
		 */
		if (ending_status || !ioinfo[irq]->ui.flags.oper) {
			ioinfo[irq]->ui.flags.oper = 1;	/* dev IS oper */

			s390_reset_flags_after_ending_status(irq);
			
			ioinfo[irq]->irq_desc.handler (irq, udp, NULL);

			/*
			 * reset intparm after final status or we will badly present unsolicited
			 *  interrupts with a intparm value possibly no longer valid.
			 */
			dp->intparm = 0;

			/*
			 * Was there anything queued ? Start the pending channel program
			 *  if there is one.
			 */
			if (ioinfo[irq]->ui.flags.doio_q) 
				s390_start_queued_io(irq, udp);

		} else {
			ioinfo[irq]->ui.flags.w4final = 1;
			
			/*
			 * Eventually reset subchannel PCI status and
			 *  set the PCI or SUSPENDED flag in the user
			 *  device status block if appropriate.
			 */
			if (dp->cstat & SCHN_STAT_PCI) {
				udp->flag |= DEVSTAT_PCI;
				dp->cstat &= ~SCHN_STAT_PCI;
			}

			if (actl & SCSW_ACTL_SUSPENDED) 
				udp->flag |= DEVSTAT_SUSPENDED;

			ioinfo[irq]->irq_desc.handler (irq, udp, NULL);

		}

	}

	return ending_status;

}

/*
 * for cc=3 after tsch
 */
static inline int
s390_process_IRQ_notoper(unsigned int irq)
{
	devstat_t *dp;
	devstat_t *udp;

	ioinfo[irq]->ui.flags.busy = 0;
	ioinfo[irq]->ui.flags.doio = 0;
	ioinfo[irq]->ui.flags.haltio = 0;
	
	dp = &ioinfo[irq]->devstat;
	udp = ioinfo[irq]->irq_desc.dev_id;

	dp->cstat = 0;
	dp->dstat = 0;
	
	if (ioinfo[irq]->ulpm != ioinfo[irq]->opm) 
		/*
		 * either it was the only path or it was restricted ...
		 */
		ioinfo[irq]->opm &=
			~(ioinfo[irq]->devstat.ii.irb.esw.esw1.lpum);
	else 
		ioinfo[irq]->opm = 0;
	
	if (ioinfo[irq]->opm == 0) 
		ioinfo[irq]->ui.flags.oper = 0;

	
	ioinfo[irq]->devstat.flag |= DEVSTAT_NOT_OPER;
	ioinfo[irq]->devstat.flag |= DEVSTAT_FINAL_STATUS;
	
	/*
	 * When we find a device "not oper" we save the status
	 *  information into the device status area and call the
	 *  device specific interrupt handler.
	 *
	 * Note: currently we don't have any way to reenable
	 *       the device unless an unsolicited interrupt
	 *       is presented. We don't check for spurious
	 *       interrupts on "not oper" conditions.
	 */
	
	if ((ioinfo[irq]->ui.flags.fast)
	    && (ioinfo[irq]->ui.flags.w4final)) {
		/*
		 * If a new request was queued already, we have
		 *  to simulate the "not oper" status for the
		 *  queued request by switching the "intparm" value
		 *  and notify the interrupt handler.
		 */
		if (ioinfo[irq]->ui.flags.doio_q) 
			ioinfo[irq]->devstat.intparm = ioinfo[irq]->qintparm;
			
	}
	
	ioinfo[irq]->ui.flags.fast = 0;
	ioinfo[irq]->ui.flags.repall = 0;
	ioinfo[irq]->ui.flags.w4final = 0;
	
	/*
	 * take fast exit if no handler is available
	 */
	if (!ioinfo[irq]->ui.flags.ready)
		return 0;
	
	memcpy (udp, &(ioinfo[irq]->devstat), 
		sizeof(devstat_t) - 
		(dp->flag & DEVSTAT_FLAG_SENSE_AVAIL ?  0 : SENSE_MAX_COUNT));
	
	ioinfo[irq]->devstat.intparm = 0;
	
	if (!(ioinfo[irq]->ui.flags.s_pend || ioinfo[irq]->ui.flags.repnone))
		ioinfo[irq]->irq_desc.handler (irq, udp, NULL);
	
	return 1;

}


/* FIXME:  help!!!! */
/*
 * s390_process_IRQ() handles status pending situations and interrupts
 *
 * Called by : do_IRQ()             - for "real" interrupts
 *             s390_start_IO, halt_IO()
 *                                  - status pending cond. after SSCH, or HSCH
 *             cancel_IO(), clear_IO(), resume_IO()
 *             s390_set_isc5()
 *             s390_validate_subchannel(),
 *             {en,dis}able_subchannel() - status pending conditions (after MSCH)
 *
 * Returns: 0 - no ending status received, no further action taken
 *          1 - interrupt handler was called with ending status
 */
int
s390_process_IRQ (unsigned int irq)
{
	int ccode;		/* cond code from tsch() operation */
	int irb_cc;		/* cond code from irb */

	int issense = 0;
	devstat_t *dp;
	devstat_t *udp;
	scsw_t *scsw;

	char dbf_txt[15];

	if (cio_count_irqs) {
		int cpu = smp_processor_id ();
		s390_irq_count[cpu]++;
	}

	CIO_TRACE_EVENT (3, "procIRQ");
	sprintf (dbf_txt, "%x", irq);
	CIO_TRACE_EVENT (3, dbf_txt);

	if (ioinfo[irq] == INVALID_STORAGE_AREA) {
		static irb_t p_init_irb; /* FIXME: could this be on the stack? */
		/* we can't properly process the interrupt ... */

		CIO_DEBUG(KERN_CRIT, 0, 
			  "s390_process_IRQ(%04X) - got interrupt "
			  "for non-initialized subchannel!\n", irq);

		tsch (irq, &p_init_irb);
		return 1;

	}

	dp = &ioinfo[irq]->devstat;
	udp = ioinfo[irq]->irq_desc.dev_id;

	/*
	 * It might be possible that a device was not-oper. at the time
	 *  of free_irq() processing. This means the handler is no longer
	 *  available when the device possibly becomes ready again. In
	 *  this case we perform delayed disable_subchannel() processing.
	 */
	if ((!ioinfo[irq]->ui.flags.ready) 
	    &&  (!ioinfo[irq]->ui.flags.d_disable)) 

		CIO_DEBUG (KERN_CRIT, 0, 
			   "s390_process_IRQ(%04X) "
			   "- no interrupt handler registered "
			   "for device %04X !\n",
			   irq, ioinfo[irq]->devstat.devno);

	/*
	 * retrieve the i/o interrupt information (irb),
	 *  update the device specific status information
	 *  and possibly call the interrupt handler.
	 *
	 * Note 1: At this time we don't process the resulting
	 *         condition code (ccode) from tsch(), although
	 *         we probably should.
	 *
	 * Note 2: Here we will have to check for channel
	 *         check conditions and call a channel check
	 *         handler.
	 *
	 * Note 3: If a start function was issued, the interruption
	 *         parameter relates to it. If a halt function was
	 *         issued for an idle device, the intparm must not
	 *         be taken from lowcore, but from the devstat area.
	 */
	ccode = tsch (irq, &(dp->ii.irb));

	sprintf (dbf_txt, "ccode:%d", ccode);
	CIO_TRACE_EVENT (3, dbf_txt);

	if (ccode == 3) 
		CIO_DEBUG (KERN_WARNING, 0, 
			   "s390_process_IRQ(%04X) - subchannel "
			   "is not operational!\n",
			   irq);

	/*
	 * We must only accumulate the status if the device is busy already
	 */

	scsw = &dp->ii.irb.scsw;

	if (ioinfo[irq]->ui.flags.busy) {
		dp->dstat |= scsw->dstat;
		dp->cstat |= scsw->cstat;
		dp->intparm = ioinfo[irq]->u_intparm;

	} else {
		dp->dstat = scsw->dstat;
		dp->cstat = scsw->cstat;

		dp->flag = 0;	/* reset status flags */
		dp->intparm = 0;

	}

	dp->lpum = dp->ii.irb.esw.esw1.lpum;

	/*
	 * reset device-busy bit if no longer set in irb
	 */
	if ((dp->dstat & DEV_STAT_BUSY)
	    && ((scsw->dstat & DEV_STAT_BUSY) == 0)) 
		dp->dstat &= ~DEV_STAT_BUSY;

	/*
	 * Save residual count and CCW information in case primary and
	 *  secondary status are presented with different interrupts.
	 */
	if (scsw->stctl
	    & (SCSW_STCTL_PRIM_STATUS | SCSW_STCTL_INTER_STATUS)) {

		/*
		 * If the subchannel status shows status pending
		 * and we received a check condition, the count
		 * information is not meaningful.
		 */

		if (!((scsw->stctl & SCSW_STCTL_STATUS_PEND)
		      && (scsw->cstat
			  & (SCHN_STAT_CHN_DATA_CHK
			     | SCHN_STAT_CHN_CTRL_CHK
			     | SCHN_STAT_INTF_CTRL_CHK
			     | SCHN_STAT_PROG_CHECK
			     | SCHN_STAT_PROT_CHECK
			     | SCHN_STAT_CHAIN_CHECK)))) {

			dp->rescnt = scsw->count;
		} else {
			dp->rescnt = SENSE_MAX_COUNT;
		}

		dp->cpa = scsw->cpa;
		
	}
	irb_cc = scsw->cc;

	/*
	 * check for any kind of channel or interface control check but don't
	 * issue the message for the console device
	 */
	if ((scsw->cstat
	     & (SCHN_STAT_CHN_DATA_CHK
		| SCHN_STAT_CHN_CTRL_CHK | SCHN_STAT_INTF_CTRL_CHK))) {

		CIO_DEBUG_NOCONS (irq, KERN_WARNING, printk, 0,
				  "Channel-Check or Interface-Control-Check "
				  "received\n"
				  " ... device %04X on subchannel %04X, dev_stat "
				  ": %02X sch_stat : %02X\n",
				  ioinfo[irq]->devstat.devno, irq, dp->dstat,
				  dp->cstat);

		if (irb_cc != 3) {

			CIO_DEBUG_NOCONS(irq, KERN_WARNING, printk, 0, 
					 "s390_process_IRQ(%04X) - irb for "
					 "device %04X after channel check "
					 "or interface control check:\n",
					 irq, dp->devno);
			
			s390_displayhex (&(dp->ii.irb), 
					 sizeof (irb_t), 0);
		}
		
	}

	if (scsw->ectl == 0) {
		issense = 0;
	} else if ((scsw->stctl == SCSW_STCTL_STATUS_PEND)
		   && (scsw->eswf == 0)) {
		issense = 0;
	} else if ((scsw->stctl ==
		    (SCSW_STCTL_STATUS_PEND | SCSW_STCTL_INTER_STATUS))
		   && ((scsw->actl & SCSW_ACTL_SUSPENDED) == 0)) {
		issense = 0;
	} else {
		issense = dp->ii.irb.esw.esw0.erw.cons;

	}

	if (issense) {
		dp->scnt = dp->ii.irb.esw.esw0.erw.scnt;
		dp->flag |= DEVSTAT_FLAG_SENSE_AVAIL;

		CIO_DEBUG_NOCONS (irq, KERN_DEBUG, DBG, 4,
				  "s390_process_IRQ( %04X ): "
				  "concurrent sense bytes avail %d\n",
				  irq, dp->scnt);
	}

	switch (irb_cc) {
	case 1:		/* status pending */
		dp->flag |= DEVSTAT_STATUS_PENDING;

	case 0:		/* normal i/o interruption */
		return s390_process_IRQ_normal(irq);

	default:	/* device/path not operational */
		return s390_process_IRQ_notoper(irq);
	}
}

/*
 * Set the special i/o-interruption subclass 7 for the
 *  device specified by parameter irq. There can only
 *  be a single device been operated on this special
 *  isc. This function is aimed being able to check
 *  on special device interrupts in disabled state,
 *  without having to delay I/O processing (by queueing)
 *  for non-console devices.
 *
 * Setting of this isc is done by set_cons_dev(), while
 *  wait_cons_dev() allows to actively wait on an interrupt
 *  for this device in disabed state. When the interrupt 
 *  condition is encountered, wait_cons_dev() calls do_IRQ()
 *  to have the console device driver processing the
 *  interrupt.
 */
int
set_cons_dev (int irq)
{
	int ccode;
	int rc = 0;
	char dbf_txt[15];

	if (cons_dev != -1)
		return -EBUSY;

	sprintf (dbf_txt, "scons%x", irq);
	CIO_TRACE_EVENT (4, dbf_txt);

	/*
	 * modify the indicated console device to operate
	 *  on special console interrupt subclass 7
	 */
	ccode = stsch (irq, &(ioinfo[irq]->schib));

	if (ccode) {
		rc = -ENODEV;
		ioinfo[irq]->devstat.flag |= DEVSTAT_NOT_OPER;
	} else {
		ioinfo[irq]->schib.pmcw.isc = 7;

		ccode = msch (irq, &(ioinfo[irq]->schib));

		if (ccode) {
			rc = -EIO;
		} else {
			cons_dev = irq;

			/*
			 * enable console I/O-interrupt subclass 7
			 */
			ctl_set_bit(6, 24);

		}
	}

	return rc;
}

int
wait_cons_dev (int irq)
{
	int rc = 0;
	long save_cr6;
	char dbf_txt[15];

	if (irq != cons_dev)
		return -EINVAL;

	sprintf (dbf_txt, "wcons%x", irq);
	CIO_TRACE_EVENT (4, dbf_txt);

	/*
	 * before entering the spinlock we may already have
	 *  processed the interrupt on a different CPU ...
	 */
	if (ioinfo[irq]->ui.flags.busy == 1) {
		long cr6 __attribute__ ((aligned (8)));

		/*
		 * disable all, but isc 7 (console device)
		 */
		__ctl_store (cr6, 6, 6);
		save_cr6 = cr6;
		cr6 = 0x01000000;
		__ctl_load (cr6, 6, 6);

		do {
			tpi_info_t tpi_info = { 0, };
			if (tpi (&tpi_info) == 1) {
				s390_process_IRQ (tpi_info.irq);
			} else {
				s390irq_spin_unlock (irq);
				udelay (100);
				s390irq_spin_lock (irq);
			}
			eieio ();
		} while (ioinfo[irq]->ui.flags.busy == 1);

		/*
		 * restore previous isc value
		 */
		cr6 = save_cr6;
		__ctl_load (cr6, 6, 6);

	}

	return rc;
}

/*
 * used by {en,dis}able_cpu_sync_isc
 */
static inline int
s390_set_isc5(int irq, int reset)
{
	int ccode;
	int rc = 0;
	int retry1 = 5;
	int retry2 = 5;
	int clear_pend = 0;
	long cr6 __attribute__ ((aligned (8)));

	do {
		if (reset)
			retry2 = 5;
		else
			retry2 = 3;
		do {
			ccode = msch (irq, &(ioinfo[irq]->schib));

			switch (ccode) {
			case 0:
				/*
				 * disable special interrupt subclass in CPU
				 */
				__ctl_store (cr6, 6, 6);
				if (reset) {
					/* disable sync isc 5 */
					cr6 &= 0xFBFFFFFF;
					/* enable standard isc 3 */
					cr6 |= 0x10000000;
					/* enable console isc 7, if neccessary */
					if (cons_dev != -1)
						cr6 |= 0x01000000;
				} else {
					/* enable sync isc 5 */
					cr6 |= 0x04000000;
					/* disable standard isc 3 */
					cr6 &= 0xEFFFFFFF;
					/* disable console isc 7, if neccessary */
					if (cons_dev != -1)
						cr6 &= 0xFEFFFFFF;
				}
				__ctl_load (cr6, 6, 6);
				
				retry2 = 0;
				rc = 0;
				break;
				
			case 1:	/* status pending */
				ioinfo[irq]->ui.flags.s_pend = 1;
				s390_process_IRQ (irq);
				ioinfo[irq]->ui.flags.s_pend = 0;

				retry2--;
				rc = -EIO;
				break;
				
			case 2:	/* busy */
				if (reset) {
					retry2--;
					udelay (100);	/* give it time */
				} else {
					retry2 = 0;
				}
				rc = -EBUSY;
				break;
				
			default:	/* not oper */
				retry2 = 0;
				rc = -ENODEV;
				break;
			}
			
		} while (retry2);


		if ((rc == 0) || (rc == -ENODEV) || (!reset))
			retry1 = 0;
		else
			retry1--;
		
		if (retry1) {
			/* try stopping it ... */
			if (!clear_pend) {
				clear_IO (irq, 0x00004711, 0);
				clear_pend = 1;
			
			}
		
		udelay (100);

		}

		
	} while (retry1 && ccode);
	
	return rc;
}


int
enable_cpu_sync_isc (int irq)
{
	int ccode;
	int rc = 0;
	char dbf_txt[15];

	sprintf (dbf_txt, "enisc%x", irq);
	CIO_TRACE_EVENT (4, dbf_txt);

	/* This one spins until it can get the sync_isc lock for irq# irq */

	if (atomic_read (&sync_isc) != irq)
		atomic_compare_and_swap_spin (-1, irq, &sync_isc);
	
	sync_isc_cnt++;
	
	if (sync_isc_cnt > 255) {	/* fixme : magic number */
		panic ("Too many recursive calls to enable_sync_isc");
		
	}
	/*
	 * we only run the STSCH/MSCH path for the first enablement
	 */
	else if (sync_isc_cnt == 1) {
		ioinfo[irq]->ui.flags.syncio = 1;
		
		ccode = stsch (irq, &(ioinfo[irq]->schib));
		
		if (!ccode) {
			ioinfo[irq]->schib.pmcw.isc = 5;
			rc = s390_set_isc5(irq, 0);
			
		} else {
			rc = -ENODEV;	/* device is not-operational */
			
		}
	}
	
	if (rc) {	/* can only happen if stsch/msch fails */
		sync_isc_cnt = 0;
		atomic_set (&sync_isc, -1);
	}

	return rc;
}


int
disable_cpu_sync_isc (int irq)
{
	int rc = 0;
	int ccode;

	char dbf_txt[15];

	sprintf (dbf_txt, "disisc%x", irq);
	CIO_TRACE_EVENT (4, dbf_txt);

	/*
	 * We disable if we're the top user only, as we may
	 *  run recursively ... 
	 * We must not decrease the count immediately; during
	 *  msch() processing we may face another pending
	 *  status we have to process recursively (sync).
	 */
	
	if (sync_isc_cnt == 1) {
		ccode = stsch (irq, &(ioinfo[irq]->schib));
		
		if (!ccode) {
			
			ioinfo[irq]->schib.pmcw.isc = 3;
			rc = s390_set_isc5(irq, 1);
		} else {
			rc = -ENODEV;
		}
		
		ioinfo[irq]->ui.flags.syncio = 0;
		
		sync_isc_cnt = 0;
		atomic_set (&sync_isc, -1);
		
	} else {
		sync_isc_cnt--;
		
	}

	return rc;
}

EXPORT_SYMBOL (halt_IO);
EXPORT_SYMBOL (clear_IO);
EXPORT_SYMBOL (do_IO);
EXPORT_SYMBOL (resume_IO);
