
/***************************************************************************
 *
 *  drivers/s390/char/tapechar.h
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

#ifndef TAPECHAR_H
#define TAPECHAR_H
#include <linux/config.h>
#define TAPECHAR_DEVFSMODE 0020644 // chardev, rwx for user, rw for group&others
#define TAPECHAR_MAJOR 0  /* get dynamic major since no major officialy defined for tape */
#define TAPECHAR_NOREW_MINOR(x) x    /* Minor for nonrewinding device */
#define TAPECHAR_REW_MINOR(x)  (x+1) /* Minor for rewinding device */

/*
 * Prototypes
 */

ssize_t tapechar_read(struct file *, char *, size_t, loff_t *);
ssize_t tapechar_write(struct file *, const char *, size_t, loff_t *);
int tapechar_ioctl(struct inode *,struct file *,unsigned int,unsigned long);
int tapechar_open (struct inode *,struct file *);
int tapechar_release (struct inode *,struct file *);
#ifdef CONFIG_DEVFS_FS
devfs_handle_t tapechar_mkdevfstree (tape_dev_t* td);
void tapechar_rmdevfstree (tape_dev_t* td);
#endif
void tapechar_init (void);
void tapechar_uninit (void);

#endif /* TAPECHAR_H */
