/* $Id: i4l_idi.h,v 1.1.2.2 2002/10/02 14:38:37 armin Exp $
 *
 * ISDN interface module for Eicon active cards.
 * I4L - IDI Interface
 *
 * Copyright 1998-2000  by Armin Schindler (mac@melware.de)
 * Copyright 1999-2002  Cytronics & Melware (info@melware.de)
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 */

#ifndef E_IDI_H
#define E_IDI_H

#include <linux/config.h>

#undef N_DATA
#undef ID_MASK

#include "pc.h"

#define AOC_IND  26		/* Advice of Charge                         */
#define PI  0x1e                /* Progress Indicator               */
#define NI  0x27                /* Notification Indicator           */

/* defines for statectrl */
#define WAITING_FOR_HANGUP	0x01
#define HAVE_CONN_REQ		0x02
#define IN_HOLD			0x04

typedef struct {
	char cpn[32];
	char oad[32];
	char dsa[32];
	char osa[32];
	__u8 plan;
	__u8 screen;
	__u8 sin[4];
	__u8 chi[4];
	__u8 e_chi[4];
	__u8 bc[12];
	__u8 e_bc[12];
 	__u8 llc[18];
	__u8 hlc[5];
	__u8 cau[4];
	__u8 e_cau[2];
	__u8 e_mt;
	__u8 dt[6];
	char display[83];
	char keypad[35];
	char rdn[32];
} idi_ind_message;

typedef struct { 
  __u16 next            __attribute__ ((packed));
  __u8  Req             __attribute__ ((packed));
  __u8  ReqId           __attribute__ ((packed));
  __u8  ReqCh           __attribute__ ((packed));
  __u8  Reserved1       __attribute__ ((packed));
  __u16 Reference       __attribute__ ((packed));
  __u8  Reserved[8]     __attribute__ ((packed));
  eicon_PBUFFER XBuffer; 
} eicon_REQ;

typedef struct {
  __u16 next            __attribute__ ((packed));
  __u8  Rc              __attribute__ ((packed));
  __u8  RcId            __attribute__ ((packed));
  __u8  RcCh            __attribute__ ((packed));
  __u8  Reserved1       __attribute__ ((packed));
  __u16 Reference       __attribute__ ((packed));
  __u8  Reserved2[8]    __attribute__ ((packed));
} eicon_RC;

typedef struct {
  __u16 next            __attribute__ ((packed));
  __u8  Ind             __attribute__ ((packed));
  __u8  IndId           __attribute__ ((packed));
  __u8  IndCh           __attribute__ ((packed));
  __u8  MInd            __attribute__ ((packed));
  __u16 MLength         __attribute__ ((packed));
  __u16 Reference       __attribute__ ((packed));
  __u8  RNR             __attribute__ ((packed));
  __u8  Reserved        __attribute__ ((packed));
  __u32 Ack             __attribute__ ((packed));
  eicon_PBUFFER RBuffer;
} eicon_IND;

typedef struct {
	__u8		*Data;
	unsigned int	Size;
	unsigned int	Len;
	__u8		*Next;
} eicon_OBJBUFFER;

extern int idi_do_req(eicon_card *card, eicon_chan *chan, int cmd, int layer);
extern int idi_hangup(eicon_card *card, eicon_chan *chan);
extern int idi_connect_res(eicon_card *card, eicon_chan *chan);
extern int eicon_idi_listen_req(eicon_card *card, eicon_chan *chan);
extern int idi_connect_req(eicon_card *card, eicon_chan *chan, char *phone,
	                    char *eazmsn, int si1, int si2);

extern void idi_handle_ack(eicon_card *card, struct sk_buff *skb);
extern void idi_handle_ind(eicon_card *card, struct sk_buff *skb);
extern int eicon_idi_manage(eicon_card *card, eicon_manifbuf *mb);
extern int idi_send_data(eicon_card *card, eicon_chan *chan, int ack, struct sk_buff *skb, int que, int chk);
extern void idi_audio_cmd(eicon_card *ccard, eicon_chan *chan, int cmd, u_char *value);
extern int capipmsg(eicon_card *card, eicon_chan *chan, capi_msg *cm);
#ifdef CONFIG_ISDN_TTY_FAX
extern void idi_fax_cmd(eicon_card *card, eicon_chan *chan);
extern int idi_faxdata_send(eicon_card *ccard, eicon_chan *chan, struct sk_buff *skb);
#endif

#include "dsp_defs.h"

#define DSP_UDATA_REQUEST_SWITCH_FRAMER         1
/*
parameters:
  <byte> transmit framer type
  <byte> receive framer type
*/

#define DSP_REQUEST_SWITCH_FRAMER_HDLC          0
#define DSP_REQUEST_SWITCH_FRAMER_TRANSPARENT   1
#define DSP_REQUEST_SWITCH_FRAMER_ASYNC         2


#define DSP_UDATA_REQUEST_CLEARDOWN             2
/*
parameters:
  - none -
*/


#define DSP_UDATA_REQUEST_TX_CONFIRMATION_ON    3
/*
parameters:
  - none -
*/


#define DSP_UDATA_REQUEST_TX_CONFIRMATION_OFF   4
/*
parameters:
  - none -
*/

typedef struct eicon_dsp_ind {
	__u16	time		__attribute__ ((packed));
	__u8	norm		__attribute__ ((packed));
	__u16	options		__attribute__ ((packed));
	__u32	speed		__attribute__ ((packed));
	__u16	delay		__attribute__ ((packed));
	__u32	txspeed		__attribute__ ((packed));
	__u32	rxspeed		__attribute__ ((packed));
} eicon_dsp_ind;

#define DSP_CONNECTED_OPTION_V42_TRANS           0x0002
#define DSP_CONNECTED_OPTION_V42_LAPM            0x0004
#define DSP_CONNECTED_OPTION_SHORT_TRAIN         0x0008
#define DSP_CONNECTED_OPTION_TALKER_ECHO_PROTECT 0x0010

#define DSP_UDATA_INDICATION_DISCONNECT         5
/*
returns:
  <byte> cause
*/

#define DSP_DISCONNECT_CAUSE_NONE               0x00
#define DSP_DISCONNECT_CAUSE_BUSY_TONE          0x01
#define DSP_DISCONNECT_CAUSE_CONGESTION_TONE    0x02
#define DSP_DISCONNECT_CAUSE_INCOMPATIBILITY    0x03
#define DSP_DISCONNECT_CAUSE_CLEARDOWN          0x04
#define DSP_DISCONNECT_CAUSE_TRAINING_TIMEOUT   0x05

#define DSP_UDATA_INDICATION_TX_CONFIRMATION    6
/*
returns:
  <word> confirmation number
*/


#define DSP_UDATA_REQUEST_SEND_DTMF_DIGITS      16
/*
parameters:
  <word> tone duration (ms)
  <word> gap duration (ms)
  <byte> digit 0 tone code
  ...
  <byte> digit n tone code
*/

#define DSP_SEND_DTMF_DIGITS_HEADER_LENGTH      5

#define DSP_DTMF_DIGIT_TONE_LOW_GROUP_697_HZ    0x00
#define DSP_DTMF_DIGIT_TONE_LOW_GROUP_770_HZ    0x01
#define DSP_DTMF_DIGIT_TONE_LOW_GROUP_852_HZ    0x02
#define DSP_DTMF_DIGIT_TONE_LOW_GROUP_941_HZ    0x03
#define DSP_DTMF_DIGIT_TONE_LOW_GROUP_MASK      0x03
#define DSP_DTMF_DIGIT_TONE_HIGH_GROUP_1209_HZ  0x00
#define DSP_DTMF_DIGIT_TONE_HIGH_GROUP_1336_HZ  0x04
#define DSP_DTMF_DIGIT_TONE_HIGH_GROUP_1477_HZ  0x08
#define DSP_DTMF_DIGIT_TONE_HIGH_GROUP_1633_HZ  0x0c
#define DSP_DTMF_DIGIT_TONE_HIGH_GROUP_MASK     0x0c

#define DSP_DTMF_DIGIT_TONE_CODE_0              0x07
#define DSP_DTMF_DIGIT_TONE_CODE_1              0x00
#define DSP_DTMF_DIGIT_TONE_CODE_2              0x04
#define DSP_DTMF_DIGIT_TONE_CODE_3              0x08
#define DSP_DTMF_DIGIT_TONE_CODE_4              0x01
#define DSP_DTMF_DIGIT_TONE_CODE_5              0x05
#define DSP_DTMF_DIGIT_TONE_CODE_6              0x09
#define DSP_DTMF_DIGIT_TONE_CODE_7              0x02
#define DSP_DTMF_DIGIT_TONE_CODE_8              0x06
#define DSP_DTMF_DIGIT_TONE_CODE_9              0x0a
#define DSP_DTMF_DIGIT_TONE_CODE_STAR           0x03
#define DSP_DTMF_DIGIT_TONE_CODE_HASHMARK       0x0b
#define DSP_DTMF_DIGIT_TONE_CODE_A              0x0c
#define DSP_DTMF_DIGIT_TONE_CODE_B              0x0d
#define DSP_DTMF_DIGIT_TONE_CODE_C              0x0e
#define DSP_DTMF_DIGIT_TONE_CODE_D              0x0f


#define DSP_UDATA_INDICATION_DTMF_DIGITS_SENT   16
/*
returns:
  - none -
  One indication will be sent for every request.
*/


#define DSP_UDATA_REQUEST_ENABLE_DTMF_RECEIVER  17
/*
parameters:
  <word> tone duration (ms)
  <word> gap duration (ms)
*/
typedef struct enable_dtmf_s {
	__u16 tone;
	__u16 gap;
} enable_dtmf_s;

#define DSP_UDATA_REQUEST_DISABLE_DTMF_RECEIVER 18
/*
parameters:
  - none -
*/

#define DSP_UDATA_INDICATION_DTMF_DIGITS_RECEIVED 17
/*
returns:
  <byte> digit 0 tone code
  ...
  <byte> digit n tone code
*/

#define DSP_DTMF_DIGITS_RECEIVED_HEADER_LENGTH  1


#define DSP_UDATA_INDICATION_MODEM_CALLING_TONE 18
/*
returns:
  - none -
*/

#define DSP_UDATA_INDICATION_FAX_CALLING_TONE   19
/*
returns:
  - none -
*/

#define DSP_UDATA_INDICATION_ANSWER_TONE        20
/*
returns:
  - none -
*/

/* ============= FAX ================ */

#define EICON_FAXID_LEN 20

typedef struct eicon_t30_s {
  __u8          code;
  __u8          rate;
  __u8          resolution;
  __u8          format;
  __u8          pages_low;
  __u8          pages_high;
  __u8          atf;
  __u8          control_bits_low;
  __u8          control_bits_high;
  __u8          feature_bits_low;
  __u8          feature_bits_high;
  __u8          universal_5;
  __u8          universal_6;
  __u8          universal_7;
  __u8          station_id_len;
  __u8          head_line_len;
  __u8          station_id[EICON_FAXID_LEN];
/* __u8          head_line[]; */
} eicon_t30_s;

        /* EDATA transmit messages */
#define EDATA_T30_DIS       0x01
#define EDATA_T30_FTT       0x02
#define EDATA_T30_MCF       0x03

        /* EDATA receive messages */
#define EDATA_T30_DCS       0x81
#define EDATA_T30_TRAIN_OK  0x82
#define EDATA_T30_EOP       0x83
#define EDATA_T30_MPS       0x84
#define EDATA_T30_EOM       0x85
#define EDATA_T30_DTC       0x86

#define T30_FORMAT_SFF            0
#define T30_FORMAT_ASCII          1
#define T30_FORMAT_COUNT          2

#define T30_CONTROL_BIT_DISABLE_FINE      0x0001
#define T30_CONTROL_BIT_ENABLE_ECM        0x0002
#define T30_CONTROL_BIT_ECM_64_BYTES      0x0004
#define T30_CONTROL_BIT_ENABLE_2D_CODING  0x0008
#define T30_CONTROL_BIT_ENABLE_T6_CODING  0x0010
#define T30_CONTROL_BIT_ENABLE_UNCOMPR    0x0020
#define T30_CONTROL_BIT_ACCEPT_POLLING    0x0040
#define T30_CONTROL_BIT_REQUEST_POLLING   0x0080
#define T30_CONTROL_BIT_MORE_DOCUMENTS    0x0100

#define T30_CONTROL_BIT_ALL_FEATURES\
  (T30_CONTROL_BIT_ENABLE_ECM | T30_CONTROL_BIT_ENABLE_2D_CODING |\
   T30_CONTROL_BIT_ENABLE_T6_CODING | T30_CONTROL_BIT_ENABLE_UNCOMPR)

#define T30_FEATURE_BIT_FINE              0x0001
#define T30_FEATURE_BIT_ECM               0x0002
#define T30_FEATURE_BIT_ECM_64_BYTES      0x0004
#define T30_FEATURE_BIT_2D_CODING         0x0008
#define T30_FEATURE_BIT_T6_CODING         0x0010
#define T30_FEATURE_BIT_UNCOMPR_ENABLED   0x0020
#define T30_FEATURE_BIT_POLLING           0x0040

#define FAX_OBJECT_DOCU		1
#define FAX_OBJECT_PAGE		2
#define FAX_OBJECT_LINE		3

#define T4_EOL			0x800
#define T4_EOL_BITSIZE		12
#define T4_EOL_DWORD		(T4_EOL << (32 - T4_EOL_BITSIZE))
#define T4_EOL_MASK_DWORD	((__u32) -1 << (32 - T4_EOL_BITSIZE))

#define SFF_LEN_FLD_SIZE	3

#define _DLE_	0x10
#define _ETX_	0x03

typedef struct eicon_sff_dochead {
	__u32	id		__attribute__ ((packed));
	__u8	version		__attribute__ ((packed));
	__u8	reserved1	__attribute__ ((packed));
	__u16	userinfo	__attribute__ ((packed));
	__u16	pagecount	__attribute__ ((packed));
	__u16	off1pagehead	__attribute__ ((packed));
	__u32	offnpagehead	__attribute__ ((packed));
	__u32	offdocend	__attribute__ ((packed));
} eicon_sff_dochead;

typedef struct eicon_sff_pagehead {
	__u8	pageheadid	__attribute__ ((packed));
	__u8	pageheadlen	__attribute__ ((packed));
	__u8	resvert		__attribute__ ((packed));
	__u8	reshoriz	__attribute__ ((packed));
	__u8	coding		__attribute__ ((packed));
	__u8	reserved2	__attribute__ ((packed));
	__u16	linelength	__attribute__ ((packed));
	__u16	pagelength	__attribute__ ((packed));
	__u32	offprevpage	__attribute__ ((packed));
	__u32	offnextpage	__attribute__ ((packed));
} eicon_sff_pagehead;

#endif	/* E_IDI_H */
