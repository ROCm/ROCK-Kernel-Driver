/*
 * RLE Compression functions for kernel crash dumps.
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
#include <linux/dump.h>

/*
 * Name: dump_compress_rle()
 * Func: Compress a DUMP_PAGE_SIZE (hardware) page down to something more
 *       reasonable, if possible.  This is the same routine we use in IRIX.
 */
static u16
dump_compress_rle(const u8 *old, u16 oldsize, u8 *new, u16 newsize,
		unsigned long loc)
{
	u16 ri, wi, count = 0;
	u_char value = 0, cur_byte;

	/*
	 * If the block should happen to "compress" to larger than the
	 * buffer size, allocate a larger one and change cur_buf_size.
	 */

	wi = ri = 0;

	while (ri < oldsize) {
		if (!ri) {
			cur_byte = value = old[ri];
			count = 0;
		} else {
			if (count == 255) {
				if (wi + 3 > oldsize) {
					return oldsize;
				}
				new[wi++] = 0;
				new[wi++] = count;
				new[wi++] = value;
				value = cur_byte = old[ri];
				count = 0;
			} else { 
				if ((cur_byte = old[ri]) == value) {
					count++;
				} else {
					if (count > 1) {
						if (wi + 3 > oldsize) {
							return oldsize;
						}
						new[wi++] = 0;
						new[wi++] = count;
						new[wi++] = value;
					} else if (count == 1) {
						if (value == 0) {
							if (wi + 3 > oldsize) {
								return oldsize;
							}
							new[wi++] = 0;
							new[wi++] = 1;
							new[wi++] = 0;
						} else {
							if (wi + 2 > oldsize) {
								return oldsize;
							}
							new[wi++] = value;
							new[wi++] = value;
						}
					} else { /* count == 0 */
						if (value == 0) {
							if (wi + 2 > oldsize) {
								return oldsize;
							}
							new[wi++] = value;
							new[wi++] = value;
						} else {
							if (wi + 1 > oldsize) {
								return oldsize;
							}
							new[wi++] = value;
						}
					} /* if count > 1 */

					value = cur_byte;
					count = 0;

				} /* if byte == value */

			} /* if count == 255 */

		} /* if ri == 0 */
		ri++;

	}
	if (count > 1) {
		if (wi + 3 > oldsize) {
			return oldsize;
		}
		new[wi++] = 0;
		new[wi++] = count;
		new[wi++] = value;
	} else if (count == 1) {
		if (value == 0) {
			if (wi + 3 > oldsize)
				return oldsize;
			new[wi++] = 0;
			new[wi++] = 1;
			new[wi++] = 0;
		} else {
			if (wi + 2 > oldsize)
				return oldsize;
			new[wi++] = value;
			new[wi++] = value;
		}
	} else { /* count == 0 */
		if (value == 0) {
			if (wi + 2 > oldsize)
				return oldsize;
			new[wi++] = value;
			new[wi++] = value;
		} else {
			if (wi + 1 > oldsize)
				return oldsize;
			new[wi++] = value;
		}
	} /* if count > 1 */

	value = cur_byte;
	count = 0;
	return wi;
}

/* setup the rle compression functionality */
static struct __dump_compress dump_rle_compression = {
	.compress_type = DUMP_COMPRESS_RLE,
	.compress_func = dump_compress_rle,
	.compress_name = "RLE",
};

/*
 * Name: dump_compress_rle_init()
 * Func: Initialize rle compression for dumping.
 */
static int __init
dump_compress_rle_init(void)
{
	dump_register_compression(&dump_rle_compression);
	return 0;
}

/*
 * Name: dump_compress_rle_cleanup()
 * Func: Remove rle compression for dumping.
 */
static void __exit
dump_compress_rle_cleanup(void)
{
	dump_unregister_compression(DUMP_COMPRESS_RLE);
}

/* module initialization */
module_init(dump_compress_rle_init);
module_exit(dump_compress_rle_cleanup);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("LKCD Development Team <lkcd-devel@lists.sourceforge.net>");
MODULE_DESCRIPTION("RLE compression module for crash dump driver");
