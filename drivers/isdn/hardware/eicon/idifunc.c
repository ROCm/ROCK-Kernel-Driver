/* $Id: idifunc.c,v 1.13 2003/08/25 14:49:53 schindler Exp $
 *
 * Driver for Eicon DIVA Server ISDN cards.
 * User Mode IDI Interface 
 *
 * Copyright 2000-2003 by Armin Schindler (mac@melware.de)
 * Copyright 2000-2003 Cytronics & Melware (info@melware.de)
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 */

#include "platform.h"
#include "di_defs.h"
#include "divasync.h"
#include "um_xdi.h"
#include "um_idi.h"

#define DBG_MINIMUM  (DL_LOG + DL_FTL + DL_ERR)
#define DBG_DEFAULT  (DBG_MINIMUM + DL_XLOG + DL_REG)

extern char *DRIVERRELEASE_IDI;

extern void DIVA_DIDD_Read(void *, int);
extern int diva_user_mode_idi_create_adapter(const DESCRIPTOR *, int);
extern void diva_user_mode_idi_remove_adapter(int);

#define MAX_DESCRIPTORS  32

static dword notify_handle;
static DESCRIPTOR DAdapter;
static DESCRIPTOR MAdapter;

static void no_printf(unsigned char *x, ...)
{
	/* dummy debug function */
}

#include "debuglib.c"

/*
 * stop debug
 */
static void stop_dbg(void)
{
	DbgDeregister();
	memset(&MAdapter, 0, sizeof(MAdapter));
	dprintf = no_printf;
}

typedef struct _udiva_card {
	struct _udiva_card *next;
	int Id;
	DESCRIPTOR d;
} udiva_card;

static udiva_card *cards;
static diva_os_spin_lock_t ll_lock;

/*
 * add card to list
 */
static void add_card_to_list(udiva_card * c)
{
	diva_os_spin_lock_magic_t old_irql;

	diva_os_enter_spin_lock(&ll_lock, &old_irql, "add card");
	c->next = cards;
	cards = c;
	diva_os_leave_spin_lock(&ll_lock, &old_irql, "add card");
}

/*
 * find card in list
 */
static udiva_card *find_card_in_list(DESCRIPTOR * d)
{
	udiva_card *card;
	diva_os_spin_lock_magic_t old_irql;

	diva_os_enter_spin_lock(&ll_lock, &old_irql, "find card");
	card = cards;
	while (card) {
		if (card->d.request == d->request) {
			diva_os_leave_spin_lock(&ll_lock, &old_irql,
						"find card");
			return (card);
		}
		card = card->next;
	}
	diva_os_leave_spin_lock(&ll_lock, &old_irql, "find card");
	return ((udiva_card *) NULL);
}

/*
 * remove card from list
 */
static void remove_card_from_list(udiva_card * c)
{
	udiva_card *list = NULL, *last;
	diva_os_spin_lock_magic_t old_irql;

	diva_os_enter_spin_lock(&ll_lock, &old_irql, "remove card");
	list = cards;
	last = list;
	while (list) {
		if (list == c) {
			if (cards == c) {
				cards = c->next;
			} else {
				last->next = c->next;
			}
			break;
		}
		last = list;
		list = list->next;
	}
	diva_os_leave_spin_lock(&ll_lock, &old_irql, "remove card");
}

/*
 * new card
 */
static void um_new_card(DESCRIPTOR * d)
{
	int adapter_nr = 0;
	udiva_card *card = NULL;
	IDI_SYNC_REQ sync_req;

	if (!(card = diva_os_malloc(0, sizeof(udiva_card)))) {
		DBG_ERR(("cannot get buffer for card"));
		return;
	}
	memcpy(&card->d, d, sizeof(DESCRIPTOR));
	sync_req.xdi_logical_adapter_number.Req = 0;
	sync_req.xdi_logical_adapter_number.Rc =
	    IDI_SYNC_REQ_XDI_GET_LOGICAL_ADAPTER_NUMBER;
	card->d.request((ENTITY *) & sync_req);
	adapter_nr =
	    sync_req.xdi_logical_adapter_number.info.logical_adapter_number;
	card->Id = adapter_nr;
	if (!(diva_user_mode_idi_create_adapter(d, adapter_nr))) {
		add_card_to_list(card);
	} else {
		DBG_ERR(("could not create user mode idi card %d",
			 adapter_nr));
	}
}

/*
 * remove card
 */
static void um_remove_card(DESCRIPTOR * d)
{
	udiva_card *card = NULL;

	if (!(card = find_card_in_list(d))) {
		DBG_ERR(("cannot find card to remove"));
		return;
	}
	diva_user_mode_idi_remove_adapter(card->Id);
	remove_card_from_list(card);
	DBG_LOG(("idi proc entry removed for card %d", card->Id));
	diva_os_free(0, card);
}

/*
 * remove all adapter
 */
static void DIVA_EXIT_FUNCTION remove_all_idi_proc(void)
{
	udiva_card *card, *last;
	diva_os_spin_lock_magic_t old_irql;

	diva_os_enter_spin_lock(&ll_lock, &old_irql, "remove all");
	card = cards;
	cards = NULL;
	diva_os_leave_spin_lock(&ll_lock, &old_irql, "remove all");

	while (card) {
		diva_user_mode_idi_remove_adapter(card->Id);
		last = card;
		card = card->next;
		diva_os_free(0, last);
	}
}

/*
 * DIDD notify callback
 */
static void *didd_callback(void *context, DESCRIPTOR * adapter,
			   int removal)
{
	if (adapter->type == IDI_DADAPTER) {
		DBG_ERR(("Notification about IDI_DADAPTER change ! Oops."));
		return (NULL);
	} else if (adapter->type == IDI_DIMAINT) {
		if (removal) {
			stop_dbg();
		} else {
			memcpy(&MAdapter, adapter, sizeof(MAdapter));
			dprintf = (DIVA_DI_PRINTF) MAdapter.request;
			DbgRegister("User IDI", DRIVERRELEASE_IDI, DBG_DEFAULT);
		}
	} else if ((adapter->type > 0) && (adapter->type < 16)) {	/* IDI Adapter */
		if (removal) {
			um_remove_card(adapter);
		} else {
			um_new_card(adapter);
		}
	}
	return (NULL);
}

/*
 * connect DIDD
 */
static int DIVA_INIT_FUNCTION connect_didd(void)
{
	int x = 0;
	int dadapter = 0;
	IDI_SYNC_REQ req;
	DESCRIPTOR DIDD_Table[MAX_DESCRIPTORS];

	DIVA_DIDD_Read(DIDD_Table, sizeof(DIDD_Table));

	for (x = 0; x < MAX_DESCRIPTORS; x++) {
		if (DIDD_Table[x].type == IDI_DADAPTER) {	/* DADAPTER found */
			dadapter = 1;
			memcpy(&DAdapter, &DIDD_Table[x], sizeof(DAdapter));
			req.didd_notify.e.Req = 0;
			req.didd_notify.e.Rc =
			    IDI_SYNC_REQ_DIDD_REGISTER_ADAPTER_NOTIFY;
			req.didd_notify.info.callback = (void *)didd_callback;
			req.didd_notify.info.context = 0;
			DAdapter.request((ENTITY *) & req);
			if (req.didd_notify.e.Rc != 0xff) {
				stop_dbg();
				return (0);
			}
			notify_handle = req.didd_notify.info.handle;
		} else if (DIDD_Table[x].type == IDI_DIMAINT) {	/* MAINT found */
			memcpy(&MAdapter, &DIDD_Table[x], sizeof(DAdapter));
			dprintf = (DIVA_DI_PRINTF) MAdapter.request;
			DbgRegister("User IDI", DRIVERRELEASE_IDI, DBG_DEFAULT);
		} else if ((DIDD_Table[x].type > 0)
			   && (DIDD_Table[x].type < 16)) {	/* IDI Adapter found */
			um_new_card(&DIDD_Table[x]);
		}
	}

	if (!dadapter) {
		stop_dbg();
	}

	return (dadapter);
}

/*
 *  Disconnect from DIDD
 */
static void DIVA_EXIT_FUNCTION disconnect_didd(void)
{
	IDI_SYNC_REQ req;

	stop_dbg();

	req.didd_notify.e.Req = 0;
	req.didd_notify.e.Rc = IDI_SYNC_REQ_DIDD_REMOVE_ADAPTER_NOTIFY;
	req.didd_notify.info.handle = notify_handle;
	DAdapter.request((ENTITY *) & req);
}

/*
 * init
 */
int DIVA_INIT_FUNCTION idifunc_init(void)
{
	diva_os_initialize_spin_lock(&ll_lock, "idifunc");

	if (diva_user_mode_idi_init()) {
		DBG_ERR(("init: init failed."));
		return (0);
	}

	if (!connect_didd()) {
		diva_user_mode_idi_finit();
		DBG_ERR(("init: failed to connect to DIDD."));
		return (0);
	}
	return (1);
}

/*
 * finit
 */
void DIVA_EXIT_FUNCTION idifunc_finit(void)
{
	diva_user_mode_idi_finit();
	disconnect_didd();
	remove_all_idi_proc();
}
