/*
 * salinfo.c
 *
 * Creates entries in /proc/sal for various system features.
 *
 * Copyright (c) 2001 Silicon Graphics, Inc.  All rights reserved.
 *
 * 09/11/2003	jbarnes@sgi.com		updated for 2.6
 * 10/30/2001	jbarnes@sgi.com		copied much of Stephane's palinfo
 *					code to create this file
 */

#include <linux/types.h>
#include <linux/proc_fs.h>
#include <linux/module.h>

#include <asm/sal.h>

MODULE_AUTHOR("Jesse Barnes <jbarnes@sgi.com>");
MODULE_DESCRIPTION("/proc interface to IA-64 SAL features");
MODULE_LICENSE("GPL");

static int salinfo_read(char *page, char **start, off_t off, int count, int *eof, void *data);

typedef struct {
	const char		*name;		/* name of the proc entry */
	unsigned long           feature;        /* feature bit */
	struct proc_dir_entry	*entry;		/* registered entry (removal) */
} salinfo_entry_t;

/*
 * List {name,feature} pairs for every entry in /proc/sal/<feature>
 * that this module exports
 */
static salinfo_entry_t salinfo_entries[]={
	{ "bus_lock",           IA64_SAL_PLATFORM_FEATURE_BUS_LOCK, },
	{ "irq_redirection",	IA64_SAL_PLATFORM_FEATURE_IRQ_REDIR_HINT, },
	{ "ipi_redirection",	IA64_SAL_PLATFORM_FEATURE_IPI_REDIR_HINT, },
	{ "itc_drift",		IA64_SAL_PLATFORM_FEATURE_ITC_DRIFT, },
};

#define NR_SALINFO_ENTRIES ARRAY_SIZE(salinfo_entries)

/*
 * One for each feature and one more for the directory entry...
 */
static struct proc_dir_entry *salinfo_proc_entries[NR_SALINFO_ENTRIES + 1];

static int __init
salinfo_init(void)
{
	struct proc_dir_entry *salinfo_dir; /* /proc/sal dir entry */
	struct proc_dir_entry **sdir = salinfo_proc_entries; /* keeps track of every entry */
	int i;

	salinfo_dir = proc_mkdir("sal", NULL);

	for (i=0; i < NR_SALINFO_ENTRIES; i++) {
		/* pass the feature bit in question as misc data */
		*sdir = create_proc_read_entry (salinfo_entries[i].name, 0, salinfo_dir,
						  salinfo_read, (void *)salinfo_entries[i].feature);
		if (*sdir)
			(*sdir)->owner = THIS_MODULE;
		sdir++;
	}
	*sdir++ = salinfo_dir;

	return 0;
}

static void __exit
salinfo_exit(void)
{
	int i = 0;

	for (i = 0; i < NR_SALINFO_ENTRIES ; i++) {
		if (salinfo_proc_entries[i])
			remove_proc_entry (salinfo_proc_entries[i]->name, NULL);
	}
}

/*
 * 'data' contains an integer that corresponds to the feature we're
 * testing
 */
static int
salinfo_read(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	int len = 0;

	len = sprintf(page, (sal_platform_features & (unsigned long)data) ? "1\n" : "0\n");

	if (len <= off+count) *eof = 1;

	*start = page + off;
	len   -= off;

	if (len>count) len = count;
	if (len<0) len = 0;

	return len;
}

module_init(salinfo_init);
module_exit(salinfo_exit);
