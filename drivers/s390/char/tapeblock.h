
/***************************************************************************
 *
 *  drivers/s390/char/tapeblock.h
 *    character device frontend for tape device driver
 *
 *  S390 and zSeries version
 *    Copyright (C) 2001 IBM Corporation
 *    Author(s): Carsten Otte <cotte@de.ibm.com>
 *               Tuan Ngo-Anh <ngoanh@de.ibm.com>
 *
 *
 ****************************************************************************
 */

#ifndef TAPEBLOCK_H
#define TAPEBLOCK_H
#include <linux/config.h>

#define TAPEBLOCK_READAHEAD 30
#define TAPEBLOCK_MAJOR 0

#define TAPEBLOCK_DEVFSMODE 0060644 // blkdev, rwx for user, rw for group&others

int tapeblock_open(struct inode *, struct file *);
int tapeblock_release(struct inode *, struct file *);
void tapeblock_setup(tape_dev_t* td);
void tapeblock_schedule_exec_io (tape_dev_t *td);
int tapeblock_mediumdetect(tape_dev_t* td);
#ifdef CONFIG_DEVFS_FS
devfs_handle_t tapeblock_mkdevfstree (tape_dev_t* td);
void tapeblock_rmdevfstree (tape_dev_t* td);
#endif
int tapeblock_init (void);
void tapeblock_uninit (void);
#endif
