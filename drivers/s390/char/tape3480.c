/***************************************************************************
 *
 *  drivers/s390/char/tape3480.c
 *    tape device discipline for 3480 tapes.
 *
 *  S390 and zSeries version
 *    Copyright (C) 2001 IBM Corporation
 *    Author(s): Carsten Otte <cotte@de.ibm.com>
 *               Tuan Ngo-Anh <ngoanh@de.ibm.com>
 * 
 ****************************************************************************
 */

#include "tapedefs.h"
#include <linux/version.h>
#include <linux/compatmac.h>
#include "tape.h"
#include "tape34xx.h"
#include "tape3480.h"

#ifdef CONFIG_S390_TAPE_3480_MODULE
static tape_discipline_t* disc;

void
init_module(void)
{
        disc = tape3480_init();
        if (disc!= NULL) 
		tape_register_discipline(disc);
}

void
cleanup_module(void)
{
	if (disc!=NULL){
		tape_unregister_discipline(disc);
		kfree(disc);
	}
}
#endif /* CONFIG_S390_TAPE_3480_MODULE */

int
tape3480_setup_device(tape_dev_t * td)
{
	tape3480_disc_data_t *data = NULL;
	tape_sprintf_event (tape_dbf_area,6,"3480 dsetup:  %x\n",td->first_minor);
	data = kmalloc (sizeof (tape3480_disc_data_t), GFP_KERNEL | GFP_DMA);
	if(data == NULL)
		return -1;
	data->modeset_byte = 0x00;
	td->discdata = (void *) data;
	return 0;
}

void
tape3480_cleanup_device(tape_dev_t * td)
{
	if(td->discdata){
		kfree(td->discdata);
		td->discdata = NULL;
	}
}

void
tape3480_shutdown (void) {
}

tape_discipline_t *
tape3480_init (void)
{
	tape_discipline_t *disc;
	tape_sprintf_event (tape_dbf_area,3,"3480 init\n");
	disc = kmalloc (sizeof (tape_discipline_t), GFP_ATOMIC);
	if (disc == NULL) {
	        tape_sprintf_exception (tape_dbf_area,3,"disc:nomem\n");
		return disc;
	}
	disc->owner = THIS_MODULE;
	disc->cu_type = 0x3480;
	disc->setup_device = tape3480_setup_device;
	disc->cleanup_device = tape3480_cleanup_device;
	disc->init_device = NULL;
	disc->process_eov = tape34xx_process_eov;
	disc->irq = tape34xx_irq;
	disc->write_block = tape34xx_write_block;
	disc->read_block = tape34xx_read_block;
	disc->ioctl = tape34xx_ioctl;
	disc->shutdown = tape3480_shutdown;
	disc->discipline_ioctl_overload = tape34xx_ioctl_overload;
	disc->bread = tape34xx_bread;
	disc->free_bread = tape34xx_free_bread;
	disc->bread_enable_locate = tape34xx_bread_enable_locate;
	disc->next = NULL;
	tape_sprintf_event (tape_dbf_area,3,"3480 regis\n");
	return disc;
}
