/*
 * JFFS2 -- Journalling Flash File System, Version 2.
 *
 * Copyright (C) 2004 Ferenc Havasi <havasi@inf.u-szeged.hu>,
 *                    University of Szeged, Hungary
 *
 * For licensing information, see the file 'LICENCE' in this directory.
 *
 * $Id: proc.c,v 1.3 2004/06/24 09:51:38 havasi Exp $
 *
 * Files in /proc/fs/jffs2 directory:
 *   compr_list
 *         read:  shows the list of the loaded compressors 
 *                (name, priority, enadbled/disabled)
 *         write: compressors can be enabled/disabled and
 *                the priority of them can be changed,
 *                required formats:
 *                    enable COMPRESSOR_NAME
 *                    disble COMPRESSOR_NAME
 *                    priority NEW_PRIORITY COMPRESSOR_NAME
 *   compr_mode
 *         read:  shows the name of the actual compression mode
 *         write: sets the actual comperession mode
 *   compr_stat
 *         read:  shows compression statistics
 */

#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/jffs.h>
#include <linux/slab.h>
#include <linux/proc_fs.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/proc_fs.h>
#include "compr.h"

extern struct proc_dir_entry *jffs_proc_root;

/* Structure for top-level entry in '/proc/fs' directory */
static struct proc_dir_entry *jffs2_proc_root;

/* Structure for files in /proc/fs/jffs2 directory */
static struct proc_dir_entry *jffs2_proc_compr_stat;
static struct proc_dir_entry *jffs2_proc_compr_mode;

/* Read the JFFS2 'compr_stat' file */

static int jffs2_proc_stat_read (char *page, char **start, off_t off,
		int count, int *eof, void *data)
{
	int len = 0,i;
        char *stat = jffs2_stats();
        
        if (strlen(stat)<off) {
	        *eof = 1;
                kfree(stat);
	        return len;
        }        
        for (i=off;((stat[i]!=0)&&(len<count));i++,len++) {
                page[len]=stat[i];
        }
        if (off+len>=strlen(stat)) *eof = 1;
        else *eof = 0;
        kfree(stat);
	return len;
}


/* Read the JFFS2 'compr_mode' file */

static int jffs2_proc_mode_read (char *page, char **start, off_t off,
		int count, int *eof, void *data)
{
	int len = 0;
        if (strlen(jffs2_get_compression_mode_name())+1>count) {
                /* it should not happen */
	        *eof = 1;
                return 0;
        }
	len += sprintf(page, "%s\n",jffs2_get_compression_mode_name());
	*eof = 1;
	return len;
}

/* Write the JFFS2 'compr_mode' file
 *   sets the actual compression mode
 */

static int jffs2_proc_mode_write(struct file *file, const char *buffer,
                           unsigned long count, void *data)
{
        char *compr_name;

        /* collect the name of the compression mode and set it */
        compr_name = kmalloc(count+1,GFP_KERNEL);
        if (sscanf(buffer,"%s",compr_name)>0) {
                if (jffs2_set_compression_mode_name(compr_name)) {
                        printk(KERN_WARNING "JFFS2: error switching compression mode. Invalid parameter (%s)?\n",compr_name);
                }
        }
        else {
                printk(KERN_WARNING "JFFS2: error: parameter missing\n");
        }
        kfree(compr_name);
        return count;
}

/* Read the JFFS2 'compr_list' file */

static int jffs2_proc_list_read (char *page, char **start, off_t off,
		int count, int *eof, void *data)
{
	int len = 0;
        char *list = jffs2_list_compressors();
        if (strlen(list)+1>count) {
                /* it should not happen */
	        *eof = 1;
                kfree(list);
                return 0;
        }
	len += sprintf(page,"%s",list);
	*eof = 1;
        kfree(list);
	return len;
}

/* Write the JFFS2 'compr_list' file 
 *   enable/disable a compressor or set the priority of it
 */

static int jffs2_proc_list_write(struct file *file, const char *buffer,
                           unsigned long count, void *data)
{
        int prior;
        char *compr_name,*compr_cmd;

        compr_name = kmalloc(count+1,GFP_KERNEL);
        compr_cmd = kmalloc(count+1,GFP_KERNEL);
        if (!compr_name) {
                printk(KERN_WARNING "JFFS2: unable to allocate memory\n");
                goto list_write_end;
        }
        compr_name[0] = 0;

        if (sscanf(buffer,"priority %d %s",&prior,compr_name)>1) {
                jffs2_set_compressor_priority(compr_name, prior);
                goto list_write_end;
        }
        if (sscanf(buffer,"enable %s",compr_name)>0) {
                jffs2_enable_compressor_name(compr_name);
                goto list_write_end;
        }
        if (sscanf(buffer,"disable %s",compr_name)>0) {
                jffs2_disable_compressor_name(compr_name);
                goto list_write_end;
        }
        printk(KERN_WARNING "JFFS2: usage of /proc/fs/jffs2/compr_list:\n"
               "  echo \"enable COMPRESSOR_NAME\"  >/proc/fs/jffs2/compr_list\n"
               "  echo \"disable COMPRESSOR_NAME\" >/proc/fs/jffs2/compr_list\n"
               "  echo \"priority NEW_PRIORITY COMPRESSOR_NAME\" >/proc/fs/jffs2/compr_list\n");
list_write_end:
        kfree(compr_cmd);
        kfree(compr_name);
	return count;
}

/* Register a JFFS2 proc directory */

int jffs2_proc_init(void)
{
	jffs2_proc_root = proc_mkdir("jffs2", proc_root_fs);

	/* create entry for 'compr_stat' file */
	if ((jffs2_proc_compr_stat = create_proc_entry ("compr_stat", 0, jffs2_proc_root))) {
		jffs2_proc_compr_stat->read_proc = jffs2_proc_stat_read;
	}
	else {
		return -ENOMEM;
	}
	/* create entry for 'compr_mode' file */
	if ((jffs2_proc_compr_mode = create_proc_entry ("compr_mode", 0, jffs2_proc_root))) {
	        jffs2_proc_compr_mode->read_proc  = jffs2_proc_mode_read;
	        jffs2_proc_compr_mode->write_proc = jffs2_proc_mode_write;
	}
	else {
		return -ENOMEM;
	}
	/* create entry for 'compr_list' file */
	if ((jffs2_proc_compr_mode = create_proc_entry ("compr_list", 0, jffs2_proc_root))) {
	        jffs2_proc_compr_mode->read_proc  = jffs2_proc_list_read;
	        jffs2_proc_compr_mode->write_proc = jffs2_proc_list_write;
	}
	else {
		return -ENOMEM;
	}
	return 0;
}


/* Unregister a JFFS2 proc directory */

int jffs2_proc_exit(void)
{
#if LINUX_VERSION_CODE < 0x020300
	remove_proc_entry ("compr_stat", &jffs2_proc_root);
	remove_proc_entry ("compr_mode", &jffs2_proc_root);
	remove_proc_entry ("compr_list", &jffs2_proc_root);
	remove_proc_entry ("jffs2", &proc_root_fs);
#else
	remove_proc_entry ("compr_stat", jffs2_proc_root);
	remove_proc_entry ("compr_mode", jffs2_proc_root);
	remove_proc_entry ("compr_list", jffs2_proc_root);
	remove_proc_entry ("jffs2", proc_root_fs);
#endif
        return 0;
}
