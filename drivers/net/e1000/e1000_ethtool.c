/*******************************************************************************

  This software program is available to you under a choice of one of two
  licenses. You may choose to be licensed under either the GNU General Public
  License 2.0, June 1991, available at http://www.fsf.org/copyleft/gpl.html,
  or the Intel BSD + Patent License, the text of which follows:
  
  Recipient has requested a license and Intel Corporation ("Intel") is willing
  to grant a license for the software entitled Linux Base Driver for the
  Intel(R) PRO/1000 Family of Adapters (e1000) (the "Software") being provided
  by Intel Corporation. The following definitions apply to this license:
  
  "Licensed Patents" means patent claims licensable by Intel Corporation which
  are necessarily infringed by the use of sale of the Software alone or when
  combined with the operating system referred to below.
  
  "Recipient" means the party to whom Intel delivers this Software.
  
  "Licensee" means Recipient and those third parties that receive a license to
  any operating system available under the GNU General Public License 2.0 or
  later.
  
  Copyright (c) 1999 - 2002 Intel Corporation.
  All rights reserved.
  
  The license is provided to Recipient and Recipient's Licensees under the
  following terms.
  
  Redistribution and use in source and binary forms of the Software, with or
  without modification, are permitted provided that the following conditions
  are met:
  
  Redistributions of source code of the Software may retain the above
  copyright notice, this list of conditions and the following disclaimer.
  
  Redistributions in binary form of the Software may reproduce the above
  copyright notice, this list of conditions and the following disclaimer in
  the documentation and/or materials provided with the distribution.
  
  Neither the name of Intel Corporation nor the names of its contributors
  shall be used to endorse or promote products derived from this Software
  without specific prior written permission.
  
  Intel hereby grants Recipient and Licensees a non-exclusive, worldwide,
  royalty-free patent license under Licensed Patents to make, use, sell, offer
  to sell, import and otherwise transfer the Software, if any, in source code
  and object code form. This license shall include changes to the Software
  that are error corrections or other minor changes to the Software that do
  not add functionality or features when the Software is incorporated in any
  version of an operating system that has been distributed under the GNU
  General Public License 2.0 or later. This patent license shall apply to the
  combination of the Software and any operating system licensed under the GNU
  General Public License 2.0 or later if, at the time Intel provides the
  Software to Recipient, such addition of the Software to the then publicly
  available versions of such operating systems available under the GNU General
  Public License 2.0 or later (whether in gold, beta or alpha form) causes
  such combination to be covered by the Licensed Patents. The patent license
  shall not apply to any other combinations which include the Software. NO
  hardware per se is licensed hereunder.
  
  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
  IMPLIED WARRANTIES OF MECHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
  ARE DISCLAIMED. IN NO EVENT SHALL INTEL OR IT CONTRIBUTORS BE LIABLE FOR ANY
  DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
  (INCLUDING, BUT NOT LIMITED, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
  ANY LOSS OF USE; DATA, OR PROFITS; OR BUSINESS INTERUPTION) HOWEVER CAUSED
  AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY OR
  TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

*******************************************************************************/

/* ethtool support for e1000 */

#include "e1000.h"

#include <asm/uaccess.h>

extern char e1000_driver_name[];
extern char e1000_driver_version[];

extern int e1000_up(struct e1000_adapter *adapter);
extern void e1000_down(struct e1000_adapter *adapter);

static void
e1000_ethtool_gset(struct e1000_adapter *adapter, struct ethtool_cmd *ecmd)
{
	struct e1000_hw *hw = &adapter->hw;

	if(hw->media_type == e1000_media_type_copper) {

		ecmd->supported = (SUPPORTED_10baseT_Half |
		                   SUPPORTED_10baseT_Full |
		                   SUPPORTED_100baseT_Half |
		                   SUPPORTED_100baseT_Full |
		                   SUPPORTED_1000baseT_Full|
		                   SUPPORTED_Autoneg |
		                   SUPPORTED_TP);

		ecmd->advertising = ADVERTISED_TP;

		if(hw->autoneg == 1) {
			ecmd->advertising |= ADVERTISED_Autoneg;

			/* the e1000 autoneg seems to match ethtool nicely */

			ecmd->advertising |= hw->autoneg_advertised;
		}

		ecmd->port = PORT_TP;
		ecmd->phy_address = hw->phy_addr;

		if(hw->mac_type == e1000_82543)
			ecmd->transceiver = XCVR_EXTERNAL;
		else
			ecmd->transceiver = XCVR_INTERNAL;

	} else {
		ecmd->supported   = (SUPPORTED_1000baseT_Full |
				     SUPPORTED_FIBRE |
				     SUPPORTED_Autoneg);

		ecmd->advertising = (SUPPORTED_1000baseT_Full |
				     SUPPORTED_FIBRE |
				     SUPPORTED_Autoneg);

		ecmd->port = PORT_FIBRE;

		if(hw->mac_type >= e1000_82545)
			ecmd->transceiver = XCVR_INTERNAL;
		else
			ecmd->transceiver = XCVR_EXTERNAL;
	}

	if(netif_carrier_ok(adapter->netdev)) {

		e1000_get_speed_and_duplex(hw, &adapter->link_speed,
		                                   &adapter->link_duplex);
		ecmd->speed = adapter->link_speed;

		/* unfortunatly FULL_DUPLEX != DUPLEX_FULL
		 *          and HALF_DUPLEX != DUPLEX_HALF */

		if(adapter->link_duplex == FULL_DUPLEX)
			ecmd->duplex = DUPLEX_FULL;
		else
			ecmd->duplex = DUPLEX_HALF;
	} else {
		ecmd->speed = -1;
		ecmd->duplex = -1;
	}

	ecmd->autoneg = (hw->autoneg ? AUTONEG_ENABLE : AUTONEG_DISABLE);
}

static int
e1000_ethtool_sset(struct e1000_adapter *adapter, struct ethtool_cmd *ecmd)
{
	struct e1000_hw *hw = &adapter->hw;

	if(ecmd->autoneg == AUTONEG_ENABLE) {
		hw->autoneg = 1;
		hw->autoneg_advertised = (ecmd->advertising & 0x002F);
	} else {
		hw->autoneg = 0;
		switch(ecmd->speed + ecmd->duplex) {
		case SPEED_10 + DUPLEX_HALF:
			hw->forced_speed_duplex = e1000_10_half;
			break;
		case SPEED_10 + DUPLEX_FULL:
			hw->forced_speed_duplex = e1000_10_full;
			break;
		case SPEED_100 + DUPLEX_HALF:
			hw->forced_speed_duplex = e1000_100_half;
			break;
		case SPEED_100 + DUPLEX_FULL:
			hw->forced_speed_duplex = e1000_100_full;
			break;
		case SPEED_1000 + DUPLEX_FULL:
			hw->autoneg = 1;
			hw->autoneg_advertised = ADVERTISE_1000_FULL;
			break;
		case SPEED_1000 + DUPLEX_HALF: /* not supported */
		default:
			return -EINVAL;
		}
	}

	/* reset the link */

	e1000_down(adapter);
	e1000_up(adapter);

	return 0;
}

static inline int
e1000_eeprom_size(struct e1000_hw *hw)
{
	if((hw->mac_type > e1000_82544) &&
	   (E1000_READ_REG(hw, EECD) & E1000_EECD_SIZE))
		return 512;
	else
		return 128;
}

static void
e1000_ethtool_gdrvinfo(struct e1000_adapter *adapter,
                       struct ethtool_drvinfo *drvinfo)
{
	strncpy(drvinfo->driver,  e1000_driver_name, 32);
	strncpy(drvinfo->version, e1000_driver_version, 32);
	strncpy(drvinfo->fw_version, "", 32);
	strncpy(drvinfo->bus_info, adapter->pdev->slot_name, 32);
#define E1000_REGS_LEN 32
	drvinfo->regdump_len  = E1000_REGS_LEN * sizeof(uint32_t);
	drvinfo->eedump_len  = e1000_eeprom_size(&adapter->hw);
}

static void
e1000_ethtool_gregs(struct e1000_adapter *adapter,
                    struct ethtool_regs *regs, uint32_t *regs_buff)
{
	struct e1000_hw *hw = &adapter->hw;

	regs->version = (1 << 24) | (hw->revision_id << 16) | hw->device_id;

	regs_buff[0]  = E1000_READ_REG(hw, CTRL);
	regs_buff[1]  = E1000_READ_REG(hw, STATUS);

	regs_buff[2]  = E1000_READ_REG(hw, RCTL);
	regs_buff[3]  = E1000_READ_REG(hw, RDLEN);
	regs_buff[4]  = E1000_READ_REG(hw, RDH);
	regs_buff[5]  = E1000_READ_REG(hw, RDT);
	regs_buff[6]  = E1000_READ_REG(hw, RDTR);
	
	regs_buff[7]  = E1000_READ_REG(hw, TCTL);
	regs_buff[8]  = E1000_READ_REG(hw, TDLEN);
	regs_buff[9]  = E1000_READ_REG(hw, TDH);
	regs_buff[10] = E1000_READ_REG(hw, TDT);
	regs_buff[11] = E1000_READ_REG(hw, TIDV);

	return;
}

static void
e1000_ethtool_geeprom(struct e1000_adapter *adapter,
                      struct ethtool_eeprom *eeprom, uint16_t *eeprom_buff)
{
	struct e1000_hw *hw = &adapter->hw;
	int i, max_len, first_word, last_word;

	if(eeprom->len == 0) return;

	eeprom->magic = hw->vendor_id | (hw->device_id << 16);

	max_len = e1000_eeprom_size(hw);

	if((eeprom->offset + eeprom->len) > max_len)
		eeprom->len = (max_len - eeprom->offset);

	first_word = eeprom->offset >> 1;
	last_word = (eeprom->offset + eeprom->len - 1) >> 1;

	for(i = 0; i <= (last_word - first_word); i++)
		e1000_read_eeprom(hw, first_word + i, &eeprom_buff[i]);
}

static int 
e1000_ethtool_seeprom(struct e1000_adapter *adapter,
                      struct ethtool_eeprom *eeprom, void *user_data)
{
	struct e1000_hw *hw = &adapter->hw;
	uint16_t eeprom_buff[256];
	int i, max_len, first_word, last_word;
	void *ptr;

	if(eeprom->magic != (hw->vendor_id | (hw->device_id << 16)))
		return -EFAULT;

	if(eeprom->len == 0) return 0;

	max_len = e1000_eeprom_size(hw);

	if((eeprom->offset + eeprom->len) > max_len)
		eeprom->len = (max_len - eeprom->offset);

	first_word = eeprom->offset >> 1;
	last_word = (eeprom->offset + eeprom->len - 1) >> 1;
	ptr = (void *)eeprom_buff;

	if(eeprom->offset & 1) {
		/* need read/modify/write of first changed EEPROM word */
		/* only the second byte of the word is being modified */
		e1000_read_eeprom(hw, first_word, &eeprom_buff[0]);
		ptr++;
	}
	if((eeprom->offset + eeprom->len) & 1) {
		/* need read/modify/write of last changed EEPROM word */
		/* only the first byte of the word is being modified */
		e1000_read_eeprom(hw, last_word,
		                  &eeprom_buff[last_word - first_word]);
	}
	if(copy_from_user(ptr, user_data, eeprom->len))
		return -EFAULT;

	for(i = 0; i <= (last_word - first_word); i++)
		e1000_write_eeprom(hw, first_word + i, eeprom_buff[i]);

	/* Update the checksum over the first part of the EEPROM if needed */
	if(first_word <= EEPROM_CHECKSUM_REG)
		e1000_update_eeprom_checksum(hw);

	return 0;
}

static void
e1000_ethtool_gwol(struct e1000_adapter *adapter, struct ethtool_wolinfo *wol)
{
	struct e1000_hw *hw = &adapter->hw;

	switch(adapter->hw.device_id) {
	case E1000_DEV_ID_82542:
	case E1000_DEV_ID_82543GC_FIBER:
	case E1000_DEV_ID_82543GC_COPPER:
	case E1000_DEV_ID_82544EI_FIBER:
	default:
		wol->supported = 0;
		wol->wolopts   = 0;
		return;

	case E1000_DEV_ID_82546EB_FIBER:
		/* Wake events only supported on port A for dual fiber */
		if(E1000_READ_REG(hw, STATUS) & E1000_STATUS_FUNC_1) {
			wol->supported = 0;
			wol->wolopts   = 0;
			return;
		}
		/* Fall Through */

	case E1000_DEV_ID_82544EI_COPPER:
	case E1000_DEV_ID_82544GC_COPPER:
	case E1000_DEV_ID_82544GC_LOM:
	case E1000_DEV_ID_82540EM:
	case E1000_DEV_ID_82540EM_LOM:
	case E1000_DEV_ID_82545EM_COPPER:
	case E1000_DEV_ID_82545EM_FIBER:
	case E1000_DEV_ID_82546EB_COPPER:
		wol->supported = WAKE_PHY | WAKE_UCAST | 
				 WAKE_MCAST | WAKE_BCAST | WAKE_MAGIC;
		
		wol->wolopts = 0;
		if(adapter->wol & E1000_WUFC_LNKC)
			wol->wolopts |= WAKE_PHY;
		if(adapter->wol & E1000_WUFC_EX)
			wol->wolopts |= WAKE_UCAST;
		if(adapter->wol & E1000_WUFC_MC)
			wol->wolopts |= WAKE_MCAST;
		if(adapter->wol & E1000_WUFC_BC)
			wol->wolopts |= WAKE_BCAST;
		if(adapter->wol & E1000_WUFC_MAG)
			wol->wolopts |= WAKE_MAGIC;
		return;
	}
}

static int
e1000_ethtool_swol(struct e1000_adapter *adapter, struct ethtool_wolinfo *wol)
{
	struct e1000_hw *hw = &adapter->hw;

	switch(adapter->hw.device_id) {
	case E1000_DEV_ID_82542:
	case E1000_DEV_ID_82543GC_FIBER:
	case E1000_DEV_ID_82543GC_COPPER:
	case E1000_DEV_ID_82544EI_FIBER:
	default:
		return wol->wolopts ? -EOPNOTSUPP : 0;

	case E1000_DEV_ID_82546EB_FIBER:
		/* Wake events only supported on port A for dual fiber */
		if(E1000_READ_REG(hw, STATUS) & E1000_STATUS_FUNC_1)
			return wol->wolopts ? -EOPNOTSUPP : 0;
		/* Fall Through */

	case E1000_DEV_ID_82544EI_COPPER:
	case E1000_DEV_ID_82544GC_COPPER:
	case E1000_DEV_ID_82544GC_LOM:
	case E1000_DEV_ID_82540EM:
	case E1000_DEV_ID_82540EM_LOM:
	case E1000_DEV_ID_82545EM_COPPER:
	case E1000_DEV_ID_82545EM_FIBER:
	case E1000_DEV_ID_82546EB_COPPER:
		if(wol->wolopts & (WAKE_ARP | WAKE_MAGICSECURE))
			return -EOPNOTSUPP;

		adapter->wol = 0;

		if(wol->wolopts & WAKE_PHY)
			adapter->wol |= E1000_WUFC_LNKC;
		if(wol->wolopts & WAKE_UCAST)
			adapter->wol |= E1000_WUFC_EX;
		if(wol->wolopts & WAKE_MCAST)
			adapter->wol |= E1000_WUFC_MC;
		if(wol->wolopts & WAKE_BCAST)
			adapter->wol |= E1000_WUFC_BC;
		if(wol->wolopts & WAKE_MAGIC)
			adapter->wol |= E1000_WUFC_MAG;
	}

	return 0;
}


/* toggle LED 4 times per second = 2 "blinks" per second */
#define E1000_ID_INTERVAL	(HZ/4)

/* bit defines for adapter->led_status */
#define E1000_LED_ON		0

static void
e1000_led_blink_callback(unsigned long data)
{
	struct e1000_adapter *adapter = (struct e1000_adapter *) data;
	
	if(test_and_change_bit(E1000_LED_ON, &adapter->led_status))
		e1000_led_off(&adapter->hw);
	else
		e1000_led_on(&adapter->hw);

	mod_timer(&adapter->blink_timer, jiffies + E1000_ID_INTERVAL);
}

static int
e1000_ethtool_led_blink(struct e1000_adapter *adapter, struct ethtool_value *id)
{
	if(!adapter->blink_timer.function) {
		init_timer(&adapter->blink_timer);
		adapter->blink_timer.function = e1000_led_blink_callback;
		adapter->blink_timer.data = (unsigned long) adapter;
	}

	e1000_setup_led(&adapter->hw);
	mod_timer(&adapter->blink_timer, jiffies);
	
	set_current_state(TASK_INTERRUPTIBLE);
	if(id->data)
		schedule_timeout(id->data * HZ);
	else
		schedule_timeout(MAX_SCHEDULE_TIMEOUT);
	
	del_timer_sync(&adapter->blink_timer);
	e1000_led_off(&adapter->hw);
	clear_bit(E1000_LED_ON, &adapter->led_status);
	e1000_cleanup_led(&adapter->hw);

	return 0;
}

int
e1000_ethtool_ioctl(struct net_device *netdev, struct ifreq *ifr)
{
	struct e1000_adapter *adapter = netdev->priv;
	void *addr = ifr->ifr_data;
	uint32_t cmd;

	if(get_user(cmd, (uint32_t *) addr))
		return -EFAULT;

	switch(cmd) {
	case ETHTOOL_GSET: {
		struct ethtool_cmd ecmd = {ETHTOOL_GSET};
		e1000_ethtool_gset(adapter, &ecmd);
		if(copy_to_user(addr, &ecmd, sizeof(ecmd)))
			return -EFAULT;
		return 0;
	}
	case ETHTOOL_SSET: {
		struct ethtool_cmd ecmd;
		if(!capable(CAP_NET_ADMIN))
			return -EPERM;
		if(copy_from_user(&ecmd, addr, sizeof(ecmd)))
			return -EFAULT;
		return e1000_ethtool_sset(adapter, &ecmd);
	}
	case ETHTOOL_GDRVINFO: {
		struct ethtool_drvinfo drvinfo = {ETHTOOL_GDRVINFO};
		e1000_ethtool_gdrvinfo(adapter, &drvinfo);
		if(copy_to_user(addr, &drvinfo, sizeof(drvinfo)))
			return -EFAULT;
		return 0;
	}
	case ETHTOOL_GREGS: {
		struct ethtool_regs regs = {ETHTOOL_GREGS};
		uint32_t regs_buff[E1000_REGS_LEN];

		if(copy_from_user(&regs, addr, sizeof(regs)))
			return -EFAULT;
		e1000_ethtool_gregs(adapter, &regs, regs_buff);
		if(copy_to_user(addr, &regs, sizeof(regs)))
			return -EFAULT;

		addr += offsetof(struct ethtool_regs, data);
		if(copy_to_user(addr, regs_buff, regs.len))
			return -EFAULT;

		return 0;
	}
	case ETHTOOL_NWAY_RST: {
		if(!capable(CAP_NET_ADMIN))
			return -EPERM;
		e1000_down(adapter);
		e1000_up(adapter);
		return 0;
	}
	case ETHTOOL_PHYS_ID: {
		struct ethtool_value id;
		if(copy_from_user(&id, addr, sizeof(id)))
			return -EFAULT;
		return e1000_ethtool_led_blink(adapter, &id);
	}
	case ETHTOOL_GLINK: {
		struct ethtool_value link = {ETHTOOL_GLINK};
		link.data = netif_carrier_ok(netdev);
		if(copy_to_user(addr, &link, sizeof(link)))
			return -EFAULT;
		return 0;
	}
	case ETHTOOL_GWOL: {
		struct ethtool_wolinfo wol = {ETHTOOL_GWOL};
		e1000_ethtool_gwol(adapter, &wol);
		if(copy_to_user(addr, &wol, sizeof(wol)) != 0)
			return -EFAULT;
		return 0;
	}
	case ETHTOOL_SWOL: {
		struct ethtool_wolinfo wol;
		if(!capable(CAP_NET_ADMIN))
			return -EPERM;
		if(copy_from_user(&wol, addr, sizeof(wol)) != 0)
			return -EFAULT;
		return e1000_ethtool_swol(adapter, &wol);
	}
	case ETHTOOL_GEEPROM: {
		struct ethtool_eeprom eeprom = {ETHTOOL_GEEPROM};
		uint16_t eeprom_buff[256];
		void *ptr;

		if(copy_from_user(&eeprom, addr, sizeof(eeprom)))
			return -EFAULT;

		e1000_ethtool_geeprom(adapter, &eeprom, eeprom_buff);

		if(copy_to_user(addr, &eeprom, sizeof(eeprom)))
			return -EFAULT;

		addr += offsetof(struct ethtool_eeprom, data);
		ptr = ((void *)eeprom_buff) + (eeprom.offset & 1);

		if(copy_to_user(addr, ptr, eeprom.len))
			return -EFAULT;
		return 0;
	}
	case ETHTOOL_SEEPROM: {
		struct ethtool_eeprom eeprom;

		if(!capable(CAP_NET_ADMIN))
			return -EPERM;

		if(copy_from_user(&eeprom, addr, sizeof(eeprom)))
			return -EFAULT;

		addr += offsetof(struct ethtool_eeprom, data);
		return e1000_ethtool_seeprom(adapter, &eeprom, addr);
	}
	default:
		return -EOPNOTSUPP;
	}
}


