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

#include "e1000.h"

/* This is the only thing that needs to be changed to adjust the
 * maximum number of ports that the driver can manage.
 */

#define E1000_MAX_NIC 32

#define OPTION_UNSET    -1
#define OPTION_DISABLED 0
#define OPTION_ENABLED  1

/* Module Parameters are always initialized to -1, so that the driver
 * can tell the difference between no user specified value or the
 * user asking for the default value.
 * The true default values are loaded in when e1000_check_options is called.
 *
 * This is a GCC extension to ANSI C.
 * See the item "Labeled Elements in Initializers" in the section
 * "Extensions to the C Language Family" of the GCC documentation.
 */

#define E1000_PARAM_INIT { [0 ... E1000_MAX_NIC] = OPTION_UNSET }

/* All parameters are treated the same, as an integer array of values.
 * This macro just reduces the need to repeat the same declaration code
 * over and over (plus this helps to avoid typo bugs).
 */

#define E1000_PARAM(X, S) \
static const int __devinitdata X[E1000_MAX_NIC + 1] = E1000_PARAM_INIT; \
MODULE_PARM(X, "1-" __MODULE_STRING(E1000_MAX_NIC) "i"); \
MODULE_PARM_DESC(X, S);

/* Transmit Descriptor Count
 *
 * Valid Range: 80-256 for 82542 and 82543 gigabit ethernet controllers
 * Valid Range: 80-4096 for 82544
 *
 * Default Value: 256
 */

E1000_PARAM(TxDescriptors, "Number of transmit descriptors");

/* Receive Descriptor Count
 *
 * Valid Range: 80-256 for 82542 and 82543 gigabit ethernet controllers
 * Valid Range: 80-4096 for 82544
 *
 * Default Value: 80
 */

E1000_PARAM(RxDescriptors, "Number of receive descriptors");

/* User Specified Speed Override
 *
 * Valid Range: 0, 10, 100, 1000
 *  - 0    - auto-negotiate at all supported speeds
 *  - 10   - only link at 10 Mbps
 *  - 100  - only link at 100 Mbps
 *  - 1000 - only link at 1000 Mbps
 *
 * Default Value: 0
 */

E1000_PARAM(Speed, "Speed setting");

/* User Specified Duplex Override
 *
 * Valid Range: 0-2
 *  - 0 - auto-negotiate for duplex
 *  - 1 - only link at half duplex
 *  - 2 - only link at full duplex
 *
 * Default Value: 0
 */

E1000_PARAM(Duplex, "Duplex setting");

/* Auto-negotiation Advertisement Override
 *
 * Valid Range: 0x01-0x0F, 0x20-0x2F
 *
 * The AutoNeg value is a bit mask describing which speed and duplex
 * combinations should be advertised during auto-negotiation.
 * The supported speed and duplex modes are listed below
 *
 * Bit           7     6     5      4      3     2     1      0
 * Speed (Mbps)  N/A   N/A   1000   N/A    100   100   10     10
 * Duplex                    Full          Full  Half  Full   Half
 *
 * Default Value: 0x2F
 */

E1000_PARAM(AutoNeg, "Advertised auto-negotiation setting");

/* User Specified Flow Control Override
 *
 * Valid Range: 0-3
 *  - 0 - No Flow Control
 *  - 1 - Rx only, respond to PAUSE frames but do not generate them
 *  - 2 - Tx only, generate PAUSE frames but ignore them on receive
 *  - 3 - Full Flow Control Support
 *
 * Default Value: Read flow control settings from the EEPROM
 */

E1000_PARAM(FlowControl, "Flow Control setting");

/* XsumRX - Receive Checksum Offload Enable/Disable
 *
 * Valid Range: 0, 1
 *  - 0 - disables all checksum offload
 *  - 1 - enables receive IP/TCP/UDP checksum offload
 *        on 82543 based NICs
 *
 * Default Value: 1
 */

E1000_PARAM(XsumRX, "Disable or enable Receive Checksum offload");

/* Receive Interrupt Delay in units of 1.024 microseconds
 *
 * Valid Range: 0-65535
 *
 * Default Value: 0/128
 */

E1000_PARAM(RxIntDelay, "Receive Interrupt Delay");

#define AUTONEG_ADV_DEFAULT  0x2F
#define AUTONEG_ADV_MASK     0x2F
#define FLOW_CONTROL_DEFAULT FLOW_CONTROL_FULL

#define DEFAULT_TXD                  256
#define MAX_TXD                      256
#define MIN_TXD                       80
#define MAX_82544_TXD               4096

#define DEFAULT_RXD                   80
#define MAX_RXD                      256
#define MIN_RXD                       80
#define MAX_82544_RXD               4096

#define DEFAULT_RDTR                   0
#define DEFAULT_RADV                 128
#define MAX_RXDELAY               0xFFFF
#define MIN_RXDELAY                    0

struct e1000_option {
	enum { enable_option, range_option, list_option } type;
	char *name;
	char *err;
	int  def;
	union {
		struct { /* range_option info */
			int min;
			int max;
		} r;
		struct { /* list_option info */
			int nr;
			struct e1000_opt_list { int i; char *str; } *p;
		} l;
	} arg;
};


static int __devinit
e1000_validate_option(int *value, struct e1000_option *opt)
{
	if(*value == OPTION_UNSET) {
		*value = opt->def;
		return 0;
	}

	switch (opt->type) {
	case enable_option:
		switch (*value) {
		case OPTION_ENABLED:
			printk(KERN_INFO "%s Enabled\n", opt->name);
			return 0;
		case OPTION_DISABLED:
			printk(KERN_INFO "%s Disabled\n", opt->name);
			return 0;
		}
		break;
	case range_option:
		if(*value >= opt->arg.r.min && *value <= opt->arg.r.max) {
			printk(KERN_INFO "%s set to %i\n", opt->name, *value);
			return 0;
		}
		break;
	case list_option: {
		int i;
		struct e1000_opt_list *ent;

		for(i = 0; i < opt->arg.l.nr; i++) {
			ent = &opt->arg.l.p[i];
			if(*value == ent->i) {
				if(ent->str[0] != '\0')
					printk(KERN_INFO "%s\n", ent->str);
				return 0;
			}
		}
	}
		break;
	default:
		BUG();
	}
		
	printk(KERN_INFO "Invalid %s specified (%i) %s\n",
	       opt->name, *value, opt->err);
	*value = opt->def;
	return -1;
}

static void e1000_check_fiber_options(struct e1000_adapter *adapter);
static void e1000_check_copper_options(struct e1000_adapter *adapter);

/**
 * e1000_check_options - Range Checking for Command Line Parameters
 * @adapter: board private structure
 *
 * This routine checks all command line paramters for valid user
 * input.  If an invalid value is given, or if no user specified
 * value exists, a default value is used.  The final value is stored
 * in a variable in the adapter structure.
 **/

void __devinit
e1000_check_options(struct e1000_adapter *adapter)
{
	int bd = adapter->bd_number;
	if(bd >= E1000_MAX_NIC) {
		printk(KERN_NOTICE 
		       "Warning: no configuration for board #%i\n", bd);
		printk(KERN_NOTICE "Using defaults for all values\n");
		bd = E1000_MAX_NIC;
	}

	{ /* Transmit Descriptor Count */
		struct e1000_option opt = {
			type: range_option,
			name: "Transmit Descriptors",
			err:  "using default of " __MODULE_STRING(DEFAULT_TXD),
			def:  DEFAULT_TXD,
			arg: { r: { min: MIN_TXD }}
		};
		struct e1000_desc_ring *tx_ring = &adapter->tx_ring;
		e1000_mac_type mac_type = adapter->hw.mac_type;
		opt.arg.r.max = mac_type < e1000_82544 ? MAX_TXD : MAX_82544_TXD;

		tx_ring->count = TxDescriptors[bd];
		e1000_validate_option(&tx_ring->count, &opt);
		E1000_ROUNDUP(tx_ring->count, REQ_TX_DESCRIPTOR_MULTIPLE);
	}
	{ /* Receive Descriptor Count */
		struct e1000_option opt = {
			type: range_option,
			name: "Receive Descriptors",
			err:  "using default of " __MODULE_STRING(DEFAULT_RXD),
			def:  DEFAULT_RXD,
			arg: { r: { min: MIN_RXD }}
		};
		struct e1000_desc_ring *rx_ring = &adapter->rx_ring;
		e1000_mac_type mac_type = adapter->hw.mac_type;
		opt.arg.r.max = mac_type < e1000_82544 ? MAX_RXD : MAX_82544_RXD;

		rx_ring->count = RxDescriptors[bd];
		e1000_validate_option(&rx_ring->count, &opt);
		E1000_ROUNDUP(rx_ring->count, REQ_RX_DESCRIPTOR_MULTIPLE);
	}
	{ /* Checksum Offload Enable/Disable */
		struct e1000_option opt = {
			type: enable_option,
			name: "Checksum Offload",
			err:  "defaulting to Enabled",
			def:  OPTION_ENABLED
		};
		
		int rx_csum = XsumRX[bd];
		e1000_validate_option(&rx_csum, &opt);
		adapter->rx_csum = rx_csum;
	}
	{ /* Flow Control */
		
		struct e1000_opt_list fc_list[] =
			{{ e1000_fc_none,    "Flow Control Disabled" },
			 { e1000_fc_rx_pause,"Flow Control Receive Only" },
			 { e1000_fc_tx_pause,"Flow Control Transmit Only" },
			 { e1000_fc_full,    "Flow Control Enabled" },
			 { e1000_fc_default, "Flow Control Hardware Default" }};

		struct e1000_option opt = {
			type: list_option,
			name: "Flow Control",
			err:  "reading default settings from EEPROM",
			def:  e1000_fc_default,
			arg: { l: { nr: ARRAY_SIZE(fc_list), p: fc_list }}
		};

		int fc = FlowControl[bd];
		e1000_validate_option(&fc, &opt);
		adapter->hw.fc = adapter->hw.original_fc = fc;
	}
	{ /* Receive Interrupt Delay */
		char *rdtr = "using default of " __MODULE_STRING(DEFAULT_RDTR);
		char *radv = "using default of " __MODULE_STRING(DEFAULT_RADV);
		struct e1000_option opt = {
			type: range_option,
			name: "Receive Interrupt Delay",
			arg: { r: { min: MIN_RXDELAY, max: MAX_RXDELAY }}
		};
		e1000_mac_type mac_type = adapter->hw.mac_type;
		opt.def = mac_type < e1000_82540 ? DEFAULT_RDTR : DEFAULT_RADV;
		opt.err = mac_type < e1000_82540 ? rdtr : radv;

		adapter->rx_int_delay = RxIntDelay[bd];
		e1000_validate_option(&adapter->rx_int_delay, &opt);
	}
	
	switch(adapter->hw.media_type) {
	case e1000_media_type_fiber:
		e1000_check_fiber_options(adapter);
		break;
	case e1000_media_type_copper:
		e1000_check_copper_options(adapter);
		break;
	default:
		BUG();
	}
}

/**
 * e1000_check_fiber_options - Range Checking for Link Options, Fiber Version
 * @adapter: board private structure
 *
 * Handles speed and duplex options on fiber adapters
 **/

static void __devinit
e1000_check_fiber_options(struct e1000_adapter *adapter)
{
	int bd = adapter->bd_number;
	bd = bd > E1000_MAX_NIC ? E1000_MAX_NIC : bd;

	if((Speed[bd] != OPTION_UNSET)) {
		printk(KERN_INFO "Speed not valid for fiber adapters, "
		       "parameter ignored\n");
	}
	if((Duplex[bd] != OPTION_UNSET)) {
		printk(KERN_INFO "Duplex not valid for fiber adapters, "
		       "parameter ignored\n");
	}
	if((AutoNeg[bd] != OPTION_UNSET)) {
		printk(KERN_INFO "AutoNeg not valid for fiber adapters, "
		       "parameter ignored\n");
	}
}

/**
 * e1000_check_copper_options - Range Checking for Link Options, Copper Version
 * @adapter: board private structure
 *
 * Handles speed and duplex options on copper adapters
 **/

static void __devinit
e1000_check_copper_options(struct e1000_adapter *adapter)
{
	int speed, dplx;
	int bd = adapter->bd_number;
	bd = bd > E1000_MAX_NIC ? E1000_MAX_NIC : bd;

	{ /* Speed */
		struct e1000_opt_list speed_list[] = {{          0, "" },
		                                      {   SPEED_10, "" },
		                                      {  SPEED_100, "" },
		                                      { SPEED_1000, "" }};
		struct e1000_option opt = {
			type: list_option,
			name: "Speed",
			err:  "parameter ignored",
			def:  0,
			arg: { l: { nr: ARRAY_SIZE(speed_list), p: speed_list }}
		};

		speed = Speed[bd];
		e1000_validate_option(&speed, &opt);
	}
	{ /* Duplex */
		struct e1000_opt_list dplx_list[] = {{           0, "" },
		                                     { HALF_DUPLEX, "" },
		                                     { FULL_DUPLEX, "" }};
		struct e1000_option opt = {
			type: list_option,
			name: "Duplex",
			err:  "parameter ignored",
			def:  0,
			arg: { l: { nr: ARRAY_SIZE(dplx_list), p: dplx_list }}
		};

		dplx = Duplex[bd];
		e1000_validate_option(&dplx, &opt);
	}

	if(AutoNeg[bd] != OPTION_UNSET && (speed != 0 || dplx != 0)) {
		printk(KERN_INFO
		       "AutoNeg specified along with Speed or Duplex, "
		       "parameter ignored\n");
		adapter->hw.autoneg_advertised = AUTONEG_ADV_DEFAULT;
	} else { /* Autoneg */
		struct e1000_opt_list an_list[] =
			#define AA "AutoNeg advertising "
			{{ 0x01, AA "10/HD" },
			 { 0x02, AA "10/FD" },
			 { 0x03, AA "10/FD, 10/HD" },
			 { 0x04, AA "100/HD" },
			 { 0x05, AA "100/HD, 10/HD" },
			 { 0x06, AA "100/HD, 10/FD" },
			 { 0x07, AA "100/HD, 10/FD, 10/HD" },
			 { 0x08, AA "100/FD" },
			 { 0x09, AA "100/FD, 10/HD" },
			 { 0x0a, AA "100/FD, 10/FD" },
			 { 0x0b, AA "100/FD, 10/FD, 10/HD" },
			 { 0x0c, AA "100/FD, 100/HD" },
			 { 0x0d, AA "100/FD, 100/HD, 10/HD" },
			 { 0x0e, AA "100/FD, 100/HD, 10/FD" },
			 { 0x0f, AA "100/FD, 100/HD, 10/FD, 10/HD" },
			 { 0x20, AA "1000/FD" },
			 { 0x21, AA "1000/FD, 10/HD" },
			 { 0x22, AA "1000/FD, 10/FD" },
			 { 0x23, AA "1000/FD, 10/FD, 10/HD" },
			 { 0x24, AA "1000/FD, 100/HD" },
			 { 0x25, AA "1000/FD, 100/HD, 10/HD" },
			 { 0x26, AA "1000/FD, 100/HD, 10/FD" },
			 { 0x27, AA "1000/FD, 100/HD, 10/FD, 10/HD" },
			 { 0x28, AA "1000/FD, 100/FD" },
			 { 0x29, AA "1000/FD, 100/FD, 10/HD" },
			 { 0x2a, AA "1000/FD, 100/FD, 10/FD" },
			 { 0x2b, AA "1000/FD, 100/FD, 10/FD, 10/HD" },
			 { 0x2c, AA "1000/FD, 100/FD, 100/HD" },
			 { 0x2d, AA "1000/FD, 100/FD, 100/HD, 10/HD" },
			 { 0x2e, AA "1000/FD, 100/FD, 100/HD, 10/FD" },
			 { 0x2f, AA "1000/FD, 100/FD, 100/HD, 10/FD, 10/HD" }};

		struct e1000_option opt = {
			type: list_option,
			name: "AutoNeg",
			err:  "parameter ignored",
			def:  AUTONEG_ADV_DEFAULT,
			arg: { l: { nr: ARRAY_SIZE(an_list), p: an_list }}
		};

		int an = AutoNeg[bd];
		e1000_validate_option(&an, &opt);
		adapter->hw.autoneg_advertised = an;
	}

	switch (speed + dplx) {
	case 0:
		adapter->hw.autoneg = 1;
		if(Speed[bd] != OPTION_UNSET || Duplex[bd] != OPTION_UNSET)
			printk(KERN_INFO
			       "Speed and duplex autonegotiation enabled\n");
		break;
	case HALF_DUPLEX:
		printk(KERN_INFO "Half Duplex specified without Speed\n");
		printk(KERN_INFO "Using Autonegotiation at Half Duplex only\n");
		adapter->hw.autoneg = 1;
		adapter->hw.autoneg_advertised = ADVERTISE_10_HALF | 
		                                 ADVERTISE_100_HALF;
		break;
	case FULL_DUPLEX:
		printk(KERN_INFO "Full Duplex specified without Speed\n");
		printk(KERN_INFO "Using Autonegotiation at Full Duplex only\n");
		adapter->hw.autoneg = 1;
		adapter->hw.autoneg_advertised = ADVERTISE_10_FULL |
		                                 ADVERTISE_100_FULL |
		                                 ADVERTISE_1000_FULL;
		break;
	case SPEED_10:
		printk(KERN_INFO "10 Mbps Speed specified without Duplex\n");
		printk(KERN_INFO "Using Autonegotiation at 10 Mbps only\n");
		adapter->hw.autoneg = 1;
		adapter->hw.autoneg_advertised = ADVERTISE_10_HALF |
		                                 ADVERTISE_10_FULL;
		break;
	case SPEED_10 + HALF_DUPLEX:
		printk(KERN_INFO "Forcing to 10 Mbps Half Duplex\n");
		adapter->hw.autoneg = 0;
		adapter->hw.forced_speed_duplex = e1000_10_half;
		adapter->hw.autoneg_advertised = 0;
		break;
	case SPEED_10 + FULL_DUPLEX:
		printk(KERN_INFO "Forcing to 10 Mbps Full Duplex\n");
		adapter->hw.autoneg = 0;
		adapter->hw.forced_speed_duplex = e1000_10_full;
		adapter->hw.autoneg_advertised = 0;
		break;
	case SPEED_100:
		printk(KERN_INFO "100 Mbps Speed specified without Duplex\n");
		printk(KERN_INFO "Using Autonegotiation at 100 Mbps only\n");
		adapter->hw.autoneg = 1;
		adapter->hw.autoneg_advertised = ADVERTISE_100_HALF |
		                                 ADVERTISE_100_FULL;
		break;
	case SPEED_100 + HALF_DUPLEX:
		printk(KERN_INFO "Forcing to 100 Mbps Half Duplex\n");
		adapter->hw.autoneg = 0;
		adapter->hw.forced_speed_duplex = e1000_100_half;
		adapter->hw.autoneg_advertised = 0;
		break;
	case SPEED_100 + FULL_DUPLEX:
		printk(KERN_INFO "Forcing to 100 Mbps Full Duplex\n");
		adapter->hw.autoneg = 0;
		adapter->hw.forced_speed_duplex = e1000_100_full;
		adapter->hw.autoneg_advertised = 0;
		break;
	case SPEED_1000:
		printk(KERN_INFO "1000 Mbps Speed specified without Duplex\n");
		printk(KERN_INFO
		       "Using Autonegotiation at 1000 Mbps Full Duplex only\n");
		adapter->hw.autoneg = 1;
		adapter->hw.autoneg_advertised = ADVERTISE_1000_FULL;
		break;
	case SPEED_1000 + HALF_DUPLEX:
		printk(KERN_INFO "Half Duplex is not supported at 1000 Mbps\n");
		printk(KERN_INFO
		       "Using Autonegotiation at 1000 Mbps Full Duplex only\n");
		adapter->hw.autoneg = 1;
		adapter->hw.autoneg_advertised = ADVERTISE_1000_FULL;
		break;
	case SPEED_1000 + FULL_DUPLEX:
		printk(KERN_INFO
		       "Using Autonegotiation at 1000 Mbps Full Duplex only\n");
		adapter->hw.autoneg = 1;
		adapter->hw.autoneg_advertised = ADVERTISE_1000_FULL;
		break;
	default:
		BUG();
	}

	/* Speed, AutoNeg and MDI/MDI-X must all play nice */
	if (e1000_validate_mdi_setting(&(adapter->hw)) < 0) {
		printk(KERN_INFO "Speed, AutoNeg and MDI-X specifications are "
		       "incompatible. Setting MDI-X to a compatible value.\n");
	}
}

