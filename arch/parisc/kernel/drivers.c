/*

drivers.c

Copyright (c) 1999 The Puffin Group 

This is a collection of routines intended to register all the devices
in a system, and register device drivers.

*/

#include <linux/slab.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <asm/hardware.h>
#include <asm/io.h>
#include <asm/pdc.h>


extern struct hp_hardware *parisc_get_reference(
	unsigned short hw_type, unsigned long hversion, 
	unsigned long sversion );


/* I'm assuming there'll never be 64 devices.  We should probably make 
   this more flexible.  */

#define MAX_DEVICES 64

unsigned int num_devices = 0;

struct hp_device devices[MAX_DEVICES];

static unsigned long pdc_result[32] __attribute__ ((aligned (16))) = {0,0,0,0};
static  u8 iodc_data[32] __attribute__ ((aligned (64)));

/*
 *	XXX should we be using a locked array ?
 */
 
int register_driver(struct pa_iodc_driver *driver)
{
	unsigned int i;
	struct hp_device * device;

	for (;driver->check;driver++)  {

		for (i=0;i<num_devices;i++) {
			device = &devices[i];

			if (device->managed) continue;

			if ((driver->check & DRIVER_CHECK_HWTYPE) &&
			    (driver->hw_type != device->hw_type))
				continue;
			if ((driver->check & DRIVER_CHECK_HVERSION) &&
			    (driver->hversion != device->hversion))
				continue;
			if ((driver->check & DRIVER_CHECK_HVERSION_REV) &&
			    (driver->hversion_rev != device->hversion_rev))
				continue;
			if ((driver->check & DRIVER_CHECK_SVERSION) &&
			    (driver->sversion != device->sversion))
				continue;
			if ((driver->check & DRIVER_CHECK_SVERSION_REV) &&
			    (driver->sversion_rev != device->sversion_rev))
				continue;
			if ((driver->check & DRIVER_CHECK_OPT) &&
			    (driver->opt != device->opt))
				continue;
			if ( (*driver->callback)(device,driver) ==0) {
				device->managed=1;
			} else {
				printk("Warning : device (%d, 0x%x, 0x%x, 0x%x, 0x%x) NOT claimed by %s %s\n",
					device->hw_type,
					device->hversion, device->hversion_rev,
					device->sversion, device->sversion_rev,
					driver->name, driver->version);
			}
		}
	}
	return 0;
}


struct hp_device * register_module(void *hpa)
{

	struct hp_device * d;
	int status;

	d = &devices[num_devices];
	status = pdc_iodc_read(&pdc_result,hpa,0,&iodc_data,32 );
	if (status !=PDC_RET_OK) {
		/* There is no device here, so we'll skip it */
		return 0;
	}

	d->hw_type = iodc_data[3]&0x1f;
	d->hversion = (iodc_data[0]<<4)|((iodc_data[1]&0xf0)>>4);
	d->sversion = 
		((iodc_data[4]&0x0f)<<16)|(iodc_data[5]<<8)|(iodc_data[6]);
	d->hversion_rev = iodc_data[1]&0x0f;
	d->sversion_rev = iodc_data[4]>>4;
	d->opt = iodc_data[7];
	d->hpa = hpa;
	d->managed=0;
	d->reference = parisc_get_reference(d->hw_type, d->hversion, 
								d->sversion);
		
	num_devices++;	

	return d;
}	

void print_devices(char * buf) {

	int i;
	struct hp_device *d;
	printk("Found devices:\n");
	for (i=0;i<num_devices;i++) {	
		d = &devices[i];
		printk(KERN_INFO 
		"%d. %s (%d) at 0x%p, versions 0x%x, 0x%x, 0x%x, 0x%x, 0x%x\n", 
		i+1,
		(d->reference) ? d->reference->name : "Unknown device",
		d->hw_type,d->hpa, d->hversion, d->hversion_rev,
		d->sversion, d->sversion_rev, d->opt);

	}
	printk("That's a total of %d devices.\n",num_devices);
}


