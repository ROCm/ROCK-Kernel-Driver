/*
 * ibm_ocp_phy.c
 *
 * Ethernet PHY routines and database for IBM 4xx PowerPC processors.
 *
 * Based on  the Fast Ethernet Controller (FEC) driver for
 * Motorola MPC8xx and other contributions, see driver for contributers.
 *
 *      Armin Kuster akuster@mvista.com
 *      Sept, 2001
 *
 * Copyright 2000 MontaVista Softare Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 *  THIS  SOFTWARE  IS PROVIDED   ``AS  IS'' AND   ANY  EXPRESS OR   IMPLIED
 *  WARRANTIES,   INCLUDING, BUT NOT  LIMITED  TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 *  NO  EVENT  SHALL   THE AUTHOR  BE    LIABLE FOR ANY   DIRECT,  INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *  NOT LIMITED   TO, PROCUREMENT OF  SUBSTITUTE GOODS  OR SERVICES; LOSS OF
 *  USE, DATA,  OR PROFITS; OR  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 *  ANY THEORY OF LIABILITY, WHETHER IN  CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 *  THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the  GNU General Public License along
 *  with this program; if not, write  to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * TODO: mii queue process
 *
 * Version 1.0 (01/09/28) by Armin kuster
 *	added find_phy to auto discover phy adder
 *	and phy type.
 * Version 1.1 (01/10/05) Armin Kuster
 *	Use dev->base_addr for EMAC reg addr
 *
 * Version 1.2 (01/10/17) Armin
 *      removed unused emac from process_mii_queue & mii_enet_mii
 *      switch 10HD w/ 100FD in  mii_parse_dp83843_pcr via Dimitrios Michailidiss
 *      general clean-up
 *      added proccess_mii_queue & mii_queue_schedule
 *      added support for National DP83846A PHY
 *
 * Vesrion: 1.3 (01/11/13) Armin
 * 	fixed mii_parse_dp83843_pcr, decode 100FDX & 10HDX wrong./
 *
 * Version: 1.4 (01/12/26) Armin & Kim Young-Han
 * 		added BCM5221 phy support, Kim
 * 		added Am79C875 phy support, armin
 * Version: 1.5 (02/05/02) Armin
 * 		Name change
 * Version: 1.6 (02/12/02) Andrew, David, Stefan
 * 		fixed find_phy
 * Version: 1.7 (03/01/02) Andrew May
 * 		Improved find_phy
 * 		added Am79C874
 * Version: 1.8 (03/25/02) Andrew May 
 *		Fix bad Partner Link check for the Am79C874 phy
 *
 * Version: 1.9 (04/04/02) Matt Porter
 * 		Added Am79C875A phy support
 * 		Message cleanup
 * Version: 2.0 (04/15/02) Todd Poynor
 * 	redid ANLPAR parser for Am79c875* to select proper speed
 *
 * Version: 2.0 (04/18/02) - Armin
 * 	shifted  Am79c865 id by 4 and changed shift to 4 (Ash suppport)
 *
 * Version: 2.1 (04/25/02) - Armin
 *  	using zmii_phyid_adj() to adjust phy addrs on those cpus
 *  	that use a zmii bridge
 *  	fixed find_phy for zmii bridge support
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/netdevice.h>

#include <asm/processor.h>	/* Processor type for cache alignment. */
#include <asm/bitops.h>
#include <asm/io.h>

#include "ibm_ocp_enet.h"
static int next_phy_available = MIN_PHY_ADDR;

/* Forward declarations of some structures to support different PHYs */

/* Interrupt events/masks. */
#define FEC_ENET_HBERR	((uint)0x80000000)	/* Heartbeat error */
#define FEC_ENET_BABR	((uint)0x40000000)	/* Babbling receiver */
#define FEC_ENET_BABT	((uint)0x20000000)	/* Babbling transmitter */
#define FEC_ENET_GRA	((uint)0x10000000)	/* Graceful stop complete */
#define FEC_ENET_TXF	((uint)0x08000000)	/* Full frame transmitted */
#define FEC_ENET_TXB	((uint)0x04000000)	/* A buffer was transmitted */
#define FEC_ENET_RXF	((uint)0x02000000)	/* Full frame received */
#define FEC_ENET_RXB	((uint)0x01000000)	/* A buffer was received */
#define FEC_ENET_MII	((uint)0x00800000)	/* MII interrupt */
#define FEC_ENET_EBERR	((uint)0x00400000)	/* SDMA bus error */

#define FEC_ECNTRL_PINMUX	0x00000004
#define FEC_ECNTRL_ETHER_EN	0x00000002
#define FEC_ECNTRL_RESET	0x00000001

#define FEC_RCNTRL_BC_REJ	0x00000010
#define FEC_RCNTRL_PROM		0x00000008
#define FEC_RCNTRL_MII_MODE	0x00000004
#define FEC_RCNTRL_DRT		0x00000002
#define FEC_RCNTRL_LOOP		0x00000001

#define FEC_TCNTRL_FDEN		0x00000004
#define FEC_TCNTRL_HBC		0x00000002
#define FEC_TCNTRL_GTS		0x00000001

void ocp_enet_mii(struct net_device *dev);
extern int ocp_enet_mdio_read(struct net_device *dev, int reg, uint * value);
extern int ocp_enet_mdio_write(struct net_device *dev, int reg);

/* Make MII read/write commands for the OCP. */

/* MII processing.  We keep this as simple as possible.  Requests are
 * placed on the list (if there is room).  When the request is finished
 * by the MII, an optional function may be called.
 */
static int mii_queue(struct net_device *dev, int request,
		     void (*func) (uint, struct net_device *));

/* Register definitions for the PHY. */

#define MII_REG_CR		0	/* Control Register */
#define MII_REG_SR		1	/* Status Register */
#define MII_REG_PHYIR1		2	/* PHY Identification Register 1 */
#define MII_REG_PHYIR2		3	/* PHY Identification Register 2 */
#define MII_REG_ANAR		4	/* A-N Advertisement Register */
#define MII_REG_ANLPAR		5	/* A-N Link Partner Ability Register */
#define MII_REG_ANER		6	/* A-N Expansion Register */
#define MII_REG_ANNPTR		7	/* A-N Next Page Transmit Register */
#define MII_REG_ANLPRNPR	8	/* A-N Link Partner Received Next Page Reg. */

/* values for phy_status */

#define PHY_CONF_ANE		0x0001	/* 1 auto-negotiation enabled */
#define PHY_CONF_LOOP		0x0002	/* 1 loopback mode enabled */
#define PHY_CONF_SPMASK		0x01E0	/* mask for speed */
#define PHY_CONF_10HDX		0x0010	/* 10 Mbit half duplex supported */
#define PHY_CONF_10FDX		0x0020	/* 10 Mbit full duplex supported */
#define PHY_CONF_100HDX		0x0040	/* 100 Mbit half duplex supported */
#define PHY_CONF_100FDX		0x0080	/* 100 Mbit full duplex supported */

#define PHY_STAT_LINK		0x0100	/* 1 up - 0 down */
#define PHY_STAT_FAULT		0x0200	/* 1 remote fault */
#define PHY_STAT_ANC		0x0400	/* 1 auto-negotiation complete */
#define PHY_STAT_SPMASK		0xf000	/* mask for speed */
#define PHY_STAT_10HDX		0x1000	/* 10 Mbit half duplex selected */
#define PHY_STAT_10FDX		0x2000	/* 10 Mbit full duplex selected */
#define PHY_STAT_100HDX		0x4000	/* 100 Mbit half duplex selected */
#define PHY_STAT_100FDX		0x8000	/* 100 Mbit full duplex selected */

mii_list_t *mii_free;
mii_list_t *mii_head;
mii_list_t *mii_tail;

/* mii routines */

/* 	Manually process all queued commands */

void
process_mii_queue(struct net_device *dev)
{
	mii_list_t *mip;
	uint mii_reg = 0;

	while ((mip = mii_head) != NULL) {

		if (mip->mii_func != NULL) {
			ocp_enet_mdio_read(dev, mip->mii_regval, &mii_reg);
			(*(mip->mii_func)) (mii_reg, dev);
		} else {
			if (mip->mii_regval & EMAC_STACR_READ)
				ocp_enet_mdio_read(dev, mip->mii_regval,
						   &mii_reg);
			else
				ocp_enet_mdio_write(dev, mip->mii_regval);
		}

		mii_head = mip->mii_next;
		mip->mii_next = mii_free;
		mii_free = mip;
	}
}

static int
mii_queue(struct net_device *dev, int regval,
	  void (*func) (uint, struct net_device *))
{
	unsigned long flags;
	mii_list_t *mip;
	int retval;

	retval = 0;
	save_flags(flags);
	cli();

	if ((mip = mii_free) != NULL) {
		mii_free = mip->mii_next;
		mip->mii_regval = regval;
		mip->mii_func = func;
		mip->mii_next = NULL;
		if (mii_head) {
			mii_tail->mii_next = mip;
			mii_tail = mip;
		} else {
			mii_head = mii_tail = mip;
		}
	} else
		retval = 1;

	restore_flags(flags);

	return retval;
}

void
mii_do_cmd(struct net_device *dev, const phy_cmd_t * c)
{
	int k;

	if (!c)
		return;

	for (k = 0; (c + k)->mii_data != mk_mii_end; k++)
		mii_queue(dev, (c + k)->mii_data, (c + k)->funct);
}

static void
mii_parse_sr(uint mii_reg, struct net_device *dev)
{
	struct ocp_enet_private *fep = dev->priv;
	volatile uint *s = &(fep->phy_status);

	*s &= ~(PHY_STAT_LINK | PHY_STAT_FAULT | PHY_STAT_ANC);

	if (mii_reg & 0x0004)
		*s |= PHY_STAT_LINK;
	if (mii_reg & 0x0010)
		*s |= PHY_STAT_FAULT;
	if (mii_reg & 0x0020)
		*s |= PHY_STAT_ANC;
}

static void
mii_parse_cr(uint mii_reg, struct net_device *dev)
{
	struct ocp_enet_private *fep = dev->priv;
	volatile uint *s = &(fep->phy_status);

	*s &= ~(PHY_CONF_ANE | PHY_CONF_LOOP);

	if (mii_reg & 0x1000)
		*s |= PHY_CONF_ANE;
	if (mii_reg & 0x4000)
		*s |= PHY_CONF_LOOP;
}

static void
mii_parse_anar(uint mii_reg, struct net_device *dev)
{
	struct ocp_enet_private *fep = dev->priv;
	volatile uint *s = &(fep->phy_status);

	if (mii_reg & 0x0020)
		*s |= PHY_CONF_10HDX;
	if (mii_reg & 0x0040)
		*s |= PHY_CONF_10FDX;
	if (mii_reg & 0x0080)
		*s |= PHY_CONF_100HDX;
	if (mii_reg & 0x00100)
		*s |= PHY_CONF_100FDX;

}

/* ------------------------------------------------------------------------- */
/* The National Semiconductor DP83843  is used on the IBM Walnut	     */

/* register definitions */

#define MII_DP83843_PHYSTS	0x10	/* Phy Status Register */
#define MII_DP83843_MIPSCR	0x11	/* MII int PHY spec. Register */
#define MII_DP83843_MIPGSR	0x12	/* MII int generic status Register */
#define MII_DP83843_DCR		0x13	/* Disconnect Counter Register */
#define MII_DP83843_FCSCR	0x14	/* False Carrier Sense Register */
#define MII_DP83843_RECS	0x15	/* Receive counter Reg. */
#define MII_DP83843_PCSR	0x16	/* PCS Sub-layer Config & status Reg. */
#define MII_DP83843_LBR		0x17	/* Loopback & bypass Reg. */
#define MII_DP83843_10BTSCR	0x18	/* 10BASE-T status & cntl Reg. */
#define MII_DP83843_PHYCTRL	0x19	/* PHY cntl Reg. */

static void
mii_parse_dp83843_pcr(uint mii_reg, struct net_device *dev)
{
	struct ocp_enet_private *fep = dev->priv;
	volatile uint *s = &(fep->phy_status);

	*s &= ~(PHY_STAT_SPMASK);

	switch ((mii_reg >> 1) & 3) {
	case 0:
		*s |= PHY_STAT_100HDX;
		break;
	case 1:
		*s |= PHY_STAT_10HDX;
		break;
	case 2:
		*s |= PHY_STAT_100FDX;
		break;
	case 3:
		*s |= PHY_STAT_10FDX;
		break;
	}
}

static phy_info_t phy_info_dp83843 = {
	0x20005c10,
	"DP83843",
	0,
	(const phy_cmd_t[]) {	/* config */

			     /* parse cr and anar to get some info */

			     {mk_mii_read(MII_REG_CR), mii_parse_cr},
			     {mk_mii_read(MII_REG_ANAR), mii_parse_anar},
			     {mk_mii_end,}
			     },
	(const phy_cmd_t[]) {	/* startup - enable interrupts */
			     {mk_mii_write(MII_DP83843_MIPSCR, 0x0001), NULL},
			     {mk_mii_write(MII_REG_CR, PHY_BMCR_RST_NEG), NULL},	/* autonegotiate */
			     {mk_mii_end,}
			     },
	(const phy_cmd_t[]) {	/* ack_int */

			     /* we need to read ISR, SR and ANER to acknowledge */

			     {mk_mii_read(MII_DP83843_MIPGSR), NULL},
			     {mk_mii_read(MII_REG_SR), mii_parse_sr},
			     {mk_mii_read(MII_REG_ANAR), mii_parse_anar},

			     /* read pcr to get info */

			     {mk_mii_read(MII_DP83843_PHYSTS),
			      mii_parse_dp83843_pcr},
			     {mk_mii_end,}
			     },
	(const phy_cmd_t[]) {	/* shutdown - disable interrupts */
			     {mk_mii_write(MII_DP83843_MIPSCR, 0x0000), NULL},
			     {mk_mii_end,}
			     },
};

/* ------------------------------------------------------------------------- */
/* The Intel LXT971A is used on the esd CPCI-405 and EP 405 */

/* register definitions */

#define MII_LXT971A_SR2		17	/* PHY status Register #2 */
#define MII_LXT971A_IER		18	/* PHY interrupt enable Register */
#define MII_LXT971A_ISR		19	/* PHY interrupt status Register */

static void
mii_parse_lxt971a_sr2(uint mii_reg, struct net_device *dev)
{
	struct ocp_enet_private *fep = dev->priv;
	volatile uint *s = &(fep->phy_status);

	*s &= ~(PHY_STAT_SPMASK);

	switch (mii_reg & 0x4200) {
	case 0x4200:
		*s |= PHY_STAT_100FDX;
		break;
	case 0x4000:
		*s |= PHY_STAT_100HDX;
		break;
	case 0x0200:
		*s |= PHY_STAT_10FDX;
		break;
	case 0x0000:
		*s |= PHY_STAT_10HDX;
		break;
	}
}

static phy_info_t phy_info_lxt971a = {
	0x0001378e,
	"LXT971A",
	4,
	(const phy_cmd_t[]) {	/* config */

			     /* parse cr and anar to get some info */

			     {mk_mii_read(MII_REG_CR), mii_parse_cr},
			     {mk_mii_read(MII_REG_ANAR), mii_parse_anar},
			     {mk_mii_end,}
			     },
	(const phy_cmd_t[]) {	/* startup - enable interrupts */
			     {mk_mii_write(MII_LXT971A_IER, 0x00f2), NULL},	/* enable interrupts */
			     {mk_mii_write(MII_REG_CR, PHY_BMCR_RST_NEG), NULL},	/* autonegotiate */
			     {mk_mii_end,}
			     },
	(const phy_cmd_t[]) {	/* ack_int */

			     /* we need to read ISR, SR and ANAR to acknowledge */

			     {mk_mii_read(MII_LXT971A_ISR), NULL},
			     {mk_mii_read(MII_REG_SR), mii_parse_sr},
			     {mk_mii_read(MII_REG_ANAR), mii_parse_anar},

			     /* read sr2 to get info */

			     {mk_mii_read(MII_LXT971A_SR2),
			      mii_parse_lxt971a_sr2},
			     {mk_mii_end,}
			     },
	(const phy_cmd_t[]) {	/* shutdown - disable interrupts */
			     {mk_mii_write(MII_LXT971A_IER, 0x0000), NULL},
			     {mk_mii_end,}
			     },
};

/* ------------------------------------------------------------------------- */
/* The Cirrus Logic CS8952 (CrystalLAN) is used on the MPL PIP405	     */

/* register definitions */

#define MII_CS8952_IMR		0x10	/* Interrupt Mask Reg. */
#define MII_CS8952_ISR		0x11	/* Interrupt Status Reg. */
#define MII_CS8952_DCR		0x12	/* Disconnect Counter Reg. */
#define MII_CS8952_FCSCR	0x13	/* False Carrier Counter Reg. */
#define MII_CS8952_SKIR		0x14	/* Scrambler Key Init Reg. */
#define MII_CS8952_RECR		0x15	/* Receive Error Counter Reg. */
#define MII_CS8952_DKIR		0x16	/* Descrambler Key Init Register */
#define MII_CS8952_PCSR		0x17	/* PCS Sub-layer Config & status Reg. */
#define MII_CS8952_LBR		0x18	/* Loopback & bypass Reg. */
#define MII_CS8952_SSR		0x19	/* Self Status Reg. */
#define MII_CS8952_10BTSR	0x1B	/* 10BASE-T Status Reg. */
#define MII_CS8952_10BTCR	0x1C	/* 10BASE-T Control Reg. */

static void
mii_parse_cs8952_ssr(uint mii_reg, struct net_device *dev)
{
	struct ocp_enet_private *fep = dev->priv;
	volatile uint *s = &(fep->phy_status);

	*s &= ~(PHY_STAT_SPMASK);

	switch ((mii_reg >> 6) & 3) {
	case 0:
		*s |= PHY_STAT_100HDX;
		break;
	case 1:
		*s |= PHY_STAT_10HDX;
		break;
	case 2:
		*s |= PHY_STAT_100FDX;
		break;
	case 3:
		*s |= PHY_STAT_10FDX;
		break;
	}
}

static phy_info_t phy_info_cs8952 = {
	0x001a2205,
	"CS8952",
	0,
	(const phy_cmd_t[]) {	/* config */
			     /* parse cr and anar to get some info */
			     {mk_mii_read(MII_REG_CR), mii_parse_cr},
			     {mk_mii_read(MII_REG_ANAR), mii_parse_anar},
			     {mk_mii_end,}
			     },
	(const phy_cmd_t[]) {	/* startup - enable interrupts */
			     {mk_mii_write(MII_CS8952_IMR, 0xFFFE), NULL},
			     /* auto-negotiate */
			     {mk_mii_write(MII_REG_CR, PHY_BMCR_RST_NEG), NULL},
			     {mk_mii_end,}
			     },
	(const phy_cmd_t[]) {	/* ack_int */
			     /* we need to read ISR, SR and ANER to acknowledge */
			     {mk_mii_read(MII_CS8952_ISR), NULL},
			     {mk_mii_read(MII_REG_SR), mii_parse_sr},
			     {mk_mii_read(MII_REG_ANAR), mii_parse_anar},
			     /* read ssr to get more info */
			     {mk_mii_read(MII_CS8952_SSR),
			      mii_parse_cs8952_ssr},
			     {mk_mii_end,}
			     },
	(const phy_cmd_t[]) {	/* shutdown - disable interrupts */
			     {mk_mii_write(MII_CS8952_IMR, 0x0000), NULL},
			     {mk_mii_end,}
			     },
};

/* ------------------------------------------------------------------------- */
/* The National Semiconductor DP83846A 	*/

/* register definitions */

#define MII_DP83846A_PHYSTS	0x10	/* Phy Status Register */
#define MII_DP83846A_FCSCR	0x14	/* False Carrier Sense Register */
#define MII_DP83846A_RECS	0x15	/* Receive Error Counter Reg. */
#define MII_DP83846A_PCSR	0x16	/* PCS Sub-layer Config/status Reg. */
#define MII_DP83846A_PHYCTRL	0x19	/* PHY cntl Reg. */

static void
mii_parse_dp83846A_pcr(uint mii_reg, struct net_device *dev)
{
	struct ocp_enet_private *fep = dev->priv;
	volatile uint *s = &(fep->phy_status);

	*s &= ~(PHY_STAT_SPMASK);

	switch ((mii_reg >> 1) & 3) {
	case 0:
		*s |= PHY_STAT_100HDX;
		break;
	case 1:
		*s |= PHY_STAT_10HDX;
		break;
	case 2:
		*s |= PHY_STAT_100FDX;
		break;
	case 3:
		*s |= PHY_STAT_10FDX;
		break;
	}
}

static phy_info_t phy_info_dp83846A = {
	0x20005c23,
	"DP83846A",
	0,
	(const phy_cmd_t[]) {	/* config */

			     /* parse cr and anar to get some info */

			     {mk_mii_read(MII_REG_CR), mii_parse_cr},
			     {mk_mii_read(MII_REG_ANAR), mii_parse_anar},
			     {mk_mii_end,}
			     },
	(const phy_cmd_t[]) {	/* startup - enable interrupts */
			     {mk_mii_write(MII_REG_CR, PHY_BMCR_RST_NEG), NULL},	/* autonegotiate */
			     {mk_mii_end,}
			     },
	(const phy_cmd_t[]) {	/* ack_int */
			     /* 83846A doesn't have interrupts but ack_int is also used to
			        get initial status so here it goes. */

			     {mk_mii_read(MII_REG_SR), mii_parse_sr},
			     {mk_mii_read(MII_REG_ANAR), mii_parse_anar},

			     /* read pcr to get info */

			     {mk_mii_read(MII_DP83846A_PHYSTS),
			      mii_parse_dp83846A_pcr},
			     {mk_mii_end,}
			     },
	(const phy_cmd_t[]) {	/* shutdown - nothing */
			     {mk_mii_end,}
			     },
};

/* ------------------------------------------------------------------------- */
/* The Lucent Technologies LU3X31FT */

/* register definitions */

#define MII_LU3X31FT_PHYCTRLSTS	0x17	/* PHY Control/Status Register */
#define MII_LU3X31FT_IER	0x1D	/* PHY interrupt enable Register */
#define MII_LU3X31FT_ISR	0x1E	/* PHY interrupt status Register */

static void
mii_parse_lu3x31ft_pcr(uint mii_reg, struct net_device *dev)
{
	struct ocp_enet_private *fep = dev->priv;
	volatile uint *s = &(fep->phy_status);

	*s &= ~(PHY_STAT_SPMASK);

	switch ((mii_reg >> 8) & 3) {
	case 0:
		*s |= PHY_STAT_10HDX;
		break;
	case 1:
		*s |= PHY_STAT_10FDX;
		break;
	case 2:
		*s |= PHY_STAT_100HDX;
		break;
	case 3:
		*s |= PHY_STAT_100FDX;
		break;
	}
}

static phy_info_t phy_info_lu3x31ft = {
	0x90307421,
	"LU3X31FT",
	0,
	(const phy_cmd_t[]) {	/* config */

			     /* parse cr and anar to get some info */
			     {mk_mii_read(MII_REG_CR), mii_parse_cr},
			     {mk_mii_read(MII_REG_ANAR), mii_parse_anar},
			     {mk_mii_end,}
			     },
	(const phy_cmd_t[]) {
			     /* startup - enable interrupts */
			     {mk_mii_write(MII_LU3X31FT_IER, 0x0000), NULL},
			     /* autonegotiate */
			     {mk_mii_write(MII_REG_CR, PHY_BMCR_RST_NEG), NULL},
			     {mk_mii_end,}
			     },
	(const phy_cmd_t[]) {	/* ack_int */

			     /* we need to read ISR, SR and ANER to acknowledge */
			     {mk_mii_read(MII_LU3X31FT_ISR), NULL},
			     {mk_mii_read(MII_REG_SR), mii_parse_sr},
			     {mk_mii_read(MII_REG_ANAR), mii_parse_anar},

			     /* read pcr to get info */
			     {mk_mii_read(MII_LU3X31FT_PHYCTRLSTS),
			      mii_parse_lu3x31ft_pcr},
			     {mk_mii_end,}
			     },
	(const phy_cmd_t[]) {	/* shutdown - disable interrupts */
			     {mk_mii_write(MII_LU3X31FT_IER, 0xff80), NULL},
			     {mk_mii_end,}
			     },
};

/* ------------------------------------------------------------------------- */
/* The AMD Am79C875 */

/* register definitions */

#define MII_AM79C875_MFR	0x10	/* Mics. Feature Register */
#define MII_AM79C875_ICR	0x11	/* Interrupt Cntl/Status Register */
#define MII_AM79C875_DIAG	0x18	/* Diag Reg. */
#define MII_AM79C875_TEST	0x13	/* Test Reg. */
#define MII_AM79C875_MFR2	0x14	/* Mics. Feature 2 Register */
#define MII_AM79C875_RCR	0x15	/* Recv. Error counter */
#define MII_AM79C875_MCR	0x18	/* Mode contl reg */

static void
mii_parse_Am79C875_pcr(uint mii_reg, struct net_device *dev)
{
	struct ocp_enet_private *fep = dev->priv;
	volatile uint *s = &(fep->phy_status);

	*s &= ~(PHY_CONF_SPMASK);

	if (mii_reg & 0x0100) {
		*s |= PHY_STAT_100FDX;
	} else if (mii_reg & 0x0080) {
		*s |= PHY_STAT_100HDX;
	} else if (mii_reg & 0x0040) {
		*s |= PHY_STAT_10FDX;
	} else if (mii_reg & 0x0020) {
		*s |= PHY_STAT_10HDX;
	} else {
		*s |= PHY_STAT_10HDX;
	}
}

static phy_info_t phy_info_Am79C875 = {
	/*0x00137886,*/
	0x00013788,
	"Am79c875",
	4,
	(const phy_cmd_t[]) {	/* config */

			     /* parse cr and anar to get some info */

			     {mk_mii_read(MII_REG_CR), mii_parse_cr},
			     {mk_mii_read(MII_REG_ANAR), mii_parse_anar},
			     {mk_mii_end,}
			     },
	(const phy_cmd_t[]) {	/* startup - enable interrupts */
			     {mk_mii_write(MII_REG_CR, PHY_BMCR_AUTON), NULL},	/* Auto neg. on */
//              { mk_mii_write(MII_AM79C875_MFR, 0x4000), NULL}, /* int 1 to signle interrupt */
//              { mk_mii_write(MII_AM79C875_ICR, 0x00ff), NULL }, /* enable interrupts */
			     {mk_mii_write(MII_REG_CR, PHY_BMCR_RST_NEG), NULL},	/* autonegotiate */
			     {mk_mii_end,}
			     },
	(const phy_cmd_t[]) {	/* ack_int */

			     {mk_mii_read(MII_AM79C875_ICR), NULL},
			     {mk_mii_read(MII_REG_SR), mii_parse_sr},
			     {mk_mii_read(MII_REG_ANAR), mii_parse_anar},
			     {mk_mii_read(MII_REG_ANLPAR),
			      mii_parse_Am79C875_pcr},
			     {mk_mii_end,}
			     },
	(const phy_cmd_t[]) {	/* shutdown - nothing */
			     {mk_mii_end,}
			     },
};

static phy_info_t phy_info_Am79C875A = {
	0x00225541,
	"Am79c875A",
	0,
	(const phy_cmd_t[]) {	/* config */

			     /* parse cr and anar to get some info */

			     {mk_mii_read(MII_REG_CR), mii_parse_cr},
			     {mk_mii_read(MII_REG_ANAR), mii_parse_anar},
			     {mk_mii_end,}
			     },
	(const phy_cmd_t[]) {	/* startup - enable interrupts */
			     {mk_mii_write(MII_REG_CR, PHY_BMCR_AUTON), NULL},	/* Auto neg. on */
//              { mk_mii_write(MII_AM79C875_MFR, 0x4000), NULL}, /* int 1 to signle interrupt */
//              { mk_mii_write(MII_AM79C875_ICR, 0x00ff), NULL }, /* enable interrupts */
			     {mk_mii_write(MII_REG_CR, PHY_BMCR_RST_NEG), NULL},	/* autonegotiate */
			     {mk_mii_read(MII_REG_ANLPAR),
			      mii_parse_Am79C875_pcr},
			     {mk_mii_end,}
			     },
	(const phy_cmd_t[]) {	/* ack_int */

			     {mk_mii_read(MII_AM79C875_ICR), NULL},
			     {mk_mii_read(MII_REG_SR), mii_parse_sr},
			     {mk_mii_read(MII_REG_ANAR), mii_parse_anar},
			     {mk_mii_read(MII_REG_ANLPAR),
			      mii_parse_Am79C875_pcr},
			     {mk_mii_end,}
			     },
	(const phy_cmd_t[]) {	/* shutdown - nothing */
			     {mk_mii_end,}
			     },
};

/* ------------------------------------------------------------------------- */
/* The Broadcom BCM5221 */

/* register definitions */

#define MII_BCM5221_IER 0x1a
#define MII_BCM5221_ISR 0x1a
#define MII_BCM5221_SR  0x19
#define MII_BCM5221_CSR 0x18

void
mii_parse_bcm5221_sr(uint mii_reg, struct net_device *dev)
{
	struct ocp_enet_private *fep = dev->priv;
	volatile uint *s = &(fep->phy_status);

	*s &= ~(PHY_STAT_LINK | PHY_STAT_FAULT | PHY_STAT_ANC);

	if (mii_reg & 0x0004)
		*s |= PHY_STAT_LINK;
	if (mii_reg & 0x0040)
		*s |= PHY_STAT_FAULT;
	if (mii_reg & 0x8000)
		*s |= PHY_STAT_ANC;
}

void
mii_parse_bcm5221_csr(uint mii_reg, struct net_device *dev)
{
	struct ocp_enet_private *fep = dev->priv;
	volatile uint *s = &(fep->phy_status);

	*s &= ~(PHY_STAT_SPMASK);

	if (mii_reg & 0x0002) {
		if (mii_reg & 0x0001)
			*s |= PHY_STAT_100FDX;
		else
			*s |= PHY_STAT_100HDX;
	} else {
		if (mii_reg & 0x0001)
			*s |= PHY_STAT_10FDX;
		else
			*s |= PHY_STAT_10HDX;
	}
}

static phy_info_t phy_info_bcm5221 = {
	0x0004061e,
	"BCM5221",
	4,
	(const phy_cmd_t[]) {	/* config */
			     {mk_mii_write(MII_REG_CR, 0x8000), NULL},	/* reset */
			     {mk_mii_read(MII_BCM5221_SR), NULL},
			     {mk_mii_read(MII_BCM5221_ISR), NULL},
			     {mk_mii_read(MII_BCM5221_CSR), NULL},

			     {mk_mii_read(MII_REG_CR), mii_parse_cr},
			     {mk_mii_read(MII_REG_ANAR), mii_parse_anar},
			     {mk_mii_end,}
			     },
	(const phy_cmd_t[]) {	/* startup - enable interrupts */
			     {mk_mii_read(MII_BCM5221_SR), NULL},
			     {mk_mii_read(MII_BCM5221_ISR), NULL},
			     {mk_mii_read(MII_BCM5221_CSR), NULL},

#if 0
			     {mk_mii_write(MII_BCM5221_IER, 0x4000), NULL},
#endif
			     {mk_mii_write(MII_REG_CR, 0x1200), NULL},	/*
									   autonegotiate */
			     {mk_mii_read(MII_BCM5221_SR),
			      mii_parse_bcm5221_sr},
			     {mk_mii_read(MII_BCM5221_SR),
			      mii_parse_bcm5221_sr},
			     {mk_mii_read(MII_BCM5221_CSR),
			      mii_parse_bcm5221_csr},

			     {mk_mii_end,}
			     },
	(const phy_cmd_t[]) {	/* ack_int */
			     /* read SR and ISR to acknowledge */
			     {mk_mii_read(MII_BCM5221_SR),
			      mii_parse_bcm5221_sr},
			     {mk_mii_read(MII_BCM5221_SR),
			      mii_parse_bcm5221_sr},
			     {mk_mii_read(MII_BCM5221_ISR), NULL},
			     /* find out the current status */
			     {mk_mii_read(MII_BCM5221_CSR),
			      mii_parse_bcm5221_csr},
			     {mk_mii_end,}
			     },
	(const phy_cmd_t[]) {	/* shutdown - disable interrupts */
			     {mk_mii_write(MII_BCM5221_IER, 0x0000), NULL},
			     {mk_mii_end,}
			     },
};

/* ------------------------------------------------------------------------- */
/* The AMD Am79C874 NetPHY-1LP same as AC101                                 */
/* This is a hackish copy of the 75 right now. It works for now.             */
/* It has 100FX support that I have not been able to add/test yet            */
/*                                                                Andrew May */
/* Using the same register definitions same as Am79c875*/

static void
mii_parse_Am79C874_pcr(uint mii_reg, struct net_device *dev)
{
	struct ocp_enet_private *fep = dev->priv;
	volatile uint *s = &(fep->phy_status);

	*s &= ~(PHY_CONF_SPMASK);
	mii_reg = mii_reg >> 5;
	if (mii_reg & 0x10) {
		/*100Base-T4 this phy doesn't support this */
	}
	/*
	 * pick the best one since a partner can advertise all
	 * anyone want 10FD over 100HD ?
	 */
	if (mii_reg & 0x08) {
		*s |= PHY_STAT_100FDX;
	} else if (mii_reg & 0x04) {
		*s |= PHY_STAT_100HDX;
	} else if (mii_reg & 0x02) {
		*s |= PHY_STAT_10FDX;
	} else if (mii_reg & 0x01) {
		*s |= PHY_STAT_10HDX;
	} else {
		*s |= PHY_STAT_10HDX;
	}
}

static phy_info_t phy_info_Am79C874 = {
	0x0022561b,
	"Am79c874",
	0,
	(const phy_cmd_t[]) {	/* config */

			     /* parse cr and anar to get some info */

			     {mk_mii_read(MII_REG_CR), mii_parse_cr},
			     {mk_mii_read(MII_REG_ANAR), mii_parse_anar},
			     {mk_mii_end,}
			     },
	(const phy_cmd_t[]) {	/* startup - enable interrupts */
			     {mk_mii_write(MII_REG_CR, PHY_BMCR_AUTON), NULL},	/* Auto neg. on */
/*{ mk_mii_write(MII_AM79C875_MFR, 0x4000), NULL}, *//* int 1 to signle interrupt */
/*{ mk_mii_write(MII_AM79C875_ICR, 0x00ff), NULL }, *//* enable interrupts */
			     {mk_mii_write(MII_REG_CR, PHY_BMCR_RST_NEG), NULL},	/* autonegotiate */
			     {mk_mii_end,}
			     },
	(const phy_cmd_t[]) {	/* ack_int */

			     {mk_mii_read(MII_AM79C875_ICR), NULL},
			     {mk_mii_read(MII_REG_SR), mii_parse_sr},
			     {mk_mii_read(MII_REG_ANAR), mii_parse_anar},
			     {mk_mii_read(MII_REG_ANLPAR),
			      mii_parse_Am79C874_pcr},
			     {mk_mii_end,}
			     },
	(const phy_cmd_t[]) {	/* shutdown - nothing */
			     {mk_mii_end,}
			     },
};

/* ------------------------------------------------------------------------- */

static phy_info_t *phy_info[] = {

	&phy_info_dp83843,
	&phy_info_lxt971a,
	&phy_info_cs8952,
	&phy_info_dp83846A,
	&phy_info_lu3x31ft,
	&phy_info_Am79C875,
	&phy_info_bcm5221,
	&phy_info_Am79C874,
	&phy_info_Am79C875A,
	NULL
};

void
mii_display_status(struct net_device *dev)
{
	struct ocp_enet_private *fep = dev->priv;
	volatile uint *s = &(fep->phy_status);
	fep->phy_speed = _10BASET;
	fep->phy_duplex = HALF;

	/* Link is still down - don't print anything */
	if (!fep->link && !fep->old_link)
		return;

	printk("IBM EMAC: %s: ", dev->name);

	if (!fep->link)
		printk("link down");
	else {
		printk("link up");

		switch (*s & PHY_STAT_SPMASK) {
		case PHY_STAT_100FDX:
			printk(", 100 Mbps FDX");
			fep->phy_speed = _100BASET;
			fep->phy_duplex = FULL;
			break;
		case PHY_STAT_100HDX:
			printk(", 100 Mbps HDX");
			fep->phy_speed = _100BASET;
			break;
		case PHY_STAT_10FDX:
			printk(", 10 Mbps FDX");
			fep->phy_duplex = FULL;
			break;
		case PHY_STAT_10HDX:
			printk(", 10 Mbps HDX");
			break;
		default:
			printk(", Unknown speed/duplex");
		}

		if (*s & PHY_STAT_ANC)
			printk(", auto-negotiation complete");
		printk(".\n");
	}

	if (*s & PHY_STAT_FAULT)
		printk(", remote fault.\n");
}

/*
 * Check if there is a valid PHY connected at address phnum.
 */
static int check_phy(struct net_device *dev, int phnum)
{
	struct ocp_enet_private *fep = dev->priv;
	uint phy_reg;
	int i;

	fep->phy_addr = phnum;
	if (ocp_enet_mdio_read(dev, mk_mii_read(MII_REG_PHYIR1), &phy_reg))
		return 0;

	ppc405_phy_dump(dev);

	/* Got 2nd part of ID, now get remainder. */
	fep->phy_id = (phy_reg & 0xffff) << 16;

	if (ocp_enet_mdio_read(dev, mk_mii_read(MII_REG_PHYIR2), &phy_reg)) {
		if (phnum == MIN_PHY_ADDR)
			printk(KERN_ERR "%s: Got bad Phy Read, missing MDIO pullup?\n",
			       dev->name);
		return 0;
	}

	fep->phy_id |= (phy_reg & 0xffff);

	for (i = 0; phy_info[i]; i++)
		if (phy_info[i]->id == (fep->phy_id >> phy_info[i]->shift))
			break;
	if (!phy_info[i]) {
		printk(KERN_ERR "%s: PHY id 0x%08x is not supported!\n",
		       dev->name, fep->phy_id);
		return 0;
	}

	fep->phy = phy_info[i];

	printk("IBM EMAC: %s: Phy @ 0x%x, type %s (0x%08x)\n",
	       dev->name, fep->phy_addr, fep->phy->name, fep->phy_id);
	return 1;
}

/* Scan all valid PHY addresses looking for someone to respond
 * with a valid ID.  This usually happens quickly. This eliminated
 * the need to pass in the addr & PHY name
 */
void
find_phy(struct net_device *dev)
{
	int i;

	for (i = next_phy_available; i <= MAX_PHY_ADDR; i++)
		if (check_phy(dev, i)){
			next_phy_available = i + 1;
			return;
		}
	printk("%s: No PHY device found.\n", dev->name);
}
/*	this decreaments the next_phy_available 
 *	This is a temp work arround
 *	armin
 */
int
free_phy(struct net_device *dev)
{
	if (next_phy_available-- == MIN_PHY_ADDR)
		next_phy_available =  MIN_PHY_ADDR;
	return(next_phy_available);

}
