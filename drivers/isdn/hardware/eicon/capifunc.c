/* $Id: capifunc.c,v 1.47 2003/09/09 06:52:29 schindler Exp $
 *
 * ISDN interface module for Eicon active cards DIVA.
 * CAPI Interface common functions
 * 
 * Copyright 2000-2003 by Armin Schindler (mac@melware.de) 
 * Copyright 2000-2003 Cytronics & Melware (info@melware.de)
 * 
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 */

#include "platform.h"
#include "os_capi.h"
#include "di_defs.h"
#include "capi20.h"
#include "divacapi.h"
#include "divasync.h"
#include "capifunc.h"

#define DBG_MINIMUM  (DL_LOG + DL_FTL + DL_ERR)
#define DBG_DEFAULT  (DBG_MINIMUM + DL_XLOG + DL_REG)

DIVA_CAPI_ADAPTER *adapter = (DIVA_CAPI_ADAPTER *) NULL;
APPL *application = (APPL *) NULL;
byte max_appl = MAX_APPL;
byte max_adapter = 0;
static CAPI_MSG *mapped_msg = (CAPI_MSG *) NULL;

byte UnMapController(byte);
char DRIVERRELEASE_CAPI[32];

extern void AutomaticLaw(DIVA_CAPI_ADAPTER *);
extern void callback(ENTITY *);
extern word api_remove_start(void);
extern word CapiRelease(word);
extern word CapiRegister(word);
extern word api_put(APPL *, CAPI_MSG *);

static diva_os_spin_lock_t api_lock;
static diva_os_spin_lock_t ll_lock;

static diva_card *cards;
static dword notify_handle;
static void DIRequest(ENTITY * e);
static DESCRIPTOR MAdapter;
static DESCRIPTOR DAdapter;
static byte ControllerMap[MAX_DESCRIPTORS + 1];


static void diva_register_appl(struct capi_ctr *, __u16,
			       capi_register_params *);
static void diva_release_appl(struct capi_ctr *, __u16);
static char *diva_procinfo(struct capi_ctr *);
static u16 diva_send_message(struct capi_ctr *,
			     diva_os_message_buffer_s *);
extern void diva_os_set_controller_struct(struct capi_ctr *);

extern void DIVA_DIDD_Read(DESCRIPTOR *, int);

/*
 * debug
 */
static void no_printf(unsigned char *, ...);
#include "debuglib.c"
void xlog(char *x, ...)
{
#ifndef DIVA_NO_DEBUGLIB
	va_list ap;
	if (myDriverDebugHandle.dbgMask & DL_XLOG) {
		va_start(ap, x);
		if (myDriverDebugHandle.dbg_irq) {
			myDriverDebugHandle.dbg_irq(myDriverDebugHandle.id,
						    DLI_XLOG, x, ap);
		} else if (myDriverDebugHandle.dbg_old) {
			myDriverDebugHandle.dbg_old(myDriverDebugHandle.id,
						    x, ap);
		}
		va_end(ap);
	}
#endif
}

/*
 * info for proc
 */
static char *diva_procinfo(struct capi_ctr *ctrl)
{
	return (ctrl->serial);
}

/*
 * stop debugging
 */
static void stop_dbg(void)
{
	DbgDeregister();
	memset(&MAdapter, 0, sizeof(MAdapter));
	dprintf = no_printf;
}

/*
 * dummy debug function
 */
static void no_printf(unsigned char *x, ...)
{
}

/*
 * Controller mapping
 */
byte MapController(byte Controller)
{
	byte i;
	byte MappedController = 0;
	byte ctrl = Controller & 0x7f;	/* mask external controller bit off */

	for (i = 1; i < max_adapter + 1; i++) {
		if (ctrl == ControllerMap[i]) {
			MappedController = (byte) i;
			break;
		}
	}
	if (i > max_adapter) {
		ControllerMap[0] = ctrl;
		MappedController = 0;
	}
	return (MappedController | (Controller & 0x80));	/* put back external controller bit */
}

/*
 * Controller unmapping
 */
byte UnMapController(byte MappedController)
{
	byte Controller;
	byte ctrl = MappedController & 0x7f;	/* mask external controller bit off */

	if (ctrl <= max_adapter) {
		Controller = ControllerMap[ctrl];
	} else {
		Controller = 0;
	}

	return (Controller | (MappedController & 0x80));	/* put back external controller bit */
}

/*
 * find a new free id
 */
static int find_free_id(void)
{
	int num = 0;
	diva_card *p;
	diva_os_spin_lock_magic_t old_irql;

	diva_os_enter_spin_lock(&ll_lock, &old_irql, "find free id");
	while (num < 100) {
		num++;
		p = cards;
		while (p) {
			if (p->Id == num)
				break;
			p = p->next;
		}
		if(!p) {
		diva_os_leave_spin_lock(&ll_lock, &old_irql,
					"find free id");
		return (num);
		}
	}
	diva_os_leave_spin_lock(&ll_lock, &old_irql, "find free id");
	return (999);
}

/*
 * find a card structure by controller number
 */
static diva_card *find_card_by_ctrl(word controller)
{
	diva_card *p;
	diva_os_spin_lock_magic_t old_irql;

	diva_os_enter_spin_lock(&ll_lock, &old_irql, "find card ctrl");
	p = cards;

	while (p) {
		if (ControllerMap[p->Id] == controller) {
			diva_os_leave_spin_lock(&ll_lock, &old_irql,
						"find card ctrl");
			return p;
		}
		p = p->next;
	}
	diva_os_leave_spin_lock(&ll_lock, &old_irql, "find card ctrl");
	return (diva_card *) 0;
}

/*
 * Buffer RX/TX 
 */
void *TransmitBufferSet(APPL * appl, dword ref)
{
	appl->xbuffer_used[ref] = TRUE;
	DBG_PRV1(("%d:xbuf_used(%d)", appl->Id, ref + 1))
	    return (void *) ref;
}

void *TransmitBufferGet(APPL * appl, void *p)
{
	if (appl->xbuffer_internal[(dword) p])
		return appl->xbuffer_internal[(dword) p];

	return appl->xbuffer_ptr[(dword) p];
}

void TransmitBufferFree(APPL * appl, void *p)
{
	appl->xbuffer_used[(dword) p] = FALSE;
	DBG_PRV1(("%d:xbuf_free(%d)", appl->Id, ((dword) p) + 1))
}

void *ReceiveBufferGet(APPL * appl, int Num)
{
	return &appl->ReceiveBuffer[Num * appl->MaxDataLength];
}

/*
 * api_remove_start/complete for cleanup
 */
void api_remove_complete(void)
{
	DBG_PRV1(("api_remove_complete"))
}

/*
 * main function called by message.c
 */
void sendf(APPL * appl, word command, dword Id, word Number, byte * format, ...)
{
	word i, j;
	word length = 12, dlength = 0;
	byte *write;
	CAPI_MSG msg;
	byte *string = 0;
	va_list ap;
	diva_os_message_buffer_s *dmb;
	diva_card *card = NULL;
	dword tmp;

	if (!appl)
		return;

	DBG_PRV1(("sendf(a=%d,cmd=%x,format=%s)",
		  appl->Id, command, (byte *) format))

	    WRITE_WORD(&msg.header.appl_id, appl->Id);
	WRITE_WORD(&msg.header.command, command);
	if ((byte) (command >> 8) == 0x82)
		Number = appl->Number++;
	WRITE_WORD(&msg.header.number, Number);

	WRITE_DWORD(((byte *) & msg.header.controller), Id);
	write = (byte *) & msg;
	write += 12;

	va_start(ap, format);
	for (i = 0; format[i]; i++) {
		switch (format[i]) {
		case 'b':
			tmp = va_arg(ap, dword);
			*(byte *) write = (byte) (tmp & 0xff);
			write += 1;
			length += 1;
			break;
		case 'w':
			tmp = va_arg(ap, dword);
			WRITE_WORD(write, (tmp & 0xffff));
			write += 2;
			length += 2;
			break;
		case 'd':
			tmp = va_arg(ap, dword);
			WRITE_DWORD(write, tmp);
			write += 4;
			length += 4;
			break;
		case 's':
		case 'S':
			string = va_arg(ap, byte *);
			length += string[0] + 1;
			for (j = 0; j <= string[0]; j++)
				*write++ = string[j];
			break;
		}
	}
	va_end(ap);

	WRITE_WORD(&msg.header.length, length);
	msg.header.controller = UnMapController(msg.header.controller);

	if (command == _DATA_B3_I)
		dlength = READ_WORD(
			      ((byte *) & msg.info.data_b3_ind.Data_Length));

	if (!(dmb = diva_os_alloc_message_buffer(length + dlength,
					  (void **) &write))) {
		DBG_ERR(("sendf: alloc_message_buffer failed, incoming msg dropped."))
		return;
	}

	/* copy msg header to sk_buff */
	memcpy(write, (byte *) & msg, length);

	/* if DATA_B3_IND, copy data too */
	if (command == _DATA_B3_I) {
		dword data = READ_DWORD(&msg.info.data_b3_ind.Data);
		memcpy(write + length, (void *) data, dlength);
	}

#ifndef DIVA_NO_DEBUGLIB
	if (myDriverDebugHandle.dbgMask & DL_XLOG) {
		switch (command) {
		default:
			xlog("\x00\x02", &msg, 0x81, length);
			break;
		case _DATA_B3_R | CONFIRM:
			if (myDriverDebugHandle.dbgMask & DL_BLK)
				xlog("\x00\x02", &msg, 0x81, length);
			break;
		case _DATA_B3_I:
			if (myDriverDebugHandle.dbgMask & DL_BLK) {
				xlog("\x00\x02", &msg, 0x81, length);
				for (i = 0; i < dlength; i += 256) {
				  DBG_BLK((((char *) READ_DWORD(&msg.info.data_b3_ind.Data)) + i,
				  	((dlength - i) < 256) ? (dlength - i) : 256))
				  if (!(myDriverDebugHandle.dbgMask & DL_PRV0))
					  break; /* not more if not explicitely requested */
				}
			}
			break;
		}
	}
#endif

	/* find the card structure for this controller */
	if (!(card = find_card_by_ctrl(write[8] & 0x7f))) {
		DBG_ERR(("sendf - controller %d not found, incoming msg dropped",
			 write[8] & 0x7f))
		diva_os_free_message_buffer(dmb);
		return;
	}
	/* send capi msg to capi layer */
	capi_ctr_handle_message(&card->capi_ctrl, appl->Id, dmb);
}

/*
 * cleanup adapter
 */
static void clean_adapter(int id)
{
	DIVA_CAPI_ADAPTER *a;
#if IMPLEMENT_LINE_INTERCONNECT2
	int i, k;
#endif				/* IMPLEMENT_LINE_INTERCONNECT2 */

	a = &adapter[id];
#if IMPLEMENT_LINE_INTERCONNECT
	if (a->li_pri) {
		if (a->li_config.pri)
			diva_os_free(0, a->li_config.pri);
	} else {
		if (a->li_config.bri)
			diva_os_free(0, a->li_config.bri);
	}
#endif				/* IMPLEMENT_LINE_INTERCONNECT */
#if IMPLEMENT_LINE_INTERCONNECT2
	k = li_total_channels - a->li_channels;
	if (k == 0) {
		diva_os_free(0, li_config_table);
		li_config_table = NULL;
	} else {
		if (a->li_base < k) {
			memmove(&li_config_table[a->li_base],
				&li_config_table[a->li_base + a->li_channels],
				(k - a->li_base) * sizeof(LI_CONFIG));
			for (i = 0; i < k; i++) {
				memmove(&li_config_table[i].flag_table[a->li_base],
					&li_config_table[i].flag_table[a->li_base + a->li_channels],
					k - a->li_base);
				memmove(&li_config_table[i].
					coef_table[a->li_base],
					&li_config_table[i].coef_table[a->li_base + a->li_channels],
					k - a->li_base);
			}
		}
	}
	li_total_channels = k;
	for (i = id; i < max_adapter; i++) {
		if (adapter[i].request)
			adapter[i].li_base -= a->li_channels;
	}
#endif				/* IMPLEMENT_LINE_INTERCONNECT2 */
	if (a->plci)
		diva_os_free(0, a->plci);

	memset(a, 0x00, sizeof(DIVA_CAPI_ADAPTER));
	while ((max_adapter != 0) && !adapter[max_adapter - 1].request)
		max_adapter--;
}

/*
 * remove cards
 */
static void DIVA_EXIT_FUNCTION divacapi_remove_cards(void)
{
	diva_card *last;
	diva_card *card;
	diva_os_spin_lock_magic_t old_irql;

	diva_os_enter_spin_lock(&ll_lock, &old_irql, "remove cards");
	card = cards;

	while (card) {
		detach_capi_ctr(&card->capi_ctrl);
		clean_adapter(card->Id - 1);
		DBG_TRC(("adapter remove, max_adapter=%d", max_adapter));
		card = card->next;
	}

	card = cards;
	while (card) {
		last = card;
		card = card->next;
		diva_os_free(0, last);
	}
	diva_os_leave_spin_lock(&ll_lock, &old_irql, "remove cards");
}

/*
 * remove a card
 */
static void divacapi_remove_card(DESCRIPTOR * d)
{
	diva_card *last;
	diva_card *card;
	diva_os_spin_lock_magic_t old_irql;

	diva_os_enter_spin_lock(&ll_lock, &old_irql, "remove card");
	last = card = cards;
	while (card) {
		if (card->d.request == d->request) {
			detach_capi_ctr(&card->capi_ctrl);
			clean_adapter(card->Id - 1);
			DBG_TRC(
				("DelAdapterMap (%d) -> (%d)",
				 ControllerMap[card->Id], card->Id))
			    ControllerMap[card->Id] = 0;
			DBG_TRC(
				("adapter remove, max_adapter=%d",
				 max_adapter));
			if (card == last)
				cards = card->next;
			else
				last->next = card->next;

			diva_os_free(0, card);
			break;
		}
		last = card;
		card = card->next;
	}
	diva_os_leave_spin_lock(&ll_lock, &old_irql, "remove card");
}

/*
 * sync_callback
 */
static void sync_callback(ENTITY * e)
{
	diva_os_spin_lock_magic_t old_irql;

	DBG_TRC(("cb:Id=%x,Rc=%x,Ind=%x", e->Id, e->Rc, e->Ind))

	diva_os_enter_spin_lock(&api_lock, &old_irql, "sync_callback");
	callback(e);
	diva_os_leave_spin_lock(&api_lock, &old_irql, "sync_callback");
}

/*
 * add a new card
 */
static int diva_add_card(DESCRIPTOR * d)
{
	int k = 0, i = 0;
	diva_os_spin_lock_magic_t old_irql;
	diva_card *card = NULL;
	struct capi_ctr *ctrl = NULL;
	DIVA_CAPI_ADAPTER *a = NULL;
	IDI_SYNC_REQ sync_req;
	char serial[16];
#if IMPLEMENT_LINE_INTERCONNECT2
	LI_CONFIG *new_li_config_table;
	int j;
#endif				/* IMPLEMENT_LINE_INTERCONNECT2 */

	if (!(card = (diva_card *) diva_os_malloc(0, sizeof(diva_card)))) {
		DBG_ERR(("diva_add_card: failed to allocate card struct."))
		    return (0);
	}
	memset((char *) card, 0x00, sizeof(diva_card));
	memcpy(&card->d, d, sizeof(DESCRIPTOR));
	sync_req.GetName.Req = 0;
	sync_req.GetName.Rc = IDI_SYNC_REQ_GET_NAME;
	card->d.request((ENTITY *) & sync_req);
	strlcpy(card->name, sync_req.GetName.name, sizeof(card->name));
	ctrl = &card->capi_ctrl;
	strcpy(ctrl->name, card->name);
	ctrl->register_appl = diva_register_appl;
	ctrl->release_appl = diva_release_appl;
	ctrl->send_message = diva_send_message;
	ctrl->procinfo = diva_procinfo;
	ctrl->driverdata = card;
	diva_os_set_controller_struct(ctrl);

	if (attach_capi_ctr(ctrl)) {
		DBG_ERR(("diva_add_card: failed to attach controller."))
		    diva_os_free(0, card);
		return (0);
	}
	card->Id = find_free_id();
	strlcpy(ctrl->manu, M_COMPANY, sizeof(ctrl->manu));
	ctrl->version.majorversion = 2;
	ctrl->version.minorversion = 0;
	ctrl->version.majormanuversion = DRRELMAJOR;
	ctrl->version.minormanuversion = DRRELMINOR;
	sync_req.GetSerial.Req = 0;
	sync_req.GetSerial.Rc = IDI_SYNC_REQ_GET_SERIAL;
	sync_req.GetSerial.serial = 0;
	card->d.request((ENTITY *) & sync_req);
	if ((i = ((sync_req.GetSerial.serial & 0xff000000) >> 24))) {
		sprintf(serial, "%ld-%d",
			sync_req.GetSerial.serial & 0x00ffffff, i + 1);
	} else {
		sprintf(serial, "%ld", sync_req.GetSerial.serial);
	}
	serial[CAPI_SERIAL_LEN - 1] = 0;
	strlcpy(ctrl->serial, serial, sizeof(ctrl->serial));

	a = &adapter[card->Id - 1];
	card->adapter = a;
	a->os_card = card;
	ControllerMap[card->Id] = (byte) (ctrl->cnr);

	DBG_TRC(("AddAdapterMap (%d) -> (%d)", ctrl->cnr, card->Id))

	    sync_req.xdi_capi_prms.Req = 0;
	sync_req.xdi_capi_prms.Rc = IDI_SYNC_REQ_XDI_GET_CAPI_PARAMS;
	sync_req.xdi_capi_prms.info.structure_length =
	    sizeof(diva_xdi_get_capi_parameters_t);
	card->d.request((ENTITY *) & sync_req);
	a->flag_dynamic_l1_down =
	    sync_req.xdi_capi_prms.info.flag_dynamic_l1_down;
	a->group_optimization_enabled =
	    sync_req.xdi_capi_prms.info.group_optimization_enabled;
	a->request = DIRequest;	/* card->d.request; */
	a->max_plci = card->d.channels + 30;
	a->max_listen = (card->d.channels > 2) ? 8 : 2;
	if (!
	    (a->plci =
	     (PLCI *) diva_os_malloc(0, sizeof(PLCI) * a->max_plci))) {
		DBG_ERR(("diva_add_card: failed alloc plci struct."))
		    memset(a, 0, sizeof(DIVA_CAPI_ADAPTER));
		return (0);
	}
	memset(a->plci, 0, sizeof(PLCI) * a->max_plci);

	for (k = 0; k < a->max_plci; k++) {
		a->Id = (byte) card->Id;
		a->plci[k].Sig.callback = sync_callback;
		a->plci[k].Sig.XNum = 1;
		a->plci[k].Sig.X = a->plci[k].XData;
		a->plci[k].Sig.user[0] = (word) (card->Id - 1);
		a->plci[k].Sig.user[1] = (word) k;
		a->plci[k].NL.callback = sync_callback;
		a->plci[k].NL.XNum = 1;
		a->plci[k].NL.X = a->plci[k].XData;
		a->plci[k].NL.user[0] = (word) ((card->Id - 1) | 0x8000);
		a->plci[k].NL.user[1] = (word) k;
		a->plci[k].adapter = a;
	}

	a->profile.Number = card->Id;
	a->profile.Channels = card->d.channels;
	if (card->d.features & DI_FAX3) {
		a->profile.Global_Options = 0x71;
		if (card->d.features & DI_CODEC)
			a->profile.Global_Options |= 0x6;
#if IMPLEMENT_DTMF
		a->profile.Global_Options |= 0x8;
#endif				/* IMPLEMENT_DTMF */
#if (IMPLEMENT_LINE_INTERCONNECT || IMPLEMENT_LINE_INTERCONNECT2)
		a->profile.Global_Options |= 0x80;
#endif				/* (IMPLEMENT_LINE_INTERCONNECT || IMPLEMENT_LINE_INTERCONNECT2) */
#if IMPLEMENT_ECHO_CANCELLER
		a->profile.Global_Options |= 0x100;
#endif				/* IMPLEMENT_ECHO_CANCELLER */
		a->profile.B1_Protocols = 0xdf;
		a->profile.B2_Protocols = 0x1fdb;
		a->profile.B3_Protocols = 0xb7;
		a->manufacturer_features = MANUFACTURER_FEATURE_HARDDTMF;
	} else {
		a->profile.Global_Options = 0x71;
		if (card->d.features & DI_CODEC)
			a->profile.Global_Options |= 0x2;
		a->profile.B1_Protocols = 0x43;
		a->profile.B2_Protocols = 0x1f0f;
		a->profile.B3_Protocols = 0x07;
		a->manufacturer_features = 0;
	}

#if IMPLEMENT_LINE_INTERCONNECT
	a->li_pri = (a->profile.Channels > 2);
	if (a->li_pri) {
		if (!(a->li_config.pri = (LI_CONFIG_PRI *) diva_os_malloc(0, sizeof(LI_CONFIG_PRI)))) {
			DBG_ERR(("diva_add_card: failed alloc li_config.pri struct."))
			memset(a, 0, sizeof(DIVA_CAPI_ADAPTER));
			return (0);
		}
		memset(a->li_config.pri, 0, sizeof(LI_CONFIG_PRI));
	} else 
		if (!(a->li_config.bri = (LI_CONFIG_BRI *) diva_os_malloc(0, sizeof(LI_CONFIG_BRI)))) {
			DBG_ERR(("diva_add_card: failed alloc li_config.bri struct."))
			memset(a, 0, sizeof(DIVA_CAPI_ADAPTER));
			return (0);
		}
		memset(a->li_config.bri, 0, sizeof(LI_CONFIG_BRI));
	}
#endif				/* IMPLEMENT_LINE_INTERCONNECT */
#if IMPLEMENT_LINE_INTERCONNECT2
	a->li_pri = (a->profile.Channels > 2);
	a->li_channels = a->li_pri ? MIXER_CHANNELS_PRI : MIXER_CHANNELS_BRI;
	a->li_base = 0;
	for (i = 0; &adapter[i] != a; i++) {
		if (adapter[i].request)
			a->li_base = adapter[i].li_base + adapter[i].li_channels;
	}
	k = li_total_channels + a->li_channels;
	new_li_config_table =
		(LI_CONFIG *) diva_os_malloc(0, ((k * sizeof(LI_CONFIG) + 3) & ~3) + (2 * k) * ((k + 3) & ~3));
	if (new_li_config_table == NULL) {
		DBG_ERR(("diva_add_card: failed alloc li_config table."))
		memset(a, 0, sizeof(DIVA_CAPI_ADAPTER));
		return (0);
	}
	j = 0;
	for (i = 0; i < k; i++) {
		if ((i >= a->li_base) && (i < a->li_base + a->li_channels))
			memset(&new_li_config_table[i], 0, sizeof(LI_CONFIG));
		else
			memcpy(&new_li_config_table[i], &li_config_table[j], sizeof(LI_CONFIG));
		new_li_config_table[i].flag_table =
			((byte *) new_li_config_table) + (((k * sizeof(LI_CONFIG) + 3) & ~3) + (2 * i) * ((k + 3) & ~3));
		new_li_config_table[i].coef_table =
			((byte *) new_li_config_table) + (((k * sizeof(LI_CONFIG) + 3) & ~3) + (2 * i + 1) * ((k + 3) & ~3));
		if ((i >= a->li_base) && (i < a->li_base + a->li_channels)) {
			new_li_config_table[i].adapter = a;
			memset(&new_li_config_table[i].flag_table[0], 0, k);
			memset(&new_li_config_table[i].coef_table[0], 0, k);
		} else {
			if (a->li_base != 0) {
				memcpy(&new_li_config_table[i].flag_table[0],
				       &li_config_table[j].flag_table[0],
				       a->li_base);
				memcpy(&new_li_config_table[i].coef_table[0],
				       &li_config_table[j].coef_table[0],
				       a->li_base);
			}
			memset(&new_li_config_table[i].flag_table[a->li_base], 0, a->li_channels);
			memset(&new_li_config_table[i].coef_table[a->li_base], 0, a->li_channels);
			if (a->li_base + a->li_channels < k) {
				memcpy(&new_li_config_table[i].flag_table[a->li_base +
				       a->li_channels],
				       &li_config_table[j].flag_table[a->li_base],
				       k - (a->li_base + a->li_channels));
				memcpy(&new_li_config_table[i].coef_table[a->li_base +
				       a->li_channels],
				       &li_config_table[j].coef_table[a->li_base],
				       k - (a->li_base + a->li_channels));
			}
			j++;
		}
	}
	li_total_channels = k;

	if (li_config_table != NULL)
		diva_os_free(0, li_config_table);

	li_config_table = new_li_config_table;
	for (i = card->Id; i < max_adapter; i++) {
		if (adapter[i].request)
			adapter[i].li_base += a->li_channels;
	}
#endif				/* IMPLEMENT_LINE_INTERCONNECT2 */

	if (a == &adapter[max_adapter])
		max_adapter++;

	diva_os_enter_spin_lock(&ll_lock, &old_irql, "add card");
	card->next = cards;
	cards = card;
	diva_os_leave_spin_lock(&ll_lock, &old_irql, "add card");

	AutomaticLaw(a);
	i = 0;
	while (i++ < 30) {
		if (a->automatic_law > 3)
			break;
		diva_os_sleep(10);
	}

	/* profile information */
	WRITE_WORD(&ctrl->profile.nbchannel, card->d.channels);
	ctrl->profile.goptions = a->profile.Global_Options;
	ctrl->profile.support1 = a->profile.B1_Protocols;
	ctrl->profile.support2 = a->profile.B2_Protocols;
	ctrl->profile.support3 = a->profile.B3_Protocols;
	/* manufacturer profile information */
	ctrl->profile.manu[0] = a->man_profile.private_options;
	ctrl->profile.manu[1] = a->man_profile.rtp_primary_payloads;
	ctrl->profile.manu[2] = a->man_profile.rtp_additional_payloads;
	ctrl->profile.manu[3] = 0;
	ctrl->profile.manu[4] = 0;

	capi_ctr_ready(ctrl);

	DBG_TRC(("adapter added, max_adapter=%d", max_adapter));
	return (1);
}

/*
 *  register appl
 */
static void diva_register_appl(struct capi_ctr *ctrl, __u16 appl,
			       capi_register_params * rp)
{
	APPL *this;
	word bnum, xnum;
	int i = 0;
	unsigned char *p;
	void *DataNCCI, *DataFlags, *ReceiveBuffer, *xbuffer_used;
	void **xbuffer_ptr, **xbuffer_internal;
	diva_os_spin_lock_magic_t old_irql;
	unsigned int mem_len;


	if (diva_os_in_irq()) {
		DBG_ERR(("CAPI_REGISTER - in irq context !"))
		return;
	}

	DBG_TRC(("application register Id=%d", appl))

	if (appl > MAX_APPL) {
		DBG_ERR(("CAPI_REGISTER - appl.Id exceeds MAX_APPL"))
		return;
	}

	if (rp->level3cnt < 1 ||
	    rp->level3cnt > 255 ||
	    rp->datablklen < 80 ||
	    rp->datablklen > 2150 || rp->datablkcnt > 255) {
		DBG_ERR(("CAPI_REGISTER - invalid parameters"))
		return;
	}

	if (application[appl - 1].Id == appl) {
		DBG_ERR(("CAPI_REGISTER - appl already registered"))
		return;	/* appl already registered */
	}

	/* alloc memory */

	bnum = rp->level3cnt * rp->datablkcnt;
	xnum = rp->level3cnt * MAX_DATA_B3;

	mem_len  = bnum * sizeof(word);		/* DataNCCI */
	mem_len += bnum * sizeof(word);		/* DataFlags */
	mem_len += bnum * rp->datablklen;	/* ReceiveBuffer */
	mem_len += xnum;			/* xbuffer_used */
	mem_len += xnum * sizeof(void *);	/* xbuffer_ptr */
	mem_len += xnum * sizeof(void *);	/* xbuffer_internal */
	mem_len += xnum * rp->datablklen;	/* xbuffer_ptr[xnum] */

	if (!(p = diva_os_malloc(0, mem_len))) {
		DBG_ERR(("CAPI_REGISTER - memory allocation failed"))
		return;
	}
	memset(p, 0, mem_len);

	DataNCCI = (void *)p;
	p += bnum * sizeof(word);
	DataFlags = (void *)p;
	p += bnum * sizeof(word);
	ReceiveBuffer = (void *)p;
	p += bnum * rp->datablklen;
	xbuffer_used = (void *)p;
	p += xnum;
	xbuffer_ptr = (void **)p;
	p += xnum * sizeof(void *);
	xbuffer_internal = (void **)p;
	p += xnum * sizeof(void *);
	for (i = 0; i < xnum; i++) {
		xbuffer_ptr[i] = (void *)p;
		p += rp->datablklen;
	}

	DBG_LOG(("CAPI_REGISTER - Id = %d", appl))
	DBG_LOG(("  MaxLogicalConnections = %d", rp->level3cnt))
	DBG_LOG(("  MaxBDataBuffers       = %d", rp->datablkcnt))
	DBG_LOG(("  MaxBDataLength        = %d", rp->datablklen))
	DBG_LOG(("  Allocated Memory      = %d", mem_len))

	/* initialize application data */
	diva_os_enter_spin_lock(&api_lock, &old_irql, "register_appl");

	this = &application[appl - 1];
	memset(this, 0, sizeof(APPL));

	this->Id = appl;

	for (i = 0; i < max_adapter; i++) {
		adapter[i].CIP_Mask[appl - 1] = 0;
	}

	this->queue_size = 1000;

	this->MaxNCCI = (byte) rp->level3cnt;
	this->MaxNCCIData = (byte) rp->datablkcnt;
	this->MaxBuffer = bnum;
	this->MaxDataLength = rp->datablklen;

	this->DataNCCI = DataNCCI;
	this->DataFlags = DataFlags;
	this->ReceiveBuffer = ReceiveBuffer;
	this->xbuffer_used = xbuffer_used;
	this->xbuffer_ptr = xbuffer_ptr;
	this->xbuffer_internal = xbuffer_internal;
	for (i = 0; i < xnum; i++) {
		this->xbuffer_ptr[i] = xbuffer_ptr[i];
	}

	CapiRegister(this->Id);
	diva_os_leave_spin_lock(&api_lock, &old_irql, "register_appl");

}

/*
 *  release appl
 */
static void diva_release_appl(struct capi_ctr *ctrl, __u16 appl)
{
	diva_os_spin_lock_magic_t old_irql;
	APPL *this = &application[appl - 1];

	DBG_TRC(("application %d(%d) cleanup", this->Id, appl))

	if (diva_os_in_irq()) {
		DBG_ERR(("CAPI_RELEASE - in irq context !"))
		return;
	}

	diva_os_enter_spin_lock(&api_lock, &old_irql, "release_appl");
	if (this->Id) {
		CapiRelease(this->Id);
		if (this->DataNCCI)
			diva_os_free(0, this->DataNCCI);
		this->DataNCCI = NULL;
		this->Id = 0;
	}
	diva_os_leave_spin_lock(&api_lock, &old_irql, "release_appl");

}

/*
 *  send message
 */
static u16 diva_send_message(struct capi_ctr *ctrl,
			     diva_os_message_buffer_s * dmb)
{
	int i = 0;
	word ret = 0;
	diva_os_spin_lock_magic_t old_irql;
	CAPI_MSG *msg = (CAPI_MSG *) DIVA_MESSAGE_BUFFER_DATA(dmb);
	APPL *this = &application[READ_WORD(&msg->header.appl_id) - 1];
	diva_card *card = ctrl->driverdata;
	__u32 length = DIVA_MESSAGE_BUFFER_LEN(dmb);
	word clength = READ_WORD(&msg->header.length);
	word command = READ_WORD(&msg->header.command);
	u16 retval = CAPI_NOERROR;

	if (diva_os_in_irq()) {
		DBG_ERR(("CAPI_SEND_MSG - in irq context !"))
		return CAPI_REGOSRESOURCEERR;
	}
	DBG_PRV1(("Write - appl = %d, cmd = 0x%x", this->Id, command))

	if (!this->Id) {
		return CAPI_ILLAPPNR;
	}

	/* patch controller number */
	msg->header.controller = ControllerMap[card->Id]
	    | (msg->header.controller & 0x80);	/* preserve external controller bit */

	diva_os_enter_spin_lock(&api_lock, &old_irql, "send message");

	switch (command) {
	default:
		xlog("\x00\x02", msg, 0x80, clength);
		break;

	case _DATA_B3_I | RESPONSE:
#ifndef DIVA_NO_DEBUGLIB
		if (myDriverDebugHandle.dbgMask & DL_BLK)
			xlog("\x00\x02", msg, 0x80, clength);
#endif
		break;

	case _DATA_B3_R:
#ifndef DIVA_NO_DEBUGLIB
		if (myDriverDebugHandle.dbgMask & DL_BLK)
			xlog("\x00\x02", msg, 0x80, clength);
#endif

		if (clength == 24)
			clength = 22;	/* workaround for PPcom bug */
		/* header is always 22      */
		if (READ_WORD(&msg->info.data_b3_req.Data_Length) >
		    this->MaxDataLength
		    || READ_WORD(&msg->info.data_b3_req.Data_Length) >
		    (length - clength)) {
			DBG_ERR(("Write - invalid message size"))
			retval = CAPI_ILLCMDORSUBCMDORMSGTOSMALL;
			goto write_end;
		}

		for (i = 0; i < (MAX_DATA_B3 * this->MaxNCCI)
		     && this->xbuffer_used[i]; i++);
		if (i == (MAX_DATA_B3 * this->MaxNCCI)) {
			DBG_ERR(("Write - too many data pending"))
			retval = CAPI_SENDQUEUEFULL;
			goto write_end;
		}
		msg->info.data_b3_req.Data = i;

		this->xbuffer_internal[i] = NULL;
		memcpy(this->xbuffer_ptr[i], &((__u8 *) msg)[clength],
		       READ_WORD(&msg->info.data_b3_req.Data_Length));

#ifndef DIVA_NO_DEBUGLIB
		if ((myDriverDebugHandle.dbgMask & DL_BLK)
		    && (myDriverDebugHandle.dbgMask & DL_XLOG)) {
			int j;
			for (j = 0; j <
			     READ_WORD(&msg->info.data_b3_req.Data_Length);
			     j += 256) {
				DBG_BLK((((char *) this->xbuffer_ptr[i]) + j,
					((READ_WORD(&msg->info.data_b3_req.Data_Length) - j) <
					  256) ? (READ_WORD(&msg->info.data_b3_req.Data_Length) - j) : 256))
				if (!(myDriverDebugHandle.dbgMask & DL_PRV0))
					break;	/* not more if not explicitely requested */
			}
		}
#endif
		break;
	}

	memcpy(mapped_msg, msg, (__u32) clength);
	mapped_msg->header.controller = MapController(mapped_msg->header.controller);
	mapped_msg->header.length = clength;
	mapped_msg->header.command = command;
	mapped_msg->header.number = READ_WORD(&msg->header.number);

	ret = api_put(this, mapped_msg);
	switch (ret) {
	case 0:
		break;
	case _BAD_MSG:
		DBG_ERR(("Write - bad message"))
		retval = CAPI_ILLCMDORSUBCMDORMSGTOSMALL;
		break;
	case _QUEUE_FULL:
		DBG_ERR(("Write - queue full"))
		retval = CAPI_SENDQUEUEFULL;
		break;
	default:
		DBG_ERR(("Write - api_put returned unknown error"))
		retval = CAPI_UNKNOWNNOTPAR;
		break;
	}

      write_end:
	diva_os_leave_spin_lock(&api_lock, &old_irql, "send message");
	diva_os_free_message_buffer(dmb);
	return retval;
}


/*
 * cards request function
 */
static void DIRequest(ENTITY * e)
{
	DIVA_CAPI_ADAPTER *a = &(adapter[(byte) e->user[0]]);
	diva_card *os_card = (diva_card *) a->os_card;

	if (e->Req && (a->FlowControlIdTable[e->ReqCh] == e->Id)) {
		a->FlowControlSkipTable[e->ReqCh] = 1;
	}

	(*(os_card->d.request)) (e);
}

/*
 * callback function from didd
 */
static void didd_callback(void *context, DESCRIPTOR * adapter, int removal)
{
	if (adapter->type == IDI_DADAPTER) {
		DBG_ERR(("Notification about IDI_DADAPTER change ! Oops."));
		return;
	} else if (adapter->type == IDI_DIMAINT) {
		if (removal) {
			stop_dbg();
		} else {
			memcpy(&MAdapter, adapter, sizeof(MAdapter));
			dprintf = (DIVA_DI_PRINTF) MAdapter.request;
			DbgRegister("CAPI20", DRIVERRELEASE_CAPI, DBG_DEFAULT);
		}
	} else if ((adapter->type > 0) && (adapter->type < 16)) {	/* IDI Adapter */
		if (removal) {
			divacapi_remove_card(adapter);
		} else {
			diva_add_card(adapter);
		}
	}
	return;
}

/*
 * connect to didd
 */
static int divacapi_connect_didd(void)
{
	int x = 0;
	int dadapter = 0;
	IDI_SYNC_REQ req;
	DESCRIPTOR DIDD_Table[MAX_DESCRIPTORS];

	DIVA_DIDD_Read(DIDD_Table, sizeof(DIDD_Table));

	for (x = 0; x < MAX_DESCRIPTORS; x++) {
		if (DIDD_Table[x].type == IDI_DIMAINT) {	/* MAINT found */
			memcpy(&MAdapter, &DIDD_Table[x], sizeof(DAdapter));
			dprintf = (DIVA_DI_PRINTF) MAdapter.request;
			DbgRegister("CAPI20", DRIVERRELEASE_CAPI, DBG_DEFAULT);
			break;
		}
	}
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
		}
			else if ((DIDD_Table[x].type > 0) && (DIDD_Table[x].type < 16)) {	/* IDI Adapter found */
			diva_add_card(&DIDD_Table[x]);
		}
	}

	if (!dadapter) {
		stop_dbg();
	}

	return (dadapter);
}

/*
 * diconnect from didd
 */
static void divacapi_disconnect_didd(void)
{
	IDI_SYNC_REQ req;

	stop_dbg();

	req.didd_notify.e.Req = 0;
	req.didd_notify.e.Rc = IDI_SYNC_REQ_DIDD_REMOVE_ADAPTER_NOTIFY;
	req.didd_notify.info.handle = notify_handle;
	DAdapter.request((ENTITY *) & req);
}

/*
 * we do not provide date/time here,
 * the application should do this. 
 */
int fax_head_line_time(char *buffer)
{
	return (0);
}

/*
 * init (alloc) main structures
 */
static int DIVA_INIT_FUNCTION init_main_structs(void)
{
	if (!(mapped_msg = (CAPI_MSG *) diva_os_malloc(0, MAX_MSG_SIZE))) {
		DBG_ERR(("init: failed alloc mapped_msg."))
		    return 0;
	}

	if (!(adapter = diva_os_malloc(0, sizeof(DIVA_CAPI_ADAPTER) * MAX_DESCRIPTORS))) {
		DBG_ERR(("init: failed alloc adapter struct."))
		diva_os_free(0, mapped_msg);
		return 0;
	}
	memset(adapter, 0, sizeof(DIVA_CAPI_ADAPTER) * MAX_DESCRIPTORS);

	if (!(application = diva_os_malloc(0, sizeof(APPL) * MAX_APPL))) {
		DBG_ERR(("init: failed alloc application struct."))
		diva_os_free(0, mapped_msg);
		diva_os_free(0, adapter);
		return 0;
	}
	memset(application, 0, sizeof(APPL) * MAX_APPL);

	return (1);
}

/*
 * remove (free) main structures
 */
static void remove_main_structs(void)
{
	if (application)
		diva_os_free(0, application);
	if (adapter)
		diva_os_free(0, adapter);
	if (mapped_msg)
		diva_os_free(0, mapped_msg);
}

/*
 * init
 */
int DIVA_INIT_FUNCTION init_capifunc(void)
{
	diva_os_initialize_spin_lock(&ll_lock, "capifunc");
	diva_os_initialize_spin_lock(&api_lock, "capifunc");
	memset(ControllerMap, 0, MAX_DESCRIPTORS + 1);
	max_adapter = 0;


	if (!init_main_structs()) {
		DBG_ERR(("init: failed to init main structs."))
		return (0);
	}

	if (!divacapi_connect_didd()) {
		DBG_ERR(("init: failed to connect to DIDD."))
		remove_main_structs();
		return (0);
	}

	return (1);
}

/*
 * finit
 */
void DIVA_EXIT_FUNCTION finit_capifunc(void)
{
	int count = 100;
	word ret = 1;

	while (ret && count--) {
		ret = api_remove_start();
		diva_os_sleep(10);
	}
	if (ret)
		DBG_ERR(("could not remove signaling ID's"))

		    divacapi_disconnect_didd();
	divacapi_remove_cards();

	remove_main_structs();

	diva_os_destroy_spin_lock(&api_lock, "capifunc");
	diva_os_destroy_spin_lock(&ll_lock, "capifunc");
}
