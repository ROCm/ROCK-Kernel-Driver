/*
  The ts_kcompat module contains backported code from the Linux kernel
  licensed under the GNU General Public License (GPL) Version 2.  All
  code is copyrighted by its authors.

  $Id: kcompat_export.c 32 2004-04-09 03:57:42Z roland $
*/

#ifndef __KERNEL__
#  define __KERNEL__
#endif
#ifndef MODULE
#  define MODULE
#endif

#include "ts_kcompat.h"

#define __NO_VERSION__
#include <linux/module.h>

MODULE_LICENSE("GPL");

/* kcompat_io.c */
#if defined(TS_KCOMPAT_PROVIDE_VSNPRINTF)
EXPORT_SYMBOL(ts_snprintf);
EXPORT_SYMBOL(ts_vsnprintf);
#endif
#if defined(TS_KCOMPAT_PROVIDE_VSSCANF)
EXPORT_SYMBOL(ts_sscanf);
EXPORT_SYMBOL(ts_vsscanf);
#endif

/* kcompat_seq_file.c */
#if defined(TS_KCOMPAT_PROVIDE_SEQ_FILE)
EXPORT_SYMBOL(seq_open);
EXPORT_SYMBOL(seq_read);
EXPORT_SYMBOL(seq_lseek);
EXPORT_SYMBOL(seq_release);
EXPORT_SYMBOL(seq_escape);
EXPORT_SYMBOL(seq_printf);
#endif

/* kcompat_rbtree.c */
#if defined(TS_KCOMPAT_PROVIDE_RBTREE)
EXPORT_SYMBOL(rb_insert_color);
EXPORT_SYMBOL(rb_erase);
#endif
EXPORT_SYMBOL(rb_replace_node);
