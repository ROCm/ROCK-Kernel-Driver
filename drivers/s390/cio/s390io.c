/*
 *  drivers/s390/cio/s390io.c
 *   S/390 common I/O routines
 *   $Revision: 1.11 $
 *
 *  S390 version
 *    Copyright (C) 1999, 2000 IBM Deutschland Entwicklung GmbH,
 *                             IBM Corporation
 *    Author(s): Ingo Adlung (adlung@de.ibm.com)
 *               Cornelia Huck (cohuck@de.ibm.com) 
 *               Arnd Bergmann (arndb@de.ibm.com)
 *    ChangeLog: 01/07/2001 Blacklist cleanup (djbarrow@de.ibm.com,barrow_dj@yahoo.com)
 *               01/04/2001 Holger Smolinski (smolinsk@de.ibm.com)
 *                          Fixed lost interrupts and do_adapter_IO
 *               xx/xx/xxxx nnn          multiple changes not reflected
 *               03/12/2001 Ingo Adlung  blacklist= - changed to cio_ignore=  
 *               03/14/2001 Ingo Adlung  disable interrupts before start_IO
 *                                        in Path Group processing 
 *                                       decrease retry2 on busy while 
 *                                        disabling sync_isc; reset isc_cnt
 *                                        on io error during sync_isc enablement
 *               05/09/2001 Cornelia Huck added exploitation of debug feature
 *               05/16/2001 Cornelia Huck added /proc/deviceinfo/<devno>/
 *               05/22/2001 Cornelia Huck added /proc/cio_ignore
 *                                        un-ignore blacklisted devices by piping 
 *                                        to /proc/cio_ignore
 *               xx/xx/xxxx some bugfixes & cleanups
 *               08/02/2001 Cornelia Huck not already known devices can be blacklisted
 *                                        by piping to /proc/cio_ignore
 *               09/xx/2001 couple more fixes
 *               10/15/2001 Cornelia Huck xsch - internal only for now
 *               10/29/2001 Cornelia Huck Blacklisting reworked again
 *               10/29/2001 Cornelia Huck improved utilization of debug feature
 *               10/29/2001 Cornelia Huck more work on cancel_IO - use the flag
 *                                        DOIO_CANCEL_ON_TIMEOUT in do_IO to get
 *                                        io cancelled
 *               11/15/2001 Cornelia Huck proper behaviour with procfs off
 *               12/10/2001 Cornelia Huck added private_data + functions to 
 *                                        ioinfo_t
 *               11-12/2001 Cornelia Huck various cleanups
 *               01/09/2002 Cornelia Huck PGID fixes
 *                                        process css machine checks 
 *               01/10/2002 Cornelia Huck added /proc/chpids
 * 		 02/11/2002 Arnd Bergmann split this file into many smaller pieces 
 * 		 			  in drivers/s39/cio/
 *               05/03/2002 Cornelia Huck debug cleanup
 */
#include <linux/module.h>
#include <linux/config.h>
#include <linux/init.h>
#include <linux/bootmem.h>
#include <linux/proc_fs.h>
#include <asm/delay.h>
#include <asm/idals.h>
#include <asm/cpcmd.h>

#include <asm/irq.h>
#include <asm/s390io.h>
#include <asm/s390dyn.h>
#include <asm/s390mach.h>
#include <asm/debug.h>

#include "s390io.h"
#include "ioinfo.h"
#include "cio.h"
#include "cio_debug.h"
#include "blacklist.h"
#include "proc.h"
#include "chsc.h"

static int init_IRQ_complete; 

static pgid_t global_pgid;
static schib_t p_init_schib;
static __u64 irq_IPL_TOD; // FIXME: can be on stack

static void s390_process_subchannels (void);
static void s390_device_recognition_all (void);
static int s390_SenseID (int irq, senseid_t * sid, __u8 lpm);
static int s390_SetPGID (int irq, __u8 lpm, pgid_t * pgid);
static int s390_SensePGID (int irq, __u8 lpm, pgid_t * pgid);

/* FIXME: intermixed with proc.c */
int cio_count_irqs = 1;			/* toggle use here... */
unsigned long s390_irq_count[NR_CPUS];	/* trace how many irqs have occured per cpu... */

int cio_show_msg;
static int cio_notoper_msg = 1;
static int cio_sid_with_pgid;     /* if we need a PGID for SenseID, switch this on */

static int __init
cio_setup (char *parm)
{
	if (!strcmp (parm, "yes")) {
		cio_show_msg = 1;
	} else if (!strcmp (parm, "no")) {
		cio_show_msg = 0;
	} else {
		printk (KERN_ERR "cio_setup : invalid cio_msg parameter '%s'",
			parm);

	}

	return 1;
}

__setup ("cio_msg=", cio_setup);


static int __init
cio_notoper_setup (char *parm)
{
	if (!strcmp (parm, "yes")) {
		cio_notoper_msg = 1;
	} else if (!strcmp (parm, "no")) {
		cio_notoper_msg = 0;
	} else {
		printk (KERN_ERR
			"cio_notoper_setup: "
			"invalid cio_notoper_msg parameter '%s'", parm);
	}

	return 1;
}

__setup ("cio_notoper_msg=", cio_notoper_setup);

static int __init
cio_pgid_setup (char *parm)
{
	if (!strcmp (parm, "yes")) {
		cio_sid_with_pgid = 1;
	} else if (!strcmp (parm, "no")) {
		cio_sid_with_pgid = 0;
	} else {
		printk (KERN_ERR 
			"cio_pgid_setup : invalid cio_msg parameter '%s'",
			parm);

	}

	return 1;
}

__setup ("cio_sid_with_pgid=", cio_pgid_setup);

/* This function is replacing the init_IRQ function in
 * arch/s390(x)/kernel/irq.c and is called early during
 * bootup. Anything called from here must be careful 
 * about memory allocations */
void __init
s390_init_IRQ (void)
{
	unsigned long flags;	/* PSW flags */
	long cr6 __attribute__ ((aligned (8)));

	asm volatile ("STCK %0":"=m" (irq_IPL_TOD));


	/*
	 * As we don't know about the calling environment
	 *  we assure running disabled. Before leaving the
	 *  function we resestablish the old environment.
	 *
	 * Note : as we don't need a system wide lock, therefore
	 *        we shouldn't use cli(), but local_irq_save() as this
	 *        affects the current CPU only.
	 */
	local_irq_save(flags);

	/*
	 * disable all interrupts
	 */
	cr6 = 0;
	__ctl_load (cr6, 6, 6);

	s390_process_subchannels ();

	if (cio_count_irqs) {
		int i;
		for (i = 0; i < NR_CPUS; i++)
			s390_irq_count[i] = 0;
	}

	
	/*
	 * Let's build our path group ID here.
	 */
	
	global_pgid.cpu_addr = *(__u16 *) __LC_CPUADDR;
	global_pgid.cpu_id = ((cpuid_t *) __LC_CPUID)->ident;
	global_pgid.cpu_model = ((cpuid_t *) __LC_CPUID)->machine;
	global_pgid.tod_high = *(__u32 *) & irq_IPL_TOD;


	/*
	 * enable default I/O-interrupt subclass 3
	 */
	cr6 = 0x10000000;
	__ctl_load (cr6, 6, 6);

	s390_device_recognition_all ();

	init_IRQ_complete = 1;

	local_irq_restore (flags);

	return;
}

/*
 * dummy handler, used during init_IRQ() processing for compatibility only
 */
static void
init_IRQ_handler (int irq, void *dev_id, struct pt_regs *regs)
{
	/* this is a dummy handler only ... */
}


/*
 * Input :
 *   devno - device number
 *   ps    - pointer to sense ID data area
 * Output : none
 */
static inline void
_VM_virtual_device_info (__u16 devno, senseid_t * ps)
{
	diag210_t *p_diag_data;
	int ccode;

	int error = 0;

	CIO_TRACE_EVENT (4, "VMvdinf");

	if (init_IRQ_complete) {
		p_diag_data = kmalloc (sizeof (diag210_t), GFP_DMA);
	} else {
		p_diag_data = alloc_bootmem_low (sizeof (diag210_t));

	}

	p_diag_data->vrdcdvno = devno;
	p_diag_data->vrdclen = sizeof (diag210_t);
	ccode = diag210 ((diag210_t *) virt_to_phys (p_diag_data));
	ps->reserved = 0xff;

	switch (p_diag_data->vrdcvcla) {
	case 0x80:

		switch (p_diag_data->vrdcvtyp) {
		case 00:

			ps->cu_type = 0x3215;

			break;

		default:

			error = 1;

			break;

		}

		break;

	case 0x40:

		switch (p_diag_data->vrdcvtyp) {
		case 0xC0:

			ps->cu_type = 0x5080;

			break;

		case 0x80:

			ps->cu_type = 0x2250;

			break;

		case 0x04:

			ps->cu_type = 0x3277;

			break;

		case 0x01:

			ps->cu_type = 0x3278;

			break;

		default:

			error = 1;

			break;

		}

		break;

	case 0x20:

		switch (p_diag_data->vrdcvtyp) {
		case 0x84:

			ps->cu_type = 0x3505;

			break;

		case 0x82:

			ps->cu_type = 0x2540;

			break;

		case 0x81:

			ps->cu_type = 0x2501;

			break;

		default:

			error = 1;

			break;

		}

		break;

	case 0x10:

		switch (p_diag_data->vrdcvtyp) {
		case 0x84:

			ps->cu_type = 0x3525;

			break;

		case 0x82:

			ps->cu_type = 0x2540;

			break;

		case 0x4F:
		case 0x4E:
		case 0x48:

			ps->cu_type = 0x3820;

			break;

		case 0x4D:
		case 0x49:
		case 0x45:

			ps->cu_type = 0x3800;

			break;

		case 0x4B:

			ps->cu_type = 0x4248;

			break;

		case 0x4A:

			ps->cu_type = 0x4245;

			break;

		case 0x47:

			ps->cu_type = 0x3262;

			break;

		case 0x43:

			ps->cu_type = 0x3203;

			break;

		case 0x42:

			ps->cu_type = 0x3211;

			break;

		case 0x41:

			ps->cu_type = 0x1403;

			break;

		default:

			error = 1;

			break;

		}

		break;

	case 0x08:

		switch (p_diag_data->vrdcvtyp) {
		case 0x82:

			ps->cu_type = 0x3422;

			break;

		case 0x81:

			ps->cu_type = 0x3490;

			break;

		case 0x10:

			ps->cu_type = 0x3420;

			break;

		case 0x02:

			ps->cu_type = 0x3430;

			break;

		case 0x01:

			ps->cu_type = 0x3480;

			break;

		case 0x42:

			ps->cu_type = 0x3424;

			break;

		case 0x44:

			ps->cu_type = 0x9348;

			break;

		default:

			error = 1;

			break;

		}

		break;

	case 02:		/* special device class ... */

		switch (p_diag_data->vrdcvtyp) {
		case 0x20:	/* OSA */

			ps->cu_type = 0x3088;
			ps->cu_model = 0x60;

			break;

		default:

			error = 1;
			break;

		}

		break;

	default:

		error = 1;

		break;

	}

	if (init_IRQ_complete) {
		kfree (p_diag_data);
	} else {
		free_bootmem ((unsigned long) p_diag_data, sizeof (diag210_t));

	}

	if (error) 
		CIO_DEBUG_ALWAYS(KERN_ERR, 0,
				 "DIAG X'210' for "
				 "device %04X returned "
				 "(cc = %d): vdev class : %02X, "
				 "vdev type : %04X \n ...  "
				 "rdev class : %02X, rdev type : %04X, "
				 "rdev model: %02X\n",
				 devno,
				 ccode,
				 p_diag_data->vrdcvcla,
				 p_diag_data->vrdcvtyp,
				 p_diag_data->vrdcrccl,
				 p_diag_data->vrdccrty,
				 p_diag_data->vrdccrmd);
		
}

/*
 * This routine returns the characteristics for the device
 *  specified. Some old devices might not provide the necessary
 *  command code information during SenseID processing. In this
 *  case the function returns -EINVAL. Otherwise the function
 *  allocates a decice specific data buffer and provides the
 *  device characteristics together with the buffer size. Its
 *  the callers responability to release the kernel memory if
 *  not longer needed. In case of persistent I/O problems -EBUSY
 *  is returned.
 *
 *  The function may be called enabled or disabled. However, the
 *   caller must have locked the irq it is requesting data for.
 *
 * Note : It would have been nice to collect this information
 *         during init_IRQ() processing but this is not possible
 *
 *         a) without statically pre-allocation fixed size buffers
 *            as virtual memory management isn't available yet.
 *
 *         b) without unnecessarily increase system startup by
 *            evaluating devices eventually not used at all.
 */
int
read_dev_chars (int irq, void **buffer, int length)
{
	unsigned int flags;
	ccw1_t *rdc_ccw;
	devstat_t devstat;
	char *rdc_buf;
	int devflag = 0;

	int ret = 0;
	int emulated = 0;
	int retry = 5;

	char dbf_txt[15];

	if (!buffer || !length) {
		return (-EINVAL);

	}

	SANITY_CHECK (irq);

	if (ioinfo[irq]->ui.flags.oper == 0) {
		return (-ENODEV);

	}

	sprintf (dbf_txt, "rddevch%x", irq);
	CIO_TRACE_EVENT (4, dbf_txt);

	/*
	 * Before playing around with irq locks we should assure
	 *   running disabled on (just) our CPU. Sync. I/O requests
	 *   also require to run disabled.
	 *
	 * Note : as no global lock is required, we must not use
	 *        cli(), but local_irq_save() instead.   
	 */
	local_irq_save(flags);

	rdc_ccw = &ioinfo[irq]->senseccw;

	if (!ioinfo[irq]->ui.flags.ready) {
		ret = request_irq (irq,
				   init_IRQ_handler, SA_PROBE, "RDC", &devstat);

		if (!ret) {
			emulated = 1;

		}

	}

	if (!ret) {
		if (!*buffer) {
			rdc_buf = kmalloc (length, GFP_KERNEL);
		} else {
			rdc_buf = *buffer;

		}

		if (!rdc_buf) {
			ret = -ENOMEM;
		} else {
			do {
				rdc_ccw->cmd_code = CCW_CMD_RDC;
				rdc_ccw->count = length;
				rdc_ccw->flags = CCW_FLAG_SLI;
				ret = set_normalized_cda (rdc_ccw, rdc_buf);
				if (!ret) {

					memset (ioinfo[irq]->irq_desc.dev_id,
						'\0', sizeof (devstat_t));

					ret = s390_start_IO (irq, rdc_ccw, 0x00524443,	/* RDC */
							     0,	/* n/a */
							     DOIO_WAIT_FOR_INTERRUPT
							     |
							     DOIO_DONT_CALL_INTHDLR);
					retry--;
					devflag =
					    ioinfo[irq]->irq_desc.dev_id->flag;

					clear_normalized_cda (rdc_ccw);
				} else {
					udelay (100);	/* wait for recovery */
					retry--;
				}

			} while ((retry)
				 && (ret
				     || (devflag & DEVSTAT_STATUS_PENDING)));

		}

		if (!retry) {
			ret = (ret == -ENOMEM) ? -ENOMEM : -EBUSY;

		}

		local_irq_restore (flags);

		/*
		 * on success we update the user input parms
		 */
		if (!ret) {
			*buffer = rdc_buf;

		}

		if (emulated) {
			free_irq (irq, &devstat);

		}

	}

	return (ret);
}

/*
 *  Read Configuration data
 */
int
read_conf_data (int irq, void **buffer, int *length, __u8 lpm)
{
	unsigned long flags;
	int ciw_cnt;

	int found = 0;		/* RCD CIW found */
	int ret = 0;		/* return code */

	char dbf_txt[15];

	SANITY_CHECK (irq);

	if (!buffer || !length) {
		return (-EINVAL);
	} else if (ioinfo[irq]->ui.flags.oper == 0) {
		return (-ENODEV);
	} else if (ioinfo[irq]->ui.flags.esid == 0) {
		return (-EOPNOTSUPP);

	}

	sprintf (dbf_txt, "rdconf%x", irq);
	CIO_TRACE_EVENT (4, dbf_txt);

	/*
	 * scan for RCD command in extended SenseID data
	 */

	for (ciw_cnt = 0; (found == 0) && (ciw_cnt < 62); ciw_cnt++) {
		if (ioinfo[irq]->senseid.ciw[ciw_cnt].ct == CIW_TYPE_RCD) {
			/*
			 * paranoia check ...
			 */
			if (ioinfo[irq]->senseid.ciw[ciw_cnt].cmd != 0) {
				found = 1;

			}

			break;

		}
	}

	if (found) {
		devstat_t devstat;	/* inline device status area */
		devstat_t *pdevstat;
		int ioflags;

		ccw1_t *rcd_ccw = &ioinfo[irq]->senseccw;
		char *rcd_buf = NULL;
		int emulated = 0;	/* no i/O handler installed */
		int retry = 5;	/* retry count */

		local_irq_save(flags);

		if (!ioinfo[irq]->ui.flags.ready) {
			pdevstat = &devstat;
			ret = request_irq (irq,
					   init_IRQ_handler,
					   SA_PROBE, "RCD", pdevstat);

			if (!ret) {
				emulated = 1;

			}	/* endif */
		} else {
			pdevstat = ioinfo[irq]->irq_desc.dev_id;

		}		/* endif */

		if (!ret) {
			if (init_IRQ_complete) {
				rcd_buf =
				    kmalloc (ioinfo[irq]->senseid.ciw[ciw_cnt].
					     count, GFP_DMA);
			} else {
				rcd_buf =
				    alloc_bootmem_low (ioinfo[irq]->senseid.
						       ciw[ciw_cnt].count);

			}

			if (rcd_buf == NULL) {
				ret = -ENOMEM;

			}
			if (!ret) {
				memset (rcd_buf,
					'\0',
					ioinfo[irq]->senseid.ciw[ciw_cnt].
					count);

				do {
					rcd_ccw->cmd_code =
					    ioinfo[irq]->senseid.ciw[ciw_cnt].
					    cmd;
					rcd_ccw->cda =
					    (__u32) virt_to_phys (rcd_buf);
					rcd_ccw->count =
					    ioinfo[irq]->senseid.ciw[ciw_cnt].
					    count;
					rcd_ccw->flags = CCW_FLAG_SLI;

					memset (pdevstat, '\0',
						sizeof (devstat_t));

					if (lpm) {
						ioflags =
						    DOIO_WAIT_FOR_INTERRUPT |
						    DOIO_VALID_LPM |
						    DOIO_DONT_CALL_INTHDLR;
					} else {
						ioflags =
						    DOIO_WAIT_FOR_INTERRUPT |
						    DOIO_DONT_CALL_INTHDLR;

					}

					ret = s390_start_IO (irq, rcd_ccw, 0x00524344,	/* == RCD */
							     lpm, ioflags);
					switch (ret) {
					case 0:
					case -EIO:

						if (!
						    (pdevstat->
						     flag &
						     (DEVSTAT_STATUS_PENDING |
						      DEVSTAT_NOT_OPER |
						      DEVSTAT_FLAG_SENSE_AVAIL)))
						{
							retry = 0;	/* we got it ... */
						} else {
							retry--;	/* try again ... */

						}

						break;

					default:	/* -EBUSY, -ENODEV, ??? */
						retry = 0;

					}

				} while (retry);

			}

			local_irq_restore (flags);

		}

		/*
		 * on success we update the user input parms
		 */
		if (ret == 0) {
			*length = ioinfo[irq]->senseid.ciw[ciw_cnt].count;
			*buffer = rcd_buf;
		} else {
			if (rcd_buf != NULL) {
				if (init_IRQ_complete) {
					kfree (rcd_buf);
				} else {
					free_bootmem ((unsigned long) rcd_buf,
						      ioinfo[irq]->senseid.
						      ciw[ciw_cnt].count);

				}

			}

			*buffer = NULL;
			*length = 0;

		}

		if (emulated)
			free_irq (irq, pdevstat);
	} else {
		ret = -EOPNOTSUPP;

	}

	return (ret);

}

/*
 * s390_device_recognition_irq
 *
 * Used for individual device recognition. Issues the device
 *  independant SenseID command to obtain info the device type.
 *
 */
void
s390_device_recognition_irq (int irq)
{
	int ret;
	char dbf_txt[15];

	sprintf (dbf_txt, "devrec%x", irq);
	CIO_TRACE_EVENT (4, dbf_txt);

	/*
	 * We issue the SenseID command on I/O subchannels we think are
	 *  operational only.
	 */
	if ((ioinfo[irq] != INVALID_STORAGE_AREA)
	    && (!ioinfo[irq]->st)
	    && (ioinfo[irq]->schib.pmcw.st == 0)
	    && (ioinfo[irq]->ui.flags.oper == 1)) {
		int irq_ret;
		devstat_t devstat;

		irq_ret = request_irq (irq,
				       init_IRQ_handler,
				       SA_PROBE, "INIT", &devstat);

		if (!irq_ret) {
			ret = enable_cpu_sync_isc (irq);

			if (!ret) {
				ioinfo[irq]->ui.flags.unknown = 0;

				memset (&ioinfo[irq]->senseid, '\0',
					sizeof (senseid_t));

				if (cio_sid_with_pgid) {
					
					ret = s390_DevicePathVerification(irq,0);
					
					if (ret == -EOPNOTSUPP) 
						/* 
						 * Doesn't prevent us from proceeding
						 */
						ret = 0;
				}

				/*
				 * we'll fallthrough here if we don't want
				 * to do SPID before SID
				 */
				if (!ret) {
					s390_SenseID (irq, &ioinfo[irq]->senseid, 0xff);
				}
				disable_cpu_sync_isc (irq);

			}

			free_irq (irq, &devstat);

		}
	}
}

/*
 * s390_device_recognition_all
 *
 * Used for system wide device recognition.
 *
 */
static void __init
s390_device_recognition_all (void)
{
	int irq = 0;		/* let's start with subchannel 0 ... */

	do {
		s390_device_recognition_irq (irq);

		irq++;

	} while (irq <= highest_subchannel);
}

/*
 * s390_process_subchannels
 *
 * Determines all subchannels available to the system.
 * Only called early during startup by s390_init_IRQ
 */
static void __init
s390_process_subchannels (void)
{
	int ret;
	int irq = 0;		/* Evaluate all subchannels starting with 0 ... */

	do {
		ret = s390_validate_subchannel (irq, 0);

		if (ret != -ENXIO)
			irq++;

	} while ((ret != -ENXIO) && (irq < __MAX_SUBCHANNELS));

	highest_subchannel = (--irq);

	CIO_DEBUG_ALWAYS(KERN_INFO, 0,
			 "Highest subchannel number detected "
			 "(hex) : %04X\n", highest_subchannel);
}

/*
 * s390_validate_subchannel()
 *
 * Process the subchannel for the requested irq. 
 * Return codes:
 * * 0 for valid subchannels
 * * -ENXIO for non-defined subchannels 
 *   (indicates highest available subchannel + 1)
 * * -ENODEV for non-IO subchannels and not operational conditions
 * * -EIO and -EBUSY for error conditions during msch
 */
int
s390_validate_subchannel (int irq, int enable)
{

	int retry;		/* retry count for status pending conditions */
	int ccode;		/* condition code for stsch() only */
	int ccode2;		/* condition code for other I/O routines */
	schib_t *p_schib;
	int ret;
#ifdef CONFIG_CHSC
	int      chp = 0;
	int      mask;
#endif /* CONFIG_CHSC */

	char dbf_txt[15];

	sprintf (dbf_txt, "valsch%x", irq);
	CIO_TRACE_EVENT (4, dbf_txt);

	/*
	 * The first subchannel that is not-operational (ccode==3)
	 *  indicates that there aren't any more devices available.
	 */
	if ((init_IRQ_complete)
	    && (ioinfo[irq] != INVALID_STORAGE_AREA)) {
		p_schib = &ioinfo[irq]->schib;
	} else {
		p_schib = &p_init_schib;

	}

	/*
	 * If we knew the device before we assume the worst case ...    
	 */
	if (ioinfo[irq] != INVALID_STORAGE_AREA) {
		ioinfo[irq]->ui.flags.oper = 0;
		ioinfo[irq]->ui.flags.dval = 0;

	}

	ccode = stsch (irq, p_schib);

	if (ccode) {
		return -ENXIO;
	}
	/*
	 * ... just being curious we check for non I/O subchannels
	 */
	if (p_schib->pmcw.st) {

		CIO_DEBUG_IFMSG(KERN_INFO, 0,
				"Subchannel %04X reports "
				"non-I/O subchannel type %04X\n",
				irq, p_schib->pmcw.st);

		if (ioinfo[irq] != INVALID_STORAGE_AREA)
			ioinfo[irq]->ui.flags.oper = 0;

	}

	if ((!p_schib->pmcw.dnv) && (!p_schib->pmcw.st)) {
		return -ENODEV;
	}
	if (!p_schib->pmcw.st) {
		if (ioinfo[irq] == INVALID_STORAGE_AREA
		    && is_blacklisted (p_schib->pmcw.dev)) {
			/* 
			 * This device must not be known to Linux. So we simply say that 
			 * there is no device and return ENODEV.
			 */
			CIO_DEBUG(KERN_DEBUG, 0,
				  "Blacklisted device detected at devno %04X\n",
				      p_schib->pmcw.dev);
			return -ENODEV;
		}
	}
	
	if (ioinfo[irq] == INVALID_STORAGE_AREA) {
		if (!init_IRQ_complete) {
			ioinfo[irq] = (ioinfo_t *)
			    alloc_bootmem_low (sizeof (ioinfo_t));
		} else {
			ioinfo[irq] = (ioinfo_t *)
			    kmalloc (sizeof (ioinfo_t), GFP_DMA);

		}

		memset (ioinfo[irq], '\0', sizeof (ioinfo_t));
		memcpy (&ioinfo[irq]->schib, &p_init_schib, sizeof (schib_t));

		/*
		 * We have to insert the new ioinfo element
		 *  into the linked list, either at its head,
		 *  its tail or insert it.
		 */
		if (ioinfo_head == NULL) {	/* first element */
			ioinfo_head = ioinfo[irq];
			ioinfo_tail = ioinfo[irq];
		} else if (irq < ioinfo_head->irq) {	/* new head */
			ioinfo[irq]->next = ioinfo_head;
			ioinfo_head->prev = ioinfo[irq];
			ioinfo_head = ioinfo[irq];
		} else if (irq > ioinfo_tail->irq) {	/* new tail */
			ioinfo_tail->next = ioinfo[irq];
			ioinfo[irq]->prev = ioinfo_tail;
			ioinfo_tail = ioinfo[irq];
		} else {	/* insert element */

			ioinfo_t *pi = ioinfo_head;

			for (pi = ioinfo_head; pi != NULL; pi = pi->next) {

				if (irq < pi->next->irq) {
					ioinfo[irq]->next = pi->next;
					ioinfo[irq]->prev = pi;
					pi->next->prev = ioinfo[irq];
					pi->next = ioinfo[irq];
					break;

				}
			}
		}
	}

	/* initialize some values ... */
	ioinfo[irq]->irq = irq;
	ioinfo[irq]->st = ioinfo[irq]->schib.pmcw.st;
	if (ioinfo[irq]->st)
		return -ENODEV;

	ioinfo[irq]->ui.flags.pgid_supp = 1;

	ioinfo[irq]->opm = ioinfo[irq]->schib.pmcw.pim
	    & ioinfo[irq]->schib.pmcw.pam & ioinfo[irq]->schib.pmcw.pom;

#ifdef CONFIG_CHSC
	if (ioinfo[irq]->opm) {
		for (chp=0;chp<=7;chp++) {
			mask = 0x80 >> chp;
			if (ioinfo[irq]->opm & mask) {
				if (!chsc_chpid_logical (irq, chp)) {
					/* disable using this path */
					ioinfo[irq]->opm &= ~mask;
				}
			}
		}
	}
#endif /* CONFIG_CHSC */

	CIO_DEBUG_IFMSG(KERN_INFO, 0,
			"Detected device %04X "
			"on subchannel %04X"
			" - PIM = %02X, "
			"PAM = %02X, POM = %02X\n",
			ioinfo[irq]->schib.pmcw.dev, 
			irq,
			ioinfo[irq]->schib.pmcw.pim,
			ioinfo[irq]->schib.pmcw.pam, 
			ioinfo[irq]->schib.pmcw.pom);

	/*
	 * initialize ioinfo structure
	 */
	if (!ioinfo[irq]->ui.flags.ready) {
		ioinfo[irq]->nopfunc = NULL;
		ioinfo[irq]->ui.flags.busy = 0;
		ioinfo[irq]->ui.flags.dval = 1;
		ioinfo[irq]->devstat.intparm = 0;

	}
	ioinfo[irq]->devstat.devno = ioinfo[irq]->schib.pmcw.dev;
	ioinfo[irq]->devno = ioinfo[irq]->schib.pmcw.dev;

	/*
	 * We should have at least one CHPID ...
	 */
	if (ioinfo[irq]->opm) {
		/*
		 * We now have to initially ...
		 *  ... set "interruption subclass"
		 *  ... enable "concurrent sense"
		 *  ... enable "multipath mode" if more than one
		 *        CHPID is available. This is done regardless
		 *        whether multiple paths are available for us.
		 *
		 * Note : we don't enable the device here, this is temporarily
		 *        done during device sensing below.
		 */
		ioinfo[irq]->schib.pmcw.isc = 3;	/* could be smth. else */
		ioinfo[irq]->schib.pmcw.csense = 1;	/* concurrent sense */
		ioinfo[irq]->schib.pmcw.ena = enable;
		ioinfo[irq]->schib.pmcw.intparm = ioinfo[irq]->schib.pmcw.dev;

		if ((ioinfo[irq]->opm != 0x80)
		    && (ioinfo[irq]->opm != 0x40)
		    && (ioinfo[irq]->opm != 0x20)
		    && (ioinfo[irq]->opm != 0x10)
		    && (ioinfo[irq]->opm != 0x08)
		    && (ioinfo[irq]->opm != 0x04)
		    && (ioinfo[irq]->opm != 0x02)
		    && (ioinfo[irq]->opm != 0x01)) {
			ioinfo[irq]->schib.pmcw.mp = 1;	/* multipath mode */

		}

		retry = 5;

		do {
			ccode2 = msch_err (irq, &ioinfo[irq]->schib);

			switch (ccode2) {
			case 0:
				/*
				 * successful completion
				 *
				 * concurrent sense facility available
				 */
				ioinfo[irq]->ui.flags.oper = 1;
				ioinfo[irq]->ui.flags.consns = 1;
				ret = 0;
				break;

			case 1:
				/*
				 * status pending
				 *
				 * How can we have a pending status 
				 * as the device is disabled for 
				 * interrupts ?
				 * Anyway, process it ...
				 */
				ioinfo[irq]->ui.flags.s_pend = 1;
				s390_process_IRQ (irq);
				ioinfo[irq]->ui.flags.s_pend = 0;
				retry--;
				ret = -EIO;
				break;

			case 2:
				/*
				 * busy
				 *
				 * we mark it not-oper as we can't 
				 * properly operate it !
				 */
				ioinfo[irq]->ui.flags.oper = 0;
				udelay (100);	/* allow for recovery */
				retry--;
				ret = -EBUSY;
				break;

			case 3:	/* not operational */
				ioinfo[irq]->ui.flags.oper = 0;
				retry = 0;
				ret = -ENODEV;
				break;

			default:
#define PGMCHK_OPERAND_EXC      0x15

				if ((ccode2 & PGMCHK_OPERAND_EXC)
				    == PGMCHK_OPERAND_EXC) {
					/*
					 * re-issue the modify subchannel without trying to
					 *  enable the concurrent sense facility
					 */
					ioinfo[irq]->schib.pmcw.csense = 0;

					ccode2 =
					    msch_err (irq, &ioinfo[irq]->schib);

					if (ccode2 != 0) {
						CIO_DEBUG_ALWAYS(KERN_ERR, 0,
								 "msch() (2) failed"
								 " with CC=%X\n",
								 ccode2);
						ioinfo[irq]->ui.flags.oper = 0;
						ret = -EIO;
					} else {
						ioinfo[irq]->ui.flags.oper = 1;
						ioinfo[irq]->ui.
						    flags.consns = 0;
						ret = 0;

					}

				} else {
					CIO_DEBUG_ALWAYS(KERN_ERR, 0,
							 "msch() (1) failed with "
							 "CC = %X\n", ccode2);
					ioinfo[irq]->ui.flags.oper = 0;
					ret = -EIO;

				}

				retry = 0;
				break;

			}

		} while (ccode2 && retry);

		if ((ccode2 != 0) && (ccode2 != 3)
		    && (!retry)) {
			CIO_DEBUG_ALWAYS(KERN_ERR, 0,
					 " ... msch() retry count for "
					 "subchannel %04X exceeded, CC = %d\n",
					 irq, ccode2);

		}
	} else {
		/* no path available ... */
		ioinfo[irq]->ui.flags.oper = 0;
		ret = -ENODEV;

	}

	return (ret);
}

/*
 * s390_SenseID
 *
 * Try to obtain the 'control unit'/'device type' information
 *  associated with the subchannel.
 *
 * The function is primarily meant to be called without irq
 *  action handler in place. However, it also allows for
 *  use with an action handler in place. If there is already
 *  an action handler registered assure it can handle the
 *  s390_SenseID() related device interrupts - interruption
 *  parameter used is 0x00E2C9C4 ( SID ).
 */
static int
s390_SenseID (int irq, senseid_t * sid, __u8 lpm)
{
	ccw1_t *sense_ccw;	/* ccw area for SenseID command */
	senseid_t isid;		/* internal sid */
	devstat_t devstat;	/* required by request_irq() */
	__u8 pathmask;		/* calulate path mask */
	__u8 domask;		/* path mask to use */
	int inlreq;		/* inline request_irq() */
	int irq_ret;		/* return code */
	devstat_t *pdevstat;	/* ptr to devstat in use */
	int retry;		/* retry count */
	int io_retry;		/* retry indicator */

	senseid_t *psid = sid;	/* start with the external buffer */
	int sbuffer = 0;	/* switch SID data buffer */

	char dbf_txt[15];
	int i;
	int failure = 0;	/* nothing went wrong yet */

	SANITY_CHECK (irq);

	if (ioinfo[irq]->ui.flags.oper == 0) {
		return (-ENODEV);

	}

	sprintf (dbf_txt, "snsID%x", irq);
	CIO_TRACE_EVENT (4, dbf_txt);

	inlreq = 0;		/* to make the compiler quiet... */

	if (!ioinfo[irq]->ui.flags.ready) {

		pdevstat = &devstat;

		/*
		 * Perform SENSE ID command processing. We have to request device
		 *  ownership and provide a dummy I/O handler. We issue sync. I/O
		 *  requests and evaluate the devstat area on return therefore
		 *  we don't need a real I/O handler in place.
		 */
		irq_ret =
		    request_irq (irq, init_IRQ_handler, SA_PROBE, "SID",
				 &devstat);

		if (irq_ret == 0)
			inlreq = 1;
	} else {
		inlreq = 0;
		irq_ret = 0;
		pdevstat = ioinfo[irq]->irq_desc.dev_id;

	}

	if (irq_ret) {
		return irq_ret;
	}

	s390irq_spin_lock (irq);

	if (init_IRQ_complete) {
		sense_ccw = kmalloc (2 * sizeof (ccw1_t), GFP_DMA);
	} else {
		sense_ccw = alloc_bootmem_low (2 * sizeof (ccw1_t));

	}

	/* more than one path installed ? */
	if (ioinfo[irq]->schib.pmcw.pim != 0x80) {
		sense_ccw[0].cmd_code = CCW_CMD_SUSPEND_RECONN;
		sense_ccw[0].cda = 0;
		sense_ccw[0].count = 0;
		sense_ccw[0].flags = CCW_FLAG_SLI | CCW_FLAG_CC;

		sense_ccw[1].cmd_code = CCW_CMD_SENSE_ID;
		sense_ccw[1].cda = (__u32) virt_to_phys (sid);
		sense_ccw[1].count = sizeof (senseid_t);
		sense_ccw[1].flags = CCW_FLAG_SLI;
	} else {
		sense_ccw[0].cmd_code = CCW_CMD_SENSE_ID;
		sense_ccw[0].cda = (__u32) virt_to_phys (sid);
		sense_ccw[0].count = sizeof (senseid_t);
		sense_ccw[0].flags = CCW_FLAG_SLI;

	}

	for (i = 0; (i < 8); i++) {
		pathmask = 0x80 >> i;

		domask = ioinfo[irq]->opm & pathmask;

		if (lpm)
			domask &= lpm;

		if (!domask)
			continue;

		failure = 0;

		psid->reserved = 0;
		psid->cu_type = 0xFFFF;	/* initialize fields ... */
		psid->cu_model = 0;
		psid->dev_type = 0;
		psid->dev_model = 0;

		retry = 5;	/* retry count    */
		io_retry = 1;	/* enable retries */

		/*
		 * We now issue a SenseID request. In case of BUSY,
		 *  STATUS PENDING or non-CMD_REJECT error conditions
		 *  we run simple retries.
		 */
		do {
			memset (pdevstat, '\0', sizeof (devstat_t));

			irq_ret = s390_start_IO (irq, sense_ccw, 0x00E2C9C4,	/* == SID */
						 domask,
						 DOIO_WAIT_FOR_INTERRUPT
						 | DOIO_TIMEOUT
						 | DOIO_VALID_LPM
						 | DOIO_DONT_CALL_INTHDLR);

			if ((psid->cu_type != 0xFFFF)
			    && (psid->reserved == 0xFF)) {
				if (!sbuffer) {	/* switch buffers */
					/*
					 * we report back the
					 *  first hit only
					 */
					psid = &isid;

					if (ioinfo[irq]->schib.pmcw.pim != 0x80) {
						sense_ccw[1].cda = (__u32)
						    virt_to_phys (psid);
					} else {
						sense_ccw[0].cda = (__u32)
						    virt_to_phys (psid);

					}

					/*
					 * if just the very first
					 *  was requested to be
					 *  sensed disable further
					 *  scans.
					 */
					if (!lpm)
						lpm = domask;

					sbuffer = 1;

				}

				if (pdevstat->rescnt < (sizeof (senseid_t) - 8)) {
					ioinfo[irq]->ui.flags.esid = 1;

				}

				io_retry = 0;

				break;
			}

			failure = 1;

			if (pdevstat->flag & DEVSTAT_STATUS_PENDING) {

				CIO_DEBUG (KERN_DEBUG, 2,
					   "SenseID : device %04X on "
					   "Subchannel %04X "
					   "reports pending status, "
					   "retry : %d\n",
					   ioinfo
					   [irq]->schib.pmcw.dev, irq, retry);
			
			} else if (pdevstat->flag & DEVSTAT_FLAG_SENSE_AVAIL) {
				/*
				 * if the device doesn't support the SenseID
				 *  command further retries wouldn't help ...
				 */
				if (pdevstat->ii.sense.data[0]
				    & (SNS0_CMD_REJECT | SNS0_INTERVENTION_REQ)) {

					CIO_DEBUG(KERN_ERR, 2,
						  "SenseID : device %04X on "
						  "Subchannel %04X "
						  "reports cmd reject or "
						  "intervention required\n",
						  ioinfo[irq]->schib.pmcw.dev, 
						  irq);

					io_retry = 0;
				} else {

					CIO_DEBUG(KERN_WARNING, 2,
						  "SenseID : UC on "
						  "dev %04X, retry %d, "
						  "lpum %02X, "
						  "cnt %02d, sns :"
						  " %02X%02X%02X%02X "
						  "%02X%02X%02X%02X ...\n",
						  ioinfo[irq]->schib.pmcw.dev,
						  retry,
						  pdevstat->lpum,
						  pdevstat->scnt,
						  pdevstat->ii.sense.data[0],
						  pdevstat->ii.sense.data[1],
						  pdevstat->ii.sense.data[2],
						  pdevstat->ii.sense.data[3],
						  pdevstat->ii.sense.data[4],
						  pdevstat->ii.sense.data[5],
						  pdevstat->ii.sense.data[6],
						  pdevstat->ii.sense.data[7]);
					
				}

			} else if ((pdevstat->flag & DEVSTAT_NOT_OPER)
				   || (irq_ret == -ENODEV)) {

				CIO_DEBUG(KERN_ERR, 2,
					  "SenseID : path %02X for "
					  "device %04X on "
					  "subchannel %04X "
					  "is 'not operational'\n",
					  domask,
					  ioinfo[irq]->schib.pmcw.dev, irq);
				
				io_retry = 0;
				ioinfo[irq]->opm &= ~domask;

			} else {

				CIO_DEBUG(KERN_INFO, 2,
					  "SenseID : start_IO() for "
					  "device %04X on "
					  "subchannel %04X "
					  "returns %d, retry %d, "
					  "status %04X\n",
					  ioinfo[irq]->schib.pmcw.dev, irq,
					  irq_ret, retry, pdevstat->flag);

				if (irq_ret == -ETIMEDOUT) {
					int xret;

					/*
					 * Seems we need to cancel the first ssch sometimes...
					 * On the next try, the ssch will usually be fine.
					 */

					xret = cancel_IO (irq);

					if (!xret)
						CIO_MSG_EVENT(4,
							      "SenseID: sch canceled "
							      "successfully for irq %x\n",
							      irq);
				}

			}

			if (io_retry) {
				retry--;

				if (retry == 0) {
					io_retry = 0;

				}
			}

			if ((failure) && (io_retry)) {
				/* reset fields... */

				failure = 0;

				psid->reserved = 0;
				psid->cu_type = 0xFFFF;
				psid->cu_model = 0;
				psid->dev_type = 0;
				psid->dev_model = 0;
			}

		} while ((io_retry));

	}

	if (init_IRQ_complete) {
		kfree (sense_ccw);
	} else {
		free_bootmem ((unsigned long) sense_ccw, 2 * sizeof (ccw1_t));

	}

	s390irq_spin_unlock (irq);

	/*
	 * If we installed the irq action handler we have to
	 *  release it too.
	 */
	if (inlreq)
		free_irq (irq, pdevstat);

	/*
	 * if running under VM check there ... perhaps we should do
	 *  only if we suffered a command reject, but it doesn't harm
	 */
	if ((sid->cu_type == 0xFFFF)
	    && (MACHINE_IS_VM)) {
		_VM_virtual_device_info (ioinfo[irq]->schib.pmcw.dev, sid);
	}

	if (sid->cu_type == 0xFFFF) {
		/*
		 * SenseID CU-type of 0xffff indicates that no device
		 *  information could be retrieved (pre-init value).
		 *
		 * If we can't couldn't identify the device type we
		 *  consider the device "not operational".
		 */
		CIO_DEBUG(KERN_WARNING, 2,
			  "SenseID : unknown device %04X on subchannel %04X\n",
			  ioinfo[irq]->schib.pmcw.dev, irq);

		ioinfo[irq]->ui.flags.unknown = 1;

	}

	/*
	 * Issue device info message if unit was operational .
	 */
	if (!ioinfo[irq]->ui.flags.unknown) {
		if (sid->dev_type != 0) {

			CIO_DEBUG_IFMSG(KERN_INFO, 2,
					"SenseID : device %04X reports: "
					"CU  Type/Mod = %04X/%02X,"
					" Dev Type/Mod = %04X/%02X\n",
					ioinfo[irq]->schib.pmcw.dev,
					sid->cu_type,
					sid->cu_model,
					sid->dev_type,
					sid->dev_model);

		} else {

			CIO_DEBUG_IFMSG(KERN_INFO, 2,
					"SenseID : device %04X reports:"
					" Dev Type/Mod = %04X/%02X\n",
					ioinfo[irq]->schib.pmcw.dev,
					sid->cu_type,
					sid->cu_model);
		}

	}

	if (!ioinfo[irq]->ui.flags.unknown)
		irq_ret = 0;
	else
		irq_ret = -ENODEV;

	return (irq_ret);
}

/*
 * Device Path Verification
 *
 * Path verification is accomplished by checking which paths (CHPIDs) are
 *  available. Further, a path group ID is set, if possible in multipath
 *  mode, otherwise in single path mode.
 *
 * Note : This function must not be called during normal device recognition,
 *         but during device driver initiated request_irq() processing only.
 */
int
s390_DevicePathVerification (int irq, __u8 usermask)
{
	int ccode;
	__u8 pathmask;
	__u8 domask;
#ifdef CONFIG_CHSC
	int chp;
	int mask;
	int old_opm = 0;
#endif /* CONFIG_CHSC */

	int ret = 0;
	int i;
	pgid_t pgid;
	__u8 dev_path;
	int first = 1;

	char dbf_txt[15];

	sprintf (dbf_txt, "dpver%x", irq);
	CIO_TRACE_EVENT (4, dbf_txt);

	if (ioinfo[irq]->st) 
		return -ENODEV;

#ifdef CONFIG_CHSC
	old_opm = ioinfo[irq]->opm;
#endif /* CONFIG_CHSC */
	ccode = stsch (irq, &(ioinfo[irq]->schib));

	if (ccode) {
		return -ENODEV;
	}
	if (ioinfo[irq]->schib.pmcw.pim == 0x80) {
		/*
		 * no error, just not required for single path only devices
		 */
		ioinfo[irq]->ui.flags.pgid_supp = 0;
		ret = 0;

#ifdef CONFIG_CHSC
		/*
		 * disable if chpid is logically offline
		 */
		if (!chsc_chpid_logical(irq, 0)) {
			not_oper_handler_func_t nopfunc=ioinfo[irq]->nopfunc;
			int was_oper = ioinfo[irq]->ui.flags.oper;

			ioinfo[irq]->opm = 0;
			ioinfo[irq]->ui.flags.oper = 0;
			CIO_DEBUG_ALWAYS(KERN_WARNING, 0,
					 "No logical path for sch %d...\n",
					 irq);
			if (old_opm && 
			    was_oper && 
			    ioinfo[irq]->ui.flags.ready) {

				free_irq( irq, ioinfo[irq]->irq_desc.dev_id);
				if (nopfunc)
					nopfunc( irq, DEVSTAT_DEVICE_GONE);
			}
			ret = -ENODEV;
		} else if (!old_opm) {

			/*
			 * check for opm...
			 */
			ioinfo[irq]->opm = ioinfo[irq]->schib.pmcw.pim
				& ioinfo[irq]->schib.pmcw.pam
				& ioinfo[irq]->schib.pmcw.pom;
				
			if (ioinfo[irq]->opm) {
				devreg_t *pdevreg;

				ioinfo[irq]->ui.flags.oper = 1;
				pdevreg = s390_search_devreg( ioinfo[irq] );

				if (pdevreg) 
					if (pdevreg->oper_func)
						pdevreg->oper_func
							( irq, pdevreg);
			}
			ret = 0;
		} else {
			ret = 0;
		}
#endif /* CONFIG_CHSC */
		return ret;
	}

	ioinfo[irq]->opm = ioinfo[irq]->schib.pmcw.pim
	    & ioinfo[irq]->schib.pmcw.pam & ioinfo[irq]->schib.pmcw.pom;

#ifdef CONFIG_CHSC
	if (ioinfo[irq]->opm) {
		for (chp=0;chp<=7;chp++) {
			mask = 0x80 >> chp;
			if (ioinfo[irq]->opm & mask) {
				if (!chsc_chpid_logical (irq, chp)) {
					/* disable using this path */
					ioinfo[irq]->opm &= ~mask;
				}
			}
		}
	}
	
	if ((ioinfo[irq]->opm == 0) && (old_opm)) {
		not_oper_handler_func_t nopfunc=ioinfo[irq]->nopfunc;
		int was_oper = ioinfo[irq]->ui.flags.ready;

		ioinfo[irq]->ui.flags.oper = 0;
		CIO_DEBUG_ALWAYS(KERN_WARNING, 0, 
				 "No logical path for sch %d...\n",
				 irq);
		if (was_oper && ioinfo[irq]->ui.flags.oper) {

				free_irq( irq, ioinfo[irq]->irq_desc.dev_id);
				if (nopfunc)
					nopfunc( irq, DEVSTAT_DEVICE_GONE);
		}
		return -ENODEV;
	}

	if (!old_opm) {
		/* Hey, we have a new logical path... */
		devreg_t *pdevreg;
		
		ioinfo[irq]->ui.flags.oper = 1;
		pdevreg = s390_search_devreg( ioinfo[irq] );
		
		if (pdevreg) 
			if (pdevreg->oper_func)
				pdevreg->oper_func( irq, pdevreg);

	}
#endif /* CONFIG_CHSC */

	if ( ioinfo[irq]->ui.flags.pgid_supp == 0 )
		return( 0);	/* just exit ... */

	if (usermask) {
		dev_path = usermask;
	} else {
		dev_path = ioinfo[irq]->opm;

	}

	if (ioinfo[irq]->ui.flags.pgid == 0) {
		memcpy (&ioinfo[irq]->pgid, &global_pgid, sizeof (pgid_t));
		ioinfo[irq]->ui.flags.pgid = 1;
	}
	memcpy (&pgid, &ioinfo[irq]->pgid, sizeof (pgid_t));

	for (i = 0; i < 8 && !ret; i++) {
		pathmask = 0x80 >> i;

		domask = dev_path & pathmask;

		if (domask) {
			ret = s390_SetPGID (irq, domask, &pgid);

			/*
			 * For the *first* path we are prepared
			 *  for recovery
			 *
			 *  - If we fail setting the PGID we assume its
			 *     using  a different PGID already (VM) we
			 *     try to sense.
			 */
			if (ret == -EOPNOTSUPP && first) {
				*(int *) &pgid = 0;

				ret = s390_SensePGID (irq, domask, &pgid);
				first = 0;

				if (ret == 0) {
					/*
					 * Check whether we retrieved
					 *  a reasonable PGID ...
					 */
					if (pgid.inf.ps.state1 ==
					    SNID_STATE1_GROUPED) {
						memcpy (&ioinfo[irq]->pgid,
							&pgid, sizeof (pgid_t));
					} else {	/* ungrouped or garbage ... */
						ret = -EOPNOTSUPP;

					}
				} else {
					ioinfo[irq]->ui.flags.pgid_supp = 0;

					CIO_DEBUG(KERN_WARNING, 2,
						  "PathVerification(%04X) "
						  "- Device %04X doesn't "
						  " support path grouping\n",
						  irq,
						  ioinfo[irq]->schib.pmcw.dev);
					
				}
			} else if (ret == -EIO) {

				CIO_DEBUG(KERN_ERR, 2,
					  "PathVerification(%04X) - I/O error "
					  "on device %04X\n", irq,
					  ioinfo[irq]->schib.pmcw.dev);
				
				ioinfo[irq]->ui.flags.pgid_supp = 0;

			} else if (ret == -ETIMEDOUT) {

				CIO_DEBUG(KERN_ERR, 2,
					  "PathVerification(%04X) - I/O timed "
					  "out on device %04X\n", irq,
					  ioinfo[irq]->schib.pmcw.dev);
				
				ioinfo[irq]->ui.flags.pgid_supp = 0;

			} else if (ret == -EAGAIN) {

				ret = 0;

			} else {

				CIO_DEBUG(KERN_ERR, 2,
					  "PathVerification(%04X) - "
					  "Unexpected error on device %04X\n",
					  irq, ioinfo[irq]->schib.pmcw.dev);
				
				ioinfo[irq]->ui.flags.pgid_supp = 0;
			}
		}
	}
	return ret;

}

/*
 * s390_SetPGID
 *
 * Set Path Group ID
 *
 */
static int
s390_SetPGID (int irq, __u8 lpm, pgid_t * pgid)
{
	ccw1_t *spid_ccw;	/* ccw area for SPID command */
	devstat_t devstat;	/* required by request_irq() */
	devstat_t *pdevstat = &devstat;
	unsigned long flags;
	char dbf_txt[15];

	int irq_ret = 0;	/* return code */
	int retry = 5;		/* retry count */
	int inlreq = 0;		/* inline request_irq() */
	int mpath = 1;		/* try multi-path first */

	SANITY_CHECK (irq);

	if (ioinfo[irq]->ui.flags.oper == 0) {
		return (-ENODEV);

	}

	sprintf (dbf_txt, "SPID%x", irq);
	CIO_TRACE_EVENT (4, dbf_txt);

	if (!ioinfo[irq]->ui.flags.ready) {
		/*
		 * Perform SetPGID command processing. We have to request device
		 *  ownership and provide a dummy I/O handler. We issue sync. I/O
		 *  requests and evaluate the devstat area on return therefore
		 *  we don't need a real I/O handler in place.
		 */
		irq_ret = request_irq (irq,
				       init_IRQ_handler,
				       SA_PROBE, "SPID", pdevstat);

		if (irq_ret == 0)
			inlreq = 1;
	} else {
		pdevstat = ioinfo[irq]->irq_desc.dev_id;

	}

	if (irq_ret) {
		return irq_ret;
	}

	s390irq_spin_lock_irqsave (irq, flags);

	if (init_IRQ_complete) {
		spid_ccw = kmalloc (2 * sizeof (ccw1_t), GFP_DMA);
	} else {
		spid_ccw = alloc_bootmem_low (2 * sizeof (ccw1_t));

	}

	spid_ccw[0].cmd_code = CCW_CMD_SUSPEND_RECONN;
	spid_ccw[0].cda = 0;
	spid_ccw[0].count = 0;
	spid_ccw[0].flags = CCW_FLAG_SLI | CCW_FLAG_CC;

	spid_ccw[1].cmd_code = CCW_CMD_SET_PGID;
	spid_ccw[1].cda = (__u32) virt_to_phys (pgid);
	spid_ccw[1].count = sizeof (pgid_t);
	spid_ccw[1].flags = CCW_FLAG_SLI;

	pgid->inf.fc = SPID_FUNC_MULTI_PATH | SPID_FUNC_ESTABLISH;

	/*
	 * We now issue a SetPGID request. In case of BUSY
	 *  or STATUS PENDING conditions we retry 5 times.
	 */
	do {
		memset (pdevstat, '\0', sizeof (devstat_t));

		irq_ret = s390_start_IO (irq, spid_ccw, 0xE2D7C9C4,	/* == SPID */
					 lpm,	/* n/a */
					 DOIO_WAIT_FOR_INTERRUPT
					 | DOIO_VALID_LPM
					 | DOIO_DONT_CALL_INTHDLR
					 | DOIO_TIMEOUT);

		if (!irq_ret) {
			if (pdevstat->flag & DEVSTAT_STATUS_PENDING) {

				CIO_DEBUG(KERN_DEBUG, 2,
					  "SPID - Device %04X "
					  "on Subchannel %04X "
					  "reports pending status, "
					  "retry : %d\n",
					  ioinfo[irq]->schib.pmcw.dev, 
					  irq, retry);

				retry--;
				irq_ret = -EIO;
			}

			if (pdevstat->flag == (DEVSTAT_START_FUNCTION
					       | DEVSTAT_FINAL_STATUS)) {
				retry = 0;	/* successfully set ... */
				irq_ret = 0;
			} else if (pdevstat->flag & DEVSTAT_FLAG_SENSE_AVAIL) {
				/*
				 * If the device doesn't support the
				 *  Sense Path Group ID command
				 *  further retries wouldn't help ...
				 */
				if (pdevstat->ii.sense.
				    data[0] & SNS0_CMD_REJECT) {
					if (mpath) {
						/*
						 * We now try single path mode.
						 * Note we must not issue the suspend
						 * multipath reconnect, or we will get
						 * a command reject by tapes.
						 */

						spid_ccw[0].cmd_code =
						    CCW_CMD_SET_PGID;
						spid_ccw[0].cda = (__u32)
						    virt_to_phys (pgid);
						spid_ccw[0].count =
						    sizeof (pgid_t);
						spid_ccw[0].flags =
						    CCW_FLAG_SLI;

						pgid->inf.fc =
						    SPID_FUNC_SINGLE_PATH
						    | SPID_FUNC_ESTABLISH;
						mpath = 0;
						retry--;
						irq_ret = -EIO;
					} else {
						irq_ret = -EOPNOTSUPP;
						retry = 0;

					}
				} else {

					CIO_DEBUG(KERN_WARNING, 2,
						  "SPID - device %04X,"
						  " unit check,"
						  " retry %d, cnt %02d,"
						  " sns :"
						  " %02X%02X%02X%02X "
						  "%02X%02X%02X%02X ...\n",
						  ioinfo[irq]->schib.pmcw.dev, 
						  retry,
						  pdevstat->scnt,
						  pdevstat->ii.sense.data[0],
						  pdevstat->ii.sense.data[1],
						  pdevstat->ii.sense.data[2],
						  pdevstat->ii.sense.data[3],
						  pdevstat->ii.sense.data[4],
						  pdevstat->ii.sense.data[5],
						  pdevstat->ii.sense.data[6],
						  pdevstat->ii.sense.data[7]);

					retry--;
					irq_ret = -EIO;

				}

			} else if (pdevstat->flag & DEVSTAT_NOT_OPER) {
				/* don't issue warnings during startup unless requested */
				if (init_IRQ_complete || cio_notoper_msg) {

					CIO_DEBUG_ALWAYS(KERN_WARNING, 2,
							 "SPID - Device %04X "
							 "on Subchannel %04X, "
							 "lpm %02X, "
							 "became 'not "
							 "operational'\n",
							 ioinfo[irq]->schib.
							 pmcw.dev, 
							 irq, lpm);
				}

				retry = 0;
				ioinfo[irq]->opm &= ~lpm;
				irq_ret = -EAGAIN;

			}
		} else if (irq_ret == -ETIMEDOUT) {
			/* 
			 * SetPGID timed out, so we cancel it before
			 * we retry
			 */
			int xret;

			xret = cancel_IO(irq);

			if (!xret) 
				CIO_MSG_EVENT(2,
					      "SetPGID: sch canceled "
					      "successfully for irq %x\n",
					      irq);
			retry--;
			
		} else if (irq_ret != -ENODEV) {
			retry--;
			irq_ret = -EIO;
		} else {
			retry = 0;
			irq_ret = -ENODEV;

		}

	} while (retry > 0);

	if (init_IRQ_complete) {
		kfree (spid_ccw);
	} else {
		free_bootmem ((unsigned long) spid_ccw, 2 * sizeof (ccw1_t));

	}

	s390irq_spin_unlock_irqrestore (irq, flags);

	/*
	 * If we installed the irq action handler we have to
	 *  release it too.
	 */
	if (inlreq)
		free_irq (irq, pdevstat);

	return (irq_ret);
}

/*
 * s390_SensePGID
 *
 * Sense Path Group ID
 *
 */
static int
s390_SensePGID (int irq, __u8 lpm, pgid_t * pgid)
{
	ccw1_t *snid_ccw;	/* ccw area for SNID command */
	devstat_t devstat;	/* required by request_irq() */
	devstat_t *pdevstat = &devstat;
	char dbf_txt[15];

	int irq_ret = 0;	/* return code */
	int retry = 5;		/* retry count */
	int inlreq = 0;		/* inline request_irq() */
	unsigned long flags;

	SANITY_CHECK (irq);

	if (ioinfo[irq]->ui.flags.oper == 0) {
		return (-ENODEV);

	}

	sprintf (dbf_txt, "SNID%x", irq);
	CIO_TRACE_EVENT (4, dbf_txt);

	if (!ioinfo[irq]->ui.flags.ready) {
		/*
		 * Perform SENSE PGID command processing. We have to request device
		 *  ownership and provide a dummy I/O handler. We issue sync. I/O
		 *  requests and evaluate the devstat area on return therefore
		 *  we don't need a real I/O handler in place.
		 */
		irq_ret = request_irq (irq,
				       init_IRQ_handler,
				       SA_PROBE, "SNID", pdevstat);

		if (irq_ret == 0)
			inlreq = 1;

	} else {
		pdevstat = ioinfo[irq]->irq_desc.dev_id;

	}

	if (irq_ret) {
		return irq_ret;
	}

	s390irq_spin_lock_irqsave (irq, flags);

	if (init_IRQ_complete) {
		snid_ccw = kmalloc (sizeof (ccw1_t), GFP_DMA);
	} else {
		snid_ccw = alloc_bootmem_low (sizeof (ccw1_t));

	}

	snid_ccw->cmd_code = CCW_CMD_SENSE_PGID;
	snid_ccw->cda = (__u32) virt_to_phys (pgid);
	snid_ccw->count = sizeof (pgid_t);
	snid_ccw->flags = CCW_FLAG_SLI;

	/*
	 * We now issue a SensePGID request. In case of BUSY
	 *  or STATUS PENDING conditions we retry 5 times.
	 */
	do {
		memset (pdevstat, '\0', sizeof (devstat_t));

		irq_ret = s390_start_IO (irq, snid_ccw, 0xE2D5C9C4,	/* == SNID */
					 lpm,	/* n/a */
					 DOIO_WAIT_FOR_INTERRUPT
					 | DOIO_VALID_LPM
					 | DOIO_DONT_CALL_INTHDLR);

		if (irq_ret == 0) {
			if (pdevstat->flag & DEVSTAT_FLAG_SENSE_AVAIL) {
				/*
				 * If the device doesn't support the
				 *  Sense Path Group ID command
				 *  further retries wouldn't help ...
				 */
				if (pdevstat->ii.sense.
				    data[0] & SNS0_CMD_REJECT) {
					retry = 0;
					irq_ret = -EOPNOTSUPP;
				} else {

					CIO_DEBUG(KERN_WARNING, 2,
						  "SNID - device %04X,"
						  " unit check,"
						  " flag %04X, "
						  " retry %d, cnt %02d,"
						  " sns :"
						  " %02X%02X%02X%02X "
						  "%02X%02X%02X%02X ...\n",
						  ioinfo[irq]->schib.pmcw.dev,
						  pdevstat->flag,
						  retry,
						  pdevstat->scnt,
						  pdevstat->ii.sense.data[0],
						  pdevstat->ii.sense.data[1],
						  pdevstat->ii.sense.data[2],
						  pdevstat->ii.sense.data[3],
						  pdevstat->ii.sense.data[4],
						  pdevstat->ii.sense.data[5],
						  pdevstat->ii.sense.data[6],
						  pdevstat->ii.sense.data[7]);

					retry--;
					irq_ret = -EIO;

				}
			} else if (pdevstat->flag & DEVSTAT_NOT_OPER) {
				/* don't issue warnings during startup unless requested */
				if (init_IRQ_complete || cio_notoper_msg) {

					CIO_DEBUG_ALWAYS(KERN_WARNING, 2,
							 "SNID - Device %04X "
							 "on Subchannel %04X, "
							 "lpm %02X, "
							 "became 'not "
							 "operational'\n",
							 ioinfo[irq]->schib.
							 pmcw.dev, 
							 irq, lpm);
				}

				retry = 0;
				irq_ret = -EIO;

			} else {
				retry = 0;	/* success ... */
				irq_ret = 0;

			}
		} else if (irq_ret != -ENODEV) {	/* -EIO, or -EBUSY */

			if (pdevstat->flag & DEVSTAT_STATUS_PENDING) {

				CIO_DEBUG(KERN_INFO, 2,
					  "SNID - Device %04X "
					  "on Subchannel %04X "
					  "reports pending status, "
					  "retry : %d\n",
					  ioinfo[irq]->schib.pmcw.dev, 
					  irq, retry);
			}

			CIO_DEBUG_ALWAYS(KERN_INFO, 2,
					 "SNID - device %04X,"
					 " start_io() reports rc : %d,"
					 " retrying ...\n",
					 ioinfo[irq]->schib.pmcw.dev, 
					 irq_ret);
			retry--;
			irq_ret = -EIO;
		} else {	/* -ENODEV ... */

			retry = 0;
			irq_ret = -ENODEV;

		}

	} while (retry > 0);

	if (init_IRQ_complete) {
		kfree (snid_ccw);
	} else {
		free_bootmem ((unsigned long) snid_ccw, sizeof (ccw1_t));

	}

	s390irq_spin_unlock_irqrestore (irq, flags);

	/*
	 * If we installed the irq action handler we have to
	 *  release it too.
	 */
	if (inlreq)
		free_irq (irq, pdevstat);

	return (irq_ret);
}

/*
 * Function: s390_send_nop
 * 
 * sends a nop CCW to the specified subchannel down the given path(s)
 * FIXME: why not put nop_ccw on the stack, it's only 64 bits?
 */
int
s390_send_nop(int irq, __u8 lpm)
{
 	char dbf_txt[15];
 	ccw1_t *nop_ccw;
 	devstat_t devstat;
 	devstat_t *pdevstat = &devstat;
 	unsigned long flags;
	
 	int irq_ret = 0;
 	int inlreq = 0;
	
 	SANITY_CHECK(irq);
	
 	if (!ioinfo[irq]->ui.flags.oper)
 		/* no sense in trying */
 		return -ENODEV;
	
 	sprintf(dbf_txt, "snop%x", irq);
 	CIO_TRACE_EVENT(5, dbf_txt);
 
 	if (!ioinfo[irq]->ui.flags.ready) {
 		/*
 		 * If there's no handler, use our dummy handler.
 		 */
 		irq_ret = request_irq (irq,
 				       init_IRQ_handler,
 				       SA_PROBE,
 				       "SNOP",
 				       pdevstat);
 		if (!irq_ret)
 			inlreq = 1;
 	} else {
 		pdevstat = ioinfo[irq]->irq_desc.dev_id;
 	}
 	
 	if (irq_ret)
 		return irq_ret;
 
 	s390irq_spin_lock_irqsave (irq, flags);
 
 	if (init_IRQ_complete)
 		nop_ccw = kmalloc (sizeof (ccw1_t), GFP_DMA);
 	else
 		nop_ccw = alloc_bootmem_low (sizeof (ccw1_t));
 
 	nop_ccw->cmd_code = CCW_CMD_NOOP;
 	nop_ccw->cda = 0;
 	nop_ccw->count = 0;
 	nop_ccw->flags = CCW_FLAG_SLI;
 
 	memset (pdevstat, '\0', sizeof (devstat_t));
 	
 	irq_ret = s390_start_IO (irq, nop_ccw, 0xE2D5D6D7, lpm,
 				 DOIO_WAIT_FOR_INTERRUPT
 				 | DOIO_TIMEOUT
 				 | DOIO_DONT_CALL_INTHDLR
 				 | DOIO_VALID_LPM);
 	
 	if (irq_ret == -ETIMEDOUT) {
 
 		/* better cancel... */
 		cancel_IO(irq);
 	}
 
 	if (init_IRQ_complete) 
 		kfree (nop_ccw);
 	else
 		free_bootmem ((unsigned long) nop_ccw, sizeof (ccw1_t));
 
 	s390irq_spin_unlock_irqrestore (irq, flags);
 
 	if (inlreq)
 		free_irq (irq, pdevstat);
 
 	return irq_ret;
 
}

EXPORT_SYMBOL (read_conf_data);
EXPORT_SYMBOL (read_dev_chars);
