#define AUTOSENSE
/* #define PSEUDO_DMA */

/*
 * EcoSCSI Generic NCR5380 driver
 *
 * Copyright 1995, Russell King
 *
 * ALPHA RELEASE 1.
 *
 * For more information, please consult
 *
 * NCR 5380 Family
 * SCSI Protocol Controller
 * Databook
 *
 * NCR Microelectronics
 * 1635 Aeroplaza Drive
 * Colorado Springs, CO 80916
 * 1+ (719) 578-3400
 * 1+ (800) 334-5454
 */

/*
 * Options :
 *
 * PARITY - enable parity checking.  Not supported.
 *
 * SCSI2 - enable support for SCSI-II tagged queueing.  Untested.
 *
 * USLEEP - enable support for devices that don't disconnect.  Untested.
 */

/*
 * $Log: ecoscsi.c,v $
 * Revision 1.2  1998/03/08 05:49:47  davem
 * Merge to 2.1.89
 *
 * Revision 1.1  1998/02/23 02:45:24  davem
 * Merge to 2.1.88
 *
 */

#include <linux/module.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/blk.h>

#include <asm/io.h>
#include <asm/system.h>

#include "../../scsi/scsi.h"
#include "../../scsi/hosts.h"
#include "ecoscsi.h"
#include "../../scsi/NCR5380.h"
#include "../../scsi/constants.h"

static char ecoscsi_read(struct Scsi_Host *instance, int reg)
{
  int iobase = instance->io_port;
  outb(reg | 8, iobase);
  return inb(iobase + 1);
}

static void ecoscsi_write(struct Scsi_Host *instance, int reg, int value)
{
  int iobase = instance->io_port;
  outb(reg | 8, iobase);
  outb(value, iobase + 1);
}

/*
 * Function : ecoscsi_setup(char *str, int *ints)
 *
 * Purpose : LILO command line initialization of the overrides array,
 *
 * Inputs : str - unused, ints - array of integer parameters with ints[0]
 *	equal to the number of ints.
 *
 */

void ecoscsi_setup(char *str, int *ints) {
}

/*
 * Function : int ecoscsi_detect(Scsi_Host_Template * tpnt)
 *
 * Purpose : initializes ecoscsi NCR5380 driver based on the
 *	command line / compile time port and irq definitions.
 *
 * Inputs : tpnt - template for this SCSI adapter.
 *
 * Returns : 1 if a host adapter was found, 0 if not.
 *
 */

int ecoscsi_detect(Scsi_Host_Template * tpnt)
{
    struct Scsi_Host *instance;

    tpnt->proc_name = "ecoscsi";

    instance = scsi_register (tpnt, sizeof(struct NCR5380_hostdata));
    instance->io_port = 0x80ce8000;
    instance->n_io_port = 144;
    instance->irq = IRQ_NONE;

    if (check_region (instance->io_port, instance->n_io_port)) {
	scsi_unregister (instance);
	return 0;
    }

    ecoscsi_write (instance, MODE_REG, 0x20);		/* Is it really SCSI? */
    if (ecoscsi_read (instance, MODE_REG) != 0x20) {	/* Write to a reg.    */
        scsi_unregister(instance);
        return 0;					/* and try to read    */
    }
    ecoscsi_write( instance, MODE_REG, 0x00 );		/* it back.	      */
    if (ecoscsi_read (instance, MODE_REG) != 0x00) {
        scsi_unregister(instance);
        return 0;
    }

    NCR5380_init(instance, 0);
    request_region (instance->io_port, instance->n_io_port, "ecoscsi");

    if (instance->irq != IRQ_NONE)
	if (request_irq(instance->irq, do_ecoscsi_intr, SA_INTERRUPT, "ecoscsi", NULL)) {
	    printk("scsi%d: IRQ%d not free, interrupts disabled\n",
	    instance->host_no, instance->irq);
	    instance->irq = IRQ_NONE;
	}

    if (instance->irq != IRQ_NONE) {
  	printk("scsi%d: eek! Interrupts enabled, but I don't think\n", instance->host_no);
	printk("scsi%d: that the board had an interrupt!\n", instance->host_no);
    }

    printk("scsi%d: at port %X irq", instance->host_no, instance->io_port);
    if (instance->irq == IRQ_NONE)
	printk ("s disabled");
    else
        printk (" %d", instance->irq);
    printk(" options CAN_QUEUE=%d CMD_PER_LUN=%d release=%d",
        CAN_QUEUE, CMD_PER_LUN, ECOSCSI_PUBLIC_RELEASE);
    printk("\nscsi%d:", instance->host_no);
    NCR5380_print_options(instance);
    printk("\n");
    return 1;
}

int ecoscsi_release (struct Scsi_Host *shpnt)
{
	if (shpnt->irq != IRQ_NONE)
		free_irq (shpnt->irq, NULL);
	if (shpnt->io_port)
		release_region (shpnt->io_port, shpnt->n_io_port);
	return 0;
}

const char * ecoscsi_info (struct Scsi_Host *spnt) {
    return "";
}

#if 0
#define STAT(p) inw(p + 144)

static inline int NCR5380_pwrite(struct Scsi_Host *instance, unsigned char *addr,
              int len)
{
  int iobase = instance->io_port;
printk("writing %p len %d\n",addr, len);
  if(!len) return -1;

  while(1)
  {
    int status;
    while(((status = STAT(iobase)) & 0x100)==0);
  }
}

static inline int NCR5380_pread(struct Scsi_Host *instance, unsigned char *addr,
              int len)
{
  int iobase = instance->io_port;
  int iobase2= instance->io_port + 0x100;
  unsigned char *start = addr;
  int s;
printk("reading %p len %d\n",addr, len);
  outb(inb(iobase + 128), iobase + 135);
  while(len > 0)
  {
    int status,b,i, timeout;
    timeout = 0x07FFFFFF;
    while(((status = STAT(iobase)) & 0x100)==0)
    {
      timeout--;
      if(status & 0x200 || !timeout)
      {
        printk("status = %p\n",status);
        outb(0, iobase + 135);
        return 1;
      }
    }
    if(len >= 128)
    {
      for(i=0; i<64; i++)
      {
        b = inw(iobase + 136);
        *addr++ = b;
        *addr++ = b>>8;
      }
      len -= 128;
    }
    else
    {
      b = inw(iobase + 136);
      *addr ++ = b;
      len -= 1;
      if(len)
        *addr ++ = b>>8;
      len -= 1;
    }
  }
  outb(0, iobase + 135);
  printk("first bytes = %02X %02X %02X %20X %02X %02X %02X\n",*start, start[1], start[2], start[3], start[4], start[5], start[6]);
  return 1;
}
#endif
#undef STAT

#include "../../scsi/NCR5380.c"

#ifdef MODULE

Scsi_Host_Template driver_template = ECOSCSI_NCR5380;

#include "../../scsi/scsi_module.c"
#endif
