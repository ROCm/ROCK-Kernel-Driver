/***************************************************************************
 *
 *  drivers/s390/char/tape34xx.c
 *    common tape device discipline for 34xx tapes.
 *
 *  S390 version
 *    Copyright (C) 2000 IBM Corporation
 *    Author(s): Tuan Ngo-Anh <ngoanh@de.ibm.com>
 *               Carsten Otte <cotte@de.ibm.com>
 *
 *  UNDER CONSTRUCTION: Work in progress...:-)
 ****************************************************************************
 */

#include "tapedefs.h"
#include <linux/config.h>
#include <linux/version.h>
#include <linux/stddef.h>
#include <linux/kernel.h>
#include <asm/types.h>
#include <asm/uaccess.h>
#include <linux/stat.h>
#include <linux/proc_fs.h>
#include <asm/ccwcache.h> 
#include <asm/idals.h>  
#ifdef CONFIG_S390_TAPE_DYNAMIC
#include <asm/s390dyn.h>
#endif
#include <asm/debug.h>
#include <linux/compatmac.h>
#include "tape.h"
#include "tape34xx.h"

#define PRINTK_HEADER "T34xx:"

tape_event_handler_t tape34xx_event_handler_table[TS_SIZE][TE_SIZE] =
{
    /* {START , DONE, FAILED, ERROR, OTHER } */
	{NULL, tape34xx_unused_done, NULL, tape34xx_unused_error, NULL},	/* TS_UNUSED */
	{NULL, tape34xx_idle_done, NULL, tape34xx_idle_error, NULL},	/* TS_IDLE */
	{NULL, NULL, NULL, NULL, NULL},		/* TS_DONE */
	{NULL, NULL, NULL, NULL, NULL},		/* TS_FAILED */
	{NULL, tape34xx_block_done, NULL, tape34xx_block_error, NULL},		/* TS_BLOCK_INIT */
	{NULL, tape34xx_bsb_init_done, NULL, NULL, NULL},	/* TS_BSB_INIT */
	{NULL, tape34xx_bsf_init_done, NULL, NULL, NULL},	/* TS_BSF_INIT */
	{NULL, tape34xx_dse_init_done, NULL, NULL, NULL},	/* TS_DSE_INIT */
	{NULL, NULL, NULL, NULL, NULL},		/* TS_EGA_INIT */
	{NULL, tape34xx_fsb_init_done, NULL, NULL, NULL},	/* TS_FSB_INIT */
	{NULL, tape34xx_fsf_init_done, NULL, tape34xx_fsf_init_error, NULL},	/* TS_FSF_INIT */
	{NULL, NULL, NULL, NULL, NULL},		/* TS_LDI_INIT */
	{NULL, tape34xx_lbl_init_done, NULL, tape34xx_lbl_init_error, NULL},	/* TS_LBL_INIT */
	{NULL, NULL, NULL, NULL, NULL},		/* TS_MSE_INIT */
	{NULL, tape34xx_nop_init_done, NULL, NULL, NULL},	/* TS_NOP_INIT */
	{NULL, NULL, NULL, NULL, NULL},		/* TS_RBA_INIT */
	{NULL, tape34xx_rbi_init_done, NULL, NULL, NULL},	/* TS_RBI_INIT */
	{NULL, NULL, NULL, NULL, NULL},		/* TS_RBU_INIT */
	{NULL, NULL, NULL, NULL, NULL},		/* TS_RBL_INIT */
	{NULL, NULL, NULL, NULL, NULL},		/* TS_RDC_INIT */
	{NULL, tape34xx_rfo_init_done, NULL, tape34xx_rfo_init_error, NULL},	/* TS_RFO_INIT */
	{NULL, NULL, NULL, NULL, NULL},		/* TS_RSD_INIT */
	{NULL, tape34xx_rew_init_done, NULL, tape34xx_rew_init_error, NULL},	/* TS_REW_INIT */
	{NULL, tape34xx_rew_release_init_done, NULL, tape34xx_rew_release_init_error, NULL},	/* TS_REW_RELEASE_IMIT */
	{NULL, tape34xx_run_init_done, NULL, tape34xx_run_init_error, NULL},	/* TS_RUN_INIT */
	{NULL, NULL, NULL, NULL, NULL},		/* TS_SEN_INIT */
	{NULL, NULL, NULL, NULL, NULL},		/* TS_SID_INIT */
	{NULL, NULL, NULL, NULL, NULL},		/* TS_SNP_INIT */
	{NULL, NULL, NULL, NULL, NULL},		/* TS_SPG_INIT */
	{NULL, NULL, NULL, NULL, NULL},		/* TS_SWI_INIT */
	{NULL, NULL, NULL, NULL, NULL},		/* TS_SMR_INIT */
	{NULL, NULL, NULL, NULL, NULL},		/* TS_SYN_INIT */
	{NULL, NULL, NULL, NULL, NULL},		/* TS_TIO_INIT */
	{NULL, NULL, NULL, NULL, NULL},		/* TS_UNA_INIT */
	{NULL, tape34xx_wri_init_done, NULL, tape34xx_wri_init_error, NULL},	/* TS_WRI_INIT */
	{NULL, tape34xx_wtm_init_done, NULL, tape34xx_wtm_init_error, NULL},	/* TS_WTM_INIT */
	{NULL, NULL, NULL, NULL, NULL}};        /* TS_NOT_OPER */


int
tape34xx_ioctl_overload (struct inode *inode, struct file *filp, unsigned int cmd, unsigned long arg)
{
	return -EINVAL;		// no additional ioctls

}

ccw_req_t *
tape34xx_write_block (const char *data, size_t count, tape_info_t * tape)
{
	long lockflags;
	ccw_req_t *cqr;
	ccw1_t *ccw;
	void *mem;
	cqr = tape_alloc_ccw_req (tape, 2, 0);
	if (!cqr) {
#ifdef TAPE_DEBUG
	        debug_text_exception (tape_debug_area,6,"xwbl nomem");
#endif /* TAPE_DEBUG */
		return NULL;
	}
	mem = kmalloc (count, GFP_KERNEL);
	if (!mem) {
		tape_free_request (cqr);
#ifdef TAPE_DEBUG
	        debug_text_exception (tape_debug_area,6,"xwbl nomem");
#endif /* TAPE_DEBUG */
		return NULL;
	}
	if (copy_from_user (mem, data, count)) {
		kfree (mem);
		tape_free_request (cqr);
#ifdef TAPE_DEBUG
	        debug_text_exception (tape_debug_area,6,"xwbl segf.");
#endif /* TAPE_DEBUG */
		return NULL;
	}
	ccw = cqr->cpaddr;
	ccw->cmd_code = MODE_SET_DB;
	ccw->flags = CCW_FLAG_CC;
	ccw->count = 1;
	set_normalized_cda (ccw, (unsigned long) (&(((tape34xx_disc_data_t *) tape->discdata)->modeset_byte)));
	ccw++;

	ccw->cmd_code = WRITE_CMD;
	ccw->flags = 0;
	ccw->count = count;
	set_normalized_cda (ccw, (unsigned long) mem);
	if ((ccw->cda) == 0) {
		kfree (mem);
		tape_free_request (cqr);
		return NULL;
	}
	s390irq_spin_lock_irqsave (tape->devinfo.irq, lockflags);
	tape->kernbuf = mem;
	tape->userbuf = (void *) data;
	tapestate_set (tape, TS_WRI_INIT);
	s390irq_spin_unlock_irqrestore (tape->devinfo.irq, lockflags);
#ifdef TAPE_DEBUG
	debug_text_event (tape_debug_area,6,"xwbl ccwg");
#endif /* TAPE_DEBUG */
	return cqr;
}

void 
tape34xx_free_write_block (ccw_req_t * cqr, tape_info_t * tape)
{
	unsigned long lockflags;
	ccw1_t *ccw;
	s390irq_spin_lock_irqsave (tape->devinfo.irq, lockflags);
	ccw = cqr->cpaddr;
	ccw++;
	clear_normalized_cda (ccw);
	kfree (tape->kernbuf);
	tape_free_request (cqr);
	tape->kernbuf = tape->userbuf = NULL;
	s390irq_spin_unlock_irqrestore (tape->devinfo.irq, lockflags);
#ifdef TAPE_DEBUG
	debug_text_event (tape_debug_area,6,"xfwb free");
#endif /* TAPE_DEBUG */
}

ccw_req_t *
tape34xx_read_block (const char *data, size_t count, tape_info_t * tape)
{
	long lockflags;
	ccw_req_t *cqr;
	ccw1_t *ccw;
	void *mem;
	cqr = tape_alloc_ccw_req (tape, 2, 0);
	if (!cqr) {
#ifdef TAPE_DEBUG
	        debug_text_exception (tape_debug_area,6,"xrbl nomem");
#endif /* TAPE_DEBUG */
		return NULL;
	}
	mem = kmalloc (count, GFP_KERNEL);
	if (!mem) {
		tape_free_request (cqr);
#ifdef TAPE_DEBUG
	        debug_text_exception (tape_debug_area,6,"xrbl nomem");
#endif /* TAPE_DEBUG */
		return NULL;
	}
	ccw = cqr->cpaddr;
	ccw->cmd_code = MODE_SET_DB;
	ccw->flags = CCW_FLAG_CC;
	ccw->count = 1;
	set_normalized_cda (ccw, (unsigned long) (&(((tape34xx_disc_data_t *) tape->discdata)->modeset_byte)));
	ccw++;

	ccw->cmd_code = READ_FORWARD;
	ccw->flags = 0;
	ccw->count = count;
	set_normalized_cda (ccw, (unsigned long) mem);
	if ((ccw->cda) == 0) {
		kfree (mem);
		tape_free_request (cqr);
		return NULL;
	}
	s390irq_spin_lock_irqsave (tape->devinfo.irq, lockflags);
	tape->kernbuf = mem;
	tape->userbuf = (void *) data;
	tapestate_set (tape, TS_RFO_INIT);
	s390irq_spin_unlock_irqrestore (tape->devinfo.irq, lockflags);
#ifdef TAPE_DEBUG
	debug_text_event (tape_debug_area,6,"xrbl ccwg");
#endif /* TAPE_DEBUG */
	return cqr;
}

void 
tape34xx_free_read_block (ccw_req_t * cqr, tape_info_t * tape)
{
	unsigned long lockflags;
	size_t cpysize;
	ccw1_t *ccw;
	s390irq_spin_lock_irqsave (tape->devinfo.irq, lockflags);
	ccw = cqr->cpaddr;
	ccw++;
	cpysize = ccw->count - tape->devstat.rescnt;
	s390irq_spin_unlock_irqrestore (tape->devinfo.irq, lockflags);
	if (copy_to_user (tape->userbuf, tape->kernbuf, cpysize)) {
#ifdef TAPE_DEBUG
	    debug_text_exception (tape_debug_area,6,"xfrb segf.");
#endif /* TAPE_DEBUG */
	}
	s390irq_spin_lock_irqsave (tape->devinfo.irq, lockflags);
	clear_normalized_cda (ccw);
	kfree (tape->kernbuf);
	tape_free_request (cqr);
	tape->kernbuf = tape->userbuf = NULL;
	s390irq_spin_unlock_irqrestore (tape->devinfo.irq, lockflags);
#ifdef TAPE_DEBUG
	debug_text_event (tape_debug_area,6,"xfrb free");
#endif /* TAPE_DEBUG */
}


/*
 * The IOCTL interface is implemented in the following section,
 * excepted the MTRESET, MTSETBLK which are handled by tapechar.c
 */
/*
 * MTFSF: Forward space over 'count' file marks. The tape is positioned
 * at the EOT (End of Tape) side of the file mark.
 */
ccw_req_t *
tape34xx_mtfsf (tape_info_t * tape, int count)
{
	long lockflags;
	int i;
	ccw_req_t *cqr;
	ccw1_t *ccw;
	if ((count == 0) || (count > 510)) {
#ifdef TAPE_DEBUG
	        debug_text_exception (tape_debug_area,6,"xfsf parm");
#endif /* TAPE_DEBUG */
		return NULL;
	}
	cqr = tape_alloc_ccw_req (tape, 2 + count, 0);
	if (!cqr) {
#ifdef TAPE_DEBUG
	        debug_text_exception (tape_debug_area,6,"xfsf nomem");
#endif /* TAPE_DEBUG */
		return NULL;
	}
	ccw = cqr->cpaddr;
	ccw->cmd_code = MODE_SET_DB;
	ccw->flags = CCW_FLAG_CC;
	ccw->count = 1;
	set_normalized_cda (ccw, (unsigned long) (&(((tape34xx_disc_data_t *) tape->discdata)->modeset_byte)));
	ccw++;
	for (i = 0; i < count; i++) {
		ccw->cmd_code = FORSPACEFILE;
		ccw->flags = CCW_FLAG_CC;
		ccw->count = 0;
		ccw->cda = (unsigned long) (&(ccw->cmd_code));
		ccw++;
	}
	ccw->cmd_code = NOP;
	ccw->flags = 0;
	ccw->count = 0;
	ccw->cda = (unsigned long) (&(ccw->cmd_code));
	s390irq_spin_lock_irqsave (tape->devinfo.irq, lockflags);
	tape->kernbuf = NULL;
	tape->userbuf = NULL;
	tapestate_set (tape, TS_FSF_INIT);
	s390irq_spin_unlock_irqrestore (tape->devinfo.irq, lockflags);
#ifdef TAPE_DEBUG
	debug_text_event (tape_debug_area,6,"xfsf ccwg");
#endif /* TAPE_DEBUG */
	return cqr;
}

/*
 * MTBSF: Backward space over 'count' file marks. The tape is positioned at
 * the EOT (End of Tape) side of the last skipped file mark.
 */
ccw_req_t *
tape34xx_mtbsf (tape_info_t * tape, int count)
{
	long lockflags;
	int i;
	ccw_req_t *cqr;
	ccw1_t *ccw;
	if ((count == 0) || (count > 510)) {
#ifdef TAPE_DEBUG
	        debug_text_exception (tape_debug_area,6,"xbsf parm");
#endif /* TAPE_DEBUG */
	        return NULL;
	}
	cqr = tape_alloc_ccw_req (tape, 2 + count, 0);
	if (!cqr) {
#ifdef TAPE_DEBUG
	        debug_text_exception (tape_debug_area,6,"xbsf nomem");
#endif /* TAPE_DEBUG */
		return NULL;
	}
	ccw = cqr->cpaddr;
	ccw->cmd_code = MODE_SET_DB;
	ccw->flags = CCW_FLAG_CC;
	ccw->count = 1;
	set_normalized_cda (ccw, (unsigned long) (&(((tape34xx_disc_data_t *) tape->discdata)->modeset_byte)));
	ccw++;
	for (i = 0; i < count; i++) {
		ccw->cmd_code = BACKSPACEFILE;
		ccw->flags = CCW_FLAG_CC;
		ccw->count = 0;
		ccw->cda = (unsigned long) (&(ccw->cmd_code));
		ccw++;
	}
	ccw->cmd_code = NOP;
	ccw->flags = 0;
	ccw->count = 0;
	ccw->cda = (unsigned long) (&(ccw->cmd_code));
	s390irq_spin_lock_irqsave (tape->devinfo.irq, lockflags);
	tape->kernbuf = NULL;
	tape->userbuf = NULL;
	tapestate_set (tape, TS_BSF_INIT);
	s390irq_spin_unlock_irqrestore (tape->devinfo.irq, lockflags);
#ifdef TAPE_DEBUG
	debug_text_event (tape_debug_area,6,"xbsf ccwg");
#endif /* TAPE_DEBUG */
	return cqr;
}

/*
 * MTFSR: Forward space over 'count' tape blocks (blocksize is set
 * via MTSETBLK.
 */
ccw_req_t *
tape34xx_mtfsr (tape_info_t * tape, int count)
{
	long lockflags;
	int i;
	ccw_req_t *cqr;
	ccw1_t *ccw;
	if ((count == 0) || (count > 510)) {
#ifdef TAPE_DEBUG
	        debug_text_exception (tape_debug_area,6,"xfsr parm");
#endif /* TAPE_DEBUG */
		return NULL;
	}
	cqr = tape_alloc_ccw_req (tape, 2 + count, 0);
	if (!cqr) {
#ifdef TAPE_DEBUG
	        debug_text_exception (tape_debug_area,6,"xfsr nomem");
#endif /* TAPE_DEBUG */
		return NULL;
	}
	ccw = cqr->cpaddr;
	ccw->cmd_code = MODE_SET_DB;
	ccw->flags = CCW_FLAG_CC;
	ccw->count = 1;
	set_normalized_cda (ccw, (unsigned long) (&(((tape34xx_disc_data_t *) tape->discdata)->modeset_byte)));
	ccw++;
	for (i = 0; i < count; i++) {
		ccw->cmd_code = FORSPACEBLOCK;
		ccw->flags = CCW_FLAG_CC;
		ccw->count = 0;
		ccw->cda = (unsigned long) (&(ccw->cmd_code));
		ccw++;
	}
	ccw->cmd_code = NOP;
	ccw->flags = 0;
	ccw->count = 0;
	ccw->cda = (unsigned long) (&(ccw->cmd_code));
	s390irq_spin_lock_irqsave (tape->devinfo.irq, lockflags);
	tape->kernbuf = NULL;
	tape->userbuf = NULL;
	tapestate_set (tape, TS_FSB_INIT);
	s390irq_spin_unlock_irqrestore (tape->devinfo.irq, lockflags);
#ifdef TAPE_DEBUG
	debug_text_event (tape_debug_area,6,"xfsr ccwgen");
#endif /* TAPE_DEBUG */
	return cqr;
}

/*
 * MTBSR: Backward space over 'count' tape blocks.
 * (blocksize is set via MTSETBLK.
 */
ccw_req_t *
tape34xx_mtbsr (tape_info_t * tape, int count)
{
	long lockflags;
	int i;
	ccw_req_t *cqr;
	ccw1_t *ccw;
	if ((count == 0) || (count > 510)) {
#ifdef TAPE_DEBUG
	        debug_text_exception (tape_debug_area,6,"xbsr parm");
#endif /* TAPE_DEBUG */   
	        return NULL;
	}
	cqr = tape_alloc_ccw_req (tape, 2 + count, 0);
	if (!cqr) {
#ifdef TAPE_DEBUG
	        debug_text_exception (tape_debug_area,6,"xbsr nomem");
#endif /* TAPE_DEBUG */
		return NULL;
	}
	ccw = cqr->cpaddr;
	ccw->cmd_code = MODE_SET_DB;
	ccw->flags = CCW_FLAG_CC;
	ccw->count = 1;
	set_normalized_cda (ccw, (unsigned long) (&(((tape34xx_disc_data_t *) tape->discdata)->modeset_byte)));
	ccw++;
	for (i = 0; i < count; i++) {
		ccw->cmd_code = BACKSPACEBLOCK;
		ccw->flags = CCW_FLAG_CC;
		ccw->count = 0;
		ccw->cda = (unsigned long) (&(ccw->cmd_code));
		ccw++;
	}
	ccw->cmd_code = NOP;
	ccw->flags = 0;
	ccw->count = 0;
	ccw->cda = (unsigned long) (&(ccw->cmd_code));
	s390irq_spin_lock_irqsave (tape->devinfo.irq, lockflags);
	tape->kernbuf = NULL;
	tape->userbuf = NULL;
	tapestate_set (tape, TS_BSB_INIT);
	s390irq_spin_unlock_irqrestore (tape->devinfo.irq, lockflags);
#ifdef TAPE_DEBUG
	debug_text_event (tape_debug_area,6,"xbsr ccwg");
#endif /* TAPE_DEBUG */
	return cqr;
}

/*
 * MTWEOF: Write 'count' file marks at the current position.
 */
ccw_req_t *
tape34xx_mtweof (tape_info_t * tape, int count)
{
	long lockflags;
	int i;
	ccw_req_t *cqr;
	ccw1_t *ccw;
	if ((count == 0) || (count > 510)) {
#ifdef TAPE_DEBUG
	        debug_text_exception (tape_debug_area,6,"xweo parm");
#endif /* TAPE_DEBUG */
		return NULL;
	}
	cqr = tape_alloc_ccw_req (tape, 2 + count, 0);
	if (!cqr) {
#ifdef TAPE_DEBUG
	        debug_text_exception (tape_debug_area,6,"xweo nomem");
#endif /* TAPE_DEBUG */
		return NULL;
	}
	ccw = cqr->cpaddr;
	ccw->cmd_code = MODE_SET_DB;
	ccw->flags = CCW_FLAG_CC;
	ccw->count = 1;
	set_normalized_cda (ccw, (unsigned long) (&(((tape34xx_disc_data_t *) tape->discdata)->modeset_byte)));
	ccw++;
	for (i = 0; i < count; i++) {
		ccw->cmd_code = WRITETAPEMARK;
		ccw->flags = CCW_FLAG_CC;
		ccw->count = 1;
		ccw->cda = (unsigned long) (&(ccw->cmd_code));
		ccw++;
	}
	ccw->cmd_code = NOP;
	ccw->flags = 0;
	ccw->count = 0;
	ccw->cda = (unsigned long) (&(ccw->cmd_code));
	ccw++;
	s390irq_spin_lock_irqsave (tape->devinfo.irq, lockflags);
	tape->kernbuf = NULL;
	tape->userbuf = NULL;
	tapestate_set (tape, TS_WTM_INIT);
	s390irq_spin_unlock_irqrestore (tape->devinfo.irq, lockflags);
#ifdef TAPE_DEBUG
	debug_text_event (tape_debug_area,6,"xweo ccwg");
#endif /* TAPE_DEBUG */
	return cqr;
}

/*
 * MTREW: Rewind the tape.
 */
ccw_req_t *
tape34xx_mtrew (tape_info_t * tape, int count)
{
	long lockflags;
	ccw_req_t *cqr;
	ccw1_t *ccw;
	cqr = tape_alloc_ccw_req (tape, 3, 0);
	if (!cqr) {
#ifdef TAPE_DEBUG
	        debug_text_exception (tape_debug_area,6,"xrew nomem");
#endif /* TAPE_DEBUG */
		return NULL;
	}
	ccw = cqr->cpaddr;
	ccw->cmd_code = MODE_SET_DB;
	ccw->flags = CCW_FLAG_CC;
	ccw->count = 1;
	set_normalized_cda (ccw, (unsigned long) (&(((tape34xx_disc_data_t *) tape->discdata)->modeset_byte)));
	ccw++;
	ccw->cmd_code = REWIND;
	ccw->flags = CCW_FLAG_CC;
	ccw->count = 0;
	ccw->cda = (unsigned long) (&(ccw->cmd_code));
	ccw++;
	ccw->cmd_code = NOP;
	ccw->flags = 0;
	ccw->count = 0;
	ccw->cda = (unsigned long) (&(ccw->cmd_code));
	s390irq_spin_lock_irqsave (tape->devinfo.irq, lockflags);
	tape->kernbuf = NULL;
	tape->userbuf = NULL;
	tapestate_set (tape, TS_REW_INIT);
	s390irq_spin_unlock_irqrestore (tape->devinfo.irq, lockflags);
#ifdef TAPE_DEBUG
	debug_text_event (tape_debug_area,6,"xrew ccwg");
#endif /* TAPE_DEBUG */
	return cqr;
}

/*
 * MTOFFL: Rewind the tape and put the drive off-line.
 * Implement 'rewind unload'
 */
ccw_req_t *
tape34xx_mtoffl (tape_info_t * tape, int count)
{
	long lockflags;
	ccw_req_t *cqr;
	ccw1_t *ccw;
	cqr = tape_alloc_ccw_req (tape, 3, 32);
	if (!cqr) {
#ifdef TAPE_DEBUG
	        debug_text_exception (tape_debug_area,6,"xoff nomem");
#endif /* TAPE_DEBUG */
		return NULL;
	}
	ccw = cqr->cpaddr;
	ccw->cmd_code = MODE_SET_DB;
	ccw->flags = CCW_FLAG_CC;
	ccw->count = 1;
	set_normalized_cda (ccw, (unsigned long) (&(((tape34xx_disc_data_t *) tape->discdata)->modeset_byte)));
	ccw++;
	ccw->cmd_code = REWIND_UNLOAD;
	ccw->flags = CCW_FLAG_CC;
	ccw->count = 1;
	ccw->cda = (unsigned long) (&(ccw->cmd_code));
	ccw++;
	ccw->cmd_code = SENSE;
	ccw->flags = 0;
	ccw->count = 32;
	ccw->cda = (unsigned long) cqr->cpaddr;
	s390irq_spin_lock_irqsave (tape->devinfo.irq, lockflags);
	tape->kernbuf = NULL;
	tape->userbuf = NULL;
	tapestate_set (tape, TS_RUN_INIT);
	s390irq_spin_unlock_irqrestore (tape->devinfo.irq, lockflags);
#ifdef TAPE_DEBUG
	debug_text_event (tape_debug_area,6,"xoff ccwg");
#endif /* TAPE_DEBUG */
	return cqr;
}

/*
 * MTNOP: 'No operation'.
 */
ccw_req_t *
tape34xx_mtnop (tape_info_t * tape, int count)
{
	long lockflags;
	ccw_req_t *cqr;
	ccw1_t *ccw;
	cqr = tape_alloc_ccw_req (tape, 1, 0);
	if (!cqr) {
#ifdef TAPE_DEBUG
	        debug_text_exception (tape_debug_area,6,"xnop nomem");
#endif /* TAPE_DEBUG */
		return NULL;
	}
	ccw = cqr->cpaddr;
	ccw->cmd_code = NOP;
	ccw->flags = 0;
	ccw->count = 0;
	ccw->cda = (unsigned long) ccw->cmd_code;
	s390irq_spin_lock_irqsave (tape->devinfo.irq, lockflags);
	tape->kernbuf = NULL;
	tape->userbuf = NULL;
	tapestate_set (tape, TS_NOP_INIT);
	s390irq_spin_unlock_irqrestore (tape->devinfo.irq, lockflags);
#ifdef TAPE_DEBUG
	debug_text_event (tape_debug_area,6,"xnop ccwg");
#endif /* TAPE_DEBUG */
	return cqr;
}

/*
 * MTBSFM: Backward space over 'count' file marks.
 * The tape is positioned at the BOT (Begin Of Tape) side of the
 * last skipped file mark.
 */
ccw_req_t *
tape34xx_mtbsfm (tape_info_t * tape, int count)
{
	long lockflags;
	int i;
	ccw_req_t *cqr;
	ccw1_t *ccw;
	if ((count == 0) || (count > 510)) {
#ifdef TAPE_DEBUG
	        debug_text_exception (tape_debug_area,6,"xbsm parm");
#endif /* TAPE_DEBUG */
		return NULL;
	}
	cqr = tape_alloc_ccw_req (tape, 2 + count, 0);
	if (!cqr) {
#ifdef TAPE_DEBUG
	        debug_text_exception (tape_debug_area,6,"xbsm nomem");
#endif /* TAPE_DEBUG */
		return NULL;
	}
	ccw = cqr->cpaddr;
	ccw->cmd_code = MODE_SET_DB;
	ccw->flags = CCW_FLAG_CC;
	ccw->count = 1;
	set_normalized_cda (ccw, (unsigned long) (&(((tape34xx_disc_data_t *) tape->discdata)->modeset_byte)));
	ccw++;
	for (i = 0; i < count; i++) {
		ccw->cmd_code = BACKSPACEFILE;
		ccw->flags = CCW_FLAG_CC;
		ccw->count = 0;
		ccw->cda = (unsigned long) (&(ccw->cmd_code));
		ccw++;
	}
	ccw->cmd_code = NOP;
	ccw->flags = 0;
	ccw->count = 0;
	ccw->cda = (unsigned long) (&(ccw->cmd_code));
	s390irq_spin_lock_irqsave (tape->devinfo.irq, lockflags);
	tape->kernbuf = NULL;
	tape->userbuf = NULL;
	tapestate_set (tape, TS_BSF_INIT);
	s390irq_spin_unlock_irqrestore (tape->devinfo.irq, lockflags);
#ifdef TAPE_DEBUG
	debug_text_event (tape_debug_area,6,"xbsm ccwg");
#endif /* TAPE_DEBUG */
	return cqr;
}

/*
 * MTFSFM: Forward space over 'count' file marks.
 * The tape is positioned at the BOT (Begin Of Tape) side
 * of the last skipped file mark.
 */
ccw_req_t *
tape34xx_mtfsfm (tape_info_t * tape, int count)
{
	long lockflags;
	int i;
	ccw_req_t *cqr;
	ccw1_t *ccw;
	if ((count == 0) || (count > 510)) {
#ifdef TAPE_DEBUG
	        debug_text_exception (tape_debug_area,6,"xfsm parm");
#endif /* TAPE_DEBUG */
		return NULL;
	}
	cqr = tape_alloc_ccw_req (tape, 2 + count, 0);
	if (!cqr) {
#ifdef TAPE_DEBUG
	        debug_text_exception (tape_debug_area,6,"xfsm nomem");
#endif /* TAPE_DEBUG */	    
		return NULL;
	}
	ccw = cqr->cpaddr;
	ccw->cmd_code = MODE_SET_DB;
	ccw->flags = CCW_FLAG_CC;
	ccw->count = 1;
	set_normalized_cda (ccw, (unsigned long) (&(((tape34xx_disc_data_t *) tape->discdata)->modeset_byte)));
	ccw++;
	for (i = 0; i < count; i++) {
		ccw->cmd_code = FORSPACEFILE;
		ccw->flags = CCW_FLAG_CC;
		ccw->count = 0;
		ccw->cda = (unsigned long) (&(ccw->cmd_code));
		ccw++;
	}
	ccw->cmd_code = NOP;
	ccw->flags = 0;
	ccw->count = 0;
	ccw->cda = (unsigned long) (&(ccw->cmd_code));
	s390irq_spin_lock_irqsave (tape->devinfo.irq, lockflags);
	tape->kernbuf = NULL;
	tape->userbuf = NULL;
	tapestate_set (tape, TS_FSF_INIT);
	s390irq_spin_unlock_irqrestore (tape->devinfo.irq, lockflags);
#ifdef TAPE_DEBUG
	debug_text_event (tape_debug_area,6,"xfsm ccwg");
#endif /* TAPE_DEBUG */
	return cqr;
}

/*
 * MTEOM: positions at the end of the portion of the tape already used
 * for recordind data. MTEOM positions after the last file mark, ready for
 * appending another file.
 * MTRETEN: Retension the tape, i.e. forward space to end of tape and rewind.
 */
ccw_req_t *
tape34xx_mteom (tape_info_t * tape, int count)
{
	long lockflags;
	ccw_req_t *cqr;
	ccw1_t *ccw;
	cqr = tape_alloc_ccw_req (tape, 4, 0);
	if (!cqr) {
#ifdef TAPE_DEBUG
	        debug_text_exception (tape_debug_area,6,"xeom nomem");
#endif /* TAPE_DEBUG */
		return NULL;
	}
	ccw = cqr->cpaddr;
	ccw->cmd_code = MODE_SET_DB;
	ccw->flags = CCW_FLAG_CC;
	ccw->count = 1;
	set_normalized_cda (ccw, (unsigned long) (&(((tape34xx_disc_data_t *) tape->discdata)->modeset_byte)));
	ccw++;
	ccw->cmd_code = FORSPACEFILE;
	ccw->flags = CCW_FLAG_CC;
	ccw->count = 0;
	ccw->cda = (unsigned long) (&(ccw->cmd_code));
	ccw++;
	ccw->cmd_code = NOP;
	ccw->flags = CCW_FLAG_CC;
	ccw->count = 0;
	ccw->cda = (unsigned long) (&(ccw->cmd_code));
	ccw++;
	ccw->cmd_code = CCW_CMD_TIC;
	ccw->flags = 0;
	ccw->count = 0;
	ccw->cda = (unsigned long) (cqr->cpaddr);
	s390irq_spin_lock_irqsave (tape->devinfo.irq, lockflags);
	tape->kernbuf = NULL;
	tape->userbuf = NULL;
	tapestate_set (tape, TS_FSF_INIT);
	s390irq_spin_unlock_irqrestore (tape->devinfo.irq, lockflags);
#ifdef TAPE_DEBUG
	debug_text_event (tape_debug_area,6,"xeom ccwg");
#endif /* TAPE_DEBUG */
	return cqr;
}

/*
 * MTERASE: erases the tape.
 */
ccw_req_t *
tape34xx_mterase (tape_info_t * tape, int count)
{
	long lockflags;
	ccw_req_t *cqr;
	ccw1_t *ccw;
	cqr = tape_alloc_ccw_req (tape, 5, 0);
	if (!cqr) {
#ifdef TAPE_DEBUG
	        debug_text_exception (tape_debug_area,6,"xera nomem");
#endif /* TAPE_DEBUG */
		return NULL;
	}
	ccw = cqr->cpaddr;
	ccw->cmd_code = MODE_SET_DB;
	ccw->flags = CCW_FLAG_CC;
	ccw->count = 1;
	set_normalized_cda (ccw, (unsigned long) (&(((tape34xx_disc_data_t *) tape->discdata)->modeset_byte)));
	ccw++;
	ccw->cmd_code = REWIND;
	ccw->flags = CCW_FLAG_CC;
	ccw->count = 0;
	ccw->cda = (unsigned long) (&(ccw->cmd_code));
	ccw++;
	ccw->cmd_code = ERASE_GAP;
	ccw->flags = CCW_FLAG_CC;
	ccw->count = 0;
	ccw->cda = (unsigned long) (&(ccw->cmd_code));
	ccw++;
	ccw->cmd_code = DATA_SEC_ERASE;
	ccw->flags = CCW_FLAG_CC;
	ccw->count = 0;
	ccw->cda = (unsigned long) (&(ccw->cmd_code));
	ccw++;
	ccw->cmd_code = NOP;
	ccw->flags = 0;
	ccw->count = 0;
	ccw->cda = (unsigned long) (&(ccw->cmd_code));
	s390irq_spin_lock_irqsave (tape->devinfo.irq, lockflags);
	tape->kernbuf = NULL;
	tape->userbuf = NULL;
	tapestate_set (tape, TS_DSE_INIT);
	s390irq_spin_unlock_irqrestore (tape->devinfo.irq, lockflags);
#ifdef TAPE_DEBUG
	debug_text_event (tape_debug_area,6,"xera ccwg");
#endif /* TAPE_DEBUG */
	return cqr;
}

/*
 * MTSETDENSITY: set tape density.
 */
ccw_req_t *
tape34xx_mtsetdensity (tape_info_t * tape, int count)
{
	long lockflags;
	ccw_req_t *cqr;
	ccw1_t *ccw;
	cqr = tape_alloc_ccw_req (tape, 2, 0);
	if (!cqr) {
#ifdef TAPE_DEBUG
	        debug_text_exception (tape_debug_area,6,"xden nomem");
#endif /* TAPE_DEBUG */
		return NULL;
	}
	ccw = cqr->cpaddr;
	ccw->cmd_code = MODE_SET_DB;
	ccw->flags = CCW_FLAG_CC;
	ccw->count = 1;
	set_normalized_cda (ccw, (unsigned long) (&(((tape34xx_disc_data_t *) tape->discdata)->modeset_byte)));
	ccw++;
	ccw->cmd_code = NOP;
	ccw->flags = 0;
	ccw->count = 0;
	ccw->cda = (unsigned long) (&(ccw->cmd_code));
	s390irq_spin_lock_irqsave (tape->devinfo.irq, lockflags);
	tape->kernbuf = NULL;
	tape->userbuf = NULL;
	tapestate_set (tape, TS_NOP_INIT);
	s390irq_spin_unlock_irqrestore (tape->devinfo.irq, lockflags);
#ifdef TAPE_DEBUG
	debug_text_event (tape_debug_area,6,"xden ccwg");
#endif /* TAPE_DEBUG */
	return cqr;
}

/*
 * MTSEEK: seek to the specified block.
 */
ccw_req_t *
tape34xx_mtseek (tape_info_t * tape, int count)
{
	long lockflags;
	__u8 *data;
	ccw_req_t *cqr;
	ccw1_t *ccw;
	if ((data = kmalloc (4 * sizeof (__u8), GFP_KERNEL)) == NULL) {
#ifdef TAPE_DEBUG
	        debug_text_exception (tape_debug_area,6,"xsee nomem");
#endif /* TAPE_DEBUG */
		return NULL;
	}
	data[0] = 0x01;
	data[1] = data[2] = data[3] = 0x00;
	if (count >= 4194304) {
#ifdef TAPE_DEBUG
	        debug_text_exception (tape_debug_area,6,"xsee parm");
#endif /* TAPE_DEBUG */
		kfree(data);
		return NULL;
	}
	if (((tape34xx_disc_data_t *) tape->discdata)->modeset_byte & 0x08)	// IDRC on

		data[1] = data[1] | 0x80;
	data[3] += count % 256;
	data[2] += (count / 256) % 256;
	data[1] += (count / 65536);
#ifdef TAPE_DEBUG
	debug_text_event (tape_debug_area,6,"xsee id:");
	debug_int_event (tape_debug_area,6,count);
#endif /* TAPE_DEBUG */
	cqr = tape_alloc_ccw_req (tape, 3, 0);
	if (!cqr) {
#ifdef TAPE_DEBUG
	        debug_text_exception (tape_debug_area,6,"xsee nomem");
#endif /* TAPE_DEBUG */
		kfree (data);
		return NULL;
	}
	ccw = cqr->cpaddr;
	ccw->cmd_code = MODE_SET_DB;
	ccw->flags = CCW_FLAG_CC;
	ccw->count = 1;
	set_normalized_cda (ccw, (unsigned long) (&(((tape34xx_disc_data_t *) tape->discdata)->modeset_byte)));
	ccw++;
	ccw->cmd_code = LOCATE;
	ccw->flags = CCW_FLAG_CC;
	ccw->count = 4;
	set_normalized_cda (ccw, (unsigned long) data);
	ccw++;
	ccw->cmd_code = NOP;
	ccw->flags = 0;
	ccw->count = 0;
	ccw->cda = (unsigned long) (&(ccw->cmd_code));
	s390irq_spin_lock_irqsave (tape->devinfo.irq, lockflags);
	tape->kernbuf = data;
	tape->userbuf = NULL;
	tapestate_set (tape, TS_LBL_INIT);
	s390irq_spin_unlock_irqrestore (tape->devinfo.irq, lockflags);
#ifdef TAPE_DEBUG
	debug_text_event (tape_debug_area,6,"xsee ccwg");
#endif /* TAPE_DEBUG */
	return cqr;
}

/*
 * MTTELL: Tell block. Return the number of block relative to current file.
 */
ccw_req_t *
tape34xx_mttell (tape_info_t * tape, int count)
{
	long lockflags;
	ccw_req_t *cqr;
	ccw1_t *ccw;
	void *mem;
	cqr = tape_alloc_ccw_req (tape, 2, 0);
	if (!cqr) {
#ifdef TAPE_DEBUG
	        debug_text_exception (tape_debug_area,6,"xtel nomem");
#endif /* TAPE_DEBUG */
		return NULL;
	}
	mem = kmalloc (8, GFP_KERNEL);
	if (!mem) {
		tape_free_request (cqr);
#ifdef TAPE_DEBUG
	        debug_text_exception (tape_debug_area,6,"xtel nomem");
#endif /* TAPE_DEBUG */
		return NULL;
	}
	ccw = cqr->cpaddr;
	ccw->cmd_code = MODE_SET_DB;
	ccw->flags = CCW_FLAG_CC;
	ccw->count = 1;
	set_normalized_cda (ccw, (unsigned long) (&(((tape34xx_disc_data_t *) tape->discdata)->modeset_byte)));
	ccw++;

	ccw->cmd_code = READ_BLOCK_ID;
	ccw->flags = 0;
	ccw->count = 8;
	set_normalized_cda (ccw, (unsigned long) mem);
	s390irq_spin_lock_irqsave (tape->devinfo.irq, lockflags);
	tape->kernbuf = mem;
	tape->userbuf = NULL;
	tapestate_set (tape, TS_RBI_INIT);
	s390irq_spin_unlock_irqrestore (tape->devinfo.irq, lockflags);
#ifdef TAPE_DEBUG
	debug_text_event (tape_debug_area,6,"xtel ccwg");
#endif /* TAPE_DEBUG */
	return cqr;
}

/*
 * MTSETDRVBUFFER: Set the tape drive buffer code to number.
 * Implement NOP.
 */
ccw_req_t *
tape34xx_mtsetdrvbuffer (tape_info_t * tape, int count)
{
	long lockflags;
	ccw_req_t *cqr;
	ccw1_t *ccw;
	cqr = tape_alloc_ccw_req (tape, 2, 0);
	if (!cqr) {
#ifdef TAPE_DEBUG
	        debug_text_exception (tape_debug_area,6,"xbuf nomem");
#endif /* TAPE_DEBUG */
		return NULL;
	}
	ccw = cqr->cpaddr;
	ccw->cmd_code = MODE_SET_DB;
	ccw->flags = CCW_FLAG_CC;
	ccw->count = 1;
	set_normalized_cda (ccw, (unsigned long) (&(((tape34xx_disc_data_t *) tape->discdata)->modeset_byte)));
	ccw++;
	ccw->cmd_code = NOP;
	ccw->flags = 0;
	ccw->count = 0;
	ccw->cda = (unsigned long) (&(ccw->cmd_code));
	s390irq_spin_lock_irqsave (tape->devinfo.irq, lockflags);
	tape->kernbuf = NULL;
	tape->userbuf = NULL;
	tapestate_set (tape, TS_NOP_INIT);
	s390irq_spin_unlock_irqrestore (tape->devinfo.irq, lockflags);
#ifdef TAPE_DEBUG
	debug_text_event (tape_debug_area,6,"xbuf ccwg");
#endif /* TAPE_DEBUG */
	return cqr;
}

/*
 * MTLOCK: Locks the tape drive door.
 * Implement NOP CCW command.
 */
ccw_req_t *
tape34xx_mtlock (tape_info_t * tape, int count)
{
	long lockflags;
	ccw_req_t *cqr;
	ccw1_t *ccw;
	cqr = tape_alloc_ccw_req (tape, 2, 0);
	if (!cqr) {
#ifdef TAPE_DEBUG
	        debug_text_exception (tape_debug_area,6,"xloc nomem");
#endif /* TAPE_DEBUG */
		return NULL;
	}
	ccw = cqr->cpaddr;
	ccw->cmd_code = MODE_SET_DB;
	ccw->flags = CCW_FLAG_CC;
	ccw->count = 1;
	set_normalized_cda (ccw, (unsigned long) (&(((tape34xx_disc_data_t *) tape->discdata)->modeset_byte)));
	ccw++;
	ccw->cmd_code = NOP;
	ccw->flags = 0;
	ccw->count = 0;
	ccw->cda = (unsigned long) (&(ccw->cmd_code));
	s390irq_spin_lock_irqsave (tape->devinfo.irq, lockflags);
	tape->kernbuf = NULL;
	tape->userbuf = NULL;
	tapestate_set (tape, TS_NOP_INIT);
	s390irq_spin_unlock_irqrestore (tape->devinfo.irq, lockflags);
#ifdef TAPE_DEBUG
	debug_text_event (tape_debug_area,6,"xloc ccwg");
#endif /* TAPE_DEBUG */
	return cqr;
}

/*
 * MTUNLOCK: Unlocks the tape drive door.
 * Implement the NOP CCW command.
 */
ccw_req_t *
tape34xx_mtunlock (tape_info_t * tape, int count)
{
	long lockflags;
	ccw_req_t *cqr;
	ccw1_t *ccw;
	cqr = tape_alloc_ccw_req (tape, 2, 0);
	if (!cqr) {
#ifdef TAPE_DEBUG
	        debug_text_exception (tape_debug_area,6,"xulk nomem");
#endif /* TAPE_DEBUG */
		return NULL;
	}
	ccw = cqr->cpaddr;
	ccw->cmd_code = MODE_SET_DB;
	ccw->flags = CCW_FLAG_CC;
	ccw->count = 1;
	set_normalized_cda (ccw, (unsigned long) (&(((tape34xx_disc_data_t *) tape->discdata)->modeset_byte)));
	ccw++;
	ccw->cmd_code = NOP;
	ccw->flags = 0;
	ccw->count = 0;
	ccw->cda = (unsigned long) (&(ccw->cmd_code));
	s390irq_spin_lock_irqsave (tape->devinfo.irq, lockflags);
	tape->kernbuf = NULL;
	tape->userbuf = NULL;
	tapestate_set (tape, TS_NOP_INIT);
	s390irq_spin_unlock_irqrestore (tape->devinfo.irq, lockflags);
#ifdef TAPE_DEBUG
	debug_text_event (tape_debug_area,6,"xulk ccwg");
#endif /* TAPE_DEBUG */
	return cqr;
}

/*
 * MTLOAD: Loads the tape.
 * Implement the NOP CCW command.
 */
ccw_req_t *
tape34xx_mtload (tape_info_t * tape, int count)
{
	long lockflags;
	ccw_req_t *cqr;
	ccw1_t *ccw;
	cqr = tape_alloc_ccw_req (tape, 2, 0);
	if (!cqr) {
#ifdef TAPE_DEBUG
	        debug_text_exception (tape_debug_area,6,"xloa nomem");
#endif /* TAPE_DEBUG */
		return NULL;
	}
	ccw = cqr->cpaddr;
	ccw->cmd_code = MODE_SET_DB;
	ccw->flags = CCW_FLAG_CC;
	ccw->count = 1;
	set_normalized_cda (ccw, (unsigned long) (&(((tape34xx_disc_data_t *) tape->discdata)->modeset_byte)));
	ccw++;
	ccw->cmd_code = NOP;
	ccw->flags = 0;
	ccw->count = 0;
	ccw->cda = (unsigned long) (&(ccw->cmd_code));
	s390irq_spin_lock_irqsave (tape->devinfo.irq, lockflags);
	tape->kernbuf = NULL;
	tape->userbuf = NULL;
	tapestate_set (tape, TS_NOP_INIT);
	s390irq_spin_unlock_irqrestore (tape->devinfo.irq, lockflags);
#ifdef TAPE_DEBUG
	debug_text_event (tape_debug_area,6,"xloa ccwg");
#endif /* TAPE_DEBUG */
	return cqr;
}

/*
 * MTUNLOAD: Rewind the tape and unload it.
 */
ccw_req_t *
tape34xx_mtunload (tape_info_t * tape, int count)
{
	long lockflags;
	ccw_req_t *cqr;
	ccw1_t *ccw;
	cqr = tape_alloc_ccw_req (tape, 3, 32);
	if (!cqr) {
#ifdef TAPE_DEBUG
	        debug_text_exception (tape_debug_area,6,"xunl nomem");
#endif /* TAPE_DEBUG */
		return NULL;
	}
	ccw = cqr->cpaddr;
	ccw->cmd_code = MODE_SET_DB;
	ccw->flags = CCW_FLAG_CC;
	ccw->count = 1;
	set_normalized_cda (ccw, (unsigned long) (&(((tape34xx_disc_data_t *) tape->discdata)->modeset_byte)));
	ccw++;
	ccw->cmd_code = REWIND_UNLOAD;
	ccw->flags = CCW_FLAG_CC;
	ccw->count = 1;
	ccw->cda = (unsigned long) (&(ccw->cmd_code));
	ccw++;
	ccw->cmd_code = SENSE;
	ccw->flags = 0;
	ccw->count = 32;
	ccw->cda = (unsigned long) cqr->cpaddr;
	s390irq_spin_lock_irqsave (tape->devinfo.irq, lockflags);
	tape->kernbuf = NULL;
	tape->userbuf = NULL;
	tapestate_set (tape, TS_RUN_INIT);
	s390irq_spin_unlock_irqrestore (tape->devinfo.irq, lockflags);
#ifdef TAPE_DEBUG
	debug_text_event (tape_debug_area,6,"xunl ccwg");
#endif /* TAPE_DEBUG */
	return cqr;
}

/*
 * MTCOMPRESSION: used to enable compression.
 * Sets the IDRC on/off.
 */
ccw_req_t *
tape34xx_mtcompression (tape_info_t * tape, int count)
{
	long lockflags;
	ccw_req_t *cqr;
	ccw1_t *ccw;
	if ((count < 0) || (count > 1)) {
#ifdef TAPE_DEBUG
	        debug_text_exception (tape_debug_area,6,"xcom parm");
#endif /* TAPE_DEBUG */
		return NULL;
	}
	if (count == 0)
		((tape34xx_disc_data_t *) tape->discdata)->modeset_byte = 0x00;		// IDRC off

	else
		((tape34xx_disc_data_t *) tape->discdata)->modeset_byte = 0x08;		// IDRC on

	cqr = tape_alloc_ccw_req (tape, 2, 0);
	if (!cqr) {
#ifdef TAPE_DEBUG
	        debug_text_exception (tape_debug_area,6,"xcom nomem");
#endif /* TAPE_DEBUG */
		return NULL;
	}
	ccw = cqr->cpaddr;
	ccw->cmd_code = MODE_SET_DB;
	ccw->flags = CCW_FLAG_CC;
	ccw->count = 1;
	set_normalized_cda (ccw, (unsigned long) (&(((tape34xx_disc_data_t *) tape->discdata)->modeset_byte)));
	ccw++;
	ccw->cmd_code = NOP;
	ccw->flags = 0;
	ccw->count = 0;
	ccw->cda = (unsigned long) (&(ccw->cmd_code));
	s390irq_spin_lock_irqsave (tape->devinfo.irq, lockflags);
	tape->kernbuf = NULL;
	tape->userbuf = NULL;
	tapestate_set (tape, TS_NOP_INIT);
	s390irq_spin_unlock_irqrestore (tape->devinfo.irq, lockflags);
#ifdef TAPE_DEBUG
	debug_text_event (tape_debug_area,6,"xcom ccwg");
#endif /* TAPE_DEBUG */
	return cqr;
}

/*
 * MTSTPART: Move the tape head at the partition with the number 'count'.
 * Implement the NOP CCW command.
 */
ccw_req_t *
tape34xx_mtsetpart (tape_info_t * tape, int count)
{
	long lockflags;
	ccw_req_t *cqr;
	ccw1_t *ccw;
	cqr = tape_alloc_ccw_req (tape, 2, 0);
	if (!cqr) {
#ifdef TAPE_DEBUG
	        debug_text_exception (tape_debug_area,6,"xspa nomem");
#endif /* TAPE_DEBUG */	    
		return NULL;
	}
	ccw = cqr->cpaddr;
	ccw->cmd_code = MODE_SET_DB;
	ccw->flags = CCW_FLAG_CC;
	ccw->count = 1;
	set_normalized_cda (ccw, (unsigned long) (&(((tape34xx_disc_data_t *) tape->discdata)->modeset_byte)));
	ccw++;
	ccw->cmd_code = NOP;
	ccw->flags = 0;
	ccw->count = 0;
	ccw->cda = (unsigned long) (&(ccw->cmd_code));
	s390irq_spin_lock_irqsave (tape->devinfo.irq, lockflags);
	tape->kernbuf = NULL;
	tape->userbuf = NULL;
	tapestate_set (tape, TS_NOP_INIT);
	s390irq_spin_unlock_irqrestore (tape->devinfo.irq, lockflags);
#ifdef TAPE_DEBUG
	debug_text_event (tape_debug_area,6,"xspa ccwg");
#endif /* TAPE_DEBUG */
	return cqr;
}

/*
 * MTMKPART: .... dummy .
 * Implement the NOP CCW command.
 */
ccw_req_t *
tape34xx_mtmkpart (tape_info_t * tape, int count)
{
	long lockflags;
	ccw_req_t *cqr;
	ccw1_t *ccw;
	cqr = tape_alloc_ccw_req (tape, 2, 0);
	if (!cqr) {
#ifdef TAPE_DEBUG
	        debug_text_exception (tape_debug_area,6,"xnpa nomem");
#endif /* TAPE_DEBUG */
		return NULL;
	}
	ccw = cqr->cpaddr;
	ccw->cmd_code = MODE_SET_DB;
	ccw->flags = CCW_FLAG_CC;
	ccw->count = 1;
	set_normalized_cda (ccw, (unsigned long) (&(((tape34xx_disc_data_t *) tape->discdata)->modeset_byte)));
	ccw++;
	ccw->cmd_code = NOP;
	ccw->flags = 0;
	ccw->count = 0;
	ccw->cda = (unsigned long) (&(ccw->cmd_code));
	s390irq_spin_lock_irqsave (tape->devinfo.irq, lockflags);
	tape->kernbuf = NULL;
	tape->userbuf = NULL;
	tapestate_set (tape, TS_NOP_INIT);
	s390irq_spin_unlock_irqrestore (tape->devinfo.irq, lockflags);
#ifdef TAPE_DEBUG
	debug_text_event (tape_debug_area,6,"xnpa ccwg");
#endif /* TAPE_DEBUG */
	return cqr;
}

/*
 * MTIOCGET: query the tape drive status.
 */
ccw_req_t *
tape34xx_mtiocget (tape_info_t * tape, int count)
{
	return NULL;
}

/*
 * MTIOCPOS: query the tape position.
 */
ccw_req_t *
tape34xx_mtiocpos (tape_info_t * tape, int count)
{
	return NULL;
}

ccw_req_t * tape34xx_bread (struct request *req,tape_info_t* tape,int tapeblock_major) {
	ccw_req_t *cqr;
	ccw1_t *ccw;
	__u8 *data;
	int s2b = blksize_size[tapeblock_major][tape->blk_minor]/hardsect_size[tapeblock_major][tape->blk_minor];
	int realcount;
	int size,bhct = 0;
	struct buffer_head* bh;
	for (bh = req->bh; bh; bh = bh->b_reqnext) {
		if (bh->b_size > blksize_size[tapeblock_major][tape->blk_minor])
			for (size = 0; size < bh->b_size; size += blksize_size[tapeblock_major][tape->blk_minor])
				bhct++;
		else
			bhct++;
	}
	if ((data = kmalloc (4 * sizeof (__u8), GFP_KERNEL)) == NULL) {
#ifdef TAPE_DEBUG
	        debug_text_exception (tape_debug_area,3,"xBREDnomem");
#endif /* TAPE_DEBUG */
		return NULL;
	}
	data[0] = 0x01;
	data[1] = data[2] = data[3] = 0x00;
	realcount=req->sector/s2b;
	if (((tape34xx_disc_data_t *) tape->discdata)->modeset_byte & 0x08)	// IDRC on

		data[1] = data[1] | 0x80;
	data[3] += realcount % 256;
	data[2] += (realcount / 256) % 256;
	data[1] += (realcount / 65536);
#ifdef TAPE_DEBUG
	debug_text_event (tape_debug_area,6,"xBREDid:");
	debug_int_event (tape_debug_area,6,realcount);
#endif /* TAPE_DEBUG */
	cqr = tape_alloc_ccw_req (tape, 2+bhct+1, 0);
	if (!cqr) {
#ifdef TAPE_DEBUG
	        debug_text_exception (tape_debug_area,6,"xBREDnomem");
#endif /* TAPE_DEBUG */
		kfree(data);
		return NULL;
	}
	ccw = cqr->cpaddr;
	ccw->cmd_code = MODE_SET_DB;
	ccw->flags = CCW_FLAG_CC;
	ccw->count = 1;
	set_normalized_cda (ccw, (unsigned long) (&(((tape34xx_disc_data_t *) tape->discdata)->modeset_byte)));
	if (realcount!=tape->position) {
	    ccw++;
	    ccw->cmd_code = LOCATE;
	    ccw->flags = CCW_FLAG_CC;
	    ccw->count = 4;
	    set_normalized_cda (ccw, (unsigned long) data);
	}
	tape->position=realcount+req->nr_sectors/s2b;
	for (bh=req->bh;bh!=NULL;) {
	        ccw->flags = CCW_FLAG_CC;
		if (bh->b_size >= blksize_size[tapeblock_major][tape->blk_minor]) {
			for (size = 0; size < bh->b_size; size += blksize_size[tapeblock_major][tape->blk_minor]) {
			        ccw++;
				ccw->flags = CCW_FLAG_CC;
				ccw->cmd_code = READ_FORWARD;
				ccw->count = blksize_size[tapeblock_major][tape->blk_minor];
				set_normalized_cda (ccw, __pa (bh->b_data + size));
			}
			bh = bh->b_reqnext;
		} else {	/* group N bhs to fit into byt_per_blk */
			for (size = 0; bh != NULL && size < blksize_size[tapeblock_major][tape->blk_minor];) {
				ccw++;
				ccw->flags = CCW_FLAG_DC;
				ccw->cmd_code = READ_FORWARD;
				ccw->count = bh->b_size;
				set_normalized_cda (ccw, __pa (bh->b_data));
				size += bh->b_size;
				bh = bh->b_reqnext;
			}
			if (size != blksize_size[tapeblock_major][tape->blk_minor]) {
				PRINT_WARN ("Cannot fulfill small request %d vs. %d (%ld sects)\n",
					    size,
					    blksize_size[tapeblock_major][tape->blk_minor],
					    req->nr_sectors);

				tape_free_request (cqr);
				kfree(data);
				return NULL;
			}
		}
	}
	ccw -> flags &= ~(CCW_FLAG_DC);
	ccw -> flags |= (CCW_FLAG_CC);
	ccw++;
	ccw->cmd_code = NOP;
	ccw->flags = 0;
	ccw->count = 0;
	ccw->cda = (unsigned long) (&(ccw->cmd_code));
	tape->kernbuf = data;
	tape->userbuf = NULL;
	tapestate_set (tape, TS_BLOCK_INIT);
#ifdef TAPE_DEBUG
	debug_text_event (tape_debug_area,6,"xBREDccwg");
#endif /* TAPE_DEBUG */
	return cqr;
}
void tape34xx_free_bread (ccw_req_t* cqr,struct _tape_info_t* tape) {
    ccw1_t* ccw;
    for (ccw=(ccw1_t*)cqr->cpaddr;(ccw->flags & CCW_FLAG_CC)||(ccw->flags & CCW_FLAG_DC);ccw++) 
	if ((ccw->cmd_code == MODE_SET_DB) ||
	    (ccw->cmd_code == LOCATE) ||
	    (ccw->cmd_code == READ_FORWARD))
	    clear_normalized_cda(ccw);
    tape_free_request(cqr);
    kfree(tape->kernbuf);
    tape->kernbuf=NULL;
}

/* event handlers */
void
tape34xx_default_handler (tape_info_t * tape)
{
#ifdef TAPE_DEBUG
    debug_text_event (tape_debug_area,6,"xdefhandle");
#endif /* TAPE_DEBUG */
	tapestate_set (tape, TS_FAILED);
	PRINT_ERR ("TAPE34XX: An unexpected Unit Check occurred.\n");
	PRINT_ERR ("TAPE34XX: Please read Documentation/s390/TAPE and report it!\n");
	PRINT_ERR ("TAPE34XX: Current state is: %s",
		   (((tapestate_get (tape) < TS_SIZE) && (tapestate_get (tape) >= 0)) ?
		    state_verbose[tapestate_get (tape)] : "->UNKNOWN STATE<-"));
	tape_dump_sense (&tape->devstat);
	tape->rc = -EIO;
	tape->wanna_wakeup=1;
	wake_up_interruptible (&tape->wq);
}

void
tape34xx_unexpect_uchk_handler (tape_info_t * tape)
{
	if ((tape->devstat.ii.sense.data[0] == 0x40) &&
	    (tape->devstat.ii.sense.data[1] == 0x40) &&
	    (tape->devstat.ii.sense.data[3] == 0x43)) {
		// no tape in the drive
	        PRINT_INFO ("Drive %d not ready. No volume loaded.\n", tape->rew_minor / 2);
#ifdef TAPE_DEBUG
	        debug_text_event (tape_debug_area,3,"xuuh nomed");
#endif /* TAPE_DEBUG */
		tapestate_set (tape, TS_FAILED);
		tape->rc = -ENOMEDIUM;
		tape->wanna_wakeup=1;
		wake_up_interruptible (&tape->wq);
	} else if ((tape->devstat.ii.sense.data[0] == 0x42) &&
		   (tape->devstat.ii.sense.data[1] == 0x44) &&
		   (tape->devstat.ii.sense.data[3] == 0x3b)) {
       	        PRINT_INFO ("Media in drive %d was changed!\n",
			    tape->rew_minor / 2);
#ifdef TAPE_DEBUG
		debug_text_event (tape_debug_area,3,"xuuh medchg");
#endif
		/* nothing to do. chan end & dev end will be reported when io is finished */
	} else {
#ifdef TAPE_DEBUG
	        debug_text_event (tape_debug_area,3,"xuuh unexp");
	        debug_text_event (tape_debug_area,3,"state:");
	        debug_text_event (tape_debug_area,3,((tapestate_get (tape) < TS_SIZE) && 
						     (tapestate_get (tape) >= 0)) ?
				  state_verbose[tapestate_get (tape)] : 
				  "TS UNKNOWN");
#endif /* TAPE_DEBUG */
		tape34xx_default_handler (tape);
	}
}

void
tape34xx_unused_done (tape_info_t * tape)
{
	if ((tape->devstat.ii.sense.data[0] == 0x40) &&
	    (tape->devstat.ii.sense.data[1] == 0x40) &&
	    (tape->devstat.ii.sense.data[3] == 0x43)) {
	    // A medium was inserted in the drive!
#ifdef TAPE_DEBUG
	    debug_text_event (tape_debug_area,6,"xuud med");
#endif /* TAPE_DEBUG */
	} else {
#ifdef TAPE_DEBUG
	        debug_text_event (tape_debug_area,3,"unsol.irq!");
	        debug_text_event (tape_debug_area,3,"dev end");
	        debug_int_exception (tape_debug_area,3,tape->devinfo.irq);
#endif /* TAPE_DEBUG */
		PRINT_WARN ("Unsolicited IRQ (Device End) caught in unused state.\n");
		tape_dump_sense (&tape->devstat);
	}
}

void
tape34xx_unused_error (tape_info_t * tape)
{
#ifdef TAPE_DEBUG
    debug_text_event (tape_debug_area,3,"unsol.irq!");
    debug_text_event (tape_debug_area,3,"unit chk!");
    debug_int_exception (tape_debug_area,3,tape->devinfo.irq);
#endif /* TAPE_DEBUG */
    PRINT_WARN ("Unsolicited IRQ (Unit Check) caught in unused state.\n");
    tape_dump_sense (&tape->devstat);
}

void
tape34xx_idle_done (tape_info_t * tape)
{
#ifdef TAPE_DEBUG
        debug_text_event (tape_debug_area,3,"unsol.irq!");
        debug_text_event (tape_debug_area,3,"dev end");
        debug_int_exception (tape_debug_area,3,tape->devinfo.irq);
#endif /* TAPE_DEBUG */
	PRINT_WARN ("Unsolicited IRQ (Device End) caught in idle state.\n");
	tape_dump_sense (&tape->devstat);
}

void
tape34xx_idle_error (tape_info_t * tape)
{
#ifdef TAPE_DEBUG
        debug_text_event (tape_debug_area,3,"unsol.irq!");
	debug_text_event (tape_debug_area,3,"unit chk!");
	debug_int_exception (tape_debug_area,3,tape->devinfo.irq);
#endif /* TAPE_DEBUG */
	PRINT_WARN ("Unsolicited IRQ (Unit Check) caught in idle state.\n");
	tape_dump_sense (&tape->devstat);
}

void
tape34xx_block_done (tape_info_t * tape)
{
#ifdef TAPE_DEBUG
        debug_text_event (tape_debug_area,6,"x:bREQdone");
#endif /* TAPE_DEBUG */
	tapestate_set(tape,TS_DONE);
	schedule_tapeblock_exec_IO(tape);
}

void
tape34xx_block_error (tape_info_t * tape)
{
#ifdef TAPE_DEBUG
        debug_text_event (tape_debug_area,3,"x:xREQfail");
#endif /* TAPE_DEBUG */
	tapestate_set(tape,TS_FAILED);
	schedule_tapeblock_exec_IO(tape);
}

void
tape34xx_bsf_init_done (tape_info_t * tape)
{
#ifdef TAPE_DEBUG
        debug_text_event (tape_debug_area,6,"bsf done");
#endif
	tapestate_set (tape, TS_DONE);
	tape->rc = 0;
	tape->wanna_wakeup=1;
	wake_up_interruptible (&tape->wq);
}

void
tape34xx_dse_init_done (tape_info_t * tape)
{
#ifdef TAPE_DEBUG
        debug_text_event (tape_debug_area,6,"dse done");
#endif
	tapestate_set (tape, TS_DONE);
	tape->rc = 0;
	tape->wanna_wakeup=1;
	wake_up_interruptible (&tape->wq);
}

void
tape34xx_fsf_init_done (tape_info_t * tape)
{
#ifdef TAPE_DEBUG
        debug_text_event (tape_debug_area,6,"fsf done");
#endif
	tapestate_set (tape, TS_DONE);
	tape->rc = 0;
	tape->wanna_wakeup=1;
	wake_up_interruptible (&tape->wq);
}

void
tape34xx_fsf_init_error (tape_info_t * tape)
{
	if (((tape->devstat.ii.sense.data[0] == 0x08) ||	// sense.data[0]=08 -> first time
	      (tape->devstat.ii.sense.data[0] == 0x10) ||	// an alternate one...
	      (tape->devstat.ii.sense.data[0] == 0x12)) &&	// sense.data[1]=12 -> repeated message
	     (tape->devstat.ii.sense.data[1] == 0x40)) {
		// end of recorded area!
#ifdef TAPE_DEBUG
        debug_text_event (tape_debug_area,3,"fsf fail");
        debug_text_exception (tape_debug_area,3,"eoRecArea");
#endif	/* TAPE_DEBUG */
		tape->rc = -EIO;
		tapestate_set (tape, TS_FAILED);
		tape->wanna_wakeup=1;
		wake_up_interruptible (&tape->wq);
	} else {
		tape34xx_unexpect_uchk_handler (tape);
	}
}

void
tape34xx_fsb_init_done (tape_info_t * tape)
{
#ifdef TAPE_DEBUG
        debug_text_event (tape_debug_area,6,"fsb done");
#endif
	tapestate_set (tape, TS_DONE);
	tape->rc = 0;
	tape->wanna_wakeup=1;
	wake_up_interruptible (&tape->wq);
}

void
tape34xx_bsb_init_done (tape_info_t * tape)
{
#ifdef TAPE_DEBUG
        debug_text_event (tape_debug_area,6,"bsb done");
#endif
	tapestate_set (tape, TS_DONE);
	tape->rc = 0;
	tape->wanna_wakeup=1;
	wake_up_interruptible (&tape->wq);
}

void
tape34xx_lbl_init_done (tape_info_t * tape)
{
#ifdef TAPE_DEBUG
        debug_text_event (tape_debug_area,6,"lbl done");
#endif
	tapestate_set (tape, TS_DONE);
	tape->rc = 0;
	//s390irq_spin_unlock(tape->devinfo.irq);
	tape->wanna_wakeup=1;
	wake_up (&tape->wq);
}

void
tape34xx_lbl_init_error (tape_info_t * tape)
{
	if (((tape->devstat.ii.sense.data[0] == 0x00) ||	// sense.data[0]=00 -> first time
	      (tape->devstat.ii.sense.data[0] == 0x08) ||	// an alternate one...
	     (tape->devstat.ii.sense.data[0] == 0x10) ||        // alternate, too
	      (tape->devstat.ii.sense.data[0] == 0x12)) &&	// sense.data[1]=12 -> repeated message
	     ((tape->devstat.ii.sense.data[1] == 0x40) ||
	      (tape->devstat.ii.sense.data[1] == 0xc0))) {
		// block not found!
#ifdef TAPE_DEBUG
        debug_text_event (tape_debug_area,3,"lbl fail");
        debug_text_exception (tape_debug_area,3,"blk nfound");
#endif	/* TAPE_DEBUG */
		tape->rc = -EIO;
		tapestate_set (tape, TS_FAILED);
		tape->wanna_wakeup=1;
		wake_up_interruptible (&tape->wq);
	} else {
		tape34xx_unexpect_uchk_handler (tape);
	}
}

void
tape34xx_nop_init_done (tape_info_t * tape)
{
#ifdef TAPE_DEBUG
        debug_text_event (tape_debug_area,6,"nop done..");
        debug_text_exception (tape_debug_area,6,"or rew/rel");
#endif
	tapestate_set (tape, TS_DONE);
	tape->rc = 0;
	//s390irq_spin_unlock(tape->devinfo.irq);
	tape->wanna_wakeup=1;
	wake_up (&tape->wq);
}

void
tape34xx_rfo_init_done (tape_info_t * tape)
{
#ifdef TAPE_DEBUG
        debug_text_event (tape_debug_area,6,"rfo done");
#endif
	//BH: use irqsave
	//s390irq_spin_lock(tape->devinfo.irq);
	tapestate_set (tape, TS_DONE);
	tape->rc = 0;
	//s390irq_spin_unlock(tape->devinfo.irq);
	tape->wanna_wakeup=1;
	wake_up_interruptible (&tape->wq);
}

void
tape34xx_rbi_init_done (tape_info_t * tape)
{
	__u8 *data;
	int i;
	tapestate_set (tape, TS_FAILED);
	data = tape->kernbuf;
	tape->rc = data[3];
	tape->rc += 256 * data[2];
	tape->rc += 65536 * (data[1] & 0x3F);
#ifdef TAPE_DEBUG
        debug_text_event (tape_debug_area,6,"rbi done");
        debug_text_event (tape_debug_area,6,"data:");
	for (i=0;i<8;i++)
	    debug_int_event (tape_debug_area,6,data[i]);
#endif
	tape->wanna_wakeup=1;
	wake_up_interruptible (&tape->wq);
}

void
tape34xx_rfo_init_error (tape_info_t * tape)
{
	if (((tape->devstat.ii.sense.data[0] == 0x08) ||	// sense.data[0]=08 -> first time
	      (tape->devstat.ii.sense.data[0] == 0x10) ||	// an alternate one...
	      (tape->devstat.ii.sense.data[0] == 0x12)) &&	// sense.data[1]=12 -> repeated message
	     (tape->devstat.ii.sense.data[1] == 0x40)) {
		// end of recorded area!
#ifdef TAPE_DEBUG
        debug_text_event (tape_debug_area,3,"rfo fail");
        debug_text_exception (tape_debug_area,3,"eoRecArea");
#endif	/* TAPE_DEBUG */
		tape->rc = 0;
		tapestate_set (tape, TS_FAILED);
		tape->wanna_wakeup=1;
		wake_up_interruptible (&tape->wq);
	} else {
		switch (tape->devstat.ii.sense.data[3]) {
		case 0x48:
#ifdef TAPE_DEBUG
		        debug_text_event (tape_debug_area,3,"rfo fail");
                        debug_text_exception (tape_debug_area,3,"recov x48");
#endif	/* TAPE_DEBUG */
			//s390irq_spin_lock(tape->devinfo.irq);
			do_IO (tape->devinfo.irq, tape->cqr->cpaddr, (unsigned long) (tape->cqr), 0x00, tape->cqr->options);
			//s390irq_spin_unlock(tape->devinfo.irq);
			break;
		case 0x2c:
			PRINT_ERR ("TAPE: Permanent Unit Check. Please check your hardware!");
#ifdef TAPE_DEBUG
			debug_text_event (tape_debug_area,3,"rfo fail");
			debug_text_exception (tape_debug_area,3,"Perm UCK");
#endif
			tape->rc = -EIO;
			tapestate_set (tape, TS_FAILED);
			tape->wanna_wakeup=1;
			wake_up_interruptible (&tape->wq);
			break;
		default:
			tape34xx_unexpect_uchk_handler (tape);
		}
	}
}

void
tape34xx_rew_init_done (tape_info_t * tape)
{
#ifdef TAPE_DEBUG
        debug_text_event (tape_debug_area,6,"rew done");
#endif
	//BH: use irqsave
	//s390irq_spin_lock(tape->devinfo.irq);
	tapestate_set (tape, TS_DONE);
	tape->rc = 0;
	//s390irq_spin_unlock(tape->devinfo.irq);
	tape->wanna_wakeup=1;
	wake_up_interruptible (&tape->wq);
}

void
tape34xx_rew_release_init_error (tape_info_t * tape)
{
	if ((tape->devstat.ii.sense.data[0] == 0x40) &&
	    (tape->devstat.ii.sense.data[1] == 0x40) &&
	    (tape->devstat.ii.sense.data[3] == 0x43)) {
		// no tape in the drive
		PRINT_INFO ("Drive %d not ready. No volume loaded.\n", tape->rew_minor / 2);
#ifdef TAPE_DEBUG
		debug_text_event (tape_debug_area,3,"rewR fail");
		debug_text_exception (tape_debug_area,3,"no medium");
#endif
		tapestate_set (tape, TS_FAILED);
		tape->rc = -ENOMEDIUM;
		tape->wanna_wakeup=1;
		wake_up (&tape->wq);
	} else {
		PRINT_ERR ("TAPE34XX: An unexpected Unit Check occurred.\n");
		PRINT_ERR ("TAPE34XX: Please send the following 20 lines of output to cotte@de.ibm.com\n");
		PRINT_ERR ("TAPE34XX: Current state is: %s",
			   (((tapestate_get (tape) < TS_SIZE) && (tapestate_get (tape) >= 0)) ?
		  state_verbose[tapestate_get (tape)] : "->UNKNOWN STATE<-"));
		tapestate_set (tape, TS_FAILED);
#ifdef TAPE_DEBUG
	        debug_text_event (tape_debug_area,3,"rewR unexp");
	        debug_text_event (tape_debug_area,3,"state:");
	        debug_text_event (tape_debug_area,3,((tapestate_get (tape) < TS_SIZE) && 
						     (tapestate_get (tape) >= 0)) ?
				  state_verbose[tapestate_get (tape)] : 
				  "TS UNKNOWN");
#endif /* TAPE_DEBUG */
		tape_dump_sense (&tape->devstat);
		tape->rc = -EIO;
		tape->wanna_wakeup=1;
		wake_up (&tape->wq);
	}
}

void
tape34xx_rew_release_init_done (tape_info_t * tape)
{
#ifdef TAPE_DEBUG
        debug_text_event (tape_debug_area,6,"rewR done");
#endif
	tapestate_set (tape, TS_DONE);
	tape->rc = 0;
	//s390irq_spin_unlock(tape->devinfo.irq);
	tape->wanna_wakeup=1;
	wake_up (&tape->wq);
}

void
tape34xx_rew_init_error (tape_info_t * tape)
{
	tape34xx_unexpect_uchk_handler (tape);
}

void
tape34xx_run_init_done (tape_info_t * tape)
{
#ifdef TAPE_DEBUG
        debug_text_event (tape_debug_area,6,"rew done");
#endif
	tapestate_set (tape, TS_DONE);
	tape->rc = 0;
	tape->wanna_wakeup=1;
	wake_up_interruptible (&tape->wq);
}

void
tape34xx_run_init_error (tape_info_t * tape)
{

	switch (tape->devstat.ii.sense.data[3]) {
	case 0x52:
	        // This error is fine for rewind and unload
	        // It reports that no volume is loaded... 
#ifdef TAPE_DEBUG
	        debug_text_event (tape_debug_area,6,"run done");
#endif	/* TAPE_DEBUG */
		tapestate_set (tape, TS_DONE);
		tape->rc = 0;
		tape->wanna_wakeup=1;
		wake_up_interruptible (&tape->wq);
		break;
	default:
		tape34xx_unexpect_uchk_handler (tape);
	}
}

void
tape34xx_wri_init_done (tape_info_t * tape)
{
#ifdef TAPE_DEBUG
        debug_text_event (tape_debug_area,6,"wri done");
#endif
	//BH: use irqsave
	//s390irq_spin_lock(tape->devinfo.irq);
	tapestate_set (tape, TS_DONE);
	tape->rc = 0;
	//s390irq_spin_unlock(tape->devinfo.irq);
	tape->wanna_wakeup=1;
	wake_up_interruptible (&tape->wq);
}

void
tape34xx_wri_init_error (tape_info_t * tape)
{
    if ((tape->devstat.ii.sense.data[0]==0x80)&&(tape->devstat.ii.sense.data[1]==0x4a)) {
	// tape is write protected
#ifdef TAPE_DEBUG
        debug_text_event (tape_debug_area,3,"wri fail");
        debug_text_exception (tape_debug_area,3,"writProte");
#endif	/* TAPE_DEBUG */
	tape->rc = -EACCES;
	tapestate_set (tape, TS_FAILED);
	tape->wanna_wakeup=1;
	wake_up_interruptible (&tape->wq);
    } else {
	switch (tape->devstat.ii.sense.data[3]) {
	case 0x48:
#ifdef TAPE_DEBUG
	        debug_text_event (tape_debug_area,3,"wri fail");
		debug_text_exception (tape_debug_area,3,"recov x48");
#endif	/* TAPE_DEBUG */
		//s390irq_spin_lock(tape->devinfo.irq);
		do_IO (tape->devinfo.irq, tape->cqr->cpaddr, (unsigned long) (tape->cqr), 0x00, tape->cqr->options);
		//s390irq_spin_unlock(tape->devinfo.irq);
		break;
	case 0x2c:
		PRINT_ERR ("TAPE: Permanent Unit Check. Please check your hardware!\n");
#ifdef TAPE_DEBUG
		debug_text_event (tape_debug_area,3,"wri fail");
		debug_text_exception (tape_debug_area,3,"Perm UCK");
#endif
		tape->rc = -EIO;
		tapestate_set (tape, TS_FAILED);
		tape->wanna_wakeup=1;
		wake_up_interruptible (&tape->wq);
		break;
	case 0x38:		//end of tape
#ifdef TAPE_DEBUG
		PRINT_WARN ("TAPE: End of Tape reached.\n");
		debug_text_event (tape_debug_area,3,"wri fail");
		debug_text_exception (tape_debug_area,3,"EOT!");
#endif
		tape->rc = tape->devstat.rescnt;
		tapestate_set (tape, TS_FAILED);
		tape->wanna_wakeup=1;
		wake_up_interruptible (&tape->wq);
		break;
	default:
		tape34xx_unexpect_uchk_handler (tape);
	}
    }
}

void
tape34xx_wtm_init_done (tape_info_t * tape)
{
#ifdef TAPE_DEBUG
        debug_text_event (tape_debug_area,3,"wtm done");
#endif
	tapestate_set (tape, TS_DONE);
	tape->rc = 0;
	tape->wanna_wakeup=1;
	wake_up_interruptible (&tape->wq);
}

void
tape34xx_wtm_init_error (tape_info_t * tape)
{
        tape34xx_unexpect_uchk_handler (tape);
    
}
