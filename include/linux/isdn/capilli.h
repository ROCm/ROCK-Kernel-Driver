/* $Id: capilli.h,v 1.4.8.1 2001/09/23 22:24:33 kai Exp $
 * 
 * Kernel CAPI 2.0 Driver Interface for Linux
 * 
 * Copyright 1999 by Carsten Paeth <calle@calle.de>
 * 
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 */

#ifndef __CAPILLI_H__
#define __CAPILLI_H__

#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/capi.h>
#include <linux/kernelcapi.h>

typedef struct capiloaddatapart {
	int user;		/* data in userspace ? */
	int len;
	unsigned char *data;
} capiloaddatapart;

typedef struct capiloaddata {
	capiloaddatapart firmware;
	capiloaddatapart configuration;
} capiloaddata;

typedef struct capicardparams {
	unsigned int port;
	unsigned irq;
	int cardtype;
	int cardnr;
	unsigned int membase;
} capicardparams;

struct capi_driver;

struct capi_ctr {
        struct list_head driver_list;		/* contrs by driver */
        struct capi_driver *driver;
	int cnr;				/* controller number */
	char name[32];				/* name of controller */
	volatile unsigned short cardstate;	/* controller state */
	volatile int blocked;			/* output blocked */
	int traceflag;				/* capi trace */

	void *driverdata;			/* driver specific */

	/* filled before calling ready callback */
	__u8 manu[CAPI_MANUFACTURER_LEN];	/* CAPI_GET_MANUFACTURER */
	capi_version version;			/* CAPI_GET_VERSION */
	capi_profile profile;			/* CAPI_GET_PROFILE */
	__u8 serial[CAPI_SERIAL_LEN];		/* CAPI_GET_SERIAL */

	/* functions */
        void (*ready)(struct capi_ctr * card);
        void (*reseted)(struct capi_ctr * card);
        void (*suspend_output)(struct capi_ctr * card);
        void (*resume_output)(struct capi_ctr * card);
        void (*handle_capimsg)(struct capi_ctr * card,
			   	__u16 appl, struct sk_buff *skb);

	/* management information for kcapi */

	unsigned long nrecvctlpkt;
	unsigned long nrecvdatapkt;
	unsigned long nsentctlpkt;
	unsigned long nsentdatapkt;

	struct proc_dir_entry *procent;
        char procfn[128];
};

struct capi_driver {
	struct module *owner;
	char name[32];				/* driver name */
	char revision[32];
	int (*load_firmware)(struct capi_ctr *, capiloaddata *);
	void (*reset_ctr)(struct capi_ctr *);
	void (*remove_ctr)(struct capi_ctr *);
	void (*register_appl)(struct capi_ctr *, __u16 appl,
			      capi_register_params *);
	void (*release_appl)(struct capi_ctr *, __u16 appl);
	u16  (*send_message)(struct capi_ctr *, struct sk_buff *skb);
	
	char *(*procinfo)(struct capi_ctr *);
	int (*ctr_read_proc)(char *page, char **start, off_t off,
			     int count, int *eof, struct capi_ctr *card);
	int (*driver_read_proc)(char *page, char **start, off_t off,
				int count, int *eof, struct capi_driver *driver);
	
	int (*add_card)(struct capi_driver *driver, capicardparams *data);
	
	/* intitialized by kcapi */
	struct list_head contr_head;		/* list of controllers */
	struct list_head driver_list;
	int ncontroller;
	struct proc_dir_entry *procent;
	char procfn[128];
};

void attach_capi_driver(struct capi_driver *driver);
void detach_capi_driver(struct capi_driver *driver);

struct capi_ctr *attach_capi_ctr(struct capi_driver *driver, char *name, void *data);
int detach_capi_ctr(struct capi_ctr *);



// ---------------------------------------------------------------------------
// library functions for use by hardware controller drivers

void capilib_new_ncci(struct list_head *head, u16 applid, u32 ncci, u32 winsize);
void capilib_free_ncci(struct list_head *head, u16 applid, u32 ncci);
void capilib_release_appl(struct list_head *head, u16 applid);
void capilib_release(struct list_head *head);
void capilib_data_b3_conf(struct list_head *head, u16 applid, u32 ncci, u16 msgid);
u16  capilib_data_b3_req(struct list_head *head, u16 applid, u32 ncci, u16 msgid);

#endif				/* __CAPILLI_H__ */
