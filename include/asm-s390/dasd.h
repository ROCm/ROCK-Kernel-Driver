/* 
 * File...........: linux/drivers/s390/block/dasd.c
 * Author(s)......: Holger Smolinski <Holger.Smolinski@de.ibm.com>
 * Bugreports.to..: <Linux390@de.ibm.com>
 * (C) IBM Corporation, IBM Deutschland Entwicklung GmbH, 1999,2000
 *
 * This file is the interface of the DASD device driver, which is exported to user space
 * any future changes wrt the API will result in a change of the APIVERSION reported
 * to userspace by the DASDAPIVER-ioctl
 *
 * History of changes (starts July 2000)
 * 05/04/01 created by moving the kernel interface to drivers/s390/block/dasd_int.h
 */

#ifndef DASD_H
#define DASD_H
#include <linux/ioctl.h>

#define DASD_IOCTL_LETTER 'D'

#if (DASD_API_VERSION == 0)

#define DASD_PARTN_BITS 2

/* 
 * struct profile_info_t
 * holds the profinling information 
 */
typedef struct dasd_profile_info_t {
        unsigned int dasd_io_reqs;	 /* number of requests processed at all */
        unsigned int dasd_io_sects;	 /* number of sectors processed at all */
        unsigned int dasd_io_secs[32];	 /* histogram of request's sizes */
        unsigned int dasd_io_times[32];	 /* histogram of requests's times */
        unsigned int dasd_io_timps[32];	 /* histogram of requests's times per sector */
        unsigned int dasd_io_time1[32];	 /* histogram of time from build to start */
        unsigned int dasd_io_time2[32];	 /* histogram of time from start to irq */
        unsigned int dasd_io_time2ps[32]; /* histogram of time from start to irq */
        unsigned int dasd_io_time3[32];	 /* histogram of time from irq to end */
        unsigned int dasd_io_nr_req[32]; /* histogram of # of requests in chanq */
} dasd_profile_info_t;

/* 
 * struct format_data_t
 * represents all data necessary to format a dasd
 */
typedef struct format_data_t {
	int start_unit; /* from track */
	int stop_unit;  /* to track */
	int blksize;    /* sectorsize */
        int intensity;  
} format_data_t;

/*
 * values to be used for format_data_t.intensity
 * 0/8: normal format
 * 1/9: also write record zero
 * 3/11: also write home address
 * 4/12: invalidate track
 */
#define DASD_FMT_INT_FMT_R0 1 /* write record zero */
#define DASD_FMT_INT_FMT_HA 2 /* write home address, also set FMT_R0 ! */
#define DASD_FMT_INT_INVAL  4 /* invalidate tracks */
#define DASD_FMT_INT_COMPAT 8 /* use OS/390 compatible disk layout */

/* 
 * struct dasd_information_t
 * represents any data about the data, which is visible to userspace
 */
typedef struct dasd_information_t {
        unsigned int devno; /* S/390 devno */
        unsigned int real_devno; /* for aliases */
        unsigned int schid; /* S/390 subchannel identifier */
        unsigned int cu_type  : 16; /* from SenseID */
        unsigned int cu_model :  8; /* from SenseID */
        unsigned int dev_type : 16; /* from SenseID */
        unsigned int dev_model : 8; /* from SenseID */
        unsigned int open_count; 
        unsigned int req_queue_len; 
        unsigned int chanq_len;
        char type[4]; /* from discipline.name, 'none' for unknown */
        unsigned int status; /* current device level */
        unsigned int label_block; /* where to find the VOLSER */
        unsigned int FBA_layout; /* fixed block size (like AIXVOL) */
        unsigned int characteristics_size;
        unsigned int confdata_size;
        char characteristics[64]; /* from read_device_characteristics */
        char configuration_data[256]; /* from read_configuration_data */
} dasd_information_t;

/* Disable the volume (for Linux) */
#define BIODASDDISABLE _IO(DASD_IOCTL_LETTER,0) 
/* Enable the volume (for Linux) */
#define BIODASDENABLE  _IO(DASD_IOCTL_LETTER,1)  
/* Issue a reserve/release command, rsp. */
#define BIODASDRSRV    _IO(DASD_IOCTL_LETTER,2) /* reserve */
#define BIODASDRLSE    _IO(DASD_IOCTL_LETTER,3) /* release */
#define BIODASDSLCK    _IO(DASD_IOCTL_LETTER,4) /* steal lock */
/* reset profiling information of a device */
#define BIODASDPRRST   _IO(DASD_IOCTL_LETTER,5)
/* retrieve API version number */
#define DASDAPIVER     _IOR(DASD_IOCTL_LETTER,0,int)
/* Get information on a dasd device */
#define BIODASDINFO    _IOR(DASD_IOCTL_LETTER,1,dasd_information_t)
/* retrieve profiling information of a device */
#define BIODASDPRRD    _IOR(DASD_IOCTL_LETTER,2,dasd_profile_info_t)
/* #define BIODASDFORMAT  _IOW(IOCTL_LETTER,0,format_data_t) , deprecated */
#define BIODASDFMT     _IOW(DASD_IOCTL_LETTER,1,format_data_t) 
#endif /* DASD_API_VERSION */
#endif				/* DASD_H */

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-indent-level: 4 
 * c-brace-imaginary-offset: 0
 * c-brace-offset: -4
 * c-argdecl-indent: 4
 * c-label-offset: -4
 * c-continued-statement-offset: 4
 * c-continued-brace-offset: 0
 * indent-tabs-mode: nil
 * tab-width: 8
 * End:
 */
