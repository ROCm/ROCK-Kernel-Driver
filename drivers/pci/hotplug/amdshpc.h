/* 
 * AMD Standard Hot Plug Controller Driver
 *
 * Copyright (c) 1995,2001 Compaq Computer Corporation
 * Copyright (C) 2001,2003 Greg Kroah-Hartman (greg@kroah.com)
 * Copyright (C) 2001,2003 IBM Corp.
 * Copyright (C) 2002-2004 Advanced Micro Devices
 *
 * YOUR USE OF THIS CODE IS SUBJECT TO THE TERMS
 * AND CONDITIONS OF THE GNU GENERAL PUBLIC
 * LICENSE FOUND IN THE "GPL.TXT" FILE THAT IS
 * INCLUDED WITH THIS FILE AND POSTED AT
 * http://www.gnu.org/licenses/gpl.html
 *
 * Send feedback to <greg@kroah.com> <david.keck@amd.com>
 *
*/

#ifndef _SHPC_H_
#define _SHPC_H_


#include <linux/types.h>
#include <linux/wait.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/timer.h>
#include <linux/sched.h>
#include <asm/semaphore.h>
#include "pci_hotplug.h"
#include "amdshpc_pcix.h"

#define SHPC_MAX_SLOT_WAIT_OBJECTS		7	
#define SHPC_SINGLE_SLOT_SPLIT_TRANSACTIONS	4		
#define SHPC_MULTIPLE_SLOT_SPLIT_TRANSACTIONS	8
//
// Timeouts
//
#define ONE_TENTH_SEC_TIMEOUT   		10	   	// 0.1 sec
#define ONE_SEC_TIMEOUT                 HZ * 1 	// 1 sec
#define FIVE_SEC_TIMEOUT                HZ * 5 	// 5 secs
#define TEN_SEC_TIMEOUT                 HZ * 10	// 10 secs
#define FIFTEEN_SEC_TIMEOUT             HZ * 15	// 15 secs
#define QUIESCE_QUIET_TIMEOUT           HZ * 30	// 30 secs
#define QUIESCE_TIMEOUT                 HZ * 60	// 60 secs
#define ONE_SEC_INCREMENT               HZ * 1 	// 1 sec

#define SLOT_MAGIC      0x67267322

//
// SHPC Constants
//
#define SHPC_MAX_NUM_SLOTS      4
#define arraysize(p) (sizeof(p)/sizeof((p)[0]))

struct slot {
	u32 magic;
	struct slot *next;
	struct list_head slot_list;
	u8 bus;
	u8 device;
	u8 number;
	u8 is_a_board;
	u8 configured;
	u8 state;
	u8 switch_save;
	u8 presence_save;
	u32 capabilities;
	u16 reserved2;
	struct timer_list task_event;
	u8 hp_slot;
	struct controller *ctrl;
	void *p_sm_slot;
	struct hotplug_slot *hotplug_slot;
	void* private;
};

struct controller {
	struct controller *next;
	void *shpc_context;
	u32 ctrl_int_comp;
	void *hpc_reg;		/* cookie for our pci controller location */
	struct pci_resource *mem_head;
	struct pci_resource *p_mem_head;
	struct pci_resource *io_head;
	struct pci_resource *bus_head;
	struct pci_dev *pci_dev;
	struct pci_bus *pci_bus;
	struct slot *slot;
	u8 interrupt;
	u8 bus;
	u8 device;
	u8 function;
	u8 slot_device_offset;
	u8 first_slot;
	u8 add_support;
	u16 vendor_id;
	u32 pcix_slots;
	u32 pcix_max_split_transactions;
	u32 pcix_max_read_byte_count;
	u8  max_split_trans_perslot[SHPC_MAX_NUM_SLOTS];
	union pci_bus_info bus_info;
};


static LIST_HEAD(slot_list);

#if !defined(CONFIG_HOTPLUG_PCI_AMD_MODULE)
	#define MY_NAME "amd_shpc.o"
#else
	#define MY_NAME THIS_MODULE->name
#endif

//
// Debug Facilities
//
#define debug 0
#define dbg(format, arg...)                                     \
	do {                                                    \
		if (debug)                                      \
		    printk (KERN_DEBUG "%s: " format "\n",      \
		    MY_NAME , ## arg);          \
	} while (0)

#define err(format, arg...) printk(KERN_ERR "%s: " format "\n", MY_NAME , ## arg)
#define info(format, arg...) printk(KERN_INFO "%s: " format "\n", MY_NAME , ## arg)
#define warn(format, arg...) printk(KERN_WARNING "%s: " format "\n", MY_NAME , ## arg)

#define msg_initialization_err  "Initialization failure, error=%d\n"
#define msg_HPC_rev_error       "Unsupported revision of the PCI hot plug controller found.\n"
#define msg_HPC_non_amd         "Non-AMD PCI hot plug controller is not supported by this driver.\n"
#define msg_HPC_not_amd_hp	"Device is not a hot plug controller.\n"
#define msg_HPC_not_supported   "This system is not supported by this version of amdshpc. Upgrade to a newer version of amdshpc\n"
#define msg_unable_to_save      "Unable to store PCI hot plug add resource information. This system must be rebooted before adding any PCI devices.\n"

struct hrt {
	char sig0;
	char sig1;
	char sig2;
	char sig3;
	u16 unused_IRQ;
	u16 PCIIRQ;
	u8 number_of_entries;
	u8 revision;
	u16 reserved1;
	u32 reserved2;
} __attribute__ ((packed));

/* offsets to the hotplug resource table registers based on the above structure layout */
enum hrt_offsets {
	SIG0 =                  offsetof(struct hrt, sig0),
	SIG1 =                  offsetof(struct hrt, sig1),
	SIG2 =                  offsetof(struct hrt, sig2),
	SIG3 =                  offsetof(struct hrt, sig3),
	UNUSED_IRQ =            offsetof(struct hrt, unused_IRQ),
	PCIIRQ =                offsetof(struct hrt, PCIIRQ),
	NUMBER_OF_ENTRIES =     offsetof(struct hrt, number_of_entries),
	REVISION =              offsetof(struct hrt, revision),
	HRT_RESERVED1 =         offsetof(struct hrt, reserved1),
	HRT_RESERVED2 =         offsetof(struct hrt, reserved2),
};

struct slot_rt {
	u8 dev_func;
	u8 primary_bus;
	u8 secondary_bus;
	u8 max_bus;
	u16 io_base;
	u16 io_length;
	u16 mem_base;
	u16 mem_length;
	u16 pre_mem_base;
	u16 pre_mem_length;
} __attribute__ ((packed));

/* offsets to the hotplug slot resource table registers based on the above structure layout */
enum slot_rt_offsets {
	DEV_FUNC =      offsetof(struct slot_rt, dev_func),
	PRIMARY_BUS =   offsetof(struct slot_rt, primary_bus),
	SECONDARY_BUS = offsetof(struct slot_rt, secondary_bus),
	MAX_BUS =       offsetof(struct slot_rt, max_bus),
	IO_BASE =       offsetof(struct slot_rt, io_base),
	IO_LENGTH =     offsetof(struct slot_rt, io_length),
	MEM_BASE =      offsetof(struct slot_rt, mem_base),
	MEM_LENGTH =    offsetof(struct slot_rt, mem_length),
	PRE_MEM_BASE =  offsetof(struct slot_rt, pre_mem_base),
	PRE_MEM_LENGTH = offsetof(struct slot_rt, pre_mem_length),
};

struct pci_func {
	struct pci_func *next;
	u8 bus;
	u8 device;
	u8 function;
	u8 is_a_board;
	u16 status;
	u8 configured;
	u8 switch_save;
	u8 presence_save;
	u32 base_length[0x06];
	u8 base_type[0x06];
	u16 reserved2;
	u32 config_space[0x20];
	struct pci_resource *mem_head;
	struct pci_resource *p_mem_head;
	struct pci_resource *io_head;
	struct pci_resource *bus_head;
	struct timer_list *p_task_event;
	struct pci_dev* pci_dev;
};


#ifndef FALSE
	#define FALSE 0
	#define TRUE 1
#endif

enum mutex_action {
	ACQUIRE,
	RELEASE,
};

enum hp_boolean {
	HP_FALSE = 0,
	HP_TRUE = 1,
};

// card power requirements
enum hp_power_requirements {
	POWER_LOW,		// low power requirements
	POWER_MEDIUM,	// medium power requirements
	POWER_HIGH,		// high power requirements
};

//
// slot event masks
//
#define ATTN_BUTTON_EVENT               0x00000001
#define ALERT_EVENT						0x00000002
#define BUS_REBALANCE_EVENT             0x00000004
#define QUIESCE_EVENT                   0x00000008
#define ATTN_LED_PROBLEM_EVENT          0x00000010
#define ATTN_LED_REQUEST_EVENT          0x00000020
#define SLOT_REQUEST_EVENT              0x00000040
#define SLOT_TIMER1_EVENT               0x00000080
#define SLOT_TIMER2_EVENT               0x00000100
#define SLOT_TIMER3_EVENT               0x00000200
#define SLOT_TIMER4_EVENT               0x00000400
#define SLOT_TIMER5_EVENT               0x00000800
#define SLOT_TIMER6_EVENT               0x00001000
#define SLOT_TIMER7_EVENT               0x00002000
#define SLOT_TIMER8_EVENT               0x00004000
#define SLOT_TIMER9_EVENT               0x00008000
#define SLOT_TIMER10_EVENT              0x00010000
#define LED_TIMER1_EVENT                0x00020000
#define LED_TIMER2_EVENT                0x00040000
#define LED_TIMER3_EVENT                0x00080000
#define LED_TIMER4_EVENT                0x00100000
#define CMD_ACQUIRE_EVENT               0x00200000
#define CMD_RELEASE_EVENT               0x00400000
#define LED_CMD_ACQUIRE_EVENT           0x00800000
#define LED_CMD_RELEASE_EVENT           0x01000000
#define BUS_RELEASE_EVENT               0x02000000
#define BUS_ACQUIRE_EVENT               0x04000000

//
// controller event masks
//
#define BUS_COMPLETE_EVENT              0x00000001
#define SUSPEND_EVENT                   0x00000002
#define RESUME_EVENT                    0x00000004
#define REMOVE_EVENT                    0x00000008
#define EXIT_REQUEST_EVENT              0x00000010
#define CTRL_TIMER_EVENT                0x00000020
#define CMD_COMPLETION_EVENT            0x00000040
#define CMD_AVAILABLE_MUTEX_EVENT       0x00000080
#define BUS_AVAILABLE_MUTEX_EVENT       0x00000100
#define LED_CMD_AVAILABLE_MUTEX_EVENT   0x00000200


#define PCI_TO_PCI_BRIDGE_CLASS         0x00060400
#define SLOT_MASK                       0x28


#define ADD_NOT_SUPPORTED               0x00000003
#define ADAPTER_NOT_SAME                0x00000006
#define NO_ADAPTER_PRESENT              0x00000009

#define REMOVE_NOT_SUPPORTED            0x00000003

//
// register masks
//

//SHPC_SLOTS_AVAILABLE1 reg masks and offsets
#define	N_33CONV	0x0000001f
#define	N_33CONV_OFFSET		0	// 4:0   
#define	N_66PCIX	0x00001f00		
#define	N_66PCIX_OFFSET		8	// 12:8  
#define	N_100PCIX	0x001f0000      
#define	N_100PCIX_OFFSET	16	// 20:16 
#define	N_133PCIX	0x1f000000      
#define	N_133PCIX_OFFSET	24	// 28:24 

// SHPC_SLOTS_AVAILABLE2 reg masks and offsets
#define	N_66CONV		0x0000001f	// 4:0  
#define	N_66CONV_OFFSET		0       
#define	N_66PCIX266		0x00000f00	// 11:8 
#define	N_66PCIX266_OFFSET	8       			
#define	N_100PCIX266	0x0000f000	// 15:12  	
#define	N_100PCIX266_OFFSET	12      			
#define	N_133PCIX266	0x000f0000  // 19:16	
#define	N_133PCIX266_OFFSET	16      			
#define	N_66PCIX533 	0x00f00000	// 23:20
#define	N_66PCIX533_OFFSET	20
#define	N_100PCIX533	0x0f000000	// 27:24
#define	N_100PCIX533_OFFSET	24
#define N_133PCIX533	0xf0000000	// 31:28
#define N_133PCIX533_OFFSET	28

// SHPC_SLOT_CONFIG reg masks and offsets
#define	NSI_MASK		0x0000001f	// 4:0
#define NSI_OFFSET		 0
#define	FDN_MASK		0x00001f00	// 12:8
#define FDN_OFFSET		 8
#define	PSN_MASK		0x07ff0000	// 26:16
#define PSN_OFFSET		16
#define	PSN_UP_MASK		0x20000000	// 29
#define PSN_UP_OFFSET	29
#define	MRLSI_MASK		0x40000000	// 30
#define	MRLSI_OFFSET	30
#define	ABI_MASK		0x80000000	// 31
#define	ABI_OFFSET		31

// SHPC_STATUS reg masks and offsets
#define	STS_BSY_MASK		0x0001		// 0	(u16)
#define	STS_BSY_OFFSET		0	
#define	MRLO_ERR_MASK		0x0002		// 1
#define	MRLO_ERR_OFFSET		1
#define	INVCMD_ERR_MASK		0x0004		// 2
#define	INVCMD_ERR_OFFSET	2
#define	INVSM_ERR_MASK		0x0008		// 3
#define	INVSM_ERR_OFFSET	3

// SHPC_SERR_LOCATOR reg masks
#define	A_SERRP_MASK	0x00000001	// 0
#define SLOT_SERRP_MASK	0x0000001e	// 4:1 (u32)

// SHPC_SERR_INT reg masks and offsets
#define GIM_MASK		0x00000001	// 0  (u32)
#define GSERRM_MASK		0x00000002	// 1
#define CC_IM_MASK		0x00000004	// 2
#define CC_IM_OFFSET	2
#define A_SERRM_MASK	0x00000008	// 3
#define CC_STS_MASK		0x00010000	// 16
#define CC_STS_OFFSET	16
#define ATOUT_STS_MASK	0x00020000	// 17

// SHPC_PME_CONTROL reg offsets
#define	PWRS		1	// 0:1
#define PME_ENABLE	8	// 8
#define PME_SCS		15	// 15
#define BPCC_EN		23	// 23

// SHPC Secondary Bus Configuration Register masks and offsets
#define MODE_1_MASK		0x00000007	// 2:0
#define MODE_1_OFFSET	0		// 2:0
#define INTERFACE_MASK	0xff000000	// 31:24
#define INTERFACE_OFFSET	24		// 31:24
#define MODE_2_MASK		0x0000000f	// 3:0
#define MODE_2_OFFSET	0		// 3:0
#define ECC_MODE	8			// 8
#define MODE1_ECC	9			// 9

// SHPC Interrupt Locator Register	masks and offsets
#define CC_IP_MASK       0x00000001	// 0
#define CC_IP_OFFSET		0
#define SLOT_IP_MASK     0x0000001e	// 4:1
#define SLOT_IP_OFFSET		1	// 

// SHPC Logical Slot Register masks and offsets
#define S_STATE_MASK	0x00000003	// 1:0
#define S_STATE_OFFSET	0			// 1:0
#define PIS_MASK		0x0000000c	// 3:2
#define PIS_OFFSET		2			// 3:2
#define AIS_MASK		0x00000030	// 5:4
#define AIS_OFFSET		4			// 5:4
#define PF_MASK			0x00000040	// 6
#define PF_OFFSET		6			// 6
#define AB_MASK			0x00000080	// 7
#define AB_OFFSET		7			// 7
#define MRLS_MASK		0x00000100	// 8
#define MRLS_OFFSET		8			// 8
#define M66_CAP_MASK	0x00000200	// 9
#define M66_CAP_OFFSET	9			// 9
#define PRSNT1_2_MASK	0x00000c00	// 11:10
#define PRSNT1_2_OFFSET	10			// 11:10
#define PCIX_CAP_MODE1_MASK	0x00003000	// 13:12
#define PCIX_CAP_MODE1_OFFSET		12	// 13:12
#define PCIX_CAP_MODE2_MASK	0x00007000	// 14:12
#define PCIX_CAP_MODE2_OFFSET		12	// 14:12
#define CPC_STS_MASK	0x00010000	// 16
#define CPC_STS_OFFSET	16			// 16
#define IPF_STS_MASK	0x00020000	// 17
#define IPF_STS_OFFSET	17			// 17
#define ABP_STS_MASK	0x00040000	// 18
#define ABP_STS_OFFSET	18			// 18
#define MRLSC_STS_MASK	0x00080000	// 19
#define MRLSC_STS_OFFSET	19		// 19
#define CPF_STS_MASK	0x00100000	// 20
#define CPF_STS_OFFSET	20			// 20
#define CP_IM_MASK		0x01000000	// 24
#define CP_IM_OFFSET	24			// 24
#define IPF_IM_MASK		0x02000000	// 25
#define IPF_IM_OFFSET	25			// 25
#define AB_IM_MASK		0x04000000	// 26
#define AB_IM_OFFSET	26			// 26
#define MRLS_IM_MASK	0x08000000	// 27
#define MRLS_IM_OFFSET	27			// 27
#define CPF_IM_MASK		0x10000000	// 28
#define CPF_IM_OFFSET	28			// 28
#define MRLS_SERRM_MASK	0x20000000	// 29
#define MRLS_SERRM_OFFSET	29		// 29
#define CPF_SERRM_MASK	0x40000000	// 30
#define CPF_SERRM_OFFSET	30		// 30

// SHPC Command Register offsets
#define SLOT_STATE_OFFSET		0	// 1:0
#define POWER_LED_OFFSET		2	// 3:2
#define ATTENTION_LED_OFFSET	4	// 5:4
#define SLOT_CODE_OFFSET		6	// 7:6
#define SLOT_TGT_OFFSET			8	// 12:8
#define MODE1_SPEED_MODE_OFFSET	0	// 2:0
#define	MODE1_CODE_OFFSET		3	// 7:3
#define MODE2_SPEED_MODE_OFFSET	0	// 3:0
#define	MODE2_CODE_OFFSET		4	// 7:4
#define ALL_CODE_OFFSET			0	// 7:0

// slot states
enum hp_states {
	SLOT_DISABLE,		// slot disable
	SLOT_ENABLE,		// slot enable
};

// indicator values
enum mode_frequency {
	MODE_PCI_33,		// PCI 33Mhz
	MODE_PCI_66,		// PCI 66Mhz

	MODE_PCIX_66,		// PCI-X 66Mhz MODE 1 PARITY
	MODE_PCIX_100,		// PCI-X 100Mhz MODE 1 PARITY
	MODE_PCIX_133,		// PCI-X 133Mhz MODE 1 PARITY

	MODE_PCIX_ECC_66,	// PCI-X 66Mhz MODE 1 ECC
	MODE_PCIX_ECC_100,	// PCI-X 100Mhz MODE 1 ECC
	MODE_PCIX_ECC_133,	// PCI-X 133Mhz MODE 1 ECC

	MODE_PCIX_ECC_66_DDR,	// PCI-X MODE 2 66Mhz DDR 266MHZ
	MODE_PCIX_ECC_100_DDR,	// PCI-X MODE 2 100Mhz DDR 266MHZ
	MODE_PCIX_ECC_133_DDR,	// PCI-X MODE 2 133Mhz DDR 266MHZ

	MODE_PCIX_ECC_66_QDR,	// PCI-X MODE 2 66Mhz QDR 533MHZ
	MODE_PCIX_ECC_100_QDR,	// PCI-X MODE 2 100Mhz QDR 533MHZ
	MODE_PCIX_ECC_133_QDR,	// PCI-X MODE 2 133Mhz QDR 533MHZ
};

enum hp_indicators {
	INDICATOR_OFF,		// Indicator off state
	INDICATOR_ON,		// Indicator on state
	INDICATOR_BLINK,	// Indicator blink state
	INDICATOR_NORMAL,	// Indicator normal state
};


struct pci_resource {
	struct pci_resource * next;
	u32 base;
	u32 length;
};

struct resource_descriptor {
	u32 base;
	u32 limit;
};

struct irq_mapping {
	u8 barber_pole;
	u8 valid_INT;
	u8 interrupt[4];
};

struct resource_lists {
	struct pci_resource *mem_head;
	struct pci_resource *p_mem_head;
	struct pci_resource *io_head;
	struct pci_resource *bus_head;
	struct irq_mapping *irqs;
};

#define ROM_PHY_ADDR                    0x0F0000
#define ROM_PHY_LEN                     0x00ffff

#define NOT_ENOUGH_RESOURCES            0x0000000B
#define DEVICE_TYPE_NOT_SUPPORTED       0x0000000C

//
// Prototypes
//
extern int  amdshpc_resource_sort_and_combine (struct pci_resource **head);

//
// State-Machine Function
//
typedef long ( *SLOT_STATE_FUNCTION )(
				     void* shpc_context,
				     void* slot_context);

//
// SHPC Register Offsets
//
enum shpc_register_offset {
	SHPC_SLOTS_AVAILABLE1_REG_OFFSET        = 0x04,
	SHPC_SLOTS_AVAILABLE2_REG_OFFSET        = 0x08,
	SHPC_SLOT_CONFIG_REG_OFFSET                     = 0x0C,
	SHPC_SEC_BUS_CONFIG_REG_OFFSET          = 0x10,
	SHPC_COMMAND_REG_OFFSET                         = 0x14,
	SHPC_STATUS_REG_OFFSET                          = 0x16,
	SHPC_INT_LOCATOR_REG_OFFSET                     = 0x18,
	SHPC_SERR_LOCATOR_REG_OFFSET            = 0x1C,
	SHPC_SERR_INT_REG_OFFSET                        = 0x20,
	SHPC_LOGICAL_SLOT_REG_OFFSET            = 0x24,
	SHPC_PME_CONTROL_REG_OFFSET                     = 0x9C,
};

//
// SHPC Programming Interface
//
enum shpc_interface {
	SHPC_MODE1_INTERFACE   = 1,
	SHPC_MODE2_INTERFACE   = 2,
};

//
// Bus Speed/Mode
//
enum shpc_speed_mode {
	SHPC_BUS_CONV_33        = 0,
	SHPC_BUS_CONV_66,

	SHPC_BUS_PCIX_66,
	SHPC_BUS_PCIX_100,
	SHPC_BUS_PCIX_133,

	SHPC_BUS_PCIX_ECC_66,
	SHPC_BUS_PCIX_ECC_100,
	SHPC_BUS_PCIX_ECC_133,

	SHPC_BUS_PCIX_ECC_66_DDR,
	SHPC_BUS_PCIX_ECC_100_DDR,
	SHPC_BUS_PCIX_ECC_133_DDR,

	SHPC_BUS_PCIX_ECC_66_QDR,
	SHPC_BUS_PCIX_ECC_100_QDR,
	SHPC_BUS_PCIX_ECC_133_QDR,

	SHPC_BUS_MAX_MODES,
};

// Slot PCIX Capability
enum shpc_slot_pcix_cap {
	SHPC_SLOT_CONV                          = 0,
	SHPC_SLOT_PCIX_66                       = 1,
	SHPC_SLOT_PCIX_133                      = 3,
	SHPC_SLOT_PCIX_ECC_133_DDR      = 4,
	SHPC_SLOT_PCIX_ECC_133_QDR      = 5,
};

// Slot LEDs
enum shpc_slot_led {
	SHPC_LED_NO_CHANGE      = 0,
	SHPC_LED_ON             = 1,
	SHPC_LED_BLINK          = 2,
	SHPC_LED_OFF            = 3,
};

// LED commands
#define SHPC_PWR_LED_NO_CHANGE	0x0000
#define SHPC_PWR_LED_ON			0x0004
#define SHPC_PWR_LED_BLINK      0x0008
#define SHPC_PWR_LED_OFF        0x000c

#define SHPC_ATTN_LED_NO_CHANGE 0x0000
#define SHPC_ATTN_LED_ON		0x0010
#define SHPC_ATTN_LED_BLINK		0x0020
#define SHPC_ATTN_LED_OFF		0x0030

// slot states
#define SHPC_SLOT_NO_CHANGE		0x0000
#define SHPC_POWER_ONLY         0x0001
#define SHPC_ENABLE_SLOT        0x0002
#define SHPC_DISABLE_SLOT       0x0003

// Command register Command Codes
#define SHPC_SLOT_OPERATION             0x0000	// 7:6  (00xxxxxxb)
#define	SHPC_SET_BUS_SPEED_MODE1		0x0040	// 7:3	(01000xxxb)
#define	SHPC_SET_BUS_SPEED_MODE2		0x0050	// 7:4	(0101xxxxb)
#define SHPC_POWER_ONLY_ALL_SLOTS       0x0048	// 7:0  (01001000b)
#define SHPC_ENABLE_ALL_SLOTS           0x0049	// 7:0  (01001001b)

// SHPC Status
enum shpc_status {
	SHPC_STATUS_CLEARED     = 0,
	SHPC_STATUS_SET         = 1,
};

// SHPC Mask
enum shpc_mask {
	SHPC_UNMASKED   = 0,
	SHPC_MASKED     = 1,
};

// Slot MRL Sensor
enum shpc_slot_mrl {
	SHPC_MRL_CLOSED = 0,
	SHPC_MRL_OPEN   = 1,
};

// Slot Attn Button
enum shpc_slot_attn_button {
	SHPC_ATTN_BUTTON_RELEASED  = 0,
	SHPC_ATTN_BUTTON_PRESSED   = 1,
};

// Card Power Requirements
enum shpc_card_power {
	SHPC_CARD_PRESENT_7_5W  = 0,
	SHPC_CARD_PRESENT_15W   = 1,
	SHPC_CARD_PRESENT_25W   = 2,
	SHPC_SLOT_EMPTY         = 3,
};

// slot config structure
struct slot_config_info {
	u8      lu_slots_implemented;	// [ 4:0 ]Number of slots implemented
	u8      lu_base_FDN;		// [ 12:8 ]First Device Number
	u16     lu_base_PSN;		// [ 26:16 ]Physical Slot Number
	u8      lu_PSN_up;		// [ 29 ]PSN Up (1=TRUE, 0=FALSE)
};

// logical slot information
struct slot_status_info {
	u8      lu_slot_state;		 // [ 0 ]Slot state (1=Enabled, 0=Disabled)
	u8      lu_power_fault;		 // [ 1 ]Power-Fault? (1=TRUE, 0=FALSE)
	u8      lu_card_present;	 // [ 2 ]Card Present? (1=TRUE, 0=FALSE)
	u8      lu_card_power;		 // [ 4:3 ]Card Power Requirements (low/medium/high)
	u8      lu_card_mode_freq_cap;	 // [ 7:5 ]Card Speed/mode capability
	u8      lu_mrl_implemented;	 // [ 8 ]MRL Implemented? (1=TRUE, 0=FALSE)
	u8      lu_mrl_opened;		 // [ 9 ]MRL State (if implemented: 1=TRUE, 0=FALSE)
	u8      lu_ai_state;		 // [ 11:10 ]Attn Indicator State (Blink/On/Off)
	u8      lu_pi_state;		 // [ 13:12 ]Power Indicator State (Blink/On/Off)
	u8      lu_card_pci66_capable;	 // [ 15 ]Card PCI66 capability (1=TRUE, 0=FALSE)
	u8      lu_bus_mode_freq;	 // [ 18:16 ]Current Bus speed/mode
	u8      lu_max_bus_mode_freq;	 // [ 21:19 ]Maximum Bus speed/mode
	u8      lu_request_failed;	 // [ 31 ]Request Failed? (1=TRUE, 0=FALSE)
};

enum return_status {
	STATUS_UNSUCCESSFUL,
	STATUS_SUCCESS
};

// Async Request
enum shpc_async_request {
	SHPC_ASYNC_ENABLE_SLOT,
	SHPC_ASYNC_DISABLE_SLOT,
	SHPC_ASYNC_SURPRISE_REMOVE,
	SHPC_ASYNC_QUIESCE_DEVNODE,
	SHPC_ASYNC_QUIESCE_DEVNODE_QUIET,
	SHPC_ASYNC_QUIESCE_DEVNODE_NOTIFY,
	SHPC_ASYNC_CANCEL_QUIESCE_DEVNODE,
	SHPC_ASYNC_LED_LOCATE,
	SHPC_ASYNC_LED_NORMAL
};

// Async Request
struct async_request {
	enum shpc_async_request type;
	wait_queue_head_t       event;
	unsigned long           timeout;
	void                    *request_context;
};

// Async Completion
struct async_completion {
	enum shpc_async_request type;
	unsigned long                   timeout;
	u8                              hw_initiated;
	u8                              done;
	enum hp_boolean                 failed;
	void                            *request_context;
};

// ****************************************************************************
//
// async_callback() @ PASSIVE_LEVEL
//
// Parameters
//      driver_context - Pointer provided in hp_AddDevice()
//      slot_id - Zero-based slot number (0..n-1).
//      Request - Async request completed.  For example: Slot Enable/Disable, AttnLED Attn/Normal.
//      Status - Slot status at completion
//      request_context - Pointer provided in hp_StartAsyncRequest(), NULL for
//              completions on hardware-initiated requests.
//
// Return Value
//      For QUIESCE_DEVNODE request: #DevNodes associated with a particular slot, else 0.
//
// ****************************************************************************
typedef unsigned long ( *SHPC_ASYNC_CALLBACK )( void* driver_context,
						u8 slot_id,
						enum shpc_async_request Request,
						struct slot_status_info Status,
						void* request_context );

//
// Slot Context
//
struct slot_context {

	spinlock_t              slot_spinlock;
	struct semaphore        slot_event_bits_semaphore;
	struct semaphore        cmd_acquire_mutex;
	struct semaphore        bus_acquire_mutex;
	u32                     *logical_slot_addr;
	u8                      slot_number;
	u8                      slot_psn;
	u8                      split_transactions;
	u32                     quiesce_requests;
	u32                     quiesce_replies;
	u8                      slot_enabled;
	enum shpc_speed_mode    card_speed_mode_cap;
	u8                      card_pci66_capable;
	u8                      in_bus_speed_mode_contention;
	u8                      problem_detected;
	u8                      slot_quiesced;
	u8                      slot_occupied;
	struct tasklet_struct   attn_button_dpc;
	struct tasklet_struct   mrl_sensor_dpc;
	struct tasklet_struct   card_presence_dpc;
	struct tasklet_struct   isolated_power_fault_dpc;
	struct tasklet_struct   connected_power_fault_dpc;
	wait_queue_head_t       slot_event;
	wait_queue_head_t       led_cmd_acquire_event;
	wait_queue_head_t       led_cmd_release_event;
	wait_queue_head_t       cmd_acquire_event;
	wait_queue_head_t       cmd_release_event;
	wait_queue_head_t       bus_acquire_event;
	wait_queue_head_t       bus_release_event;
	u32                     slot_event_bits;
	void                    *slot_thread;
	void                    *attn_led_thread;
	SLOT_STATE_FUNCTION     slot_function;
	SLOT_STATE_FUNCTION     attn_led_function;
	struct async_request    slot_request;
	struct async_completion slot_completion;
	struct async_request    attn_led_request;
	struct async_completion attn_led_completion;
	void                    *shpc_context;
	struct timer_list       slot_timer1;
	struct timer_list       slot_timer2;
	struct timer_list       slot_timer3;
	struct timer_list       slot_timer4;
	struct timer_list       slot_timer5;
	struct timer_list       slot_timer6;
	struct timer_list       slot_timer7;
	struct timer_list       slot_timer8;
	struct timer_list       slot_timer9;
	struct timer_list       slot_timer10;
	struct timer_list       led_timer1;
	struct timer_list       led_timer2;
	struct timer_list       led_timer3;
	struct timer_list       led_timer4;
};

//
// SHPC Context
//
struct shpc_context {
	enum shpc_interface     shpc_interface;
	spinlock_t              shpc_spinlock;
	struct semaphore        shpc_event_bits_semaphore;
	void                    *mmio_base_addr;
	struct shpc_context     *next;
	u8                      first_slot;
	u8                      number_of_slots;
	u8                      slots_enabled;
	u8			split_transactions;
	u8                      at_power_device_d0;
	u8                      bus_released;
	u8                      slots_available[ SHPC_BUS_MAX_MODES ];
	enum shpc_speed_mode    max_speed_mode;
	enum shpc_speed_mode    bus_speed_mode;
	struct semaphore        cmd_available_mutex;
	struct tasklet_struct   cmd_completion_dpc;
	struct semaphore        bus_available_mutex;
	wait_queue_head_t       *user_event_pointer;
	u32                     shpc_event_bits;
	void                    *driver_context;
	SHPC_ASYNC_CALLBACK     async_callback;
	u32                     shpc_instance;
	struct slot_context     slot_context[ SHPC_MAX_NUM_SLOTS ];
	void                    *hpc_reg;		// cookie for our pci controller location
	struct pci_ops          *pci_ops;
	struct pci_resource     *mem_head;
	struct pci_resource     *p_mem_head;
	struct pci_resource     *io_head;
	struct pci_resource     *bus_head;
	struct pci_dev          *pci_dev;
	u8                      interrupt;
	u8                      bus;
	u8                      device;
	u8                      function;
	u16                     vendor_id;
	u32                     ctrl_int_comp;
	u8                      add_support;
};

//
// Function Prototypes
//
void adjust_pcix_capabilities(
			struct controller * ctrl,
			u32 pcix_slots,
			u32 max_split_transactions,
			u32 max_read_byte_count,
			u32 secondary_bus,
			u32 device
			);


int amdshpc_get_bus_dev (struct controller  *ctrl, u8 * bus_num, u8 * dev_num, u8 slot);
int amdshpc_process_SI (struct controller *ctrl, struct pci_func *func);
int amdshpc_process_SS (struct controller *ctrl, struct pci_func *func);
int amdshpc_find_available_resources (struct controller *ctrl, void *rom_start);
int amdshpc_save_config(struct controller *ctrl, int busnumber, struct slot_config_info * is_hot_plug);
struct pci_func *amdshpc_slot_create(u8 busnumber);


void hp_clear_shpc_event_bit(struct shpc_context * shpc_context, u32 mask);
void hp_set_shpc_event_bit(struct shpc_context * shpc_context, u32 mask);

void hp_clear_slot_event_bit(struct slot_context * slot_context, u32 mask);
void hp_set_slot_event_bit(struct slot_context * slot_context, u32 mask);

void hp_send_event_to_all_slots(struct shpc_context *shpc_context, u32 mask);
void hp_send_slot_event(struct slot_context *slot_context, u32 mask);

int hp_get_led_cmd_available_mutex_thread(void *slot_context);
int hp_get_cmd_available_mutex_thread    (void *slot_context);
int hp_get_bus_available_mutex_thread(void *slot_context);
int hp_cmd_available_mutex_thread(void * slot_context);
int hp_bus_available_mutex_thread(void * slot_context);
int hp_led_cmd_available_mutex_thread(void * slot_context);

void hp_slot_timer1_func(unsigned long data);
void hp_slot_timer2_func(unsigned long data);
void hp_slot_timer3_func(unsigned long data);
void hp_slot_timer4_func(unsigned long data);
void hp_slot_timer5_func(unsigned long data);
void hp_slot_timer6_func(unsigned long data);
void hp_slot_timer7_func(unsigned long data);
void hp_slot_timer8_func(unsigned long data);
void hp_slot_timer9_func(unsigned long data);
void hp_slot_timer10_func(unsigned long data);
void hp_led_timer1_func(unsigned long data);
void hp_led_timer2_func(unsigned long data);
void hp_led_timer3_func(unsigned long data);
void hp_led_timer4_func(unsigned long data);

irqreturn_t hp_interrupt_service(int IRQ, void *v, struct pt_regs *regs);

u32 board_replaced(struct pci_func * func, struct controller  * ctrl);
struct pci_func *amdshpc_slot_find(u8 bus, u8 device, u8 index);
int amdshpc_save_base_addr_length(struct controller  *ctrl, struct pci_func * func);
int amdshpc_save_used_resources (struct controller  *ctrl, struct pci_func * func);
int amdshpc_return_board_resources(struct pci_func * func, struct resource_lists * resources);
int amdshpc_save_slot_config (struct controller  *ctrl, struct pci_func * new_slot);
int amdshpc_configure_device (struct controller * ctrl, struct pci_func* func);
int amdshpc_unconfigure_device(struct pci_func* func);



void
	hp_attn_button_dpc(
			  unsigned long deferred_context
			  );

void
	hp_mrl_sensor_dpc(
			 unsigned long deferred_context
			 );

void
	hp_card_presence_dpc(
			    unsigned long deferred_context
			    );

void
	hp_isolated_power_fault_dpc(
				   unsigned long deferred_context
				   );

void
	hp_connected_power_fault_dpc(
				    unsigned long deferred_context
				    );

void
	hp_cmd_completion_dpc(
			     unsigned long deferred_context
			     );

int
	hp_slot_thread(
		      void* slot_context
		      );

long
	hp_at_slot_disabled_wait_for_slot_request(
						 struct shpc_context* shpc_context,
						 struct slot_context* slot_context
						 );

long
	hp_at_slot_disabled_wait_for_led_cmd_available(
						      struct shpc_context* shpc_context,
						      struct slot_context* slot_context
						      );

long
	hp_at_slot_disabled_wait_for_led_cmd_completion(
						       struct shpc_context* shpc_context,
						       struct slot_context* slot_context
						       );

long
	hp_at_slot_disabled_wait_for_timeout(
					    struct shpc_context* shpc_context,
					    struct slot_context* slot_context
					    );

long
	hp_at_slot_disabled_wait_for_power_cmd_available(
							struct shpc_context* shpc_context,
							struct slot_context* slot_context
							);

long
	hp_at_slot_disabled_wait_for_power_cmd_timeout(
						      struct shpc_context* shpc_context,
						      struct slot_context* slot_context
						      );

long
	hp_at_slot_disabled_wait_for_power_cmd_completion(
							 struct shpc_context* shpc_context,
							 struct slot_context* slot_context
							 );

long
	hp_at_slot_disabled_wait_for_bus_available(
						  struct shpc_context* shpc_context,
						  struct slot_context* slot_context
						  );

long
	hp_at_slot_disabled_wait_for_bus_released(
						 struct shpc_context* shpc_context,
						 struct slot_context* slot_context
						 );

long
	hp_at_slot_disabled_wait_for_speed_mode_cmd_available(
							     struct shpc_context* shpc_context,
							     struct slot_context* slot_context
							     );

long
	hp_at_slot_disabled_wait_for_speed_mode_cmd_completion(
							      struct shpc_context* shpc_context,
							      struct slot_context* slot_context
							      );

long
	hp_at_slot_disabled_wait_for_enable_cmd_available(
							 struct shpc_context* shpc_context,
							 struct slot_context* slot_context
							 );

long
	hp_at_slot_disabled_wait_for_enable_cmd_completion(
							  struct shpc_context* shpc_context,
							  struct slot_context* slot_context
							  );

long
	hp_at_slot_disabled_wait_for_enable_timeout(
						   struct shpc_context* shpc_context,
						   struct slot_context* slot_context
						   );

long
	hp_to_slot_disabled_wait_for_led_cmd_available(
						      struct shpc_context* shpc_context,
						      struct slot_context* slot_context
						      );

long
	hp_to_slot_disabled_wait_for_led_cmd_completion(
						       struct shpc_context* shpc_context,
						       struct slot_context* slot_context
						       );

long
	hp_to_slot_disabled_wait_for_disable_cmd_available(
							  struct shpc_context* shpc_context,
							  struct slot_context* slot_context
							  );

long
	hp_to_slot_disabled_wait_for_disable_cmd_completion(
							   struct shpc_context* shpc_context,
							   struct slot_context* slot_context
							   );

long
	hp_to_slot_disabled_wait_for_disable_timeout(
						    struct shpc_context* shpc_context,
						    struct slot_context* slot_context
						    );

long
	hp_to_slot_disabled_wait_for_bus_available(
						  struct shpc_context* shpc_context,
						  struct slot_context* slot_context
						  );

long
	hp_at_slot_enabled_wait_for_slot_request(
						struct shpc_context* shpc_context,
						struct slot_context* slot_context
						);

long
	hp_at_slot_enabled_wait_for_stop_on_bus_rebalance(
							 struct shpc_context* shpc_context,
							 struct slot_context* slot_context
							 );

long
	hp_at_slot_enabled_wait_for_power_cmd_available(
						       struct shpc_context* shpc_context,
						       struct slot_context* slot_context
						       );

long
	hp_at_slot_enabled_wait_for_power_cmd_completion(
							struct shpc_context* shpc_context,
							struct slot_context* slot_context
							);

long
	hp_at_slot_enabled_wait_for_led_cmd_available(
						     struct shpc_context* shpc_context,
						     struct slot_context* slot_context
						     );

long
	hp_at_slot_enabled_wait_for_led_cmd_completion(
						      struct shpc_context* shpc_context,
						      struct slot_context* slot_context
						      );

long
	hp_at_slot_enabled_wait_for_timeout(
					   struct shpc_context* shpc_context,
					   struct slot_context* slot_context
					   );

long
	hp_at_slot_enabled_wait_for_stop_on_slot_disable(
							struct shpc_context* shpc_context,
							struct slot_context* slot_context
							);

long
	hp_at_slot_enabled_wait_for_stop_on_slot_disable_quiet(
							      struct shpc_context* shpc_context,
							      struct slot_context* slot_context
							      );

long
	hp_to_slot_enabled_wait_for_led_cmd_available(
						     struct shpc_context* shpc_context,
						     struct slot_context* slot_context
						     );

long
	hp_to_slot_enabled_wait_for_led_cmd_completion(
						      struct shpc_context* shpc_context,
						      struct slot_context* slot_context
						      );

void
	hp_get_slot_configuration(
				 struct shpc_context* shpc_context
				 );

void
	hp_enable_slot_interrupts(
				 struct slot_context* slot_context
				 );

void
	hp_disable_slot_interrupts(
				  struct slot_context* slot_context
				  );

void
	hp_enable_global_interrupts(
				   struct shpc_context* shpc_context
				   );

void
	hp_disable_global_interrupts(
				    struct shpc_context* shpc_context
				    );

enum shpc_speed_mode
	hp_get_bus_speed_mode(
			     struct shpc_context* shpc_context
			     );

enum shpc_speed_mode
	hp_get_card_speed_mode_cap(
				  struct slot_context* slot_context
				  );

enum mode_frequency
	hp_translate_speed_mode(
			       enum shpc_speed_mode shpc_speed_mode
			       );

enum hp_power_requirements
	hp_translate_card_power(
			       enum shpc_card_power ShpcCardPower
			       );

enum hp_indicators
	hp_translate_indicator(
			      enum shpc_slot_led ShpcIndicator
			      );

u8
	hp_flag_slot_as_enabled(
			       struct shpc_context* shpc_context,
			       struct slot_context* slot_context
			       );

u8
	hp_flag_slot_as_disabled(
				struct shpc_context* shpc_context,
				struct slot_context* slot_context
				);

u8
	hp_signal_enabled_slots_to_rebalance_bus(
						struct shpc_context* shpc_context
						);

enum shpc_speed_mode
	hp_GetMaxSpeedModeForEnabledSlots(
					 struct shpc_context* shpc_context,
					 enum shpc_speed_mode From_speed_mode
					 );

void
	hp_signal_user_event(
			    struct shpc_context* shpc_context
			    );

void
	hp_signal_user_event_at_dpc_level(
					 struct shpc_context* shpc_context
					 );

int
	hp_attn_led_thread(
			  void* slot_context
			  );

long
	hp_wait_for_attn_led_request(
				    struct shpc_context* shpc_context,
				    struct slot_context* slot_context
				    );

long
	hp_wait_for_attn_led_blink_cmd_available(
						struct shpc_context* shpc_context,
						struct slot_context* slot_context
						);

long
	hp_wait_for_attn_led_blink_cmd_completion(
						 struct shpc_context* shpc_context,
						 struct slot_context* slot_context
						 );

long
	hp_wait_for_attn_led_blink_timeout(
					  struct shpc_context* shpc_context,
					  struct slot_context* slot_context
					  );

long
	hp_wait_for_attn_led_normal_cmd_available(
						 struct shpc_context* shpc_context,
						 struct slot_context* slot_context
						 );

long
	hp_wait_for_attn_led_normal_cmd_completion(
						  struct shpc_context* shpc_context,
						  struct slot_context* slot_context
						  );

long
	hp_wait_for_attn_led_back_to_normal_cmd_available(
							 struct shpc_context* shpc_context,
							 struct slot_context* slot_context
							 );

long
	hp_wait_for_attn_led_back_to_normal_cmd_completion(
							  struct shpc_context* shpc_context,
							  struct slot_context* slot_context
							  );



#endif  // _SHPC_H_
