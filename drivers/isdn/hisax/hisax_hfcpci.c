/*
 * Driver for HFC PCI based cards
 *
 * Author       Kai Germaschewski
 * Copyright    2002 by Kai Germaschewski  <kai.germaschewski@gmx.de>
 *              2000 by Karsten Keil       <keil@isdn4linux.de>
 *              2000 by Werner Cornelius   <werner@isdn4linux.de>
 * 
 * based upon Werner Cornelius's original hfc_pci.c driver
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 */

// XXX timer3

#include <linux/version.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/kmod.h>
#include <linux/slab.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <asm/delay.h>
#include "hisax_hfcpci.h"

// debugging cruft
#define __debug_variable debug
#include "hisax_debug.h"

#ifdef CONFIG_HISAX_DEBUG
static int debug = 0;
MODULE_PARM(debug, "i");
#endif

MODULE_AUTHOR("Kai Germaschewski <kai.germaschewski@gmx.de>/Werner Cornelius <werner@isdn4linux.de>");
MODULE_DESCRIPTION("HFC PCI ISDN driver");

#define ID(ven, dev, name)                     \
        { .vendor      = PCI_VENDOR_ID_##ven,    \
	  .device      = PCI_DEVICE_ID_##dev,    \
	  .subvendor   = PCI_ANY_ID,             \
	  .subdevice   = PCI_ANY_ID,             \
	  .class       = 0,                      \
          .class_mask  = 0,                      \
	  .driver_data = (unsigned long) name }

static struct pci_device_id hfcpci_ids[] = {
	ID(CCD,     CCD_2BD0,         "CCD/Billion/Asuscom 2BD0"),
	ID(CCD,     CCD_B000,         "Billion B000"),
	ID(CCD,     CCD_B006,         "Billion B006"),
	ID(CCD,     CCD_B007,         "Billion B007"),
	ID(CCD,     CCD_B008,         "Billion B008"),
	ID(CCD,     CCD_B009,         "Billion B009"),
	ID(CCD,     CCD_B00A,         "Billion B00A"),
	ID(CCD,     CCD_B00B,         "Billion B00B"),
	ID(CCD,     CCD_B00C,         "Billion B00C"),
	ID(CCD,     CCD_B100,         "Seyeon"),
	ID(ABOCOM,  ABOCOM_2BD1,      "Abocom/Magitek"),
	ID(ASUSTEK, ASUSTEK_0675,     "Asuscom/Askey"),
	ID(BERKOM,  BERKOM_T_CONCEPT, "German Telekom T-Concept"),
	ID(BERKOM,  BERKOM_A1T,       "German Telekom A1T"),
	ID(ANIGMA,  ANIGMA_MC145575,  "Motorola MC145575"),
	ID(ZOLTRIX, ZOLTRIX_2BD0,     "Zoltrix 2BD0"),
	ID(DIGI,    DIGI_DF_M_IOM2_E, "Digi DataFire Micro V IOM2 (Europe)"),
	ID(DIGI,    DIGI_DF_M_E,      "Digi DataFire Micro V (Europe)"),
	ID(DIGI,    DIGI_DF_M_IOM2_A, "Digi DataFire Micro V IOM2 (America)"),
	ID(DIGI,    DIGI_DF_M_A,      "Digi DataFire Micro V (America)"),
	{ } 
};
MODULE_DEVICE_TABLE(pci, hfcpci_ids);

#undef ID

static int protocol = 2;       /* EURO-ISDN Default */
MODULE_PARM(protocol, "i");

// ----------------------------------------------------------------------
//

#define DBG_WARN      0x0001
#define DBG_INFO      0x0002
#define DBG_IRQ       0x0010
#define DBG_L1M       0x0020
#define DBG_PR        0x0040
#define DBG_D_XMIT    0x0100
#define DBG_D_RECV    0x0200
#define DBG_B_XMIT    0x1000
#define DBG_B_RECV    0x2000

/* memory window base address offset (in config space) */

#define HFCPCI_MWBA      0x80

/* GCI/IOM bus monitor registers */

#define HCFPCI_C_I       0x08
#define HFCPCI_TRxR      0x0C
#define HFCPCI_MON1_D    0x28
#define HFCPCI_MON2_D    0x2C


/* GCI/IOM bus timeslot registers */

#define HFCPCI_B1_SSL    0x80
#define HFCPCI_B2_SSL    0x84
#define HFCPCI_AUX1_SSL  0x88
#define HFCPCI_AUX2_SSL  0x8C
#define HFCPCI_B1_RSL    0x90
#define HFCPCI_B2_RSL    0x94
#define HFCPCI_AUX1_RSL  0x98
#define HFCPCI_AUX2_RSL  0x9C

/* GCI/IOM bus data registers */

#define HFCPCI_B1_D      0xA0
#define HFCPCI_B2_D      0xA4
#define HFCPCI_AUX1_D    0xA8
#define HFCPCI_AUX2_D    0xAC

/* GCI/IOM bus configuration registers */

#define HFCPCI_MST_EMOD  0xB4
#define HFCPCI_MST_MODE	 0xB8
#define HFCPCI_CONNECT 	 0xBC


/* Interrupt and status registers */

#define HFCPCI_FIFO_EN   0x44
#define HFCPCI_TRM       0x48
#define HFCPCI_B_MODE    0x4C
#define HFCPCI_CHIP_ID   0x58
#define HFCPCI_CIRM  	 0x60
#define HFCPCI_CTMT	 0x64
#define HFCPCI_INT_M1  	 0x68
#define HFCPCI_INT_M2  	 0x6C
#define HFCPCI_INT_S1  	 0x78
#define HFCPCI_INT_S2  	 0x7C
#define HFCPCI_STATUS  	 0x70

/* S/T section registers */

#define HFCPCI_STATES  	 0xC0
#define HFCPCI_SCTRL  	 0xC4
#define HFCPCI_SCTRL_E   0xC8
#define HFCPCI_SCTRL_R   0xCC
#define HFCPCI_SQ  	 0xD0
#define HFCPCI_CLKDEL  	 0xDC
#define HFCPCI_B1_REC    0xF0
#define HFCPCI_B1_SEND   0xF0
#define HFCPCI_B2_REC    0xF4
#define HFCPCI_B2_SEND   0xF4
#define HFCPCI_D_REC     0xF8
#define HFCPCI_D_SEND    0xF8
#define HFCPCI_E_REC     0xFC


/* bits in status register (READ) */
#define HFCPCI_PCI_PROC   0x02
#define HFCPCI_NBUSY	  0x04 
#define HFCPCI_TIMER_ELAP 0x10
#define HFCPCI_STATINT	  0x20
#define HFCPCI_FRAMEINT	  0x40
#define HFCPCI_ANYINT	  0x80

/* bits in CTMT (Write) */
#define HFCPCI_CLTIMER    0x80
#define HFCPCI_TIM3_125   0x04
#define HFCPCI_TIM25      0x10
#define HFCPCI_TIM50      0x14
#define HFCPCI_TIM400     0x18
#define HFCPCI_TIM800     0x1C
#define HFCPCI_AUTO_TIMER 0x20
#define HFCPCI_TRANSB2    0x02
#define HFCPCI_TRANSB1    0x01

/* bits in CIRM (Write) */
#define HFCPCI_AUX_MSK    0x07
#define HFCPCI_RESET  	  0x08
#define HFCPCI_B1_REV     0x40
#define HFCPCI_B2_REV     0x80

/* bits in INT_M1 and INT_S1 */
#define HFCPCI_INTS_B1TRANS  0x01
#define HFCPCI_INTS_B2TRANS  0x02
#define HFCPCI_INTS_DTRANS   0x04
#define HFCPCI_INTS_B1REC    0x08
#define HFCPCI_INTS_B2REC    0x10
#define HFCPCI_INTS_DREC     0x20
#define HFCPCI_INTS_L1STATE  0x40
#define HFCPCI_INTS_TIMER    0x80

/* bits in INT_M2 */
#define HFCPCI_PROC_TRANS    0x01
#define HFCPCI_GCI_I_CHG     0x02
#define HFCPCI_GCI_MON_REC   0x04
#define HFCPCI_IRQ_ENABLE    0x08
#define HFCPCI_PMESEL        0x80

/* bits in STATES */
#define HFCPCI_STATE_MSK     0x0F
#define HFCPCI_LOAD_STATE    0x10
#define HFCPCI_ACTIVATE	     0x20
#define HFCPCI_DO_ACTION     0x40
#define HFCPCI_NT_G2_G3      0x80

/* bits in HFCD_MST_MODE */
#define HFCPCI_MASTER	     0x01
#define HFCPCI_SLAVE         0x00
/* remaining bits are for codecs control */

/* bits in HFCD_SCTRL */
#define SCTRL_B1_ENA	     0x01
#define SCTRL_B2_ENA	     0x02
#define SCTRL_MODE_TE        0x00
#define SCTRL_MODE_NT        0x04
#define SCTRL_LOW_PRIO	     0x08
#define SCTRL_SQ_ENA	     0x10
#define SCTRL_TEST	     0x20
#define SCTRL_NONE_CAP	     0x40
#define SCTRL_PWR_DOWN	     0x80

/* bits in SCTRL_E  */
#define HFCPCI_AUTO_AWAKE    0x01
#define HFCPCI_DBIT_1        0x04
#define HFCPCI_IGNORE_COL    0x08
#define HFCPCI_CHG_B1_B2     0x80

/* bits in FIFO_EN register */
#define HFCPCI_FIFOEN_B1     0x03
#define HFCPCI_FIFOEN_B2     0x0C
#define HFCPCI_FIFOEN_DTX    0x10
#define HFCPCI_FIFOEN_DRX    0x20
#define HFCPCI_FIFOEN_B1TX   0x01
#define HFCPCI_FIFOEN_B1RX   0x02
#define HFCPCI_FIFOEN_B2TX   0x04
#define HFCPCI_FIFOEN_B2RX   0x08

/*
 * thresholds for transparent B-channel mode
 * change mask and threshold simultaneously
 */
#define HFCPCI_BTRANS_THRESHOLD 128
#define HFCPCI_BTRANS_THRESMASK 0x00

#define CLKDEL_TE	0x0e	/* CLKDEL in TE mode */
#define CLKDEL_NT	0x6c	/* CLKDEL in NT mode */

#define MAX_D_FRAMES 0x10
#define MAX_B_FRAMES 0x20
#define B_FIFO_START 0x0200
#define B_FIFO_END   0x2000
#define B_FIFO_SIZE  (B_FIFO_END - B_FIFO_START)
#define D_FIFO_START 0x0000
#define D_FIFO_END   0x0200
#define D_FIFO_SIZE  (D_FIFO_END - D_FIFO_START)

// ----------------------------------------------------------------------
// push messages to the upper layers

static inline void D_L1L2(struct hfcpci_adapter *adapter, int pr, void *arg)
{
	struct hisax_if *ifc = (struct hisax_if *) &adapter->d_if;

	DBG(DBG_PR, "pr %#x", pr);
	ifc->l1l2(ifc, pr, arg);
}

static inline void B_L1L2(struct hfcpci_bcs *bcs, int pr, void *arg)
{
	struct hisax_if *ifc = (struct hisax_if *) &bcs->b_if;

	DBG(DBG_PR, "pr %#x", pr);
	ifc->l1l2(ifc, pr, arg);
}

// ----------------------------------------------------------------------
// MMIO

static inline void
hfcpci_writeb(struct hfcpci_adapter *adapter, u8 b, unsigned char offset)
{
	writeb(b, adapter->mmio + offset);
}

static inline u8
hfcpci_readb(struct hfcpci_adapter *adapter, unsigned char offset)
{
	return readb(adapter->mmio + offset);
}

// ----------------------------------------------------------------------
// magic to define the various F/Z counter accesses

#define DECL_B_F(r, f)                                                      \
static inline u8                                                            \
get_b_##r##_##f (struct hfcpci_bcs *bcs)                                    \
{                                                                           \
	u16 off = bcs->channel ? OFF_B2_##r##_##f : OFF_B1_##r##_##f;       \
                                                                            \
	return *(bcs->adapter->fifo + off);                                 \
}                                                                           \
                                                                            \
static inline void                                                          \
set_b_##r##_##f (struct hfcpci_bcs *bcs, u8 f)                              \
{                                                                           \
	u16 off = bcs->channel ? OFF_B2_##r##_##f : OFF_B1_##r##_##f;       \
                                                                            \
	*(bcs->adapter->fifo + off) = f;                                    \
}

#define OFF_B1_rx_f1 0x6080
#define OFF_B2_rx_f1 0x6180
#define OFF_B1_rx_f2 0x6081
#define OFF_B2_rx_f2 0x6181

#define OFF_B1_tx_f1 0x2080
#define OFF_B2_tx_f1 0x2180
#define OFF_B1_tx_f2 0x2081
#define OFF_B2_tx_f2 0x2181

DECL_B_F(rx, f1)
DECL_B_F(rx, f2)
DECL_B_F(tx, f1)
DECL_B_F(tx, f2)

#undef DECL_B_F

#define DECL_B_Z(r, z)                                                      \
static inline u16                                                           \
get_b_##r##_##z (struct hfcpci_bcs *bcs, u8 f)                              \
{                                                                           \
	u16 off = bcs->channel ? OFF_B2_##r##_##z : OFF_B1_##r##_##z;       \
                                                                            \
	return le16_to_cpu(*((u16 *) (bcs->adapter->fifo + off + f * 4)));  \
}                                                                           \
                                                                            \
static inline void                                                          \
set_b_##r##_##z(struct hfcpci_bcs *bcs, u8 f, u16 z)                        \
{                                                                           \
	u16 off = bcs->channel ? OFF_B2_##r##_##z : OFF_B1_##r##_##z;       \
                                                                            \
	*((u16 *) (bcs->adapter->fifo + off + f * 4)) = cpu_to_le16(z);     \
}

#define OFF_B1_rx_z1 0x6000
#define OFF_B2_rx_z1 0x6100
#define OFF_B1_rx_z2 0x6002
#define OFF_B2_rx_z2 0x6102

#define OFF_B1_tx_z1 0x2000
#define OFF_B2_tx_z1 0x2100
#define OFF_B1_tx_z2 0x2002
#define OFF_B2_tx_z2 0x2102

DECL_B_Z(rx, z1)
DECL_B_Z(rx, z2)
DECL_B_Z(tx, z1)
DECL_B_Z(tx, z2)

#undef DECL_B_Z

#define DECL_D_F(r, f)                                                      \
static inline u8                                                            \
get_d_##r##_##f (struct hfcpci_adapter *adapter)                            \
{                                                                           \
	u16 off = OFF_D_##r##_##f;                                          \
                                                                            \
	return *(adapter->fifo + off) & 0xf;                                \
}                                                                           \
                                                                            \
static inline void                                                          \
set_d_##r##_##f (struct hfcpci_adapter *adapter, u8 f)                      \
{                                                                           \
	u16 off = OFF_D_##r##_##f;                                          \
                                                                            \
	*(adapter->fifo + off) = f | 0x10;                                  \
}

#define OFF_D_rx_f1 0x60a0
#define OFF_D_rx_f2 0x60a1

#define OFF_D_tx_f1 0x20a0
#define OFF_D_tx_f2 0x20a1

DECL_D_F(rx, f1)
DECL_D_F(rx, f2)
DECL_D_F(tx, f1)
DECL_D_F(tx, f2)

#undef DECL_D_F

#define DECL_D_Z(r, z)                                                      \
static inline u16                                                           \
get_d_##r##_##z (struct hfcpci_adapter *adapter, u8 f)                      \
{                                                                           \
	u16 off = OFF_D_##r##_##z;                                          \
                                                                            \
	return le16_to_cpu(*((u16 *) (adapter->fifo + off + (f | 0x10) * 4)));\
}                                                                           \
                                                                            \
static inline void                                                          \
set_d_##r##_##z(struct hfcpci_adapter *adapter, u8 f, u16 z)                \
{                                                                           \
	u16 off = OFF_D_##r##_##z;                                          \
                                                                            \
	*((u16 *) (adapter->fifo + off + (f | 0x10) * 4)) = cpu_to_le16(z); \
}

#define OFF_D_rx_z1 0x6080
#define OFF_D_rx_z2 0x6082

#define OFF_D_tx_z1 0x2080
#define OFF_D_tx_z2 0x2082

DECL_D_Z(rx, z1)
DECL_D_Z(rx, z2)
DECL_D_Z(tx, z1)
DECL_D_Z(tx, z2)

#undef DECL_B_Z

// ----------------------------------------------------------------------
// fill b / d fifos

static inline void
hfcpci_fill_d_fifo(struct hfcpci_adapter *adapter)
{
	u8 f1, f2;
	u16 z1, z2;
	int cnt, fcnt;
	char *fifo_adr = adapter->fifo;
	struct sk_buff *tx_skb = adapter->tx_skb;
	
	f1 = get_d_tx_f1(adapter);
	f2 = get_d_tx_f2(adapter);
	DBG(DBG_D_XMIT, "f1 %#x f2 %#x", f1, f2);

	fcnt = f1 - f2;
	if (fcnt < 0)
		fcnt += MAX_D_FRAMES;
	
	if (fcnt) {
		printk("BUG\n");
		return;
	}

	z1 = get_d_tx_z1(adapter, f1);
	z2 = get_d_tx_z2(adapter, f1); //XXX
	DBG(DBG_D_XMIT, "z1 %#x z2 %#x", z1, z2);

	cnt = z2 - z1;
	if (cnt <= 0)
		cnt += D_FIFO_SIZE;

	if (tx_skb->len > cnt) {
		printk("BUG\n");
		return;
	}

	cnt = tx_skb->len;
	if (z1 + cnt <= D_FIFO_END) {
		memcpy(fifo_adr + z1, tx_skb->data, cnt);
	} else {
		memcpy(fifo_adr + z1, tx_skb->data, D_FIFO_END - z1);
		memcpy(fifo_adr + D_FIFO_START, 
		       tx_skb->data + (D_FIFO_END - z1), 
		       cnt - (D_FIFO_END - z1));
	}
	z1 += cnt;
	if (z1 >= D_FIFO_END)
		z1 -= D_FIFO_SIZE;

	f1 = (f1 + 1) & (MAX_D_FRAMES - 1);
	mb();
	set_d_tx_z1(adapter, f1, z1);
	mb();
	set_d_tx_f1(adapter, f1);
}

static inline void
hfcpci_fill_b_fifo_hdlc(struct hfcpci_bcs *bcs)
{
	u8 f1, f2;
	u16 z1, z2;
	int cnt, fcnt;
	char *fifo_adr = bcs->adapter->fifo + (bcs->channel ? 0x2000 : 0x0000);
	struct sk_buff *tx_skb = bcs->tx_skb;
	
	f1 = get_b_tx_f1(bcs);
	f2 = get_b_tx_f2(bcs);
	DBG(DBG_B_XMIT, "f1 %#x f2 %#x", f1, f2);

	fcnt = f1 - f2;
	if (fcnt < 0)
		fcnt += MAX_B_FRAMES;
	
	if (fcnt) {
		printk("BUG\n");
		return;
	}

	z1 = get_b_tx_z1(bcs, f1);
	z2 = get_b_tx_z2(bcs, f1); //XXX
	DBG(DBG_B_XMIT, "z1 %#x z2 %#x", z1, z2);

	cnt = z2 - z1;
	if (cnt <= 0)
		cnt += B_FIFO_SIZE;

	if (tx_skb->len > cnt) {
		printk("BUG\n");
		return;
	}

	cnt = tx_skb->len;
	if (z1 + cnt <= B_FIFO_END) {
		memcpy(fifo_adr + z1, tx_skb->data, cnt);
	} else {
		memcpy(fifo_adr + z1, tx_skb->data, B_FIFO_END - z1);
		memcpy(fifo_adr + B_FIFO_START,
		       tx_skb->data + (B_FIFO_END - z1), 
		       cnt - (B_FIFO_END - z1));
	}
	z1 += cnt;
	if (z1 >= B_FIFO_END)
		z1 -= B_FIFO_SIZE;

	f1 = (f1 + 1) & (MAX_B_FRAMES - 1);
	mb();
	set_b_tx_z1(bcs, f1, z1);
	mb();
	set_b_tx_f1(bcs, f1);
}

static inline void
hfcpci_fill_b_fifo_trans(struct hfcpci_bcs *bcs)
{
	int cnt;
	char *fifo_adr = bcs->adapter->fifo + (bcs->channel ? 0x2000 : 0x0000);
	struct sk_buff *tx_skb = bcs->tx_skb;
	u8 f1, f2;
	u16 z1, z2;

	f1 = get_b_tx_f1(bcs);
	f2 = get_b_tx_f2(bcs);

	if (f1 != f2)
		BUG();

	z1 = get_b_tx_z1(bcs, f1);
	z2 = get_b_tx_z2(bcs, f1);

	cnt = z2 - z1;
	if (cnt <= 0)
		cnt += B_FIFO_SIZE;

	if (tx_skb->len > cnt)
		BUG();

	if (z1 + cnt <= B_FIFO_END) {
		memcpy(fifo_adr + z1, tx_skb->data, cnt);
	} else {
		memcpy(fifo_adr + z1, tx_skb->data, B_FIFO_END - z1);
		memcpy(fifo_adr + B_FIFO_START,
		       tx_skb->data + (B_FIFO_END - z1), 
		       cnt - (B_FIFO_END - z1));
	}
	z1 += cnt;
	if (z1 >= B_FIFO_END)
		z1 -= B_FIFO_SIZE;

	mb();
	set_b_tx_z1(bcs, f1, z1);
}

static inline void
hfcpci_fill_b_fifo(struct hfcpci_bcs *bcs)
{
	if (!bcs->tx_skb) {
		DBG(DBG_WARN, "?");
		return;
	}
	
	switch (bcs->mode) {
	case L1_MODE_TRANS:
		hfcpci_fill_b_fifo_trans(bcs);
		break;
	case L1_MODE_HDLC:
		hfcpci_fill_b_fifo_hdlc(bcs);
		break;
	default:
		DBG(DBG_WARN, "?");
	}
}

static void hfcpci_clear_b_rx_fifo(struct hfcpci_bcs *bcs);
static void hfcpci_clear_b_tx_fifo(struct hfcpci_bcs *bcs);

static void
hfcpci_b_mode(struct hfcpci_bcs *bcs, int mode)
{
	struct hfcpci_adapter *adapter = bcs->adapter;
	
	DBG(DBG_B_XMIT, "B%d mode %d --> %d",
	    bcs->channel + 1, bcs->mode, mode);

	if (bcs->mode == mode)
		return;

	switch (mode) {
	case L1_MODE_NULL:
		if (bcs->channel == 0) {
			adapter->sctrl &= ~SCTRL_B1_ENA;
			adapter->sctrl_r &= ~SCTRL_B1_ENA;
			adapter->fifo_en &= ~HFCPCI_FIFOEN_B1;
			adapter->int_m1 &= ~(HFCPCI_INTS_B1TRANS + HFCPCI_INTS_B1REC);
		} else {
			adapter->sctrl &= ~SCTRL_B2_ENA;
			adapter->sctrl_r &= ~SCTRL_B2_ENA;
			adapter->fifo_en &= ~HFCPCI_FIFOEN_B2;
			adapter->int_m1 &= ~(HFCPCI_INTS_B2TRANS + HFCPCI_INTS_B2REC);
		}
		break;
	case L1_MODE_TRANS:
	case L1_MODE_HDLC:
		hfcpci_clear_b_rx_fifo(bcs);
		hfcpci_clear_b_tx_fifo(bcs);
		if (bcs->channel == 0) {
			adapter->sctrl |= SCTRL_B1_ENA;
			adapter->sctrl_r |= SCTRL_B1_ENA;
			adapter->fifo_en |= HFCPCI_FIFOEN_B1;
			adapter->int_m1 |= (HFCPCI_INTS_B1TRANS + HFCPCI_INTS_B1REC);

			if (mode == L1_MODE_TRANS)
				adapter->ctmt |= 1;
			else
				adapter->ctmt &= ~1;

		} else {
			adapter->sctrl |= SCTRL_B2_ENA;
			adapter->sctrl_r |= SCTRL_B2_ENA;
			adapter->fifo_en |= HFCPCI_FIFOEN_B2;
			adapter->int_m1 |= (HFCPCI_INTS_B2TRANS + HFCPCI_INTS_B2REC);

			if (mode == L1_MODE_TRANS)
				adapter->ctmt |= 2;
			else
				adapter->ctmt &= ~2;

		}
		break;
	}
	hfcpci_writeb(adapter, adapter->int_m1,  HFCPCI_INT_M1);
	hfcpci_writeb(adapter, adapter->fifo_en, HFCPCI_FIFO_EN);
	hfcpci_writeb(adapter, adapter->sctrl,   HFCPCI_SCTRL);
	hfcpci_writeb(adapter, adapter->sctrl_r, HFCPCI_SCTRL_R);
	hfcpci_writeb(adapter, adapter->ctmt,    HFCPCI_CTMT);
	hfcpci_writeb(adapter, adapter->conn,    HFCPCI_CONNECT);

	bcs->mode = mode;
}

// ----------------------------------------------------------------------
// Layer 1 state machine

static struct Fsm l1fsm;

enum {
	ST_L1_F0,
	ST_L1_F2,
	ST_L1_F3,
	ST_L1_F4,
	ST_L1_F5,
	ST_L1_F6,
	ST_L1_F7,
	ST_L1_F8,
};

#define L1_STATE_COUNT (ST_L1_F8+1)

static char *strL1State[] =
{
	"ST_L1_F0",
	"ST_L1_F2",
	"ST_L1_F3",
	"ST_L1_F4",
	"ST_L1_F5",
	"ST_L1_F6",
	"ST_L1_F7",
	"ST_L1_F8",
};

enum {
	EV_PH_F0,
	EV_PH_1,
	EV_PH_F2,
	EV_PH_F3,
	EV_PH_F4,
	EV_PH_F5,
	EV_PH_F6,
	EV_PH_F7,
	EV_PH_F8,
	EV_PH_ACTIVATE_REQ,
	EV_PH_DEACTIVATE_REQ,
	EV_TIMER3,
};

#define L1_EVENT_COUNT (EV_TIMER3 + 1)

static char *strL1Event[] =
{
	"EV_PH_F0",
	"EV_PH_1",
	"EV_PH_F2",
	"EV_PH_F3",
	"EV_PH_F4",
	"EV_PH_F5",
	"EV_PH_F6",
	"EV_PH_F7",
	"EV_PH_F8",
	"EV_PH_ACTIVATE_REQ",
	"EV_PH_DEACTIVATE_REQ",
	"EV_TIMER3",
};

static void l1_ignore(struct FsmInst *fi, int event, void *arg)
{
}

static void l1_go_f3(struct FsmInst *fi, int event, void *arg)
{
	FsmChangeState(fi, ST_L1_F3);
}

static void l1_go_f3_deact_ind(struct FsmInst *fi, int event, void *arg)
{
	struct hfcpci_adapter *adapter = fi->userdata;

	FsmChangeState(fi, ST_L1_F3);
	D_L1L2(adapter, PH_DEACTIVATE | INDICATION, NULL);
}

static void l1_go_f4(struct FsmInst *fi, int event, void *arg)
{
	FsmChangeState(fi, ST_L1_F3);
}

static void l1_go_f5(struct FsmInst *fi, int event, void *arg)
{
	FsmChangeState(fi, ST_L1_F3);
}

static void l1_go_f6(struct FsmInst *fi, int event, void *arg)
{
	FsmChangeState(fi, ST_L1_F6);
}

static void l1_go_f6_deact_ind(struct FsmInst *fi, int event, void *arg)
{
	struct hfcpci_adapter *adapter = fi->userdata;

	FsmChangeState(fi, ST_L1_F6);
	D_L1L2(adapter, PH_DEACTIVATE | INDICATION, NULL);
}

static void l1_go_f7(struct FsmInst *fi, int event, void *arg)
{
	FsmChangeState(fi, ST_L1_F7);
}

static void l1_go_f7_act_ind(struct FsmInst *fi, int event, void *arg)
{
	struct hfcpci_adapter *adapter = fi->userdata;

	FsmChangeState(fi, ST_L1_F7);
	D_L1L2(adapter, PH_ACTIVATE | INDICATION, NULL);
}

static void l1_go_f8(struct FsmInst *fi, int event, void *arg)
{
	FsmChangeState(fi, ST_L1_F8);
}

static void l1_go_f8_deact_ind(struct FsmInst *fi, int event, void *arg)
{
	struct hfcpci_adapter *adapter = fi->userdata;

	FsmChangeState(fi, ST_L1_F8);
	D_L1L2(adapter, PH_DEACTIVATE | INDICATION, NULL);
}

static void l1_act_req(struct FsmInst *fi, int event, void *arg)
{
	struct hfcpci_adapter *adapter = fi->userdata;

	hfcpci_writeb(adapter, HFCPCI_ACTIVATE | HFCPCI_DO_ACTION, HFCPCI_STATES);
}

static struct FsmNode L1FnList[] __initdata =
{
	{ST_L1_F2,            EV_PH_F3,             l1_go_f3},
	{ST_L1_F2,            EV_PH_F6,             l1_go_f6},
	{ST_L1_F2,            EV_PH_F7,             l1_go_f7_act_ind},

	{ST_L1_F3,            EV_PH_F3,             l1_ignore},
	{ST_L1_F3,            EV_PH_F4,             l1_go_f4},
	{ST_L1_F3,            EV_PH_F5,             l1_go_f5},
	{ST_L1_F3,            EV_PH_F6,             l1_go_f6},
	{ST_L1_F3,            EV_PH_F7,             l1_go_f7_act_ind},
	{ST_L1_F3,            EV_PH_ACTIVATE_REQ,   l1_act_req},

	{ST_L1_F4,            EV_PH_F7,             l1_ignore},
	{ST_L1_F4,            EV_PH_F3,             l1_go_f3},
	{ST_L1_F4,            EV_PH_F5,             l1_go_f5},
	{ST_L1_F4,            EV_PH_F6,             l1_go_f6},
	{ST_L1_F4,            EV_PH_F7,             l1_go_f7},

	{ST_L1_F5,            EV_PH_F7,             l1_ignore},
	{ST_L1_F5,            EV_PH_F3,             l1_go_f3},
	{ST_L1_F5,            EV_PH_F6,             l1_go_f6},
	{ST_L1_F5,            EV_PH_F7,             l1_go_f7},

	{ST_L1_F6,            EV_PH_F7,             l1_ignore},
	{ST_L1_F6,            EV_PH_F3,             l1_go_f3},
	{ST_L1_F6,            EV_PH_F7,             l1_go_f7_act_ind},
	{ST_L1_F6,            EV_PH_F8,             l1_go_f8},

	{ST_L1_F7,            EV_PH_F7,             l1_ignore},
	{ST_L1_F7,            EV_PH_F3,             l1_go_f3_deact_ind},
	{ST_L1_F7,            EV_PH_F6,             l1_go_f6_deact_ind},
	{ST_L1_F7,            EV_PH_F8,             l1_go_f8_deact_ind},

	{ST_L1_F8,            EV_PH_F7,             l1_ignore},
	{ST_L1_F8,            EV_PH_F3,             l1_go_f3},
	{ST_L1_F8,            EV_PH_F6,             l1_go_f6},
	{ST_L1_F8,            EV_PH_F7,             l1_go_f7_act_ind},

};

static void l1m_debug(struct FsmInst *fi, char *fmt, ...)
{
	va_list args;
	char buf[256];
	
	va_start(args, fmt);
	vsprintf(buf, fmt, args);
	DBG(DBG_L1M, "%s", buf);
	va_end(args);
}

// ----------------------------------------------------------------------
// clear FIFOs

static void
hfcpci_clear_d_rx_fifo(struct hfcpci_adapter *adapter)
{
	u8 fifo_state;

	DBG(DBG_D_RECV, "");

	fifo_state = adapter->fifo_en & HFCPCI_FIFOEN_DRX;

	if (fifo_state) { // enabled
		// XXX locking
	        adapter->fifo_en &= ~fifo_state;
		hfcpci_writeb(adapter, adapter->fifo_en, HFCPCI_FIFO_EN);
	}
	
	adapter->last_fcnt = 0;

	set_d_rx_z1(adapter, MAX_D_FRAMES - 1, D_FIFO_END - 1);
	set_d_rx_z2(adapter, MAX_D_FRAMES - 1, D_FIFO_END - 1);
	mb();
	set_d_rx_f1(adapter, MAX_D_FRAMES - 1);
	set_d_rx_f2(adapter, MAX_D_FRAMES - 1);
	mb();
	
	if (fifo_state) {
	        adapter->fifo_en |= fifo_state;
		hfcpci_writeb(adapter, adapter->fifo_en, HFCPCI_FIFO_EN);
	}
}   

static void
hfcpci_clear_b_rx_fifo(struct hfcpci_bcs *bcs)
{
	struct hfcpci_adapter *adapter = bcs->adapter;
	int nr = bcs->channel;
	u8 fifo_state;

	DBG(DBG_B_RECV, "");

	fifo_state = adapter->fifo_en & 
		(nr ? HFCPCI_FIFOEN_B2RX : HFCPCI_FIFOEN_B1RX);

	if (fifo_state) { // enabled
	        adapter->fifo_en &= ~fifo_state;
		hfcpci_writeb(adapter, adapter->fifo_en, HFCPCI_FIFO_EN);
	}
	
	bcs->last_fcnt = 0;

	set_b_rx_z1(bcs, MAX_B_FRAMES - 1, B_FIFO_END - 1);
	set_b_rx_z2(bcs, MAX_B_FRAMES - 1, B_FIFO_END - 1);
	mb();
	set_b_rx_f1(bcs, MAX_B_FRAMES - 1);
	set_b_rx_f2(bcs, MAX_B_FRAMES - 1);
	mb();
	
	if (fifo_state) {
	        adapter->fifo_en |= fifo_state;
		hfcpci_writeb(adapter, adapter->fifo_en, HFCPCI_FIFO_EN);
	}
}   

// XXX clear d_tx_fifo?

static void
hfcpci_clear_b_tx_fifo(struct hfcpci_bcs *bcs)
{
	struct hfcpci_adapter *adapter = bcs->adapter;
	int nr = bcs->channel;
	u8 fifo_state;

	fifo_state = adapter->fifo_en & 
		(nr ? HFCPCI_FIFOEN_B2TX : HFCPCI_FIFOEN_B1TX);

	if (fifo_state) { // enabled
	        adapter->fifo_en &= ~fifo_state;
		hfcpci_writeb(adapter, adapter->fifo_en, HFCPCI_FIFO_EN);
	}
	
	bcs->last_fcnt = 0;

	set_b_rx_z1(bcs, MAX_B_FRAMES - 1, B_FIFO_END - 1);
	set_b_rx_z2(bcs, MAX_B_FRAMES - 1, B_FIFO_END - 1);
	mb();
	set_b_rx_f1(bcs, MAX_B_FRAMES - 1);
	set_b_rx_f2(bcs, MAX_B_FRAMES - 1);
	mb();
	
	if (fifo_state) {
	        adapter->fifo_en |= fifo_state;
		hfcpci_writeb(adapter, adapter->fifo_en, HFCPCI_FIFO_EN);
	}
}   

// ----------------------------------------------------------------------
// receive messages from upper layers

static void
hfcpci_d_l2l1(struct hisax_if *ifc, int pr, void *arg)
{
	struct hfcpci_adapter *adapter = ifc->priv;
	struct sk_buff *skb = arg;

	DBG(DBG_PR, "pr %#x", pr);

	switch (pr) {
	case PH_ACTIVATE | REQUEST:
		FsmEvent(&adapter->l1m, EV_PH_ACTIVATE_REQ, NULL);
		break;
	case PH_DEACTIVATE | REQUEST:
		FsmEvent(&adapter->l1m, EV_PH_DEACTIVATE_REQ, NULL);
		break;
	case PH_DATA | REQUEST:
		DBG(DBG_PR, "PH_DATA REQUEST len %d", skb->len);
		DBG_SKB(DBG_D_XMIT, skb);
		if (adapter->l1m.state != ST_L1_F7) {
			DBG(DBG_WARN, "L1 wrong state %d", adapter->l1m.state);
			break;
		}
		if (adapter->tx_skb)
			BUG();

		adapter->tx_skb = skb;
		hfcpci_fill_d_fifo(adapter);
		break;
	}
}

static void
hfcpci_b_l2l1(struct hisax_if *ifc, int pr, void *arg)
{
	struct hfcpci_bcs *bcs = ifc->priv;
	struct sk_buff *skb = arg;
	int mode;

	DBG(DBG_PR, "pr %#x", pr);

	switch (pr) {
	case PH_DATA | REQUEST:
		if (bcs->tx_skb)
			BUG();
		
		bcs->tx_skb = skb;
		DBG_SKB(DBG_B_XMIT, skb);
		hfcpci_fill_b_fifo(bcs);
		break;
	case PH_ACTIVATE | REQUEST:
		mode = (int) arg;
		DBG(DBG_PR,"B%d,PH_ACTIVATE_REQUEST %d", bcs->channel + 1, mode);
		hfcpci_b_mode(bcs, mode);
		B_L1L2(bcs, PH_ACTIVATE | INDICATION, NULL);
		break;
	case PH_DEACTIVATE | REQUEST:
		DBG(DBG_PR,"B%d,PH_DEACTIVATE_REQUEST", bcs->channel + 1);
		hfcpci_b_mode(bcs, L1_MODE_NULL);
		B_L1L2(bcs, PH_DEACTIVATE | INDICATION, NULL);
		break;
	}
}

// ----------------------------------------------------------------------
// receive IRQ

static inline void
hfcpci_d_recv_irq(struct hfcpci_adapter *adapter)
{
	struct sk_buff *skb;
	char *fifo_adr = adapter->fifo + 0x4000;
	char *p;
	int cnt, fcnt;
	int loop = 5;
	u8 f1, f2;
	u16 z1, z2;

	while (loop-- > 0) {
		f1 = get_d_rx_f1(adapter);
		f2 = get_d_rx_f2(adapter);
		DBG(DBG_D_RECV, "f1 %#x f2 %#x", f1, f2);
		
		fcnt = f1 - f2;
		if (fcnt < 0)
			fcnt += 16;

		if (!fcnt)
			return;
		
		if (fcnt < adapter->last_fcnt)
			/* overrun */
			hfcpci_clear_d_rx_fifo(adapter);
			// XXX init last_fcnt

		z1 = get_d_rx_z1(adapter, f2);
		z2 = get_d_rx_z2(adapter, f2);
		DBG(DBG_D_RECV, "z1 %#x z2 %#x", z1, z2);

		cnt = z1 - z2;
		if (cnt < 0)
			cnt += D_FIFO_SIZE;
		cnt++;
		
		if (cnt < 4) {
			DBG(DBG_WARN, "frame too short");
			goto next;
		}
		if (fifo_adr[z1] != 0) {
			DBG(DBG_WARN, "CRC error");
			goto next;
		}
		cnt -= 3;
		skb = dev_alloc_skb(cnt);
		if (!skb) {
			DBG(DBG_WARN, "no mem");
			goto next;
		}
		p = skb_put(skb, cnt);
		if (z2 + cnt <= D_FIFO_END) {
			memcpy(p, fifo_adr + z2, cnt);
		} else {
			memcpy(p, fifo_adr + z2, D_FIFO_END - z2);
			memcpy(p + (D_FIFO_END - z2), fifo_adr + D_FIFO_START,
			       cnt - (D_FIFO_END - z2));
		}

		DBG_SKB(DBG_D_RECV, skb);
		D_L1L2(adapter, PH_DATA | INDICATION, skb);
	
	next:
		if (++z1 >= D_FIFO_END)
			z1 -= D_FIFO_START;

		f2 = (f2 + 1) & (MAX_D_FRAMES - 1);
		mb();
		set_d_rx_z2(adapter, f2, z1);
		mb();
		set_d_rx_f2(adapter, f2);
		
		adapter->last_fcnt = fcnt - 1;
	}
}

static inline void
hfcpci_b_recv_hdlc_irq(struct hfcpci_adapter *adapter, int nr)
{
	struct hfcpci_bcs *bcs = &adapter->bcs[nr];
	struct sk_buff *skb;
	char *fifo_adr = adapter->fifo + (nr ? 0x6000 : 0x4000);
	char *p;
	int cnt, fcnt;
	int loop = 5;
	u8 f1, f2;
	u16 z1, z2;

	while (loop-- > 0) {
		f1 = get_b_rx_f1(bcs);
		f2 = get_b_rx_f2(bcs);
		DBG(DBG_B_RECV, "f1 %d f2 %d", f1, f2);
		
		fcnt = f1 - f2;
		if (fcnt < 0)
			fcnt += 32;

		if (!fcnt)
			return;
		
		if (fcnt < bcs->last_fcnt)
			/* overrun */
			hfcpci_clear_b_rx_fifo(bcs);
			// XXX init last_fcnt
		
		z1 = get_b_rx_z1(bcs, f2);
		z2 = get_b_rx_z2(bcs, f2);
		DBG(DBG_B_RECV, "z1 %d z2 %d", z1, z2);

		cnt = z1 - z2;
		if (cnt < 0)
			cnt += B_FIFO_SIZE;
		cnt++;
		
		if (cnt < 4) {
			DBG(DBG_WARN, "frame too short");
			goto next;
		}
		if (fifo_adr[z1] != 0) {
			DBG(DBG_WARN, "CRC error");
			goto next;
		}
		cnt -= 3;
		skb = dev_alloc_skb(cnt);
		if (!skb) {
			DBG(DBG_WARN, "no mem");
			goto next;
		}
		p = skb_put(skb, cnt);
		if (z2 + cnt <= B_FIFO_END) {
			memcpy(p, fifo_adr + z2, cnt);
		} else {
			memcpy(p, fifo_adr + z2, B_FIFO_END - z2);
			memcpy(p + (B_FIFO_END - z2), fifo_adr + B_FIFO_START,
			       cnt - (B_FIFO_END - z2));
		}

		DBG_SKB(DBG_B_RECV, skb);
		B_L1L2(bcs, PH_DATA | INDICATION, skb);
	
	next:
		if (++z1 >= B_FIFO_END)
			z1 -= B_FIFO_SIZE;

		f2 = (f2 + 1) & (MAX_B_FRAMES - 1);
		mb();
		set_b_rx_z2(bcs, f2, z1);
		mb();
		set_b_rx_f2(bcs, f2);
		
		bcs->last_fcnt = fcnt - 1;
	}
}

static inline void
hfcpci_b_recv_trans_irq(struct hfcpci_adapter *adapter, int nr)
{
	struct hfcpci_bcs *bcs = &adapter->bcs[nr];
	struct sk_buff *skb;
	char *fifo_adr = adapter->fifo + (nr ? 0x6000 : 0x4000);
	char *p;
	int cnt;
	int loop = 5;
	u8 f1, f2;
	u16 z1, z2;

	f1 = get_b_rx_f1(bcs);
	f2 = get_b_rx_f2(bcs);

	if (f1 != f2)
		BUG();

	while (loop-- > 0) {
		z1 = get_b_rx_z1(bcs, f2);
		z2 = get_b_rx_z2(bcs, f2);
		
		cnt = z1 - z2;
		if (!cnt)
			/* no data available */
			return;
		
		if (cnt < 0)
			cnt += B_FIFO_SIZE;
		
		if (cnt > HFCPCI_BTRANS_THRESHOLD)
			cnt = HFCPCI_BTRANS_THRESHOLD;
		
		skb = dev_alloc_skb(cnt);
		if (!skb) {
			DBG(DBG_WARN, "no mem");
			goto next;
		}
		
		p = skb_put(skb, cnt);
		if (z2 + cnt <= 0x2000) {
			memcpy(p, fifo_adr + z2, cnt);
		} else {
			memcpy(p, fifo_adr + z2, 0x2000 - z2);
			p += 0x2000 - z2;
			memcpy(p, fifo_adr + 0x200, cnt - (0x2000 - z2));
		}
		
		DBG_SKB(DBG_B_RECV, skb);
		B_L1L2(bcs, PH_DATA | INDICATION, skb);
		
	next:
		z2 += cnt;
		if (z2 >= 0x2000)
			z2 -= B_FIFO_SIZE;
		
		mb();
		set_b_rx_z2(bcs, f2, z2);
		// XXX always receive buffers of a given size
	}
}

static inline void
hfcpci_b_recv_irq(struct hfcpci_adapter *adapter, int nr)
{
	DBG(DBG_B_RECV, "");

	switch (adapter->bcs[nr].mode) {
	case L1_MODE_NULL:
		DBG(DBG_WARN, "?");
		break;
		
	case L1_MODE_HDLC:
		hfcpci_b_recv_hdlc_irq(adapter, nr);
		break;

	case L1_MODE_TRANS:
		hfcpci_b_recv_trans_irq(adapter, nr);
		break;
	}
}

// ----------------------------------------------------------------------
// transmit IRQ

// XXX make xmit FIFO deeper than 1 

static inline void
hfcpci_d_xmit_irq(struct hfcpci_adapter *adapter)
{
	struct sk_buff *skb;

	DBG(DBG_D_XMIT, "");

	skb = adapter->tx_skb;
	if (!skb) {
		DBG(DBG_WARN, "?");
		return;
	}

	adapter->tx_skb = NULL;
	D_L1L2(adapter, PH_DATA | CONFIRM, (void *) skb->truesize);
	dev_kfree_skb_irq(skb);
}

static inline void
hfcpci_b_xmit_irq(struct hfcpci_adapter *adapter, int nr)
{
	struct hfcpci_bcs *bcs = &adapter->bcs[nr];
	struct sk_buff *skb;

	DBG(DBG_B_XMIT, "");

	skb = bcs->tx_skb;
	if (!skb) {
		DBG(DBG_WARN, "?");
		return;
	}

	bcs->tx_skb = NULL;
	B_L1L2(bcs, PH_DATA | CONFIRM, skb);
}

// ----------------------------------------------------------------------
// Layer 1 state change IRQ

static inline void
hfcpci_state_irq(struct hfcpci_adapter *adapter)
{
	u8 val;

	val = hfcpci_readb(adapter, HFCPCI_STATES);
	DBG(DBG_L1M, "STATES %#x", val);
	FsmEvent(&adapter->l1m, val & 0xf, NULL);
}

// ----------------------------------------------------------------------
// Timer IRQ

static inline void
hfcpci_timer_irq(struct hfcpci_adapter *adapter)
{
	hfcpci_writeb(adapter, adapter->ctmt | HFCPCI_CLTIMER, HFCPCI_CTMT);
}

// ----------------------------------------------------------------------
// IRQ handler

static irqreturn_t
hfcpci_irq(int intno, void *dev, struct pt_regs *regs)
{
	struct hfcpci_adapter *adapter = dev;
	int loop = 15;
	u8 val, stat;

	if (!(adapter->int_m2 & 0x08))
		return IRQ_NONE;		/* not initialised */ // XX

	stat = hfcpci_readb(adapter, HFCPCI_STATUS);
	if (!(stat & HFCPCI_ANYINT))
		return IRQ_NONE;

	spin_lock(&adapter->hw_lock);
	while (loop-- > 0) {
		val = hfcpci_readb(adapter, HFCPCI_INT_S1);
		DBG(DBG_IRQ, "stat %02x s1 %02x", stat, val);
		val &= adapter->int_m1;

		if (!val)
			break;

		if (val & 0x08)
			hfcpci_b_recv_irq(adapter, 0);
			
		if (val & 0x10)
			hfcpci_b_recv_irq(adapter, 1);

		if (val & 0x01)
			hfcpci_b_xmit_irq(adapter, 0);

		if (val & 0x02)
			hfcpci_b_xmit_irq(adapter, 1);

		if (val & 0x20)
			hfcpci_d_recv_irq(adapter);

		if (val & 0x04)
			hfcpci_d_xmit_irq(adapter);

		if (val & 0x40)
			hfcpci_state_irq(adapter);

		if (val & 0x80)
			hfcpci_timer_irq(adapter);
	}
	spin_unlock(&adapter->hw_lock);
	return IRQ_HANDLED;
}

// ----------------------------------------------------------------------
// reset hardware

static void
hfcpci_reset(struct hfcpci_adapter *adapter)
{
	/* disable all interrupts */
	adapter->int_m1 = 0;
	adapter->int_m2 = 0;
	hfcpci_writeb(adapter, adapter->int_m1, HFCPCI_INT_M1);
	hfcpci_writeb(adapter, adapter->int_m2, HFCPCI_INT_M2);

	/* reset */
	hfcpci_writeb(adapter, HFCPCI_RESET, HFCPCI_CIRM);
	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule_timeout((30 * HZ) / 1000);
	hfcpci_writeb(adapter, 0, HFCPCI_CIRM);
	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule_timeout((20 * HZ) / 1000);
	if (hfcpci_readb(adapter, HFCPCI_STATUS) & 2) // XX
		printk(KERN_WARNING "HFC-PCI init bit busy\n");
}

// ----------------------------------------------------------------------
// init hardware

static void
hfcpci_hw_init(struct hfcpci_adapter *adapter)
{
	adapter->fifo_en = 0x30;	/* only D fifos enabled */ // XX
	hfcpci_writeb(adapter, adapter->fifo_en, HFCPCI_FIFO_EN);

	/* no echo connect , threshold */
	adapter->trm = HFCPCI_BTRANS_THRESMASK;
	hfcpci_writeb(adapter, adapter->trm, HFCPCI_TRM);

	/* ST-Bit delay for TE-Mode */
	hfcpci_writeb(adapter, CLKDEL_TE, HFCPCI_CLKDEL);

	/* S/T Auto awake */
	adapter->sctrl_e = HFCPCI_AUTO_AWAKE;
	hfcpci_writeb(adapter, adapter->sctrl_e, HFCPCI_SCTRL_E);

	/* no exchange */
	adapter->bswapped = 0;
	/* we are in TE mode */
	adapter->nt_mode = 0;

	adapter->ctmt = HFCPCI_TIM3_125 | HFCPCI_AUTO_TIMER;
	hfcpci_writeb(adapter, adapter->ctmt, HFCPCI_CTMT);

	adapter->int_m1 = HFCPCI_INTS_DTRANS | HFCPCI_INTS_DREC |
		HFCPCI_INTS_L1STATE;
	hfcpci_writeb(adapter, adapter->int_m1, HFCPCI_INT_M1);

	/* clear already pending ints */
	hfcpci_readb(adapter, HFCPCI_INT_S1);

	adapter->l1m.state = 2;
	hfcpci_writeb(adapter, HFCPCI_LOAD_STATE | 2, HFCPCI_STATES);	// XX /* HFC ST 2 */
	udelay(10);
	hfcpci_writeb(adapter, 2, HFCPCI_STATES);	/* HFC ST 2 */

	/* HFC Master Mode */
	adapter->mst_m = HFCPCI_MASTER;
	hfcpci_writeb(adapter, adapter->mst_m, HFCPCI_MST_MODE);

	/* set tx_lo mode, error in datasheet ! */
	adapter->sctrl = 0x40;
	hfcpci_writeb(adapter, adapter->sctrl, HFCPCI_SCTRL);

	adapter->sctrl_r = 0;
	hfcpci_writeb(adapter, adapter->sctrl_r, HFCPCI_SCTRL_R);

	// XXX
	/* Init GCI/IOM2 in master mode */
	/* Slots 0 and 1 are set for B-chan 1 and 2 */
	/* D- and monitor/CI channel are not enabled */
	/* STIO1 is used as output for data, B1+B2 from ST->IOM+HFC */
	/* STIO2 is used as data input, B1+B2 from IOM->ST */
	/* ST B-channel send disabled -> continous 1s */
	/* The IOM slots are always enabled */
	adapter->conn = 0;	/* set data flow directions */
	hfcpci_writeb(adapter, adapter->conn, HFCPCI_CONNECT);
	hfcpci_writeb(adapter, 0x80, HFCPCI_B1_SSL);	/* B1-Slot 0 STIO1 out enabled */
	hfcpci_writeb(adapter, 0x81, HFCPCI_B2_SSL);	/* B2-Slot 1 STIO1 out enabled */
	hfcpci_writeb(adapter, 0x80, HFCPCI_B1_RSL);	/* B1-Slot 0 STIO2 in enabled */
	hfcpci_writeb(adapter, 0x81, HFCPCI_B2_RSL);	/* B2-Slot 1 STIO2 in enabled */

	/* Finally enable IRQ output */
	adapter->int_m2 = HFCPCI_IRQ_ENABLE;
	hfcpci_writeb(adapter, adapter->int_m2, HFCPCI_INT_M2);

	hfcpci_readb(adapter, HFCPCI_INT_S2);
}

// ----------------------------------------------------------------------
// probe / remove

static struct hfcpci_adapter * __devinit 
new_adapter(struct pci_dev *pdev)
{
	struct hfcpci_adapter *adapter;
	struct hisax_b_if *b_if[2];
	int i;

	adapter = kmalloc(sizeof(struct hfcpci_adapter), GFP_KERNEL);
	if (!adapter)
		return NULL;

	memset(adapter, 0, sizeof(struct hfcpci_adapter));

	adapter->d_if.owner = THIS_MODULE;
	adapter->d_if.ifc.priv = adapter;
	adapter->d_if.ifc.l2l1 = hfcpci_d_l2l1;
	
	for (i = 0; i < 2; i++) {
		adapter->bcs[i].adapter = adapter;
		adapter->bcs[i].channel = i;
		adapter->bcs[i].b_if.ifc.priv = &adapter->bcs[i];
		adapter->bcs[i].b_if.ifc.l2l1 = hfcpci_b_l2l1;
	}

	pci_set_drvdata(pdev, adapter);

	for (i = 0; i < 2; i++)
		b_if[i] = &adapter->bcs[i].b_if;

	hisax_register(&adapter->d_if, b_if, "hfcpci", protocol);

	return adapter;
}

static void delete_adapter(struct hfcpci_adapter *adapter)
{
	hisax_unregister(&adapter->d_if);
	kfree(adapter);
}

static int __devinit hfcpci_probe(struct pci_dev *pdev,
				 const struct pci_device_id *ent)
{
	struct hfcpci_adapter *adapter;
	int retval;

	DBG(DBG_INFO, "");
	retval = -ENOMEM;
	adapter = new_adapter(pdev);
	if (!adapter)
		goto err;

	retval = pci_enable_device(pdev);
	if (retval)
		goto err_free;

	adapter->irq = pdev->irq;
	retval = request_irq(adapter->irq, hfcpci_irq, SA_SHIRQ, 
			     "hfcpci", adapter);
	if (retval)
		goto err_free;

	retval = -EBUSY;
	if (!request_mem_region(pci_resource_start(pdev, 1), 256, "hfcpci"))
		goto err_free_irq;

	adapter->mmio = ioremap(pci_resource_start(pdev, 1), 256); // XX pci_io
	if (!adapter->mmio)
		goto err_release_region;

	/* Allocate 32K for FIFOs */
	if (pci_set_dma_mask(pdev, 0xffffffff))
		goto err_unmap;
	
	adapter->fifo = pci_alloc_consistent(pdev, 32768, &adapter->fifo_dma); 
	if (!adapter->fifo)
		goto err_unmap;
	
	pci_write_config_dword(pdev, HFCPCI_MWBA, (u32) adapter->fifo_dma);
	pci_set_master(pdev);

	adapter->l1m.fsm = &l1fsm;
	adapter->l1m.state = ST_L1_F0;
#ifdef CONFIG_HISAX_DEBUG
	adapter->l1m.debug = 1;
#else
	adapter->l1m.debug = 0;
#endif
	adapter->l1m.userdata = adapter;
	adapter->l1m.printdebug = l1m_debug;
	FsmInitTimer(&adapter->l1m, &adapter->timer);

	hfcpci_reset(adapter);
	hfcpci_hw_init(adapter);

	printk(KERN_INFO "hisax_hfcpci: found adapter %s at %s\n",
	       (char *) ent->driver_data, pci_name(pdev));

	return 0;

 err_unmap:
	iounmap(adapter->mmio);
 err_release_region:
	release_mem_region(pci_resource_start(pdev, 1), 256);
 err_free_irq:
	free_irq(adapter->irq, adapter);
 err_free:
	delete_adapter(adapter);
 err:
	return retval;
}

static void __devexit hfcpci_remove(struct pci_dev *pdev)
{
	struct hfcpci_adapter *adapter = pci_get_drvdata(pdev);

	hfcpci_reset(adapter);

//	del_timer(&cs->hw.hfcpci.timer); XX

	/* disable DMA */
	pci_disable_device(pdev);
	pci_write_config_dword(pdev, HFCPCI_MWBA, 0);
	pci_free_consistent(pdev, 32768, adapter->fifo, adapter->fifo_dma);

	iounmap(adapter->mmio);
	release_mem_region(pci_resource_start(pdev, 1), 256);
	free_irq(adapter->irq, adapter);
	delete_adapter(adapter);
}

static struct pci_driver hfcpci_driver = {
	.name     = "hfcpci",
	.probe    = hfcpci_probe,
	.remove   = __devexit_p(hfcpci_remove),
	.id_table = hfcpci_ids,
};

static int __init hisax_hfcpci_init(void)
{
	int retval;

	printk(KERN_INFO "hisax_hfcpcipnp: HFC PCI ISDN driver v0.0.1\n");

	l1fsm.state_count = L1_STATE_COUNT;
	l1fsm.event_count = L1_EVENT_COUNT;
	l1fsm.strState = strL1State;
	l1fsm.strEvent = strL1Event;
	retval = FsmNew(&l1fsm, L1FnList, ARRAY_SIZE(L1FnList));
	if (retval)
		goto err;

	retval = pci_module_init(&hfcpci_driver);
	if (retval)
		goto err_fsm;
	
	return 0;

 err_fsm:
	FsmFree(&l1fsm);
 err:
	return retval;
}

static void __exit hisax_hfcpci_exit(void)
{
	FsmFree(&l1fsm);
	pci_unregister_driver(&hfcpci_driver);
}

module_init(hisax_hfcpci_init);
module_exit(hisax_hfcpci_exit);
