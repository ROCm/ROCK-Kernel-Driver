/* 
 * File...........: linux/drivers/s390/block/dasd_ccwstuff.c
 * Author(s)......: Holger Smolinski <Holger.Smolinski@de.ibm.com>
 * Bugreports.to..: <Linux390@de.ibm.com>
 * (C) IBM Corporation, IBM Deutschland Entwicklung GmbH, 1999,2000
 */

#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/malloc.h>
#include <linux/dasd.h>
#include <asm/atomic.h>

#include "dasd_types.h"

#define PRINTK_HEADER "dasd_ccw:"
#define MAX_CP_POWER 9		/* Maximum allowed index */
#define CP_PER_PAGE_POWER 9	/* Maximum index, fitting on page */

#define get_free_pages __get_free_pages

/* Stuff for the handling task_list */
dasd_chanq_t *cq_head = NULL;	/* head of task_list */
atomic_t chanq_tasks;

/* Array of freelists for the channel programs' -space */
static ccw1_t *ccwarea[CP_PER_PAGE_POWER + 1] =
{NULL,};

/* array of pages retrieved for internal use */
#define MAX_DASD_PAGES 64
static int dasd_page_count = 0;
static long dasd_page[MAX_DASD_PAGES];

static spinlock_t ccw_lock=SPIN_LOCK_UNLOCKED;	/* spinlock for ccwareas */
static spinlock_t cq_lock=SPIN_LOCK_UNLOCKED;	/* spinlock for cq_head */

void
ccwarea_enq (int index, ccw1_t * area)
{
	FUNCTION_ENTRY ("ccwarea_enq");
#if DASD_PARANOIA > 2
	if (!area) {
		INTERNAL_CHECK ("zero area %s\n", "");
	}
	if (index > CP_PER_PAGE_POWER) {
		INTERNAL_CHECK ("index too large %d\n", index);
	}
#endif
	*(ccw1_t **) area = ccwarea[index];
	ccwarea[index] = area;
	FUNCTION_EXIT ("ccwarea_enq");
	return;
}

ccw1_t *
ccwarea_deq (int index)
{
	ccw1_t *cp;
	FUNCTION_ENTRY ("ccwarea_deq");
#if DASD_PARANOIA > 2
	if (index > CP_PER_PAGE_POWER) {
		INTERNAL_CHECK ("index too large %d\n", index);
	}
#endif
	cp = ccwarea[index];
	ccwarea[index] = *(ccw1_t **) ccwarea[index];
#if DASD_PARANOIA > 2
	if (!cp) {
		INTERNAL_CHECK ("returning NULL %s\n", "");
	}
#endif
	FUNCTION_EXIT ("ccwarea_deq");
	return cp;
}

ccw1_t *
request_cpa (int index)
{
	ccw1_t *freeblk;
	FUNCTION_ENTRY ("request_cpa");
	if (index > MAX_CP_POWER) {
		INTERNAL_ERROR ("index too large %d\n", index);
		freeblk = NULL;
		goto exit;
	}
	if (index > CP_PER_PAGE_POWER) {
		int pc = 1 << (index - CP_PER_PAGE_POWER);
		do {
			freeblk = (ccw1_t *) get_free_pages (GFP_ATOMIC, index - CP_PER_PAGE_POWER);
			if (dasd_page_count + pc >= MAX_DASD_PAGES) {
				PRINT_WARN ("Requesting too many pages...");
			} else {
				int i;
				for (i = 0; i < pc; i++)
					dasd_page[dasd_page_count++] =
						(long) freeblk + i * PAGE_SIZE;
			}
			FUNCTION_CONTROL ("requesting index %d", index);
			if ( ! freeblk ) {
				panic ("No memory received\n");
			}
		} while (!freeblk);
		memset(freeblk,0,PAGE_SIZE<<(index-CP_PER_PAGE_POWER));
		goto exit;
	}
	while (ccwarea[index] == NULL) {
		ccw1_t *blk;
		if (index == CP_PER_PAGE_POWER) {
			do {
				blk = (ccw1_t *) get_free_page (GFP_ATOMIC);
				if (dasd_page_count + 1 >= MAX_DASD_PAGES) {
					PRINT_WARN ("Requesting too many pages...");
				} else {
					dasd_page[dasd_page_count++] =
						(long) blk;
				}
				if (blk == NULL) {
					PRINT_WARN ("Can't allocate page!\n");
				}
			} while ( ! blk );
			memset(blk,0,PAGE_SIZE);
			ccwarea_enq (CP_PER_PAGE_POWER, blk);
			continue;
		}
		blk = request_cpa (index + 1);
#if DASD_PARANOIA > 1
		if (!blk) {
			PRINT_WARN ("retrieved NULL");
		}
#endif				/* DASD_PARANOIA */
		ccwarea_enq (index, blk);
		ccwarea_enq (index, blk + (1 << index));
	}
#if DASD_PARANOIA > 2
	if (!ccwarea[index]) {
		INTERNAL_ERROR ("ccwarea is NULL\n%s", "");
	}
#endif				/* DASD_PARANOIA */

	freeblk = ccwarea_deq (index);
#if DASD_PARANOIA > 1
	if (!freeblk) {
		INTERNAL_ERROR ("freeblk is NULL\n%s", "");
	}
#endif				/* DASD_PARANOIA */
      exit:
	FUNCTION_EXIT ("request_cpa");
	return freeblk;
}

ccw1_t *
request_cp (int size)
{
	ccw1_t *freeblk;
	int index;
	int blksize;
	/* Determine the index of ccwarea to look at */
	for (index = 0, blksize = 1;
	     size > blksize;
	     index++, blksize = blksize << 1) {
	}
	if (index > MAX_CP_POWER) {
		INTERNAL_ERROR ("index too large %d\n", index);
	}
	spin_lock (&ccw_lock);
	freeblk = request_cpa (index);
	spin_unlock (&ccw_lock);
	if (freeblk == NULL) {
		printk (KERN_WARNING PRINTK_HEADER
			"No way to deliver free ccw space\n");
	}
	return freeblk;
}

void
release_cp (int size, ccw1_t * area)
{
	int index;
	int blksize;
	/* Determine the index of ccwarea to look at */
	for (index = 0, blksize = 1;
	     size > blksize;
	     index++, blksize = blksize << 1) {
	}
	if (index > MAX_CP_POWER) {
		INTERNAL_ERROR ("index too large %d\n", index);
	} else if (index > CP_PER_PAGE_POWER) {
		free_pages ((unsigned long) area,
			    index - CP_PER_PAGE_POWER);
		INTERNAL_CHECK ("large index used: %d\n", index);
	} else {
		spin_lock (&ccw_lock);
		ccwarea_enq (index, area);
		spin_unlock (&ccw_lock);
	}
	return;
}

/* ---------------------------------------------------------- */

static cqr_t *cqrp = NULL;
static spinlock_t cqr_lock=SPIN_LOCK_UNLOCKED;

void
cqf_enq (cqr_t * cqf)
{
	*(cqr_t **) cqf = cqrp;
	cqrp = cqf;
}

cqr_t *
cqf_deq (void)
{
	cqr_t *cqr = cqrp;
	cqrp = *(cqr_t **) cqrp;
	return cqr;
}

cqr_t *
request_cq (void)
{
	cqr_t *cqr = NULL;
	int i;
	cqr_t *area;

	spin_lock (&cqr_lock);
	while (cqrp == NULL) {
		do {
			area = (cqr_t *) get_free_page (GFP_ATOMIC);
			if (area == NULL) {
				printk (KERN_WARNING PRINTK_HEADER
					"No memory for chanq area\n");
			}
		} while ( ! area );
		memset(area,0,PAGE_SIZE);
		if (dasd_page_count + 1 >= MAX_DASD_PAGES) {
			PRINT_WARN ("Requesting too many pages...");
		} else {
			dasd_page[dasd_page_count++] =
				(long) area;
		}
		for (i = 0; i < 4096 / sizeof (cqr_t); i++) {
				cqf_enq (area + i);
		}
	}
	cqr = cqf_deq ();
	spin_unlock (&cqr_lock);
	return cqr;
}

void
release_cq (cqr_t * cqr)
{
	spin_lock (&cqr_lock);
	cqf_enq (cqr);
	spin_unlock (&cqr_lock);
	return;
}

/* ----------------------------------------------------------- */
cqr_t *
request_cqr (int cpsize, int datasize)
{
	cqr_t *cqr = NULL;
	cqr = request_cq ();
	if (cqr == NULL) {
		printk (KERN_WARNING PRINTK_HEADER __FILE__
			"No memory for chanq request\n");
		goto exit;
	}
	memset (cqr, 0, sizeof (cqr_t));
	cqr -> magic = DASD_MAGIC;
	if (cpsize) {
		cqr->cpaddr = request_cp (cpsize);
		if (cqr->cpaddr == NULL) {
			printk (KERN_WARNING PRINTK_HEADER __FILE__
				"No memory for channel program\n");
			goto nocp;
		}
		cqr->cplength = cpsize;
	}
	if (datasize) {
		do {
			cqr->data = (char *) kmalloc (datasize, GFP_ATOMIC);
			if (cqr->data == NULL) {
				printk (KERN_WARNING PRINTK_HEADER __FILE__
					"No memory for cqr data area\n");
			}
		} while (!cqr->data);
		memset (cqr->data,0,datasize);
	}
	goto exit;
 nocp:
	release_cq (cqr);
	cqr = NULL;
 exit:
	return cqr;
}

int
release_cqr (cqr_t * cqr)
{
	int rc = 0;
	if (cqr == NULL) {
		rc = -ENOENT;
		return rc;
	}
	if (cqr->data) {
		kfree (cqr->data);
	}
	if (cqr->dstat) {
		kfree (cqr->dstat);
	}
	if (cqr->cpaddr) {
		release_cp (cqr->cplength, cqr->cpaddr);
	}
	cqr -> magic = dasd_MAGIC;
	release_cq (cqr);
	return rc;
}

/* -------------------------------------------------------------- */
void
dasd_chanq_enq (dasd_chanq_t * q, cqr_t * cqr)
{
	if (q->head != NULL) {
		q->tail->next = cqr;
	} else
		q->head = cqr;
	cqr->next = NULL;
	q->tail = cqr;
	q->queued_requests ++;
	if (atomic_compare_and_swap(CQR_STATUS_FILLED,
				    CQR_STATUS_QUEUED,
				    &cqr->status)) {
		PRINT_WARN ("q_cqr: %p status changed %d\n", 
			    cqr,atomic_read(&cqr->status));
		atomic_set(&cqr->status,CQR_STATUS_QUEUED);
	}
}

int
dasd_chanq_deq (dasd_chanq_t * q, cqr_t * cqr)
{
	cqr_t *prev;

	if (cqr == NULL)
		return -ENOENT;
	if (cqr == (cqr_t *) q->head) {
		q->head = cqr->next;
		if (q->head == NULL)
			q->tail = NULL;
	} else {
		prev = (cqr_t *) q->head; 
		while (prev && prev->next != cqr)
			prev = prev->next;
		if (prev == NULL)
			return -ENOENT;
		prev->next = cqr->next;
		if (prev->next == NULL)
			q->tail = prev;
	}
	cqr->next = NULL;
	q->queued_requests --;
	return release_cqr(cqr);
}

/* -------------------------------------------------------------------------- */
void
cql_enq_head (dasd_chanq_t * q)
{
	if (q == NULL) {
		INTERNAL_ERROR ("NULL queue passed%s\n", "");
		return;
	}
	if (atomic_read(&q->flags) & DASD_CHANQ_ACTIVE) {
		PRINT_WARN("Queue already active");
		return;
	}
	spin_lock(&cq_lock);
	atomic_set_mask(DASD_CHANQ_ACTIVE,&q->flags);
	q->next_q = cq_head;
	cq_head = q;
	spin_unlock(&cq_lock);
}

void
cql_deq (dasd_chanq_t * q)
{
	dasd_chanq_t *c;

        if (cq_head == NULL) {
                INTERNAL_ERROR ("Channel queue is empty%s\n", "");
		return;
	}
	if (q == NULL) {
		INTERNAL_ERROR ("NULL queue passed%s\n", "");
		return;
	}
	spin_lock(&cq_lock);
	if (! (atomic_read(&q->flags) & DASD_CHANQ_ACTIVE)) {
		PRINT_WARN("Queue not active\n");
	}
	else if (cq_head == q) {
		cq_head = q->next_q;
	} else {
		c = cq_head;
		while (c->next_q && c->next_q != q)
			c = c->next_q;
		if (c->next_q != q)
			INTERNAL_ERROR ("Entry not in queue%s\n", "");
		else
			c->next_q = q->next_q;
	}
	q->next_q = NULL;
	atomic_clear_mask(DASD_CHANQ_ACTIVE,&q->flags);
	spin_unlock(&cq_lock);
}
