/***************************************************************************
 *
 *  drivers/s390/char/tapechar.c
 *    character device frontend for tape device driver
 *
 *  S390 and zSeries version
 *    Copyright (C) 2001 IBM Corporation
 *    Author(s): Carsten Otte <cotte@de.ibm.com>
 *               Michael Holzheu <holzheu@de.ibm.com>
 *               Tuan Ngo-Anh <ngoanh@de.ibm.com>
 *
 *
 ****************************************************************************
 */

#include "tapedefs.h"
#include <linux/config.h>
#include <linux/version.h>
#include <linux/types.h>
#include <linux/proc_fs.h>
#include <asm/s390dyn.h>
#include <linux/mtio.h>
#include <asm/uaccess.h>
#include <linux/compatmac.h>
#ifdef MODULE
#define __NO_VERSION__
#include <linux/module.h>
#endif
#include "tape.h"
#include "tapechar.h"

#define PRINTK_HEADER "TCHAR:"

/*******************************************************************
 * GLOBALS
 *******************************************************************/

/*
 * file operation structure for tape devices
 */
static struct file_operations tape_fops =
{
	read:tapechar_read,
	write:tapechar_write,
	ioctl:tapechar_ioctl,
	open:tapechar_open,
	release:tapechar_release,
};

int tapechar_major = TAPECHAR_MAJOR;

/*******************************************************************
 * DEVFS Functions
 *******************************************************************/

#ifdef CONFIG_DEVFS_FS

/*
 * Create Char directory with (non)rewinding entries
 */

devfs_handle_t
tapechar_mkdevfstree (tape_dev_t* td) {
    devfs_handle_t rc=NULL;
    rc=td->char_data.devfs_char_dir=devfs_mk_dir (td->devfs_dir, "char", td);
    if (rc==NULL) goto out_undo;
    rc=td->char_data.devfs_nonrewinding=devfs_register(td->char_data.devfs_char_dir, 
						    "nonrewinding",
						    DEVFS_FL_DEFAULT,tapechar_major, 
						    TAPECHAR_NOREW_MINOR(td->first_minor), 
						    TAPECHAR_DEVFSMODE, &tape_fops, td);
    if (rc==NULL) goto out_undo;
    rc=td->char_data.devfs_rewinding=devfs_register(td->char_data.devfs_char_dir, 
						 "rewinding",
						 DEVFS_FL_DEFAULT, tapechar_major, 
						 TAPECHAR_REW_MINOR(td->first_minor),
						 TAPECHAR_DEVFSMODE, &tape_fops, td);
    if (rc==NULL) goto out_undo;
    goto out;
 out_undo:
    tapechar_rmdevfstree (td);
 out:
    return rc;
}

/*
 * Remove DEVFS entries
 */

void
tapechar_rmdevfstree (tape_dev_t* td) {
    if (td->char_data.devfs_nonrewinding) 
	    devfs_unregister(td->char_data.devfs_nonrewinding);
    if (td->char_data.devfs_rewinding)
	    devfs_unregister(td->char_data.devfs_rewinding);
    if (td->char_data.devfs_char_dir)
	    devfs_unregister(td->char_data.devfs_char_dir);
}
#endif

/*******************************************************************
 * TAPECHAR Setup Functions
 *******************************************************************/

/*
 * This function is called for every new tapedevice
 */

void
tapechar_setup (tape_dev_t * td)
{
#ifdef CONFIG_DEVFS_FS
    tapechar_mkdevfstree(td);
#endif
}

/*
 * Tapechar init Function
 */

void
tapechar_init (void)
{
	int result;
	tape_frontend_t *charfront,*temp;
	tape_dev_t* td;

	tape_init();

	/* Register the tape major number to the kernel */
	result = register_chrdev (tapechar_major, "tape", &tape_fops);
	if (result < 0) {
		PRINT_WARN (KERN_ERR "tape: can't get major %d\n", tapechar_major);
		tape_sprintf_event (tape_dbf_area,3,"c:initfail\n");
		goto out;
	}
	if (tapechar_major == 0)
		tapechar_major = result;	/* accept dynamic major number */
	PRINT_WARN (KERN_ERR " tape gets major %d for character device\n", result);
	charfront = kmalloc (sizeof (tape_frontend_t), GFP_KERNEL);
	if (charfront == NULL) {
	        PRINT_WARN (KERN_ERR "tapechar: cannot alloc memory for the frontend_t structure\n");
                tape_sprintf_event (tape_dbf_area,3,"c:initfail no mem\n");
		goto out;
	}
	charfront->device_setup = (tape_setup_device_t)tapechar_setup;
#ifdef CONFIG_DEVFS_FS
	charfront->mkdevfstree = tapechar_mkdevfstree;
	charfront->rmdevfstree = tapechar_rmdevfstree;
#endif
        tape_sprintf_event (tape_dbf_area,3,"c:init ok\n");
	charfront->next=NULL;
	if (tape_first_front==NULL) {
	    tape_first_front=charfront;
	} else {
	    temp=tape_first_front;
	    while (temp->next!=NULL)
		temp=temp->next;
	    temp->next=charfront;
	}
	td=tape_first_dev;
	while (td!=NULL) {
	    tapechar_setup(td);
	    td=td->next;
	}
 out:
	return;
}

/*
 * cleanup
 */

void
tapechar_uninit (void)
{
	unregister_chrdev (tapechar_major, "tape");
}


/*******************************************************************
 * TAPECHAR Util functions
 *******************************************************************/

/*
 * Terminate write command (we write two TMs and skip backward over last)
 * This ensures that the tape is always correctly terminated.
 * When the user writes afterwards a new file, he will overwrite the
 * second TM and therefore one TM will remain to seperate the 
 * two files on the tape...
 */

static void
tapechar_terminate_write(tape_dev_t* td)
{
	tape_ccw_req_t *treq;
	int rc;

	treq = td->discipline->ioctl(td, MTWEOF,1,&rc);
	if (!treq)
		goto out;
	tape_do_io_and_wait(td,treq,TAPE_WAIT);
	tape_free_ccw_req(treq);
	treq = td->discipline->ioctl(td, MTWEOF,1,&rc);
	if (!treq)
		goto out;
	tape_do_io_and_wait(td,treq,TAPE_WAIT);
	tape_free_ccw_req(treq);
	treq = td->discipline->ioctl(td, MTBSR,1,&rc);
	if (!treq)
		goto out;
	tape_do_io_and_wait(td,treq,TAPE_WAIT);
	tape_free_ccw_req(treq);

out:
	return;
}
	

/*******************************************************************
 * TAPECHAR Functions:
 * - read
 * - write
 * - open
 * - close
 * - ioctl
 *******************************************************************/

/*
 * Tape device read function
 */

ssize_t
tapechar_read (struct file *filp, char *data, size_t count, loff_t * ppos)
{
	tape_dev_t *td;
	size_t block_size;
	tape_ccw_req_t *treq;
	int rc = 0;
	size_t cpysize;

        tape_sprintf_event (tape_dbf_area,6,"c:read\n");
	td = (tape_dev_t*)filp->private_data;
	
	if (ppos != &filp->f_pos) {
		/* "A request was outside the capabilities of the device." */
		/* This check uses internal knowledge about how pread and */
		/* read work... */
	        tape_sprintf_event (tape_dbf_area,6,"c:ppos wrong\n");
		rc = -EOVERFLOW;      /* errno=75 Value too large for def. data type */
		goto out;
	}
	if (td->char_data.block_size == 0) {
		block_size = count;
	} else {
	        if (count < td->char_data.block_size) {
		        rc = -EINVAL; // invalid argument+
			tape_sprintf_event (tape_dbf_area,3,"tapechar:read smaller than block size was requested\n");
			goto out;
		}
		block_size = td->char_data.block_size;
	}
	tape_sprintf_event (tape_dbf_area,6,"c:nbytes: %x\n",block_size);
	treq = td->discipline->read_block (data, block_size, td);
	if (!treq) {
		rc = -ENOBUFS;
		goto out;
	}

	rc = tape_do_io_and_wait(td,treq,TAPE_WAIT);
	TAPE_MERGE_RC(treq,rc);
	if(rc != 0)
		goto out_free;
	rc = cpysize = block_size - td->devstat.rescnt;
	if(idalbuf_copy_to_user(treq->userbuf, treq->idal_buf, cpysize)) {
		tape_sprintf_exception (tape_dbf_area,6,"xfrb segf.\n");
		rc = -EFAULT;
	}
	tape_sprintf_event (tape_dbf_area,6,"c:rbytes:  %x\n", cpysize);
	filp->f_pos += cpysize;
out_free:
	tape_free_ccw_req(treq);
out:
	return rc;
}

/*
 * Tape device write function
 */

ssize_t
tapechar_write (struct file *filp, const char *data, size_t count, loff_t * ppos)
{
	tape_dev_t *td;
	size_t block_size;
	tape_ccw_req_t *treq;
	int nblocks, i = 0,rc = 0;
	size_t written = 0;
	tape_sprintf_event (tape_dbf_area,6,"c:write\n");

	td = (tape_dev_t*)filp->private_data;
	block_size = count;
        
	if (ppos != &filp->f_pos) {
		/* "A request was outside the capabilities of the device." */
	        tape_sprintf_event (tape_dbf_area,6,"c:ppos wrong\n");
		rc = -EOVERFLOW; /* errno=75 Value too large for def. data type */
		goto out;
	}
	if ((td->char_data.block_size != 0) && (count < td->char_data.block_size)){
		rc = -EIO;
		goto out;
	}
	if (td->char_data.block_size == 0) {
		block_size = count;
		nblocks = 1;
	} else {
		block_size = td->char_data.block_size;
		nblocks = count / block_size;
	}
	tape_sprintf_event (tape_dbf_area,6,"c:nbytes: %x\n",block_size);
        tape_sprintf_event (tape_dbf_area,6,"c:nblocks: %x\n",nblocks);
	for (i = 0; i < nblocks; i++) {
		treq = td->discipline->write_block (data + i * block_size, block_size, td);
		if (!treq) {
			rc = -ENOBUFS;
			goto out;
		}

		rc = tape_do_io_and_wait(td,treq,TAPE_WAIT);
		TAPE_MERGE_RC(treq,rc);
		tape_free_ccw_req(treq);
		if(rc < 0)
			goto out;
	        tape_sprintf_event (tape_dbf_area,6,"c:wbytes: %x\n",block_size - td->devstat.rescnt); 
		filp->f_pos += block_size - td->devstat.rescnt;
		written += block_size - td->devstat.rescnt;
		rc = written;
		if (td->devstat.rescnt > 0)
			goto out;
	}
	tape_sprintf_event (tape_dbf_area,6,"c:wtotal: %x\n",written);

out:
	if (rc==-ENOSPC){
		if(td->discipline->process_eov)
			td->discipline->process_eov(td);
		if(i > 0){
			rc = i*block_size;
			printk("write rc = %i\n",rc); /* XXX */
		}	
	}
	return rc;
}

/*
 * MT IOCTLS
 */

static int
tapechar_mtioctop (struct file *filp, short mt_op, int mt_count)
{
	tape_dev_t *td;
	tape_ccw_req_t *treq = NULL;
	int rc = 0;

	td = (tape_dev_t*)filp->private_data; 

	tape_sprintf_event (tape_dbf_area,6,"c:mtio\n");
	tape_sprintf_event (tape_dbf_area,6,"c:ioop: %x\n",mt_op);
	tape_sprintf_event (tape_dbf_area,6,"c:arg:  %x\n",mt_count);

	switch (mt_op) {
		case MTRETEN:		// retension the tape
			treq = td->discipline->ioctl (td, MTEOM,1,&rc);
			break;
		case MTLOAD:
			treq = td->discipline->ioctl (td, MTLOAD,1,&rc);
			if (rc != -EINVAL){
				// the backend driver has an load function
				break; 
			} 
			// if no medium is in, wait until it gets inserted
			if (td->medium_state != MS_LOADED) {
				// create dummy request
				treq = tape_alloc_ccw_req(1,0,0,TO_LOAD);
				rc = tape_do_wait_req(td,treq,TAPE_WAIT_INTERRUPTIBLE_NOHALTIO);
			} else {
				rc = 0; // already loaded
			}
			goto out;
		case MTSETBLK:
			td->char_data.block_size = mt_count;
			tape_sprintf_event (tape_dbf_area,6,"c:setblk:\n");
			goto out;
		case MTRESET:
			td->char_data.block_size = 0;
			tape_sprintf_event (tape_dbf_area,6,"c:devreset:\n");
			goto out;
		default:
			treq = td->discipline->ioctl (td, mt_op,mt_count,&rc);
	}
	if (treq == NULL) {
	        tape_sprintf_event (tape_dbf_area,6,"c:ccwg fail\n");
		goto out;
	}

	if(TAPE_INTERRUPTIBLE_OP(mt_op)){
		rc = tape_do_io_and_wait(td,treq,TAPE_WAIT_INTERRUPTIBLE);
	} else {
		rc = tape_do_io_and_wait(td,treq,TAPE_WAIT);
	}
	TAPE_MERGE_RC(treq,rc);

	tape_free_ccw_req(treq);

	if ((mt_op == MTEOM) || (mt_op == MTRETEN)){
		rc = 0; // EOM and RETEN report an error, this is fine...
	}

	if(rc != 0)
		goto out; /* IO Failed */

	// if medium was unloaded, update the corresponding variable.
	switch (mt_op) {
	        case MTOFFL:
	        case MTUNLOAD:
			td->medium_state = MS_UNLOADED;
			break;
	        case MTRETEN:		//need to rewind the tape after moving to eom
			return tapechar_mtioctop (filp, MTREW, 1);
	        case MTFSFM:		//need to skip back over the filemark
			return tapechar_mtioctop (filp, MTBSFM, 1);
	        case MTBSF:		//need to skip forward over the filemark
			return tapechar_mtioctop (filp, MTFSF, 1);
	}
	tape_sprintf_event (tape_dbf_area,6,"c:mtio done\n");
out:
	return rc;
}

/*
 * Tape device io controls.
 */

int
tapechar_ioctl (struct inode *inode, struct file *filp,
	    unsigned int cmd, unsigned long arg)
{
	tape_dev_t *td;
	tape_ccw_req_t *treq = NULL;
	struct mtop op;		/* structure for MTIOCTOP */
	struct mtpos pos;	/* structure for MTIOCPOS */
	struct mtget get;
	int rc;

	tape_sprintf_event (tape_dbf_area,6,"c:ioct\n");

        td = (tape_dev_t*)filp->private_data;

	// check for discipline ioctl overloading
	if ((rc = td->discipline->discipline_ioctl_overload (td, cmd, arg)) != -EINVAL) {
		tape_sprintf_event (tape_dbf_area,6,"c:ioverloa\n");
		goto out;
	}
	rc = 0;
	switch (cmd) {
	case MTIOCTOP:		/* tape op command */
		if (copy_from_user (&op, (char *) arg, sizeof (struct mtop))) {
			rc = -EFAULT;
			goto out;
		}
		if(op.mt_count < 0){
			rc = -EINVAL;
			goto out;
		}
		if(op.mt_op == MTBSR  || 
		   op.mt_op == MTFSR  ||
		   op.mt_op == MTFSF  ||
		   op.mt_op == MTBSR  ||
		   op.mt_op == MTFSFM ||
		   op.mt_op == MTBSFM) 
		{
			int i;
			/* We assume that the backends can handle count up */	
			/* to 500. */
			for(i = 0; i < op.mt_count; i+=500){
				rc = tapechar_mtioctop (filp, op.mt_op, MIN(500,op.mt_count-i)); 
				if(rc)
					goto out;
			}
		} else {
			/* Single operations */
			rc = tapechar_mtioctop (filp, op.mt_op, op.mt_count);
		}
		goto out;

	case MTIOCPOS:		/* query tape position */
		memset (&pos,0,sizeof (struct mtpos));
		treq = td->discipline->ioctl (td, MTTELL,1,&rc);
		if(!treq)
			goto out;
		rc = tape_do_io_and_wait(td,treq,TAPE_WAIT); 
		TAPE_MERGE_RC(treq,rc);
		if(rc == 0){
			pos.mt_blkno = *((int*)(treq->kernbuf));
			if (copy_to_user ((char *) arg, &pos, sizeof (struct mtpos)))
				rc = -EFAULT;
		}
		tape_free_ccw_req(treq);
		goto out;

	case MTIOCGET:
		memset (&get,0,sizeof (struct mtget));
		treq = td->discipline->ioctl (td, MTTELL,1,&rc);
		if(!treq)
                        goto out;
		rc = tape_do_io_and_wait(td,treq,TAPE_WAIT);
		TAPE_MERGE_RC(treq,rc);
		if(rc == 0){
			get.mt_erreg = treq->rc;
			get.mt_blkno = *((int*)(treq->kernbuf));
			get.mt_type = MT_ISUNKNOWN;
			get.mt_resid = td->devstat.rescnt;
			get.mt_dsreg = td->tape_state;
			if (copy_to_user ((char *) arg, &get, sizeof (struct mtget)))
				rc = -EFAULT;	
		}
		tape_free_ccw_req(treq);
		goto out;
	default:
	        tape_sprintf_event (tape_dbf_area,3,"c:ioct inv\n");
		rc = -EINVAL;
		goto out;
	}
out:
	return rc;
}

/*
 * Tape device open function.
 */

int
tapechar_open (struct inode *inode, struct file *filp)
{
	tape_dev_t *td = NULL;
	int  rc = 0;
	long lockflags;

	MOD_INC_USE_COUNT;

	tape_sprintf_event (tape_dbf_area,6,"c:open: %x\n",td->first_minor); 

	inode = filp->f_dentry->d_inode;

	td = tape_get_device_by_minor(minor (inode->i_rdev));
	if (td == NULL){
		rc = -ENODEV;
		goto error;
	}
	s390irq_spin_lock_irqsave(td->devinfo.irq,lockflags);
	if (tape_state_get(td) == TS_NOT_OPER) {
		tape_sprintf_event (tape_dbf_area,6,"c:nodev\n");
		rc = -ENODEV;
		goto out;
	}

	if (tape_state_get (td) != TS_UNUSED) {
		tape_sprintf_event (tape_dbf_area,6,"c:dbusy\n");
		rc = -EBUSY;
		goto out;
	}
	if ( td->discipline->owner )
		__MOD_INC_USE_COUNT(td->discipline->owner);
	tape_state_set (td, TS_IN_USE);
	td->filp = filp; /* save for later reference     */
	filp->private_data = td;    /* save the dev.info for later reference */
out:
	s390irq_spin_unlock_irqrestore(td->devinfo.irq,lockflags);
error:
	if(rc != 0){
		MOD_DEC_USE_COUNT;
		if (td!=NULL)
			tape_put_device(td);
	}
	return rc;

}

/*
 * Tape device release function.
 */

int
tapechar_release (struct inode *inode, struct file *filp)
{
	tape_dev_t *td = NULL;
	tape_ccw_req_t *treq = NULL;
	int rc = 0;
	long lockflags;

	tape_sprintf_event (tape_dbf_area,6,"c:release: %x\n",td->first_minor);

	td = (tape_dev_t*)filp->private_data;

	if(td->last_op == TO_WRI)
		tapechar_terminate_write(td);

	if (minor (inode->i_rdev) == TAPECHAR_REW_MINOR(td->first_minor)) {
		treq = td->discipline->ioctl (td, MTREW,1,&rc);
		if (treq != NULL) {
		        tape_sprintf_event (tape_dbf_area,6,"c:rewrelea\n");
			rc = tape_do_io_and_wait(td, treq, TAPE_WAIT);
			tape_free_ccw_req (treq);
		}
	}

	s390irq_spin_lock_irqsave(td->devinfo.irq,lockflags);
	if(tape_state_get(td) == TS_IN_USE)
		tape_state_set (td, TS_UNUSED);
	else if (tape_state_get(td) != TS_NOT_OPER) 
		BUG();
	s390irq_spin_unlock_irqrestore(td->devinfo.irq,lockflags);
	if ( td->discipline->owner )
		__MOD_DEC_USE_COUNT(td->discipline->owner);
	tape_put_device(td);
	MOD_DEC_USE_COUNT;
	return rc;
}
