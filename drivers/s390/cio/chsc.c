/*
 *  drivers/s390/cio/chsc.c
 *   S/390 common I/O routines -- channel subsystem call
 *   $Revision: 1.12 $
 *
 *    Copyright (C) 1999-2002 IBM Deutschland Entwicklung GmbH,
 *                            IBM Corporation
 *    Author(s): Ingo Adlung (adlung@de.ibm.com)
 *               Cornelia Huck (cohuck@de.ibm.com) 
 *		 Arnd Bergmann (arndb@de.ibm.com)
 *    ChangeLog: 11/04/2002 Arnd Bergmann Split s390io.c into multiple files,
 *					  see s390io.c for complete list of
 *					  changes.
 */

#include <linux/module.h>
#include <linux/config.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/bootmem.h>
#include <linux/init.h>

#include <asm/idals.h>
#include <asm/uaccess.h>
#include <asm/s390io.h>
#include <asm/debug.h>

#include "cio_debug.h"
#include "s390io.h"
#include "proc.h"
#include "ioinfo.h"

#ifdef CONFIG_DEBUG_CHSC
#define DBGC printk
#else
#define DBGC(args,...) do {} while (0)
#endif

#define CHSC_DEBUG(printk_level,event_type,event_level,msg...) ({\
	if (cio_show_msg) DBGC(printk_level msg); \
	CIO_ ## event_type ## _EVENT (event_level, msg); \
})
static __u64 chpids[4]; /* 256 chpids */
static __u64 chpids_logical[4] = {-1,-1,-1,-1};
static __u64 chpids_known[4];

static int cio_chsc_desc_avail;

int
chsc_chpid_logical (int irq, int chp)
{
	return test_bit (ioinfo[irq]->schib.pmcw.chpid[chp], &chpids_logical);
}

static inline void
chsc_clear_chpid(int irq, int chp)
{
	clear_bit(ioinfo[irq]->schib.pmcw.chpid[chp], &chpids);
}

void
chsc_validate_chpids(int irq)
{
	int mask, chp;

	if (ioinfo[irq]->opm) {
		for (chp=0;chp<=7;chp++) {
			mask = 0x80 >> chp;
			if (ioinfo[irq]->opm & mask) {
				if (!chsc_chpid_logical(irq,chp))
					/* disable using this path */
					ioinfo[irq]->opm &= ~mask;
			} else {
				/* This chpid is not
				 * available to us */
				chsc_clear_chpid(irq,chp);
			}
		}
		
	}
	
}

void
switch_off_chpids(int irq, __u8 mask)
{
	int i;
	pmcw_t *pmcw = &ioinfo[irq]->schib.pmcw;

	for (i=0;i<8;i++)
		if ((0x80>>i) & mask
		    & pmcw->pim
		    & pmcw->pam
		    & pmcw->pom)
			clear_bit(pmcw->chpid[i], &chpids);
}

/* FIXME: this is _always_ called for every subchannel. shouldn't we
 * 	  process more than one at a time?*/
static int
chsc_get_sch_desc_irq(int irq)
{
	int j = 0;
	int ccode;

	/* FIXME: chsc_area_sei cannot be on the stack since it needs to
	 * be page-aligned. Implement proper locking or dynamic
	 * allocation or prove that this function does not have to be
	 * reentrant! */
	static chsc_area_t __attribute__ ((aligned(PAGE_SIZE))) chsc_area_ssd;

	typeof (chsc_area_ssd.response_block.response_block_data.ssd_res)
		*ssd_res = &chsc_area_ssd.response_block.response_block_data.ssd_res;

	chsc_area_ssd = (chsc_area_t) {
		request_block: {
			command_code1: 0x0010,
			command_code2: 0x0004,
			request_block_data: {
				ssd_req: {
					f_sch: irq,
					l_sch: irq,
				}
			}
		}
	};

	ccode = chsc(&chsc_area_ssd);
	if (ccode > 0) {
		if (cio_show_msg)
			DBGC( KERN_DEBUG "chsc returned with ccode = %d\n",
			     ccode);
		if (ccode == 3)
			return -ENODEV;
		return -EBUSY;
	}

	switch (chsc_area_ssd.response_block.response_code) {
	case 0x0001: /* everything ok */
		break;
	case 0x0002:
		CHSC_DEBUG (KERN_WARNING, CRW, 2, "Invalid command!\n");
	case 0x0003:
		CHSC_DEBUG (KERN_WARNING, CRW, 2, "Error in chsc request block!\n");
		return -EINVAL;
	case 0x0004:
		CHSC_DEBUG (KERN_WARNING, CRW, 2, "Model does not provide ssd\n");
		return -EOPNOTSUPP;
	default:
		CHSC_DEBUG (KERN_WARNING, CRW, 2, "Unknown CHSC response %d\n", 
				chsc_area_ssd.response_block.response_code);
		return -EIO;
	}

	/* ssd_res->st stores the type of the detected
	 * subchannel, with the following definitions:
	 * 
	 * 0: I/O subchannel:	  All fields have meaning
	 * 1: CHSC subchannel:	  Only sch_val, st and sch
	 * 			  have meaning
	 * 2: Message subchannel: All fields except unit_addr
	 * 			  have meaning
	 * 3: ADM subchannel:	  Only sch_val, st and sch
	 * 			  have meaning
	 *
	 * Other types are currently undefined.
	 *
	 */
	if (ssd_res->st > 3) { /* uhm, that looks strange... */
		CHSC_DEBUG (KERN_DEBUG, CRW, 0, "Strange subchannel type %d"
			    " for sch %x\n", ssd_res->st, irq);
		/*
		 * There may have been a new subchannel type defined in the 
		 * time since this code was written; since we don't know which
		 * fields have meaning and what to do with it we just jump out
		 */
		return 0; 
	} else { 
		const char type[4][8] = {"I/O", "chsc", "message", "ADM"};
		CHSC_DEBUG (KERN_DEBUG, CRW, 6,"ssd: sch %x is %s subchannel\n",
				irq, type[ssd_res->st]);
		if (ioinfo[irq] == INVALID_STORAGE_AREA)
			/* FIXME: we should do device rec. here... */
			return 0;

		ioinfo[irq]->ssd_info.valid = 1;
		ioinfo[irq]->ssd_info.type = ssd_res->st;
	}

	switch (ssd_res->st) {
	case 0: /* fall through */
	case 2:
		for (j=0;j<8;j++) {
			if ((0x80 >> j) & ssd_res->path_mask &
					  ssd_res->fla_valid_mask) {
				u8 chpid = ssd_res->chpid[j];
				if (chpid 
				    && (!test_and_set_bit (chpid, &chpids_known))
				    && (test_bit (chpid, &chpids_logical)))
					set_bit  (chpid, &chpids);

				ioinfo[irq]->ssd_info.chpid[j] = chpid;
				ioinfo[irq]->ssd_info.fla[j]   = ssd_res->fla[j];
			}
		}
		break;
	default:
		break;
	}
	return 0;
}

static int
chsc_get_sch_descriptions( void )
{
	int irq = 0;
	int err = 0;

	CIO_TRACE_EVENT( 4, "gsdesc");

	/*
	 * get information about chpids and link addresses 
	 * by executing the chsc command 'store subchannel description'
	 */

	for (irq=0; irq<=highest_subchannel; irq++) {

		/*
		 * retrieve information for each sch
		 */
		err = chsc_get_sch_desc_irq(irq);
		if (err) {
			static int cio_chsc_err_msg;

			if (!cio_chsc_err_msg) {
				printk( KERN_ERR
					"chsc_get_sch_descriptions:"
					" Error %d while doing chsc; "
					"processing some machine checks may "
					"not work\n", err);
				cio_chsc_err_msg=1;
			}
			return err;
		}
	}
	cio_chsc_desc_avail = 1;
	return 0;
}

__initcall(chsc_get_sch_descriptions);


/* FIXME: don't iterate all subchannels but use the list of known ones*/
static inline void
s390_do_chpid_processing( __u8 chpid)
{
	int irq;
	int j;
	char dbf_txt[15];
	int ccode;
	int was_oper;
	int mask2;

	sprintf(dbf_txt, "chpr%x", chpid);
	CIO_TRACE_EVENT( 2, dbf_txt);

	/* 
	 * TODO: the chpid may be not the chpid with the link incident,
	 * but the chpid the report came in through. How to handle???
	 */
	clear_bit(chpid, &chpids);
	if (!test_and_clear_bit(chpid, &chpids_known))
		return;  /* we didn't know the chpid anyway */

	for (irq=0;irq<=highest_subchannel;irq++) {
		if (ioinfo[irq] == INVALID_STORAGE_AREA) 
			continue;  /* we don't know the device anyway */
		if (ioinfo[irq]->st)
			continue; /* non-io subchannel */
		for (j=0; j<8;j++) {
			if (ioinfo[irq]->schib.pmcw.chpid[j] != chpid)
				continue;

 			/* 
 			 * Note: irq spinlock is grabbed several times
 			 * in here
 			 */
			mask2 = 0x80 >> j;
			s390_send_nop (irq, mask2);

			s390irq_spin_lock(irq);

			/*
			 * FIXME: is this neccessary?
			 */				
			ccode = stsch(irq, &ioinfo[irq]->schib);
			if (ccode) {
				CHSC_DEBUG (KERN_WARNING, CRW,2,
					"do_crw_pending: device on "
					"sch %x is not operational\n",
					irq);
				ioinfo[irq]->ui.flags.oper = 0;
				break;
			}

			ioinfo[irq]->opm = ioinfo[irq]->schib.pmcw.pim &
				ioinfo[irq]->schib.pmcw.pam &
				ioinfo[irq]->schib.pmcw.pom;

			chsc_validate_chpids(irq);

			if (!ioinfo[irq]->opm) {
				/*
				 * sh*t, our last path has gone...
				 * Set the device status to not operational
				 * and eventually notify the device driver
				 */
				CHSC_DEBUG (KERN_WARNING, CRW, 2,
					"do_crw_pending: Last path gone"
					" for device %x, sch %x\n",
					ioinfo[irq]->devno, irq);

				was_oper = ioinfo[irq]->ui.flags.oper;

				ioinfo[irq]->ui.flags.oper = 0;

				if (was_oper && ioinfo[irq]->ui.flags.ready) {

					not_oper_handler_func_t nopfunc = 
						ioinfo[irq]->nopfunc;

					free_irq(irq, ioinfo[irq]->irq_desc.dev_id);
					if (nopfunc)
						nopfunc(irq, DEVSTAT_DEVICE_GONE);
				}

			} 

			s390irq_spin_unlock(irq);
			break;
		}
	}
}

/* this used to be in s390_process_res_acc_*, FIXME: find a better name
 * for this function */
static int
s390_check_valid_chpid(u8 chpid)
{
	char dbf_txt[15];
	sprintf(dbf_txt, "accpr%x", chpid);
	CIO_TRACE_EVENT( 2, dbf_txt);

	/*
	 * I/O resources may have become accessible.
	 * Scan through all subchannels that may be concerned and
	 * do a validation on those.
	 * The more information we have (info), the less scanning
	 * will we have to do.
	 */

	if (!cio_chsc_desc_avail) 
		chsc_get_sch_descriptions();

	if (!cio_chsc_desc_avail) {
		/*
		 * Something went wrong...
		 */
		CHSC_DEBUG (KERN_WARNING, CRW, 0, "Error: Could not retrieve "
			    "subchannel descriptions, will not process css"
			    "machine check...\n");
		return 0;
	}

	if (!test_bit(chpid, &chpids_logical))
		return 0; /* no need to do the rest */

	return 1;
}

static void
s390_process_res_acc_chpid (u8 chpid)
{
	int irq = 0;
	int ccode;
	int chp;
	int mask;
	int ret;

	if (!s390_check_valid_chpid(chpid))
		return;
	
	DBGC( KERN_DEBUG "Looking at chpid %x...\n", chpid);

	for (irq=0; irq<=__MAX_SUBCHANNELS; irq++) {
		if (ioinfo[irq] == INVALID_STORAGE_AREA) {
			/*
			 * We don't know the device yet, but since a path
			 * may be available now to the device we'll have
			 * to do recognition again.
			 * Since we don't have any idea about which chpid
			 * that beast may be on we'll have to do a stsch
			 * on all devices, grr...
			 */
			int valret = 0;

			valret = s390_validate_subchannel(irq,0);
			if (valret == -ENXIO) {
				/* We're through */
				return;
			}
			if (irq > highest_subchannel)
				highest_subchannel = irq;
			if (valret == 0)
				s390_device_recognition_irq(irq);
			continue;
		}

		if (!ioinfo[irq]->st)
			continue;

		/*
		 * Send nops down each path to find out
		 * which path is there
		 */

		for (chp=0;chp<=7;chp++) {
			mask = 0x80 >> chp;

			/*
			 * check if chpid is in information
			 * updated by ssd
			 */
			if ((ioinfo[irq]->ssd_info.valid) &&
			    (ioinfo[irq]->ssd_info.chpid[chp] == chpid))
				ret = s390_send_nop (irq, mask);
		}

		ccode = stsch(irq, &ioinfo[irq]->schib);
		if (ccode > 0) {
			ioinfo[irq]->ui.flags.oper = 0;
			continue;
		}

		ioinfo[irq]->opm = ioinfo[irq]->schib.pmcw.pim &
				   ioinfo[irq]->schib.pmcw.pam &
				   ioinfo[irq]->schib.pmcw.pom;

		chsc_validate_chpids(irq);

		if ((ioinfo[irq]->ui.flags.ready) && (chpid & ioinfo[irq]->opm))
			s390_DevicePathVerification(irq, chpid);
	}
}

static void
s390_process_res_acc_linkaddr ( __u8 chpid, __u16 fla, u32 fla_mask)
{
	char dbf_txt[15];
	int irq = 0;
	int ccode;
	int mask2;
	int ret;
	int j;

	if (!s390_check_valid_chpid(chpid))
		return;
	
		sprintf(dbf_txt, "fla%x", fla);
		CIO_TRACE_EVENT( 2, dbf_txt);
	
	DBGC (KERN_DEBUG "Looking at chpid %x, link addr %x...\n", chpid, fla);

	for (irq=0; irq<=__MAX_SUBCHANNELS; irq++) {

		if (ioinfo[irq] == INVALID_STORAGE_AREA) {
			/* The full program again (see above), grr... */
			int valret = 0;

			valret = s390_validate_subchannel(irq,0);
			if (valret == -ENXIO) {
				/* We're done */
				return;
			}
			if (irq > highest_subchannel)
				highest_subchannel = irq;
			if (valret == 0)
				s390_device_recognition_irq(irq);
			continue;
		}

		if (!ioinfo[irq]->st)
			continue;
		
		/*
		 * Walk through all subchannels and
		 * look if our chpid and our (masked) link 
		 * address are in somewhere
		 * Do a stsch for the found subchannels and
		 * perform path grouping  
		 */

		/* Update our ssd_info */
		if (chsc_get_sch_desc_irq(irq))
			break;

		for (j=0;j<8;j++) {
			if ((ioinfo[irq]->ssd_info.chpid[j] == chpid) &&
			    ((ioinfo[irq]->ssd_info.fla[j]&fla_mask) == fla)) {

				mask2 = 0x80 >> j;
				ret = s390_send_nop (irq, mask2);

				ccode = stsch(irq,&ioinfo[irq]->schib);
				if (ccode > 0)
					break;

				ioinfo[irq]->opm = ioinfo[irq]->schib.pmcw.pim &
						   ioinfo[irq]->schib.pmcw.pam &
						   ioinfo[irq]->schib.pmcw.pom;

				chsc_validate_chpids(irq);

				if (ioinfo[irq]->ui.flags.ready)
					s390_DevicePathVerification(irq, chpid);

				break;
			}
		}
	}
}

void
s390_process_css( void )
{
	int ccode;

	/* 
	 * build the chsc request block for store event information 
	 * and do the call 
	 */

	/* FIXME: chsc_area_sei cannot be on the stack since it needs to
	 * be page-aligned. Implement proper locking or dynamic
	 * allocation or prove that this function does not have to be
	 * reentrant! */
	static chsc_area_t chsc_area_sei __attribute__ ((aligned(PAGE_SIZE))) = {
		request_block: {
			command_code1: 0x0010,
			command_code2: 0x000e
		}
	};

	typeof (chsc_area_sei.response_block.response_block_data.sei_res)
		*sei_res = &chsc_area_sei.response_block.response_block_data.sei_res;


	CIO_TRACE_EVENT( 2, "prcss");

	ccode = chsc(&chsc_area_sei);

	if (ccode > 0)
		return;


	switch (chsc_area_sei.response_block.response_code) {
	/* for debug purposes, check for problems */
	case 0x0001:
		break; /* everything ok */
	case 0x0002:
		CHSC_DEBUG (KERN_WARNING, CRW, 2, "s390_process_css:"
			    "invalid command!\n");
	case 0x0003:
		CHSC_DEBUG (KERN_WARNING, CRW, 2, "s390_process_css:"
				" error in chsc request block!\n");
		return;
	case 0x0005:
		CHSC_DEBUG (KERN_WARNING, CRW, 2, "s390_process_css:"
				" no event information stored\n");
		return;
	default:
		CHSC_DEBUG (KERN_WARNING, CRW, 2, "s390_process_css:"
				" chsc response %d\n",
				chsc_area_sei.response_block.response_code);
		return;
	}

	CHSC_DEBUG (KERN_DEBUG, CRW, 4, "s390_process_css: "
		    "event information successfully stored\n");

	if (sei_res->rs != 4) {
		CHSC_DEBUG (KERN_ERR, CRW, 2, "s390_process_css: "
			    "reporting source (%04X) isn't a chpid!"
			    "Aborting processing of machine check...\n",
			    sei_res->rsid);
		return;
	}

	/* which kind of information was stored? */
	switch (sei_res->cc) {
	case 1: /* link incident*/
		CHSC_DEBUG (KERN_DEBUG, CRW, 4, "s390_process_css: "
			"channel subsystem reports link incident,"
			" source is chpid %x\n", sei_res->rsid);

		s390_do_chpid_processing(sei_res->rsid);
		break;

	case 2: /* i/o resource accessibiliy */
		CHSC_DEBUG (KERN_DEBUG, CRW, 4, "s390_process_css: "
			"channel subsystem " "reports some I/O "
			"devices may have become accessable\n");
		DBGC ( KERN_DEBUG "Data received after sei: \n");
		DBGC ( KERN_DEBUG "Validity flags: %x\n", sei_res->vf);

		if ((sei_res->vf & 0x80) == 0) {
			DBGC ( KERN_DEBUG "chpid: %x\n", sei_res->rsid);
			s390_process_res_acc_chpid (sei_res->rsid);
		} else if ((sei_res->vf & 0xc0) == 0x80) {
			DBGC ( KERN_DEBUG "chpid: %x link addr: %x\n",
				sei_res->rsid, sei_res->fla);
			s390_process_res_acc_linkaddr (sei_res->rsid, 
					sei_res->fla, 0xff00);
		} else if ((sei_res->vf & 0xc0) == 0xc0) {
			DBGC ( KERN_DEBUG "chpid: %x full link addr: %x\n",
				sei_res->rsid, sei_res->fla);
			s390_process_res_acc_linkaddr (sei_res->rsid,
					sei_res->fla, 0xffff);
		}
		DBGC ( KERN_DEBUG "\n");

		break;

	default: /* other stuff */
		CHSC_DEBUG (KERN_DEBUG, CRW, 4, "s390_process_css: "
			    "event %d\n", sei_res->cc);
		break;
	}
}


#ifdef CONFIG_PROC_FS

static int s390_vary_chpid( __u8 chpid, int on );

/*
 * Function: cio_parse_chpids_proc_parameters
 * parse the stuff piped to /proc/chpids
 */
static inline void
cio_parse_chpids_proc_parameters(char* buf)
{
	int onoff;
	int cp;

	if (!strncmp (buf, "on ", 3)) {
		onoff = 1;
		buf += 3;
	} else if (!strncmp (buf, "off ", 4)) {
		onoff = 0;
		buf += 4;
	} else {
		printk(KERN_ERR "/proc/chpids: Parse error; "
		       "try using '{on,off} <chpid>'\n");
		return;
	}

	if (*buf == '0') {
		if (*(++buf) == 'x') 	/* strip leading zero */
			buf++;		/* strip leading x */
	}
	cp = simple_strtoul (buf, &buf, 16);	/* interpret anything as hex */

	chsc_get_sch_descriptions();
	if (!cio_chsc_desc_avail) {
		printk(KERN_ERR "Could not get chpid status, "
		       "vary on/off not available\n");
		return;
	}

	if (test_bit(cp, &chpids) != onoff) {
		if (s390_vary_chpid(cp, onoff))
			printk(KERN_WARNING "/proc/chpids: "
			       "Invalid chpid specified\n");
		else
			printk(KERN_INFO "/proc/chpids: "
			       "Varied chpid %x logically %sline\n",
			       cp, onoff ? "on" : "off");
	} else {
		printk(KERN_ERR "/proc/chpids: chpid %x is "
		       "already %sline\n", cp, onoff ? "on" : "off");
	}
}

/*
 * Function: s390_vary_chpid
 * Varies the specified chpid online or offline
 */
static int
s390_vary_chpid( __u8 chpid, int on) 
{
	char dbf_text[15];
	int irq;

	if ((chpid <=0) || (chpid >= NR_CHPIDS))
		return -EINVAL;

	sprintf(dbf_text, on?"varyon%x":"varyoff%x", chpid);
	CIO_TRACE_EVENT( 2, dbf_text);

	if (!test_bit(chpid, &chpids_known)) {
		printk(KERN_ERR "Can't vary unknown chpid %02X\n", chpid);
		return -EINVAL;
	}

	if (on) {
		set_bit(chpid, &chpids_logical);
		set_bit(chpid, &chpids);
	} else {
		clear_bit(chpid, &chpids_logical);
		clear_bit(chpid, &chpids);
	}

	/*
	 * Redo PathVerification on the devices the chpid connects to
	 */
	
	for (irq=0;irq<=highest_subchannel;irq++) {
		int chp;
		/* 
		 * We don't need to adjust the opm, as this will be done in
		 * DevicePathVerification...
		 */

		if ((ioinfo[irq] == INVALID_STORAGE_AREA)
		    || ioinfo[irq]->st
		    || (!ioinfo[irq]->ssd_info.valid))
			continue;

		for (chp = 0; chp < 8; chp++) {
			if (ioinfo[irq]->ssd_info.chpid[chp] == chpid) {

				DBGC (KERN_DEBUG "Calling "
				     "DevicePathVerification for irq %d\n",
				     irq);

				s390_DevicePathVerification(irq, 0);
				break;
			}
		}
	}

	return 0;
}

/*
 * /proc/chpids to display available chpids
 * vary chpids on/off by piping to it
 */

static int
cio_chpids_read (char *page, char **start, off_t off,
		 int count, int *eof, void *data)
{
	int chp = off;
	int len = 0;
	const unsigned int entry_size = 23;  /* longest entry is
						"0xYZ logically offline\n" */

	chsc_get_sch_descriptions();
	if (!cio_chsc_desc_avail) {
		return sprintf(page, "no info available\n");
	}

	while (chp < NR_CHPIDS && len + entry_size < count) {
		if (test_bit(chp, &chpids_known)) {

			if (!test_bit(chp, &chpids))
				len += sprintf(page+len,
					       "0x%02X n/a\n", chp);
			
			else if (test_bit(chp, &chpids_logical))
				len += sprintf(page+len,
					       "0x%02X online\n", chp);
			else
				len += sprintf(page+len,
					       "0x%02X logically offline\n", 
					       chp);
		}
		chp++;
	}

	if (chp < NR_CHPIDS)
		*eof = 1;
	
	*start = (char *) (chp - off);
	return len;
}

static int
cio_chpids_write (struct file *file, const char *user_buf,
			     unsigned long user_len, void *data)
{
	char buffer[16] = {0,};

	if (strncpy_from_user (buffer, user_buf, min(15ul,user_len)) < 0)
		return -EFAULT;

	CHSC_DEBUG (KERN_DEBUG, MSG, 2, "/proc/chpids: '%s'\n", buffer);

	cio_parse_chpids_proc_parameters(buffer);

	return user_len;
}

static int
cio_chpids_proc_init(void)
{
	struct proc_dir_entry *entry;
	entry = create_proc_entry("chpids", S_IFREG|S_IRUGO|S_IWUSR, &proc_root);

	if (!entry)
		return 0;

	entry->read_proc  = &cio_chpids_read;
	entry->write_proc = &cio_chpids_write;
	
	return 1;
}

__initcall(cio_chpids_proc_init);
#endif /* CONFIG_PROC_FS */
