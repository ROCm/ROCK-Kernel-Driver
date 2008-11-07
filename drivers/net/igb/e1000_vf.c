/*******************************************************************************

  Intel(R) Gigabit Ethernet Linux driver
  Copyright(c) 2007-2008 Intel Corporation.

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


#include <linux/types.h>
#include <linux/slab.h>
#include <linux/if_ether.h>

#include "e1000_mac.h"
#include "e1000_82575.h"

/**
 *  e1000_update_mc_addr_list_vf - Update Multicast addresses
 *  @hw: pointer to the HW structure
 *  @mc_addr_list: array of multicast addresses to program
 *  @mc_addr_count: number of multicast addresses to program
 *  @rar_used_count: the first RAR register free to program
 *  @rar_count: total number of supported Receive Address Registers
 *
 *  Updates the Receive Address Registers and Multicast Table Array.
 *  The caller must have a packed mc_addr_list of multicast addresses.
 *  The parameter rar_count will usually be hw->mac.rar_entry_count
 *  unless there are workarounds that change this.
 **/
void e1000_update_mc_addr_list_vf(struct e1000_hw *hw,
                                  u8 *mc_addr_list, u32 mc_addr_count,
                                  u32 rar_used_count, u32 rar_count)
{
	u32 msgbuf[E1000_VFMAILBOX_SIZE];
	u32 cnt;
	u32 i;
	u8 *p;

	/* Each entry in the list uses 1.5 32 bit words.  We have 15
	 * 32 bit words available in our HW msg buffer (minus 1 for the
	 * msg type).  That's 10 addresses if we pack 'em right.  If
	 * there are more than 10 MC addresses to add then punt the
	 * extras for now and then add code to handle more than 10 later.
	 * It would be unusual for a server to request that many multi-cast
	 * addresses except for in large enterprise network environments.
	 */

	cnt = (mc_addr_count > 10) ? 10 : mc_addr_count;
	msgbuf[0] = E1000_VF_SET_MULTICAST;
	msgbuf[0] |= cnt << E1000_VT_MSGINFO_SHIFT;
	p = (u8 *)&msgbuf[1];

	for (i = 0; i < cnt; i++) {
		memcpy(p, &mc_addr_list[i * 6], 6);
		p += 6;
	}

	e1000_send_mail_to_pf_vf(hw, msgbuf, E1000_VFMAILBOX_SIZE);
}

/**
 *  e1000_send_mail_to_pf_vf - Sends a mailbox message from VF to PF
 *  @hw: pointer to the HW structure
 *  @msg: The message buffer
 *  @size: Length of buffer
 **/
s32 e1000_send_mail_to_pf_vf(struct e1000_hw *hw, u32 *msg, s16 size)
{
	u32 v2p_mailbox = rd32(E1000_V2PMAILBOX(0));
	s32 ret_val = 0;
	s16 i;

	/*
	 * What happens if you enter this function and PFSTS is set?  From
	 * my thinking it should be an error, but obviously that means an
	 * event was missed and the message should be stored somewhere.
	 * but where?  Or maybe it should just be dropped on the floor?
	 * I'll have to think 'pon it some more
	 */

	/*
	 * if any of the indicated bits are set then there has been a
	 * programming error or the PF has been reset underneath the feet
	 * of the VF
	 */
	if (v2p_mailbox & (E1000_V2PMAILBOX_PFU |
	                   E1000_V2PMAILBOX_PFSTS |
	                   E1000_V2PMAILBOX_RSTI)) {
		ret_val = -1;
		goto out;
	} else {
		/* Take ownership of the buffer */
		v2p_mailbox |= E1000_V2PMAILBOX_VFU;
		wr32(E1000_V2PMAILBOX(0), v2p_mailbox);
		/* Make sure we have ownership now... */
		if (v2p_mailbox & E1000_V2PMAILBOX_PFU) {
			/*
			 * oops, PF grabbed ownership while we were attempting
			 * to take ownership - avoid the race condition
			 */
			ret_val = -1;
			goto out;
		}
	}

	/*
	 * At this point we have established ownership of the VF mailbox memory
	 * buffer.  IT IS IMPORTANT THAT THIS OWNERSHIP BE GIVEN UP!  Whether
	 * success or failure, the VF ownership bit must be cleared before
	 * exiting this function - so if you change this function keep that
	 * in mind
	 */

	/* check for overflow */
	if (size > E1000_VFMAILBOX_SIZE) {
		ret_val = -1;
		goto out;
	}

	/*
	 * copy the caller specified message to the mailbox
	 * memory buffer
	 */
	for (i = 0; i < size; i++) {
		wr32(E1000_VFVMBMEM(i * 4), msg[i]);
	}

	/* Interrupt the PF to tell it a message has been sent */
	/*
	 * The PF request bit is write only and will be read back
	 * as zero on the next register read below - thus no need
	 * to programmatically clear it
	 */
	v2p_mailbox |= E1000_V2PMAILBOX_REQ;
	wr32(E1000_V2PMAILBOX(0), v2p_mailbox);

out:
	/* Clear the VF mail box memory buffer ownership bit */
	v2p_mailbox &= ~(E1000_V2PMAILBOX_VFU | E1000_V2PMAILBOX_REQ);

	/* Clear VF ownership of the mail box memory buffer */
	wr32(E1000_V2PMAILBOX(0), v2p_mailbox);

	return ret_val;
}

/**
 *  e1000_check_for_pf_ack_vf - checks to see if the PF has ACK'd
 *  @hw: pointer to the HW structure
 *
 *  returns true if the PF has set the ACK bit or else false
 **/
bool e1000_check_for_pf_ack_vf(struct e1000_hw *hw)
{
	u32 v2p_mailbox = rd32(E1000_V2PMAILBOX(0));
	bool ret_val = false;

	if ((v2p_mailbox & E1000_V2PMAILBOX_PFACK)) {
		ret_val = true;
	}

	return ret_val;
}

/**
 *  e1000_receive_mail_from_pf_vf - Receives a mailbox message from PF to VF
 *  @hw: pointer to the HW structure
 *  @msg: The message buffer
 *  @size: Length of buffer
 **/
s32 e1000_receive_mail_from_pf_vf(struct e1000_hw *hw, u32 *msg, s16 size)
{
	u32 v2p_mailbox = rd32(E1000_V2PMAILBOX(0));
	s32 ret_val = 0;
	s16 i;

	/*
	 * copy the caller specified message to the mailbox
	 * memory buffer
	 */
	for (i = 0; i < size; i++) {
		msg[i] = rd32(E1000_VFVMBMEM(i * 4));
	}

	/*
	 * Acknowledge receipt of the message to the PF and then
	 * we're done
	 */
	/* clear status indication bit */
	v2p_mailbox &= ~(E1000_V2PMAILBOX_PFSTS | E1000_V2PMAILBOX_VFU);
	v2p_mailbox |= E1000_V2PMAILBOX_ACK;	    /* Set PF Ack bit */
	wr32(E1000_V2PMAILBOX(0), v2p_mailbox);

	return ret_val;
}

/**
 *  e1000_check_for_pf_mail_vf - checks to see if the PF has sent mail
 *  @hw: pointer to the HW structure
 *
 *  returns true if the PF has set the Status bit or else false
 **/
bool e1000_check_for_pf_mail_vf(struct e1000_hw *hw, u32 *vf_mb_val)
{
	u32 v2p_mailbox = rd32(E1000_V2PMAILBOX(0));
	bool ret_val = false;

	if ((v2p_mailbox & E1000_V2PMAILBOX_PFSTS)) {
		ret_val = true;
	}

	*vf_mb_val = v2p_mailbox;

	return ret_val;
}

/**
 *  e1000_send_mail_to_vf - Sends a mailbox message from PF to VF
 *  @hw: pointer to the HW structure
 *  @msg: The message buffer
 *  @vf_number: the VF index
 *  @size: Length of buffer
 **/
s32 e1000_send_mail_to_vf(struct e1000_hw *hw, u32 *msg, u32 vf_number,
                          s16 size)
{
	u32 p2v_mailbox = rd32(E1000_P2VMAILBOX(vf_number));
	s32 ret_val = 0;
	s16 i;

	/*
	 * if the VF owns the mailbox then we can't grab the mailbox buffer
	 * - mostly an indication of a programming error
	 */
	if (p2v_mailbox & E1000_P2VMAILBOX_VFU) {
		ret_val = -1;
		goto out;
	} else {
		/* Take ownership of the buffer */
		p2v_mailbox |= E1000_P2VMAILBOX_PFU;
		wr32(E1000_P2VMAILBOX(vf_number), p2v_mailbox);
		p2v_mailbox = rd32(E1000_P2VMAILBOX(vf_number));
		/* Make sure we have ownership now... */
		if (p2v_mailbox & E1000_P2VMAILBOX_VFU) {
			/*
			 * oops, VF grabbed ownership while we were attempting
			 * to take ownership - avoid the race condition
			 */
			ret_val = -2;
			goto out;
		}
	}

	/*
	 * At this point we have established ownership of the PF mailbox memory
	 * buffer.  IT IS IMPORTANT THAT THIS OWNERSHIP BE GIVEN UP!  Whether
	 * success or failure, the PF ownership bit must be cleared before
	 * exiting this function - so if you change this function keep that
	 * in mind
	 */

	/* check for overflow */
	if (size > E1000_VFMAILBOX_SIZE) {
		ret_val = -3;
		goto out;
	}

	/*
	 * copy the caller specified message to the mailbox
	 * memory buffer
	 */
	for (i = 0; i < size; i++) {
		wr32(((E1000_VMBMEM(vf_number)) + (i * 4)), msg[i]);
	}

	/* Interrupt the VF to tell it a message has been sent */
	/*
	 * The VF request bit is write only and will be read back
	 * as zero on the next register read below - thus no need
	 * to programmatically clear it
	 */
	p2v_mailbox |= E1000_P2VMAILBOX_STS;
	wr32(E1000_P2VMAILBOX(vf_number), p2v_mailbox);


	/* Clear PF ownership of the mail box memory buffer */
	/*
	 * Do this whether success or failure on the wait for ack from
	 * the PF
	 */
	p2v_mailbox &= ~(E1000_P2VMAILBOX_PFU);
	wr32(E1000_P2VMAILBOX(vf_number), p2v_mailbox);

out:
	return ret_val;

}

/**
 *  e1000_receive_mail_from_vf - Receives a mailbox message from VF to PF
 *  @hw: pointer to the HW structure
 *  @msg: The message buffer
 *  @vf_number: the VF index
 *  @size: Length of buffer
 **/
s32 e1000_receive_mail_from_vf(struct e1000_hw *hw,
                               u32 *msg, u32 vf_number, s16 size)
{
	u32 p2v_mailbox = rd32(E1000_P2VMAILBOX(vf_number));
	s16 i;

	/*
	 * Should we be checking if the VF has set the ownership bit?
	 * I don't know... presumably well written software will set the
	 * VF mailbox memory ownership bit but I can't think of a reason
	 * to call it an error if it doesn't... I'll think 'pon it some more
	 */

	/*
	 * No message ready polling mechanism - the presumption is that
	 * the caller knows there is a message because of the interrupt
	 * ack
	 */

	/*
	 * copy the caller specified message to the mailbox
	 * memory buffer
	 */
	for (i = 0; i < size; i++) {
		msg[i] = rd32(((E1000_VMBMEM(vf_number)) + (i * 4)));
	}

	/*
	 * Acknowledge receipt of the message to the VF and then
	 * we're done
	 */
	p2v_mailbox |= E1000_P2VMAILBOX_ACK;	    /* Set PF Ack bit */
	wr32(E1000_P2VMAILBOX(vf_number), p2v_mailbox);

	return 0;	/* Success is the only option  */
}

/**
 *  e1000_vmdq_loopback_enable_vf - Enables VM to VM queue loopback replication
 *  @hw: pointer to the HW structure
 **/
void e1000_vmdq_loopback_enable_vf(struct e1000_hw *hw)
{
	u32 reg;

	reg = rd32(E1000_DTXSWC);
	reg |= E1000_DTXSWC_VMDQ_LOOPBACK_EN;
	wr32(E1000_DTXSWC, reg);
}

/**
 *  e1000_vmdq_loopback_disable_vf - Disable VM to VM queue loopbk replication
 *  @hw: pointer to the HW structure
 **/
void e1000_vmdq_loopback_disable_vf(struct e1000_hw *hw)
{
	u32 reg;

	reg = rd32(E1000_DTXSWC);
	reg &= ~(E1000_DTXSWC_VMDQ_LOOPBACK_EN);
	wr32(E1000_DTXSWC, reg);
}

/**
 *  e1000_vmdq_replication_enable_vf - Enable replication of brdcst & multicst
 *  @hw: pointer to the HW structure
 *
 *  Enables replication of broadcast and multicast packets from the network
 *  to VM's which have their respective broadcast and multicast accept
 *  bits set in the VM Offload Register.  This gives the PF driver per
 *  VM granularity control over which VM's get replicated broadcast traffic.
 **/
void e1000_vmdq_replication_enable_vf(struct e1000_hw *hw, u32 enables)
{
	u32 reg;
	u32 i;

	for (i = 0; i < MAX_NUM_VFS; i++) {
		if (enables & (1 << i)) {
			reg = rd32(E1000_VMOLR(i));
			reg |= (E1000_VMOLR_AUPE |
				E1000_VMOLR_BAM |
				E1000_VMOLR_MPME);
			wr32(E1000_VMOLR(i), reg);
		}
	}

	reg = rd32(E1000_VT_CTL);
	reg |= E1000_VT_CTL_VM_REPL_EN;
	wr32(E1000_VT_CTL, reg);
}

/**
 *  e1000_vmdq_replication_disable_vf - Disable replication of brdcst & multicst
 *  @hw: pointer to the HW structure
 *
 *  Disables replication of broadcast and multicast packets to the VM's.
 **/
void e1000_vmdq_replication_disable_vf(struct e1000_hw *hw)
{
	u32 reg;

	reg = rd32(E1000_VT_CTL);
	reg &= ~(E1000_VT_CTL_VM_REPL_EN);
	wr32(E1000_VT_CTL, reg);
}

/**
 *  e1000_vmdq_enable_replication_mode_vf - Enables replication mode in the device
 *  @hw: pointer to the HW structure
 **/
void e1000_vmdq_enable_replication_mode_vf(struct e1000_hw *hw)
{
	u32 reg;

	reg = rd32(E1000_VT_CTL);
	reg |= E1000_VT_CTL_VM_REPL_EN;
	wr32(E1000_VT_CTL, reg);
}

/**
 *  e1000_vmdq_broadcast_replication_enable_vf - Enable replication of brdcst
 *  @hw: pointer to the HW structure
 *  @enables: PoolSet Bit - if set to ALL_QUEUES, apply to all pools.
 *
 *  Enables replication of broadcast packets from the network
 *  to VM's which have their respective broadcast accept
 *  bits set in the VM Offload Register.  This gives the PF driver per
 *  VM granularity control over which VM's get replicated broadcast traffic.
 **/
void e1000_vmdq_broadcast_replication_enable_vf(struct e1000_hw *hw,
						u32 enables)
{
	u32 reg;
	u32 i;

	for (i = 0; i < MAX_NUM_VFS; i++) {
		if ((enables == ALL_QUEUES) || (enables & (1 << i))) {
			reg = rd32(E1000_VMOLR(i));
			reg |= E1000_VMOLR_BAM;
			wr32(E1000_VMOLR(i), reg);
		}
	}
}

/**
 *  e1000_vmdq_multicast_replication_enable_vf - Enable replication of multicast
 *  @hw: pointer to the HW structure
 *  @enables: PoolSet Bit - if set to ALL_QUEUES, apply to all pools.
 *
 *  Enables replication of multicast packets from the network
 *  to VM's which have their respective multicast promiscuous mode enable
 *  bits set in the VM Offload Register.  This gives the PF driver per
 *  VM granularity control over which VM's get replicated multicast traffic.
 **/
void e1000_vmdq_multicast_replication_enable_vf(struct e1000_hw *hw,
						u32 enables)
{
	u32 reg;
	u32 i;

	for (i = 0; i < MAX_NUM_VFS; i++) {
		if ((enables == ALL_QUEUES) || (enables & (1 << i))) {
			reg = rd32(E1000_VMOLR(i));
			reg |= E1000_VMOLR_MPME;
			wr32(E1000_VMOLR(i), reg);
		}
	}
}

/**
 *  e1000_vmdq_broadcast_replication_disable_vf - Disable replication
 *  of broadcast
 *  @hw: pointer to the HW structure
 *  @disables: PoolSet Bit - if set to ALL_QUEUES, apply to all pools.
 *
 *  Disables replication of broadcast packets for specific pools. If
 *  replication (bc/mc) is disabled on all pools then replication mode is
 *  turned off.
 **/
void e1000_vmdq_broadcast_replication_disable_vf(struct e1000_hw *hw,
						u32 disables)
{
	u32 reg;
	u32 i;
	u32 oneenabled = 0;

	for (i = 0; i < MAX_NUM_VFS; i++) {
		reg = rd32(E1000_VMOLR(i));
		if ((disables == ALL_QUEUES) || (disables & (1 << i))) {
			reg &= ~(E1000_VMOLR_BAM);
			wr32(E1000_VMOLR(i), reg);
		}
		if (!oneenabled && (reg & (E1000_VMOLR_AUPE |
				E1000_VMOLR_BAM |
				E1000_VMOLR_MPME)))
				oneenabled = 1;
	}
	if (!oneenabled) {
		reg = rd32(E1000_VT_CTL);
		reg &= ~(E1000_VT_CTL_VM_REPL_EN);
		wr32(E1000_VT_CTL, reg);
	}
}

/**
 *  e1000_vmdq_multicast_replication_disable_vf - Disable replication
 *  of multicast packets
 *  @hw: pointer to the HW structure
 *  @disables: PoolSet Bit - if set to ALL_QUEUES, apply to all pools.
 *
 *  Disables replication of multicast packets for specific pools. If
 *  replication (bc/mc) is disabled on all pools then replication mode is
 *  turned off.
 **/
void e1000_vmdq_multicast_replication_disable_vf(struct e1000_hw *hw,
						u32 disables)
{
	u32 reg;
	u32 i;
	u32 oneenabled = 0;

	for (i = 0; i < MAX_NUM_VFS; i++) {
		reg = rd32(E1000_VMOLR(i));
		if ((disables == ALL_QUEUES) || (disables & (1 << i))) {
			reg &= ~(E1000_VMOLR_MPME);
			wr32(E1000_VMOLR(i), reg);
		}
		if (!oneenabled && (reg & (E1000_VMOLR_AUPE |
				E1000_VMOLR_BAM |
				E1000_VMOLR_MPME)))
				oneenabled = 1;
	}

	if (!oneenabled) {
		reg = rd32(E1000_VT_CTL);
		reg &= ~(E1000_VT_CTL_VM_REPL_EN);
		wr32(E1000_VT_CTL, reg);
	}
}

