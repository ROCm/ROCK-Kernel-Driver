/* Copyright 2001, 2002, 2003 by Hans Reiser, licensing governed by
 * reiser4/README */

/* Tree-tracing facility. Copied from reiserfs v3.x patch, never released */

/*
 * Tree-tracing is enabled by REISER4_EVENT_LOG compile option, and
 * log_file=<path> mount option.
 *
 * File at <path> is opened (created if needed) and filled with log records
 * while file system is mounted.
 *
 * Special path /dev/null disables logging.
 *
 *
 * Special path /dev/console is interpreted as outputting log records through
 * printk().
 *
 * Low-level functions to output log record are write_log() and
 * write_log_raw(). Various macros defined in log.h are used as wrappers.
 *
 * Output to log file is buffered to reduce overhead, but as target file
 * system (one where log file lives) also buffers data in memory, tracing
 * can distort file system behavior significantly. It has been experimentally
 * found that optimal was to log is by using log_file=<pipe> and piping
 * log records to another host (through netcat(1) or similar, for example).
 *
 */

#include "forward.h"
#include "debug.h"
#include "key.h"
#include "log.h"
#include "super.h"
#include "inode.h"
#include "page_cache.h" /* for jprivate() */

#include <asm/uaccess.h>
#include <linux/types.h>
#include <linux/fs.h>		/* for struct super_block  */
#include <linux/slab.h>
#include <linux/bio.h>
#include <linux/vmalloc.h>

#if REISER4_LOG

static int log_flush(reiser4_log_file * log);
static int free_space(reiser4_log_file * log, size_t * len);
static int lock_log(reiser4_log_file * log);
static void unlock_log(reiser4_log_file * log);

/* helper macro: lock log file, return with error if locking failed. */
#define LOCK_OR_FAIL( log )			\
({						\
	int __result;				\
						\
	__result = lock_log( log );		\
	if( __result != 0 )			\
		return __result;		\
})

/* open log file. This is called by mount, when log_file=<path> option is
 * used. */
int
open_log_file(struct super_block *super,
		const char *file_name,
		size_t size,
		reiser4_log_file * log)
{
	int gfp_mask;

	assert("nikita-2498", file_name != NULL);
	assert("nikita-2499", log != NULL);
	assert("nikita-2500", size > 0);

	xmemset(log, 0, sizeof *log);

	spin_lock_init(&log->lock);
	INIT_LIST_HEAD(&log->wait);

	/* special case: disable logging */
	if (!strcmp(file_name, "/dev/null")) {
		log->type = log_to_bucket;
		return 0;
	}
	log->buf = vmalloc(size);
	if (log->buf == NULL)
		return RETERR(-ENOMEM);
	log->size = size;

	/* special case: log through printk() */
	if (!strcmp(file_name, "/dev/console")) {
		log->type = log_to_console;
		return 0;
	}
	log->fd = filp_open(file_name, O_CREAT | O_WRONLY, S_IFREG | S_IWUSR);
	if (IS_ERR(log->fd)) {
		warning("nikita-2501", "cannot open log file '%s': %li", file_name, PTR_ERR(log->fd));
		log->fd = NULL;
		return PTR_ERR(log->fd);
	}
	if (log->fd->f_dentry->d_inode->i_sb == super) {
		warning("nikita-2506", "Refusing to log onto logd fs");
		return RETERR(-EINVAL);
	}
	log->fd->f_dentry->d_inode->i_flags |= S_NOATIME;
	log->fd->f_flags |= O_APPEND;

	/* avoid complications with calling memory allocator by ->write()
	 * method of target file system, but setting GFP_NOFS bit in
	 * mapping->gfp_mask */
	gfp_mask = mapping_gfp_mask(log->fd->f_dentry->d_inode->i_mapping);
	gfp_mask &= ~__GFP_FS;
	gfp_mask |= GFP_NOFS;
	mapping_set_gfp_mask(log->fd->f_dentry->d_inode->i_mapping, gfp_mask);
	log->type = log_to_file;
	return 0;
}

/* write message (formatted according to @format) into log file @file */
int
write_log(reiser4_log_file * file, const char *format, ...)
{
	size_t len;
	int result;
	va_list args;

	if (file == NULL || file->type == log_to_bucket ||
	    file->buf == NULL || file->disabled > 0)
		return 0;

	va_start(args, format);
	len = vsnprintf((char *) format, 0, format, args) + 1;
	va_end(args);

	LOCK_OR_FAIL(file);
	result = free_space(file, &len);
	if (result == 0) {
		va_start(args, format);
		file->used += vsnprintf(file->buf + file->used,
					file->size - file->used, format, args);
		va_end(args);
	}
	unlock_log(file);
	return result;
}

/* write buffer @data into @file */
int
write_log_raw(reiser4_log_file * file, const void *data, size_t len)
{
	int result;

	if (file == NULL || file->type == log_to_bucket ||
	    file->buf == NULL || file->disabled > 0)
		return 0;

	LOCK_OR_FAIL(file);
	result = free_space(file, &len);
	if (result == 0) {
		xmemcpy(file->buf + file->used, data, (size_t) len);
		file->used += len;
	}
	unlock_log(file);
	return result;
}

/* close log file. This is called by umount. */
void
close_log_file(reiser4_log_file * log)
{
	if (log->type == log_to_file && lock_log(log) == 0) {
		log_flush(log);
		unlock_log(log);
	}
	if (log->fd != NULL)
		filp_close(log->fd, NULL);
	if (log->buf != NULL) {
		vfree(log->buf);
		log->buf = NULL;
	}
}

/* temporary suspend (or resume) tracing */
int
hold_log(reiser4_log_file * file, int flag)
{
	if (flag)
		return lock_log(file);
	else {
		unlock_log(file);
		return 0;
	}
}

/* disable or enable tracing */
int
disable_log(reiser4_log_file * file, int flag)
{
	LOCK_OR_FAIL(file);
	file->disabled += flag ? +1 : -1;
	unlock_log(file);
	return 0;
}

#define START_KERNEL_IO				\
        {					\
		mm_segment_t __ski_old_fs;	\
						\
		__ski_old_fs = get_fs();	\
		set_fs( KERNEL_DS )

#define END_KERNEL_IO				\
		set_fs( __ski_old_fs );		\
	}

struct __wlink {
	struct list_head link;
	struct semaphore sema;
};

/* lock log file for exclusive use */
static int
lock_log(reiser4_log_file * log)
{
	int ret = 0;

	spin_lock(&log->lock);

	while (log->long_term) {
		/* sleep on a semaphore */
		struct __wlink link;
		sema_init(&link.sema, 0);
		list_add(&link.link, &log->wait);
		spin_unlock(&log->lock);

		ret = down_interruptible(&link.sema);

		spin_lock(&log->lock);
		list_del(&link.link);
	}

	return ret;
}

/* unlock log file */
static void
unlock_log(reiser4_log_file * log)
{
	spin_unlock(&log->lock);
}

static void convert_to_longterm (reiser4_log_file * log)
{
	assert ("zam-833", log->long_term == 0);
	log->long_term = 1;
	spin_unlock(&log->lock);
}

static void convert_to_shortterm (reiser4_log_file * log)
{
	struct list_head * pos;

	spin_lock(&log->lock);
	assert ("zam-834", log->long_term);
	log->long_term = 0;
	list_for_each(pos, &log->wait) {
		struct __wlink * link;
		link = list_entry(pos, struct __wlink, link);
		up(&link->sema);
	}
}

/*
 * flush content of the file->buf to the logging target. Free space in buffer.
 */
static int
log_flush(reiser4_log_file * file)
{
	int result;

	result = 0;
	switch (file->type) {
	case log_to_file:{
		struct file *fd;

		convert_to_longterm(file);

		/*
		 * if logging to the file, call vfs_write() until all data are
		 * written
		 */

		fd = file->fd;
		if (fd && fd->f_op != NULL && fd->f_op->write != NULL) {
			int written;

			written = 0;
			START_KERNEL_IO;
			while (file->used > 0) {
				result = vfs_write(fd, file->buf + written,
						   file->used, &fd->f_pos);
				if (result > 0) {
					file->used -= result;
					written += result;
				} else {
					static int log_io_failed = 0;

					if (IS_POW(log_io_failed))
						warning("nikita-2502",
							"Error writing log: %i",
							result);
					++ log_io_failed;
					break;
				}
			}
			END_KERNEL_IO;
		} else {
			warning("nikita-2504", "no ->write() in log-file");
			result = RETERR(-EINVAL);
		}

		convert_to_shortterm(file);

		break;
	}
	default:
		warning("nikita-2505",
			"unknown log-file type: %i. Dumping to console",
			file->type);
	case log_to_console:
		if (file->buf != NULL)
			printk(file->buf);
	case log_to_bucket:
		file->used = 0;
		break;
	}

	return result;
}

/*
 * free *@len bytes in the file->buf
 */
static int
free_space(reiser4_log_file * file, size_t * len)
{
	if (*len > file->size) {
		warning("nikita-2503",
			"log record too large: %i > %i. Truncating",
			*len, file->size);
		*len = file->size;
	}
	while (*len > file->size - file->used) {
		int result;

		/* flushing can sleep, so loop */
		result = log_flush(file);
		if (result < 0)
			return result;
	}
	return 0;
}

/*
 * log tree operation @op on the @tree.
 */
void
write_tree_log(reiser4_tree * tree, reiser4_log_op op, ...)
{
	va_list args;
	char buf[200];
	char *rest;
	reiser4_key *key;

	if (unlikely(in_interrupt() || in_irq())) {
		printk("cannot write log from interrupt\n");
		return;
	}

	/*
	 * For each operation arguments are provided by the caller. Number and
	 * type of arguments depends on operation type. Use va_args to extract
	 * them.
	 */

	/*
	 * tree_cut:    opcode, key_from, key_to
	 *
	 * tree_lookup: opcode, key
	 *
	 * tree_insert: opcode, item_data, coord, flags
	 *
	 * tree_paste:  opcode, item_data, coord, flags
	 *
	 * tree_cached: opcode
	 *
	 * tree_exit:   opcode
	 *
	 */
	va_start(args, op);

	rest = buf;
	rest += sprintf(rest, "....tree %c ", op);

	if (op != tree_cached && op != tree_exit) {
		key = va_arg(args, reiser4_key *);
		rest += sprintf_key(rest, key);
		*rest++ = ' ';
		*rest = '\0';

		switch (op) {
		case tree_cut: {
			reiser4_key *to;

			to = va_arg(args, reiser4_key *);
			rest += sprintf_key(rest, to);
			break;
		}
		case tree_lookup:
		default:
			break;
		case tree_insert:
		case tree_paste: {
			reiser4_item_data *data;
			coord_t *coord;
			__u32 flags;

			data = va_arg(args, reiser4_item_data *);
			coord = va_arg(args, coord_t *);
			flags = va_arg(args, __u32);

			rest += sprintf(rest, "%s (%u,%u) %x",
					data->iplug->h.label,
					coord->item_pos, coord->unit_pos, flags);
		}
		}
	}
	va_end(args);
	write_current_logf(WRITE_TREE_LOG, "%s", buf);
}

/* construct in @buf jnode description to be output in the log */
char *
jnode_short_info(const jnode *j, char *buf)
{
	if (j == NULL) {
		sprintf(buf, "null");
	} else {
		sprintf(buf, "%i %c %c %i",
			jnode_get_level(j),
			jnode_is_znode(j) ? 'Z' :
			jnode_is_unformatted(j) ? 'J' : '?',
			JF_ISSET(j, JNODE_OVRWR) ? 'O' :
			JF_ISSET(j, JNODE_RELOC) ? 'R' : ' ',
			j->atom ? j->atom->atom_id : -1);
	}
	return buf;
}


/* write jnode description in the log */
void
write_node_log(const jnode *node)
{
	char jbuf[100];

	jnode_short_info(node, jbuf);
	write_current_logf(WRITE_NODE_LOG, ".....node %s %s",
			   sprint_address(jnode_get_block(node)), jbuf);
}

/* write page description in the log */
void
write_page_log(const struct address_space *mapping, unsigned long index)
{
	write_current_logf(WRITE_PAGE_LOG, ".....page %llu %lu", get_inode_oid(mapping->host),
			   index);
}

/* write block IO request description in the log */
void
write_io_log(const char *moniker, int rw, struct bio *bio)
{
	struct super_block *super;
	reiser4_super_info_data *sbinfo;
	reiser4_block_nr start;
	char jbuf[100];

	/*
	 * sbinfo->last_touched is last block where IO was issued to. It is
	 * used to output seek distance into log.
	 */

	super = reiser4_get_current_sb();
	sbinfo = get_super_private(super);

	start = bio->bi_sector >> (super->s_blocksize_bits - 9);
	jnode_short_info(jprivate(bio->bi_io_vec[0].bv_page), jbuf);
	write_current_logf(WRITE_IO_LOG, "......bio %s %c %+lli  (%llu,%u) %s",
			   moniker, (rw == READ) ? 'r' : 'w',
			   start - sbinfo->last_touched - 1,
			   start, bio->bi_vcnt, jbuf);
	sbinfo->last_touched = start + bio->bi_vcnt - 1;
}

#endif

/* Make Linus happy.
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   scroll-step: 1
   End:
*/
