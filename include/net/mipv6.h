/*
 *	Mobile IPv6 header-file
 *
 *	Authors:
 *	Sami Kivisaari		<skivisaa@cc.hut.fi>
 *
 *	$Id$
 *
 *	This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 *
 */

#ifndef _NET_MIPV6_H
#define _NET_MIPV6_H

#include <linux/types.h>
#include <asm/byteorder.h>
#include <linux/in6.h>

/*
 *
 * Mobile IPv6 Protocol constants
 *
 */
#define DHAAD_RETRIES			4	/* transmissions	*/
#define INITIAL_BINDACK_TIMEOUT		1	/* seconds 		*/
#define INITIAL_DHAAD_TIMEOUT		3	/* seconds		*/
#define INITIAL_SOLICIT_TIMER		3	/* seconds		*/
#define MAX_BINDACK_TIMEOUT		32 	/* seconds		*/
#define MAX_NONCE_LIFE			240	/* seconds		*/
#define MAX_TOKEN_LIFE			210	/* seconds		*/
#define MAX_RR_BINDING_LIFE		420	/* seconds		*/
#define MAX_UPDATE_RATE			3	/* 1/s (min delay=1s) 	*/
#define PREFIX_ADV_RETRIES		3	/* transmissions	*/
#define PREFIX_ADV_TIMEOUT		3	/* seconds		*/

#define MAX_FAST_UPDATES		5 	/* transmissions	*/
#define MAX_PFX_ADV_DELAY		1000	/* seconds		*/
#define SLOW_UPDATE_RATE		10	/* 1/10s (max delay=10s)*/
#define INITIAL_BINDACK_DAD_TIMEOUT	3	/* seconds		*/

/*
 *
 * Mobile IPv6 Protocol configuration variable defaults
 *
 */
#define DefHomeRtrAdvInterval		1000	/* seconds		*/
#define DefMaxMobPfxAdvInterval		86400	/* seconds		*/
#define DefMinDelayBetweenRAs		3	/* seconds (min 0.03)	*/
#define DefMinMobPfxAdvInterval		600	/* seconds		*/
#define DefInitialBindackTimeoutFirstReg	1.5 /* seconds		*/

/* This is not actually specified in the draft, but is needed to avoid
 * prefix solicitation storm when valid lifetime of a prefix is smaller
 * than MAX_PFX_ADV_DELAY
 */
#define MIN_PFX_SOL_DELAY		5	/* seconds		*/

/* Binding update flag codes              */
#define MIPV6_BU_F_ACK			0x80
#define MIPV6_BU_F_HOME			0x40
#define MIPV6_BU_F_LLADDR		0x20
#define MIPV6_BU_F_KEYMGM		0x10

/* Binding ackknowledgment flag codes */
#define MIPV6_BA_F_KEYMGM		0x80

/* Binding error status */
#define MIPV6_BE_HAO_WO_BINDING		1
#define MIPV6_BE_UNKNOWN_MH_TYPE	2

/* Mobility Header */
struct mipv6_mh
{
	__u8	payload;		/* Payload Protocol 		*/
	__u8	length;			/* MH Length 			*/
	__u8	type;			/* MH Type			*/
	__u8	reserved;		/* Reserved			*/
	__u16	checksum;		/* Checksum			*/
	__u8	data[0];		/* Message specific data	*/
} __attribute__ ((packed));

/* Mobility Header type */
#define IPPROTO_MOBILITY                135 /* TODO: No official protocol number at this point */                
/* Mobility Header Message Types */

#define MIPV6_MH_BRR			0
#define MIPV6_MH_HOTI			1
#define MIPV6_MH_COTI			2
#define MIPV6_MH_HOT			3
#define MIPV6_MH_COT			4
#define MIPV6_MH_BU			5
#define MIPV6_MH_BA			6
#define MIPV6_MH_BE			7

/*
 * Status codes for Binding Acknowledgements
 */
#define SUCCESS				0
#define REASON_UNSPECIFIED		128
#define ADMINISTRATIVELY_PROHIBITED	129
#define INSUFFICIENT_RESOURCES		130
#define HOME_REGISTRATION_NOT_SUPPORTED	131
#define NOT_HOME_SUBNET			132
#define NOT_HA_FOR_MN			133
#define DUPLICATE_ADDR_DETECT_FAIL	134
#define SEQUENCE_NUMBER_OUT_OF_WINDOW	135
#define EXPIRED_HOME_NONCE_INDEX	136
#define EXPIRED_CAREOF_NONCE_INDEX	137
#define EXPIRED_NONCES			138
#define REG_TYPE_CHANGE_FORBIDDEN       139
/*
 * Values for mipv6_flags in struct inet6_skb_parm
 */

#define MIPV6_RCV_TUNNEL		0x1
#define MIPV6_SND_HAO			0x2


/*
 * Mobility Header Message structures
 */

struct mipv6_mh_brr
{
	__u16		reserved;
	/* Mobility options */
} __attribute__ ((packed));

struct mipv6_mh_bu
{
	__u16		sequence;	/* sequence number of BU	*/
	__u8		flags;		/* flags			*/
	__u8		reserved;	/* reserved bits		*/
	__u16		lifetime;	/* lifetime of BU		*/
	/* Mobility options */
} __attribute__ ((packed));

struct mipv6_mh_ba
{
	__u8		status;		/* statuscode			*/
	__u8		reserved;	/* reserved bits		*/
	__u16		sequence;	/* sequence number of BA	*/
	__u16		lifetime;	/* lifetime in CN's bcache	*/
	/* Mobility options */
} __attribute__ ((packed));

struct mipv6_mh_be
{
	__u8		status;
	__u8		reserved;
	struct in6_addr	home_addr;
	/* Mobility options */
} __attribute__ ((packed));

struct mipv6_mh_addr_ti
{
	__u16		reserved;	/* Reserved			*/
	u_int8_t	init_cookie[8]; /* HoT/CoT Init Cookie		*/
	/* Mobility options */
} __attribute__ ((packed));

struct mipv6_mh_addr_test
{
	__u16		nonce_index;    /* Home/Care-of Nonce Index	*/
	u_int8_t	init_cookie[8]; /* HoT/CoT Init Cookie		*/
	u_int8_t	kgen_token[8];	/* Home/Care-of key generation token */
	/* Mobility options */
} __attribute__ ((packed));

/*
 * Mobility Options for various MH types.
 */
#define MIPV6_OPT_PAD1			0x00
#define MIPV6_OPT_PADN			0x01
#define MIPV6_OPT_BIND_REFRESH_ADVICE	0x02
#define MIPV6_OPT_ALTERNATE_COA		0x03
#define MIPV6_OPT_NONCE_INDICES		0x04
#define MIPV6_OPT_AUTH_DATA		0x05

#define MIPV6_SEQ_GT(x,y) \
        ((short int)(((__u16)(x)) - ((__u16)(y))) > 0)

/*
 * Mobility Option structures
 */

struct mipv6_mo
{
	__u8		type;
	__u8		length;
	__u8		value[0];	/* type specific data */
} __attribute__ ((packed));

struct mipv6_mo_pad1
{
	__u8		type;
} __attribute__ ((packed));

struct mipv6_mo_padn
{
	__u8		type;
	__u8		length;
	__u8		data[0];
} __attribute__ ((packed));

struct mipv6_mo_alt_coa
{
	__u8		type;
	__u8		length;
	struct in6_addr	addr;		/* alternate care-of-address	*/
} __attribute__ ((packed));

struct mipv6_mo_nonce_indices
{
	__u8		type;
	__u8		length;
	__u16		home_nonce_i;	/* Home Nonce Index		*/
	__u16		careof_nonce_i;	/* Careof Nonce Index		*/
} __attribute__ ((packed)); 

struct mipv6_mo_bauth_data
{
	__u8		type;
	__u8		length;
	__u8		data[0];
} __attribute__ ((packed)); 

struct mipv6_mo_br_advice
{
	__u8		type;
	__u8		length;
	__u16		refresh_interval; /* Refresh Interval		*/
} __attribute__ ((packed));

/*
 * Home Address Destination Option structure
 */
struct mipv6_dstopt_homeaddr
{
	__u8		type;		/* type-code for option 	*/
	__u8		length;		/* option length 		*/
	struct in6_addr	addr;		/* home address 		*/
} __attribute__ ((packed));

#endif /* _NET_MIPV6_H */
