/*
 * GZIP Compression functions for kernel crash dumps.
 *
 * Created by: Matt Robinson (yakker@sourceforge.net)
 * Copyright 2001 Matt D. Robinson.  All rights reserved.
 *
 * This code is released under version 2 of the GNU GPL.
 */

/* header files */
#include <linux/config.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/dump.h>
#include <linux/zlib.h>
#include <linux/vmalloc.h>

static void *deflate_workspace;
static unsigned long workspace_paddr[2];

/*
 * Name: dump_compress_gzip()
 * Func: Compress a DUMP_PAGE_SIZE page using gzip-style algorithms (the.
 *       deflate functions similar to what's used in PPP).
 */
static u16
dump_compress_gzip(const u8 *old, u16 oldsize, u8 *new, u16 newsize,
		unsigned long loc)
{
	/* error code and dump stream */
	int err;
	z_stream dump_stream;
	struct page *pg = (struct page *)loc;
	unsigned long paddr =  page_to_pfn(pg) << PAGE_SHIFT;

	dump_stream.workspace = deflate_workspace;
	if ((paddr == workspace_paddr[0]) || (paddr == workspace_paddr[1])) {
		/* 
		 * This page belongs to deflate_workspace used as temporary 
		 * buffer for compression. Hence, dump them without compression.
		 */
		return(0);
	}
	if ((err = zlib_deflateInit(&dump_stream, Z_BEST_COMPRESSION)) != Z_OK) {
		/* fall back to RLE compression */
		printk("dump_compress_gzip(): zlib_deflateInit() "
			"failed (%d)!\n", err);
		return 0;
	}

	/* use old (page of memory) and size (DUMP_PAGE_SIZE) as in-streams */
	dump_stream.next_in = (u8 *) old;
	dump_stream.avail_in = oldsize;

	/* out streams are new (dpcpage) and new size (DUMP_DPC_PAGE_SIZE) */
	dump_stream.next_out = new;
	dump_stream.avail_out = newsize;

	/* deflate the page -- check for error */
	err = zlib_deflate(&dump_stream, Z_FINISH);
	if (err != Z_STREAM_END) {
		/* zero is return code here */
		(void)zlib_deflateEnd(&dump_stream);
		printk("dump_compress_gzip(): zlib_deflate() failed (%d)!\n",
			err);
		return 0;
	}

	/* let's end the deflated compression stream */
	if ((err = zlib_deflateEnd(&dump_stream)) != Z_OK) {
		printk("dump_compress_gzip(): zlib_deflateEnd() "
			"failed (%d)!\n", err);
	}

	/* return the compressed byte total (if it's smaller) */
	if (dump_stream.total_out >= oldsize) {
		return oldsize;
	}
	return dump_stream.total_out;
}

/* setup the gzip compression functionality */
static struct __dump_compress dump_gzip_compression = {
	.compress_type = DUMP_COMPRESS_GZIP,
	.compress_func = dump_compress_gzip,
	.compress_name = "GZIP",
};

/*
 * Name: dump_compress_gzip_init()
 * Func: Initialize gzip as a compression mechanism.
 */
static int __init
dump_compress_gzip_init(void)
{
	struct page *pg;

	deflate_workspace = vmalloc(zlib_deflate_workspacesize());
	if (!deflate_workspace) {
		printk("dump_compress_gzip_init(): Failed to "
			"alloc %d bytes for deflate workspace\n",
			zlib_deflate_workspacesize());
		return -ENOMEM;
	}
	/*
	 * Need to find pages (workspace) that are used for compression.
	 * Even though zlib_deflate_workspacesize() is 64 pages (approximately)
	 * depends on the arch, we used only 2 pages. Hence, get the physical
	 * addresses for these 2 pages and used them to not to compress those
	 * pages.
	 */
	pg = vmalloc_to_page(deflate_workspace);
	workspace_paddr[0] = page_to_pfn(pg) << PAGE_SHIFT;
	pg = vmalloc_to_page(deflate_workspace + DUMP_PAGE_SIZE);
	workspace_paddr[1] = page_to_pfn(pg) << PAGE_SHIFT;
	dump_register_compression(&dump_gzip_compression);
	return 0;
}

/*
 * Name: dump_compress_gzip_cleanup()
 * Func: Remove gzip as a compression mechanism.
 */
static void __exit
dump_compress_gzip_cleanup(void)
{
	vfree(deflate_workspace);
	dump_unregister_compression(DUMP_COMPRESS_GZIP);
}

/* module initialization */
module_init(dump_compress_gzip_init);
module_exit(dump_compress_gzip_cleanup);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("LKCD Development Team <lkcd-devel@lists.sourceforge.net>");
MODULE_DESCRIPTION("Gzip compression module for crash dump driver");
