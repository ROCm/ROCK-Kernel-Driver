/*
 * $Id: b1lli.h,v 1.8 1999/07/01 15:26:54 calle Exp $
 *
 * ISDN lowlevel-module for AVM B1-card.
 *
 * Copyright 1996 by Carsten Paeth (calle@calle.in-berlin.de)
 *
 * $Log: b1lli.h,v $
 * Revision 1.8  1999/07/01 15:26:54  calle
 * complete new version (I love it):
 * + new hardware independed "capi_driver" interface that will make it easy to:
 *   - support other controllers with CAPI-2.0 (i.e. USB Controller)
 *   - write a CAPI-2.0 for the passive cards
 *   - support serial link CAPI-2.0 boxes.
 * + wrote "capi_driver" for all supported cards.
 * + "capi_driver" (supported cards) now have to be configured with
 *   make menuconfig, in the past all supported cards where included
 *   at once.
 * + new and better informations in /proc/capi/
 * + new ioctl to switch trace of capi messages per controller
 *   using "avmcapictrl trace [contr] on|off|...."
 * + complete testcircle with all supported cards and also the
 *   PCMCIA cards (now patch for pcmcia-cs-3.0.13 needed) done.
 *
 * Revision 1.7  1999/06/21 15:24:25  calle
 * extend information in /proc.
 *
 * Revision 1.6  1999/04/15 19:49:36  calle
 * fix fuer die B1-PCI. Jetzt geht z.B. auch IRQ 17 ...
 *
 * Revision 1.5  1998/10/25 14:50:28  fritz
 * Backported from MIPS (Cobalt).
 *
 * Revision 1.4  1998/03/29 16:05:02  calle
 * changes from 2.0 tree merged.
 *
 * Revision 1.1.2.9  1998/03/20 14:30:02  calle
 * added cardnr to detect if you try to add same T1 to different io address.
 * change number of nccis depending on number of channels.
 *
 * Revision 1.1.2.8  1998/03/04 17:32:33  calle
 * Changes for T1.
 *
 * Revision 1.1.2.7  1998/02/27 15:38:29  calle
 * T1 running with slow link.
 *
 * Revision 1.1.2.6  1998/02/24 17:57:36  calle
 * changes for T1.
 *
 * Revision 1.3  1998/01/31 10:54:37  calle
 * include changes for PCMCIA cards from 2.0 version
 *
 * Revision 1.2  1997/12/10 19:38:42  calle
 * get changes from 2.0 tree
 *
 * Revision 1.1.2.2  1997/11/26 16:57:26  calle
 * more changes for B1/M1/T1.
 *
 * Revision 1.1.2.1  1997/11/26 10:47:01  calle
 * prepared for M1 (Mobile) and T1 (PMX) cards.
 * prepared to set configuration after load to support other D-channel
 * protocols, point-to-point and leased lines.
 *
 * Revision 1.1  1997/03/04 21:27:32  calle
 * First version in isdn4linux
 *
 * Revision 2.2  1997/02/12 09:31:39  calle
 * new version
 *
 * Revision 1.1  1997/01/31 10:32:20  calle
 * Initial revision
 *
 */

#ifndef _B1LLI_H_
#define _B1LLI_H_
/*
 * struct for loading t4 file 
 */
typedef struct avmb1_t4file {
	int len;
	unsigned char *data;
} avmb1_t4file;

typedef struct avmb1_loaddef {
	int contr;
	avmb1_t4file t4file;
} avmb1_loaddef;

typedef struct avmb1_loadandconfigdef {
	int contr;
	avmb1_t4file t4file;
        avmb1_t4file t4config; 
} avmb1_loadandconfigdef;

typedef struct avmb1_resetdef {
	int contr;
} avmb1_resetdef;

typedef struct avmb1_getdef {
	int contr;
	int cardtype;
	int cardstate;
} avmb1_getdef;

/*
 * struct for adding new cards 
 */
typedef struct avmb1_carddef {
	int port;
	int irq;
} avmb1_carddef;

#define AVM_CARDTYPE_B1		0
#define AVM_CARDTYPE_T1		1
#define AVM_CARDTYPE_M1		2
#define AVM_CARDTYPE_M2		3

typedef struct avmb1_extcarddef {
	int port;
	int irq;
        int cardtype;
        int cardnr;  /* for HEMA/T1 */
} avmb1_extcarddef;

#define	AVMB1_LOAD		0	/* load image to card */
#define AVMB1_ADDCARD		1	/* add a new card */
#define AVMB1_RESETCARD		2	/* reset a card */
#define	AVMB1_LOAD_AND_CONFIG	3	/* load image and config to card */
#define	AVMB1_ADDCARD_WITH_TYPE	4	/* add a new card, with cardtype */
#define AVMB1_GET_CARDINFO	5	/* get cardtype */
#define AVMB1_REMOVECARD	6	/* remove a card (usefull for T1) */

#define	AVMB1_REGISTERCARD_IS_OBSOLETE

#endif				/* _B1LLI_H_ */
