/*
 * linux/fs/zlib/inflate_syms.c
 *
 * Exported symbols for the inflate functionality.
 *
 */

#include <linux/module.h>
#include <linux/init.h>

#include <linux/zlib_fs.h>

EXPORT_SYMBOL(zlib_fs_inflate_workspacesize);
EXPORT_SYMBOL(zlib_fs_inflate);
EXPORT_SYMBOL(zlib_fs_inflateInit_);
EXPORT_SYMBOL(zlib_fs_inflateInit2_);
EXPORT_SYMBOL(zlib_fs_inflateEnd);
EXPORT_SYMBOL(zlib_fs_inflateSync);
EXPORT_SYMBOL(zlib_fs_inflateReset);
EXPORT_SYMBOL(zlib_fs_adler32);
EXPORT_SYMBOL(zlib_fs_inflateSyncPoint);
MODULE_LICENSE("GPL");
