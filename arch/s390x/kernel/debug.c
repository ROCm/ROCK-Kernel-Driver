/*
 *  arch/s390/kernel/debug.c
 *   S/390 debug facility
 *
 *    Copyright (C) 1999, 2000 IBM Deutschland Entwicklung GmbH,
 *                             IBM Corporation
 *    Author(s): Michael Holzheu (holzheu@de.ibm.com),
 *               Holger Smolinski (Holger.Smolinski@de.ibm.com)
 *
 *    Bugreports to: <Linux390@de.ibm.com>
 */

#include <linux/config.h>
#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/malloc.h>
#include <linux/vmalloc.h>
#include <linux/ctype.h>
#include <linux/version.h>
#include <asm/uaccess.h>
#include <asm/semaphore.h>

#ifdef MODULE
#include <linux/module.h>
#endif

#include <asm/debug.h>

#define MIN(a,b) (((a)<(b))?(a):(b))

#if defined(CONFIG_ARCH_S390X)
#define DEBUG_PROC_HEADER_SIZE 46
#else
#define DEBUG_PROC_HEADER_SIZE 38
#endif

#define ADD_BUFFER 1000

/* typedefs */

typedef struct file_private_info {
	loff_t len;			/* length of output in byte */
	int size;			/* size of buffer for output */
	char *data;			/* buffer for output */
	debug_info_t *debug_info;	/* the debug information struct */
	struct debug_view *view;	/* used view of debug info */
} file_private_info_t;

extern void tod_to_timeval(uint64_t todval, struct timeval *xtime);

/* internal function prototyes */

static int debug_init(void);
static int debug_format_output(debug_info_t * debug_area, char *buf,
			       int size, struct debug_view *view);
static ssize_t debug_output(struct file *file, char *user_buf,
			    size_t user_len, loff_t * offset);
static ssize_t debug_input(struct file *file, const char *user_buf,
			   size_t user_len, loff_t * offset);
static int debug_open(struct inode *inode, struct file *file);
static int debug_close(struct inode *inode, struct file *file);
static struct proc_dir_entry 
*debug_create_proc_dir_entry(struct proc_dir_entry *root,
			     const char *name, mode_t mode,
			     struct inode_operations *iops,
			     struct file_operations *fops);
static void debug_delete_proc_dir_entry(struct proc_dir_entry *root,
					struct proc_dir_entry *entry);
static debug_info_t*  debug_info_create(char *name, int page_order, int nr_areas, int buf_size);
static void debug_info_get(debug_info_t *);
static void debug_info_put(debug_info_t *);
static int debug_prolog_level_fn(debug_info_t * id,
				 struct debug_view *view, char *out_buf);
static int debug_input_level_fn(debug_info_t * id, struct debug_view *view,
				struct file *file, const char *user_buf,
				size_t user_buf_size, loff_t * offset);
static int debug_hex_ascii_format_fn(debug_info_t * id, struct debug_view *view,
                                char *out_buf, const char *in_buf);
static int debug_raw_format_fn(debug_info_t * id,
				 struct debug_view *view, char *out_buf,
				 const char *in_buf);
static int debug_raw_header_fn(debug_info_t * id, struct debug_view *view,
                         int area, debug_entry_t * entry, char *out_buf);

/* globals */

struct debug_view debug_raw_view = {
	"raw",
	NULL,
	&debug_raw_header_fn,
	&debug_raw_format_fn,
	NULL
};

struct debug_view debug_hex_ascii_view = {
	"hex_ascii",
	NULL,
	&debug_dflt_header_fn,
	&debug_hex_ascii_format_fn,
	NULL
};

struct debug_view debug_level_view = {
	"level",
	&debug_prolog_level_fn,
	NULL,
	NULL,
	&debug_input_level_fn
};

/* static globals */

static debug_info_t *debug_area_first = NULL;
static debug_info_t *debug_area_last = NULL;
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,3,98))
static struct semaphore debug_lock = MUTEX;
#else
DECLARE_MUTEX(debug_lock);
#endif

static int initialized = 0;

static struct file_operations debug_file_ops = {
	read:    debug_output,
	write:   debug_input,	
	open:    debug_open,
	release: debug_close,
};

static struct inode_operations debug_inode_ops = {
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,3,98))
	default_file_ops: &debug_file_ops,	/* file ops */
#endif
};


static struct proc_dir_entry *debug_proc_root_entry;


/* functions */

/*
 * debug_info_create
 * - create new debug-info
 */

static debug_info_t*  debug_info_create(char *name, int page_order, 
                                        int nr_areas, int buf_size)
{
	debug_info_t* rc;
	int i;

        /* alloc everything */

        rc = (debug_info_t*) kmalloc(sizeof(debug_info_t), GFP_ATOMIC);
        if(!rc) 
		goto fail_malloc_rc;
	rc->active_entry = (int*)kmalloc(nr_areas * sizeof(int), GFP_ATOMIC);
	if(!rc->active_entry)
		goto fail_malloc_active_entry;
	memset(rc->active_entry, 0, nr_areas * sizeof(int));
        rc->areas = (debug_entry_t **) kmalloc(nr_areas *
                                               sizeof(debug_entry_t *),
                                               GFP_ATOMIC);
        if (!rc->areas) 
                goto fail_malloc_areas;
        for (i = 0; i < nr_areas; i++) {
                rc->areas[i] =
                    (debug_entry_t *) __get_free_pages(GFP_ATOMIC,
                                                       page_order);
                if (!rc->areas[i]) {
                        for (i--; i >= 0; i--) {
                                free_pages((unsigned long) rc->areas[i],
                                           page_order);
                        }
                        goto fail_malloc_areas2;
                } else {
                        memset(rc->areas[i], 0, PAGE_SIZE << page_order);
                }
        }

        /* initialize members */

        spin_lock_init(&rc->lock);
        rc->page_order  = page_order;
        rc->nr_areas    = nr_areas;
        rc->active_area = 0;
        rc->level       = DEBUG_DEFAULT_LEVEL;
        rc->buf_size    = buf_size;
        rc->entry_size  = sizeof(debug_entry_t) + buf_size;
        strncpy(rc->name, name, MIN(strlen(name), (DEBUG_MAX_PROCF_LEN - 1)));
        rc->name[MIN(strlen(name), (DEBUG_MAX_PROCF_LEN - 1))] = 0;
        memset(rc->views, 0, DEBUG_MAX_VIEWS * sizeof(struct debug_view *));
	memset(rc->proc_entries, 0 ,DEBUG_MAX_VIEWS * 
               sizeof(struct proc_dir_entry*));
        atomic_set(&(rc->ref_count), 0);
        rc->proc_root_entry =
            debug_create_proc_dir_entry(debug_proc_root_entry, rc->name,
                                        S_IFDIR | S_IRUGO | S_IXUGO |
                                        S_IWUSR | S_IWGRP, NULL, NULL);

	/* append new element to linked list */

        if(debug_area_first == NULL){
                /* first element in list */
                debug_area_first = rc;
                rc->prev = NULL;
        }
        else{
                /* append element to end of list */
                debug_area_last->next = rc;
                rc->prev = debug_area_last;
        }
        debug_area_last = rc;
        rc->next = NULL;

	debug_info_get(rc);
	return rc;

fail_malloc_areas2:
	kfree(rc->areas);
fail_malloc_areas:
	kfree(rc->active_entry);
fail_malloc_active_entry:
	kfree(rc);
fail_malloc_rc:
	return NULL;
}

/*
 * debug_info_get
 * - increments reference count for debug-info
 */

static void debug_info_get(debug_info_t * db_info)
{
	if (db_info)
		atomic_inc(&db_info->ref_count);
}

/*
 * debug_info_put:
 * - decreases reference count for debug-info and frees it if necessary
 */

static void debug_info_put(debug_info_t *db_info)
{
	int i;

	if (!db_info)
		return;
	if (atomic_dec_and_test(&db_info->ref_count)) {
		printk(KERN_INFO "debug: freeing debug area %p (%s)\n",
		       db_info, db_info->name);
		for (i = 0; i < DEBUG_MAX_VIEWS; i++) {
			if (db_info->views[i] != NULL)
				debug_delete_proc_dir_entry
				    (db_info->proc_root_entry,
				     db_info->proc_entries[i]);
		}
		debug_delete_proc_dir_entry(debug_proc_root_entry,
					    db_info->proc_root_entry);
		for (i = 0; i < db_info->nr_areas; i++) {
			free_pages((unsigned long) db_info->areas[i],
				   db_info->page_order);
		}
		kfree(db_info->areas);
		kfree(db_info->active_entry);
		if(db_info == debug_area_first)
			debug_area_first = db_info->next;
		if(db_info == debug_area_last)
			debug_area_last = db_info->prev;
		if(db_info->prev) db_info->prev->next = db_info->next;
		if(db_info->next) db_info->next->prev = db_info->prev;
		kfree(db_info);
	}
}


/*
 * debug_output:
 * - called for user read()
 * - copies formated output form private_data of the file
 *   handle to the user buffer
 */

static ssize_t debug_output(struct file *file,	/* file descriptor */
			    char *user_buf,	/* user buffer */
			    size_t user_len,	/* length of buffer */
			    loff_t *offset	/* offset in the file */ )
{
	loff_t len;
	int rc;
	file_private_info_t *p_info;

	p_info = ((file_private_info_t *) file->private_data);
	if (*offset >= p_info->len) {
		return 0;	/* EOF */
	} else {
		len = MIN(user_len, (p_info->len - *offset));
		if ((rc = copy_to_user(user_buf, &(p_info->data[*offset]),len)))
			return rc;;
		(*offset) += len;
		return len;	/* number of bytes "read" */
	}
}

/*
 * debug_input:
 * - called for user write()
 * - calls input function of view
 */

static ssize_t debug_input(struct file *file,
			   const char *user_buf, size_t length,
			   loff_t *offset)
{
	int rc = 0;
	file_private_info_t *p_info;

	down(&debug_lock);
	p_info = ((file_private_info_t *) file->private_data);
	if (p_info->view->input_proc)
		rc = p_info->view->input_proc(p_info->debug_info,
					      p_info->view, file, user_buf,
					      length, offset);
	up(&debug_lock);
	return rc;		/* number of input characters */
}

/*
 * debug_format_output:
 * - calls prolog, header and format functions of view to format output
 */

static int debug_format_output(debug_info_t * debug_area, char *buf,
			       int size, struct debug_view *view)
{
	int len = 0;
	int i, j;
	int nr_of_entries;
	debug_entry_t *act_entry;

	/* print prolog */
	if (view->prolog_proc)
		len += view->prolog_proc(debug_area, view, buf);
	/* print debug records */
	if (!(view->format_proc) && !(view->header_proc))
		goto out;
	nr_of_entries = PAGE_SIZE / debug_area->entry_size
			<< debug_area->page_order;
	for (i = 0; i < debug_area->nr_areas; i++) {
		act_entry = debug_area->areas[i];
		for (j = 0; j < nr_of_entries; j++) {
			if (act_entry->id.fields.used == 0)
				break;	/* empty entry */
			if (view->header_proc)
				len += view->header_proc(debug_area, view, i,
						         act_entry, buf + len);
			if (view->format_proc)
				len += view->format_proc(debug_area, view,
						         buf + len,
						         DEBUG_DATA(act_entry));
			if (len > size) {
				printk(KERN_ERR
				"debug: error -- memory exceeded for (%s/%s)\n",
				debug_area->name, view->name);
				printk(KERN_ERR "debug: fix view %s!!\n",
				       view->name);
				printk(KERN_ERR
				"debug: area: %i (0 - %i) entry: %i (0 - %i)\n",
				i, debug_area->nr_areas - 1, j,
				nr_of_entries - 1);
				goto out;
			}
			act_entry = (debug_entry_t *) (((char *) act_entry) +
					       debug_area->entry_size);
		}
	}
      out:
	return len;
}


/*
 * debug_open:
 * - called for user open()
 * - copies formated output to private_data area of the file
 *   handle
 */

static int debug_open(struct inode *inode, struct file *file)
{
	int i = 0, size = 0, rc = 0, f_entry_size = 0;
	file_private_info_t *p_info;
	debug_info_t* debug_info;

#ifdef DEBUG
	printk("debug_open\n");
#endif

#ifdef MODULE
	MOD_INC_USE_COUNT;
#endif
	down(&debug_lock);

	/* find debug log and view */

	debug_info = debug_area_first;
	while(debug_info != NULL){
		for (i = 0; i < DEBUG_MAX_VIEWS; i++) {
			if (debug_info->views[i] == NULL)
				continue;
			else if (debug_info->proc_entries[i]->low_ino ==
				 file->f_dentry->d_inode->i_ino) {
				goto found;	/* found view ! */
			}
		}
		debug_info = debug_info->next;
	}
	/* no entry found */
	rc = -EINVAL;
	goto out;
      found:
	if ((file->private_data =
	     kmalloc(sizeof(file_private_info_t), GFP_ATOMIC)) == 0) {
		printk(KERN_ERR "debug_open: kmalloc failed\n");
		rc = -ENOMEM;
		goto out;
	}
	p_info = (file_private_info_t *) file->private_data;

	/*
	 * the size for the formated output is calculated
	 * with the following formula:
	 *
	 *   prolog-size
	 *   +
	 *   (record header size + record data field size)
	 *   * number of entries per page
	 *   * number of pages per area
	 *   * number of areas
	 */

	if (debug_info->views[i]->prolog_proc)
		size +=
		    debug_info->views[i]->prolog_proc(debug_info,
							 debug_info->
							 views[i], NULL);

	if (debug_info->views[i]->header_proc)
		f_entry_size =
		    debug_info->views[i]->header_proc(debug_info,
							 debug_info->
							 views[i], 0, NULL,
							 NULL);
	if (debug_info->views[i]->format_proc)
		f_entry_size +=
		    debug_info->views[i]->format_proc(debug_info,
							 debug_info->
							 views[i], NULL,
							 NULL);

	size += f_entry_size 
		* (PAGE_SIZE / debug_info->entry_size
		<< debug_info->page_order)
		* debug_info->nr_areas + 1;	/* terminating \0 */
#ifdef DEBUG
	printk("debug_open: size: %i\n", size);
#endif

	/* alloc some bytes more to be safe against bad views */
	if ((p_info->data = vmalloc(size + ADD_BUFFER)) == 0) {
		printk(KERN_ERR "debug_open: vmalloc failed\n");
		vfree(file->private_data);
		rc = -ENOMEM;
		goto out;
	}

	p_info->size = size;
	p_info->debug_info = debug_info;
	p_info->view = debug_info->views[i];

	spin_lock_irq(&debug_info->lock);

	p_info->len =
	    debug_format_output(debug_info, p_info->data, size,
				debug_info->views[i]);
#ifdef DEBUG
	{
		int ilen = p_info->len;
		printk("debug_open: len: %i\n", ilen);
	}
#endif

	spin_unlock_irq(&debug_info->lock);
	debug_info_get(debug_info);

      out:
	up(&debug_lock);
#ifdef MODULE
	if (rc != 0)
		MOD_DEC_USE_COUNT;
#endif
	return rc;
}

/*
 * debug_close:
 * - called for user close()
 * - deletes  private_data area of the file handle
 */

static int debug_close(struct inode *inode, struct file *file)
{
	file_private_info_t *p_info;
#ifdef DEBUG
	printk("debug_close\n");
#endif
	down(&debug_lock);
	p_info = (file_private_info_t *) file->private_data;
	debug_info_put(p_info->debug_info);
	if (p_info->data) {
		vfree(p_info->data);
		kfree(file->private_data);
	}
	up(&debug_lock);

#ifdef MODULE
	MOD_DEC_USE_COUNT;
#endif
	return 0;		/* success */
}

/*
 * debug_create_proc_dir_entry:
 * - initializes proc-dir-entry and registers it
 */

static struct proc_dir_entry *debug_create_proc_dir_entry
    (struct proc_dir_entry *root, const char *name, mode_t mode,
     struct inode_operations *iops, struct file_operations *fops) 
{
	struct proc_dir_entry *rc = NULL;

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,3,98))
	const char *fn = name;
	int len;
	len = strlen(fn);

	rc = (struct proc_dir_entry *) kmalloc(sizeof(struct proc_dir_entry)
					       + len + 1, GFP_ATOMIC);
	if (!rc)
		goto out;

	memset(rc, 0, sizeof(struct proc_dir_entry));
	memcpy(((char *) rc) + sizeof(*rc), fn, len + 1);
	rc->name = ((char *) rc) + sizeof(*rc);
	rc->namelen = len;
	rc->low_ino = 0, rc->mode = mode;
	rc->nlink = 1;
	rc->uid = 0;
	rc->gid = 0;
	rc->size = 0;
	rc->get_info = NULL;
	rc->ops = iops;

	proc_register(root, rc);
#else
	rc = create_proc_entry(name, mode, root);
	if (!rc)
		goto out;
	if (fops)
		rc->proc_fops = fops;
#endif

      out:
	return rc;
}


/*
 * delete_proc_dir_entry:
 */

static void debug_delete_proc_dir_entry
    (struct proc_dir_entry *root, struct proc_dir_entry *proc_entry) 
{

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,3,98))
	proc_unregister(root, proc_entry->low_ino);
	kfree(proc_entry);
#else
	remove_proc_entry(proc_entry->name, root);
#endif
}

/*
 * debug_register:
 * - creates and initializes debug area for the caller
 * - returns handle for debug area
 */

debug_info_t *debug_register
    (char *name, int page_order, int nr_areas, int buf_size) 
{
	debug_info_t *rc = NULL;

#ifdef MODULE
	MOD_INC_USE_COUNT;
#endif
	if (!initialized)
		debug_init();
	down(&debug_lock);

        /* create new debug_info */

	rc = debug_info_create(name, page_order, nr_areas, buf_size);
	if(!rc) 
		goto out;
	debug_register_view(rc, &debug_level_view);
	printk(KERN_INFO
	       "debug: reserved %d areas of %d pages for debugging %s\n",
	       nr_areas, 1 << page_order, rc->name);
      out:
        if (rc == NULL){
		printk(KERN_ERR "debug: debug_register failed for %s\n",name);
#ifdef MODULE
		MOD_DEC_USE_COUNT;
#endif
        }
	up(&debug_lock);
	return rc;
}

/*
 * debug_unregister:
 * - give back debug area
 */

void debug_unregister(debug_info_t * id)
{
	if (!id)
		goto out;
	down(&debug_lock);
	printk(KERN_INFO "debug: unregistering %s\n", id->name);
	debug_info_put(id);
	up(&debug_lock);

#ifdef MODULE
	MOD_DEC_USE_COUNT;
#endif
      out:
	return;
}

/*
 * debug_set_level:
 * - set actual debug level
 */

void debug_set_level(debug_info_t* id, int new_level)
{
	long flags;
	if(!id)
		return;	
	spin_lock_irqsave(&id->lock,flags);
        if(new_level == DEBUG_OFF_LEVEL){
                id->level = DEBUG_OFF_LEVEL;
                printk(KERN_INFO "debug: %s: switched off\n",id->name);
        } else if ((new_level > DEBUG_MAX_LEVEL) || (new_level < 0)) {
                printk(KERN_INFO
                        "debug: %s: level %i is out of range (%i - %i)\n",
                        id->name, new_level, 0, DEBUG_MAX_LEVEL);
        } else {
                id->level = new_level;
                printk(KERN_INFO 
			"debug: %s: new level %i\n",id->name,id->level);
        }
	spin_unlock_irqrestore(&id->lock,flags);
}


/*
 * proceed_active_entry:
 * - set active entry to next in the ring buffer
 */

static inline void proceed_active_entry(debug_info_t * id)
{
	if ((id->active_entry[id->active_area] += id->entry_size)
	    > ((PAGE_SIZE << (id->page_order)) - id->entry_size))
		id->active_entry[id->active_area] = 0;
}

/*
 * proceed_active_area:
 * - set active area to next in the ring buffer
 */

static inline void proceed_active_area(debug_info_t * id)
{
	id->active_area++;
	id->active_area = id->active_area % id->nr_areas;
}

/*
 * get_active_entry:
 */

static inline debug_entry_t *get_active_entry(debug_info_t * id)
{
	return (debug_entry_t *) ((char *) id->areas[id->active_area] +
				  id->active_entry[id->active_area]);
}

/*
 * debug_common:
 * - set timestamp, caller address, cpu number etc.
 */

static inline debug_entry_t *debug_common(debug_info_t * id)
{
	debug_entry_t *active;

	active = get_active_entry(id);
	STCK(active->id.stck);
	active->id.fields.cpuid = smp_processor_id();
	active->id.fields.used = 1;
	active->caller = __builtin_return_address(0);
	return active;
}

/*
 * debug_event:
 */

debug_entry_t *debug_event(debug_info_t * id, int level, void *buf,
			   int len)
{
	long flags;
	debug_entry_t *active = NULL;

	if ((!id) || (level > id->level))
		goto out;
	spin_lock_irqsave(&id->lock, flags);
	active = debug_common(id);
	active->id.fields.exception = 0;
	memset(DEBUG_DATA(active), 0, id->buf_size);
	memcpy(DEBUG_DATA(active), buf, MIN(len, id->buf_size));
	proceed_active_entry(id);
	spin_unlock_irqrestore(&id->lock, flags);
      out:
	return active;
}

/*
 * debug_int_event:
 */

debug_entry_t *debug_int_event(debug_info_t * id, int level,
			       unsigned int tag)
{
	long flags;
	debug_entry_t *active = NULL;

	if ((!id) || (level > id->level))
		goto out;
	spin_lock_irqsave(&id->lock, flags);
	active = debug_common(id);
	active->id.fields.exception = 0;
	memset(DEBUG_DATA(active), 0, id->buf_size);
	memcpy(DEBUG_DATA(active), &tag, MIN(sizeof(unsigned int), id->buf_size));
	proceed_active_entry(id);
	spin_unlock_irqrestore(&id->lock, flags);
      out:
	return active;
}

/*
 * debug_text_event:
 */

debug_entry_t *debug_text_event(debug_info_t * id, int level,
				const char *txt)
{
	long flags;
	debug_entry_t *active = NULL;

	if ((!id) || (level > id->level))
		goto out;
	spin_lock_irqsave(&id->lock, flags);
	active = debug_common(id);
	memset(DEBUG_DATA(active), 0, id->buf_size);
	strncpy(DEBUG_DATA(active), txt, MIN(strlen(txt), id->buf_size));
	active->id.fields.exception = 0;
	proceed_active_entry(id);
	spin_unlock_irqrestore(&id->lock, flags);
      out:
	return active;

}

/*
 * debug_exception:
 */

debug_entry_t *debug_exception(debug_info_t * id, int level, void *buf,
			       int len)
{
	long flags;
	debug_entry_t *active = NULL;

	if ((!id) || (level > id->level))
		goto out;
	spin_lock_irqsave(&id->lock, flags);
	active = debug_common(id);
	active->id.fields.exception = 1;
	memset(DEBUG_DATA(active), 0, id->buf_size);
	memcpy(DEBUG_DATA(active), buf, MIN(len, id->buf_size));
	proceed_active_entry(id);
	proceed_active_area(id);
	spin_unlock_irqrestore(&id->lock, flags);
      out:
	return active;
}

/*
 * debug_int_exception:
 */

debug_entry_t *debug_int_exception(debug_info_t * id, int level,
				   unsigned int tag)
{
	long flags;
	debug_entry_t *active = NULL;

	if ((!id) || (level > id->level))
		goto out;
	spin_lock_irqsave(&id->lock, flags);
	active = debug_common(id);
	active->id.fields.exception = 1;
	memset(DEBUG_DATA(active), 0, id->buf_size);
	memcpy(DEBUG_DATA(active), &tag,
	       MIN(sizeof(unsigned int), id->buf_size));
	proceed_active_entry(id);
	proceed_active_area(id);
	spin_unlock_irqrestore(&id->lock, flags);
      out:
	return active;
}

/*
 * debug_text_exception:
 */

debug_entry_t *debug_text_exception(debug_info_t * id, int level,
				    const char *txt)
{
	long flags;
	debug_entry_t *active = NULL;

	if ((!id) || (level > id->level))
		goto out;
	spin_lock_irqsave(&id->lock, flags);
	active = debug_common(id);
	memset(DEBUG_DATA(active), 0, id->buf_size);
	strncpy(DEBUG_DATA(active), txt, MIN(strlen(txt), id->buf_size));
	active->id.fields.exception = 1;
	proceed_active_entry(id);
	proceed_active_area(id);
	spin_unlock_irqrestore(&id->lock, flags);
      out:
	return active;

}

/*
 * debug_init:
 * - is called exactly once to initialize the debug feature
 */

int debug_init(void)
{
	int rc = 0;

	down(&debug_lock);
	if (!initialized) {
		debug_proc_root_entry =
		    debug_create_proc_dir_entry(&proc_root, DEBUG_DIR_ROOT,
						S_IFDIR | S_IRUGO | S_IXUGO
						| S_IWUSR | S_IWGRP, NULL,
						NULL);
		printk(KERN_INFO "debug: Initialization complete\n");
		initialized = 1;
	}
	up(&debug_lock);

	return rc;
}

/*
 * debug_register_view:
 */

int debug_register_view(debug_info_t * id, struct debug_view *view)
{
	int rc = 0;
	int i;
	long flags;
	mode_t mode = S_IFREG;

	if (!id)
		goto out;
	spin_lock_irqsave(&id->lock, flags);
	for (i = 0; i < DEBUG_MAX_VIEWS; i++) {
		if (id->views[i] == NULL)
			break;
	}
	if (i == DEBUG_MAX_VIEWS) {
		printk(KERN_WARNING "debug: cannot register view %s/%s\n",
			id->name,view->name);
		printk(KERN_WARNING 
			"debug: maximum number of views reached (%i)!\n", i);
		rc = -1;
	}
	else {
		id->views[i] = view;
		if (view->prolog_proc || view->format_proc || view->header_proc)
			mode |= S_IRUSR;
		if (view->input_proc)
			mode |= S_IWUSR;
		id->proc_entries[i] =
		    debug_create_proc_dir_entry(id->proc_root_entry,
						view->name, mode,
						&debug_inode_ops,
						&debug_file_ops);
		rc = 0;
	}
	spin_unlock_irqrestore(&id->lock, flags);
      out:
	return rc;
}

/*
 * debug_unregister_view:
 */

int debug_unregister_view(debug_info_t * id, struct debug_view *view)
{
	int rc = 0;
	int i;
	long flags;

	if (!id)
		goto out;
	spin_lock_irqsave(&id->lock, flags);
	for (i = 0; i < DEBUG_MAX_VIEWS; i++) {
		if (id->views[i] == view)
			break;
	}
	if (i == DEBUG_MAX_VIEWS)
		rc = -1;
	else {
		debug_delete_proc_dir_entry(id->proc_root_entry,
					    id->proc_entries[i]);		
		id->views[i] = NULL;
		rc = 0;
	}
	spin_unlock_irqrestore(&id->lock, flags);
      out:
	return rc;
}

/*
 * functions for debug-views
 ***********************************
*/

/*
 * prints out actual debug level
 */

static int debug_prolog_level_fn(debug_info_t * id,
				 struct debug_view *view, char *out_buf)
{
	int rc = 0;

	if (out_buf == NULL) {
		rc = 2;
		goto out;
	}
	if(id->level == -1) rc = sprintf(out_buf,"-\n");
	else rc = sprintf(out_buf, "%i\n", id->level);
      out:
	return rc;
}

/*
 * reads new debug level
 */

static int debug_input_level_fn(debug_info_t * id, struct debug_view *view,
				struct file *file, const char *user_buf,
				size_t in_buf_size, loff_t * offset)
{
	char input_buf[1];
	int rc = in_buf_size;

	if (*offset != 0)
		goto out;
	if ((rc = copy_from_user(input_buf, user_buf, 1)))
		goto out;
	if (isdigit(input_buf[0])) {
		int new_level = ((int) input_buf[0] - (int) '0');
		debug_set_level(id, new_level);
	} else if(input_buf[0] == '-') {
		debug_set_level(id, DEBUG_OFF_LEVEL);
	} else {
		printk(KERN_INFO "debug: level `%c` is not valid\n",
		       input_buf[0]);
	}
      out:
	*offset += in_buf_size;
	return rc;		/* number of input characters */
}

/*
 * prints debug header in raw format
 */

int debug_raw_header_fn(debug_info_t * id, struct debug_view *view,
                         int area, debug_entry_t * entry, char *out_buf)
{
        int rc;

	rc = sizeof(debug_entry_t);
        if (out_buf == NULL)
                goto out;
	memcpy(out_buf,entry,sizeof(debug_entry_t));
      out:
        return rc;
}

/*
 * prints debug data in raw format
 */

static int debug_raw_format_fn(debug_info_t * id, struct debug_view *view,
			       char *out_buf, const char *in_buf)
{
	int rc;

	rc = id->buf_size;
	if (out_buf == NULL || in_buf == NULL)
		goto out;
	memcpy(out_buf, in_buf, id->buf_size);
      out:
	return rc;
}

/*
 * prints debug data in hex/ascii format
 */

static int debug_hex_ascii_format_fn(debug_info_t * id, struct debug_view *view,
		    		  char *out_buf, const char *in_buf)
{
	int i, rc = 0;

	if (out_buf == NULL || in_buf == NULL) {
		rc = id->buf_size * 4 + 3;
		goto out;
	}
	for (i = 0; i < id->buf_size; i++) {
                rc += sprintf(out_buf + rc, "%02x ",
                              ((unsigned char *) in_buf)[i]);
        }
	rc += sprintf(out_buf + rc, "| ");
	for (i = 0; i < id->buf_size; i++) {
		unsigned char c = in_buf[i];
		if (!isprint(c))
			rc += sprintf(out_buf + rc, ".");
		else
			rc += sprintf(out_buf + rc, "%c", c);
	}
	rc += sprintf(out_buf + rc, "\n");
      out:
	return rc;
}

/*
 * prints header for debug entry
 */

int debug_dflt_header_fn(debug_info_t * id, struct debug_view *view,
			 int area, debug_entry_t * entry, char *out_buf)
{
	struct timeval time_val;
	unsigned long long time;
	char *except_str;
	unsigned long caller;
	int rc = 0;

	if (out_buf == NULL) {
		rc = DEBUG_PROC_HEADER_SIZE;
		goto out;
	}

	time = entry->id.stck;
	/* adjust todclock to 1970 */
	time -= 0x8126d60e46000000LL - (0x3c26700LL * 1000000 * 4096);
	tod_to_timeval(time, &time_val);

	if (entry->id.fields.exception)
		except_str = "*";
	else
		except_str = "-";
	caller = (unsigned long) entry->caller;
#if defined(CONFIG_ARCH_S390X)
	rc += sprintf(out_buf, "%02i %011lu:%06lu %1s %02i %016lx  ",
		      area, time_val.tv_sec,
		      time_val.tv_usec, except_str,
		      entry->id.fields.cpuid, caller);
#else
	caller &= 0x7fffffff;
	rc += sprintf(out_buf, "%02i %011lu:%06lu %1s %02i %08lx  ",
		      area, time_val.tv_sec,
		      time_val.tv_usec, except_str,
		      entry->id.fields.cpuid, caller);
#endif
      out:
	return rc;
}

/*
 * init_module:
 */

#ifdef MODULE
int init_module(void)
{
	int rc = 0;
#ifdef DEBUG
	printk("debug_module_init: \n");
#endif
	rc = debug_init();
	if (rc) 
		printk(KERN_INFO "debug: an error occurred with debug_init\n");
	return rc;
}

/*
 * cleanup_module:
 */

void cleanup_module(void)
{
#ifdef DEBUG
	printk("debug_cleanup_module: \n");
#endif
	debug_delete_proc_dir_entry(&proc_root, debug_proc_root_entry);
	return;
}

#endif			/* MODULE */
