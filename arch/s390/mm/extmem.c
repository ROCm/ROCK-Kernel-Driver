/*
 * File...........: arch/s390/mm/extmem.c
 * Author(s)......: Carsten Otte <cotte@de.ibm.com>
 * 		    Rob M van der Heij <rvdheij@nl.ibm.com>
 * 		    Steven Shultz <shultzss@us.ibm.com>
 * Bugreports.to..: <Linux390@de.ibm.com>
 * (C) IBM Corporation 2002-2004
 */

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/bootmem.h>
#include <asm/page.h>
#include <asm/ebcdic.h>
#include <asm/errno.h>
#include <asm/extmem.h>
#include <asm/cpcmd.h>
#include <linux/ctype.h>

#define DCSS_DEBUG	/* Debug messages on/off */

#define DCSS_NAME "extmem"
#ifdef DCSS_DEBUG
#define PRINT_DEBUG(x...)	printk(KERN_DEBUG DCSS_NAME " debug:" x)
#else
#define PRINT_DEBUG(x...)   do {} while (0)
#endif
#define PRINT_INFO(x...)	printk(KERN_INFO DCSS_NAME " info:" x)
#define PRINT_WARN(x...)	printk(KERN_WARNING DCSS_NAME " warning:" x)
#define PRINT_ERR(x...)		printk(KERN_ERR DCSS_NAME " error:" x)


#define DCSS_LOADSHR    0x00
#define DCSS_LOADNSR    0x04
#define DCSS_PURGESEG   0x08
#define DCSS_FINDSEG    0x0c
#define DCSS_LOADNOLY   0x10
#define DCSS_SEGEXT     0x18
#define DCSS_QACTV      0x0c

struct qout64 {
	int segstart;
	int segend;
	int segcnt;
	int segrcnt;
	char segout[8][6];
};

struct qin64 {
	char qopcode;
	char rsrv1[3];
	char qrcode;
	char rsrv2[3];
	char qname[8];
	unsigned int qoutptr;
	short int qoutlen;
};

struct dcss_segment {
	struct list_head list;
	char dcss_name[8];
	unsigned long start_addr;
	unsigned long end;
	atomic_t ref_count;
	int do_nonshared;
	int vm_segtype;
};

static spinlock_t dcss_lock = SPIN_LOCK_UNLOCKED;
static struct list_head dcss_list = LIST_HEAD_INIT(dcss_list);
static char *segtype_string[7] = { "SW", "EW", "SR", "ER", "SN", "EN", "SC" };

extern struct {
	unsigned long addr, size, type;
} memory_chunk[MEMORY_CHUNKS];

/*
 * Create the 8 bytes, ebcdic VM segment name from
 * an ascii name.
 */
static void inline
dcss_mkname(char *name, char *dcss_name)
{
	int i;

	for (i = 0; i < 8; i++) {
		if (name[i] == '\0')
			break;
		dcss_name[i] = toupper(name[i]);
	};
	for (; i < 8; i++)
		dcss_name[i] = ' ';
	ASCEBC(dcss_name, 8);
}


/*
 * search all segments in dcss_list, and return the one
 * namend *name. If not found, return NULL.
 */
static struct dcss_segment *
segment_by_name (char *name)
{
	char dcss_name[9];
	struct list_head *l;
	struct dcss_segment *tmp, *retval = NULL;

	BUG_ON (!spin_is_locked(&dcss_lock));
	dcss_mkname (name, dcss_name);
	list_for_each (l, &dcss_list) {
		tmp = list_entry (l, struct dcss_segment, list);
		if (memcmp(tmp->dcss_name, dcss_name, 8) == 0) {
			retval = tmp;
			break;
		}
	}
	return retval;
}


/*
 * Perform a function on a dcss segment.
 */
static inline int
dcss_diag (__u8 func, void *parameter,
           unsigned long *ret1, unsigned long *ret2)
{
	unsigned long rx, ry;
	int rc;

	rx = (unsigned long) parameter;
	ry = (unsigned long) func;
	__asm__ __volatile__(
#ifdef CONFIG_ARCH_S390X
		"   sam31\n" // switch to 31 bit
		"   diag    %0,%1,0x64\n"
		"   sam64\n" // switch back to 64 bit
#else
		"   diag    %0,%1,0x64\n"
#endif
		"   ipm     %2\n"
		"   srl     %2,28\n"
		: "+d" (rx), "+d" (ry), "=d" (rc) : : "cc" );
	*ret1 = rx;
	*ret2 = ry;
	return rc;
}

static inline int
dcss_diag_translate_rc (int vm_rc) {
	if (vm_rc == 44)
		return -ENOENT;
	return -EIO;
}


/* do a diag to get info about a segment.
 * fills start_address, end and vm_segtype fields
 */
static int
query_segment_info (struct dcss_segment *seg)
{
	struct qin64  *qin = kmalloc (sizeof(struct qin64), GFP_DMA);
	struct qout64 *qout = kmalloc (sizeof(struct qout64), GFP_DMA);

	int diag_cc, rc;
	unsigned long dummy, vmrc;

	if ((qin == NULL) || (qout == NULL)) {
		rc = -ENOMEM;
		goto out_free;
	}

	/* initialize diag input parameters */
	qin->qopcode = DCSS_QACTV;
	qin->qoutptr = (unsigned long) qout;
	qin->qoutlen = sizeof(struct qout64);
	memcpy (qin->qname, seg->dcss_name, 8);

	diag_cc = dcss_diag (DCSS_SEGEXT, qin, &dummy, &vmrc);

	if (diag_cc > 1) {
		rc = dcss_diag_translate_rc (vmrc);
		goto out_free;
	}

	if (qout->segcnt > 1) {
		rc = -ENOTSUPP;
		goto out_free;
	}

	/* analyze diag output and update seg */
	seg->start_addr = qout->segstart;
	seg->end = qout->segend;

	seg->vm_segtype = qout->segout[0][3];

	rc = 0;

 out_free:
	if (qin) kfree(qin);
	if (qout) kfree(qout);
	return rc;
}

/*
 * check if the given segment collides with guest storage.
 * returns 1 if this is the case, 0 if no collision was found
 */
static int
segment_overlaps_storage(struct dcss_segment *seg)
{
	int i;

	for (i=0; i < MEMORY_CHUNKS && memory_chunk[i].size > 0; i++) {
		if (memory_chunk[i].type != 0)
			continue;
		if ((memory_chunk[i].addr >> 20) > (seg->end >> 20))
			continue;
		if (((memory_chunk[i].addr + memory_chunk[i].size - 1) >> 20)
				< (seg->start_addr >> 20))
			continue;
		return 1;
	}
	return 0;
}

/*
 * check if segment collides with other segments that are currently loaded
 * returns 1 if this is the case, 0 if no collision was found
 */
static int
segment_overlaps_others (struct dcss_segment *seg)
{
	struct list_head *l;
	struct dcss_segment *tmp;

	BUG_ON (!spin_is_locked(&dcss_lock));
	list_for_each(l, &dcss_list) {
		tmp = list_entry(l, struct dcss_segment, list);
		if ((tmp->start_addr >> 20) > (seg->end >> 20))
			continue;
		if ((tmp->end >> 20) < (seg->start_addr >> 20))
			continue;
		if (seg == tmp)
			continue;
		return 1;
	}
	return 0;
}

/*
 * get info about a segment
 * possible return values:
 * -ENOSYS  : we are not running on VM
 * -EIO     : could not perform query diagnose
 * -ENOENT  : no such segment
 * -ENOTSUPP: multi-part segment cannot be used with linux
 * -ENOSPC  : segment cannot be used (overlaps with storage)
 * -ENOMEM  : out of memory
 * 0 .. 6   : type of segment as defined in include/asm-s390/extmem.h
 */
int
segment_info (char* name)
{
	int rc;
	struct dcss_segment seg;

	if (!MACHINE_IS_VM)
		return -ENOSYS;

	dcss_mkname(name, seg.dcss_name);
	rc = query_segment_info (&seg);
	if (rc < 0)
		return rc;
	return seg.vm_segtype;
}

/*
 * real segment loading function, called from segment_load
 */
static int
__segment_load (char *name, int do_nonshared, unsigned long *addr, unsigned long *end)
{
	struct dcss_segment *seg = kmalloc(sizeof(struct dcss_segment),
			GFP_DMA);
	int dcss_command, rc, diag_cc;

	if (seg == NULL) {
		rc = -ENOMEM;
		goto out;
	}
	dcss_mkname (name, seg->dcss_name);
	rc = query_segment_info (seg);
	if (rc < 0)
		goto out_free;
	if (segment_overlaps_storage(seg)) {
		PRINT_WARN ("segment_load: not loading segment %s - overlaps"
				" storage\n",name);
		rc = -ENOSPC;
		goto out_free;
	}
	if (segment_overlaps_others(seg)) {
		PRINT_WARN ("segment_load: not loading segment %s - overlaps"
				" other segments\n",name);
		rc = -EBUSY;
		goto out_free;
	}
	if (do_nonshared)
		dcss_command = DCSS_LOADNSR;
	else
		dcss_command = DCSS_LOADNOLY;

	diag_cc = dcss_diag(dcss_command, seg->dcss_name,
			&seg->start_addr, &seg->end);
	if (diag_cc > 1) {
		PRINT_WARN ("segment_load: could not load segment %s - "
				"diag returned error (%ld)\n",name,seg->end);
		rc = dcss_diag_translate_rc (seg->end);
		dcss_diag(DCSS_PURGESEG, seg->dcss_name,
				&seg->start_addr, &seg->end);
		goto out_free;
	}
	seg->do_nonshared = do_nonshared;
	atomic_set(&seg->ref_count, 1);
	list_add(&seg->list, &dcss_list);
	rc = seg->vm_segtype;
	*addr = seg->start_addr;
	*end  = seg->end;
	if (do_nonshared)
		PRINT_INFO ("segment_load: loaded segment %s range %p .. %p "
				"type %s in non-shared mode\n", name,
				(void*)seg->start_addr, (void*)seg->end,
				segtype_string[seg->vm_segtype]);
	else
		PRINT_INFO ("segment_load: loaded segment %s range %p .. %p "
				"type %s in shared mode\n", name,
				(void*)seg->start_addr, (void*)seg->end,
				segtype_string[seg->vm_segtype]);
	goto out;
 out_free:
	kfree (seg);
 out:
	return rc;
}

/*
 * this function loads a DCSS segment
 * name         : name of the DCSS
 * do_nonshared : 0 indicates that the dcss should be shared with other linux images
 *                1 indicates that the dcss should be exclusive for this linux image
 * addr         : will be filled with start address of the segment
 * end          : will be filled with end address of the segment
 * return values:
 * -ENOSYS  : we are not running on VM
 * -EIO     : could not perform query or load diagnose
 * -ENOENT  : no such segment
 * -ENOTSUPP: multi-part segment cannot be used with linux
 * -ENOSPC  : segment cannot be used (overlaps with storage)
 * -EBUSY   : segment can temporarily not be used (overlaps with dcss)
 * -EPERM   : segment is currently loaded with incompatible permissions
 * -ENOMEM  : out of memory
 * 0 .. 6   : type of segment as defined in include/asm-s390/extmem.h
 */
int
segment_load (char *name, int do_nonshared, unsigned long *addr,
		unsigned long *end)
{
	struct dcss_segment *seg;
	int rc;

	if (!MACHINE_IS_VM)
		return -ENOSYS;

	spin_lock (&dcss_lock);
	seg = segment_by_name (name);
	if (seg == NULL)
		rc = __segment_load (name, do_nonshared, addr, end);
	else {
		if (do_nonshared == seg->do_nonshared) {
			atomic_inc(&seg->ref_count);
			*addr = seg->start_addr;
			*end  = seg->end;
			rc    = seg->vm_segtype;
		} else {
			*addr = *end = 0;
			rc    = -EPERM;
		}
	}
	spin_unlock (&dcss_lock);
	return rc;
}

/*
 * Decrease the use count of a DCSS segment and remove
 * it from the address space if nobody is using it
 * any longer.
 */
void
segment_unload(char *name)
{
	unsigned long dummy;
	struct dcss_segment *seg;

	if (!MACHINE_IS_VM)
		return;

	spin_lock(&dcss_lock);
	seg = segment_by_name (name);
	if (seg == NULL) {
		PRINT_ERR ("could not find segment %s in segment_unload, "
				"please report to linux390@de.ibm.com\n",name);
		goto out_unlock;
	}
	if (atomic_dec_return(&seg->ref_count) == 0) {
		list_del(&seg->list);
		dcss_diag(DCSS_PURGESEG, seg->dcss_name,
			  &dummy, &dummy);
		kfree(seg);
	}
out_unlock:
	spin_unlock(&dcss_lock);
}

/*
 * save segment content permanently
 */
void segment_save(char *name)
{
	struct dcss_segment *seg;
	int startpfn = 0;
	int endpfn = 0;
	char cmd1[80];
	char cmd2[80];

	if (!MACHINE_IS_VM)
		return;

	spin_lock(&dcss_lock);
	seg = segment_by_name (name);

	if (seg == NULL) {
		PRINT_ERR ("could not find segment %s in segment_save, please report to linux390@de.ibm.com\n",name);
		return;
	}

	startpfn = seg->start_addr >> 12;
	endpfn = (seg->end) >> 12;
	sprintf(cmd1, "DEFSEG %s %X-%X %s", name, startpfn, endpfn,
			segtype_string[seg->vm_segtype]);
	sprintf(cmd2, "SAVESEG %s", name);
	cpcmd(cmd1, NULL, 80);
	cpcmd(cmd2, NULL, 80);
	spin_unlock(&dcss_lock);
}

EXPORT_SYMBOL(segment_load);
EXPORT_SYMBOL(segment_unload);
EXPORT_SYMBOL(segment_save);
EXPORT_SYMBOL(segment_info);
