/*
 *  drivers/s390/net/iucv.c
 *    Support for VM IUCV functions for use by other part of the
 *    kernel or loadable modules.
 *
 *  S390 version
 *    Copyright (C) 2000 IBM Corporation
 *    Author(s): Xenia Tkatschow (xenia@us.ibm.com)
 *               Alan Altmark (Alan_Altmark@us.ibm.com)
 */

#include <linux/module.h>
#include <linux/version.h>
#include <linux/spinlock.h>
#include <linux/kernel.h>
#include <linux/malloc.h>
#include <linux/init.h>
#include <linux/tqueue.h>
#include <linux/interrupt.h>
#include <asm/atomic.h>
#include "iucv.h"
#include <asm/io.h>
#include <asm/s390_ext.h>
#include <asm/ebcdic.h>

#undef KERN_DEBUG
#define KERN_DEBUG KERN_EMERG
//#define DEBUG3
//#define DEBUG         /* Turns Printk's on                         */
//#define DEBUG2        /* This prints the parameter list before and */
		      /* after the b2f0 call to cp                 */
#undef NULL
#define NULL 0
#define ADDED_STOR 64		/* ADDITIONAL STORAGE FOR PATHID @'S */
ulong declare_flag = 0;
static uchar iucv_external_int_buffer[40];
struct tq_struct short_task;	/* automatically initialized to zero */
static iucv_interrupt_ops_t my_ops;
spinlock_t lock = SPIN_LOCK_UNLOCKED;

static void do_int (iucv_ConnectionPending *);

/***************INTERRUPT HANDLING DEFINITIONS***************/
typedef struct _iucv_packet {
	struct _iucv_packet *next;
	uchar data[40];
} iucv_packet;
struct tq_struct short_task;
static spinlock_t iucv_packets_lock = SPIN_LOCK_UNLOCKED;
iucv_packet *iucv_packets_head, *iucv_packets_tail;

static atomic_t bh_scheduled = ATOMIC_INIT (0);
void bottom_half_interrupt (void);

/************FUNCTION ID'S****************************/
#define accept          10
#define connect         11
#define declare_buffer  12
#define purge           9
#define query           0
#define quiesc          13
#define receive         5
#define reject          8
#define reply           6
#define resume          14
#define retrieve_buffer 2
#define send            4
#define setmask         16
#define sever           15

/*****************************************************************/
/*  Structure: handler                                           */
/*  members: next - is a pointer to next handler on chain        */
/*           prev - is a pointer to prev handler on chain        */
/*           vmid - 8 char array of machine identification       */
/*           user_data - 16 char array for user identification   */
/*           mask - 24 char array used to compare the 2 previous */
/*           interrupt_table - functions for interrupts          */
/*           start - pointer to start of block of pointers to    */
/*                   handler_table_entries                       */
/*           end - pointer to end of block of pointers to        */
/*                 handler_table_entries                         */
/*           size - ulong, size of block                         */
/*           pgm_data - ulong, program data                      */
/* NOTE: Keep vmid and user_data together in this order          */
/*****************************************************************/
typedef struct {
	ulong *next;
	ulong *prev;
	uchar vmid[8];
	uchar user_data[16];
	uchar mask[24];
	iucv_interrupt_ops_t *interrupt_table;
	ulong *start;
	ulong *end;
	ulong size;
	ulong pgm_data;
} handler;

/*******************************************************************/
/* Structure: handler_table_entry                                  */
/* members: addrs - pointer to a handler                           */
/*          pathid - ushort containing path identification         */
/*          pgm_data - ulong, program data                         */
/*******************************************************************/
typedef struct {
	handler *addrs;
	ushort pathid;
	ulong pgm_data;
} handler_table_entry;

/* main_table: array of pointers to handler_tables         */
static handler_table_entry *main_table[128];
/* handler_anchor: points to first handler on chain        */
static handler *handler_anchor;

/****************FIVE  STRUCTURES************************************/
/* Data struct 1: iparml_control                                    */
/*                Used for iucv_accept                              */
/*                         iucv_connect                             */
/*                         iucv_quiesce                             */
/*                         iucv_resume                              */
/*                         iucv_sever                               */
/*                         iucv_retrieve_buffer                     */
/* Data struct 2: iparml_dpl  (data in parameter list)              */
/*                Used for iucv_send_prmmsg                         */
/*                         iucv_send2way_prmmsg                     */
/*                         iucv_send2way_prmmsg_array               */
/*                         iucv_reply_prmmsg                        */
/* Data struct 3: iparml_db    (data in a buffer)                   */
/*                Used for iucv_receive                             */
/*                         iucv_receive_array                       */
/*                         iucv_receive_simple                      */
/*                         iucv_reject                              */
/*                         iucv_reply                               */
/*                         iucv_reply_array                         */
/*                         iucv_send                                */
/*                         iucv_send_simple                         */
/*                         iucv_send_array                          */
/*                         iucv_send2way                            */
/*                         iucv_send2way_array                      */
/*                         iucv_declare_buffer                      */
/* Data struct 4: iparml_purge                                      */
/*                Used for iucv_purge                               */
/*                         iucv_query                               */
/* Data struct 5: iparml_set_mask                                   */
/*                Used for iucv_set_mask                            */
/********************************************************************/
typedef struct {
	ushort ippathid;
	uchar ipflags1;
	uchar iprcode;
	ushort ipmsglim;
	ushort res1;
	uchar ipvmid[8];
	uchar ipuser[16];
	uchar iptarget[8];
} iparml_control;

/******************/
typedef struct {
	ushort ippathid;
	uchar ipflags1;
	uchar iprcode;
	ulong ipmsgid;
	ulong iptrgcls;
	uchar iprmmsg[8];
	ulong ipsrccls;
	ulong ipmsgtag;
	ulong ipbfadr2;
	ulong ipbfln2f;
	ulong res;
} iparml_dpl;

/*******************/
typedef struct {
	ushort ippathid;
	uchar ipflags1;
	uchar iprcode;
	ulong ipmsgid;
	ulong iptrgcls;
	ulong ipbfadr1;
	ulong ipbfln1f;
	ulong ipsrccls;
	ulong ipmsgtag;
	ulong ipbfadr2;
	ulong ipbfln2f;
	ulong res;
} iparml_db;

/********************/
typedef struct {
	ushort ippathid;
	uchar ipflags1;
	uchar iprcode;
	ulong ipmsgid;
	uchar ipaudit[4];
	uchar res1[4];
	ulong res2;
	ulong ipsrccls;
	ulong ipmsgtag;
	ulong res3[3];
} iparml_purge;

/*******************/
typedef struct {
	uchar ipmask;
	uchar res1[2];
	uchar iprcode;
	ulong res2[9];
} iparml_set_mask;

/*********************INTERNAL FUNCTIONS*****************************/
/********************************************************************/
/* Name: b2f0                                                       */
/* Purpose: this function calls cp to execute iucv commands.        */
/* Input: code - int, identifier of iucv call to cp.                */
/*        parm - void *, pointer to 40 byte iparml area passed to cp */
/* Output: iprcode- return code from iucv call to cp                */
/********************************************************************/
/* Assembler code performing iucv call                             */
/*******************************************************************/
inline ulong
b2f0 (int code, void *parm)
{
	uchar *iprcode;		/* used to extract iprcode */
#ifdef DEBUG2
	int i;
	uchar *prt_parm;
	prt_parm = (uchar *) (parm);
	printk (KERN_DEBUG "parameter list before b2f0 call\n");
	for (i = 0; i < 40; i++)
		printk (KERN_DEBUG "%02x ", prt_parm[i]);
	printk (KERN_DEBUG "\n");
#endif
	asm volatile ("LRA   1,0(%1)\n\t"
		      "LR    0,%0\n\t"
		      ".long 0xb2f01000"
		      : : "d" (code), "a" (parm) : "0", "1");
#ifdef DEBUG2
	printk (KERN_DEBUG "parameter list after b2f0 call\n");
	for (i = 0; i < 40; i++)
		printk (KERN_DEBUG "%02x ", prt_parm[i]);
	printk (KERN_DEBUG "\n");
#endif
	iprcode = (uchar *) (parm + 3);
	return (ulong) (*iprcode);
}

/**************************************************************/
/* Name: iucv_retrieve_buffer                                 */
/* Purpose: terminates all use of iucv                        */
/* Input: void                                                */
/* Output: Return code from CP                                */
/**************************************************************/
int
iucv_retrieve_buffer (void)
{
	iparml_control parm;
	ulong b2f0_result;
#ifdef DEBUG
	printk (KERN_DEBUG "entering iucv_retrieve_buffer\n");
#endif
	memset (&(parm), 0, sizeof (parm));
	b2f0_result = b2f0 (retrieve_buffer, &parm);
	if (b2f0_result == NULL)
		declare_flag = 0;
#ifdef DEBUG
	printk (KERN_DEBUG "exiting iucv_retrieve_buffer\n");
#endif
	return b2f0_result;
}

/**************************************************************/
/* Name: iucv_declare_buffer                                  */
/* Purpose: specifies the guests real address of an external  */
/*          interrupt.                                        */
/* Input: bfr - pointer to  buffer                            */
/* Output: iprcode - return code from b2f0 call               */
/* Note : See output options for b2f0 call                    */
/**************************************************************/
int
iucv_declare_buffer (uchar * bfr)
{
	iparml_db parm;
	ulong b2f0_result;
#ifdef DEBUG
	printk (KERN_DEBUG "Entering iucv_declare_buffer\n");
#endif
	memset (&(parm), 0, sizeof (parm));
	parm.ipbfadr1 = virt_to_phys (bfr);
	b2f0_result = b2f0 (declare_buffer, &parm);
#ifdef DEBUG
	printk (KERN_DEBUG "Address of EIB = %p\n", bfr);
	printk (KERN_DEBUG "Exiting iucv_declare_buffer\n");
#endif
	return b2f0_result;
}

/**************************************************************/
/* Name: add_pathid                                           */
/* Purpose: adds a path id to the system                      */
/* Input: pathid - ushort, pathid to enter system             */
/*        handle - iucv_handle_t, address of handler to add to */
/*        pgm_data - ulong, pathid identifier.                */
/* Output: 0: successful addition of pathid                   */
/**************************************************************/
int
add_pathid (ushort pathid, iucv_handle_t handle, ulong pgm_data)
{
	ulong index1, index2;	/* index1 into main_table */
	ulong add_flag = 0;
	ulong old_size = 0, new_size = 0;
	uchar *to, *from;	/* pointer for copying the table */
	handler_table_entry *P = 0;	/*P is a pointer to H_T_E */
	handler *Q = 0;		/*Q is a pointer to handler */
	ulong *X = 0;		/*Points to array of pointers */
#ifdef DEBUG
	printk (KERN_DEBUG "entering add_pathid\n");
#endif
	Q = (handler *) handle;	/* Q points to a handler    */
	/*
	 * main_table has 128 entries.
	 * 128*512 = 65536 maximum number of pathid's allowed
	 */
	index1 = ((ulong) pathid) / 512;
	index2 = ((ulong) pathid) % 512;
#ifdef DEBUG
	printk (KERN_DEBUG "index1 = %d\n ", (int) index1);
	printk (KERN_DEBUG "index2 = %d\n ", (int) index2);
	printk (KERN_DEBUG "Q is pointing to %p ", Q);
#endif
	spin_lock (&lock);
	/*
	 * If NULL then handler table does not exist and need to get storage
	 *  and have main_table[index1] point to it
	 * If allocating storage failed, return
	 */
	if (main_table[index1] == NULL) {
		main_table[index1] = (handler_table_entry *) kmalloc
		    (512 * sizeof (handler_table_entry), GFP_KERNEL);
		if (main_table[index1] == NULL) {
			spin_unlock (&lock);
			return -ENOBUFS;
		}
		memset (main_table[index1], 0,
			512 * sizeof (handler_table_entry));
#ifdef DEBUG
		printk (KERN_DEBUG "address of table H_T is %p \n",
			main_table[index1]);
#endif
	}
	/*
	 * P points to a handler table entry (H_T_E) in which all entries in
	 * that structure should be NULL. If they're not NULL, then there
	 * is a bad pointer and it will return(-2) immediately, otherwise
	 * data will be entered into H_T_E.
	 */
	P = main_table[index1];
	if ((P + index2)->addrs) {
#ifdef DEBUG
		printk (KERN_DEBUG "main_table[index1] = %p \n",
			main_table[index1]);
		printk (KERN_DEBUG "P+index2 = %p \n", P + index2);
		printk (KERN_DEBUG "(P+index2)->addrs is %p \n",
			(P + index2)->addrs);
#endif
		spin_unlock (&lock);
		printk (KERN_DEBUG "bad pointer1\n");
		return (-2);
	}
	(P + index2)->addrs = handle;
	/*
	 * checking if address of handle is valid, if it's not valid,
	 * unlock the lock and return(-2) immediately.
	 */
	if ((P + index2)->addrs == NULL) {
		spin_unlock (&lock);
		printk (KERN_DEBUG "bad pointer2\n");
		return (-2);
	}
	(P + index2)->pathid = pathid;
	if (pgm_data)
		(P + index2)->pgm_data = pgm_data;
	else
		(P + index2)->pgm_data = Q->pgm_data;
	/*
	 * Step thru the table of addresses of pathid's to find the first
	 * available entry (NULL). If an entry is found, add the pathid,
	 * unlock and exit. If an available entry is not found, allocate a
	 * new, larger table, copy over the old table and deallocate the
	 * old table and add the pathid.
	 */
#ifdef DEBUG
	printk (KERN_DEBUG "address of handle is %p\n", handle);
	printk (KERN_DEBUG "&(Q->start) is %p\n", &(Q->start));
	printk (KERN_DEBUG "&(Q->end) is %p\n", &(Q->end));
	printk (KERN_DEBUG "start of pathid table is %p\n", (Q->start));
	printk (KERN_DEBUG "end of pathid table is %p\n", (Q->end));
	for (X = (Q->start); X < (Q->end); X++)
		printk (KERN_DEBUG "X = %p ", X);
	printk (KERN_DEBUG "\n");
#endif
	for (X = (Q->start); X < (Q->end); X++) {
		if (*X == NULL) {
#ifdef DEBUG
			printk (KERN_DEBUG "adding pathid, %p = P+index2\n",
				(P + index2));
#endif
			*X = (ulong) (P + index2);
			add_flag = 1;
		}
		if (add_flag == 1)
			break;
	}
	if (add_flag == 0) {	/* element not added to list */
		X = Q->start;
		old_size = Q->size;
		new_size = old_size + ADDED_STOR;	/* size of new table */
		from = (uchar *) (Q->start);	/* address of old table */
		(*Q).start = kmalloc (new_size * sizeof (ulong), GFP_KERNEL);
		if ((Q->start) == NULL) {
			spin_unlock (&lock);
			return -ENOBUFS;
		}
		memset ((*Q).start, 0, new_size * sizeof (ulong));
		to = (uchar *) (Q->start);	/* address of new table */
		/* copy old table to new  */
		memcpy (to, from, old_size * (sizeof (ulong)));
#ifdef DEBUG
		printk (KERN_DEBUG "Getting a new pathid table\n");
		printk (KERN_DEBUG "to is %p \n", to);
		printk (KERN_DEBUG "from is %p \n", from);
#endif
		Q->size = new_size;	/* storing new size of table */
		Q->end = (Q->start) + (Q->size);
		X = Q->start + old_size;	/* next blank in table */
		*X = (ulong) (P + index2);	/* adding element to new table */
#ifdef DEBUG
		printk (KERN_DEBUG "Q->size is %u \n", (int) (Q->size));
		printk (KERN_DEBUG "Q->end is %p \n", Q->end);
		printk (KERN_DEBUG "Q->start is %p \n", Q->start);
		printk (KERN_DEBUG "X is %p \n", X);
		printk (KERN_DEBUG "*X is %u \n", (int) (*X));
#endif
		kfree (from);	/* free old table */
	}
	spin_unlock (&lock);
#ifdef DEBUG
	printk (KERN_DEBUG "exiting add_pathid\n");
#endif
	return (0);
}				/* end of add_pathid function */

/***********************EXTERNAL FUNCTIONS***************************/
/**************************************************************/
/* Name: iucv_query                                           */
/* Purpose: determines how large an external interrupt buffer */
/*          IUCV requires to store information                */
/* Input : bufsize - ulong: size of interrupt buffer          */
/*         - filled in by function and returned to caller     */
/*         conmax  - ulong: maximum number of connections that */
/*           can be outstanding for this VM                   */
/*         - filled in by function and returned to caller     */
/* Output: void                                               */
/**************************************************************/
void
iucv_query (ulong * bufsize, ulong * conmax)
{
	iparml_purge parm;	/* DOESN'T MATTER WHICH IPARML IS USED    */
#ifdef DEBUG
	printk (KERN_DEBUG "entering iucv_purge\n");
#endif
	memset (&(parm), 0, sizeof (parm));
	/*
	 * Assembler instruction calling b2f0  and storing R0 and R1
	 */
	asm volatile ("LRA   1,0(%3)\n\t"
		      "LR    0,%2\n\t"
		      ".long 0xb2f01000\n\t"
		      "ST    0,%0\n\t"
		      "ST    1,%1\n\t":"=m" (*bufsize),
		      "=m" (*conmax):"d" (query), "a" (&parm):"0", "1");
	return;
}

/**************************************************************/
/* Name: iucv_purge                                           */
/* Purpose: cancels a message you have sent                   */
/* Input: pathid - ushort, pathid                             */
/*        msgid  - ulong, mid of message                      */
/*        srccls - ulong, sourse message class                */
/*        audit  - uchar[4], info about ansync. error condit. */
/*                 filled in by function and passed back      */
/* Output: void                                               */
/* NOTE: pathid is required, flag is always turned on         */
/**************************************************************/
int
iucv_purge (ulong msgid, ushort pathid, ulong srccls, uchar audit[4])
{
	iparml_purge parm;
	ulong b2f0_result;
#ifdef DEBUG
	printk (KERN_DEBUG "entering iucv_purge\n");
#endif
	memset (&(parm), 0, sizeof (parm));
	parm.ipmsgid = msgid;
	parm.ippathid = pathid;
	parm.ipsrccls = srccls;
	parm.ipflags1 |= specify_pathid;	/* pathid id flag */
	if (parm.ipmsgid)
		parm.ipflags1 |= specify_msgid;
	if (parm.ipsrccls)
		parm.ipflags1 |= source_class;
	b2f0_result = b2f0 (purge, &parm);
	if (b2f0_result != NULL)
		return b2f0_result;
	memcpy (audit, parm.ipaudit, 4);
#ifdef DEBUG
	printk (KERN_DEBUG "exiting iucv_purge\n");
#endif
	return b2f0_result;
}

/**************************************************************/
/* Name: iucv_quiesce                                         */
/* Purpose: temporarily suspends incoming messages            */
/* Input: pathid - ushort, pathid                             */
/*        user_data - uchar[16], user id                      */
/* Output: iprcode - return code from b2f0 call               */
/* NOTE: see b2f0 output list                                 */
/**************************************************************/
int
iucv_quiesce (ushort pathid, uchar user_data[16])
{
	iparml_control parm;
	ulong b2f0_result;
#ifdef DEBUG
	printk (KERN_DEBUG "entering iucv_quiesce\n");
#endif
	memset (&(parm), 0, sizeof (parm));
	memcpy (parm.ipuser, user_data, 16);
	parm.ippathid = pathid;
	b2f0_result = b2f0 (quiesc, &parm);
#ifdef DEBUG
	printk (KERN_DEBUG "exiting iucv_quiesce\n");
#endif
	return b2f0_result;
}

/**************************************************************/
/* Name: iucv_resume                                          */
/* Purpose: restores communication over a quiesced path       */
/* Input: pathid - ushort, pathid                             */
/*        user_data - uchar[16], user id                      */
/* Output: iprcode - return code from b2f0 call               */
/* NOTE: see b2f0 output list                                 */
/**************************************************************/
int
iucv_resume (ushort pathid, uchar user_data[16])
{
	iparml_control parm;
	ulong b2f0_result;
#ifdef DEBUG
	printk (KERN_DEBUG "entering iucv_resume\n");
#endif
	memset (&(parm), 0, sizeof (parm));
	memcpy (parm.ipuser, user_data, 16);
	parm.ippathid = pathid;
	b2f0_result = b2f0 (resume, &parm);
#ifdef DEBUG
	printk (KERN_DEBUG "exiting iucv_resume\n");
#endif
	return b2f0_result;
}

/**************************************************************/
/* Name: iucv_reject                                          */
/* Purpose: rejects a message                                 */
/* Input: pathid - ushort, pathid                             */
/*        msgid  - ulong, mid of message                      */
/*        trgcls - ulong, target message class                */
/* Output: iprcode - return code from b2f0 call               */
/* NOTE: pathid is required field, flag always turned on      */
/*       RESTRICTION: target class cannot be zero             */
/* NOTE: see b2f0 output list                                 */
/**************************************************************/
int
iucv_reject (ushort pathid, ulong msgid, ulong trgcls)
{
	iparml_db parm;
	ulong b2f0_result;
#ifdef DEBUG
	printk (KERN_DEBUG "entering iucv_reject\n");
#endif
	memset (&(parm), 0, sizeof (parm));
	parm.ipmsgid = msgid;
	parm.ippathid = pathid;
	parm.iptrgcls = trgcls;
	parm.ipflags1 |= specify_pathid;	/* flag for pathid */
	if (parm.ipmsgid)
		parm.ipflags1 |= specify_msgid;
	if (parm.iptrgcls)
		parm.ipflags1 |= target_class;
	b2f0_result = b2f0 (reject, &parm);
#ifdef DEBUG
	printk (KERN_DEBUG "exiting iucv_reject\n");
#endif
	return b2f0_result;
}

/**************************************************************/
/* Name: iucv_setmask                                         */
/* Purpose: enables or disables certain iucv external interr. */
/* Input: non_priority_interrupts - uchar                     */
/*        priority_interrupts - uchar                         */
/*        non_priority_completion_interrupts - uchar          */
/*        priority_completion_interrupts) - uchar             */
/* Output: iprcode - return code from b2f0 call               */
/* NOTE: see b2f0 output list                                 */
/**************************************************************/
int
iucv_setmask (uchar non_priority_interrupts,
	      uchar priority_interrupts,
	      uchar non_priority_completion_interrupts,
	      uchar priority_completion_interrupts)
{
	iparml_set_mask parm;
	ulong b2f0_result;
#ifdef DEBUG
	printk (KERN_DEBUG "entering iucv_setmask\n");
#endif
	memset (&(parm), 0, sizeof (parm));
	if (non_priority_interrupts)
		parm.ipmask |= 0x80;
	if (priority_interrupts)
		parm.ipmask |= 0x40;
	if (non_priority_completion_interrupts)
		parm.ipmask |= 0x20;
	if (priority_completion_interrupts)
		parm.ipmask |= 0x10;
	b2f0_result = b2f0 (setmask, &parm);
#ifdef DEBUG
	printk (KERN_DEBUG "exiting iucv_setmask\n");
#endif
	return b2f0_result;
}

/**************************************************************/
/* Name: iucv_sever                                           */
/* Purpose: terminates an iucv path to another machine        */
/* Input: pathid - ushort, pathid                             */
/*        user_data - uchar[16], user id                      */
/* Output: iprcode - return code from b2f0 call               */
/* NOTE: see b2f0 output list                                 */
/**************************************************************/
int
iucv_sever (ushort pathid, uchar user_data[16])
{
	ulong index1, index2;
	ulong b2f0_result;
	handler_table_entry *P = 0;
	handler *Q = 0;
	ulong *X;
	iparml_control parm;
#ifdef DEBUG
	printk (KERN_DEBUG "entering iucv_sever\n");
#endif
	memset (&(parm), 0, sizeof (parm));
	memcpy (parm.ipuser, user_data, 16);
	parm.ippathid = pathid;
	b2f0_result = b2f0 (sever, &parm);
	if (b2f0_result)
		return b2f0_result;
	index1 = ((ulong) pathid) / 512;
	index2 = ((ulong) pathid) % 512;
	spin_lock (&lock);
	P = main_table[index1];
	if (((P + index2)->addrs) == NULL) {	/* called from interrupt code */
		spin_unlock (&lock);
		return (-2);	/* bad pointer */
	}
	Q = (*(P + index2)).addrs;
#ifdef DEBUG
	printk (KERN_DEBUG "pathid is %d\n", pathid);
	printk (KERN_DEBUG "index1 is %d\n", (int) index1);
	printk (KERN_DEBUG "index2 is %d\n", (int) index2);
	printk (KERN_DEBUG "H_T_E is %p\n", P);
	printk (KERN_DEBUG "address of handler is %p\n", Q);
	for (X = ((*Q).start); X < ((*Q).end); X++)
		printk (KERN_DEBUG " %x ", (int) (*X));
	printk (KERN_DEBUG "\n above is pathid table\n");
#endif
/********************************************************************/
/* Searching the pathid address table for matching address, once    */
/* found, NULL the field. Then Null the H_T_E fields.               */
/********************************************************************/
	for (X = ((*Q).start); X < ((*Q).end); X++)
		if (*X == (ulong) (P + index2)) {
#ifdef DEBUG
			printk (KERN_DEBUG "found a path to sever\n");
			printk (KERN_DEBUG "severing %d \n", (int) (*X));
#endif
			*X = NULL;
			(*(P + index2)).addrs = NULL;	/*clearing the fields */
			(*(P + index2)).pathid = 0;
			(*(P + index2)).pgm_data = 0;
		}
	spin_unlock (&lock);
#ifdef DEBUG
	printk (KERN_DEBUG "exiting iucv_sever\n");
#endif
	return b2f0_result;
}

/**************************************************************/
/* Name: iucv_receive                                         */
/* Purpose: receives incoming message                         */
/* Input: pathid - ushort, pathid                             */
/*        msgid  - *ulong, mid of message                     */
/*        trgcls - *ulong, target message class               */
/*        buffer - pointer of buffer                          */
/*        buflen - length of buffer                           */
/*        adds_curr_buffer - pointer to updated buffer address*/
/*                           to write to                      */
/*        adds_curr_length - pointer to updated length in     */
/*                           buffer available to write to     */
/*        reply_required - uchar *, flag                      */
/*        priority_msg - uchar *, flag                        */
/* Output: iprcode - return code from b2f0 call               */
/* NOTE: pathid must be specified, flag being turned on       */
/* RESTRICTIONS: target class CANNOT be zero because the code */
/* checks for a non-NULL value to turn flag on, therefore if  */
/* target class = zero, flag will not be turned on.           */
/**************************************************************/
int
iucv_receive (ushort pathid, ulong * msgid, ulong * trgcls,
	      void *buffer, ulong buflen,
	      uchar * reply_required,
	      uchar * priority_msg,
	      ulong * adds_curr_buffer, ulong * adds_curr_length)
{
	iparml_db parm;
	ulong b2f0_result;
#ifdef DEBUG
	printk (KERN_DEBUG "entering iucv_receive\n");
#endif
	memset (&(parm), 0, sizeof (parm));
	parm.ipmsgid = *msgid;
	parm.ippathid = pathid;
	parm.iptrgcls = *trgcls;
	parm.ipflags1 |= specify_pathid;	/* turning pathid flag */
	if (parm.ipmsgid)
		parm.ipflags1 |= 0x05;
	if (parm.iptrgcls)
		parm.ipflags1 |= target_class;
	parm.ipbfadr1 = (ulong) buffer;
	parm.ipbfln1f = buflen;
	b2f0_result = b2f0 (receive, &parm);
	if (b2f0_result)
		return b2f0_result;
	if (msgid)
		*msgid = parm.ipmsgid;
	if (trgcls)
		*trgcls = parm.iptrgcls;
	if (parm.ipflags1 & prior_msg)
		if (priority_msg)
			*priority_msg = 0x01;	/*yes, priority msg */
	if (!(parm.ipflags1 & 0x10))	/*& with X'10'     */
		if (reply_required)
			*reply_required = 0x01;	/*yes, reply required */
	if (!(parm.ipflags1 & parm_data)) {	/*msg not in parmlist */
		if (adds_curr_length)
			*adds_curr_length = parm.ipbfln1f;
		if (adds_curr_buffer)
			*adds_curr_buffer = parm.ipbfadr1;
	} else {
		if ((buflen) >= 8) {
			if (buffer)
				memcpy ((char *) buffer,
					(char *) parm.ipbfadr1, 8);
			if (adds_curr_length)
				*adds_curr_length = ((buflen) - 8);
			if (adds_curr_buffer)
				*adds_curr_buffer = (ulong) buffer + 8;
		} else {
			parm.iprcode |= 0x05;
			b2f0_result = (ulong) parm.iprcode;
		}
	}
#ifdef DEBUG
	printk (KERN_DEBUG "exiting iucv_receive\n");
#endif
	return b2f0_result;
}

/**************************************************************/
/* Name: iucv_receive_simple                                  */
/* Purpose: receives fully-qualified message                  */
/* Input: pathid - ushort, pathid                             */
/*        msgid  - ulong, id of message                       */
/*        trgcls - ulong, target message class                */
/*        buffer - pointer of buffer                          */
/*        buflen - length of buffer                           */
/* Output: iprcode - return code from b2f0 call               */
/**************************************************************/
int
iucv_receive_simple (ushort pathid, ulong msgid, ulong trgcls,
		     void *buffer, ulong buflen)
{
	iparml_db parm;
	ulong b2f0_result;
	pr_debug ("entering iucv_receive_simple\n");

	memset (&(parm), 0, sizeof (parm));
	parm.ipmsgid = msgid;
	parm.ippathid = pathid;
	parm.iptrgcls = trgcls;
	parm.ipflags1 = IPFGMID + IPFGPID + IPFGMCL;
	parm.ipbfadr1 = (ulong) buffer;
	parm.ipbfln1f = buflen;

	b2f0_result = b2f0 (receive, &parm);
	if (b2f0_result)
		return b2f0_result;

	if (parm.ipflags1 & IPRMDATA) {	/*msg in parmlist */
		if ((buflen) >= 8)
			memcpy ((char *) buffer, (char *) parm.ipbfadr1, 8);
		else
			b2f0_result = 5;
	}
	pr_debug ("exiting iucv_receive_simple\n");
	return b2f0_result;
}

/**************************************************************/
/* Name: iucv_receive_array                                   */
/* Purpose: receives incoming message                         */
/* Input: pathid - ushort, pathid                             */
/*        msgid  -* ulong, mid of message                     */
/*        trgcls -* ulong, target message class               */
/*        buffer - pointer of iucv_array_t                    */
/*        buflen - ulong , length of buffer                   */
/*        reply_required - uchar *, flag returned to caller   */
/*        priority_msg - uchar *, flag returned to caller     */
/*        adds_curr_buffer - pointer to updated buffer array  */
/*                   to write to                              */
/*        adds_curr_length - pointer to updated length in     */
/*                   buffer available to write to             */
/* Output: iprcode - return code from b2f0 call               */
/* NOTE: pathid must be specified, flag being turned on       */
/* RESTRICTIONS: target class CANNOT be zero because the code */
/* checks for a non-NULL value to turn flag on, therefore if  */
/* target class = if target class = zero flag will not be     */
/* turned on, therefore if target class is specified it cannot */
/* be zero.                                                   */
/**************************************************************/
int
iucv_receive_array (ushort pathid, ulong * msgid, ulong * trgcls,
		    iucv_array_t * buffer, ulong * buflen,
		    uchar * reply_required,
		    uchar * priority_msg,
		    ulong * adds_curr_buffer, ulong * adds_curr_length)
{
	iparml_db parm;
	ulong b2f0_result;
#ifdef DEBUG
	printk (KERN_DEBUG "entering iucv_receive_array\n");
#endif
	memset (&(parm), 0, sizeof (parm));
	parm.ipmsgid = *msgid;
	parm.ippathid = pathid;
	parm.iptrgcls = *trgcls;
	parm.ipflags1 |= array;	/* using an address list  */
	parm.ipflags1 |= specify_pathid;	/*turning on pathid flag */
	if (parm.ipmsgid)
		parm.ipflags1 |= 0x05;
	if (parm.iptrgcls)
		parm.ipflags1 |= target_class;
	parm.ipbfadr1 = (ulong) buffer;
	parm.ipbfln1f = *buflen;
	b2f0_result = b2f0 (receive, &parm);
	if (b2f0_result)
		return b2f0_result;
	if (msgid)
		*msgid = parm.ipmsgid;
	if (trgcls)
		*trgcls = parm.iptrgcls;
	if (parm.ipflags1 & prior_msg)
		if (priority_msg)
			*priority_msg = 0x01;	/*yes, priority msg */
	if (!(parm.ipflags1 & 0x10))	/*& with X'10'     */
		if (reply_required)
			*reply_required = 0x01;	/*yes, reply required */
	if (!(parm.ipflags1 & parm_data)) {	/*msg not in parmlist */
		if (adds_curr_length)
			*adds_curr_length = parm.ipbfln1f;
		if (adds_curr_buffer)
			*adds_curr_buffer = parm.ipbfadr1;
	} else {
		if ((buffer->length) >= 8) {
			memcpy ((char *) buffer->address,
				(char *) parm.ipbfadr1, 8);
			if (adds_curr_buffer)
				*adds_curr_buffer =
				    (ulong) ((buffer->address) + 8);
			if (adds_curr_length)
				*adds_curr_length = ((buffer->length) - 8);

		} else {
			parm.iprcode |= 0x05;
			b2f0_result = (ulong) parm.iprcode;
		}
	}
#ifdef DEBUG
	printk (KERN_DEBUG "exiting iucv_receive\n");
#endif
	return b2f0_result;
}

/**************************************************************/
/* Name: iucv_send                                            */
/* Purpose: sends messages                                    */
/* Input: pathid - ushort, pathid                             */
/*        msgid  - ulong *, id of message returned to caller  */
/*        trgcls - ulong, target message class                */
/*        srccls - ulong, source message class                */
/*        msgtag - ulong, message tag                         */
/*        priority_msg - uchar, flag                          */
/*        buffer - pointer to buffer                          */
/*        buflen - ulong, length of buffer                    */
/* Output: iprcode - return code from b2f0 call               */
/*         msgid - returns message id                         */
/**************************************************************/
int
iucv_send (ushort pathid, ulong * msgid,
	   ulong trgcls, ulong srccls,
	   ulong msgtag, uchar priority_msg, void *buffer, ulong buflen)
{
	iparml_db parm;
	ulong b2f0_result;
#ifdef DEBUG
	printk (KERN_DEBUG "entering iucv_send\n");
#endif
	memset (&(parm), 0, sizeof (parm));
	parm.ippathid = pathid;
	parm.iptrgcls = trgcls;
	parm.ipbfadr1 = (ulong) buffer;
	parm.ipbfln1f = buflen;	/* length of message */
	parm.ipsrccls = srccls;
	parm.ipmsgtag = msgtag;
	parm.ipflags1 |= one_way_msg;	/* one way message */
	if (priority_msg)
		parm.ipflags1 |= prior_msg;	/* priority message */
	b2f0_result = b2f0 (send, &parm);
	if (b2f0_result)
		return b2f0_result;
	if (msgid)
		*msgid = parm.ipmsgid;
#ifdef DEBUG
	printk (KERN_DEBUG "exiting iucv_send\n");
#endif
	return b2f0_result;
}

/**************************************************************/
/* Name: iucv_send_array                                      */
/* Purpose: sends messages in buffer array                    */
/* Input: pathid - ushort, pathid                             */
/*        msgid  - ulong *, id of message returned to caller  */
/*        trgcls - ulong, target message class                */
/*        srccls - ulong, source message class                */
/*        msgtag - ulong, message tag                         */
/*        priority_msg - uchar, flag                          */
/*        buffer - pointer to iucv_array_t                    */
/*        buflen - ulong, length of buffer                    */
/* Output: iprcode - return code from b2f0 call               */
/*         msgid - returns message id                         */
/**************************************************************/
int
iucv_send_array (ushort pathid, ulong * msgid,
		 ulong trgcls, ulong srccls,
		 ulong msgtag, uchar priority_msg,
		 iucv_array_t * buffer, ulong buflen)
{
	iparml_db parm;
	ulong b2f0_result;
#ifdef DEBUG
	printk (KERN_DEBUG "entering iucv_send_array\n");
#endif
	memset (&(parm), 0, sizeof (parm));
	parm.ippathid = pathid;
	parm.iptrgcls = trgcls;
	parm.ipbfadr1 = (ulong) buffer;
	parm.ipbfln1f = buflen;	/* length of message */
	parm.ipsrccls = srccls;
	parm.ipmsgtag = msgtag;
	parm.ipflags1 |= one_way_msg;	/* one way message */
	parm.ipflags1 |= array;	/* one way w/ array */
	if (priority_msg)
		parm.ipflags1 |= prior_msg;	/* priority message */
	b2f0_result = b2f0 (send, &parm);
	if (msgid)
		*msgid = parm.ipmsgid;
#ifdef DEBUG
	printk (KERN_DEBUG "exiting iucv_send_array\n");
#endif
	return b2f0_result;
}

/**************************************************************/
/* Name: iucv_send_prmmsg                                     */
/* Purpose: sends messages in parameter list                  */
/* Input: pathid - ushort, pathid                             */
/*        msgid  - ulong *, id of message                     */
/*        trgcls - ulong, target message class                */
/*        srccls - ulong, source message class                */
/*        msgtag - ulong, message tag                         */
/*        priority_msg - uchar, flag                          */
/*        prmmsg - uchar[8], message being sent               */
/* Output: iprcode - return code from b2f0 call               */
/*         msgid - returns message id                         */
/**************************************************************/
int
iucv_send_prmmsg (ushort pathid, ulong * msgid,
		  ulong trgcls, ulong srccls,
		  ulong msgtag, uchar priority_msg, uchar prmmsg[8])
{
	iparml_dpl parm;
	ulong b2f0_result;
#ifdef DEBUG
	printk (KERN_DEBUG "entering iucv_send_prmmsg\n");
#endif
	memset (&(parm), 0, sizeof (parm));
	parm.ippathid = pathid;
	parm.iptrgcls = trgcls;
	parm.ipsrccls = srccls;
	parm.ipmsgtag = msgtag;
	parm.ipflags1 |= parm_data;	/* message in prmlist */
	parm.ipflags1 |= one_way_msg;	/* one way message */
	if (priority_msg)
		parm.ipflags1 |= prior_msg;	/* priority message */
	memcpy (parm.iprmmsg, prmmsg, 8);
	b2f0_result = b2f0 (send, &parm);
	if (msgid)
		*msgid = parm.ipmsgid;
#ifdef DEBUG
	printk (KERN_DEBUG "exiting iucv_send_prmmsg\n");
#endif
	return b2f0_result;
}

/**************************************************************/
/* Name: iucv_send2way                                        */
/* Purpose: sends messages in both directions                 */
/* Input: pathid - ushort, pathid                             */
/*        msgid  - ulong *, id of message                     */
/*        trgcls - ulong, target message class                */
/*        srccls - ulong, source message class                */
/*        msgtag - ulong, message tag                         */
/*        priority_msg - uchar, flag                          */
/*        buffer - pointer to buffer                          */
/*        buflen - ulong, length of buffer                    */
/*        ansbuf - pointer to buffer on reply                 */
/*        anslen - length of ansbuf buffer                    */
/* Output: iprcode - return code from b2f0 call               */
/*         msgid - returns message id                         */
/**************************************************************/
int
iucv_send2way (ushort pathid, ulong * msgid,
	       ulong trgcls, ulong srccls,
	       ulong msgtag, uchar priority_msg,
	       void *buffer, ulong buflen, void *ansbuf, ulong anslen)
{
	iparml_db parm;
	ulong b2f0_result;
#ifdef DEBUG
	printk (KERN_DEBUG "entering iucv_send2way\n");
#endif
	memset (&(parm), 0, sizeof (parm));
	parm.ippathid = pathid;
	parm.iptrgcls = trgcls;
	parm.ipbfadr1 = (ulong) buffer;
	parm.ipbfln1f = buflen;	/* length of message */
	parm.ipbfadr2 = (ulong) ansbuf;
	parm.ipbfln2f = anslen;
	parm.ipsrccls = srccls;
	parm.ipmsgtag = msgtag;
	if (priority_msg)
		parm.ipflags1 |= prior_msg;	/* priority message */
	b2f0_result = b2f0 (send, &parm);
	if (msgid)
		*msgid = parm.ipmsgid;
#ifdef DEBUG
	printk (KERN_DEBUG "exiting iucv_send2way\n");
#endif
	return b2f0_result;
}

/**************************************************************/
/* Name: iucv_send2way_array                                  */
/* Purpose: sends messages in both directions in arrays       */
/* Input: pathid - ushort, pathid                             */
/*        msgid  - ulong *, id of message                     */
/*        trgcls - ulong, target message class                */
/*        srccls - ulong, source message class                */
/*        msgtag - ulong, message tag                         */
/*        priority_msg - uchar, flag                          */
/*        buffer - pointer to iucv_array_t                    */
/*        buflen - ulong, length of buffer                    */
/*        ansbuf - pointer to iucv_array_t on reply           */
/*        anslen - length of ansbuf buffer                    */
/* Output: iprcode - return code from b2f0 call               */
/*         msgid - returns message id                         */
/**************************************************************/
int
iucv_send2way_array (ushort pathid, ulong * msgid,
		     ulong trgcls, ulong srccls,
		     ulong msgtag, uchar priority_msg,
		     iucv_array_t * buffer, ulong buflen,
		     iucv_array_t * ansbuf, ulong anslen)
{
	iparml_db parm;
	ulong b2f0_result;
#ifdef DEBUG
	printk (KERN_DEBUG "entering iucv_send2way_array\n");
#endif
	memset (&(parm), 0, sizeof (parm));
	parm.ippathid = pathid;
	parm.iptrgcls = trgcls;
	parm.ipbfadr1 = (ulong) buffer;
	parm.ipbfln1f = buflen;	/* length of message */
	parm.ipbfadr2 = (ulong) ansbuf;
	parm.ipbfln2f = anslen;
	parm.ipsrccls = srccls;
	parm.ipmsgtag = msgtag;
	parm.ipflags1 |= array;	/* send  w/ array  */
	parm.ipflags1 |= reply_array;	/* reply w/ array  */
	if (priority_msg)
		parm.ipflags1 |= prior_msg;	/* priority message */
	b2f0_result = b2f0 (send, &parm);
	if (msgid)
		*msgid = parm.ipmsgid;
#ifdef DEBUG
	printk (KERN_DEBUG "exiting iucv_send2way_array\n");
#endif
	return b2f0_result;
}

/**************************************************************/
/* Name: iucv_send2way_prmmsg                                 */
/* Purpose: sends messages in both directions w/parameter lst */
/* Input: pathid - ushort, pathid                             */
/*        msgid  - ulong *, id of message                     */
/*        trgcls - ulong, target message class                */
/*        srccls - ulong, source message class                */
/*        msgtag - ulong, message tag                         */
/*        priority_msg - uchar, flag                          */
/*        prmmsg - uchar[8], message being sent in parameter  */
/*        ansbuf - pointer to buffer                          */
/*        anslen - length of ansbuf buffer                    */
/* Output: iprcode - return code from b2f0 call               */
/*         msgid - returns message id                         */
/**************************************************************/
int
iucv_send2way_prmmsg (ushort pathid, ulong * msgid,
		      ulong trgcls, ulong srccls,
		      ulong msgtag, uchar priority_msg,
		      uchar prmmsg[8], void *ansbuf, ulong anslen)
{
	iparml_dpl parm;
	ulong b2f0_result;
#ifdef DEBUG
	printk (KERN_DEBUG "entering iucv_send2way_prmmsg\n");
#endif
	memset (&(parm), 0, sizeof (parm));
	parm.ippathid = pathid;
	parm.iptrgcls = trgcls;
	parm.ipsrccls = srccls;
	parm.ipmsgtag = msgtag;
	parm.ipbfadr2 = (ulong) ansbuf;
	parm.ipbfln2f = anslen;
	parm.ipflags1 |= parm_data;	/* message in prmlist */
	if (priority_msg)
		parm.ipflags1 |= prior_msg;	/* priority message */
	memcpy (parm.iprmmsg, prmmsg, 8);
	b2f0_result = b2f0 (send, &parm);
	if (msgid)
		*msgid = parm.ipmsgid;
#ifdef DEBUG
	printk (KERN_DEBUG "exiting iucv_send2way_prmmsg\n");
#endif
	return b2f0_result;
}

/**************************************************************/
/* Name: iucv_reply                                           */
/* Purpose: responds to the two-way messages that you receive */
/* Input: pathid - ushort, pathid                             */
/*        msgid  - ulong, id of message                       */
/*        trgcls - ulong, target message class                */
/*        priority_msg - uchar, flag                          */
/*        buf    - pointer, address of buffer                 */
/*        buflen - length of buffer                           */
/* Output: iprcode - return code from b2f0 call               */
/**************************************************************/
int
iucv_reply (ushort pathid, ulong msgid, ulong trgcls,
	    uchar priority_msg, void *buf, ulong buflen)
{
	iparml_db parm;
	ulong b2f0_result;
#ifdef DEBUG
	printk (KERN_DEBUG "entering iucv_reply\n");
#endif
	memset (&(parm), 0, sizeof (parm));
	parm.ippathid = pathid;
	parm.ipmsgid = msgid;
	parm.iptrgcls = trgcls;
	parm.ipbfadr2 = (ulong) buf;
	parm.ipbfln2f = buflen;	/* length of message */
	if (priority_msg)
		parm.ipflags1 |= prior_msg;	/* priority message */
	b2f0_result = b2f0 (reply, &parm);
#ifdef DEBUG
	printk (KERN_DEBUG "exiting iucv_reply\n");
#endif
	return b2f0_result;
}

/**************************************************************/
/* Name: iucv_reply_array                                     */
/* Purpose: responds to the two-way messages that you receive */
/* Input: pathid - ushort, pathid                             */
/*        msgid  - ulong, id of message                       */
/*        trgcls - ulong, target message class                */
/*        priority_msg - uchar, flag                          */
/*        buf    - pointer, address of array                  */
/*        buflen - length of buffer                           */
/* Output: iprcode - return code from b2f0 call               */
/**************************************************************/
int
iucv_reply_array (ushort pathid, ulong msgid, ulong trgcls,
		  uchar priority_msg, iucv_array_t * buffer, ulong buflen)
{
	iparml_db parm;
	ulong b2f0_result;
#ifdef DEBUG
	printk (KERN_DEBUG "entering iucv_reply_array\n");
#endif
	memset (&(parm), 0, sizeof (parm));
	parm.ippathid = pathid;
	parm.ipmsgid = msgid;
	parm.iptrgcls = trgcls;
	parm.ipbfadr2 = (ulong) buffer;
	parm.ipbfln2f = buflen;	/* length of message */
	parm.ipflags1 |= reply_array;	/* reply w/ array  */
	if (priority_msg)
		parm.ipflags1 |= prior_msg;	/* priority message */
	b2f0_result = b2f0 (reply, &parm);
#ifdef DEBUG
	printk (KERN_DEBUG "exiting iucv_reply_array\n");
#endif
	return b2f0_result;
}

/**************************************************************/
/* Name: iucv_reply_prmmsg                                    */
/* Purpose: responds to the two-way messages in parameter list */
/* Input: pathid - ushort, pathid                             */
/*        msgid  - ulong, id of message                       */
/*        trgcls - ulong, target message class                */
/*        priority_msg - uchar, flag                          */
/*        prmmsg - uchar[8], message in parameter list        */
/* Output: iprcode - return code from b2f0 call               */
/**************************************************************/
int
iucv_reply_prmmsg (ushort pathid, ulong msgid, ulong trgcls,
		   uchar priority_msg, uchar prmmsg[8])
{
	iparml_dpl parm;
	ulong b2f0_result;
#ifdef DEBUG
	printk (KERN_DEBUG "entering iucv_reply_prmmsg\n");
#endif
	memset (&(parm), 0, sizeof (parm));
	parm.ippathid = pathid;
	parm.ipmsgid = msgid;
	parm.iptrgcls = trgcls;
	memcpy (parm.iprmmsg, prmmsg, 8);
	parm.ipflags1 |= parm_data;
	if (priority_msg)
		parm.ipflags1 |= prior_msg;	/* priority message */
	b2f0_result = b2f0 (reply, &parm);
#ifdef DEBUG
	printk (KERN_DEBUG "exiting iucv_reply_prmmsg\n");
#endif
	return b2f0_result;
}

/**************************************************************/
/* Name: iucv_connect                                         */
/* Purpose: establishes an IUCV path to another vm            */
/* Input: pathid - ushort *, pathid returned to user          */
/*        msglim - ushort, limit of outstanding messages      */
/*        user_data - uchar[16], user data                    */
/*        userid - uchar[8], user's id                        */
/*        system_name - uchar[8], system identification       */
/*        priority_requested - uchar- flag                    */
/*        prmdata - uchar, flag prgrm can handler messages    */
/*                  in parameter list                         */
/*        quiesce - uchar, flag to quiesce a path being establ */
/*        control - uchar, flag, option not used              */
/*        local   - uchar, flag, establish connection only on */
/*                  local system                              */
/*        priority_permitted - uchar *, flag returned to user */
/*        handle - address of handler                         */
/*        pgm_data - ulong                                    */
/* Output: iprcode - return code from b2f0 call               */
/**************************************************************/
int
iucv_connect (ushort * pathid, ushort msglim, uchar user_data[16],
	      uchar userid[8], uchar system_name[8],
	      uchar priority_requested, uchar prmdata,
	      uchar quiesce, uchar control,
	      uchar local, uchar * priority_permitted,
	      iucv_handle_t handle, ulong pgm_data)
{
	iparml_control parm;
	ulong b2f0_result;
	int add_pathid_result, rc;
	handler *R;
#ifdef DEBUG
	printk (KERN_DEBUG "entering iucv_connect\n");
#endif
	memset (&parm, 0, sizeof (parm));
	if (declare_flag == NULL) {
		rc = iucv_declare_buffer (iucv_external_int_buffer);
		if (rc) {
			printk (KERN_DEBUG "IUCV: registration failed\n");
#ifdef DEBUG
			printk (KERN_DEBUG "rc from declare buffer is: %i\n",
				rc);
#endif
			return rc;
		} else
			declare_flag = 1;
	}
	/* Checking if handle is valid  */
	spin_lock (&lock);
	for (R = handler_anchor; R != NULL; R = (handler *) R->next)
		if (R == handle)
			break;
	if (R == NULL) {
		spin_unlock (&lock);
#ifdef DEBUG
		printk (KERN_DEBUG "iucv_connect: Invalid Handle\n");
#endif
		return (-2);
	}
	if (pathid == NULL) {
		spin_unlock (&lock);
#ifdef DEBUG
		printk (KERN_DEBUG "iucv_connect: invalid pathid pointer\n");
#endif
		return (-3);
	}
	spin_unlock (&lock);
	parm.ipmsglim = msglim;
	memcpy (parm.ipuser, user_data, 16);
	memcpy (parm.ipvmid, userid, 8);
	memcpy (parm.iptarget, system_name, 8);
	if (parm.iptarget)
		ASCEBC (parm.iptarget, 8);
	if (parm.ipvmid) {
		ASCEBC (parm.ipvmid, 8);
		EBC_TOUPPER(parm.ipvmid, 8);
	}
	if (priority_requested)
		parm.ipflags1 |= prior_msg;
	if (prmdata)
		parm.ipflags1 |= parm_data;	/*data in parameter list */
	if (quiesce)
		parm.ipflags1 |= quiesce_msg;
	if (control) {
		/* do nothing at the time being  */
		/*control not provided yet */
	}
	if (local)
		parm.ipflags1 |= local_conn;	/*connect on local system */
	b2f0_result = b2f0 (connect, &parm);
	if (b2f0_result)
		return b2f0_result;
	add_pathid_result = add_pathid (parm.ippathid, handle, pgm_data);
	if (add_pathid_result) {
#ifdef DEBUG
		printk (KERN_DEBUG "iucv_connect: add_pathid failed \n");
#endif
		return (add_pathid_result);
	}
	*pathid = parm.ippathid;
	if (parm.ipflags1 & prior_msg)
		if (priority_permitted)
			*priority_permitted = 0x01;
#ifdef DEBUG
	printk (KERN_DEBUG "exiting iucv_connect\n");
#endif
	return b2f0_result;
}

/**************************************************************/
/* Name: iucv_accept                                          */
/* Purpose: completes the iucv communication path             */
/* Input: pathid - ushort , pathid                            */
/*        msglim - ushort, limit of outstanding messages      */
/*        user_data - uchar[16], user data                    */
/*        priority_requested - uchar- flag                    */
/*        prmdata - uchar, flag prgrm can handler messages    */
/*                  in parameter list                         */
/*        quiesce - uchar, flag to quiesce a path being establ*/
/*        control - uchar, flag, option not used              */
/*        priority_permitted -uchar *, flag returned to caller*/
/*        handle - address of handler                         */
/*        pgm_data - ulong                                    */
/* Output: iprcode - return code from b2f0 call               */
/**************************************************************/
int
iucv_accept (ushort pathid, ushort msglim, uchar user_data[16],
	     uchar priority_requested,
	     uchar prmdata, uchar quiesce, uchar control,
	     uchar * priority_permitted, iucv_handle_t handle, ulong pgm_data)
{
	ulong index1, index2;
	handler_table_entry *P = 0;
	iparml_control parm;
	ulong b2f0_result;
#ifdef DEBUG
	printk (KERN_DEBUG "entering iucv_accept\n");
#endif
	memset (&(parm), 0, sizeof (parm));
	parm.ippathid = pathid;
	parm.ipmsglim = msglim;
	memcpy (parm.ipuser, user_data, 16);
	if (priority_requested)
		parm.ipflags1 |= prior_msg;
	if (prmdata)
		parm.ipflags1 |= parm_data;	/*data in parameter list */
	if (quiesce)
		parm.ipflags1 |= quiesce_msg;
	if (control) {
		/* do nothing at the time being  */
		/*control not provided yet */
	}
	b2f0_result = b2f0 (accept, &parm);
	if (b2f0_result)
		return b2f0_result;
	index1 = ((ulong) pathid) / 512;
	index2 = ((ulong) pathid) % 512;
	spin_lock (&lock);
	if (pgm_data) {
		P = main_table[index1];
		(P + index2)->pgm_data = pgm_data;
	}
	spin_unlock (&lock);
	if (parm.ipflags1 & prior_msg)
		if (priority_permitted)
			*priority_permitted = 0x01;
#ifdef DEBUG
	printk (KERN_DEBUG "exiting iucv_accept\n");
#endif
	return b2f0_result;
}

/**************************************************************/
/* Name: iucv_send2way_prmmsg_array                           */
/* Purpose: sends messages in both directions w/parameter lst */
/* Input: pathid - ushort, pathid                             */
/*        msgid  - ulong *, id of message returned to caller  */
/*        trgcls - ulong, target message class                */
/*        srccls - ulong, source message class                */
/*        msgtag - ulong, message tag                         */
/*        priority_msg - uchar, flag                          */
/*        prmmsg - uchar[8], message being sent in parameter  */
/*        ansbuf - pointer to array of buffers                */
/*        anslen - length of ansbuf buffer                    */
/* Output: iprcode - return code from b2f0 call               */
/*         msgid - returns message id                         */
/**************************************************************/
int
iucv_send2way_prmmsg_array (ushort pathid, ulong * msgid,
			    ulong trgcls, ulong srccls,
			    ulong msgtag, uchar priority_msg,
			    uchar prmmsg[8],
			    iucv_array_t * ansbuf, ulong anslen)
{
	iparml_dpl parm;
	ulong b2f0_result;
#ifdef DEBUG
	printk (KERN_DEBUG "entering iucv_send2way_prmmsg\n");
#endif
	memset (&(parm), 0, sizeof (parm));
	parm.ippathid = pathid;
	parm.iptrgcls = trgcls;
	parm.ipsrccls = srccls;
	parm.ipmsgtag = msgtag;
	parm.ipbfadr2 = (ulong) ansbuf;
	parm.ipbfln2f = anslen;
	parm.ipflags1 |= 0x88;	/* message in prmlist */
	if (priority_msg)
		parm.ipflags1 |= prior_msg;	/* priority message */
	memcpy (parm.iprmmsg, prmmsg, 8);
	b2f0_result = b2f0 (send, &parm);
	if (msgid)
		*msgid = parm.ipmsgid;
#ifdef DEBUG
	printk (KERN_DEBUG "exiting iucv_send2way_prmmsg\n");
#endif
	return b2f0_result;
}

/******************************************************************/
/* Name: top_half_handler                                         */
/* Purpose: handle minimum amount of interrupt in fastest time    */
/*  possible and then pass interrupt to bottom half handler.      */
/* Input: external interrupt buffer                               */
/* Output: void                                                   */
/******************************************************************/

inline void
top_half_interrupt (struct pt_regs *regs, __u16 code)
{
	iucv_packet *pkt;
	pkt = (iucv_packet *) kmalloc
	    (sizeof (iucv_packet), GFP_ATOMIC);
	if (pkt == NULL) {
		printk (KERN_DEBUG "out of memory\n");
		return;
	}
	memcpy (pkt->data, iucv_external_int_buffer, 40);
#ifdef DEBUG3
printk (KERN_EMERG "TH: Got INT: %08x\n", *(int *)(pkt->data+4));
#endif
	/* put new packet on the list */
	spin_lock (&iucv_packets_lock);
	pkt->next = NULL;
	if (iucv_packets_tail != NULL)
		iucv_packets_tail->next = pkt;
	else
		iucv_packets_head = pkt;
	iucv_packets_tail = pkt;
	spin_unlock (&iucv_packets_lock);

	if (atomic_compare_and_swap (0, 1, &bh_scheduled) == 0) {
#ifdef DEBUG3
printk (KERN_EMERG "TH: Queuing BH\n");
#endif
		INIT_LIST_HEAD(&short_task.list);
		short_task.sync = 0;
		short_task.routine = (void *) bottom_half_interrupt;
		queue_task (&short_task, &tq_immediate);
		mark_bh (IMMEDIATE_BH);
	}
	return;
}

/*******************************************************************/
/* Name: bottom_half_interrupt                                     */
/* Purpose: Handle interrupt at a more safer time                  */
/* Input: void                                                     */
/* Output: void                                                    */
/*******************************************************************/
void
bottom_half_interrupt (void)
{
	iucv_packet *iucv_packet_list;
	iucv_packet *tmp;
	ulong flags;

	atomic_set (&bh_scheduled, 0);
	spin_lock_irqsave (&iucv_packets_lock, flags);
	iucv_packet_list = iucv_packets_head;
	iucv_packets_head = iucv_packets_tail = NULL;
	spin_unlock_irqrestore (&iucv_packets_lock, flags);

	/* now process all the request in the iucv_packet_list */
#ifdef DEBUG3
	printk (KERN_EMERG "BH: Process all packets\n");
#endif
	while (iucv_packet_list != NULL) {
#ifdef DEBUG3
		printk( KERN_EMERG "BH:>  %08x\n", 
			*(int *)(iucv_packet_list->data+4));
#endif
		do_int ((iucv_ConnectionPending *) iucv_packet_list->data);
#ifdef DEBUG3
		printk( KERN_EMERG "BH:<  %08x\n",
			*(int *)(iucv_packet_list->data+4));
#endif
		tmp = iucv_packet_list;
		iucv_packet_list = iucv_packet_list->next;
		kfree (tmp);
	}
#ifdef DEBUG3
	printk (KERN_EMERG "BH: Done\n");
#endif
	return;
}
/*******************************************************************/
/* Name: do_int                                                    */
/* Purpose: Handle interrupt in a more safe environment            */
/* Inuput: int_buf - pointer to copy of external interrupt buffer  */
/* Output: void                                                    */
/*******************************************************************/
void
do_int (iucv_ConnectionPending * int_buf)
{
	ulong index1 = 0, index2 = 0;
	handler_table_entry *P = 0;	/* P is a pointer */
	handler *Q = 0, *R;	/* Q and R are pointers */
	iucv_interrupt_ops_t *interrupt = 0;	/* interrupt addresses */
	uchar temp_buff1[24], temp_buff2[24];	/* masked handler id. */
	int add_pathid_result = 0, j = 0;
	uchar no_listener[16] = "NO LISTENER";
#ifdef DEBUG
	int i;
	uchar *prt_parm;
#endif
#ifdef DEBUG3
	printk (KERN_DEBUG "BH:-  Entered do_int "
	                   "pathid %d, type %02X\n",
		int_buf->ippathid, int_buf->iptype);
#endif
#ifdef DEBUG
	prt_parm = (uchar *) (int_buf);
	printk (KERN_DEBUG "External Interrupt Buffer\n");
	for (i = 0; i < 40; i++)
		printk (KERN_DEBUG "%02x ", prt_parm[i]);
	printk (KERN_DEBUG "\n");
#endif
	ASCEBC (no_listener, 16);
	if (int_buf->iptype != 01) {
		index1 = ((ulong) (int_buf->ippathid)) / 512;
		index2 = ((ulong) (int_buf->ippathid)) % 512;
		spin_lock (&lock);

		P = main_table[index1];
		Q = (P + index2)->addrs;
		interrupt = Q->interrupt_table;	/* interrupt functions */
		spin_unlock (&lock);
#ifdef DEBUG
		printk (KERN_DEBUG "Handler is: \n");
		prt_parm = (uchar *) Q;
		for (i = 0; i < sizeof (handler); i++)
			printk (KERN_DEBUG " %02x ", prt_parm[i]);
		printk (KERN_DEBUG "\n");
#endif
	}			/* end of if statement */
	switch (int_buf->iptype) {
	case 0x01:		/* connection pending */
		spin_lock (&lock);
		for (R = handler_anchor; R != NULL; R = (handler *) R->next) {
			memcpy (temp_buff1, &(int_buf->ipvmid), 24);
			memcpy (temp_buff2, &(R->vmid), 24);
			for (j = 0; j < 24; j++) {
				temp_buff1[j] = (temp_buff1[j]) & (R->mask)[j];
				temp_buff2[j] = (temp_buff2[j]) & (R->mask)[j];
			}
#ifdef DEBUG
			for (i = 0; i < sizeof (temp_buff1); i++)
				printk (KERN_DEBUG " %c ", temp_buff1[i]);
			printk (KERN_DEBUG "\n");
			for (i = 0; i < sizeof (temp_buff2); i++)
				printk (KERN_DEBUG " %c ", temp_buff2[i]);
			printk (KERN_DEBUG "\n");
#endif
			if (memcmp((void *) temp_buff1,
				   (void *) temp_buff2, 24) == 0) {
#ifdef DEBUG
				printk (KERN_DEBUG
					"found a matching handler\n");
#endif
				break;
			}
		}
		spin_unlock (&lock);
		if (R) {
			/* ADD PATH TO PATHID TABLE */
			add_pathid_result =
			    add_pathid (int_buf->ippathid, R, R->pgm_data);
			if (add_pathid_result == NULL) {
				interrupt = R->interrupt_table;
				if ((*interrupt).ConnectionPending) {
					EBCASC (int_buf->ipvmid, 8);
					
					    ((*interrupt).
					 ConnectionPending) (int_buf,
							     R->pgm_data);
				} else {
					iucv_sever (int_buf->ippathid,
						    no_listener);
				}
			} /* end if if(add_p...... */
			else {
				iucv_sever (int_buf->ippathid, no_listener);
#ifdef DEBUG
				printk (KERN_DEBUG
					"add_pathid failed with rc = %d\n",
					(int) add_pathid_result);
#endif
			}
		} else
			iucv_sever (int_buf->ippathid, no_listener);
		break;
	case 0x02:		/*connection complete */
		if (Q) {
			if ((*interrupt).ConnectionComplete)
				((*interrupt).ConnectionComplete)
				    
				    ((iucv_ConnectionComplete *) int_buf,
				     (P + index2)->pgm_data);
			else {
#ifdef DEBUG
				printk (KERN_DEBUG
					"ConnectionComplete not called\n");
				printk (KERN_DEBUG "routine@ is %p\n",
					(*interrupt).ConnectionComplete);
#endif
			}
		}
		break;
	case 0x03:		/* connection severed */
		if (Q) {
			if ((*interrupt).ConnectionSevered)
				((*interrupt).ConnectionSevered)
				    
				    ((iucv_ConnectionSevered *) int_buf,
				     (P + index2)->pgm_data);
			else
				iucv_sever (int_buf->ippathid, no_listener);
		} else
			iucv_sever (int_buf->ippathid, no_listener);
		break;
	case 0x04:		/* connection quiesced */
		if (Q) {
			if ((*interrupt).ConnectionQuiesced)
				((*interrupt).ConnectionQuiesced)
				    
				    ((iucv_ConnectionQuiesced *) int_buf,
				     (P + index2)->pgm_data);
			else {
#ifdef DEBUG
				printk (KERN_DEBUG
					"ConnectionQuiesced not called\n");
				printk (KERN_DEBUG "routine@ is %p\n",
					(*interrupt).ConnectionQuiesced);
#endif
			}
		}
		break;
	case 0x05:		/* connection resumed */
		if (Q) {
			if ((*interrupt).ConnectionResumed)
				((*interrupt).ConnectionResumed)
				    
				    ((iucv_ConnectionResumed *) int_buf,
				     (P + index2)->pgm_data);
			else {
#ifdef DEBUG
				printk (KERN_DEBUG
					"ConnectionResumed not called\n");
				printk (KERN_DEBUG "routine@ is %p\n",
					(*interrupt).ConnectionResumed);
#endif
			}
		}
		break;
	case 0x06:		/* priority message complete */
	case 0x07:		/* nonpriority message complete */
		if (Q) {
			if ((*interrupt).MessageComplete)
				((*interrupt).MessageComplete)
				    
				    ((iucv_MessageComplete *) int_buf,
				     (P + index2)->pgm_data);
			else {
#ifdef DEBUG
				printk (KERN_DEBUG
					"MessageComplete not called\n");
				printk (KERN_DEBUG "routine@ is %p\n",
					(*interrupt).MessageComplete);
#endif
			}
		}
		break;
	case 0x08:		/* priority message pending  */
	case 0x09:		/* nonpriority message pending  */
		if (Q) {
			if ((*interrupt).MessagePending)
				((*interrupt).MessagePending)
				    
				    ((iucv_MessagePending *) int_buf,
				     (P + index2)->pgm_data);
			else {
#ifdef DEBUG
				printk (KERN_DEBUG
					"MessagePending not called\n");
				printk (KERN_DEBUG "routine@ is %p\n",
					(*interrupt).MessagePending);
#endif
			}
		}
		break;
	default:		/* unknown iucv type */
		printk (KERN_DEBUG "unknown iucv interrupt \n");
		break;
	}			/* end switch */
#ifdef DEBUG3
	printk (KERN_DEBUG "BH:-  Exiting do_int "
	                   "pathid %d, type %02X\n",
		int_buf->ippathid, int_buf->iptype);
#endif
	return;
}				/* end of function call */

/**************************************************************/
/* Name: iucv_register_program                                */
/* Purpose: registers a new handler                           */
/* Input: pgmname- uchar[16], user id                         */
/*        userid - uchar[8], machine id                       */
/*        prmmask- mask                                       */
/*        ops    - pointer to iucv_interrupt_ops buffer       */
/* Output: new_handler - address of new handler               */
/**************************************************************/
iucv_handle_t
iucv_register_program (uchar pgmname[16],
		       uchar userid[8],
		       uchar pgmmask[24],
		       iucv_interrupt_ops_t * ops, ulong pgm_data)
{
	int rc;
	handler *new_handler = 0;
#ifdef DEBUG
	int i;
	uchar *prt_parm;
	printk (KERN_DEBUG "enter iucv_register_program\n");
#endif
	my_ops = *ops;
	/* Allocate handler table */
	new_handler = (handler *) kmalloc (sizeof (handler), GFP_KERNEL);
	if (new_handler == NULL) {
#ifdef DEBUG
		printk (KERN_DEBUG
			"IUCV: returned NULL address for new handle \n");
#endif
		return NULL;
	}
	/* fill in handler table */
	memcpy (new_handler->user_data, pgmname, 16);
	memcpy (new_handler->vmid, userid, 8);
	memcpy (new_handler->mask, pgmmask, 24);
	new_handler->pgm_data = pgm_data;
	/* Convert from ASCII to EBCDIC */
	if (new_handler->vmid) {
		ASCEBC (new_handler->vmid, 8);
		EBC_TOUPPER(new_handler->vmid, 8);
	}
	/* fill in handler table */
	new_handler->interrupt_table = ops;
	new_handler->size = ADDED_STOR;
	/* Allocate storage for pathid table */
	new_handler->start = kmalloc (ADDED_STOR * sizeof (ulong), GFP_KERNEL);
	if (new_handler->start == NULL) {
#ifdef DEBUG
		printk (KERN_DEBUG
			"IUCV: returned NULL address for pathid table,"
			" exiting\n");
#endif
		kfree(new_handler);
		return NULL;
	}
	memset (new_handler->start, 0, ADDED_STOR * sizeof (ulong));
	new_handler->end = (*new_handler).start + ADDED_STOR;
	new_handler->next = 0;
	new_handler->prev = 0;
	/* Place handler at beginning of chain */
	spin_lock (&lock);
	if (handler_anchor == NULL)
		handler_anchor = new_handler;
	else {
		handler_anchor->prev = (ulong *) new_handler;
		new_handler->next = (ulong *) handler_anchor;
		handler_anchor = new_handler;
#ifdef DEBUG
		printk (KERN_DEBUG "adding a another handler to list\n");
		printk (KERN_DEBUG "handler_anchor->prev is %p \n",
			handler_anchor->prev);
		printk (KERN_DEBUG "new_handler->next is %p \n",
			new_handler->next);
		printk (KERN_DEBUG "handler_anchor is %p \n", handler_anchor);
#endif
	}
	spin_unlock (&lock);
	if (declare_flag == NULL) {
		rc = iucv_declare_buffer (iucv_external_int_buffer);
		if (rc == 0) {
			declare_flag = 1;
			/* request the 0x4000 external interrupt */
			rc =
			    register_external_interrupt (0x4000,
							 top_half_interrupt);
		} else {
			panic ("Registration failed");
#ifdef DEBUG
			printk (KERN_DEBUG "rc from declare buffer is: %i\n",
				rc);
#endif
		}
	}
#ifdef DEBUG
	printk (KERN_DEBUG "address of handle is %p ", new_handler);
	printk (KERN_DEBUG "size of handle is %d ", (int) (sizeof (handler)));
	printk (KERN_DEBUG "exit iucv_register_program\n");
	printk (KERN_DEBUG "main_table is %p \n", main_table);
	printk (KERN_DEBUG "handler_anchor is %p \n", handler_anchor);
	printk (KERN_DEBUG "Handler is: \n");
	prt_parm = (uchar *) new_handler;
	for (i = 0; i < sizeof (handler); i++)
		printk (KERN_DEBUG " %02x ", prt_parm[i]);
	printk (KERN_DEBUG "\n");
#endif
	return new_handler;	/* send buffer address back */
}				/* end of register function */

/**************************************************************/
/* Name: iucv_unregister                                      */
/* Purpose: remove handler from chain and sever all paths     */
/* Input: handle - address of handler to be severed           */
/* Output: returns 0                                          */
/**************************************************************/
int
iucv_unregister (iucv_handle_t handle)
{
	handler *temp_next = 0, *temp_prev = 0;
	handler *Q = 0, *R;
	handler_table_entry *H_T_E = 0;
	ulong *S = 0;		/*points to the beginning of block of h_t_e's*/
#ifdef DEBUG
	printk (KERN_DEBUG "enter iucv_unregister\n");
	printk (KERN_DEBUG "address of handle is %p ", handle);
	printk (KERN_DEBUG "size of handle is %u ", (int) (sizeof (handle)));
#endif
	spin_lock (&lock);
	Q = (handler *) handle;
	/*
	 * Checking if handle is still registered: if yes, continue
	 *  if not registered, return.
	 */
	for (R = handler_anchor; R != NULL; R = (handler *) R->next)
		if (Q == R) {
#ifdef DEBUG
			printk (KERN_DEBUG "found a matching handler\n");
#endif
			break;
		}
	if (!R) {
		spin_unlock (&lock);
		return (0);
	}
	S = Q->start;
#ifdef DEBUG
	printk (KERN_DEBUG "Q is handle? %p ", Q);
	printk (KERN_DEBUG "Q->start is %p ", Q->start);
	printk (KERN_DEBUG "&(Q->start) is %p ", &(Q->start));
	printk (KERN_DEBUG "Q->end is %p ", Q->end);
	printk (KERN_DEBUG "&(Q->end) is %p ", &(Q->end));
#endif
	while (S < (Q->end)) {	/* index thru table */
		if (*S) {
			H_T_E = (handler_table_entry *) (*S);
#ifdef DEBUG
			printk (KERN_DEBUG "Pointer to H_T_E is %p ", H_T_E);
			printk (KERN_DEBUG "Address of handle in H_T_E is %p",
				(H_T_E->addrs));
#endif
			if ((H_T_E->addrs) != handle) {
				spin_unlock (&lock);
				return (-2);	/*handler addresses don't match */
			} else {
				spin_unlock (&lock);
				iucv_sever (H_T_E->pathid, Q->user_data);
				spin_lock (&lock);
			}
		}
		S++;		/* index by size of ulong */
	}
	kfree (Q->start);
	temp_next = (handler *) Q->next;	/* address of next handler on list */
	temp_prev = (handler *) Q->prev;	/* address of prev handler on list */
	if ((temp_next != NULL) & (temp_prev != NULL)) {
		(*temp_next).prev = (ulong *) temp_prev;
		(*temp_prev).next = (ulong *) temp_next;
	} else if ((temp_next != NULL) & (temp_prev == NULL)) {
		(*temp_next).prev = NULL;
		handler_anchor = temp_next;
	} else if ((temp_next == NULL) & (temp_prev != NULL))
		(*temp_prev).next = NULL;
	else
		handler_anchor = NULL;
	if (handler_anchor == NULL)
		iucv_retrieve_buffer ();
	kfree (handle);
	spin_unlock (&lock);
#ifdef DEBUG
	printk (KERN_DEBUG "exit iucv_unregister\n");
#endif
	return 0;
}

EXPORT_SYMBOL (iucv_accept);
EXPORT_SYMBOL (iucv_connect);
EXPORT_SYMBOL (iucv_purge);
EXPORT_SYMBOL (iucv_query);
EXPORT_SYMBOL (iucv_quiesce);
EXPORT_SYMBOL (iucv_receive);
EXPORT_SYMBOL (iucv_receive_simple);
EXPORT_SYMBOL (iucv_receive_array);
EXPORT_SYMBOL (iucv_reject);
EXPORT_SYMBOL (iucv_reply);
EXPORT_SYMBOL (iucv_reply_array);
EXPORT_SYMBOL (iucv_reply_prmmsg);
EXPORT_SYMBOL (iucv_resume);
EXPORT_SYMBOL (iucv_send);
EXPORT_SYMBOL (iucv_send2way);
EXPORT_SYMBOL (iucv_send2way_array);
EXPORT_SYMBOL (iucv_send_array);
EXPORT_SYMBOL (iucv_send2way_prmmsg);
EXPORT_SYMBOL (iucv_send2way_prmmsg_array);
EXPORT_SYMBOL (iucv_send_prmmsg);
EXPORT_SYMBOL (iucv_setmask);
EXPORT_SYMBOL (iucv_sever);
EXPORT_SYMBOL (iucv_register_program);
EXPORT_SYMBOL (iucv_unregister);

