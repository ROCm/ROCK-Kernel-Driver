/*
    saa7146_core.c - core-functions + i2c driver for the saa7146 by
    Philips Semiconductors.
    
    Copyright (C) 1998,1999 Michael Hunold <michael@mihu.de>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */


#include <linux/module.h>	/* for module-version */
#include <linux/delay.h>	/* for delay-stuff */
#include <linux/slab.h>		/* for kmalloc/kfree */
#include <linux/pci.h>		/* for pci-config-stuff, vendor ids etc. */
#include <linux/wrapper.h>	/* for mem_map_reserve */
#include <linux/init.h>
#include <asm/io.h>		/* for accessing the pci-device */
#include <linux/vmalloc.h>	/* for module-version */

#include "saa7146_defs.h"
#include "saa7146_core.h"
#include "saa7146_v4l.h"
#include "av7110.h"
#include "compat.h"
#include "dvb_i2c.h"

/* insmod parameter: here you can specify the number of video-buffers
   to be allocated. for simple capturing 2 buffers (double-buffering)
   should suffice. but if you plan to do 25fps grabbing, you should
   set this to 4(=maximum), in order to be able to catch up from
   temporarily delays */
static int buffers = 2;

/* insmod parameter: some programs (e.g. ´vic´) do not allow to
   specify the used video-mode, so you have to tell this to the
   modules by hand, 0 = PAL, 1 = NTSC  */
static int mode = 0;

/* debug levels: 0 -- no debugging outputs
		 1 -- prints out entering (and exiting if useful) of functions
		 2 -- prints out very, very detailed informations of what is going on
		 3 -- both of the above */
int saa7146_debug = 0;	/* insmod parameter */

#define dprintk		if (saa7146_debug & 1) printk
#define hprintk		if (saa7146_debug & 2) printk

/* ---------------------------------------------*/
/* memory functions - taken from bttv.c		*/
/* ---------------------------------------------*/

static inline unsigned long kvirt_to_pa(unsigned long adr) 
{
	unsigned long kva;

	kva = (unsigned long) page_address(vmalloc_to_page((void *)adr));
	kva |= adr & (PAGE_SIZE-1); /* restore the offset */

	return __pa(kva);
}


static LIST_HEAD(saa7146_list);

static int saa7146_extension_count = 0;
static struct saa7146_extension* saa7146_ext[SAA7146_MAX_EXTENSIONS];

#define SAA7146_I2C_TIMEOUT  100   /* in ms */
#define SAA7146_I2C_RETRIES  6

static u32 SAA7146_I2C_BBR = SAA7146_I2C_BUS_BIT_RATE_3200;

#define	__COMPILE_SAA7146_I2C__
#define	__COMPILE_SAA7146_DEBI__
#include "saa7146.c"
#undef	__COMPILE_SAA7146_I2C__

/* ---------------------------------------------*/
/* memory functions designed for saa7146	*/
/* ---------------------------------------------*/

/* rvmalloc allocates the memory and builds up
   the page-tables for ´quant´-number of buffers */
static void* rvmalloc(int quant, u32* pt[])
{
	void* mem;

	unsigned long adr = 0;
	unsigned long count = 0;

	u32* ptp = 0;
	int i = 0, j = 0;

	dprintk(KERN_ERR "saa7146: rvmalloc called, quant:%d\n",quant);

	if(!quant) 
		return NULL;

	/* get grabbing memory */
	mem = vmalloc_32(quant*GRABBING_MEM_SIZE); 

	if(!mem) 
		return NULL;

	dprintk(KERN_ERR "saa7146: alloc page tables\n");

	/* alloc one page for a page-table for ´quant´ buffers */
	for(i = 0; i < quant; i++) {
		pt[i] = (u32*)kmalloc(PAGE_SIZE,GFP_KERNEL);

		/* error: memory could not be allocated */
		if(!pt[i]) {
			dprintk(KERN_ERR "saa7146: failed, free tables\n");
			for(j = (i-1); j >= 0; j--) 
				kfree(pt[j]);
			dprintk(KERN_ERR "saa7146: free buffer memory\n");
			vfree(mem);
			dprintk(KERN_ERR "saa7146: return 0 address for buffer\n");
			return NULL;
		}
		memset(pt[i], 0x00, PAGE_SIZE);
	}

	dprintk(KERN_ERR "saa7146: clear RAM\n");

	/* clear the ram out, no junk to the user
	   note: 0x7f gives a nice grey field
	   in RGB and YUV as well */
	memset(mem, 0x7f, quant*GRABBING_MEM_SIZE); 

	dprintk(KERN_ERR "saa7146: build page tables\n");
	adr = (unsigned long)mem;
	/* walk through the grabbing-memory and build up the page-tables */
	for(i = 0; i < quant; i++) {

	        for (count=0; count<GRABBING_MEM_SIZE; count+=PAGE_SIZE) 
		        mem_map_reserve(virt_to_page(__va(kvirt_to_pa(adr+count))));
		/* separate loop for SAA MMU, PAGE_SIZE can be !=4096 */
		ptp = pt[i];
		for (count=0; count<GRABBING_MEM_SIZE; count+=4096, adr+=4096)
 	                *(ptp++) = cpu_to_le32(kvirt_to_pa(adr));
	}
	dprintk(KERN_ERR "saa7146: page tables built\n");
	return mem;
}

static void rvfree(void* mem, int quant, u32* pt[])
{
        unsigned long adr, page;
	unsigned long size = 0;

	int i = 0;

	dprintk(KERN_ERR "saa7146: rvfree called\n");

	if (!quant) 
		return;
	
	if (mem) {
		adr = (unsigned long)mem;
		size = quant * GRABBING_MEM_SIZE;
		
		while (size > 0) {
			page = kvirt_to_pa(adr);
			mem_map_unreserve(virt_to_page(__va(page)));
			adr	+= PAGE_SIZE;
			size	-= PAGE_SIZE;
		}
		
		/* release the grabbing memory */
		vfree(mem);
	}
	/* free the page tables */
	for(i = 0; i < quant; i++) {
		kfree(pt[i]);
	}
}


/* ---------------------------------------------*/
/* i2c-functions				*/
/* ---------------------------------------------*/

static
int do_master_xfer (struct dvb_i2c_bus *i2c, struct i2c_msg msgs[], int num)
{
	struct saa7146 *a = i2c->data;
	int count;
	int i = 0;
	
	dprintk(KERN_ERR "saa7146_core.o: master_xfer called, num:%d\n",num);

	/* prepare the message(s), get number of u32s to transfer */
	count = prepare(msgs, num, a->i2c);

	if (count < 0) {
		hprintk(KERN_ERR "saa7146_core.o: could not prepare i2c-message\n");
		return -EIO;
	}

	/* reset the i2c-device if necessary */
	if (i2c_reset(a) < 0) {
		hprintk(KERN_ERR "saa7146_core.o: could not reset i2c-bus\n");
		return -EIO;
	}	

	for(i = 0; i < count; i++) {
		/* see how many u32 have to be transferred;
		 * if there is only 1,
		 * we do not start the whole rps1-engine...
		 */

			/* if address-error occured, don't retry */
		if (i2c_write_out(a, &a->i2c[i], SAA7146_I2C_TIMEOUT) < 0) {
			hprintk (KERN_ERR "saa7146_core.o: "
				"i2c error in address phase\n");
			return -EREMOTEIO;
	}
	}

	/* if any things had to be read, get the results */
	if (clean_up(msgs, num, a->i2c) < 0) {
		hprintk(KERN_ERR "saa7146_core.o: i2c cleanup failed!\n");
		return -EIO;
	}

	/* return the number of delivered messages */
	return num;
}



static
int master_xfer (struct dvb_i2c_bus *i2c, struct i2c_msg msgs[], int num)
{
	struct saa7146 *saa = i2c->data;
	int retries = SAA7146_I2C_RETRIES;
	int ret;

	if (down_interruptible (&saa->i2c_sem))
		return -ERESTARTSYS;

	do {
		ret = do_master_xfer (i2c, msgs, num);
	} while (ret != num && retries--);

	up (&saa->i2c_sem);

	return ret;
}


/* registering functions to load algorithms at runtime */
int i2c_saa7146_add_bus (struct saa7146 *saa)
{
	init_MUTEX(&saa->i2c_sem);

	/* enable i2c-port pins */
	saa7146_write (saa->mem, MC1, (MASK_08 | MASK_24));

	sprintf(saa->name, "saa7146(%d)", saa->dvb_adapter->num);	

	saa->i2c_bus = dvb_register_i2c_bus (master_xfer, saa,
					     saa->dvb_adapter, 0);
	if (!saa->i2c_bus)
		return -ENOMEM;

	return 0;
}


void i2c_saa7146_del_bus (struct saa7146 *saa)
{
	dvb_unregister_i2c_bus (master_xfer,
				saa->i2c_bus->adapter, saa->i2c_bus->id);

	dvb_unregister_adapter (saa->dvb_adapter);
}

/* ---------------------------------------------*/
/* debug-helper function: dump-registers	*/
/* ---------------------------------------------*/

void	dump_registers(unsigned char* mem) {	
	
	u16 j = 0;
	
	for( j = 0x0; j < 0x1fe; j+=0x4 ) {
	printk("0x%03x: 0x%08x\n",j,saa7146_read(mem,j));
	}

}

/* -----------------------------------------------------*/
/* dispatcher-function for handling external commands	*/
/* -----------------------------------------------------*/

static int saa7146_core_command (struct dvb_i2c_bus *i2c, unsigned int cmd, void *arg)
{
	int i = 0, result = -ENOIOCTLCMD;
	struct saa7146* saa = i2c->data;

	dprintk("saa7146_core.o: ==> saa7146_core_command\n");

	if( NULL == saa)
		return -EINVAL;

	/* first let the extensions handle the command */
	for (i = 0; i < SAA7146_MAX_EXTENSIONS; i++) {
		if (NULL != saa7146_ext[i]) {
			if( -ENOIOCTLCMD != (result = saa7146_ext[i]->command(saa, saa->data[i], cmd, arg))) {
				break;
			}
		}
	}

	/* if command has not been handled by an extension, handle it now */
	if( result == -ENOIOCTLCMD ) {
		
		switch(cmd) {
	        case SAA7146_DUMP_REGISTERS:
		{
			dump_registers(saa->mem);
                        break;
		}
		case SAA7146_SET_DD1:
		{
			u32 *i = arg;

			dprintk(KERN_ERR "saa7146_core.o: SAA7146_SET_DD1 to 0x%08x\n",*i);

			/* set dd1 port register */
			saa7146_write(saa->mem, DD1_INIT, *i);

			/* write out init-values */
			saa7146_write(saa->mem,MC2, (MASK_09 | MASK_10 | MASK_26 | MASK_26));

			break;
		}
		case SAA7146_DO_MMAP:
		{
			struct vm_area_struct *vma = arg;
			unsigned long size = vma->vm_end - vma->vm_start;
			unsigned long start = vma->vm_start;
			unsigned long page,pos;

			dprintk(KERN_ERR "saa7146_core.o: SAA7146_DO_MMAP.\n");
			
			if (size > saa->buffers * GRABBING_MEM_SIZE)
			        return -EINVAL;

                        if ( NULL == saa->grabbing )
                                return -EINVAL;

			pos=(unsigned long)saa->grabbing;

			while (size > 0) 
			{
			        page = kvirt_to_pa(pos);
				if (remap_page_range(vma, start, page,
						     PAGE_SIZE, PAGE_SHARED))
					return -EAGAIN;
				start	+= PAGE_SIZE;
				pos	+= PAGE_SIZE;
				size	-= PAGE_SIZE;    
			}
				
			break;
		}
		case SAA7146_DEBI_TRANSFER: {
		
			struct saa7146_debi_transfer *dt = arg;

			dprintk("saa7146_core.o: SAA7146_DEBI_TRANSFER\n");
			dprintk("saa7146_core.o: timeout:%d, swap:%d, slave16:%d, increment:%d, intel:%d, tien:%d\n", dt->timeout, dt->swap, dt->slave16, dt->increment, dt->intel, dt->tien);
			dprintk("saa7146_core.o: address:0x%04x, num_bytes:%d, direction:%d, mem:0x%08x\n",dt->address,dt->address,dt->direction,dt->mem);						

			debi_transfer(saa, dt);
			break;
		}
		
		default: {
			return -ENOIOCTLCMD;
		}
		}
	}
	
	return 0;
}

/* -----------------------------------------------------*/
/* dispatcher-function for handling irq-events		*/
/* -----------------------------------------------------*/

/* irq-handler function */
static void saa7146_irq(int irq, void *dev_id, struct pt_regs * regs)
{
	struct saa7146 *saa = (struct saa7146 *)dev_id;
	u32 isr = 0;
	int i;
	int count = 0;

	/* process all interrupts */
	while (1) {

		/* read out the primary status register */
		isr = saa7146_read(saa->mem, ISR);
		/* clear all IRQs */
		saa7146_write(saa->mem, ISR, isr);
	
		/* is anything to do? */
		if ( 0 == isr )
			return;

		dprintk("%s: irq-call: isr:0x%08x\n",saa->name,isr);
		
		/* first let the extensions handle the interrupt */
		for (i = 0; i < SAA7146_MAX_EXTENSIONS; i++) 
			if (saa7146_ext[i] && 
			    (isr&saa7146_ext[i]->handles_irqs)) {
			  saa7146_ext[i]->irq_handler(saa, isr, saa->data[i]);
			  //saa7146_write(saa->mem, ISR, saa7146_ext[i]->handles_irqs);
			}
		
		//printk(KERN_ERR "%s: unhandled interrupt: 0x%08x\n", saa->name, isr);
		
		/* see if we are in a hard interrupt loop */
		++count;
		if (count > 10)
			printk (KERN_WARNING "%s: irq loop %d\n", saa->name, count);
		if (count > 20) {
			saa7146_write(saa->mem, IER, 0x00000000);
			printk(KERN_ERR "%s: IRQ lockup, cleared int mask\n", saa->name);
			break;
		}
	}
}

/* -----------------------------------------------------
   functions for finding any saa7146s in the system,
   inserting/removing module for kernel, etc.
   -----------------------------------------------------*/

int configure_saa7146 (struct saa7146 *saa)
{
	u32 rev = 0;
	int result = 0;

	hprintk("saa7146_core.o: ==> configure_saa7146\n");

	/* check module-parameters for sanity */

	/* check if wanted number of video-buffers is valid, otherwise fix it */
	//if (buffers < 2)
	//	buffers = 2;

	if ( buffers > SAA7146_MAX_BUF )
		buffers = SAA7146_MAX_BUF;

	/* check if mode is supported */
	switch( mode ) {
		/* 0 = pal, 1 = ntsc */
		case 0:
		case 1:
		{
			break;
		}
		/* default to pal */
		default:
		{
			mode = 0;
			break;
		}
	}

	/* get chip-revision; this is needed to enable bug-fixes */
	if( 0 > pci_read_config_dword(saa->device, 0x08, &rev)) {
		printk (KERN_ERR 
			"saa7146_core.o: cannot read from pci-device!\n");
		return -1;
	}

	saa->revision = (rev & 0xf);

	/* remap the memory from virtual to physical adress */
	saa->mem = ioremap ((saa->device->resource[0].start)
			    &PCI_BASE_ADDRESS_MEM_MASK, 0x1000);

	if ( !saa->mem ) {
	    	printk(KERN_ERR "saa7146_core.o: cannot map pci-address!\n");
		return -EFAULT;
	}
	
	/* get clipping memory */	
	saa->clipping = (u32*) kmalloc (CLIPPING_MEM_SIZE*sizeof(u32),GFP_KERNEL);

	if ( !saa->clipping ) {
	    	printk(KERN_ERR "saa7146_core.o: not enough kernel-memory for clipping!\n");
		return -ENOMEM;
	}

	memset(saa->clipping, 0x0, CLIPPING_MEM_SIZE*sizeof(u32));

	/* get i2c memory */	
	saa->i2c = (u32*) kmalloc (I2C_MEM_SIZE*sizeof(u32),GFP_KERNEL); /*64*/

	if ( !saa->i2c ) {
	    	printk(KERN_ERR "saa7146_core.o: not enough kernel-memory for i2c!\n");
		kfree(saa->clipping);
		return -ENOMEM;
	}

	memset(saa->i2c, 0x0, I2C_MEM_SIZE*sizeof(u32));
	
	/* get grabbing memory */
	saa->grabbing = (u32*) rvmalloc (buffers, &saa->page_table[0]);

	if ( !saa->grabbing ) {
	    	printk(KERN_ERR "saa7146_core.o: not enough kernel-memory for grabbing_mem!\n");
		kfree(saa->i2c);
		kfree(saa->clipping);
		return -ENOMEM;
	}

	/* get rps0 memory */
	saa->rps0 = (u32*) kmalloc (RPS_MEM_SIZE*sizeof(u32),GFP_KERNEL);

	if ( !saa->rps0 ) {
	    	printk(KERN_ERR "saa7146_core.o: not enough kernel-memory for rps0_mem!\n");
		kfree(saa->i2c);
		kfree(saa->clipping);
		rvfree(saa->grabbing, buffers, &saa->page_table[0]);
		return -ENOMEM;
	}

	memset(saa->rps0, 0x0, RPS_MEM_SIZE*sizeof(u32));

	/* get rps1 memory */
	saa->rps1 = (u32*) kmalloc (RPS_MEM_SIZE*sizeof(u32),GFP_KERNEL);
	if ( !saa->rps1 ) {
	    	printk(KERN_ERR "saa7146_core.o: not enough kernel-memory for rps1_mem!\n");
		kfree(saa->rps0);
		kfree(saa->i2c);
		kfree(saa->clipping);
		rvfree(saa->grabbing, buffers, &saa->page_table[0]);
		return -1;
	}

	memset(saa->rps1, 0x0, RPS_MEM_SIZE*sizeof(u32));

	/* get debi memory (32kB) */
	saa->debi = (u32*) kmalloc (8192*sizeof(u32),GFP_KERNEL);

	if ( !saa->debi ) {
	    	printk(KERN_ERR "saa7146_core.o: not enough kernel-memory for debi_mem!\n");
		kfree(saa->rps1);
		kfree(saa->rps0);
		kfree(saa->i2c);
		kfree(saa->clipping);
		rvfree(saa->grabbing, buffers, &saa->page_table[0]);
		return -1;
	}

	memset(saa->debi, 0x0, 8192*sizeof(u32));


	/* clear out memory for grabbing information */
	memset(&saa->grab_width[0],  0x0, sizeof(int)*SAA7146_MAX_BUF);
	memset(&saa->grab_height[0], 0x0, sizeof(int)*SAA7146_MAX_BUF);
	memset(&saa->grab_format[0], 0x0, sizeof(int)*SAA7146_MAX_BUF);
	memset(&saa->grab_port[0], 0x0, sizeof(int)*SAA7146_MAX_BUF);

	/* init the frame-status array */
	memset(&saa->frame_stat[0], GBUFFER_UNUSED, sizeof(int)*SAA7146_MAX_BUF);
	
	/* clear out all wait queues */
	init_waitqueue_head(&saa->rps0_wq);
	init_waitqueue_head(&saa->rps1_wq);
	

    	/* request an interrupt for the saa7146 */
	result = request_irq (saa->device->irq, saa7146_irq,
			      SA_SHIRQ | SA_INTERRUPT, saa->name, (void *) saa);

	switch(result) {
		case -EINVAL:
		{
			printk(KERN_ERR "saa7146_core.o: Bad irq number or handler\n");
			return -EINVAL;
		}
		case -EBUSY:
		{
			printk(KERN_ERR "saa7146_core.o: IRQ %d busy, change your PnP config in BIOS\n", saa->device->irq);
			return -EBUSY;
		}
		case 0:
		{
			break;
		}
		default:
		{
			return result;
		}
	}
	
	/* print status message */
    	dprintk("saa7146_core.o: %s: bus:%d, rev:%d, mem:0x%08x.\n", saa->name, saa->device->bus->number, saa->revision, (unsigned int) saa->mem);

	/* enable bus-mastering */
 	pci_set_master( saa->device );

	/* disable everything on the saa7146, perform a software-reset */
	saa7146_write(saa->mem, MC1, 0xbfff0000);
	mdelay(2);
#if 0
	{
		int j;

		/* clear all registers */
		for( j = 0x0; j < 0xfc; j+=0x4 ) {
			saa7146_write(saa->mem,j, 0x0000000);
		}
		for( j = 0x104; j < 0x1fc; j+=0x4 ) {
			saa7146_write(saa->mem,j, 0x0000000); 
		}
	}
#endif 
	/* clear out any rps-signals pending */
	saa7146_write(saa->mem, MC2, 0xf8000000);

	/* enable video-port-pins*/
	saa7146_write(saa->mem,MC1, (MASK_10 | MASK_26));

	/* disable all interrupt-conditions, only enable RPS interrupts */
	saa7146_write(saa->mem, ISR, 0xffffffff);
	saa7146_write(saa->mem, IER, (MASK_27 | MASK_28));
/*
	printk("main: 0x114: 0x%08x\n",saa7146_read(saa->mem, 0x114));
	printk("main: 0x0e4: 0x%08x\n",saa7146_read(saa->mem, 0x0e4));
	printk("PSR:   0x%08x\n",saa7146_read(saa->mem, PSR));
	printk("SSR:   0x%08x\n",saa7146_read(saa->mem, SSR));
	printk("IER:   0x%08x\n",saa7146_read(saa->mem, IER));
	printk("ISR:   0x%08x\n",saa7146_read(saa->mem, ISR));
*/

	saa7146_write(saa->mem,PCI_BT_V1, 0x1c00101f);
	saa7146_write(saa->mem,BCS_CTRL, 0x80400040);

	/* set dd1 stream a & b */
      	saa7146_write(saa->mem, DD1_STREAM_B, 0x00000000);
	saa7146_write(saa->mem, DD1_INIT, 0x02000000);
	saa7146_write(saa->mem, MC2, (MASK_09 | MASK_25 | MASK_10 | MASK_26));

	saa7146_write(saa->mem, MC2, 0x077c077c);

	/* the Siemens DVB needs this if you want to have the i2c chips
           get recognized before the main driver is loaded 
	*/
        saa7146_write(saa->mem, GPIO_CTRL, 0x500000);

	saa->command = &saa7146_core_command;
	saa->buffers = buffers;
	saa->mode = mode;
	saa->interlace = 1;

	i2c_saa7146_add_bus (saa);

        saa7146_write(saa->mem, GPIO_CTRL, 0x000000);
	return 0;
}


void saa7146_foreach (void (*callback) (struct saa7146* saa, void *data),
		      void *data)
{
	struct list_head *entry;

	list_for_each (entry, &saa7146_list) {
		struct saa7146* saa;

		saa = list_entry (entry, struct saa7146, list_head);
		callback (saa, data);
	}
}


static
void saa7146_attach_extension (struct saa7146* saa, void *data)
{
	int ext_id = (int) data;
	saa7146_ext[ext_id]->attach (saa, &saa->data[ext_id]);
}


static
void saa7146_detach_extension (struct saa7146* saa, void *data)
{
	int ext_id = (int) data;
	saa7146_ext[ext_id]->detach (saa, &saa->data[ext_id]);
}


int saa7146_add_extension(struct saa7146_extension* ext)
{
	int ext_id = 0;

	for (ext_id = 0; ext_id < SAA7146_MAX_EXTENSIONS; ext_id++) {
		if (NULL == saa7146_ext[ext_id])
			break;
		if (SAA7146_MAX_EXTENSIONS == ext_id) {
			printk(KERN_WARNING "saa7146.o: attach_extension(%s) - "
			       "enlarge SAA7146_MAX_EXTENSIONS.\n",ext->name);
			return -ENOMEM;
		}
	}

	saa7146_ext[ext_id] = ext;
	saa7146_extension_count++;

	if (ext->attach)
 		saa7146_foreach (saa7146_attach_extension, (void*) ext_id);
	
	return 0;
}


int saa7146_del_extension(struct saa7146_extension* ext)
{
	int ext_id = 0;

	for (ext_id = 0; ext_id < SAA7146_MAX_EXTENSIONS; ext_id++)
		if (ext == saa7146_ext[ext_id])
			break;

	if (SAA7146_MAX_EXTENSIONS == ext_id) {
		printk("%s: detach_extension extension [%s] not found.\n",
			__FUNCTION__, ext->name);
		return -ENODEV;
	}		

	if (ext->detach)
 		saa7146_foreach (saa7146_detach_extension, (void*) ext_id);
	
	saa7146_ext[ext_id] = NULL;
	saa7146_extension_count--;		

	return 0;
}


static
void remove_saa7146(struct saa7146 *saa)
{
	i2c_saa7146_del_bus (saa);

	/* shut down all dma transfers */
        saa7146_write(saa->mem, MC1, 0xbfff0000);
	
	dprintk("free irqs\n");
	/* disable alle irqs, release irq-routine */
	saa7146_write(saa->mem, IER, 0x00);
	saa7146_write(saa->mem, ISR, 0xffffffff);
	free_irq(saa->device->irq, (void *)saa);
	dprintk("unmap memory\n");
	/* unmap the memory, if necessary */
	if (saa->mem)
		iounmap((unsigned char *)((unsigned int)saa->mem));
	
	dprintk("release grabbing memory\n");
	/* release grabbing memory */
	if(saa->grabbing)
		rvfree(saa->grabbing, buffers, &saa->page_table[0]);
	
	dprintk("release other memory\n");
	/* release clipping, i2c, rps0 memory */
	kfree(saa->clipping);
	kfree(saa->i2c);
	kfree(saa->rps0);
	kfree(saa->rps1);
	kfree(saa->debi);
}


static int saa7146_suspend(struct pci_dev *pdev, u32 state)
{
        printk("saa7146_suspend()\n");
	saa7146_core_command(((struct saa7146 *) pdev->driver_data)->i2c_bus,
			     SAA7146_SUSPEND, 0);
	return 0;
}

static int
saa7146_resume(struct pci_dev *pdev)
{
        printk("saa7146_resume()\n");
	saa7146_core_command(((struct saa7146 *) pdev->driver_data)->i2c_bus,
			     SAA7146_RESUME, 0);
	return 0;
}


struct card_info {
	int type;
	char *name;
};


static
int __devinit saa7146_init_one (struct pci_dev *pdev,
				const struct pci_device_id *ent)
{
	struct dvb_adapter *adap;
	struct saa7146 *saa;
	int card_type;
	struct card_info *cinfo= (struct card_info *) ent->driver_data;

	dprintk("saa7146_init_one()\n");

	card_type = cinfo->type;
	dvb_register_adapter(&adap, cinfo->name);

	if (!(saa = kmalloc (sizeof (struct saa7146), GFP_KERNEL))) {
		printk ("%s: out of memory!\n", __FUNCTION__);
		return -ENOMEM;
	}

	memset (saa, 0, sizeof (struct saa7146));

	saa->device = pdev;
	saa->device->driver_data = saa;
	saa->card_type = card_type;
	saa->dvb_adapter = adap;

	pci_enable_device (saa->device);

	configure_saa7146 (saa);

	list_add_tail (&saa->list_head, &saa7146_list);

	return 0;
}

static
void __devexit saa7146_remove_one (struct pci_dev *pdev)
{
	struct saa7146 *saa = pdev->driver_data;

	dprintk("saa7146_remove_one()\n");

	list_del (&saa->list_head);
	pci_disable_device(pdev);
	remove_saa7146 (saa);
}


static struct card_info fs_1_5 = { DVB_CARD_TT_SIEMENS,   "Siemens cable card PCI rev1.5" };
static struct card_info fs_1_3 = { DVB_CARD_TT_SIEMENS,   "Siemens/Technotrend/Hauppauge PCI rev1.3" };
static struct card_info ttbs   = { DVB_CARD_TT_BUDGET,    "TT-Budget/WinTV-NOVA-S  PCI" };
static struct card_info ttbc   = { DVB_CARD_TT_BUDGET,    "TT-Budget/WinTV-NOVA-C  PCI" };
static struct card_info ttbt   = { DVB_CARD_TT_BUDGET,    "TT-Budget/WinTV-NOVA-T  PCI" };
static struct card_info ttbci  = { DVB_CARD_TT_BUDGET_CI, "TT-Budget/WinTV-NOVA-CI PCI" };
static struct card_info satel  = { DVB_CARD_TT_BUDGET,    "SATELCO Multimedia PCI"};
static struct card_info unkwn  = { DVB_CARD_TT_SIEMENS,   "Technotrend/Hauppauge PCI rev?(unknown0)?"};
static struct card_info tt_1_6 = { DVB_CARD_TT_SIEMENS,   "Technotrend/Hauppauge PCI rev1.3 or 1.6" };
static struct card_info tt_2_1 = { DVB_CARD_TT_SIEMENS,   "Technotrend/Hauppauge PCI rev2.1" };
static struct card_info tt_t   = { DVB_CARD_TT_SIEMENS,   "Technotrend/Hauppauge PCI DVB-T" };
static struct card_info knc1   = { DVB_CARD_KNC1,         "KNC1 DVB-S" };

#define PHILIPS_SAA7146 PCI_VENDOR_ID_PHILIPS, PCI_DEVICE_ID_PHILIPS_SAA7146
#define CARD_INFO driver_data: (unsigned long) &

static struct pci_device_id saa7146_pci_tbl[] __devinitdata = {
	{ PHILIPS_SAA7146, 0x110a, 0xffff, CARD_INFO fs_1_5 },
	{ PHILIPS_SAA7146, 0x110a, 0x0000, CARD_INFO fs_1_5 },
	{ PHILIPS_SAA7146, 0x13c2, 0x1003, CARD_INFO ttbs },
	{ PHILIPS_SAA7146, 0x13c2, 0x1004, CARD_INFO ttbc },
	{ PHILIPS_SAA7146, 0x13c2, 0x1005, CARD_INFO ttbt },
	{ PHILIPS_SAA7146, 0x13c2, 0x100c, CARD_INFO ttbci },
	{ PHILIPS_SAA7146, 0x13c2, 0x1013, CARD_INFO satel },
	{ PHILIPS_SAA7146, 0x13c2, 0x0000, CARD_INFO fs_1_3 },
	{ PHILIPS_SAA7146, 0x13c2, 0x1002, CARD_INFO unkwn },
	{ PHILIPS_SAA7146, 0x13c2, 0x0001, CARD_INFO tt_1_6 },
	{ PHILIPS_SAA7146, 0x13c2, 0x0002, CARD_INFO tt_2_1 },
	{ PHILIPS_SAA7146, 0x13c2, 0x0003, CARD_INFO tt_2_1 },
	{ PHILIPS_SAA7146, 0x13c2, 0x0004, CARD_INFO tt_2_1 },
	{ PHILIPS_SAA7146, 0x13c2, 0x0006, CARD_INFO tt_1_6 },
	{ PHILIPS_SAA7146, 0x13c2, 0x0008, CARD_INFO tt_t },
	{ PHILIPS_SAA7146, 0xffc2, 0x0000, CARD_INFO unkwn },
	{ PHILIPS_SAA7146, 0x1131, 0x4f56, CARD_INFO knc1 },
	{ 0,},
};

MODULE_DEVICE_TABLE(pci, saa7146_pci_tbl);
	
static struct pci_driver saa7146_driver = {
	.name		= "saa7146",
	.id_table	= saa7146_pci_tbl,
	.probe		= saa7146_init_one,
	.remove		= saa7146_remove_one,
	.suspend	= saa7146_suspend,
	.resume		= saa7146_resume,
};


static
int __init saa7146_init_module(void)
{
	int err;

	dprintk("saa7146_init_module\n");

	if ((err = pci_module_init(&saa7146_driver)))
		return err;

	if ((err = saa7146_v4l_init ()))
		return err;

	if ((err = av7110_init ()))
		return err;

	if ((err = av7110_ir_init ()))
		return err;

	return 0;
}

static
void __exit saa7146_cleanup_module(void)
{
	av7110_ir_exit ();
	av7110_exit ();
	saa7146_v4l_exit ();
	pci_unregister_driver(&saa7146_driver);
}

module_init(saa7146_init_module);
module_exit(saa7146_cleanup_module);

MODULE_AUTHOR("Michael Hunold <michael@mihu.de>, "
	      "Christian Theiss <mistert@rz.fh-augsburg.de>, "
	      "Ralph Metzler <rjkm@convergence.de>, "
	      "Marcus Metzler <mocm@convergence.de>, "
	      "Holger Waechtler <holger@convergence.de> and others");

MODULE_DESCRIPTION("driver for saa7146/av7110 based DVB PCI cards");
MODULE_LICENSE("GPL");
MODULE_PARM(mode,"i");
MODULE_PARM(saa7146_debug,"i");
MODULE_PARM(buffers,"i");

