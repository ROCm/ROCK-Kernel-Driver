
/*
 * ibm_ocp_phy.h
 *
 *
 *      Armin Kuster akuster@mvista.com
 *      June, 2002
 *
 * Copyright 2002 MontaVista Softare Inc.
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
 *  Version: 1.0: armin
 *  moved phy defines out of enet.h
 *
 */

#ifndef _IBM_OCP_PHY_H_
#define _IBM_OCP_PHY_H_

#define mk_mii_end			0

typedef struct mii_list {
	uint mii_regval;
	void (*mii_func) (uint val, struct net_device * dev);
	struct mii_list *mii_next;
} mii_list_t;

typedef struct {
	uint mii_data;
	void (*funct) (uint mii_reg, struct net_device * dev);
} phy_cmd_t;

typedef struct {
	uint id;
	char *name;
	uint shift;
	const phy_cmd_t *config;
	const phy_cmd_t *startup;
	const phy_cmd_t *ack_int;
	const phy_cmd_t *shutdown;
} phy_info_t;

extern void process_mii_queue(struct net_device *dev);
extern void mii_do_cmd(struct net_device *dev, const phy_cmd_t * c);
extern void mii_display_status(struct net_device *dev);
extern void find_phy(struct net_device *dev);
extern int free_phy(struct net_device *dev);
extern mii_list_t *mii_free;
extern mii_list_t *mii_nead;
extern mii_list_t *mii_tail;

/* phy register offsets */
#define PHY_BMCR			0x00
#define PHY_BMS				0x01
#define PHY_PHY1DR1			0x02
#define PHY_PHYIDR2			0x03
#define PHY_ANAR			0x04
#define PHY_ANLPAR			0x05
#define PHY_ANER			0x06
#define PHY_ANNPTR			0x07
#define PHY_PHYSTS			0x10
#define PHY_MIPSCR			0x11
#define PHY_MIPGSR			0x12
#define PHY_DCR				0x13
#define PHY_FCSCR			0x14
#define PHY_RECR			0x15
#define PHY_PCSR			0x16
#define PHY_LBR				0x17
#define PHY_10BTSCR			0x18
#define PHY_PHYCTRL			0x19

/* PHY BMCR */
#define PHY_BMCR_RESET			0x8000
#define PHY_BMCR_LOOP			0x4000
#define PHY_BMCR_100MB			0x2000
#define PHY_BMCR_AUTON			0x1000
#define PHY_BMCR_POWD			0x0800
#define PHY_BMCR_ISO			0x0400
#define PHY_BMCR_RST_NEG		0x0200
#define PHY_BMCR_DPLX			0x0100
#define PHY_BMCR_COL_TST		0x0080

/* phy BMSR */
#define PHY_BMSR_100T4			0x8000
#define PHY_BMSR_100TXF			0x4000
#define PHY_BMSR_100TXH			0x2000
#define PHY_BMSR_10TF			0x1000
#define PHY_BMSR_10TH			0x0800
#define PHY_BMSR_PRE_SUP		0x0040
#define PHY_BMSR_AUTN_COMP		0x0020
#define PHY_BMSR_RF			0x0010
#define PHY_BMSR_AUTN_ABLE		0x0008
#define PHY_BMSR_LS			0x0004
#define PHY_BMSR_JD			0x0002
#define PHY_BMSR_EXT			0x0001

/* phy ANAR */
#define PHY_ANAR_NP			0x8000	/* Next page indication */
#define PHY_ANAR_RF			0x2000	/* Remote Fault */
#define PHY_ANAR_FDFC			0x0400	/* Full Duplex control */
#define PHY_ANAR_T4			0x0200	/* 100BASE-T4 supported */
#define PHY_ANAR_TX_FD			0x0100	/* 100BASE-TX Full duplex supported */
#define PHY_ANAR_TX			0x0080	/* 100BASE-TX supported */
#define PHY_ANAR_10_FD			0x0040	/* 10BASE-T Full duplex supported */
#define PHY_ANAR_10			0x0020	/* 10BASE-T Supported */
#define PHY_ANAR_SEL			0x0010	/* Protocol selection bits  */

/* phy ANLPAR */
#define PHY_ANLPAR_NP			0x8000
#define PHY_ANLPAR_ACK			0x4000
#define PHY_ANLPAR_RF			0x2000
#define PHY_ANLPAR_T4			0x0200
#define PHY_ANLPAR_TXFD			0x0100
#define PHY_ANLPAR_TX			0x0080
#define PHY_ANLPAR_10FD			0x0040
#define PHY_ANLPAR_10			0x0020
#define PHY_ANLPAR_100			0x0380	/* we can run at 100 */

/* phy status PHYSTS */

#define PHY_PHYSTS_RLE			0x8000	/* Receive error latch 1: rx error */
#define PHY_PHYSTS_CIM			0x4000	/* Carrier Integrity 1: False carrier */
#define PHY_PHYSTS_FC 			0x2000	/* False carrier 1: false carrier */
#define PHY_PHYSTS_DR 			0x0800	/* Device ready 1: ready 0: not */
#define PHY_PHYSTS_PR 			0x0400	/* Page received 1: new page code */
#define PHY_PHYSTS_AN 			0x0200	/* Auto Negociate Enabled 1: enabled 0: disabled  */
#define PHY_PHYSTS_MI 			0x0100	/* MII interrupt pending */
#define PHY_PHYSTS_RF 			0x0080	/* Remote fault 1: fault 0: no falut */
#define PHY_PHYSTS_JD 			0x0040	/* Jabber detect 1:jabber 0: no jabber */
#define PHY_PHYSTS_NWC			0x0020	/* Auto negociate complete 1: done 0:not */
#define PHY_PHYSTS_RS	 		0x0010	/* Reset Status 1: in progress 0: normal */
#define PHY_PHYSTS_LBS			0x0008	/* Loopback 1:LB enabled 0:disabled */
#define PHY_PHYSTS_DS 			0x0004	/* Duplex status 1:FD 0: HD */
#define PHY_PHYSTS_SS 			0x0002	/* Speed status 1:10 0:100 */
#define PHY_PHYSTS_LS 			0x0001	/* Link status 1: valid 0: no link */

#endif				/* _IBM_OCP_PHY_H_ */
