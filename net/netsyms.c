/*
 *  linux/net/netsyms.c
 *
 *  Symbol table for the linux networking subsystem. Moved here to
 *  make life simpler in ksyms.c.
 */

#include <linux/config.h>
#include <linux/fs.h>
#include <linux/module.h>

/* Needed by unix.o */
EXPORT_SYMBOL(files_stat);
