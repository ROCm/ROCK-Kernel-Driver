/*
 * File...........: arch/s390/mm/dcss.c
 * Author(s)......: Steven Shultz <shultzss@us.ibm.com>
 *                  Carsten Otte <cotte@de.ibm.com>
 * Bugreports.to..: <Linux390@de.ibm.com>
 * thanks to Rob M van der Heij
 * - he wrote the diag64 function
 * (C) IBM Corporation 2002
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

struct dcss_segment {
        struct list_head list;
        char dcss_name[8];
        unsigned long start_addr;
        unsigned long end;
        atomic_t ref_count;
        int dcss_attr;
	int shared_attr;
};

static spinlock_t dcss_lock = SPIN_LOCK_UNLOCKED;
static struct list_head dcss_list = LIST_HEAD_INIT(dcss_list);
extern struct {
	unsigned long addr, size, type;
} memory_chunk[MEMORY_CHUNKS];

/*
 * Create the 8 bytes, ebcdic VM segment name from
 * an ascii name.
 */
static void inline dcss_mkname(char *name, char *dcss_name)
{
        int i;

        for (i = 0; i <= 8; i++) {
                if (name[i] == '\0')
                        break;
                dcss_name[i] = toupper(name[i]);
        };
        for (; i <= 8; i++)
                dcss_name[i] = ' ';
        ASCEBC(dcss_name, 8);
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


/* use to issue "extended" dcss query */
static inline int
dcss_diag_query(char *name, int *rwattr, int *shattr, unsigned long *segstart, unsigned long *segend)
{
        int i,j,rc;
        unsigned long  rx, ry;

        typedef struct segentry {
                char thisseg[8];
        } segentry;

        struct qout64 {
                int segstart;
                int segend;
                int segcnt;
                int segrcnt;
                segentry segout[6];
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


        struct qin64  *qinarea;
        struct qout64 *qoutarea;

        qinarea = (struct qin64*) get_zeroed_page (GFP_DMA);
        if (!qinarea) {
                rc =-ENOMEM;
                goto out;
        }
        qoutarea = (struct qout64*) get_zeroed_page (GFP_DMA);
        if (!qoutarea) {
                rc = -ENOMEM;
                free_page ((unsigned long) qinarea);
                goto out;
        }
        memset (qinarea,0,PAGE_SIZE);
        memset (qoutarea,0,PAGE_SIZE);

        qinarea->qopcode = DCSS_QACTV; /* do a query for active
                                          segments */
        qinarea->qoutptr = (unsigned long) qoutarea;
        qinarea->qoutlen = sizeof(struct qout64);

        /* Move segment name into double word aligned
           field and pad with blanks to 8 long.
         */

        for (i = j = 0 ; i < 8; i++) {
                qinarea->qname[i] = (name[j] == '\0') ? ' ' : name[j++];
        }

        /* name already in EBCDIC */
        /* ASCEBC ((void *)&qinarea.qname, 8); */

        /* set the assembler variables */
        rx = (unsigned long) qinarea;
        ry = DCSS_SEGEXT; /* this is extended function */

        /* issue diagnose x'64' */
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

        /* parse the query output area */
	*segstart=qoutarea->segstart;
	*segend=qoutarea->segend;

        if (rc > 1)
                {
                        *rwattr = 2;
                        *shattr = 2;
                        rc = 0;
                        goto free;
                }

        if (qoutarea->segcnt > 6)
                {
                        *rwattr = 3;
                        *shattr = 3;
                        rc = 0;
                        goto free;
                }

        *rwattr = 1;
        *shattr = 1;

        for (i=0; i < qoutarea->segrcnt; i++) {
                if (qoutarea->segout[i].thisseg[3] == 2 ||
                    qoutarea->segout[i].thisseg[3] == 3 ||
                    qoutarea->segout[i].thisseg[3] == 6 )
                        *rwattr = 0;
                if (qoutarea->segout[i].thisseg[3] == 1 ||
                    qoutarea->segout[i].thisseg[3] == 3 ||
                    qoutarea->segout[i].thisseg[3] == 5 )
                        *shattr = 0;
        } /* end of for statement */
        rc = 0;
 free:
        free_page ((unsigned long) qoutarea);
        free_page ((unsigned long) qinarea);
 out:
        return rc;
}

/*
 * Load a DCSS segment via the diag 0x64.
 */
int segment_load(char *name, int segtype, unsigned long *addr,
                 unsigned long *end)
{
        char dcss_name[8];
        struct list_head *l;
        struct dcss_segment *seg, *tmp;
	unsigned long dummy;
	unsigned long segstart, segend;
        int rc = 0,i;
        int rwattr, shattr;

        if (!MACHINE_IS_VM)
                return -ENOSYS;
        dcss_mkname(name, dcss_name);
	/* search for the dcss in list of currently loaded segments */
        spin_lock(&dcss_lock);
        seg = NULL;
        list_for_each(l, &dcss_list) {
                tmp = list_entry(l, struct dcss_segment, list);
                if (memcmp(tmp->dcss_name, dcss_name, 8) == 0) {
                        seg = tmp;
                        break;
                }
        }

        if (seg == NULL) {
                /* find out the attributes of this
                   shared segment */
                dcss_diag_query(dcss_name, &rwattr, &shattr, &segstart, &segend);
		/* does segment collide with main memory? */
		for (i=0; i < MEMORY_CHUNKS; i++) {
			if (memory_chunk[i].type != 0)
				continue;
			if (memory_chunk[i].addr > segend)
				continue;
			if (memory_chunk[i].addr + memory_chunk[i].size <= segstart)
				continue;
			spin_unlock(&dcss_lock);
			return -ENOENT;
		}
		/* or does it collide with other (loaded) segments? */
        	list_for_each(l, &dcss_list) {
                	tmp = list_entry(l, struct dcss_segment, list);
	                if ((segstart <= tmp->end && segstart >= tmp->start_addr) ||
				(segend <= tmp->end && segend >= tmp->start_addr) ||
				(segstart <= tmp->start_addr && segend >= tmp->end)) {
				PRINT_ERR("Segment Overlap!\n");
			        spin_unlock(&dcss_lock);
				return -ENOENT;
	                }
        	}

                /* do case statement on segtype */
                /* if asking for shared ro,
                   shared rw works */
                /* if asking for exclusive ro,
                   exclusive rw works */

                switch(segtype) {
                case SEGMENT_SHARED_RO:
                        if (shattr > 1 || rwattr > 1) {
                                spin_unlock(&dcss_lock);
                                return -ENOENT;
                        } else {
                                if (shattr == 0 && rwattr == 0)
                                        rc = SEGMENT_EXCLUSIVE_RO;
                                if (shattr == 0 && rwattr == 1)
                                        rc = SEGMENT_EXCLUSIVE_RW;
                                if (shattr == 1 && rwattr == 0)
                                        rc = SEGMENT_SHARED_RO;
                                if (shattr == 1 && rwattr == 1)
                                        rc = SEGMENT_SHARED_RW;
                        }
                        break;
                case SEGMENT_SHARED_RW:
                        if (shattr > 1 || rwattr != 1) {
                                spin_unlock(&dcss_lock);
                                return -ENOENT;
                        } else {
                                if (shattr == 0)
                                        rc = SEGMENT_EXCLUSIVE_RW;
                                if (shattr == 1)
                                        rc = SEGMENT_SHARED_RW;
                        }
                        break;

                case SEGMENT_EXCLUSIVE_RO:
                        if (shattr > 0 || rwattr > 1) {
                                spin_unlock(&dcss_lock);
                                return -ENOENT;
                        } else {
                                if (rwattr == 0)
                                        rc = SEGMENT_EXCLUSIVE_RO;
                                if (rwattr == 1)
                                        rc = SEGMENT_EXCLUSIVE_RW;
                        }
                        break;

                case SEGMENT_EXCLUSIVE_RW:
/*                        if (shattr != 0 || rwattr != 1) {
                                spin_unlock(&dcss_lock);
                                return -ENOENT;
                        } else {
*/
                                rc = SEGMENT_EXCLUSIVE_RW;
//                        }
                        break;

                default:
                        spin_unlock(&dcss_lock);
                        return -ENOENT;
                } /* end switch */

                seg = kmalloc(sizeof(struct dcss_segment), GFP_DMA);
                if (seg != NULL) {
                        memcpy(seg->dcss_name, dcss_name, 8);
			if (rc == SEGMENT_EXCLUSIVE_RW) {
				if (dcss_diag(DCSS_LOADNSR, seg->dcss_name,
						&seg->start_addr, &seg->end) == 0) {
					if (seg->end < max_low_pfn*PAGE_SIZE ) {
						atomic_set(&seg->ref_count, 1);
						list_add(&seg->list, &dcss_list);
						*addr = seg->start_addr;
						*end = seg->end;
						seg->dcss_attr = rc;
						if (shattr == 1 && rwattr == 1)
							seg->shared_attr = SEGMENT_SHARED_RW;
						else if (shattr == 1 && rwattr == 0)
							seg->shared_attr = SEGMENT_SHARED_RO;
						else
							seg->shared_attr = SEGMENT_EXCLUSIVE_RW;
					} else {
						dcss_diag(DCSS_PURGESEG, seg->dcss_name, &dummy, &dummy);
						kfree (seg);
						rc = -ENOENT;
					}
				} else {
					kfree(seg);
					rc = -ENOENT;
			        }
				goto out;
                        }
			if (dcss_diag(DCSS_LOADNOLY, seg->dcss_name,
                                      &seg->start_addr, &seg->end) == 0) {
				if (seg->end < max_low_pfn*PAGE_SIZE ) {
		                        atomic_set(&seg->ref_count, 1);
					list_add(&seg->list, &dcss_list);
					*addr = seg->start_addr;
					*end = seg->end;
					seg->dcss_attr = rc;
					seg->shared_attr = rc;
				} else {
					dcss_diag(DCSS_PURGESEG, seg->dcss_name, &dummy, &dummy);
					kfree (seg);
					rc = -ENOENT;
				}
                        } else {
                                kfree(seg);
                                rc = -ENOENT;
                        }
                } else rc = -ENOMEM;
        } else {
		/* found */
		if ((segtype == SEGMENT_EXCLUSIVE_RW) && (seg->dcss_attr != SEGMENT_EXCLUSIVE_RW)) {
			PRINT_ERR("Segment already loaded in other mode than EXCLUSIVE_RW!\n");
			rc = -EPERM;
			goto out;
			/* reload segment in exclusive mode */
/*			dcss_diag(DCSS_LOADNSR, seg->dcss_name,
				  &seg->start_addr, &seg->end);
			seg->dcss_attr = SEGMENT_EXCLUSIVE_RW;*/
		}
		if ((segtype != SEGMENT_EXCLUSIVE_RW) && (seg->dcss_attr == SEGMENT_EXCLUSIVE_RW)) {
			PRINT_ERR("Segment already loaded in EXCLUSIVE_RW mode!\n");
			rc = -EPERM;
			goto out;
		}
                atomic_inc(&seg->ref_count);
                *addr = seg->start_addr;
                *end = seg->end;
                rc = seg->dcss_attr;
        }
out:
        spin_unlock(&dcss_lock);
        return rc;
}

/*
 * Decrease the use count of a DCSS segment and remove
 * it from the address space if nobody is using it
 * any longer.
 */
void segment_unload(char *name)
{
        char dcss_name[8];
        unsigned long dummy;
        struct list_head *l,*l_tmp;
        struct dcss_segment *seg;

        if (!MACHINE_IS_VM)
                return;
        dcss_mkname(name, dcss_name);
        spin_lock(&dcss_lock);
        list_for_each_safe(l, l_tmp, &dcss_list) {
                seg = list_entry(l, struct dcss_segment, list);
                if (memcmp(seg->dcss_name, dcss_name, 8) == 0) {
                        if (atomic_dec_return(&seg->ref_count) == 0) {
                                /* Last user of the segment is
                                   gone. */
                                list_del(&seg->list);
                                dcss_diag(DCSS_PURGESEG, seg->dcss_name,
                                          &dummy, &dummy);
				kfree(seg);
                        }
                        break;
                }
        }
        spin_unlock(&dcss_lock);
}

/*
 * Replace an existing DCSS segment, so that machines
 * that load it anew will see the new version.
 */
void segment_replace(char *name)
{
        char dcss_name[8];
        struct list_head *l;
        struct dcss_segment *seg;
        int mybeg = 0;
        int myend = 0;
        char mybuff1[80];
        char mybuff2[80];

        if (!MACHINE_IS_VM)
                return;
        dcss_mkname(name, dcss_name);

        memset (mybuff1, 0, sizeof(mybuff1));
        memset (mybuff2, 0, sizeof(mybuff2));

        spin_lock(&dcss_lock);
        list_for_each(l, &dcss_list) {
                seg = list_entry(l, struct dcss_segment, list);
                if (memcmp(seg->dcss_name, dcss_name, 8) == 0) {
                        mybeg = seg->start_addr >> 12;
                        myend = (seg->end) >> 12;
                        if (seg->shared_attr == SEGMENT_EXCLUSIVE_RW)
                                sprintf(mybuff1, "DEFSEG %s %X-%X EW",
                                        name, mybeg, myend);
                        if (seg->shared_attr == SEGMENT_EXCLUSIVE_RO)
                                sprintf(mybuff1, "DEFSEG %s %X-%X RO",
                                        name, mybeg, myend);
                        if (seg->shared_attr == SEGMENT_SHARED_RW)
                                sprintf(mybuff1, "DEFSEG %s %X-%X SW",
                                        name, mybeg, myend);
                        if (seg->shared_attr == SEGMENT_SHARED_RO)
                                sprintf(mybuff1, "DEFSEG %s %X-%X SR",
                                        name, mybeg, myend);
                        spin_unlock(&dcss_lock);
                        sprintf(mybuff2, "SAVESEG %s", name);
                        cpcmd(mybuff1, NULL, 80);
                        cpcmd(mybuff2, NULL, 80);
                        break;
                }

        }
        if (myend == 0) spin_unlock(&dcss_lock);
}

EXPORT_SYMBOL(segment_load);
EXPORT_SYMBOL(segment_unload);
EXPORT_SYMBOL(segment_replace);
