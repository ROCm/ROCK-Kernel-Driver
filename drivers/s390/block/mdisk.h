/*
 *  drivers/s390/block/mdisk.h
 *    VM minidisk device driver.
 *
 *  S390 version
 *    Copyright (C) 1999 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Hartmut Penner (hp@de.ibm.com)
 */

#include <linux/ioctl.h>
#include <linux/types.h>

#define MDISK_DEVS 8                   /* for disks                        */
#define MDISK_RAHEAD 8                 /* read ahead                       */
#define MDISK_BLKSIZE 1024             /* 1k blocks                        */
#define MDISK_HARDSECT 512             /* FIXME -- 512 byte blocks         */
#define MDISK_MAXSECTORS 256           /* max sectors for one request      */



/* 
 * low level io defines for diagnose 250
 */

#define MDISK_WRITE_REQ 0x01                                                 
#define MDISK_READ_REQ  0x02                                                   

#define MDISK_SYNC      0x00
#define MDISK_ASYNC     0x02
#define INIT_BIO        0x00
#define RW_BIO          0x01
#define TERM_BIO        0x02

/*
 * This stucture is used for clustered request
 * up to 256 different request can be handled with one invocation
 */

typedef struct {
        u8      type;
        u8      status;
        u16     spare1;
        u32     block_number;
        u32     alet;
        u32     buffer;
} mdisk_bio_t;

typedef struct {
        u16     dev_nr;
        u16     spare1[11];
        u32     block_size;
        u32     offset;
        u32     start_block;
        u32     end_block;
        u32     spare2[6];
} mdisk_init_io_t;
 
typedef struct {
        u16     dev_nr;
        u16     spare1[11];
        u8      key;
        u8      flags;
        u16     spare2;
        u32     block_count;
        u32     alet;
        u32     bio_list;
        u32     interrupt_params;
        u32     spare3[5];
} mdisk_rw_io_t;

/*
 * low level definitions for Diagnose 210
 */

#define DEV_CLASS_FBA   0x01

/*
 * Data structures for Diagnose 210
 */

typedef struct {
        u16     dev_nr;
        u16     rdc_len;
        u8      vdev_class;
        u8      vdev_type;
        u8      vdev_status;
        u8      vdev_flags;
        u8      rdev_class;
        u8      rdev_type;
        u8      rdev_model;
        u8      rdev_features;
} mdisk_dev_char_t;

                                                                               
