/* hermes.h
 *
 * Driver core for the "Hermes" wireless MAC controller, as used in
 * the Lucent Orinoco and Cabletron RoamAbout cards. It should also
 * work on the hfa3841 and hfa3842 MAC controller chips used in the
 * Prism I & II chipsets.
 *
 * This is not a complete driver, just low-level access routines for
 * the MAC controller itself.
 *
 * Based on the prism2 driver from Absolute Value Systems' linux-wlan
 * project, the Linux wvlan_cs driver, Lucent's HCF-Light
 * (wvlan_hcf.c) library, and the NetBSD wireless driver.
 *
 * Copyright (C) 2000, David Gibson, Linuxcare Australia <hermes@gibson.dropbear.id.au>
 *
 * Portions taken from hfa384x.h, Copyright (C) 1999 AbsoluteValue Systems, Inc. All Rights Reserved.
 *
 * This file distributed under the GPL, version 2.
 */

#ifndef _HERMES_H
#define _HERMES_H

/* Notes on locking:
 *
 * As a module of low level hardware access routines, there is no
 * locking. Users of this module should ensure that they serialize
 * access to the hermes_t structure, and to the hardware
*/

#include <linux/delay.h>
#include <linux/if_ether.h>

/*
 * Limits and constants
 */
#define		HERMES_ALLOC_LEN_MIN		(4)
#define		HERMES_ALLOC_LEN_MAX		(2400)
#define		HERMES_LTV_LEN_MAX		(34)
#define		HERMES_BAP_DATALEN_MAX		(4096)
#define		HERMES_BAP_OFFSET_MAX		(4096)
#define		HERMES_PORTID_MAX		(7)
#define		HERMES_NUMPORTS_MAX		(HERMES_PORTID_MAX+1)
#define		HERMES_PDR_LEN_MAX		(260)	/* in bytes, from EK */
#define		HERMES_PDA_RECS_MAX		(200)	/* a guess */
#define		HERMES_PDA_LEN_MAX		(1024)	/* in bytes, from EK */
#define		HERMES_SCANRESULT_MAX		(35)
#define		HERMES_CHINFORESULT_MAX		(8)
#define		HERMES_FRAME_LEN_MAX		(2304)
#define		HERMES_MAX_MULTICAST		(16)
#define		HERMES_MAGIC			(0x7d1f)

/*
 * Hermes register offsets
 */
#define		HERMES_CMD			(0x00)
#define		HERMES_PARAM0			(0x02)
#define		HERMES_PARAM1			(0x04)
#define		HERMES_PARAM2			(0x06)
#define		HERMES_STATUS			(0x08)
#define		HERMES_RESP0			(0x0A)
#define		HERMES_RESP1			(0x0C)
#define		HERMES_RESP2			(0x0E)
#define		HERMES_INFOFID			(0x10)
#define		HERMES_RXFID			(0x20)
#define		HERMES_ALLOCFID			(0x22)
#define		HERMES_TXCOMPLFID		(0x24)
#define		HERMES_SELECT0			(0x18)
#define		HERMES_OFFSET0			(0x1C)
#define		HERMES_DATA0			(0x36)
#define		HERMES_SELECT1			(0x1A)
#define		HERMES_OFFSET1			(0x1E)
#define		HERMES_DATA1			(0x38)
#define		HERMES_EVSTAT			(0x30)
#define		HERMES_INTEN			(0x32)
#define		HERMES_EVACK			(0x34)
#define		HERMES_CONTROL			(0x14)
#define		HERMES_SWSUPPORT0		(0x28)
#define		HERMES_SWSUPPORT1		(0x2A)
#define		HERMES_SWSUPPORT2		(0x2C)
#define		HERMES_AUXPAGE			(0x3A)
#define		HERMES_AUXOFFSET		(0x3C)
#define		HERMES_AUXDATA			(0x3E)

/*
 * CMD register bitmasks
 */
#define		HERMES_CMD_BUSY			(0x8000)
#define		HERMES_CMD_AINFO		(0x7f00)
#define		HERMES_CMD_MACPORT		(0x0700)
#define		HERMES_CMD_RECL			(0x0100)
#define		HERMES_CMD_WRITE		(0x0100)
#define		HERMES_CMD_PROGMODE		(0x0300)
#define		HERMES_CMD_CMDCODE		(0x003f)

/*
 * STATUS register bitmasks
 */
#define		HERMES_STATUS_RESULT		(0x7f00)
#define		HERMES_STATUS_CMDCODE		(0x003f)

/*
 * OFFSET register bitmasks
 */
#define		HERMES_OFFSET_BUSY		(0x8000)
#define		HERMES_OFFSET_ERR		(0x4000)
#define		HERMES_OFFSET_DATAOFF		(0x0ffe)

/*
 * Event register bitmasks (INTEN, EVSTAT, EVACK)
 */
#define		HERMES_EV_TICK			(0x8000)
#define		HERMES_EV_WTERR			(0x4000)
#define		HERMES_EV_INFDROP		(0x2000)
#define		HERMES_EV_INFO			(0x0080)
#define		HERMES_EV_DTIM			(0x0020)
#define		HERMES_EV_CMD			(0x0010)
#define		HERMES_EV_ALLOC			(0x0008)
#define		HERMES_EV_TXEXC			(0x0004)
#define		HERMES_EV_TX			(0x0002)
#define		HERMES_EV_RX			(0x0001)

/*
 * Command codes
 */
/*--- Controller Commands --------------------------*/
#define		HERMES_CMD_INIT			(0x0000)
#define		HERMES_CMD_ENABLE		(0x0001)
#define		HERMES_CMD_DISABLE		(0x0002)
#define		HERMES_CMD_DIAG			(0x0003)

/*--- Buffer Mgmt Commands --------------------------*/
#define		HERMES_CMD_ALLOC		(0x000A)
#define		HERMES_CMD_TX			(0x000B)
#define		HERMES_CMD_CLRPRST		(0x0012)

/*--- Regulate Commands --------------------------*/
#define		HERMES_CMD_NOTIFY		(0x0010)
#define		HERMES_CMD_INQ			(0x0011)

/*--- Configure Commands --------------------------*/
#define		HERMES_CMD_ACCESS		(0x0021)
#define		HERMES_CMD_DOWNLD		(0x0022)

/*--- Debugging Commands -----------------------------*/
#define 	HERMES_CMD_MONITOR		(0x0038)
#define		HERMES_MONITOR_ENABLE		(0x000b)
#define		HERMES_MONITOR_DISABLE		(0x000f)

/*
 * Configuration RIDs
 */

#define		HERMES_RID_CNF_PORTTYPE		(0xfc00)
#define		HERMES_RID_CNF_MACADDR		(0xfc01)
#define		HERMES_RID_CNF_DESIRED_SSID	(0xfc02)
#define		HERMES_RID_CNF_CHANNEL		(0xfc03)
#define		HERMES_RID_CNF_OWN_SSID		(0xfc04)
#define		HERMES_RID_CNF_SYSTEM_SCALE	(0xfc06)
#define		HERMES_RID_CNF_MAX_DATA_LEN	(0xfc07)
#define		HERMES_RID_CNF_PM_ENABLE	(0xfc09)
#define		HERMES_RID_CNF_PM_MCAST_RX	(0xfc0b)
#define		HERMES_RID_CNF_PM_PERIOD	(0xfc0c)
#define		HERMES_RID_CNF_PM_HOLDOVER	(0xfc0d)
#define		HERMES_RID_CNF_NICKNAME		(0xfc0e)
#define		HERMES_RID_CNF_WEP_ON		(0xfc20)
#define		HERMES_RID_CNF_MWO_ROBUST	(0xfc25)
#define		HERMES_RID_CNF_MULTICAST_LIST	(0xfc80)
#define		HERMES_RID_CNF_CREATEIBSS	(0xfc81)
#define		HERMES_RID_CNF_FRAG_THRESH	(0xfc82)
#define		HERMES_RID_CNF_RTS_THRESH	(0xfc83)
#define		HERMES_RID_CNF_TX_RATE_CTRL	(0xfc84)
#define		HERMES_RID_CNF_PROMISCUOUS	(0xfc85)
#define		HERMES_RID_CNF_KEYS		(0xfcb0)
#define		HERMES_RID_CNF_TX_KEY		(0xfcb1)
#define		HERMES_RID_CNF_TICKTIME		(0xfce0)

#define		HERMES_RID_CNF_PRISM2_WEP_ON	(0xfc28)
#define		HERMES_RID_CNF_PRISM2_TX_KEY	(0xfc23)
#define		HERMES_RID_CNF_PRISM2_KEY0	(0xfc24)
#define		HERMES_RID_CNF_PRISM2_KEY1	(0xfc25)
#define		HERMES_RID_CNF_PRISM2_KEY2	(0xfc26)
#define		HERMES_RID_CNF_PRISM2_KEY3	(0xfc27)
#define		HERMES_RID_CNF_SYMBOL_MANDATORY_BSSID	(0xfc21)
#define		HERMES_RID_CNF_SYMBOL_AUTH_TYPE		(0xfc2A)
#define		HERMES_RID_CNF_SYMBOL_BASIC_RATES	(0xfc8A)
#define		HERMES_RID_CNF_SYMBOL_PREAMBLE		(0xfc8C)

/*
 * Information RIDs
 */
#define		HERMES_RID_CHANNEL_LIST		(0xfd10)
#define		HERMES_RID_STAIDENTITY		(0xfd20)
#define		HERMES_RID_CURRENT_SSID		(0xfd41)
#define		HERMES_RID_CURRENT_BSSID	(0xfd42)
#define		HERMES_RID_COMMSQUALITY		(0xfd43)
#define 	HERMES_RID_CURRENT_TX_RATE	(0xfd44)
#define 	HERMES_RID_SHORT_RETRY_LIMIT	(0xfd48)
#define 	HERMES_RID_LONG_RETRY_LIMIT	(0xfd49)
#define 	HERMES_RID_MAX_TX_LIFETIME	(0xfd4A)
#define		HERMES_RID_WEP_AVAIL		(0xfd4f)
#define		HERMES_RID_CURRENT_CHANNEL	(0xfdc1)
#define		HERMES_RID_DATARATES		(0xfdc6)
#define		HERMES_RID_SYMBOL_PRIMARY_VER	(0xfd03)
#define		HERMES_RID_SYMBOL_SECONDARY_VER	(0xfd21)
#define		HERMES_RID_SYMBOL_KEY_LENGTH	(0xfc2B)

/*
 * Frame structures and constants
 */

#define __PACKED__ __attribute__ ((packed))

typedef struct hermes_frame_desc {
	/* Hermes - i.e. little-endian byte-order */
	uint16_t status __PACKED__;
	uint16_t res1, res2 __PACKED__;
	uint16_t q_info __PACKED__;
	uint16_t res3, res4 __PACKED__;
	uint16_t tx_ctl __PACKED__;
} hermes_frame_desc_t;

#define		HERMES_RXSTAT_ERR		(0x0003)
#define		HERMES_RXSTAT_MACPORT		(0x0700)
#define		HERMES_RXSTAT_MSGTYPE		(0xE000)

#define		HERMES_RXSTAT_BADCRC		(0x0001)
#define		HERMES_RXSTAT_UNDECRYPTABLE	(0x0002)

/* RFC-1042 encoded frame */
#define		HERMES_RXSTAT_1042		(0x2000)
/* Bridge-tunnel encoded frame */
#define		HERMES_RXSTAT_TUNNEL		(0x4000)
/* Wavelan-II Management Protocol frame */
#define		HERMES_RXSTAT_WMP		(0x6000)

#ifdef __KERNEL__

/* Basic control structure */
typedef struct hermes {
	uint iobase;

	uint16_t inten; /* Which interrupts should be enabled? */
} hermes_t;

typedef struct hermes_response {
	uint16_t status, resp0, resp1, resp2;
} hermes_response_t;

/* "ID" structure - used for ESSID and station nickname */
typedef struct hermes_id {
	uint16_t len;
	uint16_t val[16];
} __attribute__ ((packed)) hermes_id_t;

typedef struct hermes_multicast {
	uint8_t addr[HERMES_MAX_MULTICAST][ETH_ALEN];
} __attribute__ ((packed)) hermes_multicast_t;

/* Register access convenience macros */
#define hermes_read_reg(hw, off) (inw((hw)->iobase + (off)))
#define hermes_write_reg(hw, off, val) (outw_p((val), (hw)->iobase + (off)))

#define hermes_read_regn(hw, name) (hermes_read_reg((hw), HERMES_##name))
#define hermes_write_regn(hw, name, val) (hermes_write_reg((hw), HERMES_##name, (val)))

/* Note that for the next two, the count is in 16-bit words, not bytes */
#define hermes_read_data(hw, off, buf, count) (insw((hw)->iobase + (off), (buf), (count)))
#define hermes_write_data(hw, off, buf, count) (outsw((hw)->iobase + (off), (buf), (count)))

/* Function prototypes */
void hermes_struct_init(hermes_t *hw, uint io);
int hermes_reset(hermes_t *hw);
int hermes_docmd_wait(hermes_t *hw, uint16_t cmd, uint16_t parm0, hermes_response_t *resp);
int hermes_allocate(hermes_t *hw, uint16_t size, uint16_t *fid);


int hermes_bap_pread(hermes_t *hw, int bap, void *buf, uint16_t len,
		       uint16_t id, uint16_t offset);
int hermes_bap_pwrite(hermes_t *hw, int bap, const void *buf, uint16_t len,
			uint16_t id, uint16_t offset);
int hermes_read_ltv(hermes_t *hw, int bap, uint16_t rid, int buflen,
		    uint16_t *length, void *buf);
int hermes_write_ltv(hermes_t *hw, int bap, uint16_t rid,
		      uint16_t length, const void *value);

/* Inline functions */

static inline int hermes_present(hermes_t *hw)
{
	return hermes_read_regn(hw, SWSUPPORT0) == HERMES_MAGIC;
}

static inline void hermes_enable_interrupt(hermes_t *hw, uint16_t events)
{
	hw->inten |= events;
	hermes_write_regn(hw, INTEN, hw->inten);
}

static inline void hermes_set_irqmask(hermes_t *hw, uint16_t events)
{
	hw->inten = events;
	hermes_write_regn(hw, INTEN, events);
}

static inline int hermes_enable_port(hermes_t *hw, int port)
{
	hermes_response_t resp;

	return hermes_docmd_wait(hw, HERMES_CMD_ENABLE | (port << 8),
				 0, &resp);
}

static inline int hermes_disable_port(hermes_t *hw, int port)
{
	hermes_response_t resp;

	return hermes_docmd_wait(hw, HERMES_CMD_ENABLE | (port << 8), 
				 0, &resp);
}

#define HERMES_BYTES_TO_RECLEN(n) ( ((n) % 2) ? (((n)+1)/2)+1 : ((n)/2)+1 )
#define HERMES_RECLEN_TO_BYTES(n) ( ((n)-1) * 2 )

#define HERMES_READ_RECORD(hw, bap, rid, buf) \
	(hermes_read_ltv((hw),(bap),(rid), sizeof(*buf), NULL, (buf)))
#define HERMES_WRITE_RECORD(hw, bap, rid, buf) \
	(hermes_write_ltv((hw),(bap),(rid),HERMES_BYTES_TO_RECLEN(sizeof(*buf)),(buf)))

static inline int hermes_read_wordrec(hermes_t *hw, int bap, uint16_t rid, uint16_t *word)
{
	uint16_t rec;
	int err;

	err = HERMES_READ_RECORD(hw, bap, rid, &rec);
	*word = le16_to_cpu(rec);
	return err;
}

static inline int hermes_write_wordrec(hermes_t *hw, int bap, uint16_t rid, uint16_t word)
{
	uint16_t rec = cpu_to_le16(word);
	return HERMES_WRITE_RECORD(hw, bap, rid, &rec);
}

#else /* ! __KERNEL__ */

/* These are provided for the benefit of userspace drivers and testing programs
   which use ioperm() or iopl() */

#define hermes_read_reg(base, off) (inw((base) + (off)))
#define hermes_write_reg(base, off, val) (outw((val), (base) + (off)))

#define hermes_read_regn(base, name) (hermes_read_reg((base), HERMES_##name))
#define hermes_write_regn(base, name, val) (hermes_write_reg((base), HERMES_##name, (val)))

/* Note that for the next two, the count is in 16-bit words, not bytes */
#define hermes_read_data(base, off, buf, count) (insw((base) + (off), (buf), (count)))
#define hermes_write_data(base, off, buf, count) (outsw((base) + (off), (buf), (count)))

#endif /* ! __KERNEL__ */

#endif  /* _HERMES_H */
