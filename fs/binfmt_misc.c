/*
 *  binfmt_misc.c
 *
 *  Copyright (C) 1997 Richard Günther
 *
 *  binfmt_misc detects binaries via a magic or filename extension and invokes
 *  a specified wrapper. This should obsolete binfmt_java, binfmt_em86 and
 *  binfmt_mz.
 *
 *  1997-04-25 first version
 *  [...]
 *  1997-05-19 cleanup
 *  1997-06-26 hpa: pass the real filename rather than argv[0]
 *  1997-06-30 minor cleanup
 *  1997-08-09 removed extension stripping, locking cleanup
 */

#include <linux/config.h>
#include <linux/module.h>

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/malloc.h>
#include <linux/binfmts.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/file.h>
#include <linux/spinlock.h>
#include <asm/uaccess.h>

/*
 * We should make this work with a "stub-only" /proc,
 * which would just not be able to be configured.
 * Right now the /proc-fs support is too black and white,
 * though, so just remind people that this should be
 * fixed..
 */
#ifndef CONFIG_PROC_FS
#error You really need /proc support for binfmt_misc. Please reconfigure!
#endif

#define VERBOSE_STATUS /* undef this to save 400 bytes kernel memory */

struct binfmt_entry {
	struct binfmt_entry *next;
	long id;
	int flags;			/* type, status, etc. */
	int offset;			/* offset of magic */
	int size;			/* size of magic/mask */
	char *magic;			/* magic or filename extension */
	char *mask;			/* mask, NULL for exact match */
	char *interpreter;		/* filename of interpreter */
	char *proc_name;
	struct proc_dir_entry *proc_dir;
};

#define ENTRY_ENABLED 1		/* the old binfmt_entry.enabled */
#define	ENTRY_MAGIC 8		/* not filename detection */

static int load_misc_binary(struct linux_binprm *bprm, struct pt_regs *regs);
static void entry_proc_cleanup(struct binfmt_entry *e);
static int entry_proc_setup(struct binfmt_entry *e);

static struct linux_binfmt misc_format = {
	NULL, THIS_MODULE, load_misc_binary, NULL, NULL, 0
};

static struct proc_dir_entry *bm_dir;

static struct binfmt_entry *entries;
static int free_id = 1;
static int enabled = 1;

static rwlock_t entries_lock __attribute__((unused)) = RW_LOCK_UNLOCKED;


/*
 * Unregister one entry
 */
static void clear_entry(int id)
{
	struct binfmt_entry **ep, *e;

	write_lock(&entries_lock);
	ep = &entries;
	while (*ep && ((*ep)->id != id))
		ep = &((*ep)->next);
	if ((e = *ep))
		*ep = e->next;
	write_unlock(&entries_lock);

	if (e) {
		entry_proc_cleanup(e);
		kfree(e);
	}
}

/*
 * Clear all registered binary formats
 */
static void clear_entries(void)
{
	struct binfmt_entry *e, *n;

	write_lock(&entries_lock);
	n = entries;
	entries = NULL;
	write_unlock(&entries_lock);

	while ((e = n)) {
		n = e->next;
		entry_proc_cleanup(e);
		kfree(e);
	}
}

/*
 * Find entry through id and lock it
 */
static struct binfmt_entry *get_entry(int id)
{
	struct binfmt_entry *e;

	read_lock(&entries_lock);
	e = entries;
	while (e && (e->id != id))
		e = e->next;
	if (!e)
		read_unlock(&entries_lock);
	return e;
}

/*
 * unlock entry
 */
static inline void put_entry(struct binfmt_entry *e)
{
	if (e)
		read_unlock(&entries_lock);
}


/* 
 * Check if we support the binfmt
 * if we do, return the binfmt_entry, else NULL
 * locking is done in load_misc_binary
 */
static struct binfmt_entry *check_file(struct linux_binprm *bprm)
{
	struct binfmt_entry *e;
	char *p = strrchr(bprm->filename, '.');
	int j;

	e = entries;
	while (e) {
		if (e->flags & ENTRY_ENABLED) {
			if (!(e->flags & ENTRY_MAGIC)) {
				if (p && !strcmp(e->magic, p + 1))
					return e;
			} else {
				j = 0;
				while ((j < e->size) &&
				  !((bprm->buf[e->offset + j] ^ e->magic[j])
				   & (e->mask ? e->mask[j] : 0xff)))
					j++;
				if (j == e->size)
					return e;
			}
		}
		e = e->next;
	};
	return NULL;
}

/*
 * the loader itself
 */
static int load_misc_binary(struct linux_binprm *bprm, struct pt_regs *regs)
{
	struct binfmt_entry *fmt;
	struct file * file;
	char iname[BINPRM_BUF_SIZE];
	char *iname_addr = iname;
	int retval;

	retval = -ENOEXEC;
	if (!enabled)
		goto _ret;

	/* to keep locking time low, we copy the interpreter string */
	read_lock(&entries_lock);
	fmt = check_file(bprm);
	if (fmt) {
		strncpy(iname, fmt->interpreter, BINPRM_BUF_SIZE - 1);
		iname[BINPRM_BUF_SIZE - 1] = '\0';
	}
	read_unlock(&entries_lock);
	if (!fmt)
		goto _ret;

	allow_write_access(bprm->file);
	fput(bprm->file);
	bprm->file = NULL;

	/* Build args for interpreter */
	remove_arg_zero(bprm);
	retval = copy_strings_kernel(1, &bprm->filename, bprm);
	if (retval < 0) goto _ret; 
	bprm->argc++;
	retval = copy_strings_kernel(1, &iname_addr, bprm);
	if (retval < 0) goto _ret; 
	bprm->argc++;
	bprm->filename = iname;	/* for binfmt_script */

	file = open_exec(iname);
	retval = PTR_ERR(file);
	if (IS_ERR(file))
		goto _ret;
	bprm->file = file;

	retval = prepare_binprm(bprm);
	if (retval >= 0)
		retval = search_binary_handler(bprm, regs);
_ret:
	return retval;
}



/*
 * /proc handling routines
 */

/*
 * parses and copies one argument enclosed in del from *sp to *dp,
 * recognising the \x special.
 * returns pointer to the copied argument or NULL in case of an
 * error (and sets err) or null argument length.
 */
static char *copyarg(char **dp, const char **sp, int *count,
		     char del, int special, int *err)
{
	char c = 0, *res = *dp;

	while (!*err && ((c = *((*sp)++)), (*count)--) && (c != del)) {
		switch (c) {
		case '\\':
			if (special && (**sp == 'x')) {
				if (!isxdigit(c = toupper(*(++*sp))))
					*err = -EINVAL;
				**dp = (c - (isdigit(c) ? '0' : 'A' - 10)) * 16;
				if (!isxdigit(c = toupper(*(++*sp))))
					*err = -EINVAL;
				*((*dp)++) += c - (isdigit(c) ? '0' : 'A' - 10);
				++*sp;
				*count -= 3;
				break;
			}
		default:
			*((*dp)++) = c;
		}
	}
	if (*err || (c != del) || (res == *dp))
		res = NULL;
	else if (!special)
		*((*dp)++) = '\0';
	return res;
}

/*
 * This registers a new binary format, it recognises the syntax
 * ':name:type:offset:magic:mask:interpreter:'
 * where the ':' is the IFS, that can be chosen with the first char
 */
static int proc_write_register(struct file *file, const char *buffer,
			       unsigned long count, void *data)
{
	const char *sp;
	char del, *dp;
	struct binfmt_entry *e;
	int memsize, cnt = count - 1, err;

	/* some sanity checks */
	err = -EINVAL;
	if ((count < 11) || (count > 256))
		goto _err;

	err = -ENOMEM;
	memsize = sizeof(struct binfmt_entry) + count;
	if (!(e = (struct binfmt_entry *) kmalloc(memsize, GFP_USER)))
		goto _err;

	err = 0;
	sp = buffer + 1;
	del = buffer[0];
	dp = (char *)e + sizeof(struct binfmt_entry);

	e->proc_name = copyarg(&dp, &sp, &cnt, del, 0, &err);

	/* we can use bit 3 of type for ext/magic
	   flag due to the nice encoding of E and M */
	if ((*sp & ~('E' | 'M')) || (sp[1] != del))
		err = -EINVAL;
	else
		e->flags = (*sp++ & (ENTRY_MAGIC | ENTRY_ENABLED));
	cnt -= 2; sp++;

	e->offset = 0;
	while (cnt-- && isdigit(*sp))
		e->offset = e->offset * 10 + *sp++ - '0';
	if (*sp++ != del)
		err = -EINVAL;

	e->magic = copyarg(&dp, &sp, &cnt, del, (e->flags & ENTRY_MAGIC), &err);
	e->size = dp - e->magic;
	e->mask = copyarg(&dp, &sp, &cnt, del, 1, &err);
	if (e->mask && ((dp - e->mask) != e->size))
		err = -EINVAL;
	e->interpreter = copyarg(&dp, &sp, &cnt, del, 0, &err);
	e->id = free_id++;

	/* more sanity checks */
	if (err || !(!cnt || (!(--cnt) && (*sp == '\n'))) ||
	    (e->size < 1) || ((e->size + e->offset) > (BINPRM_BUF_SIZE - 1)) ||
	    !(e->proc_name) || !(e->interpreter) || entry_proc_setup(e))
		goto free_err;

	write_lock(&entries_lock);
	e->next = entries;
	entries = e;
	write_unlock(&entries_lock);

	err = count;
_err:
	return err;
free_err:
	kfree(e);
	err = -EINVAL;
	goto _err;
}

/*
 * Get status of entry/binfmt_misc
 * FIXME? should an entry be marked disabled if binfmt_misc is disabled though
 *        entry is enabled?
 */
static int proc_read_status(char *page, char **start, off_t off,
			    int count, int *eof, void *data)
{
	struct binfmt_entry *e;
	char *dp;
	int elen, i, err;

#ifndef VERBOSE_STATUS
	if (data) {
		if (!(e = get_entry((int) data))) {
			err = -ENOENT;
			goto _err;
		}
		i = e->flags & ENTRY_ENABLED;
		put_entry(e);
	} else {
		i = enabled;
	} 
	sprintf(page, "%s\n", (i ? "enabled" : "disabled"));
#else
	if (!data)
		sprintf(page, "%s\n", (enabled ? "enabled" : "disabled"));
	else {
		if (!(e = get_entry((long) data))) {
			err = -ENOENT;
			goto _err;
		}
		sprintf(page, "%s\ninterpreter %s\n",
		        (e->flags & ENTRY_ENABLED ? "enabled" : "disabled"),
			e->interpreter);
		dp = page + strlen(page);
		if (!(e->flags & ENTRY_MAGIC)) {
			sprintf(dp, "extension .%s\n", e->magic);
			dp = page + strlen(page);
		} else {
			sprintf(dp, "offset %i\nmagic ", e->offset);
			dp = page + strlen(page);
			for (i = 0; i < e->size; i++) {
				sprintf(dp, "%02x", 0xff & (int) (e->magic[i]));
				dp += 2;
			}
			if (e->mask) {
				sprintf(dp, "\nmask ");
				dp += 6;
				for (i = 0; i < e->size; i++) {
					sprintf(dp, "%02x", 0xff & (int) (e->mask[i]));
					dp += 2;
				}
			}
			*dp++ = '\n';
			*dp = '\0';
		}
		put_entry(e);
	}
#endif

	elen = strlen(page) - off;
	if (elen < 0)
		elen = 0;
	*eof = (elen <= count) ? 1 : 0;
	*start = page + off;
	err = elen;

_err:
	return err;
}

/*
 * Set status of entry/binfmt_misc:
 * '1' enables, '0' disables and '-1' clears entry/binfmt_misc
 */
static int proc_write_status(struct file *file, const char *buffer,
			     unsigned long count, void *data)
{
	struct binfmt_entry *e;
	int res = count;

	if (buffer[count-1] == '\n')
		count--;
	if ((count == 1) && !(buffer[0] & ~('0' | '1'))) {
		if (data) {
			if ((e = get_entry((long) data)))
				e->flags = (e->flags & ~ENTRY_ENABLED)
					    | (int)(buffer[0] - '0');
			put_entry(e);
		} else {
			enabled = buffer[0] - '0';
		}
	} else if ((count == 2) && (buffer[0] == '-') && (buffer[1] == '1')) {
		if (data)
			clear_entry((long) data);
		else
			clear_entries();
	} else {
		res = -EINVAL;
	}
	return res;
}

/*
 * Remove the /proc-dir entries of one binfmt
 */
static void entry_proc_cleanup(struct binfmt_entry *e)
{
	remove_proc_entry(e->proc_name, bm_dir);
}

/*
 * Create the /proc-dir entry for binfmt
 */
static int entry_proc_setup(struct binfmt_entry *e)
{
	if (!(e->proc_dir = create_proc_entry(e->proc_name,
			 	S_IFREG | S_IRUGO | S_IWUSR, bm_dir)))
	{
		printk(KERN_WARNING "Unable to create /proc entry.\n");
		return -ENOENT;
	}
	e->proc_dir->data = (void *) (e->id);
	e->proc_dir->read_proc = proc_read_status;
	e->proc_dir->write_proc = proc_write_status;
	return 0;
}

static int __init init_misc_binfmt(void)
{
	int error = -ENOENT;
	struct proc_dir_entry *status = NULL, *reg;

	bm_dir = proc_mkdir("sys/fs/binfmt_misc", NULL); /* WTF??? */
	if (!bm_dir)
		goto out;
	bm_dir->owner = THIS_MODULE;

	status = create_proc_entry("status", S_IFREG | S_IRUGO | S_IWUSR,
					bm_dir);
	if (!status)
		goto cleanup_bm;
	status->read_proc = proc_read_status;
	status->write_proc = proc_write_status;

	reg = create_proc_entry("register", S_IFREG | S_IWUSR, bm_dir);
	if (!reg)
		goto cleanup_status;
	reg->write_proc = proc_write_register;

	error = register_binfmt(&misc_format);
out:
	return error;

cleanup_status:
	remove_proc_entry("status", bm_dir);
cleanup_bm:
	remove_proc_entry("sys/fs/binfmt_misc", NULL);
	goto out;
}

static void __exit exit_misc_binfmt(void)
{
	unregister_binfmt(&misc_format);
	remove_proc_entry("register", bm_dir);
	remove_proc_entry("status", bm_dir);
	clear_entries();
	remove_proc_entry("sys/fs/binfmt_misc", NULL);
}

EXPORT_NO_SYMBOLS;

module_init(init_misc_binfmt);
module_exit(exit_misc_binfmt);
