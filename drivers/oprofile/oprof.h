/**
 * @file oprof.h
 *
 * @remark Copyright 2002 OProfile authors
 * @remark Read the file COPYING
 *
 * @author John Levon <levon@movementarian.org>
 */

#ifndef OPROF_H
#define OPROF_H

#include <linux/spinlock.h>
#include <linux/oprofile.h>
 
int oprofile_setup(void);
void oprofile_shutdown(void); 

int oprofilefs_register(void);
void oprofilefs_unregister(void);

int oprofile_start(void);
void oprofile_stop(void);

extern unsigned long fs_buffer_size;
extern unsigned long fs_cpu_buffer_size;
extern unsigned long fs_buffer_watershed;
extern enum oprofile_cpu oprofile_cpu_type;
extern struct oprofile_operations * oprofile_ops;
extern unsigned long oprofile_started;
 
void oprofile_create_files(struct super_block * sb, struct dentry * root);
 
#endif /* OPROF_H */
