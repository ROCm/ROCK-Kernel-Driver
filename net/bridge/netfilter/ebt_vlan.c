/*
 * Description: EBTables 802.1Q match extension kernelspace module.
 * Authors: Nick Fedchik <nick@fedchik.org.ua>
 *          Bart De Schuymer <bart.de.schuymer@pandora.be>
 *    
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *  
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/if_ether.h>
#include <linux/if_vlan.h>
#include <linux/module.h>
#include <linux/netfilter_bridge/ebtables.h>
#include <linux/netfilter_bridge/ebt_vlan.h>

static unsigned char debug;
#define MODULE_VERSION "0.4 (" __DATE__ " " __TIME__ ")"

MODULE_PARM (debug, "0-1b");
MODULE_PARM_DESC (debug, "debug=1 is turn on debug messages");
MODULE_AUTHOR ("Nick Fedchik <nick@fedchik.org.ua>");
MODULE_DESCRIPTION ("802.1Q match module (ebtables extension), v"
		    MODULE_VERSION);
MODULE_LICENSE ("GPL");


#define DEBUG_MSG(...) if (debug) printk (KERN_DEBUG __FILE__ ":" __VA_ARGS__)
#define INV_FLAG(_inv_flag_) (info->invflags & _inv_flag_) ? "!" : ""
#define GET_BITMASK(_BIT_MASK_) info->bitmask & _BIT_MASK_
#define SET_BITMASK(_BIT_MASK_) info->bitmask |= _BIT_MASK_
#define EXIT_ON_MISMATCH(_MATCH_,_MASK_) if (!((info->_MATCH_ == _MATCH_)^!!(info->invflags & _MASK_))) return 1;

/*
 * Function description: ebt_filter_vlan() is main engine for 
 * checking passed 802.1Q frame according to 
 * the passed extension parameters (in the *data buffer)
 * ebt_filter_vlan() is called after successfull check the rule params
 * by ebt_check_vlan() function.
 * Parameters:
 * const struct sk_buff *skb - pointer to passed ethernet frame buffer
 * const void *data - pointer to passed extension parameters
 * unsigned int datalen - length of passed *data buffer
 * const struct net_device *in  -
 * const struct net_device *out -
 * const struct ebt_counter *c -
 * Returned values:
 * 0 - ok (all rule params matched)
 * 1 - miss (rule params not acceptable to the parsed frame)
 */
static int
ebt_filter_vlan (const struct sk_buff *skb,
		 const struct net_device *in,
		 const struct net_device *out,
		 const void *data,
		 unsigned int datalen)
{
	struct ebt_vlan_info *info = (struct ebt_vlan_info *) data;	/* userspace data */
	struct vlan_ethhdr *frame = (struct vlan_ethhdr *) skb->mac.raw;	/* Passed tagged frame */

	unsigned short TCI;	/* Whole TCI, given from parsed frame */
	unsigned short id;	/* VLAN ID, given from frame TCI */
	unsigned char prio;	/* user_priority, given from frame TCI */
	unsigned short encap;	/* VLAN encapsulated Type/Length field, given from orig frame */

	/*
	 * Tag Control Information (TCI) consists of the following elements:
	 * - User_priority. This field allows the tagged frame to carry user_priority
	 * information across Bridged LANs in which individual LAN segments may be unable to signal
	 * priority information (e.g., 802.3/Ethernet segments). 
	 * The user_priority field is three bits in length, 
	 * interpreted as a binary number. The user_priority is therefore
	 * capable of representing eight priority levels, 0 through 7. 
	 * The use and interpretation of this field is defined in ISO/IEC 15802-3.
	 * - Canonical Format Indicator (CFI). This field is used,
	 * in 802.3/Ethernet, to signal the presence or absence
	 * of a RIF field, and, in combination with the Non-canonical Format Indicator (NCFI) carried
	 * in the RIF, to signal the bit order of address information carried in the encapsulated
	 * frame. The Canonical Format Indicator (CFI) is a single bit flag value.
	 * - VLAN Identifier (VID). This field uniquely identifies the VLAN to
	 * which the frame belongs. The twelve-bit VLAN Identifier (VID) field 
	 * uniquely identify the VLAN to which the frame belongs. 
	 * The VID is encoded as an unsigned binary number. 
	 */
	TCI = ntohs (frame->h_vlan_TCI);
	id = TCI & 0xFFF;
	prio = TCI >> 13;
	encap = frame->h_vlan_encapsulated_proto;

	/*
	 * First step is to check is null VLAN ID present
	 * in the parsed frame
	 */
	if (!(id)) {
		/*
		 * Checking VLAN Identifier (VID)
		 */
		if (GET_BITMASK (EBT_VLAN_ID)) {	/* Is VLAN ID parsed? */
			EXIT_ON_MISMATCH (id, EBT_VLAN_ID);
			DEBUG_MSG
			    ("matched rule id=%s%d for frame id=%d\n",
			     INV_FLAG (EBT_VLAN_ID), info->id, id);
		}
	} else {
		/*
		 * Checking user_priority
		 */
		if (GET_BITMASK (EBT_VLAN_PRIO)) {	/* Is VLAN user_priority parsed? */
			EXIT_ON_MISMATCH (prio, EBT_VLAN_PRIO);
			DEBUG_MSG
			    ("matched rule prio=%s%d for frame prio=%d\n",
			     INV_FLAG (EBT_VLAN_PRIO), info->prio,
			     prio);
		}
	}
	/*
	 * Checking Encapsulated Proto (Length/Type) field
	 */
	if (GET_BITMASK (EBT_VLAN_ENCAP)) {	/* Is VLAN Encap parsed? */
		EXIT_ON_MISMATCH (encap, EBT_VLAN_ENCAP);
		DEBUG_MSG ("matched encap=%s%2.4X for frame encap=%2.4X\n",
			   INV_FLAG (EBT_VLAN_ENCAP),
			   ntohs (info->encap), ntohs (encap));
	}
	/*
	 * All possible extension parameters was parsed.
	 * If rule never returned by missmatch, then all ok.
	 */
	return 0;
}

/*
 * Function description: ebt_vlan_check() is called when userspace 
 * delivers the table to the kernel, 
 * and to check that userspace doesn't give a bad table.
 * Parameters:
 * const char *tablename - table name string
 * unsigned int hooknr - hook number
 * const struct ebt_entry *e - ebtables entry basic set
 * const void *data - pointer to passed extension parameters
 * unsigned int datalen - length of passed *data buffer
 * Returned values:
 * 0 - ok (all delivered rule params are correct)
 * 1 - miss (rule params is out of range, invalid, incompatible, etc.)
 */
static int
ebt_check_vlan (const char *tablename,
		unsigned int hooknr,
		const struct ebt_entry *e, void *data,
		unsigned int datalen)
{
	struct ebt_vlan_info *info = (struct ebt_vlan_info *) data;

	/*
	 * Parameters buffer overflow check 
	 */
	if (datalen != sizeof (struct ebt_vlan_info)) {
		DEBUG_MSG
		    ("params size %d is not eq to ebt_vlan_info (%d)\n",
		     datalen, sizeof (struct ebt_vlan_info));
		return -EINVAL;
	}

	/*
	 * Is it 802.1Q frame checked?
	 */
	if (e->ethproto != __constant_htons (ETH_P_8021Q)) {
		DEBUG_MSG ("passed entry proto %2.4X is not 802.1Q (8100)\n",
			   (unsigned short) ntohs (e->ethproto));
		return -EINVAL;
	}

	/*
	 * Check for bitmask range 
	 * True if even one bit is out of mask
	 */
	if (info->bitmask & ~EBT_VLAN_MASK) {
		DEBUG_MSG ("bitmask %2X is out of mask (%2X)\n",
			   info->bitmask, EBT_VLAN_MASK);
		return -EINVAL;
	}

	/*
	 * Check for inversion flags range 
	 */
	if (info->invflags & ~EBT_VLAN_MASK) {
		DEBUG_MSG ("inversion flags %2X is out of mask (%2X)\n",
			   info->invflags, EBT_VLAN_MASK);
		return -EINVAL;
	}

	/*
	 * Reserved VLAN ID (VID) values
	 * -----------------------------
	 * 0 - The null VLAN ID. Indicates that the tag header contains only user_priority information;
	 * no VLAN identifier is present in the frame. This VID value shall not be
	 * configured as a PVID, configured in any Filtering Database entry, or used in any
	 * Management operation.
	 * 
	 * 1 - The default Port VID (PVID) value used for classifying frames on ingress through a Bridge
	 * Port. The PVID value can be changed by management on a per-Port basis.
	 * 
	 * 0x0FFF - Reserved for implementation use. This VID value shall not be configured as a
	 * PVID or transmitted in a tag header.
	 * 
	 * The remaining values of VID are available for general use as VLAN identifiers.
	 * A Bridge may implement the ability to support less than the full range of VID values; 
	 * i.e., for a given implementation,
	 * an upper limit, N, is defined for the VID values supported, where N is less than or equal to 4094.
	 * All implementations shall support the use of all VID values in the range 0 through their defined maximum
	 * VID, N.
	 * 
	 * For Linux, N = 4094.
	 */
	if (GET_BITMASK (EBT_VLAN_ID)) {	/* when vlan-id param was spec-ed */
		if (!!info->id) {	/* if id!=0 => check vid range */
			if (info->id > 4094) {	/* check if id > than (0x0FFE) */
				DEBUG_MSG
				    ("vlan id %d is out of range (1-4094)\n",
				     info->id);
				return -EINVAL;
			}
			/*
			 * Note: This is valid VLAN-tagged frame point.
			 * Any value of user_priority are acceptable, but could be ignored
			 * according to 802.1Q Std.
			 */
		} else {
			/*
			 * if id=0 (null VLAN ID)  => Check for user_priority range 
			 */
			if (GET_BITMASK (EBT_VLAN_PRIO)) {
				if ((unsigned char) info->prio > 7) {
					DEBUG_MSG
					    ("prio %d is out of range (0-7)\n",
					     info->prio);
					return -EINVAL;
				}
			}
			/*
			 * Note2: This is valid priority-tagged frame point
			 * with null VID field.
			 */
		}
	} else {		/* VLAN Id not set */
		if (GET_BITMASK (EBT_VLAN_PRIO)) {	/* But user_priority is set - abnormal! */
			info->id = 0;	/* Set null VID (case for Priority-tagged frames) */
			SET_BITMASK (EBT_VLAN_ID);	/* and set id flag */
		}
	}
	/*
	 * Check for encapsulated proto range - it is possible to be any value for u_short range.
	 * When relaying a tagged frame between 802.3/Ethernet MACs, 
	 * a Bridge may adjust the padding field such that
	 * the minimum size of a transmitted tagged frame is 68 octets (7.2).
	 * if_ether.h:  ETH_ZLEN        60   -  Min. octets in frame sans FCS
	 */
	if (GET_BITMASK (EBT_VLAN_ENCAP)) {
		if ((unsigned short) ntohs (info->encap) < ETH_ZLEN) {
			DEBUG_MSG
			    ("encap packet length %d is less than minimal %d\n",
			     ntohs (info->encap), ETH_ZLEN);
			return -EINVAL;
		}
	}

	/*
	 * Otherwise is all correct 
	 */
	DEBUG_MSG ("802.1Q tagged frame checked (%s table, %d hook)\n",
		   tablename, hooknr);
	return 0;
}

static struct ebt_match filter_vlan = {
	{NULL, NULL},
	EBT_VLAN_MATCH,
	ebt_filter_vlan,
	ebt_check_vlan,
	NULL,
	THIS_MODULE
};

/*
 * Module initialization function.
 * Called when module is loaded to kernelspace
 */
static int __init init (void)
{
	DEBUG_MSG ("ebtables 802.1Q extension module v"
		   MODULE_VERSION "\n");
	DEBUG_MSG ("module debug=%d\n", !!debug);
	return ebt_register_match (&filter_vlan);
}

/*
 * Module "finalization" function
 * Called when download module from kernelspace
 */
static void __exit fini (void)
{
	ebt_unregister_match (&filter_vlan);
}

module_init (init);
module_exit (fini);
