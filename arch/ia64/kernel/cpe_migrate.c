/*
 * File:	cpe_migrate.c
 * Purpose:	Migrate data from physical pages with excessive correctable
 *		errors to new physical pages.  Keep the old pages on a discard
 *		list.
 *
 * Copyright (C) 2008 SGI - Silicon Graphics Inc.
 * Copyright (C) 2008 Russ Anderson <rja@sgi.com>
 */

#include <linux/sysdev.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/smp.h>
#include <linux/workqueue.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/vmalloc.h>
#include <linux/migrate.h>
#include <linux/page-isolation.h>
#include <linux/memcontrol.h>
#include <linux/kobject.h>
#include <linux/kthread.h>

#include <asm/page.h>
#include <asm/system.h>
#include <asm/sn/sn_cpuid.h>
#include <asm/mca.h>

#define BADRAM_BASENAME		"badram"
#define CE_HISTORY_LENGTH	30

struct cpe_info {
	u64 	paddr;
	u16	node;
};
static struct cpe_info cpe[CE_HISTORY_LENGTH];

static int cpe_polling_enabled = 1;
static int cpe_head;
static int cpe_tail;
static int mstat_cannot_isolate;
static int mstat_failed_to_discard;
static int mstat_already_marked;
static int mstat_already_on_list;

/* IRQ handler notifies this wait queue on receipt of an IRQ */
DECLARE_WAIT_QUEUE_HEAD(cpe_activate_IRQ_wq);
static DECLARE_COMPLETION(kthread_cpe_migrated_exited);
int cpe_active;
DEFINE_SPINLOCK(cpe_migrate_lock);

static void
get_physical_address(void *buffer, u64 *paddr, u16 *node)
{
	sal_log_record_header_t *rh;
	sal_log_mem_dev_err_info_t *mdei;
	ia64_err_rec_t *err_rec;
	sal_log_platform_err_info_t *plat_err;
	efi_guid_t guid;

	err_rec = buffer;
	rh = &err_rec->sal_elog_header;
	*paddr = 0;
	*node = 0;

	/*
	 * Make sure it is a corrected error.
	 */
	if (rh->severity != sal_log_severity_corrected)
		return;

	plat_err = (sal_log_platform_err_info_t *)&err_rec->proc_err;

	guid = plat_err->mem_dev_err.header.guid;
	if (efi_guidcmp(guid, SAL_PLAT_MEM_DEV_ERR_SECT_GUID) == 0) {
		/*
		 * Memory cpe
		 */
		mdei = &plat_err->mem_dev_err;
		if (mdei->valid.oem_data) {
			if (mdei->valid.physical_addr)
				*paddr = mdei->physical_addr;

			if (mdei->valid.node) {
				if (ia64_platform_is("sn2"))
					*node = nasid_to_cnodeid(mdei->node);
				else
					*node = mdei->node;
			}
		}
	}
}

static struct page *
alloc_migrate_page(struct page *ignored, unsigned long node, int **x)
{

	return alloc_pages_node(node, GFP_HIGHUSER_MOVABLE, 0);
}

static int
validate_paddr_page(u64 paddr)
{
	struct page *page;

	if (!paddr)
		return -EINVAL;

	if (!ia64_phys_addr_valid(paddr))
		return -EINVAL;

	if (!pfn_valid(paddr >> PAGE_SHIFT))
		return -EINVAL;

	page = phys_to_page(paddr);
	if (PageMemError(page))
		mstat_already_marked++;
	return 0;
}

extern int isolate_lru_page(struct page *);
static int
ia64_mca_cpe_move_page(u64 paddr, u32 node)
{
	LIST_HEAD(pagelist);
	struct page *page;
	int ret;

	ret = validate_paddr_page(paddr);
	if (ret < 0)
		return ret;

	/*
	 * convert physical address to page number
	 */
	page = phys_to_page(paddr);

	migrate_prep();
	ret = isolate_lru_page(page);
	if (ret) {
		mstat_cannot_isolate++;
		return ret;
	}

	list_add(&page->lru, &pagelist);
	ret = migrate_pages(&pagelist, alloc_migrate_page, node, 0);
	if (ret == 0) {
		total_badpages++;
		list_add_tail(&page->lru, &badpagelist);
	} else {
		mstat_failed_to_discard++;
		/*
		 * The page failed to migrate and is not on the bad page list.
		 * Clearing the error bit will allow another attempt to migrate
		 * if it gets another correctable error.
		 */
		ClearPageMemError(page);
	}

	return 0;
}

/*
 * cpe_process_queue
 *	Pulls the physical address off the list and calls the migration code.
 *	Will process all the addresses on the list.
 */
void
cpe_process_queue(void)
{
	int ret;
	u64 paddr;
	u16 node;

	do {
		paddr = cpe[cpe_tail].paddr;
		if (paddr) {
			/*
			 * There is a valid entry that needs processing.
			 */
			node = cpe[cpe_tail].node;

			ret = ia64_mca_cpe_move_page(paddr, node);
			if (ret <= 0)
				/*
				 * Even though the return status is negative,
				 * clear the entry.  If the same address has
				 * another CPE it will be re-added to the list.
				 */
				cpe[cpe_tail].paddr = 0;

		}
		if (++cpe_tail >= CE_HISTORY_LENGTH)
			cpe_tail = 0;

	} while (cpe_tail != cpe_head);
	return;
}

inline int
cpe_list_empty(void)
{
	return (cpe_head == cpe_tail) && (!cpe[cpe_head].paddr);
}

/*
 * kthread_cpe_migrate
 *	kthread_cpe_migrate is created at module load time and lives
 *	until the module is removed.  When not active, it will sleep.
 */
static int
kthread_cpe_migrate(void *ignore)
{
	while (cpe_active) {
		/*
		 * wait for work
		 */
		(void)wait_event_interruptible(cpe_activate_IRQ_wq,
						(!cpe_list_empty() ||
						!cpe_active));
		cpe_process_queue();		/* process work */
	}
	complete(&kthread_cpe_migrated_exited);
	return 0;
}

DEFINE_SPINLOCK(cpe_list_lock);

/*
 * cpe_setup_migrate
 *	Get the physical address out of the CPE record, add it
 *	to the list of addresses to migrate (if not already on),
 *	and schedule the back end worker task.  This is called
 *	in interrupt context so cannot directly call the migration
 *	code.
 *
 *  Inputs
 *	rec	The CPE record
 *  Outputs
 *	1 on Success, -1 on failure
 */
static int
cpe_setup_migrate(void *rec)
{
	u64 paddr;
	u16 node;
	/* int head, tail; */
	int i, ret;

	if (!rec)
		return -EINVAL;

	get_physical_address(rec, &paddr, &node);
	ret = validate_paddr_page(paddr);
	if (ret < 0)
		return -EINVAL;

	if (!cpe_list_empty())
		for (i = 0; i < CE_HISTORY_LENGTH; i++) {
			if (PAGE_ALIGN(cpe[i].paddr) == PAGE_ALIGN(paddr)) {
				mstat_already_on_list++;
				return 1;	/* already on the list */
			}
		}

	if (!spin_trylock(&cpe_list_lock)) {
		/*
		 * Someone else has the lock.  To avoid spinning in interrupt
		 * handler context, bail.
		 */
		return 1;
	}

	if (cpe[cpe_head].paddr == 0) {
		cpe[cpe_head].node = node;
		cpe[cpe_head].paddr = paddr;

		if (++cpe_head >= CE_HISTORY_LENGTH)
			cpe_head = 0;
	}
	spin_unlock(&cpe_list_lock);

	wake_up_interruptible(&cpe_activate_IRQ_wq);

	return 1;
}

/*
 * =============================================================================
 */

/*
 * free_one_bad_page
 *	Free one page from the list of bad pages.
 */
static int
free_one_bad_page(unsigned long paddr)
{
	LIST_HEAD(pagelist);
	struct page *page, *page2, *target;

	/*
	 * Verify page address
	 */
	target = phys_to_page(paddr);
	list_for_each_entry_safe(page, page2, &badpagelist, lru) {
		if (page != target)
			continue;

		ClearPageMemError(page);        /* Mark the page as good */
		total_badpages--;
		list_move_tail(&page->lru, &pagelist);
		putback_lru_pages(&pagelist);
		break;
	}
	return 0;
}

/*
 * free_all_bad_pages
 *	Free all of the pages on the bad pages list.
 */
static int
free_all_bad_pages(void)
{
	struct page *page, *page2;

	list_for_each_entry_safe(page, page2, &badpagelist, lru) {
		ClearPageMemError(page);        /* Mark the page as good */
		total_badpages--;
	}
	putback_lru_pages(&badpagelist);
	return 0;
}

#define OPT_LEN	16

static ssize_t
badpage_store(struct kobject *kobj,
	      struct kobj_attribute *attr, const char *buf, size_t count)
{
	char optstr[OPT_LEN];
	unsigned long opt;
	int len = OPT_LEN;
	int err;

	if (count < len)
		len = count;

	strlcpy(optstr, buf, len);

	err = strict_strtoul(optstr, 16, &opt);
	if (err)
		return err;

	if (opt == 0)
		free_all_bad_pages();
	else
		free_one_bad_page(opt);

	return count;
}

/*
 * badpage_show
 *	Display the number, size, and addresses of all the pages on the
 *	bad page list.
 *
 *	Note that sysfs provides buf of PAGE_SIZE length.  bufend tracks
 *	the remaining space in buf to avoid overflowing.
 */
static ssize_t
badpage_show(struct kobject *kobj,
	     struct kobj_attribute *attr, char *buf)

{
	struct page *page, *page2;
	int i = 0, cnt = 0;
	char *bufend = buf + PAGE_SIZE;

	cnt = snprintf(buf, bufend - (buf + cnt),
			"Memory marked bad:        %d kB\n"
			"Pages marked bad:         %d\n"
			"Unable to isolate on LRU: %d\n"
			"Unable to migrate:        %d\n"
			"Already marked bad:       %d\n"
			"Already on list:          %d\n"
			"List of bad physical pages\n",
			total_badpages << (PAGE_SHIFT - 10), total_badpages,
			mstat_cannot_isolate, mstat_failed_to_discard,
			mstat_already_marked, mstat_already_on_list
			);

	list_for_each_entry_safe(page, page2, &badpagelist, lru) {
		if (bufend - (buf + cnt) < 20)
			break;		/* Avoid overflowing the buffer */
		cnt += snprintf(buf + cnt, bufend - (buf + cnt),
				" 0x%011lx", page_to_phys(page));
		if (!(++i % 5))
			cnt += snprintf(buf + cnt, bufend - (buf + cnt), "\n");
	}
	cnt += snprintf(buf + cnt, bufend - (buf + cnt), "\n");

	return cnt;
}

static struct kobj_attribute badram_attr = {
	.attr    = {
		.name = "badram",
		.mode = S_IWUSR | S_IRUGO,
	},
	.show = badpage_show,
	.store = badpage_store,
};

static int __init
cpe_migrate_external_handler_init(void)
{
	int error;
	struct task_struct *kthread;

	error = sysfs_create_file(kernel_kobj, &badram_attr.attr);
	if (error)
		return -EINVAL;

	/*
	 * set up the kthread
	 */
	cpe_active = 1;
	kthread = kthread_run(kthread_cpe_migrate, NULL, "cpe_migrate");
	if (IS_ERR(kthread)) {
		complete(&kthread_cpe_migrated_exited);
		return -EFAULT;
	}

	/*
	 * register external ce handler
	 */
	if (ia64_reg_CE_extension(cpe_setup_migrate)) {
		printk(KERN_ERR "ia64_reg_CE_extension failed.\n");
		return -EFAULT;
	}
	cpe_poll_enabled = cpe_polling_enabled;

	printk(KERN_INFO "Registered badram Driver\n");
	return 0;
}

static void __exit
cpe_migrate_external_handler_exit(void)
{
	/* unregister external mca handlers */
	ia64_unreg_CE_extension();

	/* Stop kthread */
	cpe_active = 0;			/* tell kthread_cpe_migrate to exit */
	wake_up_interruptible(&cpe_activate_IRQ_wq);
	wait_for_completion(&kthread_cpe_migrated_exited);

	sysfs_remove_file(kernel_kobj, &badram_attr.attr);
}

module_init(cpe_migrate_external_handler_init);
module_exit(cpe_migrate_external_handler_exit);

module_param(cpe_polling_enabled, int, 0644);
MODULE_PARM_DESC(cpe_polling_enabled,
		"Enable polling with migration");

MODULE_AUTHOR("Russ Anderson <rja@sgi.com>");
MODULE_DESCRIPTION("ia64 Corrected Error page migration driver");
