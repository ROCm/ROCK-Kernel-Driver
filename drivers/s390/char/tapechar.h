
/***************************************************************************
 *
 *  drivers/s390/char/tapechar.h
 *    character device frontend for tape device driver
 *
 *  S390 version
 *    Copyright (C) 2000 IBM Corporation
 *    Author(s): Tuan Ngo-Anh <ngoanh@de.ibm.com>
 *               Carsten Otte <cotte@de.ibm.com>
 *
 *  UNDER CONSTRUCTION: Work in progress...:-)
 ****************************************************************************
 */

#ifndef TAPECHAR_H
#define TAPECHAR_H
#define  TAPE_MAJOR                    0        /* get dynamic major since no major officialy defined for tape */
/*
 * Prototypes for tape_fops
 */
static ssize_t tape_read(struct file *, char *, size_t, loff_t *);
static ssize_t tape_write(struct file *, const char *, size_t, loff_t *);
static int tape_ioctl(struct inode *,struct file *,unsigned int,unsigned long);
static int tape_open (struct inode *,struct file *);
static int tape_release (struct inode *,struct file *);
#endif /* TAPECHAR_H */
