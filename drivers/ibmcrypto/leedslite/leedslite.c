/* drivers/char/leedslite.c: An IBM Crypto Adapter driver for Linux. */
/* Copyright (c) International Business Machines Corp., 2001 */
/* 
  NOTICE: Have only developed test on 2.2.18+ or 2.4.0+ kernels.

	Written 2000-2001 by Jon Grimm

	Version history:
	YYYY Mon DD First Lastname <email@host>
		Change Description
    2003 Nov 18 Serge Hallyn <sergeh@us.ibm.com>
	    Separate 2.4 and 2.6 driver code.
    2003 Nov 10 Serge Hallyn <sergeh@us.ibm.com>
    	    Update for 2.6 kernel (with chardev ripped out of devfs)
    2001 Mar 05 Jon Grimm <jgrimm@us.ibm.com>
	    Fix multi-thread bug in devica (devica interface changed)
		Enable REE bits _after_ chip enabled
		Move signal handling so we complete request if I/O is completed.

*/


#if !defined(__OPTIMIZE__)  ||  !defined(__KERNEL__)
#warning  You must compile this file with the correct options!
#warning  See the last lines of the source file.
#error You must compile this driver with "-O".
#endif

#include <linux/config.h>
#include <linux/version.h>
#include <linux/module.h>

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/devfs_fs_kernel.h>
#include <linux/kmod.h>
#include <linux/list.h>
#include <asm/uaccess.h>

#include <asm/bitops.h>
#include <asm/io.h>
#include <linux/delay.h>




#include <linux/icaioctl.h>
#include "leedslite.h"


#define DESBUFFER_SIZE 4096

typedef struct rsa_free {
	struct list_head freelist;
	int index;
	wait_queue_head_t wait;
	leedslite_vfifo_entry_t vfstatus;
	int status;
} rsa_free_t;

typedef struct devfs_entry *devfs_handle_t;

typedef struct leedslite_dev {
   
	long                      ioaddr;
	char *                    window1;
	char *                    window2;
	int                       irq;
	struct pci_dev *          pdev;

	
	wait_queue_head_t         rng_current_wait;
	atomic_t                  entropy_available;

	struct semaphore          des_wait;
	wait_queue_head_t         des_current_wait;
	char *                    desbuffer;  
	dma_addr_t                des_handle;
	int                       des_status;

	
	struct semaphore          rsa_wait;
	rsa_free_t *              rsa_freelist;
	struct list_head          rsa_freelist_head;
	leedslite_rip_entry_t *   rip;
	leedslite_rop_entry_t *   rop;
	leedslite_vfifo_entry_t * vfifo; 
	dma_addr_t                rip_handle;
	dma_addr_t                rop_handle;
	dma_addr_t                vfifo_handle;
	int                       lwp;     

	ica_worker_t              icareg;
	int                       bind;
	devfs_handle_t            icahandle;
	int			  minor;
	
	struct list_head          node;
} leedslite_dev_t;



typedef struct _camelot_operation {
	int (*verify)(leedslite_dev_t *dev, unsigned int cmd, unsigned long arg);
	int (*execute)(leedslite_dev_t *dev, unsigned int cmd, unsigned long arg);
} camelot_op_t; 

typedef struct _rsa_operation {
	int (*verify)(leedslite_dev_t *dev, unsigned int cmd, unsigned long arg);
	int (*execute)(leedslite_dev_t *dev, unsigned int cmd, unsigned long arg, rsa_free_t *entry);
} rsa_op_t; 
	

static const char *version =
"leedslite.c:v0.22 05/27/03 Jon Grimm (c) IBM Corp.";


static int driver_major;
static int devica = 1;          /* register with /dev/ica partition */    
static int maxdevices = 12;
static int desbuffersize = 8192;
static int rsabufs = LEEDSLITE_VFIFO_ENTRIES; 
static int sam = SAM_MIN;
static int pmwi = 1;     
static int error_timeout = LEEDSLITE_ERR_TIMEOUT_MIN;

static leedslite_dev_t **devices;

static struct list_head devicelist;

static int bigendian = 0; /* FLAG to determine if big endian or not */



/* fcn to determine at runtime the endianess of the platform */
int
isbigendian(void) {

#define LE  0x2143 
#define BE  0x4321

unsigned short *value;
unsigned char p[2];

p[0] = 0x43;
p[1] = 0x21;
value = (unsigned short *)p;

if ( *value == BE )
    return(1);
else 
     return(0);

}


static inline int camelot_gcra_enable_rsa(leedslite_dev_t *dev)
{
	unsigned int gcra, rcr;
	unsigned int i;

	gcra = readw(dev->window2+REG_GCR);
	gcra |= CAM_GCRA_RSA_Enable;
		
	writew(gcra, dev->window2+REG_GCR);	

    /* 
	 * Per engineers poll the safety bit.   Limit the polling
	 * loop so we don't spin here forever.  Shouldn't happen 
	 * but I don't like endless loops. 
	 */

	i=200; 
	do {
		udelay(1000);  
		rcr = readw(dev->window2 + REG_CAM_RCR);
		i--;
		
	} while (!(rcr & CAM_RCR_COMPLETE) && i );

	return (rcr & CAM_RCR_COMPLETE);
}

static inline void camelot_rscr_enable_ree(leedslite_dev_t *dev, int id)
{
	int rscr;

	/* 
	 * Convert CamelotID to REE bit 
	 * FYI, id is value 1-5 (as opposed to zero-based index)
	 */

	id = 1 << (id-1);
	id <<= 8;  

	/* Turn on the appropriate REE bit */

	rscr = inw(dev->ioaddr + REG_RSCR);
	rscr |= id;
	
	outw(rscr, dev->ioaddr + REG_RSCR);
}




static inline void camelot_gsra_enable_rng(leedslite_dev_t *dev){
	int gsra;

	/* Enable RNG Registers */

	gsra = readw(dev->window2 + REG_GSRA);
	gsra |=  CAM_GSRA_RNG_INT_EN;		
	
	/* Set for rng for maximum speed */

	gsra |=  CAM_GSRA_RNGSpd0 | CAM_GSRA_RNGSpd1 | CAM_GSRA_RNGSpd2;  
	writew(gsra, dev->window2 + REG_GSRA);
}

static inline void camelot_gsr_enable_des(leedslite_dev_t *dev){
	int gsr;

	/* Enable SHA registers */

	gsr = readw(dev->window2 + REG_GSR);
	gsr |= 1<<10;			
	writew(gsr, dev->window2 + REG_GSR);
}

static inline void camelot_gsra_enable_sha(leedslite_dev_t *dev){
	int gsra;
	int ctrl;

	/* Enable SHA registers */

	gsra = readw(dev->window2 + REG_GSRA);
	gsra |=  CAM_GSRA_SHA_EN;			
	writew(gsra, dev->window2 + REG_GSRA);

	/* Prime SHA variables with FIPS-180 values */

	ctrl = SHA_KEY_CONTROL_SET_H | SHA_KEY_CONTROL_SET_K;
	writew(ctrl, dev->window2 + REG_SHA_KEY_CONTROL);
}

static inline void camelot_enable_cam1(leedslite_dev_t *dev)
{
	
	camelot_gsr_enable_des(dev);
	camelot_gsra_enable_sha(dev);   

	/* Clear any pending RNG data 
	 *    RNG interrupt won't fire if there is already data available
	 *    Let's clear it here, then we will know our state from here
     *    on out.
	 */
	readl(dev->window2 + REG_RNG);
	readl(dev->window2 + REG_RNG +4);

	/*
	 * Make sure the Selected Camelot will start generating random numbers.
	 * May have already been enabled.   
	 */

	camelot_gsra_enable_rng(dev);
}

static inline void camelot_select(leedslite_dev_t *dev, int id)
{
	int oscr;
	
	oscr = inw(dev->ioaddr+REG_OSCR);
	oscr &= ~OSCR_CSV;
	oscr = (id << OSCR_CSV_SHIFT);
	
	outw(oscr, dev->ioaddr+REG_OSCR);
}

static inline void camelot_reset(leedslite_dev_t *dev, int id)
{
	unsigned int oscr;
	
	/* Select the offending camelot */

	camelot_select(dev, id);
	
	/* Go reset the identified Camelot */

	oscr = inw(dev->ioaddr+REG_OSCR);
	oscr |= 1 << (id-1);
	outw(oscr, dev->ioaddr+REG_OSCR);

	udelay(1);             // Reset needs min 500 ns delay

	oscr &= ~OSCR_Cam_RST;
	outw(oscr, dev->ioaddr+REG_OSCR);

	/* Now go turn RSA back on... however beware, the Camelot needs
	 * a bit of time to initialize...  per the engineers, poll on
	 * the COMPLETE bit
	 */

	if (!camelot_gcra_enable_rsa(dev))
		printk(KERN_ERR "leedslite: unable to reset camelot %d\n", id);

	/* Another safety delay */

	udelay(1000);

	/* Make sure we put back the selected camelot back to 1 */
	
	camelot_select(dev, 1);

	/* 
	 * If this was camelot #1, we also need to go renable rng, 
	 * des, & sha.
	 */

	if (id == 1)
		camelot_enable_cam1(dev);
	   		
}

static inline void camelot_des_csr_reset(leedslite_dev_t *dev){
	int csr;

	csr = readw(dev->window2 + REG_DES_CSR);
	csr &=  ~CAM_DESCSR_START;
	writew(0, dev->window2 + REG_DES_CSR);
}


/*
 * Read operation on rng device. 
 *    The adapter generates a 64-bit random number upon request.
 *  
 *    The code below may look a little odd as my goal is to keep this
 * as asynchronous as possible.  Instead of clocking the rng generation
 * by toggling the rng interrupt enable bit, I'll leave it free running
 * with a new rng being generated any time I read out the previous
 * rng data.   
 *    
 * A thread will atomically check the entropy_available field, if no
 * entropy is available, it will go to sleep (via 'schedule').  
 * 'add_wait_queue' will put the entry at the front of the queue. 
 * 
 */
static ssize_t rng_read_worker(struct file * filp, char * buf, size_t nbytes, loff_t *ppos, void *private)
{
	DECLARE_WAITQUEUE(current_wait, current);
	ssize_t count;
	ssize_t read;
	ssize_t error = 0;
	leedslite_dev_t *dev = (leedslite_dev_t *)private;
	u32 loc_buf[2];

 	count = 0;
	
	//printk("nbytes: %d\nbuf: %p\n", nbytes, buf);

	while(nbytes - count){
				
		/* 
		 * If there is no data already available for us,
		 * wait until the interrupt handler wakes us up.
		 *
		 * Note: we put our selves on the wait_queue and have marked
		 * ourselves, as sleeping before we check entropy_availalbe.
		 * 
		 * If the interrupt handler fires before we check the entropy
		 * available, the handler will mark us RUNNING and the schedule()
		 * will do nothing.
		 *
		 * Otherwise, we'll go to sleep and the interrupt handler will
		 * wake us.  
		 *
		 */
		init_waitqueue_entry(&current_wait, current);

		set_current_state(TASK_INTERRUPTIBLE);
       

		
#ifdef WQ_FLAG_EXCLUSIVE
			add_wait_queue_exclusive(&dev->rng_current_wait, &current_wait);
#else
			add_wait_queue(&dev->rng_current_wait, &current_wait);
#endif
						
	   
		if (atomic_read(&dev->entropy_available)){
			atomic_set(&dev->entropy_available, 0);
			set_current_state(TASK_RUNNING);

			read = (nbytes-count) > 8 ? 8 : nbytes-count;

                       /* Read out the full 8 bytes of random data that the card
                        * provides here, in order to trigger it to provide 8 more.
                        * Then only copy_to_user what we need. Doing a copy_to_user
                        * directly from the card breaks ppc64. - KEY
                        */
                       loc_buf[0] = readl(dev->window2+REG_RNG);
                       loc_buf[1] = readl(dev->window2+REG_RNG+4);

                       if (copy_to_user(buf+count, loc_buf, read))
                               error = -EFAULT;

			count+=read;
			
			remove_wait_queue(&dev->rng_current_wait, &current_wait);
			if (error)
				goto out;
			
		} else {
			
			schedule();
			
			set_current_state(TASK_RUNNING);
			remove_wait_queue(&dev->rng_current_wait, &current_wait);

			/*
			 * If we got interrupted by a signal, remove ourselves from
			 * all the queues and let the next up task in to stage 2.
			 */

			if (signal_pending(current)){
				return -ERESTARTSYS;
			}
			
		} 
			
	      		
   		
	}

 out:	
	return count;
}

static ssize_t rng_read(struct file * filp, char * buf, size_t nbytes, loff_t *ppos)
{
	void *private = filp->private_data;	

	return rng_read_worker(filp, buf, nbytes, ppos, private);
}


static struct file_operations ica_fops;

static int ica_open(struct inode *inode, struct file *filp)
{
	unsigned slot;
	int rc;
	
	slot = MINOR(inode->i_rdev);
	
	if(slot >= maxdevices){
		assertk(slot < maxdevices);
		rc =  -ENODEV;
		goto err_open;
	}

	
	filp->private_data = devices[slot];

	if(!filp->private_data){
		rc = -ENODEV;
		goto err_open;
	}
	
	
	assertk(filp->f_op == &ica_fops);
	
	return 0;

 err_open:
	return rc;
	
}



static int ica_release(struct inode *inode, struct file *filp)
{
	return 0;
}



static int ica_ioctl_setbind(struct inode *inode, struct file *filp, unsigned int cmd, unsigned long arg, leedslite_dev_t *dev)
{
	int bind;
	int err;

	err = get_user(bind, (int *)arg);	

	if(!err){

		
		if(bind != dev->bind){
	   	   
			if(bind < 0){
				bind = -1;
				err = ica_unregister_worker(dev->bind, &dev->icareg);
			} else {
				ica_unregister_worker(dev->bind, &dev->icareg);
				err = ica_register_worker(bind, &dev->icareg);
				if(err)
					dev->bind = -1;
			}
			if(!err){
				dev->bind = bind;
			} 
		}
	}
		
	return err;
}

static int ica_ioctl_getbind(struct inode *inode, struct file *filp, unsigned int cmd, unsigned long arg, leedslite_dev_t *dev)
{
	int err;

	err = put_user(dev->bind, (int *)arg);

	return err;
}

static int ica_ioctl_getcount(struct inode *inode, struct file *filp, unsigned int cmd, unsigned long arg, leedslite_dev_t *dev)
{
	return 0;
}

static int ica_ioctl_getid(struct inode *inode, struct file *filp, unsigned int cmd, unsigned long arg, leedslite_dev_t *dev)
{
	return 0;
}


static int des_verify(leedslite_dev_t *dev, unsigned int cmd, unsigned long arg){ 
	ica_des_t pkt;
	int keysize;
	ica_des_vector_t kiv;
	ica_des_key_t kkeys[3];

	
	if(copy_from_user(&pkt, (ica_des_t *)arg, sizeof(ica_des_t)))
		return -EFAULT;  

	if((pkt.inputdatalength > ICA_DES_DATALENGTH_MAX) || (pkt.outputdatalength > ICA_DES_DATALENGTH_MAX))
		return -EINVAL;


	keysize = (cmd == ICATDES) ? sizeof(ica_des_triple_t) : sizeof(ica_des_single_t);
	
	if((pkt.mode != DEVICA_MODE_DES_ECB) && (pkt.mode != DEVICA_MODE_DES_CBC)) 
		return -EINVAL;
	
	if((pkt.direction != DEVICA_DIR_DES_ENCRYPT) && (pkt.direction != DEVICA_DIR_DES_DECRYPT))
		return -EINVAL;
	
	if(pkt.outputdatalength < pkt.inputdatalength)
		return -EINVAL;
	
	if((pkt.inputdatalength % ICA_DES_DATALENGTH_MIN) != 0)
		return -EINVAL;


	/* 
	 * Go ahead and read the iv and key to see if the pointers are good, do
	 * this by copying to kernel space... the xxx_user macros do all sorts of 
	 * exception handling magic to return -EFAULT when invalid.   Seems
	 * easier doing this here than clutter up some of the i/o code. 
	 */
	
	if(copy_from_user(&kiv, pkt.iv, sizeof(ica_des_vector_t)))
		return -EFAULT;
	

	if(copy_from_user(kkeys, pkt.keys, keysize))
		return -EFAULT;

	return 0;
}


#define MAX_BUF_SIZE DESBUFFER_SIZE 


static inline void camelot_des_iv_write(leedslite_dev_t *dev, ica_des_vector_t *iv)
{
	int i;
	u32 tmp;
	
	//for( i = 0; i < sizeof(ica_des_vector_t); i++)
	//	writeb((char *)iv[i], dev->window2 + REG_DES_IV + i);

	for( i = 0; i < sizeof(ica_des_vector_t)/sizeof(u32); i++) {
		//((u32 *)iv)[i] = __cpu_to_le32( ((u32 *)iv)[i] );
		tmp = __cpu_to_le32( ((u32 *)iv)[i] );
		//writel((u32 *)iv[i], dev->window2 + REG_DES_IV + (i*sizeof(u32)));
		writel(tmp, dev->window2 + REG_DES_IV + (i*sizeof(u32)));
	}

	//memcpy(dev->window2 + REG_DES_IV, iv, sizeof(ica_des_vector_t));
}

static inline void camelot_des_key_write(leedslite_dev_t *dev, ica_des_key_t *key, int index)
{
	int offset, i;
	u32 tmp;

	offset = REG_DES_KEY_1;
	offset += index * sizeof(ica_des_key_t);
	
	for( i = 0; i < sizeof(ica_des_key_t)/sizeof(u32); i++) {
		tmp = __cpu_to_le32( ((u32 *)key)[i] );
		writel(tmp, dev->window2 + offset + (i*sizeof(u32)));
		//((u32 *)key)[i] = __cpu_to_le32( ((u32 *)key)[i] );
		//writel((u32 *)key[i], dev->window2 + offset + (i*sizeof(u32)));
	}

	//for( i = 0; i < sizeof(ica_des_key_t); i++)
	//	writeb((char *)key[i], dev->window2 + offset + i);
	//memcpy(dev->window2 + offset, key, sizeof(ica_des_key_t));
}

static void camelot_setup_key_regs(leedslite_dev_t *dev, unsigned int opcode, ica_des_vector_t *iv, ica_des_key_t *keys)
{

	/* Write iv out to the Camelot */
	if(!(opcode & DCR_ECB))
		camelot_des_iv_write(dev, iv);
	
	/* Write keys out to the Camelot */		
	
	camelot_des_key_write(dev, &keys[0], 0);
	
	if(opcode & DCR_TDES){
		camelot_des_key_write(dev, &keys[1], 1);
		camelot_des_key_write(dev, &keys[2], 2);
	}

}



static int des_execute_common(leedslite_dev_t *dev, unsigned int opcode, char *input, char *output, int bytesleft, ica_des_vector_t *iv, ica_des_key_t *keys)
{
	DECLARE_WAITQUEUE(wait, current);
	unsigned int timeleft;
	int offset;
	int transfercount;
	int command;
	int setup;
	int gsr;

	setup = 0;
	offset = 0;

	/* Clear out the DES start bit, so the IV will be read. */

	camelot_des_csr_reset(dev);
	
	/*
	 * This does an explicit DES enable on the card before we
	 * issue the card the command. Under stress this bit may
	 * not be set, which would cause incorrect results.
	 */
	gsr = readw(dev->window2 + REG_GSR);
	gsr |=  CAM_GSR_DES_EN;
	writew(gsr, dev->window2 + REG_GSR);

		
	while(bytesleft){	

		/*
		 * If we got a signal, don't continue on.  
		 */

		if(signal_pending(current)){
			//printk(KERN_INFO "got signal des\n");
			return -ERESTARTSYS;
		}

		
		transfercount = (bytesleft >= MAX_BUF_SIZE) ? MAX_BUF_SIZE : bytesleft;

		if(copy_from_user(dev->desbuffer, input+offset, transfercount))
			return -EFAULT;
		
		/* Set up the DES Command Register */

		
		command = opcode | (_dlen(transfercount) << DCR_DLen_SHIFT);
		outl(command, dev->ioaddr + REG_DCR);
		
		
		/* 
		 * Only need to write the keys out once to the Camelot.
		 * Note: I've already touched the data in the verify routine
		 * to make sure the pointers are good.
		 */

		
		if(!setup){
			camelot_setup_key_regs(dev, opcode, iv, keys);			
			setup = 1;
		}


		/* Getting ready for the interrupt handler to wake us.
		 * Pretend we are sleeping, If interrupt handler fires before
		 * we actually schedule, the handler will mark us as Running, 
		 * otherwise, we'll actually sleep at the schedule()
		 * I'm going to mark this as TASK_UNINTERRUPTIBLE, as we are
		 * doing DMA; it would not be good for our buffer to go away.
		 */

		set_current_state(TASK_UNINTERRUPTIBLE);
		add_wait_queue(&dev->des_current_wait, &wait);

		
		/* Setup up the Busmastering channels, the command will start
		   as soon as the count gets configured on channel 2 */
  
		/* DES to PCI Setup */

		outl(dev->des_handle, dev->ioaddr+REG_BMAR3);	
		outl(transfercount, dev->ioaddr+REG_BMCR3);
		
		
		/* PCI to DES Setup, once the count is fired I/O commences */

		outl(dev->des_handle, dev->ioaddr+REG_BMAR2);
		outl(transfercount, dev->ioaddr+REG_BMCR2);	

		
		/* Wait for completion */
		timeleft = schedule_timeout(HZ * error_timeout);		
	  
		
		remove_wait_queue(&dev->des_current_wait, &wait);
		
		/* 
		 * If we've timed out it is likely we have some hardware 
		 * problem
		 */

		if(!timeleft){
			printk(KERN_ERR "leedslite: des DMA timeout!\n");		
			return -EIO;
		}


		if(!dev->des_status){
		    
			if(copy_to_user(output+offset, dev->desbuffer, transfercount))
				return -EFAULT;
			   
			bytesleft -= transfercount;
			offset += transfercount;
		}

	}

	return 0;	
} 





static int des_execute(leedslite_dev_t *dev, unsigned int cmd, unsigned long arg)
{
	ica_des_t *pkt;
	unsigned int opcode;

	pkt = (ica_des_t *)arg;

	opcode = (pkt->direction == DEVICA_DIR_DES_ENCRYPT) ? DCR_Opcode_DES_Enc : DCR_Opcode_DES_Dec;
	opcode |= (cmd == ICATDES) ? DCR_TDES : 0;
	opcode |= (pkt->mode == DEVICA_MODE_DES_ECB) ? DCR_ECB : 0;


	/*  Call common (to DES/TDES) worker */

	return des_execute_common(dev,
							  opcode,
							  pkt->inputdata,
							  pkt->outputdata,
							  pkt->inputdatalength,
							  pkt->iv,
							  pkt->keys);
							  							  
}

camelot_op_t des_op = {
	des_verify,
	des_execute
};


static int desmac_verify(leedslite_dev_t *dev, unsigned int cmd, unsigned long arg){ 
	ica_desmac_t pkt;
	int keysize;
	ica_des_vector_t kiv;
	ica_des_key_t kkeys[3];

	
	if(copy_from_user(&pkt, (ica_desmac_t *)arg, sizeof(ica_desmac_t)))
		return -EFAULT;  
	
	if((pkt.inputdatalength > ICA_DES_DATALENGTH_MAX) || (pkt.outputdatalength > ICA_DES_DATALENGTH_MAX))
		return -EINVAL;

	keysize = (cmd == ICATDES) ? sizeof(ica_des_triple_t) : sizeof(ica_des_single_t);

	if(pkt.outputdatalength < pkt.inputdatalength)
		return -EINVAL;

	if((pkt.inputdatalength % ICA_DES_DATALENGTH_MIN) != 0)
		return -EINVAL;

	/* 
	 * Go ahead and read the iv and key to see if the pointers are good, do
	 * this by copying to kernel space... the xxx_user macros do all sorts of 
	 * exception handling magic to return -EFAULT when invalid.   Seems
	 * easier doing this here than clutter up some of the i/o code. 
	 */

	if(copy_from_user(&kiv, pkt.iv, sizeof(ica_des_vector_t)))
		return -EFAULT;

	if(copy_from_user(kkeys, pkt.keys, keysize))
		return -EFAULT;

	return 0;
}




/* This routine differs from des_execute_common in that the output buffer is only actually read back out after all input data has been processed */

static int desmac_execute_common(leedslite_dev_t *dev, unsigned int opcode, char *input, char *output, int bytesleft, ica_des_vector_t *iv, ica_des_key_t *keys)
{
	DECLARE_WAITQUEUE(wait, current);
	unsigned int timeleft;
	int offset;
	int transfercount;
	int command;
	int setup, i;
	u32 loc_buf[ICA_DES_DATALENGTH_MIN/sizeof(u32)];

	setup = 0;
	offset = 0;

	printk("LEEDS DMAC_COMMON: output: %p\n", output);

	/* Clear out the DES start bit, so the IV will be read. */

	camelot_des_csr_reset(dev);

	while(bytesleft){	

		/*
		 * If we got a signal, don't continue on.   
		 */

		if(signal_pending(current)){
			//printk(KERN_INFO "got signal desmac\n");
			return -ERESTARTSYS;
		}
		
		
		transfercount = (bytesleft >= MAX_BUF_SIZE) ? MAX_BUF_SIZE : bytesleft;

		if(copy_from_user(dev->desbuffer, input + offset, transfercount))
			return -EFAULT;
		
		/* Set up the DES Command Register */

		command = opcode | (_dlen(transfercount) << DCR_DLen_SHIFT);
		command |= DCR_INT_back;
		
		outl(command, dev->ioaddr + REG_DCR);	
		
		/* 
		 * Only need to write the keys out once to the Camelot.
		 * Note: I've already touched the data in the verify routine
		 * to make sure the pointers are good.
		 */		

		if(!setup){	
			camelot_setup_key_regs(dev, opcode, iv, keys);			
			setup = 1;
		}


		/* Getting ready for the interrupt handler to wake us.
		 * Pretend we are sleeping, If interrupt handler fires before
		 * we actually schedule, the handler will mark us as Running, 
		 * otherwise, we'll actually sleep at the schedule()
		 * I'm going to mark this as TASK_UNINTERRUPTIBLE, as we are
		 * doing DMA; it would not be good for our buffer to go away.
		 */

		set_current_state(TASK_UNINTERRUPTIBLE);	
		add_wait_queue(&dev->des_current_wait, &wait);
		
		
		/* Setup up the Busmastering channels, the command will start
		   as soon as the count gets configured on channel 2 */
  
		/* PCI to DES Setup, once the count is fired I/O commences */

		outl(dev->des_handle, dev->ioaddr+REG_BMAR2);
		outl(transfercount, dev->ioaddr+REG_BMCR2);	

		
		/* Wait for completion */
		timeleft = schedule_timeout(HZ * error_timeout);


		remove_wait_queue(&dev->des_current_wait, &wait);		

		/* 
		 * If we've timed out it is likely we have some hardware 
		 * problem
		 */

		if(!timeleft){
			printk(KERN_ERR "leedslite: desmac DMA timeout!\n");		
			return -EIO;
		}

	   
		bytesleft -= transfercount;
		offset += transfercount;
	}

	for(i=0; i < ICA_DES_DATALENGTH_MIN/sizeof(u32); i++) {
		loc_buf[i] = readl(dev->window2 + REG_DES_OUT + (i*sizeof(u32)));
		//loc_buf[i] = __le32_to_cpu(loc_buf[i]);
	}

	if(copy_to_user(output, loc_buf, ICA_DES_DATALENGTH_MIN))
		return -EFAULT;

	//if(copy_to_user(output, dev->window2+REG_DES_OUT, ICA_DES_DATALENGTH_MIN))
	//	return -EFAULT;



	
	return 0;	
} 


static int desmac_execute(leedslite_dev_t *dev, unsigned int cmd, unsigned long arg)
{
	ica_desmac_t *pkt;
	unsigned int opcode;

	pkt = (ica_desmac_t *)arg;

	opcode = DCR_Opcode_MAC;
	opcode |= (cmd == ICATDESMAC) ? DCR_TDES : 0;


	/*  Call common (to DES/TDES) worker */

	return desmac_execute_common(dev,
								 opcode,
								 pkt->inputdata,
								 pkt->outputdata,
								 pkt->inputdatalength,
								 pkt->iv,
								 pkt->keys);
							  							  
}



camelot_op_t desmac_op = {
	desmac_verify, 
	desmac_execute
}; 



static int sha1_verify(leedslite_dev_t *dev, unsigned int cmd, unsigned long arg){ 
	ica_sha1_t pkt;

	if(copy_from_user(&pkt, (ica_sha1_t *)arg, sizeof(ica_sha1_t)))
		return -EFAULT;   
	
	if((pkt.inputdatalength < ICA_SHA_BLOCKLENGTH) || (pkt.inputdatalength > ICA_DES_DATALENGTH_MAX))
		return -EINVAL;

	if((pkt.inputdatalength % ICA_SHA_BLOCKLENGTH) != 0)
		return -EINVAL;
	
	
	return 0;
}


static int sha1_execute_common(leedslite_dev_t *dev, unsigned int opcode, char *input, char *output, int bytesleft, char *initialh)
{
	DECLARE_WAITQUEUE(wait, current);
	unsigned int timeleft;
	int offset;
	int transfercount;
	int command;
	int setup, i, gsra;
	u32 loc_buf[ICA_SHA_DATALENGTH/sizeof(u32)];

	setup = 0;
	offset = 0;

	/*
	 * This does an explicit SHA enable on the card before we
	 * issue the card the command. Under stress this bit may
	 * not be set, which would cause incorrect results.
	 */
	gsra = readw(dev->window2 + REG_GSRA);
	gsra |=  CAM_GSRA_SHA_EN;
	writew(gsra, dev->window2 + REG_GSRA);

	
	while(bytesleft){	

		/*
		 * If we got a signal, don't continue on.   
		 */

		if(signal_pending(current)){
			//printk(KERN_INFO "got signal sha\n");
			return -ERESTARTSYS;
		}
		
		transfercount = (bytesleft >= MAX_BUF_SIZE) ? MAX_BUF_SIZE : bytesleft;

		//printk("transfercount: %d\n", transfercount);

		if(copy_from_user(dev->desbuffer, input + offset, transfercount))
			return -EFAULT;
	   
#if 0
                for(i=0;i<transfercount;i++){
                      printk("%0x ", *(((unsigned char *)dev->desbuffer) + i));
                 }

                 printk("\n");
#endif	

		/* Set up the DES Command Regi(ter */

		
		command = opcode | (_dlen(transfercount) << DCR_DLen_SHIFT);
		command |= DCR_INT_back;

		/* 
		 *  Initialize the H Constants, either to FIPS values or to
		 *  user input data.   User data is useful for chaining together
		 *  larger requests.   Only need to do this first time through.
		 */

		if(!setup){
			setup = 1;

			if(initialh){
				/* Copy to an intermediate buffer to make ppc64 happy. - KEY */
				if(copy_from_user(loc_buf, initialh, ICA_SHA_DATALENGTH))
					return -EFAULT;
#if 0
				printk("initialh: ");
				   for(i=0;i<ICA_SHA_DATALENGTH;i++){
				      printk("%0x ", *(((unsigned char *)loc_buf) + i));
				    }

				  printk("\n");
#endif
				for( i = 0; i < ICA_SHA_DATALENGTH/sizeof(u32); i++ ) {
					loc_buf[i] = __cpu_to_le32(loc_buf[i]);
					writel(loc_buf[i], dev->window2 + REG_SHA_OUT0 + (i*sizeof(u32)));
				}

				

				//if(copy_from_user(dev->window2+REG_SHA_OUT0, initialh, ICA_SHA_DATALENGTH))
				//	return -EFAULT;
			} else {
				command |= DCR_init_FIPS;
			}
		}

		
		outl(command, dev->ioaddr + REG_DCR);
		

		
		/* Getting ready for the interrupt handler to wake us.
		 * Pretend we are sleeping, If interrupt handler fires before
		 * we actually schedule, the handler will mark us as Running, 
		 * otherwise, we'll actually sleep at the schedule()
		 * I'm going to mark this as TASK_UNINTERRUPTIBLE, as we are
		 * doing DMA; it would not be good for our buffer to go away.
		 */
	
		set_current_state(TASK_UNINTERRUPTIBLE);
		add_wait_queue(&dev->des_current_wait, &wait);
		
		
		/* Setup up the Busmastering channels, the command will start
		   as soon as the count gets configured on channel 2 */
  
		/* PCI to DES Setup, once the count is fired I/O commences */

		outl(dev->des_handle, dev->ioaddr+REG_BMAR2);
		outl(transfercount, dev->ioaddr+REG_BMCR2);	
		
		/* Wait for completion */
		timeleft = schedule_timeout(HZ * error_timeout);

		/* 
		 * If we've timed out it is likely we have some hardware 
		 * problem
		 */

		if(!timeleft){
			printk(KERN_ERR "leedslite: sha1 DMA timeout!\n");		
			return -EIO;
		}		

		remove_wait_queue(&dev->des_current_wait, &wait);		
	   
		bytesleft -= transfercount;
		offset += transfercount;
	}
	
	//printk("loc_buf: ");
	/* Read into an intermediate buffer to make ppc64 happy. - KEY */
	for( i = 0; i < ICA_SHA_DATALENGTH/sizeof(u32); i++ ) {
		loc_buf[i] = readl(dev->window2 + REG_SHA_OUT0 + (i*sizeof(u32)));
		loc_buf[i] = __le32_to_cpu(loc_buf[i]);
	}
#if 0
         for(i=0;i<ICA_SHA_DATALENGTH;i++){
               printk("%0x ", *(((unsigned char *)loc_buf) + i));
          }

        printk("\n");
#endif

	if(copy_to_user(output, loc_buf, ICA_SHA_DATALENGTH))
		return -EFAULT;



	return 0;	
} 

static int sha1_execute(leedslite_dev_t *dev, unsigned int cmd, unsigned long arg)
{
	ica_sha1_t *pkt;
	unsigned int opcode;

	pkt = (ica_sha1_t *)arg;

	opcode = DCR_Opcode_SHA;

	/*  Call common (to DES/TDES) worker */

	return sha1_execute_common(dev,
			    			   opcode,
							   pkt->inputdata,
							   (char *)pkt->outputdata,
							   pkt->inputdatalength,
							   (char *)pkt->initialh);					  
						  							  
}



camelot_op_t sha_op = {
	sha1_verify, 
	sha1_execute
}; 





static int camelot_schedule(leedslite_dev_t *dev, unsigned long cmd, unsigned long arg, camelot_op_t *op)
{
	int err=0;
		

	/* 
	 * Verify IOCTL packet to see if we are even interested in processing it 
	 *
	 */

	err = op->verify(dev, cmd, arg);
	if(err)
		goto exit_camelot_schedule;
	
	
	/*
	 * Stage 1: if there is already a process actively
	 * waiting for the des engine, wait here for our turn.
	 */

	
	err = down_interruptible(&dev->des_wait);
	if(err){
		if(err == -EINTR)
			err = -ERESTARTSYS;
		goto exit_camelot_schedule;
	}

		
		
	/*
	 * Stage 2:  We now own the DES engine 
	 */ 

	err = op->execute(dev, cmd, arg);
       
	/* Done;  Wake up next process waiting for the camelot */
	
	up(&dev->des_wait);

 exit_camelot_schedule:	
	return err;	
}


static int rsa_execute_common(leedslite_dev_t *dev, unsigned int opcode, rsa_free_t *free, unsigned int length, char *output)
{
	unsigned int command;
	unsigned int timeleft;
	DECLARE_WAITQUEUE(wait, current);
	

	do{

		/*
		 * If we got a signal, don't continue on.   
		 */

		if(signal_pending(current)){
			//printk(KERN_INFO "got signal rsa\n");
			return -ERESTARTSYS;
		}


		/* Fix up RSA command */
		
		command = opcode;
		command |= _mlen(length) << RCR_MLen_SHIFT;
		command |= free->index << RCR_OpID_SHIFT;
		

		/* Getting ready for the interrupt handler to wake us.
		 * Pretend we are sleeping, If interrupt handler fires before
		 * we actually schedule, the handler will mark us as Running, 
		 * otherwise, we'll actually sleep at the schedule()
		 * I'm going to mark this as TASK_UNINTERRUPTIBLE, as we are
		 * doing DMA; it would not be good for our buffer to go away.
		 */
	
   
		free->vfstatus = 0;
		free->status = 0;


		set_current_state(TASK_UNINTERRUPTIBLE);
		add_wait_queue(&free->wait, &wait);
		
		
		/* Start the command by writing to the RCR, RSA Command Register */
		outl(command, dev->ioaddr+REG_RCR);	

		/* Wait for completion */		
		timeleft = schedule_timeout(HZ * error_timeout);


		remove_wait_queue(&free->wait, &wait);

		/* 
		 * If we've timed out it is likely we have some hardware 
		 * problem
		 */

		if(!timeleft){
			printk(KERN_ERR "leedslite: rsa DMA timeout!\n");		
			return -EIO;
		}

	
	    if(free->status == 0){
						
			/* On error, the offending Camelot will be removed as a 
			 * work candidate by having its RSCR REE bit turned off
			 * A simple testcase is to use modulus = 0.
			 */
			if(free->vfstatus & RSA_VF_ERR_MASK){
				int id;
				
				printk(KERN_ERR "leedslite:  vfifo error = %x\n", free->vfstatus);
				/* Recover the failing Camelot */
				
				id = (free->vfstatus & RSA_VF_CamID) >> 6;
				
				/* Camlot got turned off, turn it back on */
			
				camelot_reset(dev, id);
				camelot_rscr_enable_ree(dev, id);
				if(!(free->vfstatus & RSA_VF_AEF))
					return -EIO;
			}

		}


		if(free->status & (PIR_MI)){
			return -EIO;
		}	

	} while(free->status != 0);


	/* No errors, so lets copy the result back out */

	
	if(copy_to_user(output, (char *)&dev->rop[free->index], length))
		return -EFAULT;

		
	return 0;	
} 

static int rsa_modexpo_verify(leedslite_dev_t *dev, unsigned int cmd, unsigned long arg){ 
	ica_rsa_modexpo_t pkt;

	if(copy_from_user(&pkt, (ica_rsa_modexpo_t *)arg, sizeof(ica_rsa_modexpo_t)))
		return -EFAULT;   
   

	if((pkt.inputdatalength < ICA_RSA_DATALENGTH_MIN) || (pkt.inputdatalength > ICA_RSA_DATALENGTH_MAX))
		return -EINVAL;

	if((pkt.inputdatalength % ICA_RSA_DATALENGTH_MIN) != 0)
		return -EINVAL;
	
	
	return 0;
}

static int rsa_modexpo_execute(leedslite_dev_t *dev, unsigned int cmd, unsigned long arg, rsa_free_t *free)
{
   	ica_rsa_modexpo_t *pkt;
	unsigned int opcode;
	unsigned char *buffer;

	pkt = (ica_rsa_modexpo_t *)arg;
	
	opcode = RCR_Opcode_Mod_Expo;
	
	buffer = (char *)&dev->rip[free->index];
	

	/* Setup Data  */
	if(copy_from_user(buffer, pkt->inputdata, pkt->inputdatalength))
		return -EFAULT;

	buffer += pkt->inputdatalength;


	/* Setup Input Buffer */
	if(copy_from_user(buffer, pkt->b_key, pkt->inputdatalength))
		return -EFAULT;

	buffer += pkt->inputdatalength;

	/* Setup Input Buffer */
	if(copy_from_user(buffer, pkt->n_modulus, pkt->inputdatalength))
		return -EFAULT;

	/*  Call common to RSA engine worker */

	return rsa_execute_common(dev,
							  opcode,
							  free,
							  pkt->inputdatalength,
							  pkt->outputdata);
							  

	return 0;						  							  
}

rsa_op_t rsa_modexpo_op = {
	rsa_modexpo_verify, 
	rsa_modexpo_execute
}; 


static int rsa_modmult_verify(leedslite_dev_t *dev, unsigned int cmd, unsigned long arg){ 
	ica_rsa_modmult_t pkt;

	if(copy_from_user(&pkt, (ica_rsa_modmult_t *)arg, sizeof(ica_rsa_modmult_t)))
		return -EFAULT;   
	


	if((pkt.inputdatalength < ICA_RSA_DATALENGTH_MIN) || (pkt.inputdatalength > ICA_RSA_DATALENGTH_MAX))
		return -EINVAL;


	if((pkt.inputdatalength % ICA_RSA_DATALENGTH_MIN) != 0)
		return -EINVAL;
	
	
	return 0;
}

static int rsa_modmult_execute(leedslite_dev_t *dev, unsigned int cmd, unsigned long arg, rsa_free_t *free)
{
   	ica_rsa_modexpo_t *pkt;
	unsigned char *buffer;

	pkt = (ica_rsa_modexpo_t *)arg;

	buffer = (char *)&dev->rip[free->index];

	/* Setup Data  */
	if(copy_from_user(buffer, pkt->inputdata, pkt->inputdatalength))
		return -EFAULT;

	buffer += pkt->inputdatalength;

	/* Setup Input Buffer */
	if(copy_from_user(buffer, pkt->b_key, pkt->inputdatalength))
		return -EFAULT;

	buffer += pkt->inputdatalength;

	/* Setup Input Buffer */
	if(copy_from_user(buffer, pkt->n_modulus, pkt->inputdatalength))
		return -EFAULT;

	/*  Call common to RSA engine worker */

	return rsa_execute_common(dev,
							  RCR_Opcode_Mod_Mult,
							  free,
							  pkt->inputdatalength,
							  pkt->outputdata);
							  

	return 0;						  							  
}

rsa_op_t rsa_modmult_op = {
	rsa_modmult_verify, 
	rsa_modmult_execute
}; 

static int rsa_modexpo_crt_verify(leedslite_dev_t *dev, unsigned int cmd, unsigned long arg){ 
	ica_rsa_modmult_t pkt;

	if(copy_from_user(&pkt, (ica_rsa_modmult_t *)arg, sizeof(ica_rsa_modmult_t)))
		return -EFAULT;   
	

	if((pkt.inputdatalength < ICA_RSA_DATALENGTH_MIN) || (pkt.inputdatalength > ICA_RSA_DATALENGTH_MAX))
		return -EINVAL;


	if((pkt.inputdatalength % ICA_RSA_DATALENGTH_MIN) != 0)
		return -EINVAL;
	
	
	return 0;
}

static int rsa_modexpo_crt_execute(leedslite_dev_t *dev, unsigned int cmd, unsigned long arg, rsa_free_t *free)
{
   	ica_rsa_modexpo_crt_t *pkt;
	unsigned char *buffer;
	unsigned int count;

	pkt = (ica_rsa_modexpo_crt_t *)arg;

	buffer = (char *)&dev->rip[free->index];

	/* Setup Data  */
	if(copy_from_user(buffer, pkt->inputdata, pkt->inputdatalength))
		return -EFAULT;

	buffer += pkt->inputdatalength;

	count = pkt->inputdatalength / 2;

	/* Setup Bp */
	if(copy_from_user(buffer, pkt->bp_key, count+8))
		return -EFAULT;

	buffer += (count+8);

	/* Setup Bq */
	if(copy_from_user(buffer, pkt->bq_key, count))
		return -EFAULT;

	buffer += count;

	/* Setup Np */
	if(copy_from_user(buffer, pkt->np_prime, count+8))
		return -EFAULT;

	buffer += count+8;
	
	/* Setup Nq */
	if(copy_from_user(buffer, pkt->nq_prime, count))
		return -EFAULT;

	buffer += count;

	/* Setup U (Multiplicative Inverse) */
	if(copy_from_user(buffer, pkt->u_mult_inv, count+8))
		return -EFAULT;

	

	/*  Call common to RSA engine worker */

	return rsa_execute_common(dev,
							  RCR_Opcode_Mod_Expo_CRT,
							  free,
							  pkt->inputdatalength,
							  pkt->outputdata);
							  

	return 0;						  							  
}

rsa_op_t rsa_modexpo_crt_op = {
	rsa_modexpo_crt_verify, 
	rsa_modexpo_crt_execute
}; 


static int rsa_schedule(leedslite_dev_t *dev, unsigned long cmd, unsigned long arg, rsa_op_t *op)
{
	int err=0;
	struct list_head  *entry;
	rsa_free_t        *free;
		

	/* 
	 * Verify IOCTL packet to see if we are even interested in processing it 
	 *
	 */

	err = op->verify(dev, cmd, arg);
	if(err)
		goto exit_rsa_schedule;
	
	
	
	/*
	 * Stage 1:  Wait here for a RIP buffer.   We only let 64 tasks in as
	 * that is the number of buffers we have.
	 */

	err = down_interruptible(&dev->rsa_wait);
	if (err) {
		if(err == -EINTR)
			err = -ERESTARTSYS;
		goto exit_rsa_schedule;
	}
	
		
	/*
	 * Stage 2:  We should now be able to grab a free buffer; 
	 */ 

	if(list_empty(&dev->rsa_freelist_head)){
		// This should never happen but seems like a good check to have
		printk(KERN_ERR "leedslite:  rsa_free list is empty!\n");
	}
	   
	entry = dev->rsa_freelist_head.next;
	
	/* Remove entry and clean it up for reuse */

	list_del(entry);
	INIT_LIST_HEAD(entry);

	/* Recover the outter struct */

	free = list_entry(entry, rsa_free_t, freelist);  

	/* Go do the operation according to its type */

	err = op->execute(dev, cmd, arg, free);
       
	list_add_tail(&free->freelist, &dev->rsa_freelist_head);

	/* Wake up next process waiting for the camelot */
	
	up(&dev->rsa_wait);
	

 exit_rsa_schedule:	
	return err;	
}



static int ica_ioctl_rsa_modexpo(struct inode *inode, struct file *filp, unsigned int cmd, unsigned long arg, leedslite_dev_t *dev)
{
	return rsa_schedule(dev, cmd, arg, &rsa_modexpo_op);
}

static int ica_ioctl_rsa_modmult(struct inode *inode, struct file *filp, unsigned int cmd, unsigned long arg, leedslite_dev_t *dev)
{
	return rsa_schedule(dev, cmd, arg, &rsa_modmult_op);
}

static int ica_ioctl_rsa_modexpo_crt(struct inode *inode, struct file *filp, unsigned int cmd, unsigned long arg, leedslite_dev_t *dev)
{
	return rsa_schedule(dev, cmd, arg, &rsa_modexpo_crt_op);
}

static int ica_ioctl_des(struct inode *inode, struct file *filp, unsigned int cmd, unsigned long arg, leedslite_dev_t *dev)
{   
	return camelot_schedule(dev, cmd, arg, &des_op);
}

static int ica_ioctl_tdes(struct inode *inode, struct file *filp, unsigned int cmd, unsigned long arg, leedslite_dev_t *dev)
{
	return camelot_schedule(dev, cmd, arg, &des_op);
}

static int ica_ioctl_desmac(struct inode *inode, struct file *filp, unsigned int cmd, unsigned long arg, leedslite_dev_t *dev)
{
	return camelot_schedule(dev, cmd, arg, &desmac_op);
}

static int ica_ioctl_tdesmac(struct inode *inode, struct file *filp, unsigned int cmd, unsigned long arg, leedslite_dev_t *dev)
{
	return camelot_schedule(dev, cmd, arg, &desmac_op);
}

static int ica_ioctl_sha1(struct inode *inode, struct file *filp, unsigned int cmd, unsigned long arg, leedslite_dev_t *dev)
{
	return camelot_schedule(dev, cmd, arg, &sha_op);
}

static int ica_ioctl_rng(struct inode *inode, struct file *filp, unsigned int cmd, unsigned long arg, leedslite_dev_t *dev)
{
	int err;
	char *buf;
	int nbytes;
	ica_rng_t *pkt;

	pkt = (ica_rng_t *)arg;

	/* Verify user access to the ioctl pkt */

	err = get_user(nbytes,  (int *)&pkt->nbytes);
	if(err)
		goto err_ioctl_rng;

	err = get_user(buf, (char **)&pkt->buf);
	if(err)
		goto err_ioctl_rng;

	/* Verify user access to write to the given buffer */

	if(!access_ok(VERIFY_WRITE, buf, nbytes) || (buf == NULL)){
		err = -EFAULT;
		goto err_ioctl_rng;
	}

	
	/*
	 * Simply wrapper the rng's read operation
	 */

	err = rng_read(filp, buf, nbytes, 0);
	if(err >= 0){
		err = put_user(err, (int *)&pkt->nbytes);
	}
		
	
	
 err_ioctl_rng:
	return err;
}



static int ica_ioctl_worker(struct inode *inode, struct file *filp, unsigned int cmd, unsigned long arg, void *private)
{
	int rc;
	leedslite_dev_t *dev = (leedslite_dev_t *)private;



	
	switch(cmd){
	case ICASETBIND:
		rc = ica_ioctl_setbind(inode, filp, cmd, arg, dev);
		break;
	case ICAGETBIND:
		rc = ica_ioctl_getbind(inode, filp, cmd, arg, dev);
		break;
	case ICAGETCOUNT:
		rc = ica_ioctl_getcount(inode, filp, cmd, arg, dev);
		break;
	case ICAGETID:
		rc = ica_ioctl_getid(inode, filp, cmd, arg, dev);
		break;
	case ICARSAMODEXPO:
		rc = ica_ioctl_rsa_modexpo(inode, filp, cmd, arg, dev);
		break;
	case ICARSACRT:
		rc = ica_ioctl_rsa_modexpo_crt(inode, filp, cmd, arg, dev);
		break;
	case ICARSAMODMULT:
		rc = ica_ioctl_rsa_modmult(inode, filp, cmd, arg, dev);
		break;
	case ICADES:
		rc = ica_ioctl_des(inode, filp, cmd, arg, dev);
		break;
	case ICATDES:
		rc = ica_ioctl_tdes(inode, filp, cmd, arg, dev);
		break;
	case ICADESMAC:
		rc = ica_ioctl_desmac(inode, filp, cmd, arg, dev);
		break;
	case ICATDESSHA:
		rc = -EOPNOTSUPP;
		break;
	case ICATDESMAC:
		rc = ica_ioctl_tdesmac(inode, filp, cmd, arg, dev);
		break;
	case ICASHA1:
		rc = ica_ioctl_sha1(inode, filp, cmd, arg, dev);
		break;
	case ICARNG:
		rc = ica_ioctl_rng(inode, filp, cmd, arg, dev);
		break;
	default:
		rc = -EOPNOTSUPP;
		break;
	}
   
	return rc;
}


static int ica_ioctl(struct inode *inode, struct file *filp, unsigned int cmd, unsigned long arg)
{
	void *private  = (leedslite_dev_t *)filp->private_data;
	
	return ica_ioctl_worker(inode, filp, cmd, arg, private);
}



static struct file_operations ica_fops = {
	owner: THIS_MODULE,
	open: ica_open,
	release: ica_release,
	read: rng_read,
	ioctl: ica_ioctl,
};


static struct ica_operations ica_ops = {
	read: rng_read_worker,
	ioctl: ica_ioctl_worker,
};




static inline void leedslite_soft_reset(leedslite_dev_t *dev)
{
	outw(SCR_SORST, dev->ioaddr+REG_SCR);
	udelay(1);             // Reset needs min 500 ns delay
	outw(0x0000, dev->ioaddr+REG_SCR);
}

static inline void leedslite_bmcsr_configure(leedslite_dev_t *dev)
{
	int bmcsr;

	bmcsr = 
		BMCSR_flush_DES_to_PCI_FIFO |
		BMCSR_flush_PCI_to_DES_FIFO |
		BMCSR_flush_RSA_to_PCI_FIFO |
		BMCSR_flush_PCI_to_DES_FIFO;

	if(pmwi)
		bmcsr |= BMCSR_pMWI_EN;

	outl(bmcsr, dev->ioaddr+REG_BMCSR);
	udelay(1);             // Reset needs min 500 ns delay

	bmcsr = 
		BMCSR_D_BM_EN |
		BMCSR_R_BM_EN;

	outl(bmcsr, dev->ioaddr+REG_BMCSR);
}


static void leedslite_abort_des(leedslite_dev_t *dev, int interrupt)
{
	/* Abort current DES */
	dev->des_status=interrupt;
	wake_up(&dev->des_current_wait);
}

static void leedslite_abort_rsa(leedslite_dev_t *dev, int interrupt)
{
	int i;

	/* Abort current RSA */
	for(i=0; i<rsabufs; i++){
		if(waitqueue_active(&dev->rsa_freelist[i].wait)){
			dev->rsa_freelist[i].status = interrupt;	
			wake_up(&dev->rsa_freelist[i].wait);
		}	
	}
}

static int leedslite_init(leedslite_dev_t *dev)
{
	int i;
	int oscr;

	leedslite_soft_reset(dev);

	/* 
	 * Reset software counter for the the RSA LWP 
     * Do it here so we don't forget anytime we reset 
	 * the adapter.   This is duplicate, but will not
	 * hurt anything.   
	 */

	dev->lwp = 0;

	/* Configure the adapter with our DMA buffers */

	outl(dev->rip_handle, dev->ioaddr + REG_RIBAR); 		
	outl(dev->rop_handle, dev->ioaddr + REG_ROBAR);
	outl(dev->vfifo_handle, dev->ioaddr+REG_VF_BAR);

	

	/* Configure BMCSR - Busmaster Control Register 
	 * Enable:
	 * D_BM_EN - DES Busmaster Enable
	 * R_BM_EN - RSA Busmaster Enable
	 *
	 */

	leedslite_bmcsr_configure(dev);


	
	

	/*
	 * Cycle through each of the Camelot, turning on the RSA
	 * engines as we go.  We'll go through backwards as we want
	 * to have the 1st engine, selected via CSV from here on out.
	 */

	for(i = 5; i>0; i--){
	
		

		/* Configure OSCR - Overall Setup and Control 
		 *
		 * OSCR_ADSM - DES, etc interrupt.  DISABLE this globally, as we'll
		 * turn on INT_back in each specific command if needed.
		 *
		 * OSCR_CSV_* - Select Camelot * for the CSV
		 *
		 * OSCR_SAM - Speed Adjusment Mechanism (power consumption/perf.)
		 */
	
		oscr = i << OSCR_CSV_SHIFT;    // select camelot
		oscr |= sam << OSCR_SAM_SHIFT; // speed adjustment 

		outw(oscr, dev->ioaddr+REG_OSCR);

		camelot_gcra_enable_rsa(dev);
		
	}

	/* Configure RSCR - RSA Setup and Control 
	 *  RSCR_REE - RSA Engine Enable: enable all engines
	 *  RSCR_RDLE - RSA Dispatch logic enable
	 */
	   
	outw(RSCR_REE|RSCR_RDLE, dev->ioaddr+REG_RSCR);


	/* Configure MIER - Merlin Interrupt Register
	 *   Configure them all, as Merlin interrupts are catestrophic
	 */
	outw(MIER_WRC|MIER_RCRO|MIER_WRL|MIER_WDC|MIER_DCRO, dev->ioaddr+REG_MIER);

	/* Configure PIER - PCI (Piuma) Interrupt Enable Register 
	 *  Enable:
	 *  MI - Merlin Interrupts
	 *  BCZ3 - DES to PCI (DES result)
     *
	 * Other interrupts are either handled by the adapter or enabled as
	 * needed
	 */

	outw(PIER_MI|PIER_BCZ3|PIER_DSM_done|PIER_RNG_done|PIER_RSA_done, dev->ioaddr+REG_PIER);

	/* The first camelot has some extra setup for rng, des, & sha */

	camelot_enable_cam1(dev);

	return 0;
}

static void leedslite_channel_reset(leedslite_dev_t *dev, int channel)
{  
	int fp, ar, cr, bmcsr, zero, i;

	if((channel == 1) || (channel == 3)){
		fp = (channel == 1) ? REG_RFP : REG_DFP;
		ar = (channel == 1) ? REG_BMAR1: REG_BMAR3;
		cr = (channel == 1) ? REG_BMCR1: REG_BMCR3;
		bmcsr = (channel == 1) ? BMCSR_R_BM_EN: BMCSR_D_BM_EN;
		zero  = (channel == 1) ? (1<<26) : (1<<24);

		/* Busmaster Error Recovery procedure */

		leedslite_soft_reset(dev);


		/* 
		 * Set to Alternate Operating Mode, so we can talk BM for the 
         * RSA channel
		 */

		outw(SCR_AOM,dev->ioaddr+REG_SCR);

		/* Set up to wrapback */
			
		outw(RSCR_RFWB, dev->ioaddr + REG_RSCR);

		/* Write 4 DWORD 0s directly the the FIFO */
		for(i=0; i<4; i++)
			outl(0, dev->ioaddr+fp);
		
		/* Enable the appropriate BM channel only */

		outl(bmcsr, dev->ioaddr+REG_BMCSR);

		/* 
		 * Set up the Busmaster Registers, once the count
		 * is set up the bm will commence 
		 */

		outl(dev->des_handle, dev->ioaddr+ar);	
		outl(16, dev->ioaddr+cr);

		/* Hang out waiting for the busmaster count is 0 */

		for(i=0; i<10; i++){
			bmcsr = inl(dev->ioaddr+REG_BMCSR);
			if(bmcsr & zero)
				break;
			printk(KERN_ERR "leedslite:  bm error recovery waiting!\n");
			udelay(10);            
		}
	
	}

}

static void leedslite_interrupt_pao(leedslite_dev_t *dev, int interrupt)
{
	int pier;

	/* 
	 * I have not found a way to really test this.   For now, 
	 * I'll assume the abort is Not catastrophic (though it should
	 * not happen) and just clear the interrupt.   
	 */

	/*
	 * JAG - Verify this with the newest spec and database.  
	 * Worst case, I probably need to reset the card and restart 
	 * current requests.
	 */

	printk(KERN_ERR "leedslite: pci abort occurence\n");


	/* First, clear the interrupt condition */

	pier = inw(dev->ioaddr + REG_PIER);
	pier &= ~PIER_PAO;
	outw(pier, dev->ioaddr + REG_PIER);
}

static void leedslite_interrupt_mi(leedslite_dev_t *dev, int interrupt)
{
	int pier;

	/* 
	 * This routine can be tested by setting an invalid RSA modulus
	 * length (e.g. >8).   
	 */

	printk(KERN_ERR "leedslite: merlin interrupt\n");
	
	/* First, clear the interrupt condition */

	pier = inw(dev->ioaddr + REG_PIER);
	pier &= ~PIER_MI;
	outw(pier, dev->ioaddr + REG_PIER);

	/* 
	 * Reinitialize the card, and restart the current requests for this
	 * card.
	 */

	leedslite_init(dev);
	leedslite_abort_des(dev, interrupt);
	leedslite_abort_rsa(dev, interrupt);

}

static void leedslite_interrupt_beo(leedslite_dev_t *dev, int interrupt)
{
	int bmcsr;
	int channel;
	
	/* 
	 * I've tested this routine on P-Level hardware which had problems
	 * and generated busmaster errors.   I can't guarantee that the
	 * routine will fix all beo errors, but the card does seem to 
	 * come back to life and is usable from then on out after this
	 * routine.   Busmaster errors in general Should_Not_Happen, but
	 * this is the procedure as described by the silicon vendor.  
	 */

	printk(KERN_ERR "leedslite:  busmaster error occurence\n");

    /* 
	 * If the busmaster error is on channel 1 or 3 (the write channels)
	 * a specific error recovery procedure must be followed
	 */
	
	bmcsr = inl(dev->ioaddr + REG_BMCSR);
	channel = (bmcsr & BMCSR_last_BM) >> BMCSR_last_BM_SHIFT;
	
	
	if((channel == 1) || (channel == 3))
		leedslite_channel_reset(dev, channel);	

	/* Re-initialize the adapter */

	leedslite_init(dev);
	
	/* 
	 * Restart the current requests... don't worry about RNG as it
	 * is very resilient and a new Int will fire by re-initializing the
	 * adapter.
	 */

	leedslite_abort_des(dev, interrupt);
	leedslite_abort_rsa(dev, interrupt);
	
   
}




static void leedslite_interrupt_des(leedslite_dev_t *dev, int interrupt)
{
	
	dev->des_status=0;
	wake_up(&dev->des_current_wait);
}

static void leedslite_interrupt_rng(leedslite_dev_t *dev, int interrupt)
{
	atomic_inc(&dev->entropy_available);
	wake_up_interruptible(&dev->rng_current_wait);	
}


unsigned long long_reverse( unsigned long x)
{
if (bigendian) {
   return (
	((0x000000FF & x)<<24) |
	((0x0000FF00 & x)<<8) |
	((0x00FF0000 & x)>>8) |
	((0xFF000000 & x)>>24) );
} else {
	return(x);
}
}


static void leedslite_interrupt_rsa(leedslite_dev_t *dev, int interrupt)
{
	int lwp, index, opid;
	leedslite_vfifo_entry_t status;
	rsa_free_t             *entry;

	/* 
	 * Determine the number of buffers that have been completed.  
	 * Based on the previous lwp (last write pointer); 
	 */
		
	lwp = (interrupt & PIR_LWP) >> 2;
	index = dev->lwp;

		
	/* 
	 * For every completed entry, find its buffer and wake up the task
	 * waiting for it 
	 */
	
	do {
		
		if(index >= LEEDSLITE_VFIFO_ENTRIES)
			index = 0;
		
#if 0
		status = dev->vfifo[index];
#else
		status = long_reverse(dev->vfifo[index]);
#endif
		if(status & RSA_VF_VEM){
			dev->vfifo[index]=0;
			
			opid  = (status & RSA_VF_RSAopID) >> 9;
			
			entry = &dev->rsa_freelist[opid];
			entry->vfstatus = status;
			
			//printk("wakeup index=%x status=%x\n", opid, status);
			
			wake_up(&entry->wait);		
		} else {
			printk("leedslite:  VEM not set!! vfindex=%x\n", index);
		}
			
	} while (index++ != lwp);
	
	/* 
	 * Keep track where we left off 
	 */

	dev->lwp = index;	
}

static irqreturn_t 
leedslite_interrupt(int irq, void *instance, struct pt_regs *regs)
{
	leedslite_dev_t *dev = instance;
	int interrupt;
	

	interrupt = inw(dev->ioaddr+REG_PIR);
	

	/* PCI Abort Occurence */

	if(interrupt & PIR_PAO){
		leedslite_interrupt_pao(dev, interrupt);
	}

	/* Merlin Interrupt (Error) */

	if(interrupt & PIR_MI){
		leedslite_interrupt_mi(dev, interrupt);
	}

	/* Busmaster Error Occured */

	if(interrupt & PIR_BEO){
		leedslite_interrupt_beo(dev, interrupt);
		goto out;
	}


	/* 
	 * Des Engine completed operation 
	 *   DSM_done for SHA/DESMAC or
	 *   BCZ3 for DES with busmastered DMA
	 */
	
	 if(interrupt & (PIR_DSM_done|PIR_BCZ3))
		 leedslite_interrupt_des(dev, interrupt);

	 /* Random Number Generator data ready */

	 if(interrupt & PIR_RNG_done)
		 leedslite_interrupt_rng(dev, interrupt);
	
	 /* RSA operation(s) completed */

	if(interrupt & PIR_RSA_done) 
		leedslite_interrupt_rsa(dev, interrupt);
	
out:
	return IRQ_HANDLED;
}




static int __exit leedslite_shutdown (leedslite_dev_t *dev)
{
	return 0;
}

static int __devinit leedslite_init_one (struct pci_dev *pdev,
                const struct pci_device_id *ent, leedslite_dev_t **data)
{
	leedslite_dev_t *dev;
	leedslite_dev_t **slot;
	int i, err, slotnum;
	
	/*
     * This driver supports 'maxdevices'.   We'll map devices to 
     * nodes by the order that we find them.   If a hole, or slot 
     * were to open up (by the removal of a device).  We'll map the 
     * next discovered device back into that slot.   It is possible
     * that someday we may want to even keep a unique adapter ID.  
     * However, below works for the foreseeable future.  
     * 
	 */

	
	dev = kmalloc(sizeof(leedslite_dev_t), GFP_KERNEL);
	if(!dev){
		printk(KERN_ERR "leedslite_init_one: out of memory\n");
		err = -ENOMEM;
		goto err_init_one;
	}
	memset(dev, 0, sizeof(leedslite_dev_t));
	
	slot = NULL;
	slotnum = 0;

	for(i=0; i<maxdevices; i++){
		if(devices[i]== NULL){
			slot = &devices[i];
			slotnum = i;
			break;
		}
	}

	if(!slot){
		printk(KERN_ERR "leedslite_init_one: no more device structures\n");
		err  = -ENOMEM;
		goto err_init_one_slot;
	}
	
	*slot = dev;
	
	/* 
	 * Initialize wait queues needed by the various operations 
	 */

	
	init_waitqueue_head(&dev->rng_current_wait);
	
	sema_init(&dev->des_wait, 1); 
	init_waitqueue_head(&dev->des_current_wait);
	
	
	sema_init(&dev->rsa_wait, rsabufs); 


	/* SAB endian adjustment */
	bigendian = isbigendian();


	/* 
	 * Start allocating the various DMA buffers used by RSA.   
	 * rip - RSA Input
	 * rop - RSA Output
	 * vfifo - RSA Virtual Fifo (status)
	 * 
	 * Note: for DES operations we'll pin down the buffers as needed.
	 */

	
	dev->rip = pci_alloc_consistent(pdev, rsabufs * RIP_ENTRY_SIZE, &dev->rip_handle);
	if(!dev->rip){
		assertk(dev->rip);
		err = -ENOMEM;
		goto err_init_one_rip;
	}
	
	
	dev->rop = pci_alloc_consistent(pdev, rsabufs * ROP_ENTRY_SIZE, &dev->rop_handle);
	if(!dev->rop){
		assertk(dev->rop);		
		err = -ENOMEM;
		goto err_init_one_rop;
	}


	dev->vfifo = pci_alloc_consistent(pdev, LEEDSLITE_VFIFO_ENTRIES * VFIFO_ENTRY_SIZE, &dev->vfifo_handle);
	if(!dev->vfifo){
		printk(KERN_ERR "leedslite_init_one: unable to allocate vfifo buffer\n");
		err = -ENOMEM;
		goto err_init_one_vfifo;
	}

	dev->desbuffer = pci_alloc_consistent(pdev, desbuffersize, &dev->des_handle);
	if(!dev->desbuffer){
		printk(KERN_ERR "leedslite_init_one: unable to allocate vfifo buffer\n");
		err = -ENOMEM;
		goto err_init_one_des;
	}

	/* 
	 * Intialize the vfifo previous word pointer (pwp).   Allows
	 * us to keep track of how many vfifo statuses are ready by
	 * comparing this to the lwp (last word pointer) reported by the
	 * PIR 
	 */

	dev->lwp = 0;

	/*
     * Multiple pending RSA operations can be sent to the adapter.   We
     * need to be able to tell which RSA data structures are currently in 
	 * use.   rsa_freelist is used for this purpose. 
	 */

	dev->rsa_freelist = kmalloc(sizeof(rsa_free_t) * rsabufs, GFP_KERNEL);
	if(!dev->rsa_freelist){
		err = -ENOMEM;
		goto err_init_one_freelist;
	}

	INIT_LIST_HEAD(&dev->rsa_freelist_head);
	for(i=0; i<rsabufs; i++){
		INIT_LIST_HEAD(&dev->rsa_freelist[i].freelist);
		dev->rsa_freelist[i].index = i;
		init_waitqueue_head(&dev->rsa_freelist[i].wait);
		list_add_tail(&dev->rsa_freelist[i].freelist, &dev->rsa_freelist_head);
	}

	/* 
	 * Leedslite has 3 PCI I/O resources
	 *  resource 0 - io port region (various Merlin/Piuma registers)
	 *  resource 1 - mem-mapped region (SRAM key storage)
	 *  resource 2 - mem-mapped region (currenly selected Camelot registers)
	 */

	err = -ENODEV;
	if (!request_region(pci_resource_start(pdev, 0),
			pci_resource_len(pdev, 0), "leedslite")) {
		printk (KERN_ERR "leedslite: cannot reserve I/O ports\n");
		goto err_init_one_pci_reserve_0;
	}

	if (!request_mem_region(pci_resource_start(pdev, 1),
			pci_resource_len(pdev, 1), "leedslite")) {
		printk (KERN_ERR "leedslite: cannot reserve MMIO region\n");
		goto err_init_one_pci_reserve_1;
	}

	if (!request_mem_region(pci_resource_start(pdev, 2),
			pci_resource_len(pdev, 2), "leedslite")) {
		printk (KERN_ERR "leedslite: cannot reserve MMIO region\n");
		goto err_init_one_pci_reserve_2;
	}
	
	if (pci_enable_device(pdev))
		goto err_init_one_pci_enable;

	dev->irq = pdev->irq;
	dev->ioaddr = pci_resource_start(pdev, 0);
	dev->window1 = ioremap(pci_resource_start(pdev, 1), 
						   pci_resource_len(pdev, 1));
	if (!dev->window1){
		printk(KERN_ERR "leedslite: cannot remap window 1\n");
		goto err_init_one_window1_remap;
	}

	dev->window2 = ioremap(pci_resource_start(pdev, 2), 
						   pci_resource_len(pdev, 2));
	if (!dev->window2){
		printk(KERN_ERR "leedslite: cannot remap window 2\n");
		goto err_init_one_window2_remap;
	}
	
	pci_set_master(pdev);
	

	/* 
	 * Let's set up the interrupt handler, just in case banging on the ports
	 * inadvertantly triggers an interrupt.   
	 */
	err = request_irq(dev->irq, leedslite_interrupt, SA_SHIRQ, "leedslite", dev);
	if (err){
		printk(KERN_ERR "leedslite: unable to request irq\n");
		goto err_init_one_irq;
	}

	/* 
	 * Finally, go set up the adapter.
	 */

	err = leedslite_init(dev);
	

	if (err){
		goto err_init_one_adapter_init;
	}

	/* 
	 * Register our entries under /dev/leedslite 
	 */

	devfs_mk_cdev(MKDEV(driver_major,slotnum), S_IRUGO|S_IWUGO|S_IFCHR,
		"leedslite/ica%d", slotnum);
	dev->minor = slotnum;

	/*  
     * Autoregister with /dev/ica if configured so.   The devica
	 * driver provides a virtual ica interface to spread the 
	 * crypto operations across multiple cards.   This is the
     * default behavior unless the 'devica=0' option is set.
     */

	if (devica) {

		dev->icareg.icaops = &ica_ops;
		dev->icareg.private_data = dev;
		
		if (ica_register_worker(0, &dev->icareg)){
			dev->bind = -1;
		} else {
			dev->bind = 0;
		}
	}
	
	dev->pdev = pdev;
	*data = dev;

	printk(KERN_INFO "%s: Init Success\n", version);
	
	return 0;
 

 err_init_one_adapter_init:
	free_irq(dev->irq, dev);
 err_init_one_irq:
	iounmap(dev->window2);
 err_init_one_window2_remap:
	iounmap(dev->window1);
 err_init_one_window1_remap:
 err_init_one_pci_enable:
	release_mem_region(pci_resource_start(pdev,2), pci_resource_len(pdev,2));
 err_init_one_pci_reserve_2:
	release_mem_region(pci_resource_start(pdev,1), pci_resource_len(pdev,1));
 err_init_one_pci_reserve_1:
	release_region(pci_resource_start(pdev, 0), pci_resource_len(pdev, 0));
 err_init_one_pci_reserve_0:
 err_init_one_freelist:
	pci_free_consistent(pdev, desbuffersize, dev->desbuffer, dev->des_handle);
 err_init_one_des:
	pci_free_consistent(pdev, LEEDSLITE_VFIFO_ENTRIES * VFIFO_ENTRY_SIZE, dev->vfifo, dev->vfifo_handle);
 err_init_one_vfifo:
	pci_free_consistent(pdev, rsabufs * ROP_ENTRY_SIZE, dev->rop, dev->rop_handle);
 err_init_one_rop:
	pci_free_consistent(pdev, rsabufs * RIP_ENTRY_SIZE, dev->rip, dev->rip_handle); 
 err_init_one_rip:
 err_init_one_slot:
	kfree(dev);
 err_init_one:
	return err;


}

static int __devinit leedslite_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{ 
	int rc;
	leedslite_dev_t *dev;

	rc = leedslite_init_one(pdev, ent, &dev);

	/* This may look highly unusual.. For single source compatibility
	 * I've added a list to the dev structure and am stringing
	 * all the devices together so I can duplicate the pci probe/remove
	 * behavior.   Since the old pci_dev does not have a place to store
	 * drvdata I'll wrapper the drvdata with a list element.  I can
	 * retrieve it while calling pci_driver->probe .. 
	 * See devicacompat.h for magic 
	 */

	INIT_LIST_HEAD(&dev->node);
	list_add_tail(&dev->node, &devicelist); 

	pci_set_drvdata(pdev, dev);

	return rc;
}


static void __devexit leedslite_remove_one (struct pci_dev *pdev, leedslite_dev_t *data)
{  
	leedslite_dev_t *dev;
	int rc, i;
	

	dev = data;

	if (!dev)
		return;

	rc = ica_unregister_worker(0, &dev->icareg);
	

	free_irq(dev->irq, dev);
	leedslite_shutdown(dev);
	
	iounmap(dev->window1);
	iounmap(dev->window2);

	release_mem_region(pci_resource_start(pdev,2), pci_resource_len(pdev,2));
	release_mem_region(pci_resource_start(pdev,1), pci_resource_len(pdev,1));
	release_region(pci_resource_start(pdev, 0), pci_resource_len(pdev, 0));


	for(i=0; i<maxdevices; i++){
		if (devices[i] == dev){
			devices[i] = NULL;
			break;
		}
	}
	
	
	devfs_remove("leedslite/ica%u", dev->minor);

	pci_free_consistent(pdev, desbuffersize, dev->desbuffer, dev->des_handle);
	pci_free_consistent(pdev, rsabufs * RIP_ENTRY_SIZE, dev->rip, dev->rip_handle);
	pci_free_consistent(pdev, rsabufs * ROP_ENTRY_SIZE, dev->rop, dev->rop_handle);
	pci_free_consistent(pdev, LEEDSLITE_VFIFO_ENTRIES * VFIFO_ENTRY_SIZE, dev->vfifo, dev->vfifo_handle);
	kfree(dev->rsa_freelist);

}

static void __devexit leedslite_remove (struct pci_dev *pdev)
{
	leedslite_dev_t *dev;
	struct list_head *pos;


	dev = NULL;

	/* More compatibility magic.. 2.2 does not have drv_data, so
	 * to emulate the probe/remove when running on 2.2 I'll keep the
	 * device specific data all linked together... I can go find
	 * the correct drv_data by comparing the pci_dev.  On 2.4+ I
	 * would not need to do this, but I might as well keep the 
	 * code common.   
	 */
	
	list_for_each(pos, &devicelist){
		dev = list_entry(pos, leedslite_dev_t, node);
		
		if (dev->pdev == pdev) {
			list_del(pos);
			break;
		} else {
			dev = NULL;
		}
	}
    
	if (dev)
		leedslite_remove_one(pdev, dev);
}


static struct pci_device_id leedslite_pci_tbl[] __devinitdata = {
	{ PCI_VENDOR_ID_IBM, PCI_DEVICE_ID_IBM_LEEDSLITE,
	  PCI_ANY_ID, PCI_ANY_ID,},
	{ 0,}
};

MODULE_DEVICE_TABLE(pci, leedslite_pci_tbl);


static struct pci_driver leedslite_driver = { \
	name:           "leedslite", \
	id_table:       leedslite_pci_tbl,\
	probe:          leedslite_probe, \
	remove:         leedslite_remove, \
};


int __init leedslite_driver_init(void)
{
	int rc;

	rc = register_chrdev(driver_major, "leedslite", &ica_fops);

	if (rc < 0) {
		goto err_init;
	} 

	if (driver_major == 0) 
		driver_major = rc;

	devfs_mk_dir("leedslite");

	if (desbuffersize < ICA_DES_DATALENGTH_MIN)
		desbuffersize = DESBUFFER_SIZE;

	if ((rsabufs <= 0) || (rsabufs > LEEDSLITE_VFIFO_ENTRIES))
	   rsabufs = LEEDSLITE_VFIFO_ENTRIES;
	
	if ((sam < SAM_MIN) || (sam > SAM_MAX))
		sam = SAM_MIN;

	if (pmwi != 1)
		pmwi = 0;

	if (error_timeout < LEEDSLITE_ERR_TIMEOUT_MIN)
		error_timeout = LEEDSLITE_ERR_TIMEOUT_MIN;
	
	devices = kmalloc(sizeof(leedslite_dev_t *) * maxdevices, GFP_KERNEL);

	if (!devices){
		rc = -ENOMEM;
		goto err_init_mem;
	}

	memset(devices, 0, sizeof(leedslite_dev_t *) * maxdevices);
	  

	INIT_LIST_HEAD(&devicelist);
	
	rc = pci_module_init(&leedslite_driver);
	
	if (rc < 0){
		goto err_init_pci;
	}
	
	return 0;

 err_init_pci:
	kfree(devices);
 err_init_mem:
 	devfs_remove("leedslite");
	unregister_chrdev(driver_major, "leedslite");
 err_init:
	return rc;
}

static int __init leedslite_init_module(void)
{
	return leedslite_driver_init();
}

static void __exit leedslite_cleanup_module(void)
{
	pci_unregister_driver(&leedslite_driver);
	unregister_chrdev(driver_major, "leedslite");
	kfree(devices);		
}


#ifdef MODULE

MODULE_AUTHOR("Jon Grimm <jgrimm@us.ibm.com>");
MODULE_DESCRIPTION("IBM Crypto Adapter (Leedslite)");
MODULE_PARM(maxdevices, "i");
MODULE_PARM(devica, "i");
MODULE_PARM(desbuffersize, "i");
MODULE_PARM(rsabufs, "i");
MODULE_PARM(sam, "i");
MODULE_PARM(pmwi, "i");
MODULE_PARM(driver_major, "i");
MODULE_PARM(error_timeout, "i");

#endif /* MODULE */


module_init(leedslite_init_module);
module_exit(leedslite_cleanup_module);


/*
 * Local variables:
 *  compile-command: "gcc -g -DMODULE -D__KERNEL__  -Wall -Wstrict-prototypes -O6 -c leedslite.c `[ -f /usr/include/linux/modversions.h ] && echo -DMODVERSIONS`"
 *  c-indent-level: 4
 *  c-basic-offset: 4
 *  tab-width: 4
 * End:
 */
