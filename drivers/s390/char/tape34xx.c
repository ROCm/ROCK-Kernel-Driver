/***************************************************************************
 *
 *  drivers/s390/char/tape34xx.c
 *    common tape device discipline for 34xx tapes.
 *
 *  S390 and zSeries version
 *    Copyright (C) 2001 IBM Corporation
 *    Author(s): Carsten Otte <cotte@de.ibm.com>
 *               Michael Holzheu <holzheu@de.ibm.com>
 *               Tuan Ngo-Anh <ngoanh@de.ibm.com>
 *
 ****************************************************************************
 */

#include "tapedefs.h"
#include <linux/config.h>
#include <linux/version.h>
#include <linux/stddef.h>
#include <linux/kernel.h>
#include <asm/types.h>
#include <linux/compatmac.h>
#include "tape.h"
#include "tape34xx.h"
#include <asm/idals.h>
#include <asm/ebcdic.h>
#include <asm/tape390.h>

#define PRINTK_HEADER "T3xxx:"

/*
 * Done Handler is called when dev stat = DEVICE-END (successfull operation)
 */

void tape34xx_done_handler(tape_dev_t* td)
{
	__u8 *data = NULL;
	int i;
	tape_ccw_req_t *treq = tape_get_active_ccw_req(td);

	tape_sprintf_event (tape_dbf_area,6,"%s\n",tape_op_verbose[treq->op]);
	tape_sprintf_event (tape_dbf_area,6,"done\n");

	treq->rc = 0;
	switch(treq->op){
		case TO_BSB:
		case TO_BSF:
		case TO_DSE:
		case TO_FSB:
		case TO_FSF:
		case TO_LBL:
		case TO_RFO:
		case TO_RBA:
		case TO_REW:
		case TO_WRI:
		case TO_WTM: 
		case TO_BLOCK:
		case TO_LOAD:
	        case TO_DIS:
			tape_med_state_set(td,MS_LOADED);
			break;
		case TO_NOP:
			break;
		case TO_RUN:
			tape_med_state_set(td,MS_UNLOADED);
                        break;
		case TO_RBI:
			data = treq->kernbuf;
			tape_sprintf_event (tape_dbf_area,6,"data: %04x %04x\n",*((unsigned int*)&data[0]),*((unsigned int*)&data[4]));
			i = 0;
			i = data[3];
			i += 256 * data[2];
			i += 65536 * (data[1] & 0x3F);
			memcpy(data,&i,4);
			tape_med_state_set(td,MS_LOADED);
			break;
		default:
			tape_sprintf_exception (tape_dbf_area,3,"TE UNEXPEC\n");
			tape34xx_default_handler (td);
			return;
	}
	
	if(treq->wakeup)
		treq->wakeup (treq);
	return;
}

/*
 * This function is called, when no request is outstanding and we get an
 * interrupt
 */

void tape34xx_unsolicited_irq(tape_dev_t* td)
{
	if(td->devstat.dstat == 0x85 /* READY */) {
		// A medium was inserted in the drive!
		tape_sprintf_event (tape_dbf_area,6,"xuud med\n");
		tape_med_state_set(td,MS_LOADED);
	} else {
		tape_sprintf_event (tape_dbf_area,3,"unsol.irq! dev end: %x\n",td->devinfo.irq);
		PRINT_WARN ("Unsolicited IRQ (Device End) caught.\n");
		tape_dump_sense (td);
        }
}


/*
 * tape34xx_display 
 */

int
tape34xx_display (tape_dev_t* td, unsigned long arg)
{
        struct display_struct d_struct;
        tape_ccw_req_t *treq = NULL;
        ccw1_t *ccw = NULL;
        int ds = 17;           /* datasize  */
        int ccw_cnt = 2;       /* ccw count */
        int op = TO_DIS;       /* tape operation */
        int i = 0, rc = -1;

        rc = copy_from_user(&d_struct, (char *)arg, sizeof(d_struct));
        if (rc != 0)
                goto error;

        treq=tape_alloc_ccw_req(ccw_cnt, ds, 0, op);
        if (!treq)
	        goto error;

        ((unsigned char *)treq->kernbuf)[0] = d_struct.cntrl;

        for (i = 0; i < 8; i++) {
                ((unsigned char *)treq->kernbuf)[i+1] = d_struct.message1[i];
                ((unsigned char *)treq->kernbuf)[i+9] = d_struct.message2[i];
        }

        ASCEBC (((unsigned char*)treq->kernbuf) + 1, 16);

        ccw = treq->cpaddr;
        ccw = tape_ccw_cc(ccw, LOAD_DISPLAY, 17, treq->kernbuf, 1);
        ccw = tape_ccw_end(ccw,NOP,0,0,1);

        tape_do_io_and_wait(td,treq,TAPE_WAIT_INTERRUPTIBLE);
    
        tape_free_ccw_req(treq);

        return(0);

 error:
	return -EINVAL;

}


/*
 * ioctl_overload 
 */

int
tape34xx_ioctl_overload (tape_dev_t* td, unsigned int cmd, unsigned long arg)
{
        if (cmd == TAPE390_DISPLAY)
	        return tape34xx_display(td, arg);
	else
                return -EINVAL;		// no additional ioctls
}


/*******************************************************************
 * Request creating functions:
 *******************************************************************/

/*
 * 34xx IOCTLS
 *
 * MTFSF: Forward space over 'count' file marks. The tape is positioned
 * at the EOT (End of Tape) side of the file mark.
 *
 * MTBSF: Backward space over 'count' file marks. The tape is positioned at
 * the EOT (End of Tape) side of the last skipped file mark.
 *
 * MTFSR: Forward space over 'count' tape blocks (blocksize is set
 * via MTSETBLK.
 *
 * MTBSR: Backward space over 'count' tape blocks.
 * (blocksize is set via MTSETBLK.
 *
 * MTWEOF: Write 'count' file marks at the current position.
 *
 * MTREW: Rewind the tape.
 *
 * MTOFFL: Rewind the tape and put the drive off-line.
 * Implement 'rewind unload'
 *
 * MTNOP: 'No operation'.
 *
 * MTBSFM: Backward space over 'count' file marks.
 * The tape is positioned at the BOT (Begin Of Tape) side of the
 * last skipped file mark.
 *
 * MTFSFM: Forward space over 'count' file marks.
 * The tape is positioned at the BOT (Begin Of Tape) side
 * of the last skipped file mark.
 *
 * MTEOM: positions at the end of the portion of the tape already used
 * for recordind data. MTEOM positions after the last file mark, ready for
 * appending another file.
 * MTRETEN: Retension the tape, i.e. forward space to end of tape and rewind.
 *
 * MTERASE: erases the tape.
 *
 * MTSETDENSITY: set tape density.
 *
 * MTSEEK: seek to the specified block.
 *
 * MTTELL: Tell block. Return the number of block relative to current file.
 *
 * MTSETDRVBUFFER: Set the tape drive buffer code to number.
 * Implement NOP.
 *
 * MTLOCK: Locks the tape drive door.
 * Implement NOP CCW command.
 *
 * MTUNLOCK: Unlocks the tape drive door.
 * Implement the NOP CCW command.
 *
 * MTLOAD: Loads the tape.
 * This function is not implemented and returns NULL, which causes the
 * Frontend to wait for a medium being loaded.
 * The 3480/3490 type Tapes do not support a load command
 *
 * MTUNLOAD: Rewind the tape and unload it.
 *
 * MTCOMPRESSION: used to enable compression.
 * Sets the IDRC on/off.
 *
 * MTSTPART: Move the tape head at the partition with the number 'count'.
 * Implement the NOP CCW command.
 *
 * MTMKPART: .... dummy .
 * Implement the NOP CCW command.
 *
 * MTIOCGET: query the tape drive status.
 *
 * MTIOCPOS: query the tape position.
 *
 */ 

tape_ccw_req_t *
tape34xx_ioctl(tape_dev_t* td, int mtcmd,int count, int* rc)
{
	tape_ccw_req_t *treq = NULL;
        ccw1_t *ccw = NULL;
	int ds = 0;   /* datasize  */
	int ccw_cnt;  /* ccw count */
	int op = -1;  /* tape operation */

	tape_sprintf_event(tape_dbf_area,6,"34xxioctl: op(%x) count(%x)\n",mtcmd,count);
	/* Preprocessing */

	switch(mtcmd){
		case MTLOAD:   *rc = -EINVAL; goto error;
		case MTIOCGET: *rc = -EINVAL; goto error;
		case MTIOCPOS: *rc = -EINVAL; goto error;

		case MTFSF:          op=TO_FSF; ccw_cnt=count+2; ds=0; break;
		case MTBSF:          op=TO_BSF; ccw_cnt=count+2; ds=0; break;
		case MTFSR:          op=TO_FSB; ccw_cnt=count+2; ds=0; break;
		case MTBSR:          op=TO_BSB; ccw_cnt=count+2; ds=0; break;
		case MTWEOF:         op=TO_WTM; ccw_cnt=count+2; ds=0; break;
		case MTREW:          op=TO_REW; ccw_cnt=3;       ds=0; break;
		case MTOFFL:         op=TO_RUN; ccw_cnt=3;       ds=0; break;
		case MTNOP:          op=TO_NOP; ccw_cnt=2;       ds=0; break; 
		case MTBSFM:         op=TO_BSF; ccw_cnt=count+2; ds=0; break;
		case MTFSFM:         op=TO_FSF; ccw_cnt=count+2; ds=0; break;
		case MTEOM:          op=TO_FSF; ccw_cnt=4;       ds=0; break;
		case MTERASE:        op=TO_DSE; ccw_cnt=5;       ds=0; break;
		case MTSETDENSITY:   op=TO_NOP; ccw_cnt=3;       ds=0; break;
		case MTSEEK:         op=TO_LBL; ccw_cnt=3;       ds=4; break;
		case MTTELL:         op=TO_RBI; ccw_cnt=3;       ds=8; break;
		case MTSETDRVBUFFER: op=TO_NOP; ccw_cnt=3;       ds=0; break;
		case MTLOCK:         op=TO_NOP; ccw_cnt=3;       ds=0; break;
		case MTUNLOCK:       op=TO_NOP; ccw_cnt=3;       ds=0; break;
		case MTUNLOAD:       op=TO_RUN; ccw_cnt=3;       ds=32; break;
		case MTCOMPRESSION:  op=TO_NOP; ccw_cnt=3;       ds=0; break;
		case MTSETPART:      op=TO_NOP; ccw_cnt=3;       ds=0; break;
		case MTMKPART:       op=TO_NOP; ccw_cnt=3;       ds=0; break;
		default:
			PRINT_ERR( "IOCTL %x not implemented\n",op );
			*rc = -EINVAL;
			goto error;
	}

	if (ccw_cnt > 510) {
		tape_sprintf_exception (tape_dbf_area,6,"wrng parm\n");
		*rc = -EINVAL;
		goto error;
	}

	treq=tape_alloc_ccw_req(ccw_cnt,ds,0,op);
        if (!treq){
		*rc = -ENOSPC;
                goto error;
	}
	ccw = treq->cpaddr;

	/* setup first ccw */
	ccw = tape_ccw_cc(ccw,MODE_SET_DB,1,&MOD_BYTE,1);

	/* setup middle ccw(s) */

	switch(mtcmd){
		case MTFSF:
                	ccw = tape_ccw_cc(ccw,FORSPACEFILE,0,0,count);
			break;
		case MTBSF:
			ccw = tape_ccw_cc(ccw,BACKSPACEFILE,0,0,count);
			break;
		case MTFSR:
			ccw = tape_ccw_cc(ccw,FORSPACEBLOCK,0,0,count);
			break;
		case MTBSR:
			ccw = tape_ccw_cc(ccw,BACKSPACEBLOCK,0,0,count);
			break;
		case MTWEOF:
			ccw = tape_ccw_cc(ccw,WRITETAPEMARK,0,0,count); // this operation does _always_ write only one tape mark :(
			break;
		case MTREW:
			ccw = tape_ccw_cc(ccw,REWIND,0,0,1);
			break;
		case MTOFFL:
			ccw = tape_ccw_cc(ccw,REWIND_UNLOAD,0,0,1);
			break;
		case MTUNLOCK:
		case MTLOCK:
		case MTSETDRVBUFFER:
		case MTSETDENSITY:
		case MTSETPART:
		case MTMKPART:
		case MTNOP:
			ccw = tape_ccw_cc(ccw,NOP,0,0,1);
			break;
		case MTBSFM:
			ccw = tape_ccw_cc(ccw,BACKSPACEFILE,0,0,count);
			break;
		case MTFSFM:
			ccw = tape_ccw_cc(ccw,FORSPACEFILE,0,0,count);
			break;
		case MTEOM:
			ccw = tape_ccw_cc(ccw,FORSPACEFILE,0,0,1);
			ccw = tape_ccw_cc(ccw,NOP,0,0,1);
			break;
		case MTERASE:
			ccw = tape_ccw_cc(ccw,REWIND,0,0,1);
			ccw = tape_ccw_cc(ccw,ERASE_GAP,0,0,1);
			ccw = tape_ccw_cc(ccw,DATA_SEC_ERASE,0,0,1);
			break;
		case MTTELL:
			ccw = tape_ccw_cc(ccw,READ_BLOCK_ID,8,treq->kernbuf,1);
			break;
		case MTUNLOAD:
			ccw = tape_ccw_cc(ccw,REWIND_UNLOAD,0,0,1);
			break;
		case MTCOMPRESSION:
			if((count < 0) || (count > 1)){
				tape_sprintf_exception (tape_dbf_area,6,"xcom parm\n");
				goto error;
			}
			if(count == 0){
				PRINT_INFO( "(%x) Compression switched off\n", td->devstat.devno);
				MOD_BYTE = 0x00; // IDRC off
			} else {
				PRINT_INFO( "(%x) Compression switched on\n", td->devstat.devno);
				MOD_BYTE = 0x08; // IDRC on
			}
			ccw = tape_ccw_cc(ccw,NOP,0,0,1);
			break; // Modset does the job
		case MTSEEK:
			{
			__u8* data = treq->kernbuf;
			data[0] = 0x01;
			data[1] = data[2] = data[3] = 0x00;
			if (count >= 4194304){
				tape_sprintf_exception(tape_dbf_area,6,"xsee parm\n");
				*rc = -EINVAL;
				goto error;
			}
			if(MOD_BYTE && 0x08)
				data[1] = data[1] | 0x80;

			data[3] += count % 256;
			data[2] += (count / 256) % 256;
			data[1] += (count / 65536);
			ccw = tape_ccw_cc(ccw,LOCATE,4,treq->kernbuf,1);
			break;
			}
		default:
			PRINT_WARN( "IOCTL %x not implemented\n",op );
			*rc = -EINVAL;
			goto error;

        }

	/* setup last ccw */

	switch(mtcmd){
		case MTEOM:
			ccw = tape_ccw_end(ccw,CCW_CMD_TIC,0,treq->cpaddr,1);
			break;
		case MTUNLOAD:
			ccw = tape_ccw_end(ccw,SENSE,32,treq->kernbuf,1);
			break;
		default:
			ccw = tape_ccw_end(ccw,NOP,0,0,1);
			break;
	}
	*rc = 0;
	return treq;
error:
	if (treq)
	        tape_free_ccw_req(treq);
	return NULL;
}

/*
 * Write Block 
 */

tape_ccw_req_t *
tape34xx_write_block (const char *data, size_t count, tape_dev_t* td)
{
	tape_ccw_req_t *treq = NULL;
	ccw1_t *ccw;
	treq = tape_alloc_ccw_req (2, 0, count,TO_WRI);
	if (!treq)
		goto error;
	if (idalbuf_copy_from_user (treq->idal_buf, data, count)) {
	        tape_sprintf_exception (tape_dbf_area,6,"xwbl segf.\n");
		goto error;
	}
	ccw = treq->cpaddr;
	ccw = tape_ccw_cc(ccw,MODE_SET_DB,1,&MOD_BYTE,1);
	ccw = tape_ccw_end_idal(ccw,WRITE_CMD,treq->idal_buf);
	treq->userbuf = (void *) data;
	tape_sprintf_event (tape_dbf_area,6,"xwbl ccwg\n");
	return treq;
error:
	tape_sprintf_exception (tape_dbf_area,6,"xwbl fail\n");
	if (treq)
	        tape_free_ccw_req(treq);
	return NULL;
}

/*
 * Read Block
 */

tape_ccw_req_t *
tape34xx_read_block (const char *data, size_t count, tape_dev_t* td)
{
	tape_ccw_req_t *treq = NULL;
	ccw1_t *ccw;
	/* we have to alloc 4 ccws in order to be able to transform request */
	/* into a read backward request in error case                       */
	treq = tape_alloc_ccw_req (4, 0, count,TO_RFO);
	if (!treq) 
		goto error;
	treq->userbuf = (void*)data;
	treq->userbuf_size = count;
	ccw = treq->cpaddr;
	ccw = tape_ccw_cc(ccw,MODE_SET_DB,1,&MOD_BYTE,1);
	ccw = tape_ccw_end_idal(ccw,READ_FORWARD,treq->idal_buf);
	tape_sprintf_event (tape_dbf_area,6,"xrbl ccwg\n");
	return treq;
error:
	tape_sprintf_exception (tape_dbf_area,6,"xrbl fail");
	if (treq)
	        tape_free_ccw_req(treq);
	return NULL;
}

/*
 * Read Opposite Error Recovery Function:
 * Used, when Read Forward does not work
 */

tape_ccw_req_t *
tape34xx_read_opposite (tape_dev_t* td)
{
	ccw1_t *ccw;
	tape_ccw_req_t* treq = tape_get_active_ccw_req(td);
	if (treq==NULL) // no request to recover?
		BUG();

	// transform read forward request into read backward request.
	ccw = treq->cpaddr;
	ccw = tape_ccw_cc(ccw,MODE_SET_DB,1,&MOD_BYTE,1);
	ccw = tape_ccw_cc_idal(ccw,READ_BACKWARD,treq->idal_buf);
	ccw = tape_ccw_cc(ccw,FORSPACEBLOCK,0,0,1);
	ccw = tape_ccw_end(ccw,NOP,0,0,1);
	treq->op = TO_RBA;
	tape_sprintf_event (tape_dbf_area,6,"xrop ccwg");
	return treq;
}

/*
 * Tape Block READ
 */

tape_ccw_req_t * tape34xx_bread (struct request *req,tape_dev_t* td,int tapeblock_major) {
	tape_ccw_req_t *treq;
	ccw1_t *ccw;
	__u8 *data;
	int s2b = blksize_size[tapeblock_major][td->first_minor]/hardsect_size[tapeblock_major][td->first_minor];
	int realcount = 0;
	int size,bhct = 0;
	struct buffer_head* bh;
	for (bh = req->bh; bh; bh = bh->b_reqnext) {
		if (bh->b_size > blksize_size[tapeblock_major][td->first_minor])
			for (size = 0; size < bh->b_size; size += blksize_size[tapeblock_major][td->first_minor])
				bhct++;
		else
			bhct++;
	}
	tape_sprintf_event (tape_dbf_area,6,"xBREDid:");
	treq = tape_alloc_ccw_req (2+bhct+1, 4,0,TO_BLOCK);
	if (!treq) {
		tape_sprintf_exception (tape_dbf_area,6,"xBREDnomem\n");
                goto error;
        }

	data = treq->kernbuf;
	data[0] = 0x01;
	data[1] = data[2] = data[3] = 0x00;
	realcount=req->sector/s2b;
	if (MOD_BYTE & 0x08)	// IDRC on
		data[1] = data[1] | 0x80;
	data[3] += realcount % 256;
	data[2] += (realcount / 256) % 256;
	data[1] += (realcount / 65536);
	tape_sprintf_event (tape_dbf_area,6,"realcount = %i\n",realcount);

	ccw = treq->cpaddr;
	ccw = tape_ccw_cc(ccw,MODE_SET_DB,1,&MOD_BYTE,1);
	if (realcount!=td->blk_data.position)
		ccw = tape_ccw_cc(ccw,LOCATE,4,treq->kernbuf,1);
	else
		ccw = tape_ccw_cc(ccw,NOP,0,0,1);
	td->blk_data.position=realcount+req->nr_sectors/s2b;
	for (bh=req->bh;bh!=NULL;) {
		if (bh->b_size >= blksize_size[tapeblock_major][td->first_minor]) {
			for (size = 0; size < bh->b_size; size += blksize_size[tapeblock_major][td->first_minor]){
				ccw->flags = CCW_FLAG_CC;
				ccw->cmd_code = READ_FORWARD;
				ccw->count = blksize_size[tapeblock_major][td->first_minor];
				set_normalized_cda(ccw,__pa(bh->b_data+size));
				ccw++;
			}
			bh = bh->b_reqnext;
		} else {	/* group N bhs to fit into byt_per_blk */
		    BUG();
		}
	}
	ccw = tape_ccw_end(ccw,NOP,0,0,1);
	tape_sprintf_event (tape_dbf_area,6,"xBREDccwg\n");
	return treq;
error:
	tape_sprintf_exception (tape_dbf_area,6,"xBREDccwg fail");
	if (treq)
	        tape_free_ccw_req(treq);
	return NULL;
}

void tape34xx_free_bread (tape_ccw_req_t* treq) {
	ccw1_t* ccw;
	for (ccw=(ccw1_t*)treq->cpaddr;ccw->flags & CCW_FLAG_CC;ccw++)
	if (ccw->cmd_code == READ_FORWARD)
		clear_normalized_cda(ccw);
	tape_free_ccw_req(treq); 
}

// FIXME: Comment?

void tape34xx_bread_enable_locate (tape_ccw_req_t * treq) {
	ccw1_t *ccw;
	if (treq==NULL) BUG();
	ccw=treq->cpaddr;
	ccw++;
	ccw = tape_ccw_cc(ccw,LOCATE,4,treq->kernbuf,1);
	return;
}

/*******************************************************************
 * Event Handlers
 *******************************************************************/

/*
 * Default Handler is called, when an unexpected IRQ comes in
 */

void
tape34xx_default_handler (tape_dev_t * td)
{
	tape_ccw_req_t* treq = tape_get_active_ccw_req(td);
	tape_sprintf_event (tape_dbf_area,6,"xdefhandle\n");
	PRINT_ERR ("TAPE34XX: An unexpected Unit Check occurred.\n");
	PRINT_ERR ("TAPE34XX: Please read Documentation/s390/TAPE and report it!\n");
	PRINT_ERR ("TAPE34XX: Current op is: %s",tape_op_verbose[treq->op]);
	tape_dump_sense (td);
	treq->rc = -EIO;
	if(treq->wakeup)
		treq->wakeup (treq);
}

/* This function analyses the tape's sense-data in case of a unit-check. */
/* If possible, it tries to recover from the error. Else the user is */ 
/* informed about the problem.  */

void
tape34xx_error_recovery (tape_dev_t* td)
{
    __u8* sense=td->devstat.ii.sense.data;
    int inhibit_cu_recovery=0;
    int cu_type=td->discipline->cu_type;
    tape_ccw_req_t *treq = tape_get_active_ccw_req(td);

    if (treq==NULL) {
	    // Nothing to recover! Why call me?
	    BUG();
    }
    if (MOD_BYTE&0x80) inhibit_cu_recovery=1;
    if (treq->op==TO_BLOCK) {
	// no recovery for block device, bottom half will retry...
	tape34xx_error_recovery_has_failed(td,EIO);
	return;
    }
    if (sense[0]&SENSE_COMMAND_REJECT)
	switch (treq->op) {
	case TO_DSE:
	case TO_EGA:
	case TO_WRI:
	case TO_WTM:
	    if (sense[1]&SENSE_WRITE_PROTECT) {
		// trying to write, but medium is write protected
		tape34xx_error_recovery_has_failed(td,EACCES);
		return;
	    }
	default:
	    tape34xx_error_recovery_HWBUG(td,1);
	    return;
	}
    // special cases for various tape-states when reaching end of recorded area
    if (((sense[0]==0x08) || (sense[0]==0x10) || (sense[0]==0x12)) &&
	((sense[1]==0x40) || (sense[1]==0x0c)))
	switch (treq->op) {
	case TO_FSF:
	    // Trying to seek beyond end of recorded area
	    tape34xx_error_recovery_has_failed(td,EIO);
	    return;
	case TO_LBL:
	    // Block could not be located.
	    tape34xx_error_recovery_has_failed(td,EIO);
	    return;
	case TO_RFO:
	    // Try to read beyond end of recorded area -> 0 bytes read
	    tape34xx_error_recovery_has_failed(td,0);
	    return;
	default:
            PRINT_ERR("Invalid op in %s:%i\n",__FUNCTION__,__LINE__);
            tape34xx_error_recovery_has_failed(td,0);
            return;
	}
    // Sensing special bits
    if (sense[0]&SENSE_BUS_OUT_CHECK) {
	tape34xx_error_recovery_do_retry(td);
	return;
    }
    if (sense[0]&SENSE_DATA_CHECK) {
	// hardware failure, damaged tape or improper operating conditions
	switch (sense[3]) {
	case 0x23:
	    // a read data check occurred
	    if ((sense[2]&SENSE_TAPE_SYNC_MODE) ||
		(inhibit_cu_recovery)) {
		// data check is not permanent, may be recovered. 
		// We always use async-mode with cu-recovery, so this should *never* happen.
		tape34xx_error_recovery_HWBUG(td,2);
		return;
	    } else {
		// data check is permanent, CU recovery has failed
		PRINT_WARN("Permanent read error, recovery failed!\n");
		tape34xx_error_recovery_has_failed(td,EIO);
		return;
	    }
	case 0x25:
	    // a write data check occurred
	    if ((sense[2]&SENSE_TAPE_SYNC_MODE) ||
		(inhibit_cu_recovery)) {
		// data check is not permanent, may be recovered.
		// We always use async-mode with cu-recovery, so this should *never* happen.
		tape34xx_error_recovery_HWBUG(td,3);
		return;
	    } else {
		// data check is permanent, cu-recovery has failed
		PRINT_WARN("Permanent write error, recovery failed!\n");
		tape34xx_error_recovery_has_failed(td,EIO);
		return;
	    }
	case 0x26:
	    // Data Check (read opposite) occurred. We'll recover this.
	    tape34xx_error_recovery_read_opposite(td);
	    return;
	case 0x28:
	    // The ID-Mark at the beginning of the tape could not be written. This is fatal, we'll report and exit.
	    PRINT_WARN("ID-Mark could not be written. Check your hardware!\n");
	    tape34xx_error_recovery_has_failed(td,EIO);
	    return;
	case 0x31:
	    // Tape void. Tried to read beyond end of device. We'll report and exit.
	    PRINT_WARN("Try to read beyond end of recorded area!\n");
	    tape34xx_error_recovery_has_failed(td,ENOSPC);
	    return;
	case 0x41:
	    // Record sequence error. cu detected incorrect block-id sequence on tape. We'll report and exit.
	    PRINT_WARN("Illegal block-id sequence found!\n");
	    tape34xx_error_recovery_has_failed(td,EIO);
	    return;
	    default:
	    // well, all data checks for 3480 should result in one of the above erpa-codes. if not -> bug
	    // On 3490, other data-check conditions do exist.
		if (cu_type==0x3480) {
		    tape34xx_error_recovery_HWBUG(td,4);
		    return;
		}
	}
    }
    if (sense[0]&SENSE_OVERRUN) {
	// A data overrun between cu and drive occurred. The channel speed is to slow! We'll report this and exit!
	switch (sense[3]) {
	case 0x40: // overrun error
	    PRINT_WARN ("Data overrun error between control-unit and drive. Use a faster channel connection, if possible! \n");
	    tape34xx_error_recovery_has_failed(td,EIO);
	    return;
	default:
	    // Overrun bit is set, but erpa does not show overrun error. This is a bug.
	    tape34xx_error_recovery_HWBUG(td,5);
	    return;
	}
    }
    if (sense[1]&SENSE_RECORD_SEQUENCE_ERR) {
	switch (sense[3]) {
	case 0x41:
	    // Record sequence error. cu detected incorrect block-id sequence on tape. We'll report and exit.
	    PRINT_WARN("Illegal block-id sequence found!\n");
	    tape34xx_error_recovery_has_failed(td,EIO);
	    return;
	default:
	    // Record sequence error bit is set, but erpa does not show record sequence error. This is a bug.
	    tape34xx_error_recovery_HWBUG(td,6);
	    return;
	}
    }
    // Sensing erpa codes
    switch (sense[3]) {
    case 0x00:
	// Everything is fine, but we got a unit check. Report and ignore!
	PRINT_WARN ("Non-error sense was found. Unit-check will be ignored, expect errors...\n");
	return;
    case 0x21:
	// Data streaming not operational. Cu switches to interlock mode, we reissue the command.
	PRINT_WARN ("Data streaming not operational. Switching to interlock-mode! \n");
	tape34xx_error_recovery_do_retry(td);
	return;
    case 0x22:
	// Path equipment check. Might be drive adapter error, buffer error on the lower interface, internal path not useable, or error during cartridge load.
	// All of the above are not recoverable
	PRINT_WARN ("A path equipment check occurred. One of the following conditions occurred:\n");
	PRINT_WARN ("drive adapter error,buffer error on the lower interface, internal path not useable, error during cartridge load.\n");
	tape34xx_error_recovery_has_failed(td,EIO);
	return;
    case 0x23:
	// Read data check. Should have been be covered earlier -> Bug!
	tape34xx_error_recovery_HWBUG(td,7);
	return;
    case 0x24:
	// Load display check. Load display was command was issued, but the drive is displaying a drive check message. Can be threated as "device end".
	tape34xx_error_recovery_succeded(td);
	return;
    case 0x25:
	// Write data check. Should have been covered earlier -> Bug!
	tape34xx_error_recovery_HWBUG(td,8);
	return;
    case 0x26:
	// Data check (read opposite). Should have been covered earlier -> Bug!
	tape34xx_error_recovery_HWBUG(td,9);
	return;
    case 0x27:
	// Command reject. May indicate illegal channel program or buffer over/underrun. 
	// Since all channel programms are issued by this driver and ought be correct,
	// we assume a over/underrun situaltion and retry the channel program.
	tape34xx_error_recovery_do_retry(td);
	return;
    case 0x28:
	// Write id mark check. Should have beed covered earlier -> bug!
	tape34xx_error_recovery_HWBUG(td,10);
	return;
    case 0x29:
	// Function incompatible. Either idrc is on but hardware not capable doing idrc 
	// or a perform subsystem func is issued and the cu is not online. Anyway, this 
	// cannot be recovered and is an I/O error.
	PRINT_WARN ("Function incompatible. Try to switch off idrc! \n");
	tape34xx_error_recovery_has_failed(td,EIO);
	return;
    case 0x2a:
	// Unsolicited environmental data. An internal counter overflows, we can ignore
	// this and reissue the cmd.
	tape34xx_error_recovery_do_retry(td);
	return;
    case 0x2b:
	// Environmental data present. Indicates either unload completed ok or read buffered 
	// log command completed ok. 
	if (treq->op==TO_RUN) {
	    // Rewind unload completed ok.
	    tape34xx_error_recovery_succeded(td);
	    return;
	}
	// Since we do not issue read buffered log commands, this should never occur -> bug.
	tape34xx_error_recovery_HWBUG(td,11);
	return;
    case 0x2c:
	// Permanent equipment check. cu has tried recovery, but did not succeed. This is an
	// I/O error.
	tape34xx_error_recovery_has_failed(td,EIO);
	return;
    case 0x2d:
	// Data security erase failure.
	if (treq->op==TO_DSE) {
	    // report an I/O error
	    tape34xx_error_recovery_has_failed(td,EIO);
	    return;
	}
	// Data security erase failure, but no such command issued. This is a bug.
	tape34xx_error_recovery_HWBUG(td,12);
	return;
    case 0x2e:
	// Not capable. This indicates either that the drive fails reading the format id mark
	// or that that format specified is not supported by the drive. We write a message and
	// return an I/O error.
	PRINT_WARN("Drive not capable processing the tape format!");
	tape34xx_error_recovery_has_failed(td,EMEDIUMTYPE);
	return;
    case 0x2f:
	// This erpa is reserved. This is a bug.
	tape34xx_error_recovery_HWBUG(td,13);
	return;
    case 0x30:
	// The medium is write protected, while trying to write on it. We'll report this.
	PRINT_WARN("Medium is write protected!\n");
	tape34xx_error_recovery_has_failed(td,EACCES);
	return;
    case 0x31:
	// Tape void. Should have beed covered ealier -> bug
	tape34xx_error_recovery_HWBUG(td,14);
	return;
    case 0x32:
	// Tension loss. We cannot recover this, it's an I/O error.
	PRINT_WARN("The drive lost tape tension.\n");
	tape34xx_error_recovery_has_failed(td,EIO);
	return;
    case 0x33:
	// Load Failure. The catridge was not inserted correctly or the tape is not threaded
	// correctly. We cannot recover this, the user has to reload the catridge.
	PRINT_WARN("Cartridge load failure. Reload the cartridge and try again.\n");
	tape34xx_error_recovery_has_failed(td,EIO);
	return;
    case 0x34:
	// Unload failure. The drive cannot maintain tape tension and control tape movement 
	// during an unload operation. 
	PRINT_WARN("Failure during cartridge unload. Please try manually.\n");
	if (treq->op!=TO_RUN) {
	    tape34xx_error_recovery_HWBUG(td,15);
	    return;
	}
	tape34xx_error_recovery_has_failed(td,EIO);
	return;
    case 0x35:
	// Drive equipment check. One of the following:
	// - cu cannot recover from a drive detected error
	// - a check code message is displayed on drive message/load displays
	// - the cartridge loader does not respond correctly
	// - a failure occurs during an index, load, or unload cycle
	PRINT_WARN("Equipment check! Please check the drive and the cartridge loader.\n");
	tape34xx_error_recovery_has_failed(td,EIO);
	return;
    case 0x36:
	switch (cu_type) {
	case 0x3480:
	    // This erpa is reserved for 3480 -> BUG
	    tape34xx_error_recovery_HWBUG(td,16);
	    return;
	case 0x3490:
	    // End of data. This is a permanent I/O error, which cannot be recovered.
	    // A read-type command has reached the end-of-data mark.
	    tape34xx_error_recovery_has_failed(td,EIO);
	    return;
	}
    case 0x37:
	// Tape length error. The tape is shorter than reported in the beginning-of-tape data.
	PRINT_WARN("Tape length error.\n");
	tape34xx_error_recovery_has_failed(td,EIO);
	return;
    case 0x38:
	// Physical end of tape. A read/write operation reached the physical end of tape.
        if (treq->op==TO_WRI ||
            treq->op==TO_DSE ||
            treq->op==TO_EGA ||
            treq->op==TO_WTM){
            tape34xx_error_recovery_has_failed(td,ENOSPC);
        } else {
            tape34xx_error_recovery_has_failed(td,EIO);
        }
        return;
    case 0x39:
	// Backward at BOT. The drive is at BOT and is requestet to move backward.
	tape34xx_error_recovery_has_failed(td,EIO);
	return;
    case 0x3a:
	// Drive switched not ready, but the command needs the drive to be ready.
	PRINT_WARN("Drive not ready. Turn the ready/not ready switch to ready position and try again.\n");
	tape34xx_error_recovery_has_failed(td,EIO);
	return;
    case 0x3b:
	// Manual rewind or unload. This causes an I/O error.
	PRINT_WARN("Medium was rewound or unloaded manually. Expect errors! Please do only use the mtoffl and mtrew ioctl to unload tapes or rewind tapes.\n");
	tape34xx_error_recovery_has_failed(td,EIO);
	return;
    case 0x3c:
    case 0x3d:
    case 0x3e:
    case 0x3f:
	// These erpas are reserved -> BUG
	tape34xx_error_recovery_HWBUG(td,17);
	return;
    case 0x40:
	// Overrun error. This should have been covered earlier -> bug.
	tape34xx_error_recovery_HWBUG(td,18);
	return;
    case 0x41:
	// Record sequence error. This should have been covered earlier -> bug.
	tape34xx_error_recovery_HWBUG(td,19);
	return;
    case 0x42:
	// Degraded mode. A condition that can cause degraded performace is detected.
	PRINT_WARN("Subsystem is running in degraded mode. This may compromise your performace.\n");
	tape34xx_error_recovery_do_retry(td);
	return;
    case 0x43:
	// Drive not ready. Probably swith the ready/not ready switch to ready?
	PRINT_WARN("The drive is not ready. Maybe no medium in?\n");
	tape_med_state_set(td,MS_UNLOADED);
	tape34xx_error_recovery_has_failed(td,ENOMEDIUM);
	return;
    case 0x44:
	// Locate Block unsuccessfull. We'll report this.
	if ((treq->op!=TO_BLOCK) &&
	    (treq->op!=TO_LBL)) {
	    tape34xx_error_recovery_HWBUG(td,20); // No locate block was issued...
	    return;
	}
	tape34xx_error_recovery_has_failed(td,EIO);
	return;
    case 0x45:
	// The drive is assigned elsewhere [to a different channel path/computer].
	PRINT_WARN("The drive is assigned elsewhere.\n");
	tape34xx_error_recovery_has_failed(td,EIO);
	return;
    case 0x46:
	// Drive not online. Drive may be switched offline, the power supply may be switched off 
	// or the drive address may not be set correctly.
	PRINT_WARN("The drive is not online.");
	tape34xx_error_recovery_has_failed(td,EIO);
	return;
    case 0x47:
	// Volume fenced. cu reports volume integrity is lost! 
	PRINT_WARN("Volume fenced. The volume integrity is lost! \n");
	tape34xx_error_recovery_has_failed(td,EIO);
	return;
    case 0x48:
	// Log sense data and retry request. We'll do so...
	tape34xx_error_recovery_do_retry(td);
	return;
    case 0x49:
	// Bus out check. A parity check error on the bus was found.	PRINT_WARN("Bus out check. A data transfer over the bus was corrupted.\n");
	tape34xx_error_recovery_has_failed(td,EIO);
	return;
    case 0x4a:
	// Control unit erp failed. We'll report this.
	PRINT_WARN("The control unit failed recovering an I/O error.\n");
	tape34xx_error_recovery_has_failed(td,EIO);
	return;
    case 0x4b:
	// Cu and drive incompatible. The drive requests micro-program patches, which are not available on the cu.
	PRINT_WARN("The drive needs microprogram patches from the control unit, which are not available.\n");
	tape34xx_error_recovery_has_failed(td,EIO);
	return;
    case 0x4c:
	// Recovered Check-One failure. Cu develops a hardware error, but is able to recover. We'll reissue the command.
	tape34xx_error_recovery_do_retry(td);
	return;
    case 0x4d:
	switch (cu_type) {
	case 0x3480:
	    // This erpa is reserved for 3480 -> bug
      	    tape34xx_error_recovery_HWBUG(td,21);
	    return;
	case 0x3490:
	    // Resetting event received. Since the driver does not support resetting event recovery
	    // (which has to be handled by the I/O Layer), we'll report and retry our command.
	    tape34xx_error_recovery_do_retry(td);
	    return;
	}
    case 0x4e:
	switch (cu_type) {
	case 0x3480:
	    // This erpa is reserved for 3480 -> bug.
	    tape34xx_error_recovery_HWBUG(td,22);
	    return;
	case 0x3490:
	    // Maximum block size exeeded. This indicates, that the block to be written is larger
	    // than allowed for buffered mode. We'll report this...
	    PRINT_WARN("Maximum block size for buffered mode exceeded.\n");
	    tape34xx_error_recovery_has_failed(td,ENOBUFS);
	    return;
	}
    case 0x4f:
	// These erpas are reserved -> bug
	tape34xx_error_recovery_HWBUG(td,23);
	return;
    case 0x50:
	// Read buffered log (Overflow). Cu is running in extended beffered log mode, and a counter overflows.
	// This should never happen, since we're never running in extended buffered log mode -> bug.
	tape34xx_error_recovery_do_retry(td);
	return;
    case 0x51:
	// Read buffered log (EOV). EOF processing occurs while the cu is in extended buffered log mode.
	// This should never happen, since we're never running in extended buffered log mode -> bug.
	tape34xx_error_recovery_do_retry(td);
	return;
    case 0x52:
	// End of Volume complete. Rewind unload completed ok. We'll report to the user...
	if (treq->op!=TO_RUN) {
	    tape34xx_error_recovery_HWBUG(td,24);
	    return;
	}
	tape34xx_error_recovery_succeded(td);
	return;
    case 0x53:
	// Global command intercept. We'll have to reissue our command.
	tape34xx_error_recovery_do_retry(td);
	return;
    case 0x54:
	// Channel interface recovery (temporary). This can be recovered by reissuing the command.
	tape34xx_error_recovery_do_retry(td);
	return;
    case 0x55:
	// Channel interface recovery (permanent). This cannot be recovered, we'll inform the user.
	PRINT_WARN("A permanent channel interface error occurred.\n");
	tape34xx_error_recovery_has_failed(td,EIO);
	return;
    case 0x56:
	// Channel protocol error. This cannot be recovered.
	PRINT_WARN("A channel protocol error occurred.\n");
	tape34xx_error_recovery_has_failed(td,EIO);
	return;
    case 0x57:
	switch (cu_type) {
	case 0x3480:
	    // Attention intercept. We have to reissue the command.
	    PRINT_WARN("An attention intercept occurred, which will be recovered.\n");
	    tape34xx_error_recovery_do_retry(td);
	    return;
	case 0x3490:
	    // Global status intercept. We have to reissue the command.
	    PRINT_WARN("An global status intercept was received, which will be recovered.\n");
	    tape34xx_error_recovery_do_retry(td);
	    return;
	}
    case 0x58:
    case 0x59:
	// These erpas are reserved -> bug.
	tape34xx_error_recovery_HWBUG(td,25);
	return;
    case 0x5a:
	// Tape length incompatible. The tape inserted is too long, 
	// which could cause damage to the tape or the drive.
	PRINT_WARN("Tape length incompatible [should be IBM Cartridge System Tape]. May cause damage to drive or tape.n");
	tape34xx_error_recovery_has_failed(td,EIO);
	return;
    case 0x5b:
	// Format 3480 XF incompatible
	if (sense[1]&SENSE_BEGINNING_OF_TAPE) {
	    // Everything is fine. The tape will be overwritten in a different format.
	    tape34xx_error_recovery_do_retry(td);
	    return;
	}
	PRINT_WARN("Tape format is incompatible to the drive, which writes 3480-2 XF.\n");
	tape34xx_error_recovery_has_failed(td,EIO);
	return;
    case 0x5c:
	// Format 3480-2 XF incompatible
	PRINT_WARN("Tape format is incompatible to the drive. The drive cannot access 3480-2 XF volumes.\n");
	tape34xx_error_recovery_has_failed(td,EIO);
	return;
    case 0x5d:
	// Tape length violation. 
	PRINT_WARN("Tape length violation [should be IBM Enhanced Capacity Cartridge System Tape]. May cause damage to drive or tape.\n");
	tape34xx_error_recovery_has_failed(td,EMEDIUMTYPE);
	return;
    case 0x5e:
	// Compaction algorithm incompatible.
	PRINT_WARN("The volume is recorded using an incompatible compaction algorith, which is not supported by the control unit.\n");
	tape34xx_error_recovery_has_failed(td,EMEDIUMTYPE);
	return;
    default:
	// Reserved erpas -> bug
	tape34xx_error_recovery_HWBUG(td,26);
	return;
    }
}

void tape34xx_error_recovery_has_failed (tape_dev_t* td,int error_id) {
	tape_ccw_req_t *treq = tape_get_active_ccw_req(td);
	tape_sprintf_event (tape_dbf_area,3,"Error Recovery failed for %s\n", tape_op_verbose[treq->op]);
	treq->rc = -error_id;
	if(treq->wakeup)
		treq->wakeup (treq);
}    

void tape34xx_error_recovery_succeded(tape_dev_t* td) {
	tape_ccw_req_t *treq = tape_get_active_ccw_req(td);
	tape_sprintf_event (tape_dbf_area,3,"Error Recovery successfull for %s\n", tape_op_verbose[treq->op]);
	tape34xx_done_handler(td);
}

void tape34xx_error_recovery_do_retry(tape_dev_t* td) {
	tape_ccw_req_t* treq = tape_get_active_ccw_req(td);
	tape_sprintf_event (tape_dbf_area,3,"xerp retr\n");
	tape_sprintf_event (tape_dbf_area,3, "%s\n",tape_op_verbose[treq->op]);
	tape_remove_ccw_req(td,treq);
	tape_do_io_irq(td, treq,TAPE_NO_WAIT);
}
    
void 
tape34xx_error_recovery_read_opposite (tape_dev_t* td) {
	tape_ccw_req_t *treq = tape_get_active_ccw_req(td);
	switch (treq->op) {
		case TO_RFO:
			// We did read forward, but the data could not be read 
			// *correctly*. We will read backward and then skip 
			// forward again.
			if(tape34xx_read_opposite(td))
				tape34xx_error_recovery_do_retry(td);
			else
				tape34xx_error_recovery_has_failed(td,EIO);
			break;
		case TO_RBA:
			// We tried to read forward and backward, but hat no 
			// success -> failed.
			tape34xx_error_recovery_has_failed(td,EIO);
			break;
		default:
			PRINT_ERR("read_opposite_recovery_called_with_state:%s\n", tape_op_verbose[treq->op]);
			tape34xx_error_recovery_has_failed(td,EIO);
		}
}

void 
tape34xx_error_recovery_HWBUG (tape_dev_t* td,int condno) {
	tape_ccw_req_t *treq = tape_get_active_ccw_req(td);
	PRINT_WARN("An unexpected condition #%d was caught in tape error recovery.\n",condno);
	PRINT_WARN("Please report this incident.\n");
	if(treq)
	PRINT_WARN("Operation of tape:%s\n", tape_op_verbose[treq->op]);
	tape_dump_sense(td);
	tape34xx_error_recovery_has_failed(td,EIO);
}

/*
 * This routine is called by frontend after an ENOSP on write
 */
 
void tape34xx_process_eov(tape_dev_t* ti)
{
	tape_ccw_req_t *treq;
	int rc;
	int tm_written = 0;
 
	/* End of volume: We have to backspace the last written record, then */
	/* we TRY to write a tapemark and then backspace over the written TM */
 
	treq = tape34xx_ioctl(ti,MTBSR,1,&rc);
	if(treq){
		tape_do_io_and_wait(ti,treq,TAPE_WAIT);
		tape_free_ccw_req(treq);
	}
	treq = tape34xx_ioctl(ti,MTWEOF,1,&rc);
	if(treq){
		rc = tape_do_io_and_wait(ti,treq,TAPE_WAIT);
		if((rc == 0) && (treq->rc == 0))
			tm_written = 1;
		tape_free_ccw_req(treq);
	}
	if(tm_written){
		treq = tape34xx_ioctl(ti,MTBSR,1,&rc);
		if(treq){
			tape_do_io_and_wait(ti,treq,TAPE_WAIT);
			tape_free_ccw_req(treq);
		}
	}
}

/*
 * 34xx first level interrupt handler
 */

void tape34xx_irq(tape_dev_t* td)
{
	tape_ccw_req_t* treq = tape_get_active_ccw_req(td);
	if (treq == NULL) {
		tape34xx_unsolicited_irq(td);
	} else if ((td->devstat.dstat & DEV_STAT_UNIT_EXCEP) && 
		(td->devstat.dstat & DEV_STAT_DEV_END) &&
		(treq->op == TO_WRI)){
		/* Write at end of volume */
		PRINT_INFO("End of volume\n"); /* XXX */
		tape34xx_error_recovery_has_failed(td,ENOSPC);
	} else if (td->devstat.dstat & DEV_STAT_UNIT_CHECK) {
		tape34xx_error_recovery(td);
	} else if (td->devstat.dstat & (DEV_STAT_DEV_END)) {
		tape34xx_done_handler(td);
        } else {
		tape34xx_default_handler(td);
	}
}

EXPORT_SYMBOL(tape34xx_irq);
EXPORT_SYMBOL(tape34xx_write_block);
EXPORT_SYMBOL(tape34xx_read_block);
EXPORT_SYMBOL(tape34xx_ioctl);
EXPORT_SYMBOL(tape34xx_ioctl_overload);
EXPORT_SYMBOL(tape34xx_bread);
EXPORT_SYMBOL(tape34xx_free_bread);
EXPORT_SYMBOL(tape34xx_process_eov);
EXPORT_SYMBOL(tape34xx_bread_enable_locate);

