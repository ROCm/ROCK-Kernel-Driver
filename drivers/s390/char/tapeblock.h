
/***************************************************************************
 *
 *  drivers/s390/char/tapechar.h
 *    character device frontend for tape device driver
 *
 *  S390 version
 *    Copyright (C) 2000 IBM Corporation
 *    Author(s): Tuan Ngo-Anh <ngoanh@de.ibm.com>
 *
 *  UNDER CONSTRUCTION: Work in progress...:-)
 ****************************************************************************
 */

#ifndef TAPEBLOCK_H
#define TAPEBLOCK_H
#define PARTN_BITS 0

#define TAPEBLOCK_READAHEAD 30
#define TAPEBLOCK_MAJOR 0
static int tapeblock_open(struct inode *, struct file *);
static int tapeblock_release(struct inode *, struct file *);
void tapeblock_setup(tape_info_t* tape);
void schedule_tapeblock_exec_IO (tape_info_t *tape);
static int tapeblock_mediumdetect(tape_info_t* tape);
#endif
