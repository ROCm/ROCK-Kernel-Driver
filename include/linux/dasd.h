
#ifndef DASD_H
#define DASD_H

/* First of all the external stuff */
#include <linux/ioctl.h>
#include <linux/major.h>
#include <linux/wait.h>

#define IOCTL_LETTER 'D'
#define BIODASDFORMAT  _IO(IOCTL_LETTER,0) /* Format the volume or an extent */
#define BIODASDDISABLE _IO(IOCTL_LETTER,1) /* Disable the volume (for Linux) */
#define BIODASDENABLE  _IO(IOCTL_LETTER,2) /* Enable the volume (for Linux) */
/* Stuff for reading and writing the Label-Area to/from user space */
#define BIODASDGTVLBL  _IOR(IOCTL_LETTER,3,dasd_volume_label_t)
#define BIODASDSTVLBL  _IOW(IOCTL_LETTER,4,dasd_volume_label_t)
#define BIODASDRWTB    _IOWR(IOCTL_LETTER,5,int)
#define BIODASDRSID    _IOR(IOCTL_LETTER,6,senseid_t)

typedef
union {
	char bytes[512];
	struct {
		/* 80 Bytes of Label data */
		char identifier[4];	/* e.g. "LNX1", "VOL1" or "CMS1" */
		char label[6];	/* Given by user */
		char security;
		char vtoc[5];	/* Null in "LNX1"-labelled partitions */
		char reserved0[5];
		long ci_size;
		long blk_per_ci;
		long lab_per_ci;
		char reserved1[4];
		char owner[0xe];
		char no_part;
		char reserved2[0x1c];
		/* 16 Byte of some information on the dasd */
		short blocksize;
		char nopart;
		char unused;
		long unused2[3];
		/* 7*10 = 70 Bytes of partition data */
		struct {
			char type;
			long start;
			long size;
			char unused;
		} part[7];
	} __attribute__ ((packed)) label;
} dasd_volume_label_t;

typedef union {
	struct {
		unsigned long no;
		unsigned int ct;
	} __attribute__ ((packed)) input;
	struct {
		unsigned long noct;
	} __attribute__ ((packed)) output;
} __attribute__ ((packed)) dasd_xlate_t;

int dasd_init (void);
#ifdef MODULE
int init_module (void);
void cleanup_module (void);
#endif				/* MODULE */

/* Definitions for blk.h */
/* #define DASD_MAGIC 0x44415344  is ascii-"DASD" */
/* #define dasd_MAGIC 0x64617364;  is ascii-"dasd" */
#define DASD_MAGIC 0xC4C1E2C4 /* is ebcdic-"DASD" */
#define dasd_MAGIC 0x8481A284 /* is ebcdic-"dasd" */
#define DASD_NAME "dasd"
#define DASD_PARTN_BITS 2
#define DASD_MAX_DEVICES (256>>DASD_PARTN_BITS)

#define MAJOR_NR DASD_MAJOR
#define PARTN_BITS DASD_PARTN_BITS

#ifdef __KERNEL__
/* Now lets turn to the internal sbtuff */
/*
   define the debug levels:
   - 0 No debugging output to console or syslog
   - 1 Log internal errors to syslog, ignore check conditions 
   - 2 Log internal errors and check conditions to syslog
   - 3 Log internal errors to console, log check conditions to syslog
   - 4 Log internal errors and check conditions to console
   - 5 panic on internal errors, log check conditions to console
   - 6 panic on both, internal errors and check conditions
 */
#define DASD_DEBUG 4

#define DASD_PROFILE
/*
   define the level of paranoia
   - 0 quite sure, that things are going right
   - 1 sanity checking, only to avoid panics
   - 2 normal sanity checking
   - 3 extensive sanity checks
   - 4 exhaustive debug messages
 */
#define DASD_PARANOIA 2

/*
   define the depth of flow control, which is logged as a check condition
   - 0 No flow control messages
   - 1 Entry of functions logged like check condition
   - 2 Entry and exit of functions logged like check conditions
   - 3 Internal structure broken down
   - 4 unrolling of loops,...
 */
#define DASD_FLOW_CONTROL 0

#if DASD_DEBUG > 0
#define PRINT_DEBUG(x...) printk ( KERN_DEBUG PRINTK_HEADER x )
#define PRINT_INFO(x...) printk ( KERN_INFO PRINTK_HEADER x )
#define PRINT_WARN(x...) printk ( KERN_WARNING PRINTK_HEADER x )
#define PRINT_ERR(x...) printk ( KERN_ERR PRINTK_HEADER x )
#define PRINT_FATAL(x...) panic ( PRINTK_HEADER x )
#else
#define PRINT_DEBUG(x...) printk ( KERN_DEBUG PRINTK_HEADER x )
#define PRINT_INFO(x...) printk ( KERN_DEBUG PRINTK_HEADER x )
#define PRINT_WARN(x...) printk ( KERN_DEBUG PRINTK_HEADER x )
#define PRINT_ERR(x...) printk ( KERN_DEBUG PRINTK_HEADER x )
#define PRINT_FATAL(x...) printk ( KERN_DEBUG PRINTK_HEADER x )
#endif				/* DASD_DEBUG */

#define INTERNAL_ERRMSG(x,y...) \
"Internal error: in file " __FILE__ " line: %d: " x, __LINE__, y
#define INTERNAL_CHKMSG(x,y...) \
"Inconsistency: in file " __FILE__ " line: %d: " x, __LINE__, y
#define INTERNAL_FLWMSG(x,y...) \
"Flow control: file " __FILE__ " line: %d: " x, __LINE__, y

#if DASD_DEBUG > 4
#define INTERNAL_ERROR(x...) PRINT_FATAL ( INTERNAL_ERRMSG ( x ) )
#elif DASD_DEBUG > 2
#define INTERNAL_ERROR(x...) PRINT_ERR ( INTERNAL_ERRMSG ( x ) )
#elif DASD_DEBUG > 0
#define INTERNAL_ERROR(x...) PRINT_WARN ( INTERNAL_ERRMSG ( x ) )
#else
#define INTERNAL_ERROR(x...)
#endif				/* DASD_DEBUG */

#if DASD_DEBUG > 5
#define INTERNAL_CHECK(x...) PRINT_FATAL ( INTERNAL_CHKMSG ( x ) )
#elif DASD_DEBUG > 3
#define INTERNAL_CHECK(x...) PRINT_ERR ( INTERNAL_CHKMSG ( x ) )
#elif DASD_DEBUG > 1
#define INTERNAL_CHECK(x...) PRINT_WARN ( INTERNAL_CHKMSG ( x ) )
#else
#define INTERNAL_CHECK(x...)
#endif				/* DASD_DEBUG */

#if DASD_DEBUG > 3
#define INTERNAL_FLOW(x...) PRINT_ERR ( INTERNAL_FLWMSG ( x ) )
#elif DASD_DEBUG > 2
#define INTERNAL_FLOW(x...) PRINT_WARN ( INTERNAL_FLWMSG ( x ) )
#else
#define INTERNAL_FLOW(x...)
#endif				/* DASD_DEBUG */

#if DASD_FLOW_CONTROL > 0
#define FUNCTION_ENTRY(x) INTERNAL_FLOW( x "entered %s\n","" );
#else
#define FUNCTION_ENTRY(x)
#endif				/* DASD_FLOW_CONTROL */

#if DASD_FLOW_CONTROL > 1
#define FUNCTION_EXIT(x) INTERNAL_FLOW( x "exited %s\n","" );
#else
#define FUNCTION_EXIT(x)
#endif				/* DASD_FLOW_CONTROL */

#if DASD_FLOW_CONTROL > 2
#define FUNCTION_CONTROL(x...) INTERNAL_FLOW( x );
#else
#define FUNCTION_CONTROL(x...)
#endif				/* DASD_FLOW_CONTROL */

#if DASD_FLOW_CONTROL > 3
#define LOOP_CONTROL(x...) INTERNAL_FLOW( x );
#else
#define LOOP_CONTROL(x...)
#endif				/* DASD_FLOW_CONTROL */

#define DASD_DO_IO_SLEEP 0x01
#define DASD_DO_IO_NOLOCK 0x02
#define DASD_DO_IO_NODEC 0x04

#define DASD_NOT_FORMATTED 0x01

extern wait_queue_head_t dasd_waitq;

#undef DEBUG_DASD_MALLOC
#ifdef DEBUG_DASD_MALLOC
void *b;
#define kmalloc(x...) (PRINT_INFO(" kmalloc %p\n",b=kmalloc(x)),b)
#define kfree(x) PRINT_INFO(" kfree %p\n",x);kfree(x)
#define get_free_page(x...) (PRINT_INFO(" gfp %p\n",b=get_free_page(x)),b)
#define __get_free_pages(x...) (PRINT_INFO(" gfps %p\n",b=__get_free_pages(x)),b)
#endif				/* DEBUG_DASD_MALLOC */

#endif				/* __KERNEL__ */
#endif				/* DASD_H */

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-indent-level: 4 
 * c-brace-imaginary-offset: 0
 * c-brace-offset: -4
 * c-argdecl-indent: 4
 * c-label-offset: -4
 * c-continued-statement-offset: 4
 * c-continued-brace-offset: 0
 * indent-tabs-mode: nil
 * tab-width: 8
 * End:
 */
