/*
 * DTC controller, taken from T128 driver by...
 * Copyright 1993, Drew Eckhardt
 *	Visionary Computing
 *	(Unix and Linux consulting and custom programming)
 *	drew@colorado.edu
 *      +1 (303) 440-4894
 *
 * DISTRIBUTION RELEASE 2. 
 *
 * For more information, please consult 
 *
 * 
 * 
 * and 
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

#ifndef DTC3280_H
#define DTC3280_H

static int dtc_abort(Scsi_Cmnd *);
static int dtc_biosparam(struct scsi_device *, struct block_device *,
		         sector_t, int*);
static int dtc_detect(Scsi_Host_Template *);
static int dtc_queue_command(Scsi_Cmnd *, void (*done)(Scsi_Cmnd *));
static int dtc_bus_reset(Scsi_Cmnd *);
static int dtc_device_reset(Scsi_Cmnd *);
static int dtc_host_reset(Scsi_Cmnd *);

#ifndef CMD_PER_LUN
#define CMD_PER_LUN 2
#endif

#ifndef CAN_QUEUE
#define CAN_QUEUE 32 
#endif

#define NCR5380_implementation_fields \
    unsigned int base

#define NCR5380_local_declare() \
    unsigned int base

#define NCR5380_setup(instance) \
    base = (unsigned int)(instance)->base

#define DTC_address(reg) (base + DTC_5380_OFFSET + reg)

#define dbNCR5380_read(reg)                                              \
    (rval=isa_readb(DTC_address(reg)), \
     (((unsigned char) printk("DTC : read register %d at addr %08x is: %02x\n"\
    , (reg), (int)DTC_address(reg), rval)), rval ) )

#define dbNCR5380_write(reg, value) do {                                  \
    printk("DTC : write %02x to register %d at address %08x\n",         \
            (value), (reg), (int)DTC_address(reg));     \
    isa_writeb(value, DTC_address(reg));} while(0)


#if !(DTCDEBUG & DTCDEBUG_TRANSFER) 
#define NCR5380_read(reg) (isa_readb(DTC_address(reg)))
#define NCR5380_write(reg, value) (isa_writeb(value, DTC_address(reg)))
#else
#define NCR5380_read(reg) (isa_readb(DTC_address(reg)))
#define xNCR5380_read(reg)						\
    (((unsigned char) printk("DTC : read register %d at address %08x\n"\
    , (reg), DTC_address(reg))), isa_readb(DTC_address(reg)))

#define NCR5380_write(reg, value) do {					\
    printk("DTC : write %02x to register %d at address %08x\n", 	\
	    (value), (reg), (int)DTC_address(reg));	\
    isa_writeb(value, DTC_address(reg));} while(0)
#endif

#define NCR5380_intr			dtc_intr
#define NCR5380_queue_command		dtc_queue_command
#define NCR5380_abort			dtc_abort
#define NCR5380_bus_reset		dtc_bus_reset
#define NCR5380_device_reset		dtc_device_reset
#define NCR5380_host_reset		dtc_host_reset
#define NCR5380_proc_info		dtc_proc_info 

/* 15 12 11 10
   1001 1100 0000 0000 */

#define DTC_IRQS 0x9c00


#endif /* DTC3280_H */
