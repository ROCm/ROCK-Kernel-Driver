/*                                                                                             
 *   drivers/s390/net/iucv.c
 *    Support for VM IUCV functions for use by other part of the
 *    kernel or loadable modules.
 *
 *  S390 version
 *    Copyright (C) 2000 IBM Corporation
 *    Author(s): Alan Altmark (Alan_Altmark@us.ibm.com)
 *               Xenia Tkatschow (xenia@us.ibm.com)
 * Functionality:                                                    
 * To explore any of the IUCV functions, one must first register     
 * their program using iucv_register(). Once your program has        
 * successfully completed a register, it can use the other functions.
 * For furthur reference on all IUCV functionality, refer to the     
 * CP Programming Services book, also available on the web            
 * thru www.ibm.com/s390/vm/pubs , manual # SC24-5760.               
 *                                                                    
 *      Definition of Return Codes                                    
 *      -All positive return codes including zero are reflected back 
 *       from CP and the definition can be found in CP Programming    
 *       Services book.                  
 *      - (-ENOMEM) Out of memory
 *      - (-EINVAL) Invalid value                             
*/
/* #define DEBUG 1 */
#include <linux/module.h>
#include <linux/config.h>
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
#include <asm/irq.h>
#include <asm/s390_ext.h>
#include <asm/ebcdic.h>

#ifdef DEBUG
#undef KERN_INFO
#undef KERN_DEBUG
#define KERN_INFO KERN_EMERG
#define KERN_DEBUG KERN_EMERG
#endif

#undef NULL
#define NULL 0

#define PRRTY_PRMTD    0x01	/* priority permitted */
#define RPY_RQRD       0x01	/* reply required */
#define ADDED_STOR  64		/* ADDITIONAL STORAGE FOR PATHID @'S */
#define BUFFER_SIZE 40		/* Size of 31-bit iparml */

/* FLAGS:
 * All flags are defined in the field IPFLAGS1 of each function
 * and can be found in CP Programming Services.
 * IPSRCCLS - Indicates you have specified a source class
 * IPFGMCL  - Indicates you have specified a target class
 * IPFGPID  - Indicates you have specified a pathid
 * IPFGMID  - Indicates you have specified a message ID
 * IPANSLST - Indicates that you are using an address list for
 *            reply data
 * IPBUFLST - Indicates that you are using an address list for
 *            message data
 */

#define IPSRCCLS 	0x01
#define IPFGMCL         0x01
#define IPFGPID         0x02
#define IPFGMID         0x04
#define IPANSLST        0x08
#define IPBUFLST        0x40

static uchar iucv_external_int_buffer[40];

/* Spin Lock declaration */
struct tq_struct short_task;	/* automatically initialized to zero */
static spinlock_t iucv_lock = SPIN_LOCK_UNLOCKED;

/* General IUCV interrupt structure */
typedef struct {
	u16 ippathid;
	uchar res1;
	uchar iptype;
	u32 res2;
	uchar ipvmid[8];
	uchar res3[24];
} iucv_GeneralInterrupt;

/***************INTERRUPT HANDLING DEFINITIONS***************/
typedef struct _iucv_packet {
	struct _iucv_packet *next;
	uchar data[40];
} iucv_packet;
struct tq_struct short_task;

static spinlock_t iucv_packets_lock = SPIN_LOCK_UNLOCKED;

iucv_packet *iucv_packets_head, *iucv_packets_tail;

static atomic_t bh_scheduled = ATOMIC_INIT (0);

/* 
 *Internal function prototypes 
 */

static ulong iucv_vmsize (void);

int iucv_declare_buffer (void);

int iucv_retrieve_buffer (void);

static int iucv_add_pathid (u16 pathid, iucv_handle_t handle, void *pgm_data);

static void iucv_remove_pathid (u16 pathid);

void bottom_half_interrupt (void);

static void do_int (iucv_GeneralInterrupt *);

inline void top_half_interrupt (struct pt_regs *regs, __u16 code);
/************FUNCTION ID'S****************************/

#define ACCEPT          10
#define CONNECT         11
#define DECLARE_BUFFER  12
#define PURGE           9
#define QUERY           0
#define QUIESCE         13
#define RECEIVE         5
#define REJECT          8
#define REPLY           6
#define RESUME          14
#define RETRIEVE_BUFFER 2
#define SEND            4
#define SETMASK         16
#define SEVER           15

/*                                                               
 * Structure: handler                                            
 * members: next - is a pointer to next handler on chain         
 *          prev - is a pointer to prev handler on chain         
 *          structure: id                                        
 *             vmid - 8 char array of machine identification     
 *             user_data - 16 char array for user identification 
 *             mask - 24 char array used to compare the 2 previous  
 *          interrupt_table - vector of interrupt functions.     
 *          pathid_head - pointer to start of user_pathid_table  
 *          pathid_tail - pointer to end of user_pathid_table    
 *          entries -  ulong, size of user_pathid_table          
 *          pgm_data -  ulong, application data that is passed   
 *                      to the interrupt handlers                
*/
typedef struct {
	ulong *next;
	ulong *prev;
	struct {
		uchar userid[8];
		uchar user_data[16];
		uchar mask[24];
	} id;
	iucv_interrupt_ops_t *interrupt_table;
	ulong *pathid_head;
	ulong *pathid_tail;
	ulong entries;
	void *pgm_data;
} handler;

/*                                                         
 * Structure: handler_table_entry                          
 * members: addrs - pointer to a handler                   
 *          pathid - ushort containing path identification 
 *          pgm_data - ulong, application data that is     
 *                     passed to the interrupt handlers    
 *          ops - pointer to iucv interrupt vector         
 */

typedef struct {
	handler *addrs;
	u16 pathid;
	void *pgm_data;
	iucv_interrupt_ops_t *ops;
} handler_table_entry;

/* 
 * Internal function prototypes 
 */

static int iucv_add_handler (handler * new_handler);

static void iucv_remove_handler (handler * users_handler);

/* handler_anchor: points to first handler on chain */
/* handler_tail: points to last handler on chain */
/* handler_table_anchor: points to beginning of handler_table_entries*/

static handler *handler_anchor = NULL;

static handler *handler_tail = NULL;

static handler_table_entry *handler_table_anchor = NULL;

/* declare_flag: is 0 when iucv_declare_buffer has not been called */

static ulong declare_flag = 0;

/****************FIVE 40-BYTE PARAMETER STRUCTURES******************/
/* Data struct 1: iparml_control                                      
 * Used for iucv_accept                                               
 *          iucv_connect                                              
 *          iucv_quiesce                                              
 *          iucv_resume                                               
 *          iucv_sever                                                
 *          iucv_retrieve_buffer                                      
 * Data struct 2: iparml_dpl     (data in parameter list)             
 * Used for iucv_send_prmmsg                                          
 *          iucv_send2way_prmmsg                                      
 *          iucv_send2way_prmmsg_array                                
 *          iucv_reply_prmmsg                                         
 * Data struct 3: iparml_db       (data in a buffer)                  
 * Used for iucv_receive                                              
 *          iucv_receive_array                                        
 *          iucv_reject                                               
 *          iucv_reply                                                
 *          iucv_reply_array                
 *          iucv_send                       
 *          iucv_send_array                 
 *          iucv_send2way                   
 *          iucv_send2way_array             
 *          iucv_declare_buffer             
 * Data struct 4: iparml_purge              
 * Used for iucv_purge                      
 *          iucv_query                      
 * Data struct 5: iparml_set_mask           
 * Used for iucv_set_mask                   
*/

typedef struct {
	u16 ippathid;
	uchar ipflags1;
	uchar iprcode;
	u16 ipmsglim;
	u16 res1;
	uchar ipvmid[8];
	uchar ipuser[16];
	uchar iptarget[8];
} iparml_control;

typedef struct {
	u16 ippathid;
	uchar ipflags1;
	uchar iprcode;
	u32 ipmsgid;
	u32 iptrgcls;
	uchar iprmmsg[8];
	u32 ipsrccls;
	u32 ipmsgtag;
	u32 ipbfadr2;
	u32 ipbfln2f;
	u32 res;
} iparml_dpl;

typedef struct {
	u16 ippathid;
	uchar ipflags1;
	uchar iprcode;
	u32 ipmsgid;
	u32 iptrgcls;
	u32 ipbfadr1;
	u32 ipbfln1f;
	u32 ipsrccls;
	u32 ipmsgtag;
	u32 ipbfadr2;
	u32 ipbfln2f;
	u32 res;
} iparml_db;

typedef struct {
	u16 ippathid;
	uchar ipflags1;
	uchar iprcode;
	u32 ipmsgid;
	uchar ipaudit[3];
	uchar res1[5];
	u32 res2;
	u32 ipsrccls;
	u32 ipmsgtag;
	u32 res3[3];
} iparml_purge;

typedef struct {
	uchar ipmask;
	uchar res1[2];
	uchar iprcode;
	u32 res2[9];
} iparml_set_mask;

/*********************INTERNAL FUNCTIONS*****************************/

static ulong
iucv_vmsize (void)
{
	extern unsigned long memory_size;
	return memory_size;
}

/*
 * Name: dumpit                                                     
 * Purpose: print to the console buffers of a given length          
 * Input: buf - (* uchar) - pointer to buffer to be printed         
 *        len - int - length of buffer being printed                
 * Output: void                                                     
 */

#ifdef DEBUG

static void
iucv_dumpit (uchar * buf, int len)
{
	int i;
	for (i = 0; i < len; i++) {
		if (!(i % 16) && i != 0)
			printk ("\n");
		else if (!(i % 4) && i != 0)
			printk (" ");
		printk ("%02X", buf[i]);
	}
	if (len % 16)
		printk ("\n");
	return;
}

#else
static void
iucv_dumpit (uchar * buf, int len)
{
}

#endif

/*
 * Name iucv_add_handler
 * Purpose: Place new handle on handler_anchor chain, if identical handler is not
 *	    found. Handlers are ordered with largest mask integer value first.
 * Input: new_handler - handle that is being entered into chain
 * Return: int
 *	   0 - handler added
 *	   1 - identical handler found, handler not added to chain
*/
int
iucv_add_handler (handler * new_handler)
{
	handler *R = new_handler;
	int rc = 1, comp = 0;	/* return code (rc = 1 not added) or (rc = 0 added) */
	ulong flags;
	pr_debug ("iucv_add_handler: entering\n");
	iucv_dumpit ((uchar *) new_handler, sizeof (handler));
	spin_lock_irqsave (&iucv_lock, flags);
	if (handler_anchor == NULL) {
		/* add to beginning of chain */
		handler_anchor = handler_tail = new_handler;
		rc = 0;
	} else
		for (R = handler_anchor; R != NULL; R = (handler *) R->next) {
			comp = memcmp ((void *) &(new_handler->id),
				       (void *) &(R->id), sizeof (R->id));
			pr_debug ("comp = %d\n", comp);
			if (comp == 0)	/* identicle handler found */
				break;	/* break out of for loop */
			else if (comp > 0) {	/* new_handler > R */
				pr_debug
				    ("iucv_add_handler: Found a place to add,"
				     "R is\n");
				iucv_dumpit ((uchar *) R, sizeof (handler));
				if ((R->prev != NULL)) {
					/* add to middle of chain */
					pr_debug
					    ("iucv_add_handler: added to middle\n");
					new_handler->prev = R->prev;
					new_handler->next = (ulong *) R;
					((handler *) (R->prev))->next =
					    (ulong *) new_handler;
					R->prev = (ulong *) new_handler;
					rc = 0;
					break;	/* break out of FOR loop */
				} else {	/* R->prev == NULL */
					/* add to start of chain;  */
					pr_debug ("iucv_add_handler:"
						  "added to beginning\n");
					R->prev = (ulong *) new_handler;
					new_handler->next = (ulong *) R;
					handler_anchor = new_handler;
					rc = 0;
					break;	/* break out of FOR loop */
				}
			}	/* end of else if */
		}		/* end of for loop */
	if (R == NULL) {
		/* add to end of chain */
		pr_debug ("iucv_add_handler: added to end\n");
		handler_tail->next = (ulong *) new_handler;
		new_handler->prev = (ulong *) handler_tail;
		handler_tail = new_handler;
		rc = 0;
	}
	spin_unlock_irqrestore (&iucv_lock, flags);

	pr_debug ("Current Chain of handlers is\n");
	for (R = handler_anchor; R != NULL; R = (handler *) R->next)
		iucv_dumpit ((uchar *) R, (int) sizeof (handler));

	pr_debug ("iucv_add_handler: exiting\n");
	return rc;
}

/* 
 * Name: iucv_remove_handler
 * Purpose: Remove handler when application unregisters.
 * Input: users_handler - handler to be removed
 * Output: void
*/
void
iucv_remove_handler (handler * users_handler)
{
	handler *R;		/* used for Debugging */
	pr_debug ("iucv_remove_handler: entering\n");
	if ((users_handler->next != NULL) & (users_handler->prev != NULL)) {
		/* remove from middle of chain */
		((handler *) (users_handler->next))->prev =
		    (ulong *) users_handler->prev;
		((handler *) (users_handler->prev))->next =
		    (ulong *) users_handler->next;
	} else if ((users_handler->next != NULL) &
		   (users_handler->prev == NULL)) {
		/* remove from start of chain */
		((handler *) (users_handler->next))->prev = NULL;
		handler_anchor = (handler *) users_handler->next;
	} else if ((users_handler->next == NULL) &
		   (users_handler->prev != NULL)) {
		/* remove from end of chain */
		((handler *) (users_handler->prev))->next = NULL;
		handler_tail = (handler *) users_handler->prev;
	} else {
		handler_anchor = NULL;
		handler_tail = NULL;
	}

	pr_debug ("Current Chain of handlers is\n");
	for (R = handler_anchor; R != NULL; R = (handler *) R->next)
		iucv_dumpit ((uchar *) R, (int) sizeof (handler));

	pr_debug ("iucv_remove_handler: exiting\n");
	return;
}

/*
 * Name: b2f0
 * Purpose: This function calls CP to execute IUCV commands.
 * Input: code -  identifier of IUCV call to CP.
 *        parm -  pointer to 40 byte iparml area
 *               passed to CP
 * Output: iprcode- return code from CP's IUCV call
 * NOTE: Assembler code performing IUCV call
*/
inline ulong
b2f0 (u32 code, void *parm)
{
	uchar *iprcode;
	pr_debug ("iparml before b2f0 call\n");
	iucv_dumpit ((uchar *) parm, (int) BUFFER_SIZE);
	asm volatile ("LRA   1,0(%1)\n\t"
		      "LR    0,%0\n\t"
		      ".long 0xb2f01000"::"d" (code), "a" (parm):"0", "1");
	pr_debug ("iparml after b2f0 call\n");
	iucv_dumpit ((uchar *) parm, (int) BUFFER_SIZE);
	iprcode = (uchar *) (parm + 3);
	return (ulong) (*iprcode);
}

/*
 * Name: iucv_add_pathid                                            
 * Purpose: Adds a path id to the system.                       
 * Input: pathid -  pathid that is going to be entered into system              
 *        handle -  address of handler that the pathid will be associated
 *		   with.
 *        pgm_data - token passed in by application.                
 * Output: 0: successful addition of pathid
 *	   - EINVAL - pathid entry is being used by another application
 *	   - ENOMEM - storage allocation for a new pathid table failed      
*/
int
iucv_add_pathid (u16 pathid, iucv_handle_t handle, void *pgm_data)
{
	ulong add_flag = 0;
	ulong old_size = 0, new_size = 0;
	ulong flags;
	uchar *to, *from;	/* pointer for copying the table */
	handler_table_entry *P = 0;	/*P is a pointer to the users H_T_E */
	handler *users_handler = 0;
	ulong *X = 0;		/* Points to array of pointers to H-T_E */

	pr_debug ("iucv_add_pathid: entering\n");

	users_handler = (handler *) handle;

	pr_debug ("iucv_add_pathid: users_handler is pointing to %p ",
		  users_handler);

	spin_lock_irqsave (&iucv_lock, flags);

	/*
	 * P points to the users handler table entry (H_T_E) in which all entries in
	 * that structure should be NULL. If they're not NULL, then there
	 * is a bad pointer and it will return(-EINVAL) immediately, otherwise users
	 * data will be entered into H_T_E.
	 */

	P = handler_table_anchor + pathid;	/* index into users handler table */

	pr_debug ("handler_table_anchor is %p\n", handler_table_anchor);
	pr_debug ("P=handler_table_anchor+pathid = %p\n", P);

	if (P->addrs) {
		pr_debug ("iucv_add_pathid: P = %p \n", P);
		pr_debug ("iucv_add_pathid: P->addrs is %p \n", P->addrs);
		spin_unlock_irqrestore (&iucv_lock, flags);
		/* This message should be sent to syslog */
		printk (KERN_WARNING "iucv_add_pathid: Pathid being used,"
			"error.\n");
		return (-EINVAL);
	}

	P->addrs = handle;
	P->pathid = pathid;

	/*
	 * pgm_data provided in iucv_register may be overwritten on a connect, accept. 
	 */

	if (pgm_data)
		P->pgm_data = pgm_data;
	else
		P->pgm_data = users_handler->pgm_data;

	/*
	 * Address of pathid's iucv_interrupt_ops is taken from the associated handler
	 * and added here for quicker access to the interrupt tables during interrupt
	 * handling.
	 */

	P->ops = (P->addrs)->interrupt_table;

	pr_debug ("Complete users H_T_E is\n");
	iucv_dumpit ((uchar *) P, sizeof (handler_table_entry));

	/*
	 * Step thru the table of addresses of pathid's to find the first
	 * available entry (NULL). If an entry is found, add the pathid,
	 * unlock and exit. If an available entry is not found, allocate a
	 * new, larger table, copy over the old table to the new table. De-allocate the
	 * old table and enter the new pathid.
	 */

	pr_debug ("iucv_add_pathid: address of handle is %p\n", handle);
	pr_debug ("iucv_add_pathid: &(users_handler->pathid_head) is %p\n",
		  &(users_handler->pathid_head));
	pr_debug ("iucv_add_pathid: &(users_handler->pathid_tail) is %p\n",
		  &(users_handler->pathid_tail));
	pr_debug ("iucv_add_pathid: start of pathid table is %p\n",
		  (users_handler->pathid_head));
	pr_debug ("iucv_add_pathid: end of pathid table is %p\n",
		  (users_handler->pathid_tail));
	iucv_dumpit ((uchar *) users_handler->pathid_head,
		     (int) (users_handler->pathid_tail -
			    users_handler->pathid_head));

	for (X = (users_handler->pathid_head);
	     X <
	     (users_handler->pathid_head +
	      users_handler->entries * sizeof (ulong)); X++)
		if (*X == NULL) {
			pr_debug ("adding pathid, %p = P\n", P);
			*X = (ulong) P;
			add_flag = 1;
			break;	/* breaks out of for loop */
		}

	pr_debug ("Addresses of HTE's are\n");
	iucv_dumpit ((uchar *) users_handler->pathid_head,
		     users_handler->entries * sizeof (ulong));

	if (add_flag == 0) {	/* element not added to list: must get a new table */
		X = users_handler->pathid_head;
		old_size = users_handler->entries;
		new_size = old_size + ADDED_STOR;	/*number of entries of new table */
		from = (uchar *) (users_handler->pathid_head);	/*address of old table */
		users_handler->pathid_head =
		    kmalloc (new_size * sizeof (ulong), GFP_ATOMIC);

		if (users_handler->pathid_head == NULL) {
			users_handler->pathid_head = X;	/*setting old condition */
			spin_unlock_irqrestore (&iucv_lock, flags);
			printk (KERN_WARNING
				"iucv_add_pathid: storage allocation"
				"failed for new pathid table \n ");
			memset (P, 0, sizeof (handler_table_entry));
			return -ENOMEM;
		}

		memset (users_handler->pathid_head, 0,
			new_size * sizeof (ulong));
		to = (uchar *) (users_handler->pathid_head);	/* address of new table */
		/* copy old table to new  */
		memcpy (to, from, old_size * (sizeof (ulong)));

		pr_debug ("iucv: add_pathid: Getting a new pathid table\n");
		pr_debug ("iucv: add_pathid: to is %p \n", to);
		pr_debug ("iucv: add_pathid: from is %p \n", from);

		users_handler->entries = new_size;	/* storing new size of table */
		users_handler->pathid_tail =
		    (users_handler->pathid_head) + (users_handler->entries);
		X = users_handler->pathid_head + old_size;
		*X = (ulong) P;	/* adding element to new table */

		pr_debug ("iucv: add_pathid: users_handler->entries is %u \n",
			  (int) (users_handler->entries));
		pr_debug
		    ("iucv: add_pathid: users_handler->pathid_tail is %p\n",
		     users_handler->pathid_tail);
		pr_debug ("users_handler->pathid_head is %p \n",
			  users_handler->pathid_head);
		pr_debug ("iucv: add_pathid: X is %p \n", X);
		pr_debug ("iucv: add_pathid: *X is %u \n", (int) (*X));
		pr_debug ("Addresses of HTE's after getting new table is\n");
		iucv_dumpit ((uchar *) users_handler->pathid_head,
			     users_handler->entries * sizeof (ulong));
		pr_debug ("New handler is\n");
		iucv_dumpit ((uchar *) users_handler, sizeof (handler));

		kfree (from);	/* free old table */
	}
	spin_unlock_irqrestore (&iucv_lock, flags);
	pr_debug ("iucv_dd_pathid: exiting\n");
	return (0);
}				/* end of add_pathid function */

/*
 * Name: iucv_declare_buffer                                    
 * Purpose: Specifies the guests real address of an external  
 *          interrupt.                                        
 * Input: void                             
 * Output: iprcode - return code from b2f0 call               
*/
int
iucv_declare_buffer (void)
{
	iparml_db parm;
	ulong b2f0_result;
	pr_debug ("iucv_declare_buffer: entering\n");
	memset (&parm, 0, sizeof (parm));
	parm.ipbfadr1 = virt_to_phys ((uchar *) iucv_external_int_buffer);
	b2f0_result = b2f0 (DECLARE_BUFFER, &parm);
	pr_debug ("iucv_declare_buffer: Address of EIB = %p\n",
		  iucv_external_int_buffer);
	pr_debug ("iucv_declare_buffer: exiting\n");
	return b2f0_result;
}

/*
 * Name: iucv_retrieve_buffer
 * Purpose: Terminates all use of IUCV.
 * Input: void
 * Output:
 *      b2f0_result: return code from CP
*/
int
iucv_retrieve_buffer (void)
{
	iparml_control parm;
	ulong b2f0_result = 0;
	pr_debug ("iucv_retrieve_buffer: entering\n");
	memset (&parm, 0, sizeof (parm));
	b2f0_result = b2f0 (RETRIEVE_BUFFER, &parm);
	if (b2f0_result == NULL) {
		kfree (handler_table_anchor);
		handler_table_anchor = NULL;
		declare_flag = 0;
	}
	pr_debug ("iucv_retrieve_buffer: exiting\n");
	return b2f0_result;
}

/*
 * Name: iucv_register_program
 * Purpose: Registers an application with IUCV.   
 * Input: prmname - user identification
 *        userid  - machine identification
 *        pgmmask - indicates which bits in the prmname and userid combined will be used
 *	   to determine who is given control
 *        ops - address of vector of interrupt handlers
 *        pgm_data- application data passed to interrupt handlers
 * Output: NA
 * Return: type: iucv_handle_t
 *          address of handler
 *         (0) - registration failed
 *	       - Machine size > 2GB
 *	       - new_handler kmalloc failed
 * 	       - pgmname was not provided
 *	       - pathid_table kmalloc failed
 *             - application with identical pgmname, userid, and pgmmask is registered
 * 	       - iucv_declare_buffer failed
 * NOTE: pgmmask
 *	When pgmname, userid, pgmmask is provided, mask is entered into the handler
 *	as is.
 *	When pgmname, userid is provided, pgmmask is all 0xff's
 *	When pgmname, pgmmask is provided, the first 8 bytes = 0x00 and the last 16
 *      bytes are as provided by pgmmask. 
 *	When pgmname is provided is provided, the first 8 bytes = 0x00 and the last
 *	16 bytes are 0xff.   
*/

iucv_handle_t
iucv_register_program (uchar pgmname[16],
		       uchar userid[8],
		       uchar pgmmask[24],
		       iucv_interrupt_ops_t * ops, void *pgm_data)
{
	ulong rc = 0;		/* return code from function calls */
	ulong machine_size = 0;	/* size of virtual machine */
	static u32 maxconn1;
	handler *new_handler = NULL;

	pr_debug ("iucv_register_program:entering\n");

	if (ops == NULL) {
		/* interrupt table is not defined */
		printk (KERN_WARNING "iucv_register_program:"
			"Interrupt table is not defined, exiting\n");
		return NULL;
	}

	if (declare_flag == 0) {
		/* check size of virtual machine */
		if ((machine_size = iucv_vmsize ()) > 0x100000000) {	/* 2GB */
			printk (KERN_WARNING "iucv_register_progam: Virtual"
				"storage = %lx hex," "exiting\n", machine_size);
			return NULL;
		}

		pr_debug ("machine_size is %lx\n", machine_size);

		maxconn1 = iucv_query_maxconn ();
		handler_table_anchor = kmalloc (maxconn1 * sizeof
						(handler_table_entry),
						GFP_KERNEL);

		if (handler_table_anchor == NULL) {
			printk (KERN_WARNING "iucv_register_program:"
				"handler_table_anchor"
				"storage allocation failed\n");
			return NULL;
		}

		memset (handler_table_anchor, 0,
			maxconn1 * sizeof (handler_table_entry));

	}
	/* Allocate handler table */
	new_handler = (handler *) kmalloc (sizeof (handler), GFP_KERNEL);
	if (new_handler == NULL) {
		printk (KERN_WARNING "iucv_register_program: storage allocation"
			"for new handler failed. \n ");
		return NULL;
	}
	memset (new_handler, 0, sizeof (handler));
	if (pgmname) {
		memcpy (new_handler->id.user_data, pgmname,
			sizeof (new_handler->id.user_data));
		if (userid) {
			memcpy (new_handler->id.userid, userid,
				sizeof (new_handler->id.userid));
			ASCEBC (new_handler->id.userid,
				sizeof (new_handler->id.userid));
			EBC_TOUPPER (new_handler->id.userid,
				     sizeof (new_handler->id.userid));

			if (pgmmask) {
				memcpy (new_handler->id.mask, pgmmask,
					sizeof (new_handler->id.mask));
			} else {
				memset (new_handler->id.mask, 0xFF,
					sizeof (new_handler->id.mask));
			}
		} else {
			if (pgmmask) {
				memcpy (new_handler->id.mask, pgmmask,
					sizeof (new_handler->id.mask));
			} else {
				memset (new_handler->id.mask, 0xFF,
					sizeof (new_handler->id.mask));
			}
			memset (new_handler->id.mask, 0x00,
				sizeof (new_handler->id.userid));
		}
	} else {
		kfree (new_handler);
		printk (KERN_WARNING "iucv_register_program: pgmname not"
			"provided\n");
		return NULL;
	}
	/* fill in the rest of handler */
	new_handler->pgm_data = pgm_data;
	new_handler->interrupt_table = ops;
	new_handler->entries = ADDED_STOR;
	/* Allocate storage for pathid table */
	new_handler->pathid_head =
	    kmalloc (new_handler->entries * sizeof (ulong), GFP_KERNEL);
	if (new_handler->pathid_head == NULL) {
		printk (KERN_WARNING "iucv_register_program: storage allocation"
			"failed\n");
		kfree (new_handler);
		return NULL;
	}

	memset (new_handler->pathid_head, 0,
		new_handler->entries * sizeof (ulong));
	new_handler->pathid_tail =
	    new_handler->pathid_head + new_handler->entries;
	/* 
	 * Check if someone else is registered with same pgmname, userid, and mask. 
	 * If someone is already registered with same pgmname, userid, and mask 
	 * registration will fail and NULL will be returned to the application. 
	 * If identical handler not found, then handler is added to list.
	 */
	rc = iucv_add_handler (new_handler);
	if (rc) {
		printk (KERN_WARNING "iucv_register_program: Someone already"
			"registered with same pgmname, userid, pgmmask\n");
		kfree (new_handler->pathid_head);
		kfree (new_handler);
		return NULL;
	}

	if (declare_flag == 0) {
		rc = iucv_declare_buffer ();
		if (rc) {
			kfree (handler_table_anchor);
			kfree (new_handler->pathid_head);
			kfree (new_handler);
			handler_table_anchor = NULL;
			printk (KERN_WARNING "iucv_register_program: rc from"
				"iucv_declare_buffer is:% ld \n ", rc);
			return NULL;
		}
		/* request the 0x4000 external interrupt */
		rc = register_external_interrupt (0x4000, top_half_interrupt);
		if (rc) {
			iucv_retrieve_buffer ();
			kfree (new_handler->pathid_head);
			kfree (new_handler);
			printk (KERN_WARNING "iucv_register_program: rc from"
				"register_external_interrupt is:% ld \n ", rc);
			return NULL;

		}
		declare_flag = 1;
	}
	pr_debug ("iucv_register_program: exiting\n");
	return new_handler;
}				/* end of register function */

/*
 * Name: iucv_unregister_program
 * Purpose: Unregister application with IUCV.
 * Input: handle address of handler
 * Output: NA
 * Return: (0) - Normal return
 *         (-EINVAL)- Matching handler was not found
*/

int
iucv_unregister_program (iucv_handle_t handle)
{
	handler *users_handler = 0, *R;
	handler_table_entry *H_T_E = 0;
	ulong *S = 0;		/*points to the beginning of block of h_t_e's */
	ulong flags;
	u16 pathid_sever = 0;
	pr_debug ("iucv_unregister_program: entering\n");
	pr_debug ("iucv_unregister_program: address of handle is %p\n", handle);
	spin_lock_irqsave (&iucv_lock, flags);
	users_handler = (handler *) handle;
	/* 
	 * Checking if handle is still registered: if yes, continue
	 *  if not registered, return.
	 */
	for (R = handler_anchor; R != NULL; R = (handler *) R->next)
		if (users_handler == R) {
			pr_debug ("iucv_unregister_program: found a matching"
				  "handler\n");
			break;
		}
	if (!R) {
		pr_debug ("You are not registered\n");
		spin_unlock_irqrestore (&iucv_lock, flags);
		return (0);
	}
	S = users_handler->pathid_head;
	while (S < (users_handler->pathid_tail)) {	/* index thru table */
		if (*S) {
			H_T_E = (handler_table_entry *) (*S);

			pr_debug ("iucv_unregister_program: pointer to H_T_E is"
				  "%p\n", H_T_E);
			pr_debug
			    ("iucv_unregister_program: address of handle in"
			     "H_T_E is %p", (H_T_E->addrs));
			pathid_sever = H_T_E->pathid;
			spin_unlock_irqrestore (&iucv_lock, flags);
			iucv_sever (pathid_sever, users_handler->id.user_data);
			spin_lock_irqsave (&iucv_lock, flags);
		}

		S++;		/* index by address */
	}

	kfree (users_handler->pathid_head);
	iucv_remove_handler (users_handler);
	spin_unlock_irqrestore (&iucv_lock, flags);
	kfree (handle);
	pr_debug ("iucv_unregister_program: exiting\n");
	return 0;
}

/*
 * Name: iucv_accept
 * Purpose: This function is issued after the user receives a Connection Pending external
 *          interrupt and now wishes to complete the IUCV communication path.
 * Input:  pathid - u16 , path identification number   
 *         msglim_reqstd - u16, The number of outstanding messages requested.
 *         user_data - uchar[16], Data specified by the iucv_connect function.
 *	   flags1 - int, Contains options for this path.
 *           -IPPRTY - 0x20- Specifies if you want to send priority message.
 *           -IPRMDATA - 0x80, Specifies whether your program can handle a message
 *            	in  the parameter list.
 *           -IPQUSCE - 0x40, Specifies whether you want to quiesce the path being
 *		established.
 *         handle - iucv_handle_t, Address of handler.
 *         pgm_data - ulong, Application data passed to interrupt handlers.
 *	   flags1_out - int *, Options for path.
 *           IPPRTY - 0x20 - Indicates you may send a priority message.
 *         priority_permitted -uchar *, Indicates you may send priority messages.
 *         msglim - *u16, Number of outstanding messages.
 * Output: b2f0_result - return code from CP
*/
int
iucv_accept (u16 pathid, u16 msglim_reqstd,
	     uchar user_data[16], int flags1,
	     iucv_handle_t handle, void *pgm_data,
	     int *flags1_out, u16 * msglim)
{
	iparml_control parm;
	ulong b2f0_result = 0;
	ulong flags;
	handler *R = NULL;
	pr_debug ("iucv_accept: entering \n");
	pr_debug ("iucv_accept: pathid = %d\n", pathid);

	/* Checking if handle is valid  */
	spin_lock_irqsave (&iucv_lock, flags);

	for (R = handler_anchor; R != NULL; R = (handler *) R->next)
		if (R == handle)
			break;

	spin_unlock_irqrestore (&iucv_lock, flags);
	if (R == NULL) {
		printk (KERN_WARNING "iucv_connect: NULL handle passed by"
			"application\n");
		return -EINVAL;
	}

	memset (&parm, 0, sizeof (parm));
	parm.ippathid = pathid;
	parm.ipmsglim = msglim_reqstd;
	if (user_data)
		memcpy (parm.ipuser, user_data, sizeof (parm.ipuser));
	parm.ipflags1 = (uchar) flags1;
	b2f0_result = b2f0 (ACCEPT, &parm);

	if (b2f0_result == 0) {
		if (pgm_data)
			(handler_table_anchor + pathid)->pgm_data = pgm_data;
		if (parm.ipflags1 & IPPRTY)
			if (flags1_out) {
				pr_debug ("*flags1_out = %d\n", *flags1_out);
				*flags1_out = 0;
				*flags1_out |= IPPRTY;
				pr_debug (" *flags1_out = %d\n", *flags1_out);
			}
	}

	pr_debug ("iucv_accept: exiting\n");
	return b2f0_result;
}

/*
 * Name: iucv_connect                                         
 * Purpose: This function establishes an IUCV path. Although the connect may complete
 *	    successfully, you are not able to use the path until you receive an IUCV 
 *          Connection Complete external interrupt.            
 * Input: pathid - u16 *, path identification number          
 *        msglim_reqstd - u16, number of outstanding messages requested       
 *        user_data - uchar[16], 16-byte user data                    
 *        userid - uchar[8], 8-byte of user identification                        
 *        system_name - uchar[8], 8-byte identifying the system name 
 *        flags1 - int, Contains options for this path.
 *          -IPPRTY - 0x20- Specifies if you want to send priority message.
 *          -IPRMDATA - 0x80, Specifies whether your program can handle a message
 *               in  the parameter list.
 *          -IPQUSCE - 0x40, Specifies whether you want to quiesce the path being
 *              established.
 *          -IPLOCAL - 0X01, allows an application to force the partner to be on the
 *              local system. If local is specified then target class cannot be
 *              specified.  
 *         flags1_out - int *, Options for path. 
 *           IPPRTY - 0x20 - Indicates you may send a priority message.
 *        msglim - * u16, number of outstanding messages
 *        handle - iucv_handle_t, address of handler                         
 *        pgm_data - *void, application data passed to interrupt handlers                  
 * Output: b2f0_result - return code from CP
 *         -ENOMEM
 *         rc - return code from iucv_declare_buffer
 *         -EINVAL - invalid handle passed by application 
 *         -EINVAL - pathid address is NULL 
 *	   -ENOMEM - pathid table storage allocation failed
 *         add_pathid_result - return code from internal function add_pathid             
*/
int
iucv_connect (u16 * pathid, u16 msglim_reqstd,
	      uchar user_data[16], uchar userid[8],
	      uchar system_name[8], int flags1,
	      int *flags1_out, u16 * msglim,
	      iucv_handle_t handle, void *pgm_data)
{
	iparml_control parm;
	ulong b2f0_result = 0;
	ulong flags;
	int add_pathid_result = 0;
	handler *R = NULL;
	uchar no_memory[16] = "NO MEMORY";

	pr_debug ("iucv_connect: entering \n");

	/* Checking if handle is valid  */
	spin_lock_irqsave (&iucv_lock, flags);

	for (R = handler_anchor; R != NULL; R = (handler *) R->next)
		if (R == handle)
			break;

	spin_unlock_irqrestore (&iucv_lock, flags);

	if (R == NULL) {
		printk (KERN_WARNING "iucv_connect: NULL handle passed by"
			"application\n");
		return -EINVAL;
	}

	if (pathid == NULL) {
		printk (KERN_WARNING "iucv_connect: NULL pathid pointer\n");
		return -EINVAL;
	}
	memset (&parm, 0, sizeof (iparml_control));
	parm.ipmsglim = msglim_reqstd;

	if (user_data)
		memcpy (parm.ipuser, user_data, sizeof (parm.ipuser));

	if (userid) {
		memcpy (parm.ipvmid, userid, sizeof (parm.ipvmid));
		ASCEBC (parm.ipvmid, sizeof (parm.ipvmid));
		EBC_TOUPPER (parm.ipvmid, sizeof (parm.ipvmid));
	}

	if (system_name) {
		memcpy (parm.iptarget, system_name, sizeof (parm.iptarget));
		ASCEBC (parm.iptarget, sizeof (parm.iptarget));
		EBC_TOUPPER (parm.iptarget, sizeof (parm.iptarget));
	}

	parm.ipflags1 = (uchar) flags1;
	b2f0_result = b2f0 (CONNECT, &parm);
	if (b2f0_result)
		return b2f0_result;

	add_pathid_result = iucv_add_pathid (parm.ippathid, handle, pgm_data);
	if (add_pathid_result) {

		iucv_sever (parm.ippathid, no_memory);
		printk (KERN_WARNING "iucv_connect: add_pathid failed with rc ="
			"%d\n", add_pathid_result);
		return (add_pathid_result);
	}

	*pathid = parm.ippathid;

	if (msglim)
		*msglim = parm.ipmsglim;

	if (parm.ipflags1 & IPPRTY)
		if (flags1_out) {
			*flags1_out = 0;
			*flags1_out |= IPPRTY;
		}

	pr_debug ("iucv_connect: exiting\n");
	return b2f0_result;
}

/*
 * Name: iucv_purge                                           
 * Purpose: Cancels a message you have sent.                   
 * Input: pathid -   address of pathid                                  
 *        msgid  -   address of message identification             
 *        srccls -   address of source message class                     
 *        audit  -  contains information about                              
 *                 asynchronous error that may have affected                           
 *                 the normal completion of this message.                  
 * Output:b2f0_result - return code from CP                   
*/
int
iucv_purge (u16 pathid, u32 msgid, u32 srccls, uchar audit[3])
{
	iparml_purge parm;
	ulong b2f0_result = 0;
	pr_debug ("iucv_purge: entering\n");
	pr_debug ("iucv_purge: pathid = %d \n", pathid);
	memset (&parm, 0, sizeof (parm));
	parm.ipmsgid = msgid;
	parm.ippathid = pathid;
	parm.ipsrccls = srccls;
	parm.ipflags1 |= (IPSRCCLS | IPFGMID | IPFGPID);
	b2f0_result = b2f0 (PURGE, &parm);

	if ((b2f0_result == 0) && (audit))
		memcpy (audit, parm.ipaudit, sizeof (parm.ipaudit));

	pr_debug ("iucv_purge: b2f0_result = %ld \n", b2f0_result);
	pr_debug ("iucv_purge: exiting\n");
	return b2f0_result;
}

/*
 * Name: iucv_query_maxconn                                            
 * Purpose: Determines the maximum number of connections thay may be established.
 * Output: maxconn - ulong: Maximum number of connections that can be.     
*/
ulong
iucv_query_maxconn (void)
{
	iparml_purge parm;	/* DOESN'T MATTER WHICH IPARML IS USED    */
	static u32 maxconn1, bufsize1;

	pr_debug ("iucv_query_maxconn: entering\n");

	memset (&parm, 0, sizeof (parm));

	/* Assembler instruction calling b2f0  and storing R0 and R1 */
	asm volatile ("LRA   1,0(%3)\n\t"
		      "LR    0,%2\n\t"
		      ".long 0xb2f01000\n\t"
		      "ST    0,%0\n\t"
		      "ST    1,%1\n\t":"=m" (bufsize1),
		      "=m" (maxconn1):"d" (QUERY), "a" (&parm):"0", "1");

	pr_debug (" bufsize1 = %d and maxconn1 = %d \n", bufsize1, maxconn1);
	pr_debug ("iucv_query_maxconn: exiting\n");

	return maxconn1;
}

/*
 * Name: iucv_query_bufsize
 * Purpose: Determines the size of the external interrupt buffer.
 * Output: bufsize - ulong: Size of external interrupt buffer.
 */
ulong
iucv_query_bufsize (void)
{
	iparml_purge parm;	/* DOESN'T MATTER WHICH IPARML IS USED    */
	static u32 maxconn1, bufsize1;

	pr_debug ("iucv_query_bufsize: entering\n");
	pr_debug ("iucv_query_maxconn: entering\n");

	memset (&parm, 0, sizeof (parm));

	/* Assembler instruction calling b2f0  and storing R0 and R1 */
	asm volatile ("LRA   1,0(%3)\n\t"
		      "LR    0,%2\n\t"
		      ".long 0xb2f01000\n\t"
		      "ST    0,%0\n\t"
		      "ST    1,%1\n\t":"=m" (bufsize1),
		      "=m" (maxconn1):"d" (QUERY), "a" (&parm):"0", "1");

	pr_debug (" bufsize1 = %d and maxconn1 = %d \n", bufsize1, maxconn1);
	pr_debug ("iucv_query_bufsize: exiting\n");

	return bufsize1;
}

/*
 * Name: iucv_quiesce                                         
 * Purpose: temporarily suspends incoming messages on an IUCV path.
 *          You can later reactivate the path by invoking the iucv_resume function        
 * Input: pathid - u16, path identification number                             
 *        user_data - uchar[16], 16-byte user data                      
 * Output: b2f0_result - return code from CP               
 */
int
iucv_quiesce (u16 pathid, uchar user_data[16])
{
	iparml_control parm;
	ulong b2f0_result = 0;

	pr_debug ("iucv_quiesce: entering \n");
	pr_debug ("iucv_quiesce: pathid = %d\n", pathid);

	memset (&parm, 0, sizeof (parm));
	memcpy (parm.ipuser, user_data, sizeof (parm.ipuser));
	parm.ippathid = pathid;

	b2f0_result = b2f0 (QUIESCE, &parm);

	pr_debug ("iucv_quiesce: b2f0_result = %ld\n", b2f0_result);
	pr_debug ("iucv_quiesce: exiting\n");

	return b2f0_result;
}

/*
 * Name: iucv_receive
 * Purpose: This function receives messages that are being sent to you
 *          over established paths. 
 * Input:
 *        pathid - path identification number
 *        buffer - address of buffer to receive
 *        buflen - length of buffer to receive
 *        msgid - specifies the message ID.
 *        trgcls - specifies target class
 * Output:
 *	 flags1_out: Options for path.
 *         IPNORPY - 0x10 specifies whether a reply is required
 *         IPPRTY - 0x20 specifies if you want to send priority message
 *         IPRMDATA - 0x80 specifies the data is contained in the parameter list
 *       residual_buffer - address of buffer updated by the number
 *                         of bytes you have received.
 *       residual_length - 
 *		Contains one of the following values, if the receive buffer is:
 *		 The same length as the message, this field is zero.
 *		 Longer than the message, this field contains the number of
 *		  bytes remaining in the buffer.
 *		 Shorter than the message, this field contains the residual
 *		  count (that is, the number of bytes remaining in the
 *		  message that does not fit into the buffer. In this
 *		  case b2f0_result = 5. 
 * Return: b2f0_result - return code from CP IUCV call.
 *         (-EINVAL) - buffer address is pointing to NULL
 */
int
iucv_receive (u16 pathid, u32 msgid, u32 trgcls,
	      void *buffer, ulong buflen,
	      int *flags1_out, ulong * residual_buffer, ulong * residual_length)
{
	iparml_db parm;
	ulong b2f0_result;
	int moved = 0;		/* number of bytes moved from parmlist to buffer */
	pr_debug ("iucv_receive: entering\n");

	if (!buffer)
		return -EINVAL;

	memset (&parm, 0, sizeof (parm));
	parm.ipbfadr1 = (u32) buffer;
	parm.ipbfln1f = (u32) ((ulong) buflen);
	parm.ipmsgid = msgid;
	parm.ippathid = pathid;
	parm.iptrgcls = trgcls;
	parm.ipflags1 = (IPFGPID | IPFGMID | IPFGMCL);

	b2f0_result = b2f0 (RECEIVE, &parm);

	if (b2f0_result == 0 || b2f0_result == 5) {
		if (flags1_out) {
			pr_debug ("*flags1_out = %d\n", *flags1_out);
			*flags1_out = (parm.ipflags1 & (~0x07));
			pr_debug ("*flags1_out = %d\n", *flags1_out);
		}

		if (!(parm.ipflags1 & IPRMDATA)) {	/*msg not in parmlist */
			if (residual_length)
				*residual_length = parm.ipbfln1f;

			if (residual_buffer)
				*residual_buffer = parm.ipbfadr1;
		} else {
			moved = min_t(unsigned int, buflen, 8);

			memcpy ((char *) buffer,
				(char *) &parm.ipbfadr1, moved);

			if (buflen < 8)
				b2f0_result = 5;

			if (residual_length)
				*residual_length = abs (buflen - 8);

			if (residual_buffer)
				*residual_buffer = (ulong) (buffer + moved);
		}
	}
	pr_debug ("iucv_receive: exiting \n");
	return b2f0_result;
}

/*
 * Name: iucv_receive_array
 * Purpose: This function receives messages that are being sent to you
 *          over established paths. 
 * Input: pathid - path identification number
 *        buffer - address of array of buffers
 *        buflen - total length of buffers
 *        msgid - specifies the message ID.
 *        trgcls - specifies target class
 * Output:
 *        flags1_out: Options for path.
 *          IPNORPY - 0x10 specifies whether a reply is required
 *          IPPRTY - 0x20 specifies if you want to send priority message
 *         IPRMDATA - 0x80 specifies the data is contained in the parameter list
 *       residual_buffer - address points to the current list entry IUCV
 *                         is working on.
 *       residual_length -
 *              Contains one of the following values, if the receive buffer is:
 *               The same length as the message, this field is zero.
 *               Longer than the message, this field contains the number of
 *                bytes remaining in the buffer.
 *               Shorter than the message, this field contains the residual
 *                count (that is, the number of bytes remaining in the
 *                message that does not fit into the buffer. In this case
 *		  b2f0_result = 5.
 * Return: b2f0_result - return code from CP
 *         (-EINVAL) - buffer address is NULL
 */
int
iucv_receive_array (u16 pathid,
		    u32 msgid, u32 trgcls,
		    iucv_array_t * buffer, ulong buflen,
		    int *flags1_out,
		    ulong * residual_buffer, ulong * residual_length)
{
	iparml_db parm;
	ulong b2f0_result;
	int i = 0, moved = 0, need_to_move = 8, dyn_len;
	pr_debug ("iucv_receive_array: entering\n");

	if (!buffer)
		return -EINVAL;

	memset (&parm, 0, sizeof (parm));
	parm.ipbfadr1 = (u32) ((ulong) buffer);
	parm.ipbfln1f = (u32) buflen;
	parm.ipmsgid = msgid;
	parm.ippathid = pathid;
	parm.iptrgcls = trgcls;
	parm.ipflags1 = (IPBUFLST | IPFGPID | IPFGMID | IPFGMCL);

	b2f0_result = b2f0 (RECEIVE, &parm);

	if (b2f0_result == 0 || b2f0_result == 5) {

		if (flags1_out) {
			pr_debug ("*flags1_out = %d\n", *flags1_out);
			*flags1_out = (parm.ipflags1 & (~0x07));
			pr_debug ("*flags1_out = %d\n", *flags1_out);
		}

		if (!(parm.ipflags1 & IPRMDATA)) {	/*msg not in parmlist */

			if (residual_length)
				*residual_length = parm.ipbfln1f;

			if (residual_buffer)
				*residual_buffer = parm.ipbfadr1;

		} else {
			/* copy msg from parmlist to users array. */

			while ((moved < 8) && (moved < buflen)) {
				dyn_len =
				    min_t(unsigned int,
					(buffer + i)->length, need_to_move);

				memcpy ((char *)((ulong)((buffer + i)->address)),
					((char *) &parm.ipbfadr1) + moved,
					dyn_len);

				moved += dyn_len;
				need_to_move -= dyn_len;

				(buffer + i)->address =
				    	(u32)  
				((ulong)(uchar *) ((ulong)(buffer + i)->address) 
						+ dyn_len);

				(buffer + i)->length -= dyn_len;
				i++;
			}

			if (need_to_move)	/* buflen < 8 bytes */
				b2f0_result = 5;

			if (residual_length)
				*residual_length = abs (buflen - 8);

			if (residual_buffer) {
				if (moved == 0)
					*residual_buffer = (ulong) buffer;
				else
					*residual_buffer =
					    (ulong) (buffer + (i - 1));
			}

		}
	}

	pr_debug ("iucv_receive_array: exiting\n");
	return b2f0_result;
}

/*
 * Name: iucv_reject                                          
 * Purpose: Refuses a specified message. Between the time you are notified of a 
 *          message and the time that you complete the message, the message may
 *          be rejected.                                 
 * Input: pathid - u16, path identification number.                            
 *        msgid  - u32, specifies the message ID.
 *        trgcls - u32, specifies target class.                
 * Output: b2f0_result - return code from CP
 * NOTE: see b2f0 output list                                 
*/
int
iucv_reject (u16 pathid, u32 msgid, u32 trgcls)
{
	iparml_db parm;
	ulong b2f0_result = 0;

	pr_debug ("iucv_reject: entering \n");
	pr_debug ("iucv_reject: pathid = %d\n", pathid);

	memset (&parm, 0, sizeof (parm));
	parm.ippathid = pathid;
	parm.ipmsgid = msgid;
	parm.iptrgcls = trgcls;
	parm.ipflags1 = (IPFGMCL | IPFGMID | IPFGPID);

	b2f0_result = b2f0 (REJECT, &parm);

	pr_debug ("iucv_reject: b2f0_result = %ld\n", b2f0_result);
	pr_debug ("iucv_reject: exiting\n");

	return b2f0_result;
}

/* 
 * Name: iucv_reply
 * Purpose: This function responds to the two-way messages that you
 *          receive. You must identify completely the message to
 *          which you wish to reply. ie, pathid, msgid, and trgcls.
 * Input: pathid - path identification number
 *        msgid - specifies the message ID. 
 *        trgcls - specifies target class
 *        flags1 - option for path
 *                 IPPRTY- 0x20 - specifies if you want to send priority message
 *        buffer - address of reply buffer
 *        buflen - length of reply buffer
 * Output: ipbfadr2 - Address of buffer updated by the number
 *                    of bytes you have moved.
 *         ipbfln2f - Contains on the the following values
 *              If the answer buffer is the same length as the reply, this field
 *               contains zero.
 *              If the answer buffer is longer than the reply, this field contains
 *               the number of bytes remaining in the buffer.
 *              If the answer buffer is shorter than the reply, this field contains
 *               a residual count (that is, the number of bytes remianing in the
 *               reply that does not fit into the buffer. In this
 *                case b2f0_result = 5.
 * Return: b2f0_result - return code from CP
 *         (-EINVAL) - buffer address is NULL
 */
int
iucv_reply (u16 pathid,
	    u32 msgid, u32 trgcls,
	    int flags1,
	    void *buffer, ulong buflen, ulong * ipbfadr2, ulong * ipbfln2f)
{
	iparml_db parm;
	ulong b2f0_result;

	pr_debug ("iucv_reply: entering\n");

	if (!buffer)
		return -EINVAL;

	memset (&parm, 0, sizeof (parm));
	parm.ipbfadr2 = (u32) ((ulong) buffer);
	parm.ipbfln2f = (u32) buflen;	/* length of message */
	parm.ippathid = pathid;
	parm.ipmsgid = msgid;
	parm.iptrgcls = trgcls;
	parm.ipflags1 = (uchar) flags1;	/* priority message */

	b2f0_result = b2f0 (REPLY, &parm);

	if ((b2f0_result == 0) || (b2f0_result == 5)) {
		if (ipbfadr2)
			*ipbfadr2 = parm.ipbfadr2;
		if (ipbfln2f)
			*ipbfln2f = parm.ipbfln2f;
	}

	pr_debug ("iucv_reply: exiting\n");

	return b2f0_result;
}

/*
 * Name: iucv_reply_array
 * Purpose: This function responds to the two-way messages that you
 *          receive. You must identify completely the message to   
 *          which you wish to reply. ie, pathid, msgid, and trgcls.
 *          The array identifies a list of addresses and lengths of
 *          discontiguous buffers that contains the reply data.
 * Input: pathid - path identification number
 *        msgid - specifies the message ID. 
 *        trgcls - specifies target class 
 *        flags1 - option for path
 *                 IPPRTY- specifies if you want to send priority message
 *        buffer - address of array of reply buffers
 *        buflen - total length of reply buffers
 * Output: ipbfadr2 - Address of buffer which IUCV is currently working on.
 *         ipbfln2f - Contains on the the following values
 *              If the answer buffer is the same length as the reply, this field
 *               contains zero.
 *              If the answer buffer is longer than the reply, this field contains
 *               the number of bytes remaining in the buffer.
 *              If the answer buffer is shorter than the reply, this field contains
 *               a residual count (that is, the number of bytes remianing in the
 *               reply that does not fit into the buffer. In this
 *               case b2f0_result = 5.
 * Return: b2f0_result - return code from CP
 *             (-EINVAL) - buffer address is NULL
*/
int
iucv_reply_array (u16 pathid,
		  u32 msgid, u32 trgcls,
		  int flags1,
		  iucv_array_t * buffer,
		  ulong buflen, ulong * ipbfadr2, ulong * ipbfln2f)
{
	iparml_db parm;
	ulong b2f0_result;

	pr_debug ("iucv_reply_array: entering\n");

	if (!buffer)
		return -EINVAL;

	memset (&parm, 0, sizeof (parm));
	parm.ipbfadr2 = (u32) ((ulong) buffer);
	parm.ipbfln2f = buflen;	/* length of message */
	parm.ippathid = pathid;
	parm.ipmsgid = msgid;
	parm.iptrgcls = trgcls;
	parm.ipflags1 = (IPANSLST | flags1);

	b2f0_result = b2f0 (REPLY, &parm);

	if ((b2f0_result == 0) || (b2f0_result == 5)) {

		if (ipbfadr2)
			*ipbfadr2 = parm.ipbfadr2;
		if (ipbfln2f)
			*ipbfln2f = parm.ipbfln2f;
	}

	pr_debug ("iucv_reply_array: exiting\n");

	return b2f0_result;
}

/*
 * Name: iucv_reply_prmmsg
 * Purpose: This function responds to the two-way messages that you
 *          receive. You must identify completely the message to
 *          which you wish to reply. ie, pathid, msgid, and trgcls.
 *          Prmmsg signifies the data is moved into the
 *          parameter list.
 * Input: pathid - path identification number
 *        msgid - specifies the message ID. 
 *        trgcls - specifies target class
 *        flags1 - option for path
 *                 IPPRTY- specifies if you want to send priority message
 *        prmmsg - 8-bytes of data to be placed into the parameter
 *                 list.
 * Output: NA
 * Return: b2f0_result - return code from CP
*/
int
iucv_reply_prmmsg (u16 pathid,
		   u32 msgid, u32 trgcls, int flags1, uchar prmmsg[8])
{
	iparml_dpl parm;
	ulong b2f0_result;

	pr_debug ("iucv_reply_prmmsg: entering\n");

	memset (&parm, 0, sizeof (parm));
	parm.ippathid = pathid;
	parm.ipmsgid = msgid;
	parm.iptrgcls = trgcls;
	memcpy (parm.iprmmsg, prmmsg, sizeof (parm.iprmmsg));
	parm.ipflags1 = (IPRMDATA | flags1);

	b2f0_result = b2f0 (REPLY, &parm);

	pr_debug ("iucv_reply_prmmsg: exiting\n");

	return b2f0_result;
}

/*
 * Name: iucv_resume                                          
 * Purpose: This function restores communication over a quiesced path.       
 * Input: pathid - u16, path identification number                             
 *        user_data - uchar[16], 16-byte of user data                     
 * Output: b2f0_result - return code from CP               
 */
int
iucv_resume (u16 pathid, uchar user_data[16])
{
	iparml_control parm;
	ulong b2f0_result = 0;

	pr_debug ("iucv_resume: entering\n");
	pr_debug ("iucv_resume: pathid = %d\n", pathid);

	memset (&parm, 0, sizeof (parm));
	memcpy (parm.ipuser, user_data, sizeof (*user_data));
	parm.ippathid = pathid;

	b2f0_result = b2f0 (RESUME, &parm);

	pr_debug ("iucv_resume: exiting \n");

	return b2f0_result;
}

/*
 * Name: iucv_send                                             
 * Purpose: sends messages                                    
 * Input: pathid - ushort, pathid                            
 *        msgid  - ulong *, id of message returned to caller   
 *        trgcls - ulong, target message class              
 *        srccls - ulong, source message class              
 *        msgtag - ulong, message tag                    
 *	  flags1  - Contains options for this path.
 *		IPPRTY - Ox20 - specifies if you want to send a priority message.
 *        buffer - pointer to buffer                           
 *        buflen - ulong, length of buffer                     
 * Output: b2f0_result - return code from b2f0 call               
 *         msgid - returns message id                         
 */
int
iucv_send (u16 pathid, u32 * msgid,
	   u32 trgcls, u32 srccls,
	   u32 msgtag, int flags1, void *buffer, ulong buflen)
{
	iparml_db parm;
	ulong b2f0_result;

	pr_debug ("iucv_send: entering\n");

	if (!buffer)
		return -EINVAL;

	memset (&parm, 0, sizeof (parm));
	parm.ipbfadr1 = (u32) ((ulong) buffer);
	parm.ippathid = pathid;
	parm.iptrgcls = trgcls;
	parm.ipbfln1f = (u32) buflen;	/* length of message */
	parm.ipsrccls = srccls;
	parm.ipmsgtag = msgtag;
	parm.ipflags1 = (IPNORPY | flags1);	/* one way priority message */

	b2f0_result = b2f0 (SEND, &parm);

	if ((b2f0_result == 0) && (msgid))
		*msgid = parm.ipmsgid;

	pr_debug ("iucv_send: exiting\n");

	return b2f0_result;
}

/*
 * Name: iucv_send_array
 * Purpose: This function transmits data to another application.
 *          The contents of buffer is the address of the array of
 *          addresses and lengths of discontiguous buffers that hold
 *          the message text. This is a one-way message and the
 *          receiver will not reply to the message.
 * Input: pathid - path identification number
 *        trgcls - specifies target class
 *        srccls - specifies the source message class
 *        msgtag - specifies a tag to be associated witht the message
 *        flags1 - option for path
 *                 IPPRTY- specifies if you want to send priority message
 *        buffer - address of array of send buffers
 *        buflen - total length of send buffers
 * Output: msgid - specifies the message ID.
 * Return: b2f0_result - return code from CP 
 *         (-EINVAL) - buffer address is NULL
 */
int
iucv_send_array (u16 pathid,
		 u32 * msgid,
		 u32 trgcls,
		 u32 srccls,
		 u32 msgtag, int flags1, iucv_array_t * buffer, ulong buflen)
{
	iparml_db parm;
	ulong b2f0_result;

	pr_debug ("iucv_send_array: entering\n");

	if (!buffer)
		return -EINVAL;

	memset (&parm, 0, sizeof (parm));
	parm.ippathid = pathid;
	parm.iptrgcls = trgcls;
	parm.ipbfadr1 = (u32) ((ulong) buffer);
	parm.ipbfln1f = (u32) buflen;	/* length of message */
	parm.ipsrccls = srccls;
	parm.ipmsgtag = msgtag;
	parm.ipflags1 = (IPNORPY | IPBUFLST | flags1);
	b2f0_result = b2f0 (SEND, &parm);

	if ((b2f0_result == 0) && (msgid))
		*msgid = parm.ipmsgid;
	pr_debug ("iucv_send_array: exiting\n");
	return b2f0_result;
}

/*
 * Name: iucv_send_prmmsg
 * Purpose: This function transmits data to another application. 
 *          Prmmsg specifies that the 8-bytes of data are to be moved
 *          into the parameter list. This is a one-way message and the
 *          receiver will not reply to the message.
 * Input: pathid - path identification number
 *        trgcls - specifies target class
 *        srccls - specifies the source message class
 *        msgtag - specifies a tag to be associated with the message
 *        flags1 - option for path
 *                 IPPRTY- specifies if you want to send priority message
 *        prmmsg - 8-bytes of data to be placed into parameter list
 * Output: msgid - specifies the message ID.   
 * Return: b2f0_result - return code from CP
*/
int
iucv_send_prmmsg (u16 pathid,
		  u32 * msgid,
		  u32 trgcls,
		  u32 srccls, u32 msgtag, int flags1, uchar prmmsg[8])
{
	iparml_dpl parm;
	ulong b2f0_result;

	pr_debug ("iucv_send_prmmsg: entering\n");

	memset (&parm, 0, sizeof (parm));
	parm.ippathid = pathid;
	parm.iptrgcls = trgcls;
	parm.ipsrccls = srccls;
	parm.ipmsgtag = msgtag;
	parm.ipflags1 = (IPRMDATA | IPNORPY | flags1);
	memcpy (parm.iprmmsg, prmmsg, sizeof (parm.iprmmsg));

	b2f0_result = b2f0 (SEND, &parm);

	if ((b2f0_result == 0) && (msgid))
		*msgid = parm.ipmsgid;

	pr_debug ("iucv_send_prmmsg: exiting\n");
	return b2f0_result;
}

/*
 * Name: iucv_send2way
 * Purpose: This function transmits data to another application.
 *          Data to be transmitted is in a buffer. The receiver
 *          of the send is expected to reply to the message and
 *          a buffer is provided into which IUCV moves the reply 
 *          to this message.
 * Input: pathid - path identification number
 *        trgcls - specifies target class
 *        srccls - specifies the source message class
 *        msgtag - specifies a tag associated with the message
 *        flags1 - option for path
 *                 IPPRTY- specifies if you want to send priority message
 *        buffer - address of send buffer
 *        buflen - length of send buffer
 *        ansbuf - address of buffer to reply with
 *        anslen - length of buffer to reply with
 * Output: msgid - specifies the message ID.
 * Return: b2f0_result - return code from CP
 *         (-EINVAL) - buffer or ansbuf address is NULL
 */
int
iucv_send2way (u16 pathid,
	       u32 * msgid,
	       u32 trgcls,
	       u32 srccls,
	       u32 msgtag,
	       int flags1,
	       void *buffer, ulong buflen, void *ansbuf, ulong anslen)
{
	iparml_db parm;
	ulong b2f0_result;
	pr_debug ("iucv_send2way: entering\n");

	if (!buffer || !ansbuf)
		return -EINVAL;

	memset (&parm, 0, sizeof (parm));
	parm.ippathid = pathid;
	parm.iptrgcls = trgcls;
	parm.ipbfadr1 = (u32) ((ulong) buffer);
	parm.ipbfln1f = (u32) buflen;	/* length of message */
	parm.ipbfadr2 = (u32) ((ulong) ansbuf);
	parm.ipbfln2f = (u32) anslen;
	parm.ipsrccls = srccls;
	parm.ipmsgtag = msgtag;
	parm.ipflags1 = flags1;	/* priority message */

	b2f0_result = b2f0 (SEND, &parm);

	if ((b2f0_result == 0) && (msgid))
		*msgid = parm.ipmsgid;

	pr_debug ("iucv_send2way: exiting\n");

	return b2f0_result;
}

/*
 * Name: iucv_send2way_array
 * Purpose: This function transmits data to another application.
 *          The contents of buffer is the address of the array of
 *          addresses and lengths of discontiguous buffers that hold
 *          the message text. The receiver of the send is expected to
 *          reply to the message and a buffer is provided into which
 *          IUCV moves the reply to this message.
 * Input: pathid - path identification number
 *        trgcls - specifies target class
 *        srccls - specifies the source message class
 *        msgtag - spcifies a tag to be associated with the message
 *        flags1 - option for path
 *                 IPPRTY- specifies if you want to send priority message
 *        buffer - address of array of send buffers
 *        buflen - total length of send buffers
 *        ansbuf - address of buffer to reply with                       
 *        anslen - length of buffer to reply with     
 * Output: msgid - specifies the message ID.
 * Return: b2f0_result - return code from CP
 *         (-EINVAL) - buffer address is NULL
 */
int
iucv_send2way_array (u16 pathid,
		     u32 * msgid,
		     u32 trgcls,
		     u32 srccls,
		     u32 msgtag,
		     int flags1,
		     iucv_array_t * buffer,
		     ulong buflen, iucv_array_t * ansbuf, ulong anslen)
{
	iparml_db parm;
	ulong b2f0_result;

	pr_debug ("iucv_send2way_array: entering\n");

	if (!buffer || !ansbuf)
		return -EINVAL;

	memset (&parm, 0, sizeof (parm));
	parm.ippathid = pathid;
	parm.iptrgcls = trgcls;
	parm.ipbfadr1 = (u32) ((ulong) buffer);
	parm.ipbfln1f = (u32) buflen;	/* length of message */
	parm.ipbfadr2 = (u32) ((ulong) ansbuf);
	parm.ipbfln2f = (u32) anslen;
	parm.ipsrccls = srccls;
	parm.ipmsgtag = msgtag;
	parm.ipflags1 = (IPBUFLST | IPANSLST | flags1);
	b2f0_result = b2f0 (SEND, &parm);
	if ((b2f0_result == 0) && (msgid))
		*msgid = parm.ipmsgid;
	pr_debug ("iucv_send2way_array: exiting\n");
	return b2f0_result;
}

/*
 * Name: iucv_send2way_prmmsg
 * Purpose: This function transmits data to another application.
 *          Prmmsg specifies that the 8-bytes of data are to be moved
 *          into the parameter list. This is a two-way message and the
 *          receiver of the message is expected to reply. A buffer
 *          is provided into which IUCV moves the reply to this
 *          message.
 * Input: pathid - path identification number  
 *        trgcls - specifies target class
 *        srccls - specifies the source message class
 *        msgtag - specifies a tag to be associated with the message
 *        flags1 - option for path
 *                 IPPRTY- specifies if you want to send priority message
 *        prmmsg - 8-bytes of data to be placed in parameter list
 *        ansbuf - address of buffer to reply with                       
 *        anslen - length of buffer to reply with     
 * Output: msgid - specifies the message ID.
 * Return: b2f0_result - return code from CP
 *         (-EINVAL) - buffer address is NULL
*/
int
iucv_send2way_prmmsg (u16 pathid,
		      u32 * msgid,
		      u32 trgcls,
		      u32 srccls,
		      u32 msgtag,
		      ulong flags1, uchar prmmsg[8], void *ansbuf, ulong anslen)
{
	iparml_dpl parm;
	ulong b2f0_result;
	pr_debug ("iucv_send2way_prmmsg: entering\n");

	if (!ansbuf)
		return -EINVAL;

	memset (&parm, 0, sizeof (parm));
	parm.ippathid = pathid;
	parm.iptrgcls = trgcls;
	parm.ipsrccls = srccls;
	parm.ipmsgtag = msgtag;
	parm.ipbfadr2 = (u32) ((ulong) ansbuf);
	parm.ipbfln2f = (u32) anslen;
	parm.ipflags1 = (IPRMDATA | flags1);	/* message in prmlist */
	memcpy (parm.iprmmsg, prmmsg, sizeof (parm.iprmmsg));

	b2f0_result = b2f0 (SEND, &parm);

	if ((b2f0_result == 0) && (msgid))
		*msgid = parm.ipmsgid;

	pr_debug ("iucv_send2way_prmmsg: exiting\n");

	return b2f0_result;
}

/*
 * Name: iucv_send2way_prmmsg_array
 * Purpose: This function transmits data to another application.
 *          Prmmsg specifies that the 8-bytes of data are to be moved
 *          into the parameter list. This is a two-way message and the
 *          receiver of the message is expected to reply. A buffer
 *          is provided into which IUCV moves the reply to this
 *          message. The contents of ansbuf is the address of the
 *          array of addresses and lengths of discontiguous buffers
 *          that contain the reply.
 * Input: pathid - path identification number
 *        trgcls - specifies target class
 *        srccls - specifies the source message class   
 *        msgtag - specifies a tag to be associated with the message
 *        flags1 - option for path
 *                 IPPRTY- specifies if you want to send priority message
 *        prmmsg - 8-bytes of data to be placed into the parameter list
 *        ansbuf - address of buffer to reply with                       
 *        anslen - length of buffer to reply with     
 * Output: msgid - specifies the message ID.
 * Return: b2f0_result - return code from CP
 *         (-EINVAL) - ansbuf address is NULL
 */
int
iucv_send2way_prmmsg_array (u16 pathid,
			    u32 * msgid,
			    u32 trgcls,
			    u32 srccls,
			    u32 msgtag,
			    int flags1,
			    uchar prmmsg[8],
			    iucv_array_t * ansbuf, ulong anslen)
{
	iparml_dpl parm;
	ulong b2f0_result;

	pr_debug ("iucv_send2way_prmmsg_array: entering\n");

	if (!ansbuf)
		return -EINVAL;

	memset (&parm, 0, sizeof (parm));
	parm.ippathid = pathid;
	parm.iptrgcls = trgcls;
	parm.ipsrccls = srccls;
	parm.ipmsgtag = msgtag;
	parm.ipbfadr2 = (u32) ((ulong) ansbuf);
	parm.ipbfln2f = (u32) anslen;
	parm.ipflags1 = (IPRMDATA | IPANSLST | flags1);
	memcpy (parm.iprmmsg, prmmsg, sizeof (parm.iprmmsg));
	b2f0_result = b2f0 (SEND, &parm);
	if ((b2f0_result == 0) && (msgid))
		*msgid = parm.ipmsgid;
	pr_debug ("iucv_send2way_prmmsg_array: exiting\n");
	return b2f0_result;
}

/*
 * Name: iucv_setmask
 * Purpose: This function enables or disables the following IUCV   
 *          external interruptions: Nonpriority and priority message
 *          interrupts, nonpriority and priority reply interrupts.
 * Input: SetMaskFlag - options for interrupts
 *           0x80 - Nonpriority_MessagePendingInterruptsFlag
 *           0x40 - Priority_MessagePendingInterruptsFlag
 *           0x20 - Nonpriority_MessageCompletionInterruptsFlag
 *           0x10 - Priority_MessageCompletionInterruptsFlag
 * Output: NA
 * Return: b2f0_result - return code from CP
*/
int
iucv_setmask (int SetMaskFlag)
{
	iparml_set_mask parm;
	ulong b2f0_result = 0;
	pr_debug ("iucv_setmask: entering \n");

	memset (&parm, 0, sizeof (parm));
	parm.ipmask = (uchar) SetMaskFlag;

	b2f0_result = b2f0 (SETMASK, &parm);

	pr_debug ("iucv_setmask: b2f0_result = %ld\n", b2f0_result);
	pr_debug ("iucv_setmask: exiting\n");

	return b2f0_result;
}

/*
 * Name: iucv_sever                                           
 * Purpose: This function terminates an iucv path         
 * Input: pathid - u16, path identification number                             
 *        user_data - uchar[16], 16-byte of user data                     
 * Output: b2f0_result - return code from CP          
 *         -EINVAL - NULL address found for handler                                 
 */
int
iucv_sever (u16 pathid, uchar user_data[16])
{
	iparml_control parm;
	ulong b2f0_result = 0;
	pr_debug ("iucv_sever: entering\n");
	memset (&parm, 0, sizeof (parm));
	memcpy (parm.ipuser, user_data, sizeof (parm.ipuser));
	parm.ippathid = pathid;

	b2f0_result = b2f0 (SEVER, &parm);

	if (!b2f0_result)
		iucv_remove_pathid (pathid);

	pr_debug ("iucv_sever: exiting \n");
	return b2f0_result;
}

static void
iucv_remove_pathid (u16 pathid)
{
	handler_table_entry *users_hte = NULL;	/*users handler_table_entry */
	handler *users_handler = NULL;
	ulong *users_pathid = NULL;
	ulong flags;
	spin_lock_irqsave (&iucv_lock, flags);
	users_hte = handler_table_anchor + (int) pathid;

	if ((users_hte->addrs) == NULL) {
		spin_unlock_irqrestore (&iucv_lock, flags);
		return;		/* wild pointer has been found */
	}

	users_handler = users_hte->addrs;

	pr_debug ("iucv_sever: pathid is %d\n", pathid);
	pr_debug ("iucv_sever: H_T_E is %p\n", users_hte);
	pr_debug ("iucv_sever: address of handler is %p\n", users_handler);
	pr_debug ("iucv_sever: below is pathid table\n");
	iucv_dumpit ((uchar *) users_handler->pathid_head,
		     (int) users_handler->entries * sizeof (ulong));

/*
 * Searching the pathid address table for matching address, once    
 * found, NULL the handler_table_entry field and then zero the H_T_E fields.               
 */

	for (users_pathid = (users_handler->pathid_head);
	     users_pathid < (users_handler->pathid_tail); users_pathid++)

		if (*users_pathid == (ulong) users_hte) {
			pr_debug ("iucv_sever: found a path to remove from"
				  "table\n");
			pr_debug ("iucv_sever: removing %d \n",
				  (int) (*users_pathid)); *users_pathid = NULL;

			memset (users_hte, 0, sizeof (handler_table_entry));
		}
	spin_unlock_irqrestore (&iucv_lock, flags);
	return;
}

/*
 * Interrupt Handling Functions
 * 	top_half_interrupt
 * 	bottom_half_interrupt
 * 	do_int
 */

/*
 * Name: top_half_interrupt                                         
 * Purpose: Handles interrupts coming in from CP.  Places the interrupt on a queue and  
 * 	    calls bottom_half_interrupt          
 * Input: external interrupt buffer                                
 * Output: void                                                    
 */

inline void
top_half_interrupt (struct pt_regs *regs, __u16 code)
{
	iucv_packet *pkt;
	int cpu = smp_processor_id();

	irq_enter(cpu, 0x4000);

	pkt = (iucv_packet *) kmalloc (sizeof (iucv_packet), GFP_ATOMIC);
	if (pkt == NULL) {
		printk (KERN_WARNING
			"iucv:top_half_interrupt: out of memory\n");
		irq_exit(cpu, 0x4000);
		return;
	}

	memcpy (pkt->data, iucv_external_int_buffer, BUFFER_SIZE);

	pr_debug ("TH: Got INT: %08x\n", *(int *) (pkt->data + 4));

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
		short_task.routine = (void *) bottom_half_interrupt;
		queue_task (&short_task, &tq_immediate);
		mark_bh (IMMEDIATE_BH);
	}
	irq_exit(cpu, 0x4000);
	return;
}

/*
 * Name: bottom_half_interrupt                                      
 * Purpose: Handle interrupt at a more safer time                  
 * Input: void                                                     
 * Output: void                                                    
 */
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
	pr_debug ("BH: Process all packets\n");
	while (iucv_packet_list != NULL) {
		pr_debug ("BH:>  %08x\n",
			  *(int *) (iucv_packet_list->data + 4));

		do_int ((iucv_GeneralInterrupt *) iucv_packet_list->data);

		pr_debug ("BH:<  %08x\n",
			  *(int *) (iucv_packet_list->data + 4));
		tmp = iucv_packet_list;
		iucv_packet_list = iucv_packet_list->next;
		kfree (tmp);
	}
	pr_debug ("BH: Done\n");
	return;
}

/*
 * Name: do_int                                                     
 * Purpose: Handles the interrupts in a more safe environment             
 * Input: int_buf - pointer to copy of external interrupt buffer  
 * Output: void                                                     
 */
void
do_int (iucv_GeneralInterrupt * int_buf)
{
	handler_table_entry *P = 0;
	ulong flags;
	handler *Q = 0, *R;
	iucv_interrupt_ops_t *interrupt = 0;	/* interrupt addresses */
	uchar temp_buff1[24], temp_buff2[24];	/* masked handler id. */
	int add_pathid_result = 0, j = 0;
	uchar no_listener[16] = "NO LISTENER";

	pr_debug ("IUCV: BHI: -  Entered do_int "
		  "pathid %d, type %02X\n", int_buf->ippathid, int_buf->iptype);
	pr_debug ("BHI:External Interrupt Buffer\n");
	iucv_dumpit ((uchar *) int_buf, sizeof (iucv_GeneralInterrupt));

	ASCEBC (no_listener, 16);
	if (int_buf->iptype != 01) {
		spin_lock_irqsave (&iucv_lock, flags);
		P = handler_table_anchor + int_buf->ippathid;
		Q = P->addrs;
		interrupt = P->ops;	/* interrupt functions */

		pr_debug ("iucv: do_int: Handler\n");
		iucv_dumpit ((uchar *) Q, sizeof (handler));
		spin_unlock_irqrestore (&iucv_lock, flags);
	}
	/* end of if statement */
	switch (int_buf->iptype) {
	case 0x01:		/* connection pending */
		spin_lock_irqsave (&iucv_lock, flags);
		for (R = handler_anchor; R != NULL; R = (handler *) R->next) {
			memcpy (temp_buff1, &(int_buf->ipvmid), 24);
			memcpy (temp_buff2, &(R->id.userid), 24);
			for (j = 0; j < 24; j++) {
				temp_buff1[j] =
				    (temp_buff1[j]) & (R->id.mask)[j];
				temp_buff2[j] =
				    (temp_buff2[j]) & (R->id.mask)[j];
			}

			pr_debug ("iucv:do_int: temp_buff1\n");
			iucv_dumpit (temp_buff1, sizeof (temp_buff1));
			pr_debug ("iucv:do_int: temp_buff2\n");
			iucv_dumpit (temp_buff2, sizeof (temp_buff2));

			if (memcmp ((void *) temp_buff1,
				    (void *) temp_buff2, 24) == 0) {

				pr_debug
				    ("iucv:do_int: found a matching handler\n");
				break;
			}
		}
		spin_unlock_irqrestore (&iucv_lock, flags);

		if (R) {
			/* ADD PATH TO PATHID TABLE */
			add_pathid_result = iucv_add_pathid (int_buf->ippathid,
							     R, R->pgm_data);
			if (add_pathid_result == NULL) {
				interrupt = R->interrupt_table;
				if (interrupt->ConnectionPending) {

					EBCASC (int_buf->ipvmid, 8);

					(interrupt->ConnectionPending)
					    ((iucv_ConnectionPending *) int_buf,
					     (R->pgm_data));
				} else {
					iucv_sever (int_buf->ippathid,
						    no_listener);
				}
			} /* end of if(add_p...... */
			else {
				iucv_sever (int_buf->ippathid, no_listener);
				pr_debug ("iucv:do_int:add_pathid failed"
					  "with rc = %d\n",
					  (int) add_pathid_result);
			}
		} else
			iucv_sever (int_buf->ippathid, no_listener);
		break;

	case 0x02:		/*connection complete */
		if (Q) {
			if (interrupt->ConnectionComplete)
				(interrupt->ConnectionComplete)
				    ((iucv_ConnectionComplete *) int_buf, (P->pgm_data));
			else
				pr_debug ("iucv:do_int:"
					  "ConnectionComplete not called\n");
		}
		
		break;

	case 0x03:		/* connection severed */
		if (Q) {
			if (interrupt->ConnectionSevered)
				(interrupt->ConnectionSevered)
				    ((iucv_ConnectionSevered *) int_buf,
				     (P->pgm_data));

			else
				iucv_sever (int_buf->ippathid, no_listener);
		} else
			iucv_sever (int_buf->ippathid, no_listener);
		break;

	case 0x04:		/* connection quiesced */
		if (Q) {
			if (interrupt->ConnectionQuiesced)
				(interrupt->ConnectionQuiesced)
				    ((iucv_ConnectionQuiesced *) int_buf,
				     (P->pgm_data));
			else
				pr_debug ("iucv:do_int:"
					  "ConnectionQuiesced not called\n");
		}
		break;

	case 0x05:		/* connection resumed */
		if (Q) {
			if (interrupt->ConnectionResumed)
				(interrupt->ConnectionResumed)
				    ((iucv_ConnectionResumed *) int_buf, (P->pgm_data));
			else
				pr_debug ("iucv:do_int:"
					  "ConnectionResumed not called\n");
		}
		break;

	case 0x06:		/* priority message complete */
	case 0x07:		/* nonpriority message complete */
		if (Q) {
			if (interrupt->MessageComplete)
				(interrupt->MessageComplete)
				    ((iucv_MessageComplete *) int_buf, (P->pgm_data));
			else
				pr_debug ("iucv:do_int:"
					  "MessageComplete not called\n");
		}
		break;

	case 0x08:		/* priority message pending  */
	case 0x09:		/* nonpriority message pending  */
		if (Q) {
			if (interrupt->MessagePending)
				(interrupt->MessagePending)
				    ((iucv_MessagePending *) int_buf, (P->pgm_data));
			else
				pr_debug ("iucv:do_int:"
					  "MessagePending not called\n");
		}
		break;
	default:		/* unknown iucv type */
		printk (KERN_WARNING "iucv:do_int: unknown iucv interrupt \n");
		break;
	}			/* end switch */

	pr_debug ("BH:-  Exiting do_int "
		  "pathid %d, type %02X\n", int_buf->ippathid, int_buf->iptype);

	return;
}
/* end of function call */

EXPORT_SYMBOL (iucv_accept);
EXPORT_SYMBOL (iucv_connect);
EXPORT_SYMBOL (iucv_purge);
EXPORT_SYMBOL (iucv_query_maxconn);
EXPORT_SYMBOL (iucv_query_bufsize);
EXPORT_SYMBOL (iucv_quiesce);
EXPORT_SYMBOL (iucv_receive);
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
EXPORT_SYMBOL (iucv_unregister_program);
