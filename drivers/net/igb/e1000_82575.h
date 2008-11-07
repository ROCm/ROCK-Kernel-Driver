/*******************************************************************************

  Intel(R) Gigabit Ethernet Linux driver
  Copyright(c) 2007 - 2008 Intel Corporation.

  This program is free software; you can redistribute it and/or modify it
  under the terms and conditions of the GNU General Public License,
  version 2, as published by the Free Software Foundation.

  This program is distributed in the hope it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
  more details.

  You should have received a copy of the GNU General Public License along with
  this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.

  The full GNU General Public License is included in this distribution in
  the file called "COPYING".

  Contact Information:
  e1000-devel Mailing List <e1000-devel@lists.sourceforge.net>
  Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497

*******************************************************************************/

#ifndef _E1000_82575_H_
#define _E1000_82575_H_

void igb_update_mc_addr_list_82575(struct e1000_hw*, u8*, u32, u32, u32);
extern void igb_shutdown_fiber_serdes_link_82575(struct e1000_hw *hw);
extern void igb_rx_fifo_flush_82575(struct e1000_hw *hw);

#define E1000_RAR_ENTRIES_82575   16
#define E1000_RAR_ENTRIES_82576   24

/* SRRCTL bit definitions */
#define E1000_SRRCTL_BSIZEPKT_SHIFT                     10 /* Shift _right_ */
#define E1000_SRRCTL_BSIZEHDRSIZE_SHIFT                 2  /* Shift _left_ */
#define E1000_SRRCTL_DESCTYPE_ADV_ONEBUF                0x02000000
#define E1000_SRRCTL_DESCTYPE_HDR_SPLIT_ALWAYS          0x0A000000

#define E1000_MRQC_ENABLE_RSS_4Q            0x00000002
#define E1000_MRQC_RSS_FIELD_IPV4_UDP       0x00400000
#define E1000_MRQC_RSS_FIELD_IPV6_UDP       0x00800000
#define E1000_MRQC_RSS_FIELD_IPV6_UDP_EX    0x01000000

#define E1000_EICR_TX_QUEUE ( \
    E1000_EICR_TX_QUEUE0 |    \
    E1000_EICR_TX_QUEUE1 |    \
    E1000_EICR_TX_QUEUE2 |    \
    E1000_EICR_TX_QUEUE3)

#define E1000_EICR_RX_QUEUE ( \
    E1000_EICR_RX_QUEUE0 |    \
    E1000_EICR_RX_QUEUE1 |    \
    E1000_EICR_RX_QUEUE2 |    \
    E1000_EICR_RX_QUEUE3)

#define E1000_EIMS_RX_QUEUE E1000_EICR_RX_QUEUE
#define E1000_EIMS_TX_QUEUE E1000_EICR_TX_QUEUE

/* Immediate Interrupt Rx (A.K.A. Low Latency Interrupt) */

/* Receive Descriptor - Advanced */
union e1000_adv_rx_desc {
	struct {
		__le64 pkt_addr;             /* Packet buffer address */
		__le64 hdr_addr;             /* Header buffer address */
	} read;
	struct {
		struct {
			struct {
				__le16 pkt_info;   /* RSS type, Packet type */
				__le16 hdr_info;   /* Split Header,
						    * header buffer length */
			} lo_dword;
			union {
				__le32 rss;          /* RSS Hash */
				struct {
					__le16 ip_id;    /* IP id */
					__le16 csum;     /* Packet Checksum */
				} csum_ip;
			} hi_dword;
		} lower;
		struct {
			__le32 status_error;     /* ext status/error */
			__le16 length;           /* Packet length */
			__le16 vlan;             /* VLAN tag */
		} upper;
	} wb;  /* writeback */
};

#define E1000_RXDADV_HDRBUFLEN_MASK      0x7FE0
#define E1000_RXDADV_HDRBUFLEN_SHIFT     5

/* RSS Hash results */

/* RSS Packet Types as indicated in the receive descriptor */
#define E1000_RXDADV_PKTTYPE_IPV4        0x00000010 /* IPV4 hdr present */
#define E1000_RXDADV_PKTTYPE_TCP         0x00000100 /* TCP hdr present */

/* Transmit Descriptor - Advanced */
union e1000_adv_tx_desc {
	struct {
		__le64 buffer_addr;    /* Address of descriptor's data buf */
		__le32 cmd_type_len;
		__le32 olinfo_status;
	} read;
	struct {
		__le64 rsvd;       /* Reserved */
		__le32 nxtseq_seed;
		__le32 status;
	} wb;
};

/* Adv Transmit Descriptor Config Masks */
#define E1000_ADVTXD_DTYP_CTXT    0x00200000 /* Advanced Context Descriptor */
#define E1000_ADVTXD_DTYP_DATA    0x00300000 /* Advanced Data Descriptor */
#define E1000_ADVTXD_DCMD_IFCS    0x02000000 /* Insert FCS (Ethernet CRC) */
#define E1000_ADVTXD_DCMD_DEXT    0x20000000 /* Descriptor extension (1=Adv) */
#define E1000_ADVTXD_DCMD_VLE     0x40000000 /* VLAN pkt enable */
#define E1000_ADVTXD_DCMD_TSE     0x80000000 /* TCP Seg enable */
#define E1000_ADVTXD_PAYLEN_SHIFT    14 /* Adv desc PAYLEN shift */

/* Context descriptors */
struct e1000_adv_tx_context_desc {
	__le32 vlan_macip_lens;
	__le32 seqnum_seed;
	__le32 type_tucmd_mlhl;
	__le32 mss_l4len_idx;
};

#define E1000_ADVTXD_MACLEN_SHIFT    9  /* Adv ctxt desc mac len shift */
#define E1000_ADVTXD_TUCMD_IPV4    0x00000400  /* IP Packet Type: 1=IPv4 */
#define E1000_ADVTXD_TUCMD_L4T_TCP 0x00000800  /* L4 Packet TYPE of TCP */
/* IPSec Encrypt Enable for ESP */
#define E1000_ADVTXD_L4LEN_SHIFT     8  /* Adv ctxt L4LEN shift */
#define E1000_ADVTXD_MSS_SHIFT      16  /* Adv ctxt MSS shift */
/* Adv ctxt IPSec SA IDX mask */
/* Adv ctxt IPSec ESP len mask */

/* Additional Transmit Descriptor Control definitions */
#define E1000_TXDCTL_QUEUE_ENABLE  0x02000000 /* Enable specific Tx Queue */
/* Tx Queue Arbitration Priority 0=low, 1=high */

/* Additional Receive Descriptor Control definitions */
#define E1000_RXDCTL_QUEUE_ENABLE  0x02000000 /* Enable specific Rx Queue */

/* Direct Cache Access (DCA) definitions */
#define E1000_DCA_CTRL_DCA_ENABLE  0x00000000 /* DCA Enable */
#define E1000_DCA_CTRL_DCA_DISABLE 0x00000001 /* DCA Disable */

#define E1000_DCA_CTRL_DCA_MODE_CB1 0x00 /* DCA Mode CB1 */
#define E1000_DCA_CTRL_DCA_MODE_CB2 0x02 /* DCA Mode CB2 */

#define E1000_DCA_RXCTRL_CPUID_MASK 0x0000001F /* Rx CPUID Mask */
#define E1000_DCA_RXCTRL_DESC_DCA_EN (1 << 5) /* DCA Rx Desc enable */
#define E1000_DCA_RXCTRL_HEAD_DCA_EN (1 << 6) /* DCA Rx Desc header enable */
#define E1000_DCA_RXCTRL_DATA_DCA_EN (1 << 7) /* DCA Rx Desc payload enable */

#define E1000_DCA_TXCTRL_CPUID_MASK 0x0000001F /* Tx CPUID Mask */
#define E1000_DCA_TXCTRL_DESC_DCA_EN (1 << 5) /* DCA Tx Desc enable */
#define E1000_DCA_TXCTRL_TX_WB_RO_EN (1 << 11) /* Tx Desc writeback RO bit */

/* Additional DCA related definitions, note change in position of CPUID */
#define E1000_DCA_TXCTRL_CPUID_MASK_82576 0xFF000000 /* Tx CPUID Mask */
#define E1000_DCA_RXCTRL_CPUID_MASK_82576 0xFF000000 /* Rx CPUID Mask */
#define E1000_DCA_TXCTRL_CPUID_SHIFT 24 /* Tx CPUID now in the last byte */
#define E1000_DCA_RXCTRL_CPUID_SHIFT 24 /* Rx CPUID now in the last byte */

#define MAX_NUM_VFS                   8

#define E1000_DTXSWC_MAC_SPOOF_MASK   0x000000FF /* Per VF MAC spoof control */
#define E1000_DTXSWC_VLAN_SPOOF_MASK  0x0000FF00 /* Per VF VLAN spoof control */
#define E1000_DTXSWC_LLE_MASK         0x00FF0000 /* Per VF Local LB enables */
#define E1000_DTXSWC_VMDQ_LOOPBACK_EN (1 << 31)  /* global VF LB enable */

/* Easy defines for setting default pool, would normally be left a zero */
#define E1000_VT_CTL_DEFAULT_POOL_SHIFT 7
#define E1000_VT_CTL_DEFAULT_POOL_MASK  (0x7 << E1000_VT_CTL_DEFAULT_POOL_SHIFT)

/* Other useful VMD_CTL register defines */
#define E1000_VT_CTL_IGNORE_MAC         (1 << 28)
#define E1000_VT_CTL_DISABLE_DEF_POOL   (1 << 29)
#define E1000_VT_CTL_VM_REPL_EN         (1 << 30)

/* Per VM Offload register setup */
#define E1000_VMOLR_LPE        0x00010000 /* Accept Long packet */
#define E1000_VMOLR_AUPE       0x01000000 /* Accept untagged packets */
#define E1000_VMOLR_BAM        0x08000000 /* Accept Broadcast packets */
#define E1000_VMOLR_MPME       0x10000000 /* Multicast promiscuous mode */
#define E1000_VMOLR_STRVLAN    0x40000000 /* Vlan stripping enable */

#define E1000_V2PMAILBOX_REQ   0x00000001 /* Request for PF Ready bit */
#define E1000_V2PMAILBOX_ACK   0x00000002 /* Ack PF message received */
#define E1000_V2PMAILBOX_VFU   0x00000004 /* VF owns the mailbox buffer */
#define E1000_V2PMAILBOX_PFU   0x00000008 /* PF owns the mailbox buffer */
#define E1000_V2PMAILBOX_PFSTS 0x00000010 /* PF wrote a message in the MB */
#define E1000_V2PMAILBOX_PFACK 0x00000020 /* PF ack the previous VF msg */
#define E1000_V2PMAILBOX_RSTI  0x00000040 /* PF has reset indication */

#define E1000_P2VMAILBOX_STS   0x00000001 /* Initiate message send to VF */
#define E1000_P2VMAILBOX_ACK   0x00000002 /* Ack message recv'd from VF */
#define E1000_P2VMAILBOX_VFU   0x00000004 /* VF owns the mailbox buffer */
#define E1000_P2VMAILBOX_PFU   0x00000008 /* PF owns the mailbox buffer */
#define E1000_P2VMAILBOX_RVFU  0x00000010 /* Reset VFU - used when VF stuck */

#define E1000_VFMAILBOX_SIZE   16 /* 16 32 bit words - 64 bytes */

/* If it's a E1000_VF_* msg then it originates in the VF and is sent to the
 * PF.  The reverse is true if it is E1000_PF_*.
 * Message ACK's are the value or'd with 0xF0000000
 */
#define E1000_VT_MSGTYPE_ACK      0xF0000000  /* Messages below or'd with
                                               * this are the ACK */
#define E1000_VT_MSGTYPE_NACK     0xFF000000  /* Messages below or'd with
                                               * this are the NACK */
#define E1000_VT_MSGINFO_SHIFT    16
/* bits 23:16 are used for exra info for certain messages */
#define E1000_VT_MSGINFO_MASK     (0xFF << E1000_VT_MSGINFO_SHIFT)

#define E1000_VF_MSGTYPE_REQ_MAC  1 /* VF needs to know its MAC */
#define E1000_VF_MSGTYPE_VFLR     2 /* VF notifies VFLR to PF */
#define E1000_VF_SET_MULTICAST    3 /* VF requests PF to set MC addr */
#define E1000_VF_SET_VLAN         4 /* VF requests PF to set VLAN */
#define E1000_VF_SET_LPE          5 /* VF requests PF to set VMOLR.LPE */

/* Add 100h to all PF msgs, leaves room for up to 255 discrete message types
 * from VF to PF - way more than we'll ever need */
#define E1000_PF_MSGTYPE_RESET    (1 + 0x100) /* PF notifies global reset
                                               * imminent to VF */
#define E1000_PF_MSGTYPE_LSC      (2 + 0x100) /* PF notifies VF of LSC... VF
                                               * will see extra msg info for
                                               * status */

#define E1000_PF_MSG_LSCDOWN      (1 << E1000_VT_MSGINFO_SHIFT)
#define E1000_PF_MSG_LSCUP        (2 << E1000_VT_MSGINFO_SHIFT)

#define ALL_QUEUES   0xFFFF

s32  e1000_send_mail_to_pf_vf(struct e1000_hw *hw, u32 *msg,
                              s16 size);
s32  e1000_receive_mail_from_pf_vf(struct e1000_hw *hw,
                                   u32 *msg, s16 size);
s32  e1000_send_mail_to_vf(struct e1000_hw *hw, u32 *msg,
                           u32 vf_number, s16 size);
s32  e1000_receive_mail_from_vf(struct e1000_hw *hw, u32 *msg,
                                u32 vf_number, s16 size);
void e1000_vmdq_loopback_enable_vf(struct e1000_hw *hw);
void e1000_vmdq_loopback_disable_vf(struct e1000_hw *hw);
void e1000_vmdq_replication_enable_vf(struct e1000_hw *hw, u32 enables);
void e1000_vmdq_replication_disable_vf(struct e1000_hw *hw);
void e1000_vmdq_enable_replication_mode_vf(struct e1000_hw *hw);
void e1000_vmdq_broadcast_replication_enable_vf(struct e1000_hw *hw,
						u32 enables);
void e1000_vmdq_multicast_replication_enable_vf(struct e1000_hw *hw,
						u32 enables);
void e1000_vmdq_broadcast_replication_disable_vf(struct e1000_hw *hw,
						u32 disables);
void e1000_vmdq_multicast_replication_disable_vf(struct e1000_hw *hw,
						u32 disables);
bool e1000_check_for_pf_ack_vf(struct e1000_hw *hw);

bool e1000_check_for_pf_mail_vf(struct e1000_hw *hw, u32*);
#endif
