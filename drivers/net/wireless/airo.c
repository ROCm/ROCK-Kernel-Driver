/*======================================================================

    Aironet driver for 4500 and 4800 series cards

    This code is released under both the GPL version 2 and BSD licenses.
    Either license may be used.  The respective licenses are found at
    the end of this file.

    This code was developed by Benjamin Reed <breed@users.sourceforge.net>
    including portions of which come from the Aironet PC4500
    Developer's Reference Manual and used with permission.  Copyright
    (C) 1999 Benjamin Reed.  All Rights Reserved.  Permission to use
    code in the Developer's manual was granted for this driver by
    Aironet.  Major code contributions were received from Javier Achirica
    and Jean Tourrilhes <jt@hpl.hp.com>.  Code was also integrated from
    the Cisco Aironet driver for Linux.

======================================================================*/

#include <linux/config.h>
#include <linux/version.h>
#include <asm/segment.h>
#include <linux/init.h>

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/proc_fs.h>

#include <linux/sched.h>
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/in.h>
#include <asm/io.h>
#include <asm/system.h>
#include <asm/bitops.h>

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/if_arp.h>
#include <linux/ioport.h>
#include <linux/config.h>
#include <linux/pci.h>
#include <asm/uaccess.h>

#ifdef CONFIG_PCI
static struct pci_device_id card_ids[] = __devinitdata {
	{ 0x14b9, 1, PCI_ANY_ID, PCI_ANY_ID, },
	{ 0x14b9, 0x4500, PCI_ANY_ID, PCI_ANY_ID },
	{ 0x14b9, 0x4800, PCI_ANY_ID, PCI_ANY_ID, },
	{ 0x14b9, 0x0340, PCI_ANY_ID, PCI_ANY_ID, },
	{ 0x14b9, 0x0350, PCI_ANY_ID, PCI_ANY_ID, },
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, card_ids);

static int airo_pci_probe(struct pci_dev *, const struct pci_device_id *);
static void airo_pci_remove(struct pci_dev *);

static struct pci_driver airo_driver = {
	name:     "airo",
	id_table: card_ids,
	probe:    airo_pci_probe,
	remove:   airo_pci_remove,
};
#endif /* CONFIG_PCI */

/* Include Wireless Extension definition and check version - Jean II */
#include <linux/wireless.h>
#define WIRELESS_SPY		// enable iwspy support
#if WIRELESS_EXT < 9
#warning "Wireless extension v9 or newer required - please upgrade your kernel"
#undef WIRELESS_EXT
#undef WIRELESS_SPY
#endif
#define CISCO_EXT		// enable Cisco extensions

#ifdef CISCO_EXT
#include <linux/delay.h>
#endif

/* As you can see this list is HUGH!
   I really don't know what a lot of these counts are about, but they
   are all here for completeness.  If the IGNLABEL macro is put in
   infront of the label, that statistic will not be included in the list
   of statistics in the /proc filesystem */

#define IGNLABEL 0&(int)
static char *statsLabels[] = {
	"RxOverrun",
	IGNLABEL "RxPlcpCrcErr",
	IGNLABEL "RxPlcpFormatErr",
	IGNLABEL "RxPlcpLengthErr",
	"RxMacCrcErr",
	"RxMacCrcOk",
	"RxWepErr",
	"RxWepOk",
	"RetryLong",
	"RetryShort",
	"MaxRetries",
	"NoAck",
	"NoCts",
	"RxAck",
	"RxCts",
	"TxAck",
	"TxRts",
	"TxCts",
	"TxMc",
	"TxBc",
	"TxUcFrags",
	"TxUcPackets",
	"TxBeacon",
	"RxBeacon",
	"TxSinColl",
	"TxMulColl",
	"DefersNo",
	"DefersProt",
	"DefersEngy",
	"DupFram",
	"RxFragDisc",
	"TxAged",
	"RxAged",
	"LostSync-MaxRetry",
	"LostSync-MissedBeacons",
	"LostSync-ArlExceeded",
	"LostSync-Deauth",
	"LostSync-Disassoced",
	"LostSync-TsfTiming",
	"HostTxMc",
	"HostTxBc",
	"HostTxUc",
	"HostTxFail",
	"HostRxMc",
	"HostRxBc",
	"HostRxUc",
	"HostRxDiscard",
	IGNLABEL "HmacTxMc",
	IGNLABEL "HmacTxBc",
	IGNLABEL "HmacTxUc",
	IGNLABEL "HmacTxFail",
	IGNLABEL "HmacRxMc",
	IGNLABEL "HmacRxBc",
	IGNLABEL "HmacRxUc",
	IGNLABEL "HmacRxDiscard",
	IGNLABEL "HmacRxAccepted",
	"SsidMismatch",
	"ApMismatch",
	"RatesMismatch",
	"AuthReject",
	"AuthTimeout",
	"AssocReject",
	"AssocTimeout",
	IGNLABEL "ReasonOutsideTable",
	IGNLABEL "ReasonStatus1",
	IGNLABEL "ReasonStatus2",
	IGNLABEL "ReasonStatus3",
	IGNLABEL "ReasonStatus4",
	IGNLABEL "ReasonStatus5",
	IGNLABEL "ReasonStatus6",
	IGNLABEL "ReasonStatus7",
	IGNLABEL "ReasonStatus8",
	IGNLABEL "ReasonStatus9",
	IGNLABEL "ReasonStatus10",
	IGNLABEL "ReasonStatus11",
	IGNLABEL "ReasonStatus12",
	IGNLABEL "ReasonStatus13",
	IGNLABEL "ReasonStatus14",
	IGNLABEL "ReasonStatus15",
	IGNLABEL "ReasonStatus16",
	IGNLABEL "ReasonStatus17",
	IGNLABEL "ReasonStatus18",
	IGNLABEL "ReasonStatus19",
	"RxMan",
	"TxMan",
	"RxRefresh",
	"TxRefresh",
	"RxPoll",
	"TxPoll",
	"HostRetries",
	"LostSync-HostReq",
	"HostTxBytes",
	"HostRxBytes",
	"ElapsedUsec",
	"ElapsedSec",
	"LostSyncBetterAP",
	"PrivacyMismatch",
	"Jammed",
	"DiscRxNotWepped",
	"PhyEleMismatch",
	(char*)-1 };
#ifndef RUN_AT
#define RUN_AT(x) (jiffies+(x))
#endif


/* These variables are for insmod, since it seems that the rates
   can only be set in setup_card.  Rates should be a comma separated
   (no spaces) list of rates (up to 8). */

static int rates[8];
static int basic_rate;
static char *ssids[3];

static int io[4];
static int irq[4];

static
int maxencrypt /* = 0 */; /* The highest rate that the card can encrypt at.
		       0 means no limit.  For old cards this was 4 */

static int auto_wep /* = 0 */; /* If set, it tries to figure out the wep mode */
static int aux_bap /* = 0 */; /* Checks to see if the aux ports are needed to read
		    the bap, needed on some older cards and buses. */
static int adhoc;

static int proc_uid /* = 0 */;

static int proc_gid /* = 0 */;

static int airo_perm = 0555;

static int proc_perm = 0644;

MODULE_AUTHOR("Benjamin Reed");
MODULE_DESCRIPTION("Support for Cisco/Aironet 802.11 wireless ethernet \
                   cards.  Direct support for ISA/PCI cards and support \
		   for PCMCIA when used with airo_cs.");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_SUPPORTED_DEVICE("Aironet 4500, 4800 and Cisco 340");
MODULE_PARM(io,"1-4i");
MODULE_PARM(irq,"1-4i");
MODULE_PARM(basic_rate,"i");
MODULE_PARM(rates,"1-8i");
MODULE_PARM(ssids,"1-3s");
MODULE_PARM(auto_wep,"i");
MODULE_PARM_DESC(auto_wep, "If non-zero, the driver will keep looping through \
the authentication options until an association is made.  The value of \
auto_wep is number of the wep keys to check.  A value of 2 will try using \
the key at index 0 and index 1.");
MODULE_PARM(aux_bap,"i");
MODULE_PARM_DESC(aux_bap, "If non-zero, the driver will switch into a mode \
than seems to work better for older cards with some older buses.  Before \
switching it checks that the switch is needed.");
MODULE_PARM(maxencrypt, "i");
MODULE_PARM_DESC(maxencrypt, "The maximum speed that the card can do \
encryption.  Units are in 512kbs.  Zero (default) means there is no limit. \
Older cards used to be limited to 2mbs (4).");
MODULE_PARM(adhoc, "i");
MODULE_PARM_DESC(adhoc, "If non-zero, the card will start in adhoc mode.");

MODULE_PARM(proc_uid, "i");
MODULE_PARM_DESC(proc_uid, "The uid that the /proc files will belong to.");
MODULE_PARM(proc_gid, "i");
MODULE_PARM_DESC(proc_gid, "The gid that the /proc files will belong to.");
MODULE_PARM(airo_perm, "i");
MODULE_PARM_DESC(airo_perm, "The permission bits of /proc/[driver/]aironet.");
MODULE_PARM(proc_perm, "i");
MODULE_PARM_DESC(proc_perm, "The permission bits of the files in /proc");

#include <asm/uaccess.h>

/* This is a kind of sloppy hack to get this information to OUT4500 and
   IN4500.  I would be extremely interested in the situation where this
   doesnt work though!!! */
static int do8bitIO = 0;

/* Return codes */
#define SUCCESS 0
#define ERROR -1
#define NO_PACKET -2

/* Commands */
#define NOP 0x0010
#define MAC_ENABLE 0x0001
#define MAC_DISABLE 0x0002
#define CMD_LOSE_SYNC 0x0003 /* Not sure what this does... */
#define CMD_ACCESS 0x0021
#define CMD_ALLOCATETX 0x000a
#define CMD_TRANSMIT 0x000b
#define HOSTSLEEP 0x85
#define CMD_SETMODE 0x0009
#define CMD_ENABLEAUX 0x0111
#define CMD_SOFTRESET 0x0004
#define CMD_LISTBSS 0x0103

/* Registers */
#define COMMAND 0x00
#define PARAM0 0x02
#define PARAM1 0x04
#define PARAM2 0x06
#define STATUS 0x08
#define RESP0 0x0a
#define RESP1 0x0c
#define RESP2 0x0e
#define LINKSTAT 0x10
#define SELECT0 0x18
#define OFFSET0 0x1c
#define RXFID 0x20
#define TXALLOCFID 0x22
#define TXCOMPLFID 0x24
#define DATA0 0x36
#define EVSTAT 0x30
#define EVINTEN 0x32
#define EVACK 0x34
#define SWS0 0x28
#define SWS1 0x2a
#define SWS2 0x2c
#define SWS3 0x2e
#define AUXPAGE 0x3A
#define AUXOFF 0x3C
#define AUXDATA 0x3E

/* BAP selectors */
#define BAP0 0 // Used for receiving packets
#define BAP1 2 // Used for xmiting packets and working with RIDS

/* Flags */
#define COMMAND_BUSY 0x8000

#define BAP_BUSY 0x8000
#define BAP_ERR 0x4000
#define BAP_DONE 0x2000

#define PROMISC 0xffff
#define NOPROMISC 0x0000

#define EV_CMD 0x10
#define EV_CLEARCOMMANDBUSY 0x4000
#define EV_RX 0x01
#define EV_TX 0x02
#define EV_TXEXC 0x04
#define EV_ALLOC 0x08
#define EV_LINK 0x80
#define EV_AWAKE 0x100
#define EV_UNKNOWN 0x800
#define STATUS_INTS ( EV_AWAKE | EV_LINK | EV_TXEXC | EV_TX | EV_RX)
#define IGNORE_INTS ( EV_CMD | EV_UNKNOWN)

/* The RIDs */
#define RID_CAPABILITIES 0xFF00
#define RID_APINFO     0xFF01
#define RID_RADIOINFO  0xFF02
#define RID_UNKNOWN3   0xFF03
#define RID_RSSI       0xFF04
#define RID_CONFIG     0xFF10
#define RID_SSID       0xFF11
#define RID_APLIST     0xFF12
#define RID_DRVNAME    0xFF13
#define RID_ETHERENCAP 0xFF14
#define RID_WEP_TEMP   0xFF15
#define RID_WEP_PERM   0xFF16
#define RID_MODULATION 0xFF17
#define RID_OPTIONS    0xFF18
#define RID_ACTUALCONFIG 0xFF20 /*readonly*/
#define RID_FACTORYCONFIG 0xFF21
#define RID_UNKNOWN22  0xFF22
#define RID_LEAPUSERNAME 0xFF23
#define RID_LEAPPASSWORD 0xFF24
#define RID_STATUS     0xFF50
#define RID_UNKNOWN52  0xFF52
#define RID_UNKNOWN54  0xFF54
#define RID_UNKNOWN55  0xFF55
#define RID_UNKNOWN56  0xFF56
#define RID_STATS16    0xFF60
#define RID_STATS16DELTA 0xFF61
#define RID_STATS16DELTACLEAR 0xFF62
#define RID_STATS      0xFF68
#define RID_STATSDELTA 0xFF69
#define RID_STATSDELTACLEAR 0xFF6A
#define RID_UNKNOWN70  0xFF70
#define RID_UNKNOWN71  0xFF71
#define RID_BSSLISTFIRST 0xFF72
#define RID_BSSLISTNEXT  0xFF73

typedef struct {
	u16 cmd;
	u16 parm0;
	u16 parm1;
	u16 parm2;
} Cmd;

typedef struct {
	u16 status;
	u16 rsp0;
	u16 rsp1;
	u16 rsp2;
} Resp;

/*
 * Rids and endian-ness:  The Rids will always be in cpu endian, since
 * this all the patches from the big-endian guys end up doing that.
 * so all rid access should use the read/writeXXXRid routines.
 */

/* This is redundant for x86 archs, but it seems necessary for ARM */
#pragma pack(1)

/* This structure came from an email sent to me from an engineer at
   aironet for inclusion into this driver */
typedef struct {
	u16 len;
	u16 kindex;
	u8 mac[6];
	u16 klen;
	u8 key[16];
} WepKeyRid;

/* These structures are from the Aironet's PC4500 Developers Manual */
typedef struct {
	u16 len;
	u8 ssid[32];
} Ssid;

typedef struct {
	u16 len;
	Ssid ssids[3];
} SsidRid;

typedef struct {
        u16 len;
        u16 modulation;
#define MOD_DEFAULT 0
#define MOD_CCK 1
#define MOD_MOK 2
} ModulationRid;

typedef struct {
	u16 len; /* sizeof(ConfigRid) */
	u16 opmode; /* operating mode */
#define MODE_STA_IBSS 0
#define MODE_STA_ESS 1
#define MODE_AP 2
#define MODE_AP_RPTR 3
#define MODE_ETHERNET_HOST (0<<8) /* rx payloads converted */
#define MODE_LLC_HOST (1<<8) /* rx payloads left as is */
#define MODE_AIRONET_EXTEND (1<<9) /* enable Aironet extenstions */
#define MODE_AP_INTERFACE (1<<10) /* enable ap interface extensions */
#define MODE_ANTENNA_ALIGN (1<<11) /* enable antenna alignment */
#define MODE_ETHER_LLC (1<<12) /* enable ethernet LLC */
#define MODE_LEAF_NODE (1<<13) /* enable leaf node bridge */
#define MODE_CF_POLLABLE (1<<14) /* enable CF pollable */
	u16 rmode; /* receive mode */
#define RXMODE_BC_MC_ADDR 0
#define RXMODE_BC_ADDR 1 /* ignore multicasts */
#define RXMODE_ADDR 2 /* ignore multicast and broadcast */
#define RXMODE_RFMON 3 /* wireless monitor mode */
#define RXMODE_RFMON_ANYBSS 4
#define RXMODE_LANMON 5 /* lan style monitor -- data packets only */
#define RXMODE_DISABLE_802_3_HEADER (1<<8) /* disables 802.3 header on rx */
#define RXMODE_NORMALIZED_RSSI (1<<9) /* return normalized RSSI */
	u16 fragThresh;
	u16 rtsThres;
	u8 macAddr[6];
	u8 rates[8];
	u16 shortRetryLimit;
	u16 longRetryLimit;
	u16 txLifetime; /* in kusec */
	u16 rxLifetime; /* in kusec */
	u16 stationary;
	u16 ordering;
	u16 u16deviceType; /* for overriding device type */
	u16 cfpRate;
	u16 cfpDuration;
	u16 _reserved1[3];
	/*---------- Scanning/Associating ----------*/
	u16 scanMode;
#define SCANMODE_ACTIVE 0
#define SCANMODE_PASSIVE 1
#define SCANMODE_AIROSCAN 2
	u16 probeDelay; /* in kusec */
	u16 probeEnergyTimeout; /* in kusec */
        u16 probeResponseTimeout;
	u16 beaconListenTimeout;
	u16 joinNetTimeout;
	u16 authTimeout;
	u16 authType;
#define AUTH_OPEN 0x1
#define AUTH_ENCRYPT 0x101
#define AUTH_SHAREDKEY 0x102
#define AUTH_ALLOW_UNENCRYPTED 0x200
	u16 associationTimeout;
	u16 specifiedApTimeout;
	u16 offlineScanInterval;
	u16 offlineScanDuration;
	u16 linkLossDelay;
	u16 maxBeaconLostTime;
	u16 refreshInterval;
#define DISABLE_REFRESH 0xFFFF
	u16 _reserved1a[1];
	/*---------- Power save operation ----------*/
	u16 powerSaveMode;
#define POWERSAVE_CAM 0
#define POWERSAVE_PSP 1
#define POWERSAVE_PSPCAM 2
	u16 sleepForDtims;
	u16 listenInterval;
	u16 fastListenInterval;
	u16 listenDecay;
	u16 fastListenDelay;
	u16 _reserved2[2];
	/*---------- Ap/Ibss config items ----------*/
	u16 beaconPeriod;
	u16 atimDuration;
	u16 hopPeriod;
	u16 channelSet;
	u16 channel;
	u16 dtimPeriod;
	u16 bridgeDistance;
	u16 radioID;
	/*---------- Radio configuration ----------*/
	u16 radioType;
#define RADIOTYPE_DEFAULT 0
#define RADIOTYPE_802_11 1
#define RADIOTYPE_LEGACY 2
	u8 rxDiversity;
	u8 txDiversity;
	u16 txPower;
#define TXPOWER_DEFAULT 0
	u16 rssiThreshold;
#define RSSI_DEFAULT 0
        u16 modulation;
#define PREAMBLE_AUTO 0
#define PREAMBLE_LONG 1
#define PREAMBLE_SHORT 2
	u16 preamble;
	u16 homeProduct;
	u16 radioSpecific;
	/*---------- Aironet Extensions ----------*/
	u8 nodeName[16];
	u16 arlThreshold;
	u16 arlDecay;
	u16 arlDelay;
	u16 _reserved4[1];
	/*---------- Aironet Extensions ----------*/
	u16 magicAction;
#define MAGIC_ACTION_STSCHG 1
#define MACIC_ACTION_RESUME 2
#define MAGIC_IGNORE_MCAST (1<<8)
#define MAGIC_IGNORE_BCAST (1<<9)
#define MAGIC_SWITCH_TO_PSP (0<<10)
#define MAGIC_STAY_IN_CAM (1<<10)
	u16 magicControl;
	u16 autoWake;
} ConfigRid;

typedef struct {
	u16 len;
	u8 mac[6];
	u16 mode;
	u16 errorCode;
	u16 sigQuality;
	u16 SSIDlen;
	char SSID[32];
	char apName[16];
	char bssid[4][6];
	u16 beaconPeriod;
	u16 dimPeriod;
	u16 atimDuration;
	u16 hopPeriod;
	u16 channelSet;
	u16 channel;
	u16 hopsToBackbone;
	u16 apTotalLoad;
	u16 generatedLoad;
	u16 accumulatedArl;
	u16 signalQuality;
	u16 currentXmitRate;
	u16 apDevExtensions;
	u16 normalizedSignalStrength;
	u16 _reserved[10];
} StatusRid;

typedef struct {
	u16 len;
	u16 spacer;
	u32 vals[100];
} StatsRid;


typedef struct {
	u16 len;
	u8 ap[4][6];
} APListRid;

typedef struct {
	u16 len;
	char oui[3];
	char zero;
	u16 prodNum;
	char manName[32];
	char prodName[16];
	char prodVer[8];
	char factoryAddr[6];
	char aironetAddr[6];
	u16 radioType;
	u16 country;
	char callid[6];
	char supportedRates[8];
	char rxDiversity;
	char txDiversity;
	u16 txPowerLevels[8];
	u16 hardVer;
	u16 hardCap;
	u16 tempRange;
	u16 softVer;
	u16 softSubVer;
	u16 interfaceVer;
	u16 softCap;
	u16 bootBlockVer;
	u16 requiredHard;
} CapabilityRid;

typedef struct {
  u16 len;
  u16 index; /* First is 0 and 0xffff means end of list */
#define RADIO_FH 1 /* Frequency hopping radio type */
#define RADIO_DS 2 /* Direct sequence radio type */
#define RADIO_TMA 4 /* Proprietary radio used in old cards (2500) */
  u16 radioType;
  u8 bssid[6]; /* Mac address of the BSS */
  u8 zero;
  u8 ssidLen;
  u8 ssid[32];
  u16 rssi;
#define CAP_ESS (1<<0)
#define CAP_IBSS (1<<1)
#define CAP_PRIVACY (1<<4)
#define CAP_SHORTHDR (1<<5)
  u16 cap;
  u16 beaconInterval;
  u8 rates[8]; /* Same as rates for config rid */
  struct { /* For frequency hopping only */
    u16 dwell;
    u8 hopSet;
    u8 hopPattern;
    u8 hopIndex;
    u8 fill;
  } fh;
  u16 dsChannel;
  u16 atimWindow;
} BSSListRid;

typedef struct {
  u8 rssipct;
  u8 rssidBm;
} tdsRssiEntry;

typedef struct {
  u16 len;
  tdsRssiEntry x[256];
} tdsRssiRid;

#pragma pack()

#define TXCTL_TXOK (1<<1) /* report if tx is ok */
#define TXCTL_TXEX (1<<2) /* report if tx fails */
#define TXCTL_802_3 (0<<3) /* 802.3 packet */
#define TXCTL_802_11 (1<<3) /* 802.11 mac packet */
#define TXCTL_ETHERNET (0<<4) /* payload has ethertype */
#define TXCTL_LLC (1<<4) /* payload is llc */
#define TXCTL_RELEASE (0<<5) /* release after completion */
#define TXCTL_NORELEASE (1<<5) /* on completion returns to host */

#define BUSY_FID 0x10000

#ifdef CISCO_EXT
#define AIROMAGIC	0xa55a
/* Warning : SIOCDEVPRIVATE may disapear during 2.5.X - Jean II */
#ifdef SIOCIWFIRSTPRIV
#ifdef SIOCDEVPRIVATE
#define AIROOLDIOCTL	SIOCDEVPRIVATE
#define AIROOLDIDIFC 	AIROOLDIOCTL + 1
#endif /* SIOCDEVPRIVATE */
#else /* SIOCIWFIRSTPRIV */
#define SIOCIWFIRSTPRIV SIOCDEVPRIVATE
#endif /* SIOCIWFIRSTPRIV */
#define AIROIOCTL	SIOCIWFIRSTPRIV
#define AIROIDIFC 	AIROIOCTL + 1

/* Ioctl constants to be used in airo_ioctl.command */

#define	AIROGCAP  		0	// Capability rid
#define AIROGCFG		1       // USED A LOT
#define AIROGSLIST		2	// System ID list
#define AIROGVLIST		3       // List of specified AP's
#define AIROGDRVNAM		4	//  NOTUSED
#define AIROGEHTENC		5	// NOTUSED
#define AIROGWEPKTMP		6
#define AIROGWEPKNV		7
#define AIROGSTAT		8
#define AIROGSTATSC32		9
#define AIROGSTATSD32		10

/* Leave gap of 40 commands after AIROGSTATSD32 for future */

#define AIROPCAP               	AIROGSTATSD32 + 40
#define AIROPVLIST              AIROPCAP      + 1
#define AIROPSLIST		AIROPVLIST    + 1
#define AIROPCFG		AIROPSLIST    + 1
#define AIROPSIDS		AIROPCFG      + 1
#define AIROPAPLIST		AIROPSIDS     + 1
#define AIROPMACON		AIROPAPLIST   + 1	/* Enable mac  */
#define AIROPMACOFF		AIROPMACON    + 1 	/* Disable mac */
#define AIROPSTCLR		AIROPMACOFF   + 1
#define AIROPWEPKEY		AIROPSTCLR    + 1
#define AIROPWEPKEYNV		AIROPWEPKEY   + 1
#define AIROPLEAPPWD            AIROPWEPKEYNV + 1
#define AIROPLEAPUSR            AIROPLEAPPWD  + 1

/* Flash codes */

#define AIROFLSHRST	       AIROPWEPKEYNV  + 40
#define AIROFLSHGCHR           AIROFLSHRST    + 1
#define AIROFLSHSTFL           AIROFLSHGCHR   + 1
#define AIROFLSHPCHR           AIROFLSHSTFL   + 1
#define AIROFLPUTBUF           AIROFLSHPCHR   + 1
#define AIRORESTART            AIROFLPUTBUF   + 1

#define FLASHSIZE	32768

typedef struct aironet_ioctl {
	unsigned short command;	// What to do
	unsigned short len;		// Len of data
	unsigned char *data;		// d-data
} aironet_ioctl;
#endif /* CISCO_EXT */

#ifdef WIRELESS_EXT
// Frequency list (map channels to frequencies)
const long frequency_list[] = { 2412, 2417, 2422, 2427, 2432, 2437, 2442,
				2447, 2452, 2457, 2462, 2467, 2472, 2484 };

// A few details needed for WEP (Wireless Equivalent Privacy)
#define MAX_KEY_SIZE 13			// 128 (?) bits
#define MIN_KEY_SIZE  5			// 40 bits RC4 - WEP
typedef struct wep_key_t {
	u16	len;
	u8	key[16];	/* 40-bit and 104-bit keys */
} wep_key_t;
#endif /* WIRELESS_EXT */

static const char version[] = "airo.c 0.3 (Ben Reed & Javier Achirica)";

struct airo_info;

static int get_dec_u16( char *buffer, int *start, int limit );
static void OUT4500( struct airo_info *, u16 register, u16 value );
static unsigned short IN4500( struct airo_info *, u16 register );
static u16 setup_card(struct airo_info*, u8 *mac, ConfigRid *);
static void enable_interrupts(struct airo_info*);
static void disable_interrupts(struct airo_info*);
static u16 lock_issuecommand(struct airo_info*, Cmd *pCmd, Resp *pRsp);
static u16 issuecommand(struct airo_info*, Cmd *pCmd, Resp *pRsp);
static int bap_setup(struct airo_info*, u16 rid, u16 offset, int whichbap);
static int aux_bap_read(struct airo_info*, u16 *pu16Dst, int bytelen,
			int whichbap);
static int fast_bap_read(struct airo_info*, u16 *pu16Dst, int bytelen,
			 int whichbap);
static int bap_write(struct airo_info*, const u16 *pu16Src, int bytelen,
		     int whichbap);
static int PC4500_accessrid(struct airo_info*, u16 rid, u16 accmd);
static int PC4500_readrid(struct airo_info*, u16 rid, void *pBuf, int len);
static int PC4500_writerid(struct airo_info*, u16 rid, const void
			   *pBuf, int len);
static int do_writerid( struct airo_info*, u16 rid, const void *rid_data,
			int len );
static u16 transmit_allocate(struct airo_info*, int lenPayload);
static int transmit_802_3_packet(struct airo_info*, u16 TxFid, char
				 *pPacket, int len);

static void airo_interrupt( int irq, void* dev_id, struct pt_regs
			    *regs);
static int airo_ioctl(struct net_device *dev, struct ifreq *rq, int cmd);
#ifdef WIRELESS_EXT
struct iw_statistics *airo_get_wireless_stats (struct net_device *dev);
#endif /* WIRELESS_EXT */
#ifdef CISCO_EXT
static int readrids(struct net_device *dev, aironet_ioctl *comp);
static int writerids(struct net_device *dev, aironet_ioctl *comp);
int flashcard(struct net_device *dev, aironet_ioctl *comp);
#endif /* CISCO_EXT */

struct airo_info {
	struct net_device_stats	stats;
	int open;
	struct net_device             *dev;
	/* Note, we can have MAX_FIDS outstanding.  FIDs are 16-bits, so we
	   use the high bit to mark wether it is in use. */
#define MAX_FIDS 6
	int                           fids[MAX_FIDS];
	int registered;
	ConfigRid config;
	u16 authtype; // Used with auto_wep
	char keyindex; // Used with auto wep
	char defindex; // Used with auto wep
	struct timer_list timer;
	struct proc_dir_entry *proc_entry;
	struct airo_info *next;
        spinlock_t aux_lock;
        spinlock_t main_lock;
        int flags;
#define FLAG_PROMISC   IFF_PROMISC
#define FLAG_RADIO_OFF 0x02
	int (*bap_read)(struct airo_info*, u16 *pu16Dst, int bytelen,
			int whichbap);
	int (*header_parse)(struct sk_buff*, unsigned char *);
	unsigned short *flash;
	tdsRssiEntry *rssi;
#ifdef WIRELESS_EXT
	int			need_commit;	// Need to set config
	struct iw_statistics	wstats;		// wireless stats
#ifdef WIRELESS_SPY
	int			spy_number;
	u_char			spy_address[IW_MAX_SPY][6];
	struct iw_quality	spy_stat[IW_MAX_SPY];
#endif /* WIRELESS_SPY */
#endif /* WIRELESS_EXT */
};

static inline int bap_read(struct airo_info *ai, u16 *pu16Dst, int bytelen,
			   int whichbap) {
	return ai->bap_read(ai, pu16Dst, bytelen, whichbap);
}

static int setup_proc_entry( struct net_device *dev,
			     struct airo_info *apriv );
static int takedown_proc_entry( struct net_device *dev,
				struct airo_info *apriv );

static int readBSSListRid(struct airo_info *ai, int first,
		      BSSListRid *list) {
	int rc;
			Cmd cmd;
			Resp rsp;

	if (first == 1) {
			memset(&cmd, 0, sizeof(cmd));
			cmd.cmd=CMD_LISTBSS;
			lock_issuecommand(ai, &cmd, &rsp);
			/* Let the command take effect */
			set_current_state (TASK_INTERRUPTIBLE);
			schedule_timeout (3*HZ);
		}
	rc = PC4500_readrid(ai,
		            first ? RID_BSSLISTFIRST : RID_BSSLISTNEXT,
			    list, sizeof(*list));

	list->len = le16_to_cpu(list->len);
	list->index = le16_to_cpu(list->index);
	list->radioType = le16_to_cpu(list->radioType);
	list->cap = le16_to_cpu(list->cap);
	list->beaconInterval = le16_to_cpu(list->beaconInterval);
	list->fh.dwell = le16_to_cpu(list->fh.dwell);
	list->dsChannel = le16_to_cpu(list->dsChannel);
	list->atimWindow = le16_to_cpu(list->atimWindow);
	return rc;
}

static int readWepKeyRid(struct airo_info*ai, WepKeyRid *wkr, int temp) {
	int rc = PC4500_readrid(ai, temp ? RID_WEP_TEMP : RID_WEP_PERM,
				wkr, sizeof(*wkr));

	wkr->len = le16_to_cpu(wkr->len);
	wkr->kindex = le16_to_cpu(wkr->kindex);
	wkr->klen = le16_to_cpu(wkr->klen);
	return rc;
}
/* In the writeXXXRid routines we copy the rids so that we don't screwup
 * the originals when we endian them... */
static int writeWepKeyRid(struct airo_info*ai, WepKeyRid *pwkr, int perm) {
	int rc;
	WepKeyRid wkr = *pwkr;

	wkr.len = cpu_to_le16(wkr.len);
	wkr.kindex = cpu_to_le16(wkr.kindex);
	wkr.klen = cpu_to_le16(wkr.klen);
	rc = do_writerid(ai, RID_WEP_TEMP, &wkr, sizeof(wkr));
	if (rc!=SUCCESS) printk(KERN_ERR "airo:  WEP_TEMP set %x\n", rc);
	if (perm) {
		rc = do_writerid(ai, RID_WEP_PERM, &wkr, sizeof(wkr));
		if (rc!=SUCCESS) {
			printk(KERN_ERR "airo:  WEP_PERM set %x\n", rc);
		}
	}
	return rc;
}

static int readSsidRid(struct airo_info*ai, SsidRid *ssidr) {
	int i;
	int rc = PC4500_readrid(ai, RID_SSID, ssidr, sizeof(*ssidr));

	ssidr->len = le16_to_cpu(ssidr->len);
	for(i = 0; i < 3; i++) {
		ssidr->ssids[i].len = le16_to_cpu(ssidr->ssids[i].len);
	}
	return rc;
}
static int writeSsidRid(struct airo_info*ai, SsidRid *pssidr) {
	int rc;
	int i;
	SsidRid ssidr = *pssidr;

	ssidr.len = cpu_to_le16(ssidr.len);
	for(i = 0; i < 3; i++) {
		ssidr.ssids[i].len = cpu_to_le16(ssidr.ssids[i].len);
	}
	rc = do_writerid(ai, RID_SSID, &ssidr, sizeof(ssidr));
	return rc;
}
static int readConfigRid(struct airo_info*ai, ConfigRid *cfgr) {
	int rc = PC4500_readrid(ai, RID_ACTUALCONFIG, cfgr, sizeof(*cfgr));
	u16 *s;

	for(s = &cfgr->len; s <= &cfgr->rtsThres; s++) *s = le16_to_cpu(*s);

	for(s = &cfgr->shortRetryLimit; s <= &cfgr->radioType; s++)
		*s = le16_to_cpu(*s);

	for(s = &cfgr->txPower; s <= &cfgr->radioSpecific; s++)
		*s = le16_to_cpu(*s);

	for(s = &cfgr->arlThreshold; s <= &cfgr->autoWake; s++)
		*s = le16_to_cpu(*s);

	return rc;
}
static int writeConfigRid(struct airo_info*ai, ConfigRid *pcfgr) {
	u16 *s;
	ConfigRid cfgr = *pcfgr;

	for(s = &cfgr.len; s <= &cfgr.rtsThres; s++) *s = cpu_to_le16(*s);

	for(s = &cfgr.shortRetryLimit; s <= &cfgr.radioType; s++)
		*s = cpu_to_le16(*s);

	for(s = &cfgr.txPower; s <= &cfgr.radioSpecific; s++)
		*s = cpu_to_le16(*s);

	for(s = &cfgr.arlThreshold; s <= &cfgr.autoWake; s++)
		*s = cpu_to_le16(*s);

	return do_writerid( ai, RID_CONFIG, &cfgr, sizeof(cfgr));
}
static int readStatusRid(struct airo_info*ai, StatusRid *statr) {
	int rc = PC4500_readrid(ai, RID_STATUS, statr, sizeof(*statr));
	u16 *s;

	statr->len = le16_to_cpu(statr->len);
	for(s = &statr->mode; s <= &statr->SSIDlen; s++) *s = le16_to_cpu(*s);

	for(s = &statr->beaconPeriod; s <= &statr->_reserved[9]; s++)
		*s = le16_to_cpu(*s);

	return rc;
}
static int readAPListRid(struct airo_info*ai, APListRid *aplr) {
	int rc =  PC4500_readrid(ai, RID_APLIST, aplr, sizeof(*aplr));
	aplr->len = le16_to_cpu(aplr->len);
	return rc;
}
static int writeAPListRid(struct airo_info*ai, APListRid *aplr) {
	int rc;
	aplr->len = cpu_to_le16(aplr->len);
	rc = do_writerid(ai, RID_APLIST, aplr, sizeof(*aplr));
	return rc;
}
static int readCapabilityRid(struct airo_info*ai, CapabilityRid *capr) {
	int rc = PC4500_readrid(ai, RID_CAPABILITIES, capr, sizeof(*capr));
	u16 *s;

	capr->len = le16_to_cpu(capr->len);
	capr->prodNum = le16_to_cpu(capr->prodNum);
	capr->radioType = le16_to_cpu(capr->radioType);
	capr->country = le16_to_cpu(capr->country);
	for(s = &capr->txPowerLevels[0]; s <= &capr->requiredHard; s++)
		*s = le16_to_cpu(*s);
	return rc;
}
static int readStatsRid(struct airo_info*ai, StatsRid *sr, int rid) {
	int rc = PC4500_readrid(ai, rid, sr, sizeof(*sr));
	u32 *i;

	sr->len = le16_to_cpu(sr->len);
	for(i = &sr->vals[0]; i <= &sr->vals[99]; i++) *i = le32_to_cpu(*i);
	return rc;
}

static int airo_open(struct net_device *dev) {
	struct airo_info *info = dev->priv;

	enable_interrupts(info);

	netif_start_queue(dev);
	return 0;
}

static int airo_start_xmit(struct sk_buff *skb, struct net_device *dev) {
	s16 len;
	u16 status;
	u32 flags;
	int i,j;
	struct airo_info *priv = (struct airo_info*)dev->priv;
	u32 *fids = priv->fids;

	if ( skb == NULL ) {
		printk( KERN_ERR "airo:  skb == NULL!!!\n" );
		return 0;
	}

	/* Find a vacant FID */
	spin_lock_irqsave(&priv->main_lock, flags);
	for( j = 0, i = -1; j < MAX_FIDS; j++ ) {
		if ( !( fids[j] & 0xffff0000 ) ) {
			if ( i == -1 ) i = j;
			else break;
		}
	}
	if ( j == MAX_FIDS ) netif_stop_queue(dev);
	if ( i == -1 ) {
		priv->stats.tx_fifo_errors++;
		goto tx_done;
	}

	len = ETH_ZLEN < skb->len ? skb->len : ETH_ZLEN; /* check min length*/
	status = transmit_802_3_packet( priv, fids[i], skb->data, len );

	if ( status == SUCCESS ) {
                /* Mark fid as used & save length for later */
		fids[i] |= (len << 16);
		dev->trans_start = jiffies;
	} else {
		priv->stats.tx_window_errors++;
	}
 tx_done:
	spin_unlock_irqrestore(&priv->main_lock, flags);
	dev_kfree_skb(skb);
	return 0;
}

struct net_device_stats *airo_get_stats(struct net_device *dev)
{
	struct airo_info *local = (struct airo_info*) dev->priv;
	StatsRid stats_rid;
	u32 *vals = stats_rid.vals;

	/* Get stats out of the card */
	readStatsRid(local, &stats_rid, RID_STATS);

	local->stats.rx_packets = vals[43] + vals[44] + vals[45];
	local->stats.tx_packets = vals[39] + vals[40] + vals[41];
	local->stats.rx_bytes = vals[92];
	local->stats.tx_bytes = vals[91];
	local->stats.rx_errors = vals[0] + vals[2] + vals[3] + vals[4];
	local->stats.tx_errors = vals[42] + local->stats.tx_fifo_errors;
	local->stats.multicast = vals[43];
	local->stats.collisions = vals[89];

	/* detailed rx_errors: */
	local->stats.rx_length_errors = vals[3];
	local->stats.rx_crc_errors = vals[4];
	local->stats.rx_frame_errors = vals[2];
	local->stats.rx_fifo_errors = vals[0];

	return (&local->stats);
}

static int enable_MAC( struct airo_info *ai, Resp *rsp );
static void disable_MAC(struct airo_info *ai);

static void airo_set_multicast_list(struct net_device *dev) {
	struct airo_info *ai = (struct airo_info*)dev->priv;
	Cmd cmd;
	Resp rsp;

	/* For some reason this command takes a lot of time (~20 ms) and it's
	 * run in an interrupt handler, so we'd better be sure we needed it
	 * before executing it.
	 */
	if ((dev->flags ^ ai->flags) & IFF_PROMISC) {
		memset(&cmd, 0, sizeof(cmd));
		cmd.cmd=CMD_SETMODE;
		cmd.parm0=(dev->flags&IFF_PROMISC) ? PROMISC : NOPROMISC;
		lock_issuecommand(ai, &cmd, &rsp);
		ai->flags^=IFF_PROMISC;
	}

	if ((dev->flags&IFF_ALLMULTI)||dev->mc_count>0) {
		/* Turn on multicast.  (Should be already setup...) */
	}
}

static int airo_set_mac_address(struct net_device *dev, void *p)
{
	struct airo_info *ai = (struct airo_info*)dev->priv;
	struct sockaddr *addr = p;
	ConfigRid cfg;

	readConfigRid (ai, &cfg);
	memcpy (cfg.macAddr, addr->sa_data, dev->addr_len);
	writeConfigRid (ai, &cfg);
	memcpy (dev->dev_addr, addr->sa_data, dev->addr_len);
	return 0;
}

static int airo_change_mtu(struct net_device *dev, int new_mtu)
{
	if ((new_mtu < 68) || (new_mtu > 2400))
		return -EINVAL;
	dev->mtu = new_mtu;
	return 0;
}


static int airo_close(struct net_device *dev) {
	struct airo_info *ai = (struct airo_info*)dev->priv;

	netif_stop_queue(dev);
	disable_interrupts( ai );
	return 0;
}

static void del_airo_dev( struct net_device *dev );

void stop_airo_card( struct net_device *dev, int freeres )
{
	struct airo_info *ai = (struct airo_info*)dev->priv;
	if (ai->flash)
		kfree(ai->flash);
	if (ai->rssi)
		kfree(ai->rssi);
	takedown_proc_entry( dev, ai );
	if (ai->registered) {
		unregister_netdev( dev );
		ai->registered = 0;
	}
	disable_interrupts(ai);
	free_irq( dev->irq, dev );
	if (auto_wep) del_timer_sync(&ai->timer);
	if (freeres) {
		/* PCMCIA frees this stuff, so only for PCI and ISA */
	        release_region( dev->base_addr, 64 );
        }
	del_airo_dev( dev );
	kfree( dev );
}

static int add_airo_dev( struct net_device *dev );

struct net_device *init_airo_card( unsigned short irq, int port, int is_pcmcia )
{
	struct net_device *dev;
	struct airo_info *ai;
	int i, rc;

	/* Create the network device object. */
        dev = alloc_etherdev(sizeof(*ai));
        if (!dev) {
		printk(KERN_ERR "airo:  Couldn't alloc_etherdev\n");
		return NULL;
        }
	if (dev_alloc_name(dev, dev->name) < 0) {
		printk(KERN_ERR "airo:  Couldn't get name!\n");
		goto err_out_free;
	}

	ai = dev->priv;
	ai->registered = 0;
        ai->dev = dev;
	ai->aux_lock = SPIN_LOCK_UNLOCKED;
	ai->main_lock = SPIN_LOCK_UNLOCKED;
	ai->header_parse = dev->hard_header_parse;
	rc = add_airo_dev( dev );
	if (rc)
		goto err_out_free;

	/* The Airo-specific entries in the device structure. */
	dev->hard_start_xmit = &airo_start_xmit;
	dev->get_stats = &airo_get_stats;
	dev->set_multicast_list = &airo_set_multicast_list;
	dev->set_mac_address = &airo_set_mac_address;
	dev->do_ioctl = &airo_ioctl;
#ifdef WIRELESS_EXT
	dev->get_wireless_stats = airo_get_wireless_stats;
#endif /* WIRELESS_EXT */
	dev->change_mtu = &airo_change_mtu;
	dev->open = &airo_open;
	dev->stop = &airo_close;
	dev->irq = irq;
	dev->base_addr = port;

	rc = request_irq( dev->irq, airo_interrupt, SA_SHIRQ, dev->name, dev );
	if (rc) {
		printk(KERN_ERR "airo: register interrupt %d failed, rc %d\n", irq, rc );
		goto err_out_unlink;
	}
	if (!is_pcmcia) {
		if (!request_region( dev->base_addr, 64, dev->name )) {
			rc = -EBUSY;
			goto err_out_irq;
		}
	}

	if ( setup_card( ai, dev->dev_addr, &ai->config) != SUCCESS ) {
		printk( KERN_ERR "airo: MAC could not be enabled\n" );
		rc = -EIO;
		goto err_out_res;
	}

	rc = register_netdev(dev);
	if (rc)
		goto err_out_res;

	ai->registered = 1;
	printk( KERN_INFO "airo: MAC enabled %s %x:%x:%x:%x:%x:%x\n",
		dev->name,
		dev->dev_addr[0], dev->dev_addr[1], dev->dev_addr[2],
		dev->dev_addr[3], dev->dev_addr[4], dev->dev_addr[5] );

	/* Allocate the transmit buffers */
	for( i = 0; i < MAX_FIDS; i++ )
		ai->fids[i] = transmit_allocate( ai, 2312 );

	setup_proc_entry( dev, dev->priv ); /* XXX check for failure */
	netif_start_queue(dev);
	SET_MODULE_OWNER(dev);
	return dev;

err_out_res:
	if (!is_pcmcia)
	        release_region( dev->base_addr, 64 );
err_out_irq:
	free_irq(dev->irq, dev);
err_out_unlink:
	del_airo_dev(dev);
err_out_free:
	kfree(dev);
	return NULL;
}

int waitbusy (struct airo_info *ai) {
	int delay = 0;
	while ((IN4500 (ai, COMMAND) & COMMAND_BUSY) & (delay < 10000)) {
		udelay (10);
		if (++delay % 20)
			OUT4500(ai, EVACK, EV_CLEARCOMMANDBUSY);
	}
	return delay < 10000;
}

int reset_airo_card( struct net_device *dev ) {
	int i;
	struct airo_info *ai = (struct airo_info*)dev->priv;

	disable_MAC(ai);
	waitbusy (ai);
	OUT4500(ai,COMMAND,CMD_SOFTRESET);
	set_current_state (TASK_UNINTERRUPTIBLE);
	schedule_timeout (HZ/5);
	waitbusy (ai);
	set_current_state (TASK_UNINTERRUPTIBLE);
	schedule_timeout (HZ/5);
	if ( setup_card(ai, dev->dev_addr, &(ai)->config) != SUCCESS ) {
		printk( KERN_ERR "airo: MAC could not be enabled\n" );
		return -1;
	} else {
		printk( KERN_INFO "airo: MAC enabled %s %x:%x:%x:%x:%x:%x\n",
			dev->name,
			dev->dev_addr[0],
			dev->dev_addr[1],
			dev->dev_addr[2],
			dev->dev_addr[3],
			dev->dev_addr[4],
			dev->dev_addr[5]
			);
		/* Allocate the transmit buffers */
		for( i = 0; i < MAX_FIDS; i++ )
			ai->fids[i] = transmit_allocate( ai, 2312 );
	}
	enable_interrupts( ai );
	netif_wake_queue(dev);
	return 0;
}

int wll_header_parse(struct sk_buff *skb, unsigned char *haddr)
{
	memcpy(haddr, skb->mac.raw + 10, ETH_ALEN);
	return ETH_ALEN;
}

static void airo_interrupt ( int irq, void* dev_id, struct pt_regs *regs) {
	struct net_device *dev = (struct net_device *)dev_id;
	u16 status;
	u16 fid;
	struct airo_info *apriv = (struct airo_info *)dev->priv;
	u16 savedInterrupts = 0;

	if (!netif_device_present(dev))
		return;

	for (;;) {
		status = IN4500( apriv, EVSTAT );
		if ( !status || status == 0xffff ) break;

		if ( status & EV_AWAKE ) {
			OUT4500( apriv, EVACK, EV_AWAKE );
			OUT4500( apriv, EVACK, EV_AWAKE );
		}

		if (!savedInterrupts) {
			savedInterrupts = IN4500( apriv, EVINTEN );
			OUT4500( apriv, EVINTEN, 0 );
		}

		if ( status & EV_LINK ) {
			/* The link status has changed, if you want to put a
			   monitor hook in, do it here.  (Remember that
			   interrupts are still disabled!)
			*/
			u16 newStatus = IN4500(apriv, LINKSTAT);
			OUT4500( apriv, EVACK, EV_LINK);
			/* Here is what newStatus means: */
#define NOBEACON 0x8000 /* Loss of sync - missed beacons */
#define MAXRETRIES 0x8001 /* Loss of sync - max retries */
#define MAXARL 0x8002 /* Loss of sync - average retry level exceeded*/
#define FORCELOSS 0x8003 /* Loss of sync - host request */
#define TSFSYNC 0x8004 /* Loss of sync - TSF synchronization */
#define DEAUTH 0x8100 /* Deauthentication (low byte is reason code) */
#define DISASS 0x8200 /* Disassociation (low byte is reason code) */
#define ASSFAIL 0x8400 /* Association failure (low byte is reason
			  code) */
#define AUTHFAIL 0x0300 /* Authentication failure (low byte is reason
			   code) */
#define ASSOCIATED 0x0400 /* Assocatied */
#define RC_RESERVED 0 /* Reserved return code */
#define RC_NOREASON 1 /* Unspecified reason */
#define RC_AUTHINV 2 /* Previous authentication invalid */
#define RC_DEAUTH 3 /* Deauthenticated because sending station is
		       leaving */
#define RC_NOACT 4 /* Disassociated due to inactivity */
#define RC_MAXLOAD 5 /* Disassociated because AP is unable to handle
			all currently associated stations */
#define RC_BADCLASS2 6 /* Class 2 frame received from
			  non-Authenticated station */
#define RC_BADCLASS3 7 /* Class 3 frame received from
			  non-Associated station */
#define RC_STATLEAVE 8 /* Disassociated because sending station is
			  leaving BSS */
#define RC_NOAUTH 9 /* Station requesting (Re)Association is not
		       Authenticated with the responding station */
			if (newStatus != ASSOCIATED) {
				if (auto_wep && !timer_pending(&apriv->timer)) {
					apriv->timer.expires = RUN_AT(HZ*3);
		      			add_timer(&apriv->timer);
				}
			}
		}

		/* Check to see if there is something to receive */
		if ( status & EV_RX  ) {
			struct sk_buff *skb = NULL;
			u16 fc, len, hdrlen = 0;
			struct {
				u16 status, len;
				u8 rssi[2];
			} hdr;

			fid = IN4500( apriv, RXFID );

			/* Get the packet length */
			if (dev->type == ARPHRD_IEEE80211) {
				bap_setup (apriv, fid, 4, BAP0);
				bap_read (apriv, (u16*)&hdr, sizeof(hdr), BAP0);
				/* Bad CRC. Ignore packet */
				if (le16_to_cpu(hdr.status) & 2)
					hdr.len = 0;
			} else {
				bap_setup (apriv, fid, 6, BAP0);
				bap_read (apriv, (u16*)&hdr.len, 4, BAP0);
			}
			len = le16_to_cpu(hdr.len);

			if (len > 2312) {
				printk( KERN_ERR "airo: Bad size %d\n", len );
				len = 0;
			}
			if (len) {
				if (dev->type == ARPHRD_IEEE80211) {
					bap_setup (apriv, fid, 0x14, BAP0);
					bap_read (apriv, (u16*)&fc, sizeof(fc), BAP0);
					if ((le16_to_cpu(fc) & 0x300) == 0x300)
						hdrlen = 30;
					else
						hdrlen = 24;
				} else
					hdrlen = 12;

				skb = dev_alloc_skb( len + hdrlen + 2 );
				if ( !skb ) {
					apriv->stats.rx_dropped++;
					len = 0;
				}
			}
			if (len) {
				u16 *buffer;
				buffer = (u16*)skb_put (skb, len + hdrlen);
				if (dev->type == ARPHRD_IEEE80211) {
					u16 gap, tmpbuf[4];
					buffer[0] = fc;
					bap_read (apriv, buffer + 1, hdrlen - 2, BAP0);
					if (hdrlen == 24)
						bap_read (apriv, tmpbuf, 6, BAP0);

					bap_read (apriv, &gap, sizeof(gap), BAP0);
					gap = le16_to_cpu(gap);
					if (gap && gap <= 8)
						bap_read (apriv, tmpbuf, gap, BAP0);

					bap_read (apriv, buffer + hdrlen/2, len, BAP0);
				} else {
			                bap_setup (apriv, fid, 0x38, BAP0);
					bap_read (apriv, buffer,len + hdrlen,BAP0);
				}
				OUT4500( apriv, EVACK, EV_RX);
#ifdef WIRELESS_SPY
				if (apriv->spy_number > 0) {
					int i;
					char *sa;

					sa = (char*)buffer + ((dev->type == ARPHRD_IEEE80211) ? 10 : 6);

					for (i=0; i<apriv->spy_number; i++)
						if (!memcmp(sa,apriv->spy_address[i],6))
						{
							apriv->spy_stat[i].qual = hdr.rssi[0];
							if (apriv->rssi)
								apriv->spy_stat[i].level = 0x100 - apriv->rssi[hdr.rssi[1]].rssidBm;
							else
								apriv->spy_stat[i].level = (hdr.rssi[1] + 321) / 2;
							apriv->spy_stat[i].noise = 0;
							apriv->spy_stat[i].updated = 3;
							break;
						}
				}
#endif /* WIRELESS_SPY  */
				dev->last_rx = jiffies;
				skb->dev = dev;
				skb->ip_summed = CHECKSUM_NONE;
				if (dev->type == ARPHRD_IEEE80211) {
					skb->mac.raw = skb->data;
					skb_pull (skb, hdrlen);
					skb->pkt_type = PACKET_OTHERHOST;
					skb->protocol = htons(ETH_P_802_2);
				} else
					skb->protocol = eth_type_trans(skb,dev);

				netif_rx( skb );
			} else
				OUT4500( apriv, EVACK, EV_RX);
		}

		/* Check to see if a packet has been transmitted */
		if (  status & ( EV_TX|EV_TXEXC ) ) {
			int i;
			int len = 0;
			int index = -1;

			fid = IN4500(apriv, TXCOMPLFID);

			for( i = 0; i < MAX_FIDS; i++ ) {
				if ( ( apriv->fids[i] & 0xffff ) == fid ) {
					len = apriv->fids[i] >> 16;
					index = i;
					/* Set up to be used again */
					apriv->fids[i] &= 0xffff;
				}
			}
			if (index != -1) netif_wake_queue(dev);
			if ((status & EV_TXEXC) &&
				(bap_setup(apriv, fid, 4, BAP1) == SUCCESS)) {

				u16 status;
				bap_read(apriv, &status, 2, BAP1);
				if (le16_to_cpu(status) & 2)
					apriv->stats.tx_aborted_errors++;
				if (le16_to_cpu(status) & 4)
					apriv->stats.tx_heartbeat_errors++;
				if (le16_to_cpu(status) & 0x10)
					apriv->stats.tx_carrier_errors++;
			}
			OUT4500( apriv, EVACK, status & (EV_TX | EV_TXEXC));
			if (index==-1) {
				printk( KERN_ERR "airo: Unallocated FID was used to xmit\n" );
			}
		}
		if ( status & ~STATUS_INTS )
			OUT4500( apriv, EVACK, status & ~STATUS_INTS);

		if ( status & ~STATUS_INTS & ~IGNORE_INTS )
			printk( KERN_WARNING "airo: Got weird status %x\n",
				status & ~STATUS_INTS & ~IGNORE_INTS );
	}

	if (savedInterrupts)
		OUT4500( apriv, EVINTEN, savedInterrupts );

	/* done.. */
	return;
}

/*
 *  Routines to talk to the card
 */

/*
 *  This was originally written for the 4500, hence the name
 *  NOTE:  If use with 8bit mode and SMP bad things will happen!
 *         Why would some one do 8 bit IO in an SMP machine?!?
 */
static void OUT4500( struct airo_info *ai, u16 reg, u16 val ) {
	if ( !do8bitIO )
		outw( val, ai->dev->base_addr + reg );
	else {
		outb( val & 0xff, ai->dev->base_addr + reg );
		outb( val >> 8, ai->dev->base_addr + reg + 1 );
	}
}

static u16 IN4500( struct airo_info *ai, u16 reg ) {
	unsigned short rc;

	if ( !do8bitIO )
		rc = inw( ai->dev->base_addr + reg );
	else {
		rc = inb( ai->dev->base_addr + reg );
		rc += ((int)inb( ai->dev->base_addr + reg + 1 )) << 8;
	}
	return rc;
}

static int enable_MAC( struct airo_info *ai, Resp *rsp ) {
        Cmd cmd;

        if (ai->flags&FLAG_RADIO_OFF) return SUCCESS;
	memset(&cmd, 0, sizeof(cmd));
	cmd.cmd = MAC_ENABLE;
	return lock_issuecommand(ai, &cmd, rsp);
}

static void disable_MAC( struct airo_info *ai ) {
        Cmd cmd;
	Resp rsp;

	memset(&cmd, 0, sizeof(cmd));
	cmd.cmd = MAC_DISABLE; // disable in case already enabled
	lock_issuecommand(ai, &cmd, &rsp);
}

static void enable_interrupts( struct airo_info *ai ) {
	/* Reset the status register */
	u16 status = IN4500( ai, EVSTAT );
	OUT4500( ai, EVACK, status );
	/* Enable the interrupts */
	OUT4500( ai, EVINTEN, STATUS_INTS );
	/* Note there is a race condition between the last two lines that
	   I dont know how to get rid of right now... */
}

static void disable_interrupts( struct airo_info *ai ) {
	OUT4500( ai, EVINTEN, 0 );
}

static u16 setup_card(struct airo_info *ai, u8 *mac,
		      ConfigRid *config)
{
	Cmd cmd;
	Resp rsp;
	ConfigRid cfg;
	int status;
	int i;
	SsidRid mySsid;
	u16 lastindex;
	WepKeyRid wkr;
	int rc;

	memset( &mySsid, 0, sizeof( mySsid ) );
	if (ai->flash) {
		kfree (ai->flash);
		ai->flash = NULL;
	}

	/* The NOP is the first step in getting the card going */
	cmd.cmd = NOP;
	cmd.parm0 = cmd.parm1 = cmd.parm2 = 0;
	if ( lock_issuecommand( ai, &cmd, &rsp ) != SUCCESS ) {
		return ERROR;
	}
	memset(&cmd, 0, sizeof(cmd));
	cmd.cmd = MAC_DISABLE; // disable in case already enabled
	if ( lock_issuecommand( ai, &cmd, &rsp ) != SUCCESS ) {
		return ERROR;
	}

	// Let's figure out if we need to use the AUX port
	cmd.cmd = CMD_ENABLEAUX;
	if (lock_issuecommand(ai, &cmd, &rsp) != SUCCESS) {
		printk(KERN_ERR "airo: Error checking for AUX port\n");
		return ERROR;
	}
	if (!aux_bap || rsp.status & 0xff00) {
		ai->bap_read = fast_bap_read;
		printk(KERN_DEBUG "airo: Doing fast bap_reads\n");
	} else {
		ai->bap_read = aux_bap_read;
		printk(KERN_DEBUG "airo: Doing AUX bap_reads\n");
	}
	if ( config->len ) {
		cfg = *config;
	} else {
		tdsRssiRid rssi_rid;

		// general configuration (read/modify/write)
		status = readConfigRid(ai, &cfg);
		if ( status != SUCCESS ) return ERROR;

		status = PC4500_readrid(ai,RID_RSSI,&rssi_rid,sizeof(rssi_rid));
		if ( status == SUCCESS ) {
			if (ai->rssi || (ai->rssi = kmalloc(512, GFP_KERNEL)) != NULL)
				memcpy(ai->rssi, (u8*)&rssi_rid + 2, 512);
		}
		else {
			CapabilityRid cap_rid;
			if (ai->rssi) {
				kfree(ai->rssi);
				ai->rssi = NULL;
			}
			status = readCapabilityRid(ai, &cap_rid);
			if ((status == SUCCESS) && (cap_rid.softCap & 8))
				cfg.rmode |= RXMODE_NORMALIZED_RSSI;
			else
				printk(KERN_WARNING "airo: unknown received signal level scale\n");
		}
		cfg.opmode = adhoc ? MODE_STA_IBSS : MODE_STA_ESS;

		/* Save off the MAC */
		for( i = 0; i < 6; i++ ) {
			mac[i] = cfg.macAddr[i];
		}

		/* Check to see if there are any insmod configured
		   rates to add */
		if ( rates ) {
			int i = 0;
			if ( rates[0] ) memset(cfg.rates,0,sizeof(cfg.rates));
			for( i = 0; i < 8 && rates[i]; i++ ) {
				cfg.rates[i] = rates[i];
			}
		}
		if ( basic_rate > 0 ) {
			int i;
			for( i = 0; i < 8; i++ ) {
				if ( cfg.rates[i] == basic_rate ||
				     !cfg.rates ) {
					cfg.rates[i] = basic_rate | 0x80;
					break;
				}
			}
		}
		cfg.authType = ai->authtype;
		*config = cfg;
	}

	/* Setup the SSIDs if present */
	if ( ssids[0] ) {
		int i = 0;
		for( i = 0; i < 3 && ssids[i]; i++ ) {
			mySsid.ssids[i].len = strlen(ssids[i]);
			if ( mySsid.ssids[i].len > 32 )
				mySsid.ssids[i].len = 32;
			memcpy(mySsid.ssids[i].ssid, ssids[i],
			       mySsid.ssids[i].len);
			mySsid.ssids[i].len = mySsid.ssids[i].len;
		}
	}

	status = writeConfigRid(ai, &cfg);
	if ( status != SUCCESS ) return ERROR;

	/* Set up the SSID list */
	status = writeSsidRid(ai, &mySsid);
	if ( status != SUCCESS ) return ERROR;

	/* Grab the initial wep key, we gotta save it for auto_wep */
	rc = readWepKeyRid(ai, &wkr, 1);
	if (rc == SUCCESS) do {
		lastindex = wkr.kindex;
		if (wkr.kindex == 0xffff) {
			ai->defindex = wkr.mac[0];
		}
		rc = readWepKeyRid(ai, &wkr, 0);
	} while(lastindex != wkr.kindex);

	if (auto_wep && !timer_pending(&ai->timer)) {
		ai->timer.expires = RUN_AT(HZ*3);
		add_timer(&ai->timer);
	}
	return SUCCESS;
}

static u16 lock_issuecommand(struct airo_info *ai, Cmd *pCmd, Resp *pRsp) {
	int rc;
	long flags;

	spin_lock_irqsave(&ai->main_lock, flags);
	rc = issuecommand(ai, pCmd, pRsp);
	spin_unlock_irqrestore(&ai->main_lock, flags);
	return rc;
}

static u16 issuecommand(struct airo_info *ai, Cmd *pCmd, Resp *pRsp) {
        // Im really paranoid about letting it run forever!
	int max_tries = 600000;

	OUT4500(ai, PARAM0, pCmd->parm0);
	OUT4500(ai, PARAM1, pCmd->parm1);
	OUT4500(ai, PARAM2, pCmd->parm2);
	OUT4500(ai, COMMAND, pCmd->cmd);
	while ( max_tries-- &&
		(IN4500(ai, EVSTAT) & EV_CMD) == 0) {
		if ( IN4500(ai, COMMAND) == pCmd->cmd) {
			// PC4500 didn't notice command, try again
			OUT4500(ai, COMMAND, pCmd->cmd);
		}
	}
	if ( max_tries == -1 ) {
		printk( KERN_ERR
			"airo: Max tries exceeded when issueing command\n" );
                return ERROR;
	}
	// command completed
	pRsp->status = IN4500(ai, STATUS);
	pRsp->rsp0 = IN4500(ai, RESP0);
	pRsp->rsp1 = IN4500(ai, RESP1);
	pRsp->rsp2 = IN4500(ai, RESP2);

	// clear stuck command busy if necessary
	if (IN4500(ai, COMMAND) & COMMAND_BUSY) {
		OUT4500(ai, EVACK, EV_CLEARCOMMANDBUSY);
	}
	// acknowledge processing the status/response
	OUT4500(ai, EVACK, EV_CMD);
	return SUCCESS;
}

/* Sets up the bap to start exchange data.  whichbap should
 * be one of the BAP0 or BAP1 defines.  Locks should be held before
 * calling! */
static int bap_setup(struct airo_info *ai, u16 rid, u16 offset, int whichbap )
{
	int timeout = 50;
	int max_tries = 3;

	OUT4500(ai, SELECT0+whichbap, rid);
	OUT4500(ai, OFFSET0+whichbap, offset);
	while (1) {
		int status = IN4500(ai, OFFSET0+whichbap);
		if (status & BAP_BUSY) {
                        /* This isn't really a timeout, but its kinda
			   close */
			if (timeout--) {
				continue;
			}
		} else if ( status & BAP_ERR ) {
			/* invalid rid or offset */
			printk( KERN_ERR "airo: BAP error %x %d\n",
				status, whichbap );
			return ERROR;
		} else if (status & BAP_DONE) { // success
			return SUCCESS;
		}
		if ( !(max_tries--) ) {
			printk( KERN_ERR
				"airo: BAP setup error too many retries\n" );
			return ERROR;
		}
		// -- PC4500 missed it, try again
		OUT4500(ai, SELECT0+whichbap, rid);
		OUT4500(ai, OFFSET0+whichbap, offset);
		timeout = 50;
	}
}

/* should only be called by aux_bap_read.  This aux function and the
   following use concepts not documented in the developers guide.  I
   got them from a patch given to my by Aironet */
static u16 aux_setup(struct airo_info *ai, u16 page,
		     u16 offset, u16 *len)
{
	u16 next;

	OUT4500(ai, AUXPAGE, page);
	OUT4500(ai, AUXOFF, 0);
	next = IN4500(ai, AUXDATA);
	*len = IN4500(ai, AUXDATA)&0xff;
	if (offset != 4) OUT4500(ai, AUXOFF, offset);
	return next;
}

/* requires call to bap_setup() first */
static int aux_bap_read(struct airo_info *ai, u16 *pu16Dst,
			int bytelen, int whichbap)
{
	u16 len;
	u16 page;
	u16 offset;
	u16 next;
	int words;
	int i;
	long flags;

	spin_lock_irqsave(&ai->aux_lock, flags);
	page = IN4500(ai, SWS0+whichbap);
	offset = IN4500(ai, SWS2+whichbap);
	next = aux_setup(ai, page, offset, &len);
	words = (bytelen+1)>>1;

	for (i=0; i<words;) {
		int count;
		count = (len>>1) < (words-i) ? (len>>1) : (words-i);
		if ( !do8bitIO )
			insw( ai->dev->base_addr+DATA0+whichbap,
			      pu16Dst+i,count );
		else
			insb( ai->dev->base_addr+DATA0+whichbap,
			      pu16Dst+i, count << 1 );
		i += count;
		if (i<words) {
			next = aux_setup(ai, next, 4, &len);
		}
	}
	spin_unlock_irqrestore(&ai->aux_lock, flags);
	return SUCCESS;
}


/* requires call to bap_setup() first */
static int fast_bap_read(struct airo_info *ai, u16 *pu16Dst,
			 int bytelen, int whichbap)
{
	bytelen = (bytelen + 1) & (~1); // round up to even value
	if ( !do8bitIO )
		insw( ai->dev->base_addr+DATA0+whichbap, pu16Dst, bytelen>>1 );
	else
		insb( ai->dev->base_addr+DATA0+whichbap, pu16Dst, bytelen );
	return SUCCESS;
}

/* requires call to bap_setup() first */
static int bap_write(struct airo_info *ai, const u16 *pu16Src,
		     int bytelen, int whichbap)
{
	bytelen = (bytelen + 1) & (~1); // round up to even value
	if ( !do8bitIO )
		outsw( ai->dev->base_addr+DATA0+whichbap,
		       pu16Src, bytelen>>1 );
	else
		outsb( ai->dev->base_addr+DATA0+whichbap, pu16Src, bytelen );
	return SUCCESS;
}

static int PC4500_accessrid(struct airo_info *ai, u16 rid, u16 accmd)
{
	Cmd cmd; /* for issuing commands */
	Resp rsp; /* response from commands */
	u16 status;

	memset(&cmd, 0, sizeof(cmd));
	cmd.cmd = accmd;
	cmd.parm0 = rid;
	status = issuecommand(ai, &cmd, &rsp);
	if (status != 0) return status;
	if ( (rsp.status & 0x7F00) != 0) {
		return (accmd << 8) + (rsp.rsp0 & 0xFF);
	}
	return 0;
}

/*  Note, that we are using BAP1 which is also used by transmit, so
 *  we must get a lock. */
static int PC4500_readrid(struct airo_info *ai, u16 rid, void *pBuf, int len)
{
	u16 status;
        long flags;
        int rc = SUCCESS;

	spin_lock_irqsave(&ai->main_lock, flags);
	if ( (status = PC4500_accessrid(ai, rid, CMD_ACCESS)) != SUCCESS) {
                rc = status;
                goto done;
        }
	if (bap_setup(ai, rid, 0, BAP1) != SUCCESS) {
		rc = ERROR;
                goto done;
        }
	// read the rid length field
	bap_read(ai, pBuf, 2, BAP1);
	// length for remaining part of rid
	len = min(len, (int)le16_to_cpu(*(u16*)pBuf)) - 2;

	if ( len <= 2 ) {
		printk( KERN_ERR
			"airo: Rid %x has a length of %d which is too short\n",
			(int)rid,
			(int)len );
		rc = ERROR;
                goto done;
	}
	// read remainder of the rid
	rc = bap_read(ai, ((u16*)pBuf)+1, len, BAP1);
 done:
	spin_unlock_irqrestore(&ai->main_lock, flags);
	return rc;
}

/*  Note, that we are using BAP1 which is also used by transmit, so
 *  make sure this isnt called when a transmit is happening */
static int PC4500_writerid(struct airo_info *ai, u16 rid,
			   const void *pBuf, int len)
{
	u16 status;
        long flags;
	int rc = SUCCESS;

	spin_lock_irqsave(&ai->main_lock, flags);
	// --- first access so that we can write the rid data
	if ( (status = PC4500_accessrid(ai, rid, CMD_ACCESS)) != 0) {
                rc = status;
                goto done;
        }
	// --- now write the rid data
	if (bap_setup(ai, rid, 0, BAP1) != SUCCESS) {
                rc = ERROR;
                goto done;
        }
	bap_write(ai, pBuf, len, BAP1);
	// ---now commit the rid data
	rc = PC4500_accessrid(ai, rid, 0x100|CMD_ACCESS);
 done:
	spin_unlock_irqrestore(&ai->main_lock, flags);
        return rc;
}

/* Allocates a FID to be used for transmitting packets.  We only use
   one for now. */
static u16 transmit_allocate(struct airo_info *ai, int lenPayload)
{
	Cmd cmd;
	Resp rsp;
	u16 txFid;
	u16 txControl;
        long flags;

	cmd.cmd = CMD_ALLOCATETX;
	cmd.parm0 = lenPayload;
	if (lock_issuecommand(ai, &cmd, &rsp) != SUCCESS) return 0;
	if ( (rsp.status & 0xFF00) != 0) return 0;
	/* wait for the allocate event/indication
	 * It makes me kind of nervous that this can just sit here and spin,
	 * but in practice it only loops like four times. */
	while ( (IN4500(ai, EVSTAT) & EV_ALLOC) == 0) ;
	// get the allocated fid and acknowledge
	txFid = IN4500(ai, TXALLOCFID);
	OUT4500(ai, EVACK, EV_ALLOC);

	/*  The CARD is pretty cool since it converts the ethernet packet
	 *  into 802.11.  Also note that we don't release the FID since we
	 *  will be using the same one over and over again. */
	/*  We only have to setup the control once since we are not
	 *  releasing the fid. */
	txControl = cpu_to_le16(TXCTL_TXOK | TXCTL_TXEX | TXCTL_802_3
		| TXCTL_ETHERNET | TXCTL_NORELEASE);
	spin_lock_irqsave(&ai->main_lock, flags);
	if (bap_setup(ai, txFid, 0x0008, BAP1) != SUCCESS) {
		spin_unlock_irqrestore(&ai->main_lock, flags);
		return ERROR;
	}
	bap_write(ai, &txControl, sizeof(txControl), BAP1);
	spin_unlock_irqrestore(&ai->main_lock, flags);

	return txFid;
}

/* In general BAP1 is dedicated to transmiting packets.  However,
   since we need a BAP when accessing RIDs, we also use BAP1 for that.
   Make sure the BAP1 spinlock is held when this is called. */
static int transmit_802_3_packet(struct airo_info *ai, u16 txFid,
				 char *pPacket, int len)
{
	u16 payloadLen;
	Cmd cmd;
	Resp rsp;

	if (len < 12) {
		printk( KERN_WARNING "Short packet %d\n", len );
		return ERROR;
	}

	// packet is destination[6], source[6], payload[len-12]
	// write the payload length and dst/src/payload
	if (bap_setup(ai, txFid, 0x0036, BAP1) != SUCCESS) return ERROR;
	/* The hardware addresses aren't counted as part of the payload, so
	 * we have to subtract the 12 bytes for the addresses off */
	payloadLen = cpu_to_le16(len-12);
	bap_write(ai, &payloadLen, sizeof(payloadLen),BAP1);
	bap_write(ai, (const u16*)pPacket, len, BAP1);
	// issue the transmit command
	memset( &cmd, 0, sizeof( cmd ) );
	cmd.cmd = CMD_TRANSMIT;
	cmd.parm0 = txFid;
	if (issuecommand(ai, &cmd, &rsp) != SUCCESS) return ERROR;
	if ( (rsp.status & 0xFF00) != 0) return ERROR;
	return SUCCESS;
}

/*
 *  This is the proc_fs routines.  It is a bit messier than I would
 *  like!  Feel free to clean it up!
 */

static ssize_t proc_read( struct file *file,
			  char *buffer,
			  size_t len,
			  loff_t *offset);

static ssize_t proc_write( struct file *file,
			   const char *buffer,
			   size_t len,
			   loff_t *offset );
static int proc_close( struct inode *inode, struct file *file );

static int proc_stats_open( struct inode *inode, struct file *file );
static int proc_statsdelta_open( struct inode *inode, struct file *file );
static int proc_status_open( struct inode *inode, struct file *file );
static int proc_SSID_open( struct inode *inode, struct file *file );
static int proc_APList_open( struct inode *inode, struct file *file );
static int proc_BSSList_open( struct inode *inode, struct file *file );
static int proc_config_open( struct inode *inode, struct file *file );
static int proc_wepkey_open( struct inode *inode, struct file *file );

static struct file_operations proc_statsdelta_ops = {
	read:           proc_read,
	open:           proc_statsdelta_open,
	release:        proc_close
};

static struct file_operations proc_stats_ops = {
	read:           proc_read,
	open:           proc_stats_open,
	release:        proc_close
};

static struct file_operations proc_status_ops = {
	read:            proc_read,
	open:            proc_status_open,
	release:         proc_close
};

static struct file_operations proc_SSID_ops = {
	read:          proc_read,
	write:         proc_write,
	open:          proc_SSID_open,
	release:       proc_close
};

static struct file_operations proc_BSSList_ops = {
	read:          proc_read,
	write:         proc_write,
	open:          proc_BSSList_open,
	release:       proc_close
};

static struct file_operations proc_APList_ops = {
	read:          proc_read,
	write:         proc_write,
	open:          proc_APList_open,
	release:       proc_close
};

static struct file_operations proc_config_ops = {
	read:          proc_read,
	write:         proc_write,
	open:          proc_config_open,
	release:       proc_close
};

static struct file_operations proc_wepkey_ops = {
	read:          proc_read,
	write:         proc_write,
	open:          proc_wepkey_open,
	release:       proc_close
};

static struct proc_dir_entry *airo_entry = 0;

struct proc_data {
	int release_buffer;
	int readlen;
	char *rbuffer;
	int writelen;
	int maxwritelen;
	char *wbuffer;
	void (*on_close) (struct inode *, struct file *);
};

#ifndef SETPROC_OPS
#define SETPROC_OPS(entry, ops) (entry)->proc_fops = &(ops)
#endif

static int setup_proc_entry( struct net_device *dev,
			     struct airo_info *apriv ) {
	struct proc_dir_entry *entry;
	/* First setup the device directory */
	apriv->proc_entry = create_proc_entry(dev->name,
					      S_IFDIR|airo_perm,
					      airo_entry);
        apriv->proc_entry->uid = proc_uid;
        apriv->proc_entry->gid = proc_gid;

	/* Setup the StatsDelta */
	entry = create_proc_entry("StatsDelta",
				  S_IFREG | (S_IRUGO&proc_perm),
				  apriv->proc_entry);
        entry->uid = proc_uid;
        entry->gid = proc_gid;
	entry->data = dev;
	SETPROC_OPS(entry, proc_statsdelta_ops);

	/* Setup the Stats */
	entry = create_proc_entry("Stats",
				  S_IFREG | (S_IRUGO&proc_perm),
				  apriv->proc_entry);
        entry->uid = proc_uid;
        entry->gid = proc_gid;
	entry->data = dev;
	SETPROC_OPS(entry, proc_stats_ops);

	/* Setup the Status */
	entry = create_proc_entry("Status",
				  S_IFREG | (S_IRUGO&proc_perm),
				  apriv->proc_entry);
        entry->uid = proc_uid;
        entry->gid = proc_gid;
	entry->data = dev;
	SETPROC_OPS(entry, proc_status_ops);

	/* Setup the Config */
	entry = create_proc_entry("Config",
				  S_IFREG | proc_perm,
				  apriv->proc_entry);
        entry->uid = proc_uid;
        entry->gid = proc_gid;
	entry->data = dev;
	SETPROC_OPS(entry, proc_config_ops);

	/* Setup the SSID */
	entry = create_proc_entry("SSID",
				  S_IFREG | proc_perm,
				  apriv->proc_entry);
        entry->uid = proc_uid;
        entry->gid = proc_gid;
	entry->data = dev;
	SETPROC_OPS(entry, proc_SSID_ops);

	/* Setup the APList */
	entry = create_proc_entry("APList",
				  S_IFREG | proc_perm,
				  apriv->proc_entry);
        entry->uid = proc_uid;
        entry->gid = proc_gid;
	entry->data = dev;
	SETPROC_OPS(entry, proc_APList_ops);

	/* Setup the BSSList */
	entry = create_proc_entry("BSSList",
				  S_IFREG | proc_perm,
				  apriv->proc_entry);
	entry->uid = proc_uid;
	entry->gid = proc_gid;
	entry->data = dev;
	SETPROC_OPS(entry, proc_BSSList_ops);

	/* Setup the WepKey */
	entry = create_proc_entry("WepKey",
				  S_IFREG | proc_perm,
				  apriv->proc_entry);
        entry->uid = proc_uid;
        entry->gid = proc_gid;
	entry->data = dev;
	SETPROC_OPS(entry, proc_wepkey_ops);

	return 0;
}

static int takedown_proc_entry( struct net_device *dev,
				struct airo_info *apriv ) {
	if ( !apriv->proc_entry->namelen ) return 0;
	remove_proc_entry("Stats",apriv->proc_entry);
	remove_proc_entry("StatsDelta",apriv->proc_entry);
	remove_proc_entry("Status",apriv->proc_entry);
	remove_proc_entry("Config",apriv->proc_entry);
	remove_proc_entry("SSID",apriv->proc_entry);
	remove_proc_entry("APList",apriv->proc_entry);
	remove_proc_entry("BSSList",apriv->proc_entry);
	remove_proc_entry("WepKey",apriv->proc_entry);
	remove_proc_entry(dev->name,airo_entry);
	return 0;
}

/*
 *  What we want from the proc_fs is to be able to efficiently read
 *  and write the configuration.  To do this, we want to read the
 *  configuration when the file is opened and write it when the file is
 *  closed.  So basically we allocate a read buffer at open and fill it
 *  with data, and allocate a write buffer and read it at close.
 */

/*
 *  The read routine is generic, it relies on the preallocated rbuffer
 *  to supply the data.
 */
static ssize_t proc_read( struct file *file,
			  char *buffer,
			  size_t len,
			  loff_t *offset )
{
	int i;
	int pos;
	struct proc_data *priv = (struct proc_data*)file->private_data;

	if( !priv->rbuffer ) return -EINVAL;

	pos = *offset;
	for( i = 0; i+pos < priv->readlen && i < len; i++ ) {
		if (put_user( priv->rbuffer[i+pos], buffer+i ))
			return -EFAULT;
	}
	*offset += i;
	return i;
}

/*
 *  The write routine is generic, it fills in a preallocated rbuffer
 *  to supply the data.
 */
static ssize_t proc_write( struct file *file,
			   const char *buffer,
			   size_t len,
			   loff_t *offset )
{
	int i;
	int pos;
	struct proc_data *priv = (struct proc_data*)file->private_data;

	if ( !priv->wbuffer ) {
		return -EINVAL;
	}

	pos = *offset;

	for( i = 0; i + pos <  priv->maxwritelen &&
		     i < len; i++ ) {
		if (get_user( priv->wbuffer[i+pos], buffer + i ))
			return -EFAULT;
	}
	if ( i+pos > priv->writelen ) priv->writelen = i+file->f_pos;
	*offset += i;
	return i;
}

static int proc_status_open( struct inode *inode, struct file *file ) {
	struct proc_data *data;
	struct proc_dir_entry *dp = inode->u.generic_ip;
	struct net_device *dev = dp->data;
	struct airo_info *apriv = (struct airo_info *)dev->priv;
	CapabilityRid cap_rid;
	StatusRid status_rid;
	int i;

	MOD_INC_USE_COUNT;

	dp = inode->u.generic_ip;

	if ((file->private_data = kmalloc(sizeof(struct proc_data ), GFP_KERNEL)) == NULL)
		return -ENOMEM;
	memset(file->private_data, 0, sizeof(struct proc_data));
	data = (struct proc_data *)file->private_data;
	if ((data->rbuffer = kmalloc( 2048, GFP_KERNEL )) == NULL) {
		kfree (file->private_data);
		return -ENOMEM;
	}

	readStatusRid(apriv, &status_rid);
	readCapabilityRid(apriv, &cap_rid);

        i = sprintf(data->rbuffer, "Status: %s%s%s%s%s%s%s%s%s\n",
                    status_rid.mode & 1 ? "CFG ": "",
                    status_rid.mode & 2 ? "ACT ": "",
                    status_rid.mode & 0x10 ? "SYN ": "",
                    status_rid.mode & 0x20 ? "LNK ": "",
                    status_rid.mode & 0x40 ? "LEAP ": "",
                    status_rid.mode & 0x80 ? "PRIV ": "",
                    status_rid.mode & 0x100 ? "KEY ": "",
                    status_rid.mode & 0x200 ? "WEP ": "",
                    status_rid.mode & 0x8000 ? "ERR ": "");
	sprintf( data->rbuffer+i, "Mode: %x\n"
		 "Signal Strength: %d\n"
		 "Signal Quality: %d\n"
		 "SSID: %-.*s\n"
		 "AP: %-.16s\n"
		 "Freq: %d\n"
		 "BitRate: %dmbs\n"
		 "Driver Version: %s\n"
		 "Device: %s\nManufacturer: %s\nFirmware Version: %s\n"
		 "Radio type: %x\nCountry: %x\nHardware Version: %x\n"
		 "Software Version: %x\nSoftware Subversion: %x\n"
		 "Boot block version: %x\n",
		 (int)status_rid.mode,
		 (int)status_rid.normalizedSignalStrength,
		 (int)status_rid.signalQuality,
		 (int)status_rid.SSIDlen,
		 status_rid.SSID,
		 status_rid.apName,
		 (int)status_rid.channel,
		 (int)status_rid.currentXmitRate/2,
		 version,
		 cap_rid.prodName,
		 cap_rid.manName,
		 cap_rid.prodVer,
		 cap_rid.radioType,
		 cap_rid.country,
		 cap_rid.hardVer,
		 (int)cap_rid.softVer,
		 (int)cap_rid.softSubVer,
		 (int)cap_rid.bootBlockVer );
	data->readlen = strlen( data->rbuffer );
	return 0;
}

static int proc_stats_rid_open(struct inode*, struct file*, u16);
static int proc_statsdelta_open( struct inode *inode,
				 struct file *file ) {
	if (file->f_mode&FMODE_WRITE) {
		return proc_stats_rid_open(inode, file, RID_STATSDELTACLEAR);
	}
	return proc_stats_rid_open(inode, file, RID_STATSDELTA);
}

static int proc_stats_open( struct inode *inode, struct file *file ) {
	return proc_stats_rid_open(inode, file, RID_STATS);
}

static int proc_stats_rid_open( struct inode *inode,
				struct file *file,
				u16 rid ) {
	struct proc_data *data;
	struct proc_dir_entry *dp = inode->u.generic_ip;
	struct net_device *dev = dp->data;
	struct airo_info *apriv = (struct airo_info *)dev->priv;
	StatsRid stats;
	int i, j;
	int *vals = stats.vals;
	MOD_INC_USE_COUNT;


	dp = inode->u.generic_ip;

	if ((file->private_data = kmalloc(sizeof(struct proc_data ), GFP_KERNEL)) == NULL)
		return -ENOMEM;
	memset(file->private_data, 0, sizeof(struct proc_data));
	data = (struct proc_data *)file->private_data;
	if ((data->rbuffer = kmalloc( 4096, GFP_KERNEL )) == NULL) {
		kfree (file->private_data);
		return -ENOMEM;
	}

	readStatsRid(apriv, &stats, rid);

        j = 0;
	for(i=0; (int)statsLabels[i]!=-1 &&
		    i*4<stats.len; i++){
		if (!statsLabels[i]) continue;
		if (j+strlen(statsLabels[i])+16>4096) {
			printk(KERN_WARNING
			       "airo: Potentially disasterous buffer overflow averted!\n");
			break;
		}
		j+=sprintf(data->rbuffer+j, "%s: %d\n", statsLabels[i], vals[i]);
	}
	if (i*4>=stats.len){
		printk(KERN_WARNING
		       "airo: Got a short rid\n");
	}
	data->readlen = j;
	return 0;
}

static int get_dec_u16( char *buffer, int *start, int limit ) {
	u16 value;
	int valid = 0;
	for( value = 0; buffer[*start] >= '0' &&
		     buffer[*start] <= '9' &&
		     *start < limit; (*start)++ ) {
		valid = 1;
		value *= 10;
		value += buffer[*start] - '0';
	}
	if ( !valid ) return -1;
	return value;
}

static void checkThrottle(ConfigRid *config) {
	int i;
/* Old hardware had a limit on encryption speed */
	if (config->authType != AUTH_OPEN && maxencrypt) {
		for(i=0; i<8; i++) {
			if (config->rates[i] > maxencrypt) {
				config->rates[i] = 0;
			}
		}
	}
}

static void proc_config_on_close( struct inode *inode, struct file *file ) {
	struct proc_data *data = file->private_data;
	struct proc_dir_entry *dp = inode->u.generic_ip;
	struct net_device *dev = dp->data;
	struct airo_info *ai = (struct airo_info*)dev->priv;
	ConfigRid config;
	Resp rsp;
	char *line;
	int need_reset = 0;

	if ( !data->writelen ) return;
	dp = (struct proc_dir_entry *) inode->u.generic_ip;

	disable_MAC(ai);
	readConfigRid(ai, &config);

	line = data->wbuffer;
	while( line[0] ) {
/*** Mode processing */
		if ( !strncmp( line, "Mode: ", 6 ) ) {
			line += 6;
			config.rmode &= 0xfe00;
			if ( line[0] == 'a' ) {
				config.opmode = 0;
			} else {
				config.opmode = 1;
				if ( line[0] == 'r' )
					config.rmode |= RXMODE_RFMON | RXMODE_DISABLE_802_3_HEADER;
				else if ( line[0] == 'y' )
					config.rmode |= RXMODE_RFMON_ANYBSS | RXMODE_DISABLE_802_3_HEADER;
			}
			if (config.rmode & RXMODE_DISABLE_802_3_HEADER) {
				dev->type = ARPHRD_IEEE80211;
				dev->hard_header_parse = wll_header_parse;
			} else if (dev->type == ARPHRD_IEEE80211) {
				dev->type = ARPHRD_ETHER;
				dev->hard_header_parse = ai->header_parse;
				need_reset = 1;
			}
		}

/*** Radio status */
		else if (!strncmp(line,"Radio: ", 7)) {
			line += 7;
			if (!strncmp(line,"off",3)) {
				ai->flags |= FLAG_RADIO_OFF;
			} else {
				ai->flags &= ~FLAG_RADIO_OFF;
			}
		}
/*** NodeName processing */
		else if ( !strncmp( line, "NodeName: ", 10 ) ) {
			int j;

			line += 10;
			memset( config.nodeName, 0, 16 );
/* Do the name, assume a space between the mode and node name */
			for( j = 0; j < 16 && line[j] != '\n'; j++ ) {
				config.nodeName[j] = line[j];
			}
		}

/*** PowerMode processing */
		else if ( !strncmp( line, "PowerMode: ", 11 ) ) {
			line += 11;
			if ( !strncmp( line, "PSPCAM", 6 ) ) {
				config.powerSaveMode = POWERSAVE_PSPCAM;
			} else if ( !strncmp( line, "PSP", 3 ) ) {
				config.powerSaveMode = POWERSAVE_PSP;
			} else {
				config.powerSaveMode = POWERSAVE_CAM;
			}
		} else if ( !strncmp( line, "DataRates: ", 11 ) ) {
			int v, i = 0, k = 0; /* i is index into line,
						k is index to rates */

			line += 11;
			while((v = get_dec_u16(line, &i, 3))!=-1) {
				config.rates[k++] = (u8)v;
				line += i + 1;
				i = 0;
			}
		} else if ( !strncmp( line, "Channel: ", 9 ) ) {
			int v, i = 0;
			line += 9;
			v = get_dec_u16(line, &i, i+3);
			if ( v != -1 )
				config.channelSet = (u16)v;
		} else if ( !strncmp( line, "XmitPower: ", 11 ) ) {
			int v, i = 0;
			line += 11;
			v = get_dec_u16(line, &i, i+3);
			if ( v != -1 ) config.txPower = (u16)v;
		} else if ( !strncmp( line, "WEP: ", 5 ) ) {
			line += 5;
			switch( line[0] ) {
			case 's':
				config.authType = (u16)AUTH_SHAREDKEY;
				break;
			case 'e':
				config.authType = (u16)AUTH_ENCRYPT;
				break;
			default:
				config.authType = (u16)AUTH_OPEN;
				break;
			}
		} else if ( !strncmp( line, "LongRetryLimit: ", 16 ) ) {
			int v, i = 0;

			line += 16;
			v = get_dec_u16(line, &i, 3);
			v = (v<0) ? 0 : ((v>255) ? 255 : v);
			config.longRetryLimit = (u16)v;
		} else if ( !strncmp( line, "ShortRetryLimit: ", 17 ) ) {
			int v, i = 0;

			line += 17;
			v = get_dec_u16(line, &i, 3);
			v = (v<0) ? 0 : ((v>255) ? 255 : v);
			config.shortRetryLimit = (u16)v;
		} else if ( !strncmp( line, "RTSThreshold: ", 14 ) ) {
			int v, i = 0;

			line += 14;
			v = get_dec_u16(line, &i, 4);
			v = (v<0) ? 0 : ((v>2312) ? 2312 : v);
			config.rtsThres = (u16)v;
		} else if ( !strncmp( line, "TXMSDULifetime: ", 16 ) ) {
			int v, i = 0;

			line += 16;
			v = get_dec_u16(line, &i, 5);
			v = (v<0) ? 0 : v;
			config.txLifetime = (u16)v;
		} else if ( !strncmp( line, "RXMSDULifetime: ", 16 ) ) {
			int v, i = 0;

			line += 16;
			v = get_dec_u16(line, &i, 5);
			v = (v<0) ? 0 : v;
			config.rxLifetime = (u16)v;
		} else if ( !strncmp( line, "TXDiversity: ", 13 ) ) {
			config.txDiversity =
				(line[13]=='l') ? 1 :
				((line[13]=='r')? 2: 3);
		} else if ( !strncmp( line, "RXDiversity: ", 13 ) ) {
			config.rxDiversity =
				(line[13]=='l') ? 1 :
				((line[13]=='r')? 2: 3);
		} else if ( !strncmp( line, "FragThreshold: ", 15 ) ) {
			int v, i = 0;

			line += 15;
			v = get_dec_u16(line, &i, 4);
			v = (v<256) ? 256 : ((v>2312) ? 2312 : v);
			v = v & 0xfffe; /* Make sure its even */
			config.fragThresh = (u16)v;
		} else if (!strncmp(line, "Modulation: ", 12)) {
			line += 12;
			switch(*line) {
			case 'd':  config.modulation=MOD_DEFAULT; break;
			case 'c':  config.modulation=MOD_CCK; break;
			case 'm':  config.modulation=MOD_MOK; break;
			default:
				printk( KERN_WARNING "airo: Unknown modulation\n" );
			}
		} else if (!strncmp(line, "Preamble: ", 10)) {
			line += 10;
			switch(*line) {
			case 'a': config.preamble=PREAMBLE_AUTO; break;
			case 'l': config.preamble=PREAMBLE_LONG; break;
			case 's': config.preamble=PREAMBLE_SHORT; break;
		        default: printk(KERN_WARNING "airo: Unknown preamble\n");
			}
		} else {
			printk( KERN_WARNING "Couldn't figure out %s\n", line );
		}
		while( line[0] && line[0] != '\n' ) line++;
		if ( line[0] ) line++;
	}
	checkThrottle(&config);
	ai->config = config;
	if (need_reset) {
		APListRid APList_rid;
		SsidRid SSID_rid;

		readAPListRid(ai, &APList_rid);
		readSsidRid(ai, &SSID_rid);
		reset_airo_card(dev);
		writeSsidRid(ai, &SSID_rid);
		writeAPListRid(ai, &APList_rid);
	}
	writeConfigRid(ai, &config);
	enable_MAC(ai, &rsp);
}

static int proc_config_open( struct inode *inode, struct file *file ) {
	struct proc_data *data;
	struct proc_dir_entry *dp = inode->u.generic_ip;
	struct net_device *dev = dp->data;
	struct airo_info *ai = (struct airo_info*)dev->priv;
	ConfigRid config;
	int i;

	MOD_INC_USE_COUNT;

	dp = (struct proc_dir_entry *) inode->u.generic_ip;

	if ((file->private_data = kmalloc(sizeof(struct proc_data ), GFP_KERNEL)) == NULL)
		return -ENOMEM;
	memset(file->private_data, 0, sizeof(struct proc_data));
	data = (struct proc_data *)file->private_data;
	if ((data->rbuffer = kmalloc( 2048, GFP_KERNEL )) == NULL) {
		kfree (file->private_data);
		return -ENOMEM;
	}
	if ((data->wbuffer = kmalloc( 2048, GFP_KERNEL )) == NULL) {
		kfree (data->rbuffer);
		kfree (file->private_data);
		return -ENOMEM;
	}
	memset( data->wbuffer, 0, 2048 );
	data->maxwritelen = 2048;
	data->on_close = proc_config_on_close;

	readConfigRid(ai, &config);

	i = sprintf( data->rbuffer,
		     "Mode: %s\n"
		     "Radio: %s\n"
		     "NodeName: %-16s\n"
		     "PowerMode: %s\n"
		     "DataRates: %d %d %d %d %d %d %d %d\n"
		     "Channel: %d\n"
		     "XmitPower: %d\n",
		     config.opmode == 0 ? "adhoc" :
		     config.opmode == 1 ? "ESS" :
		     config.opmode == 2 ? "AP" :
		     config.opmode == 3 ? "AP RPTR" : "Error",
		     ai->flags&FLAG_RADIO_OFF ? "off" : "on",
		     config.nodeName,
		     config.powerSaveMode == 0 ? "CAM" :
		     config.powerSaveMode == 1 ? "PSP" :
		     config.powerSaveMode == 2 ? "PSPCAM" : "Error",
		     (int)config.rates[0],
		     (int)config.rates[1],
		     (int)config.rates[2],
		     (int)config.rates[3],
		     (int)config.rates[4],
		     (int)config.rates[5],
		     (int)config.rates[6],
		     (int)config.rates[7],
		     (int)config.channelSet,
		     (int)config.txPower
		);
	sprintf( data->rbuffer + i,
		 "LongRetryLimit: %d\n"
		 "ShortRetryLimit: %d\n"
		 "RTSThreshold: %d\n"
		 "TXMSDULifetime: %d\n"
		 "RXMSDULifetime: %d\n"
		 "TXDiversity: %s\n"
		 "RXDiversity: %s\n"
		 "FragThreshold: %d\n"
		 "WEP: %s\n"
		 "Modulation: %s\n"
		 "Preamble: %s\n",
		 (int)config.longRetryLimit,
		 (int)config.shortRetryLimit,
		 (int)config.rtsThres,
		 (int)config.txLifetime,
		 (int)config.rxLifetime,
		 config.txDiversity == 1 ? "left" :
		 config.txDiversity == 2 ? "right" : "both",
		 config.rxDiversity == 1 ? "left" :
		 config.rxDiversity == 2 ? "right" : "both",
		 (int)config.fragThresh,
		 config.authType == AUTH_ENCRYPT ? "encrypt" :
		 config.authType == AUTH_SHAREDKEY ? "shared" : "open",
		 config.modulation == 0 ? "default" :
		 config.modulation == MOD_CCK ? "cck" :
		 config.modulation == MOD_MOK ? "mok" : "error",
		 config.preamble == PREAMBLE_AUTO ? "auto" :
		 config.preamble == PREAMBLE_LONG ? "long" :
		 config.preamble == PREAMBLE_SHORT ? "short" : "error"
		);
	data->readlen = strlen( data->rbuffer );
	return 0;
}

static void proc_SSID_on_close( struct inode *inode, struct file *file ) {
	struct proc_data *data = (struct proc_data *)file->private_data;
	struct proc_dir_entry *dp = inode->u.generic_ip;
	struct net_device *dev = dp->data;
	struct airo_info *ai = (struct airo_info*)dev->priv;
	SsidRid SSID_rid;
	int i;
	int offset = 0;

	if ( !data->writelen ) return;

	memset( &SSID_rid, 0, sizeof( SSID_rid ) );

	for( i = 0; i < 3; i++ ) {
		int j;
		for( j = 0; j+offset < data->writelen && j < 32 &&
			     data->wbuffer[offset+j] != '\n'; j++ ) {
			SSID_rid.ssids[i].ssid[j] = data->wbuffer[offset+j];
		}
		if ( j == 0 ) break;
		SSID_rid.ssids[i].len = j;
		offset += j;
		while( data->wbuffer[offset] != '\n' &&
		       offset < data->writelen ) offset++;
		offset++;
	}
	writeSsidRid(ai, &SSID_rid);
}

inline static u8 hexVal(char c) {
	if (c>='0' && c<='9') return c -= '0';
	if (c>='a' && c<='f') return c -= 'a'-10;
	if (c>='A' && c<='F') return c -= 'A'-10;
	return 0;
}

static void proc_APList_on_close( struct inode *inode, struct file *file ) {
	struct proc_data *data = (struct proc_data *)file->private_data;
	struct proc_dir_entry *dp = inode->u.generic_ip;
	struct net_device *dev = dp->data;
	struct airo_info *ai = (struct airo_info*)dev->priv;
	APListRid APList_rid;
	int i;

	if ( !data->writelen ) return;

	memset( &APList_rid, 0, sizeof(APList_rid) );
	APList_rid.len = sizeof(APList_rid);

	for( i = 0; i < 4 && data->writelen >= (i+1)*6*3; i++ ) {
		int j;
		for( j = 0; j < 6*3 && data->wbuffer[j+i*6*3]; j++ ) {
			switch(j%3) {
			case 0:
				APList_rid.ap[i][j/3]=
					hexVal(data->wbuffer[j+i*6*3])<<4;
				break;
			case 1:
				APList_rid.ap[i][j/3]|=
					hexVal(data->wbuffer[j+i*6*3]);
				break;
			}
		}
	}
	writeAPListRid(ai, &APList_rid);
}

/* This function wraps PC4500_writerid with a MAC disable */
static int do_writerid( struct airo_info *ai, u16 rid, const void *rid_data,
			int len ) {
	int rc;
	Resp rsp;

	disable_MAC(ai);
	rc = PC4500_writerid(ai, rid, rid_data, len);
	enable_MAC(ai, &rsp);
	return rc;
}

/* Returns the length of the key at the index.  If index == 0xffff
 * the index of the transmit key is returned.  If the key doesn't exist,
 * -1 will be returned.
 */
static int get_wep_key(struct airo_info *ai, u16 index) {
	WepKeyRid wkr;
	int rc;
	u16 lastindex;

	rc = readWepKeyRid(ai, &wkr, 1);
	if (rc == SUCCESS) do {
		lastindex = wkr.kindex;
		if (wkr.kindex == index) {
			if (index == 0xffff) {
				return wkr.mac[0];
			}
			return wkr.klen;
		}
		readWepKeyRid(ai, &wkr, 0);
	} while(lastindex != wkr.kindex);
	return -1;
}

static int set_wep_key(struct airo_info *ai, u16 index,
		       const char *key, u16 keylen, int perm ) {
	static const unsigned char macaddr[6] = { 0x01, 0, 0, 0, 0, 0 };
	WepKeyRid wkr;

	memset(&wkr, 0, sizeof(wkr));
	if (keylen == 0) {
// We are selecting which key to use
		wkr.len = sizeof(wkr);
		wkr.kindex = 0xffff;
		wkr.mac[0] = (char)index;
		if (perm) printk(KERN_INFO "Setting transmit key to %d\n", index);
		if (perm) ai->defindex = (char)index;
	} else {
// We are actually setting the key
		wkr.len = sizeof(wkr);
		wkr.kindex = index;
		wkr.klen = keylen;
		memcpy( wkr.key, key, keylen );
		memcpy( wkr.mac, macaddr, 6 );
		printk(KERN_INFO "Setting key %d\n", index);
	}

	writeWepKeyRid(ai, &wkr, perm);
	return 0;
}

static void proc_wepkey_on_close( struct inode *inode, struct file *file ) {
	struct proc_data *data;
	struct proc_dir_entry *dp = inode->u.generic_ip;
	struct net_device *dev = dp->data;
	struct airo_info *ai = (struct airo_info*)dev->priv;
	int i;
	char key[16];
	u16 index = 0;
	int j = 0;

	memset(key, 0, sizeof(key));

	dp = (struct proc_dir_entry *) inode->u.generic_ip;
	data = (struct proc_data *)file->private_data;
	if ( !data->writelen ) return;

	if (data->wbuffer[0] >= '0' && data->wbuffer[0] <= '3' &&
	    (data->wbuffer[1] == ' ' || data->wbuffer[1] == '\n')) {
		index = data->wbuffer[0] - '0';
		if (data->wbuffer[1] == '\n') {
			set_wep_key(ai, index, 0, 0, 1);
			return;
		}
		j = 2;
	} else {
		printk(KERN_ERR "airo:  WepKey passed invalid key index\n");
		return;
	}

	for( i = 0; i < 16*3 && data->wbuffer[i+j]; i++ ) {
		switch(i%3) {
		case 0:
			key[i/3] = hexVal(data->wbuffer[i+j])<<4;
			break;
		case 1:
			key[i/3] |= hexVal(data->wbuffer[i+j]);
			break;
		}
	}
	set_wep_key(ai, index, key, i/3, 1);
}

static int proc_wepkey_open( struct inode *inode, struct file *file ) {
	struct proc_data *data;
	struct proc_dir_entry *dp = inode->u.generic_ip;
	struct net_device *dev = dp->data;
	struct airo_info *ai = (struct airo_info*)dev->priv;
	char *ptr;
	WepKeyRid wkr;
	u16 lastindex;
	int j=0;
	int rc;

	MOD_INC_USE_COUNT;

	dp = (struct proc_dir_entry *) inode->u.generic_ip;

	if ((file->private_data = kmalloc(sizeof(struct proc_data ), GFP_KERNEL)) == NULL)
		return -ENOMEM;
	memset(file->private_data, 0, sizeof(struct proc_data));
	memset(&wkr, 0, sizeof(wkr));
	data = (struct proc_data *)file->private_data;
	if ((data->rbuffer = kmalloc( 180, GFP_KERNEL )) == NULL) {
		kfree (file->private_data);
		return -ENOMEM;
	}
	memset(data->rbuffer, 0, 180);
	data->writelen = 0;
	data->maxwritelen = 80;
	if ((data->wbuffer = kmalloc( 80, GFP_KERNEL )) == NULL) {
		kfree (data->rbuffer);
		kfree (file->private_data);
		return -ENOMEM;
	}
	memset( data->wbuffer, 0, 80 );
	data->on_close = proc_wepkey_on_close;

	ptr = data->rbuffer;
	strcpy(ptr, "No wep keys\n");
	rc = readWepKeyRid(ai, &wkr, 1);
	if (rc == SUCCESS) do {
		lastindex = wkr.kindex;
		if (wkr.kindex == 0xffff) {
			j += sprintf(ptr+j, "Tx key = %d\n",
				     (int)wkr.mac[0]);
		} else {
			j += sprintf(ptr+j, "Key %d set with length = %d\n",
				     (int)wkr.kindex, (int)wkr.klen);
		}
		readWepKeyRid(ai, &wkr, 0);
	} while((lastindex != wkr.kindex) && (j < 180-30));

	data->readlen = strlen( data->rbuffer );
	return 0;
}

static int proc_SSID_open( struct inode *inode, struct file *file ) {
	struct proc_data *data;
	struct proc_dir_entry *dp = inode->u.generic_ip;
	struct net_device *dev = dp->data;
	struct airo_info *ai = (struct airo_info*)dev->priv;
	int i;
	char *ptr;
	SsidRid SSID_rid;

	MOD_INC_USE_COUNT;

	dp = (struct proc_dir_entry *) inode->u.generic_ip;

	if ((file->private_data = kmalloc(sizeof(struct proc_data ), GFP_KERNEL)) == NULL)
		return -ENOMEM;
	memset(file->private_data, 0, sizeof(struct proc_data));
	data = (struct proc_data *)file->private_data;
	if ((data->rbuffer = kmalloc( 104, GFP_KERNEL )) == NULL) {
		kfree (file->private_data);
		return -ENOMEM;
	}
	data->writelen = 0;
	data->maxwritelen = 33*3;
	if ((data->wbuffer = kmalloc( 33*3, GFP_KERNEL )) == NULL) {
		kfree (data->rbuffer);
		kfree (file->private_data);
		return -ENOMEM;
	}
	memset( data->wbuffer, 0, 33*3 );
	data->on_close = proc_SSID_on_close;

	readSsidRid(ai, &SSID_rid);
	ptr = data->rbuffer;
	for( i = 0; i < 3; i++ ) {
		int j;
		if ( !SSID_rid.ssids[i].len ) break;
		for( j = 0; j < 32 &&
			     j < SSID_rid.ssids[i].len &&
			     SSID_rid.ssids[i].ssid[j]; j++ ) {
			*ptr++ = SSID_rid.ssids[i].ssid[j];
		}
		*ptr++ = '\n';
	}
	*ptr = '\0';
	data->readlen = strlen( data->rbuffer );
	return 0;
}

static int proc_APList_open( struct inode *inode, struct file *file ) {
	struct proc_data *data;
	struct proc_dir_entry *dp = inode->u.generic_ip;
	struct net_device *dev = dp->data;
	struct airo_info *ai = (struct airo_info*)dev->priv;
	int i;
	char *ptr;
	APListRid APList_rid;

	MOD_INC_USE_COUNT;

	dp = (struct proc_dir_entry *) inode->u.generic_ip;

	if ((file->private_data = kmalloc(sizeof(struct proc_data ), GFP_KERNEL)) == NULL)
		return -ENOMEM;
	memset(file->private_data, 0, sizeof(struct proc_data));
	data = (struct proc_data *)file->private_data;
	if ((data->rbuffer = kmalloc( 104, GFP_KERNEL )) == NULL) {
		kfree (file->private_data);
		return -ENOMEM;
	}
	data->writelen = 0;
	data->maxwritelen = 4*6*3;
	if ((data->wbuffer = kmalloc( data->maxwritelen, GFP_KERNEL )) == NULL) {
		kfree (data->rbuffer);
		kfree (file->private_data);
		return -ENOMEM;
	}
	memset( data->wbuffer, 0, data->maxwritelen );
	data->on_close = proc_APList_on_close;

	readAPListRid(ai, &APList_rid);
	ptr = data->rbuffer;
	for( i = 0; i < 4; i++ ) {
// We end when we find a zero MAC
		if ( !*(int*)APList_rid.ap[i] &&
		     !*(int*)&APList_rid.ap[i][2]) break;
		ptr += sprintf(ptr, "%02x:%02x:%02x:%02x:%02x:%02x\n",
			       (int)APList_rid.ap[i][0],
			       (int)APList_rid.ap[i][1],
			       (int)APList_rid.ap[i][2],
			       (int)APList_rid.ap[i][3],
			       (int)APList_rid.ap[i][4],
			       (int)APList_rid.ap[i][5]);
	}
	if (i==0) ptr += sprintf(ptr, "Not using specific APs\n");

	*ptr = '\0';
	data->readlen = strlen( data->rbuffer );
	return 0;
}

static int proc_BSSList_open( struct inode *inode, struct file *file ) {
	struct proc_data *data;
	struct proc_dir_entry *dp = inode->u.generic_ip;
	struct net_device *dev = dp->data;
	struct airo_info *ai = (struct airo_info*)dev->priv;
	char *ptr;
	BSSListRid BSSList_rid;
	int rc;
	/* If doLoseSync is not 1, we won't do a Lose Sync */
	int doLoseSync = -1;

	MOD_INC_USE_COUNT;

	dp = (struct proc_dir_entry *) inode->u.generic_ip;

	if ((file->private_data = kmalloc(sizeof(struct proc_data ), GFP_KERNEL)) == NULL)
		return -ENOMEM;
	memset(file->private_data, 0, sizeof(struct proc_data));
	data = (struct proc_data *)file->private_data;
	if ((data->rbuffer = kmalloc( 1024, GFP_KERNEL )) == NULL) {
		kfree (file->private_data);
		return -ENOMEM;
	}
	data->writelen = 0;
	data->maxwritelen = 0;
	data->wbuffer = 0;
	data->on_close = 0;

	if (file->f_mode & FMODE_WRITE) {
		if (!(file->f_mode & FMODE_READ)) {
			Cmd cmd;
			Resp rsp;

			memset(&cmd, 0, sizeof(cmd));
			cmd.cmd=CMD_LISTBSS;
			lock_issuecommand(ai, &cmd, &rsp);
			data->readlen = 0;
			return 0;
		}
		doLoseSync = 1;
	}
	ptr = data->rbuffer;
	/* There is a race condition here if there are concurrent opens.
           Since it is a rare condition, we'll just live with it, otherwise
           we have to add a spin lock... */
	rc = readBSSListRid(ai, doLoseSync, &BSSList_rid);
	while(rc == 0 && BSSList_rid.index != 0xffff) {
		ptr += sprintf(ptr, "%02x:%02x:%02x:%02x:%02x:%02x %*s rssi = %d",
				(int)BSSList_rid.bssid[0],
				(int)BSSList_rid.bssid[1],
				(int)BSSList_rid.bssid[2],
				(int)BSSList_rid.bssid[3],
				(int)BSSList_rid.bssid[4],
				(int)BSSList_rid.bssid[5],
				(int)BSSList_rid.ssidLen,
				BSSList_rid.ssid,
				(int)BSSList_rid.rssi);
		ptr += sprintf(ptr, " channel = %d %s %s %s %s\n",
				(int)BSSList_rid.dsChannel,
				BSSList_rid.cap & CAP_ESS ? "ESS" : "",
				BSSList_rid.cap & CAP_IBSS ? "adhoc" : "",
				BSSList_rid.cap & CAP_PRIVACY ? "wep" : "",
				BSSList_rid.cap & CAP_SHORTHDR ? "shorthdr" : "");
		rc = readBSSListRid(ai, 0, &BSSList_rid);
	}
	*ptr = '\0';
	data->readlen = strlen( data->rbuffer );
	return 0;
}

static int proc_close( struct inode *inode, struct file *file )
{
	struct proc_data *data = (struct proc_data *)file->private_data;
	if ( data->on_close != NULL ) data->on_close( inode, file );
	MOD_DEC_USE_COUNT;
	if ( data->rbuffer ) kfree( data->rbuffer );
	if ( data->wbuffer ) kfree( data->wbuffer );
	kfree( data );
	return 0;
}

static struct net_device_list {
	struct net_device *dev;
	struct net_device_list *next;
} *airo_devices = 0;

/* Since the card doesnt automatically switch to the right WEP mode,
   we will make it do it.  If the card isn't associated, every secs we
   will switch WEP modes to see if that will help.  If the card is
   associated we will check every minute to see if anything has
   changed. */
static void timer_func( u_long data ) {
	struct net_device *dev = (struct net_device*)data;
	struct airo_info *apriv = (struct airo_info *)dev->priv;
	u16 linkstat = IN4500(apriv, LINKSTAT);

	if (linkstat != 0x400 ) {
/* We don't have a link so try changing the authtype */
		ConfigRid config = apriv->config;

		switch(apriv->authtype) {
		case AUTH_ENCRYPT:
/* So drop to OPEN */
			config.authType = AUTH_OPEN;
			apriv->authtype = AUTH_OPEN;
			break;
		case AUTH_SHAREDKEY:
			if (apriv->keyindex < auto_wep) {
				set_wep_key(apriv, apriv->keyindex, 0, 0, 0);
				config.authType = AUTH_SHAREDKEY;
				apriv->authtype = AUTH_SHAREDKEY;
				apriv->keyindex++;
			} else {
			        /* Drop to ENCRYPT */
				apriv->keyindex = 0;
				set_wep_key(apriv, apriv->defindex, 0, 0, 0);
				config.authType = AUTH_ENCRYPT;
				apriv->authtype = AUTH_ENCRYPT;
			}
			break;
		default:  /* We'll escalate to SHAREDKEY */
			config.authType = AUTH_SHAREDKEY;
			apriv->authtype = AUTH_SHAREDKEY;
		}
		checkThrottle(&config);
		writeConfigRid(apriv, &config);

/* Schedule check to see if the change worked */
		apriv->timer.expires = RUN_AT(HZ*3);
		add_timer(&apriv->timer);
	}
}

static int add_airo_dev( struct net_device *dev ) {
	struct net_device_list *node = kmalloc( sizeof( *node ), GFP_KERNEL );
	if ( !node )
		return -ENOMEM;

	if ( auto_wep ) {
		struct airo_info *apriv=dev->priv;
		struct timer_list *timer = &apriv->timer;

		timer->function = timer_func;
		timer->data = (u_long)dev;
		init_timer(timer);
		apriv->authtype = AUTH_SHAREDKEY;
	}

	node->dev = dev;
	node->next = airo_devices;
	airo_devices = node;

	return 0;
}

static void del_airo_dev( struct net_device *dev ) {
	struct net_device_list **p = &airo_devices;
	while( *p && ( (*p)->dev != dev ) )
		p = &(*p)->next;
	if ( *p && (*p)->dev == dev )
		*p = (*p)->next;
}

#ifdef CONFIG_PCI
static int __devinit airo_pci_probe(struct pci_dev *pdev,
				    const struct pci_device_id *pent)
{
	struct net_device *dev;

	dev = init_airo_card(pdev->irq,	pdev->resource[2].start, 0);
	if (!dev)
		return -ENODEV;

	pci_set_drvdata(pdev, dev);
	return 0;
}

static void __devexit airo_pci_remove(struct pci_dev *pdev)
{
	stop_airo_card(pci_get_drvdata(pdev), 1);
}
#endif

static int __init airo_init_module( void )
{
	int i, rc = 0, have_isa_dev = 0;

	airo_entry = create_proc_entry("aironet",
				       S_IFDIR | airo_perm,
				       proc_root_driver);
        airo_entry->uid = proc_uid;
        airo_entry->gid = proc_gid;

	for( i = 0; i < 4 && io[i] && irq[i]; i++ ) {
		printk( KERN_INFO
			"airo:  Trying to configure ISA adapter at irq=%d io=0x%x\n",
			irq[i], io[i] );
		if (init_airo_card( irq[i], io[i], 0 ))
			have_isa_dev = 1;
	}

#ifdef CONFIG_PCI
	printk( KERN_INFO "airo:  Probing for PCI adapters\n" );
	rc = pci_module_init(&airo_driver);
	printk( KERN_INFO "airo:  Finished probing for PCI adapters\n" );
#endif

	/* Always exit with success, as we are a library module
	 * as well as a driver module
	 */
	return 0;
}

static void __exit airo_cleanup_module( void )
{
	while( airo_devices ) {
		printk( KERN_INFO "airo: Unregistering %s\n", airo_devices->dev->name );
		stop_airo_card( airo_devices->dev, 1 );
	}
	remove_proc_entry("aironet", proc_root_driver);
}

#ifdef WIRELESS_EXT
/*
 * Initial Wireless Extension code for Aironet driver by :
 *	Jean Tourrilhes <jt@hpl.hp.com> - HPL - 17 November 00
 */
#ifndef IW_ENCODE_NOKEY
#define IW_ENCODE_NOKEY         0x0800  /* Key is write only, so not present */
#define IW_ENCODE_MODE  (IW_ENCODE_DISABLED | IW_ENCODE_RESTRICTED | IW_ENCODE_OPEN)
#endif /* IW_ENCODE_NOKEY */
#endif /* WIRELESS_EXT */

/*
 * This defines the configuration part of the Wireless Extensions
 * Note : irq and spinlock protection will occur in the subroutines
 *
 * TODO :
 *	o Check input value more carefully and fill correct values in range
 *	o Implement : POWER, SPY, APLIST
 *	o Optimise when adapter is closed (aggregate changes, commit later)
 *	o Test and shakeout the bugs (if any)
 *
 * Jean II
 *
 * Javier Achirica did a great job of merging code from the unnamed CISCO
 * developer that added support for flashing the card.
 */
static int airo_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	int i, rc = 0;
#ifdef WIRELESS_EXT
	struct airo_info *local = (struct airo_info*) dev->priv;
	struct iwreq *wrq = (struct iwreq *) rq;
	ConfigRid config;		/* Configuration info */
	CapabilityRid cap_rid;		/* Card capability info */
	StatusRid status_rid;		/* Card status info */

#ifdef CISCO_EXT
	if (cmd != SIOCGIWPRIV && cmd != AIROIOCTL && cmd != AIROIDIFC
#ifdef AIROOLDIOCTL
		&& cmd != AIROOLDIOCTL && cmd != AIROOLDIDIFC
#endif
		)
#endif /* CISCO_EXT */
	{
		/* If the command read some stuff, we better get it out of
		 * the card first... */
		if(IW_IS_GET(cmd))
			readStatusRid(local, &status_rid);
		if(IW_IS_GET(cmd) || (cmd == SIOCSIWRATE) || (cmd == SIOCSIWENCODE))
			readCapabilityRid(local, &cap_rid);
		/* Get config in all cases, because SET will just modify it */
		readConfigRid(local, &config);
	}
#endif /* WIRELESS_EXT */

	switch (cmd) {
#ifdef WIRELESS_EXT
		// Get name
	case SIOCGIWNAME:
		strcpy(wrq->u.name, "IEEE 802.11-DS");
		break;

		// Set frequency/channel
	case SIOCSIWFREQ:
		/* If setting by frequency, convert to a channel */
		if((wrq->u.freq.e == 1) &&
		   (wrq->u.freq.m >= (int) 2.412e8) &&
		   (wrq->u.freq.m <= (int) 2.487e8)) {
			int f = wrq->u.freq.m / 100000;
			int c = 0;
			while((c < 14) && (f != frequency_list[c]))
				c++;
			/* Hack to fall through... */
			wrq->u.freq.e = 0;
			wrq->u.freq.m = c + 1;
		}
		/* Setting by channel number */
		if((wrq->u.freq.m > 1000) || (wrq->u.freq.e > 0))
			rc = -EOPNOTSUPP;
		else {
			int channel = wrq->u.freq.m;
			/* We should do a better check than that,
			 * based on the card capability !!! */
			if((channel < 1) || (channel > 16)) {
				printk(KERN_DEBUG "%s: New channel value of %d is invalid!\n", dev->name, wrq->u.freq.m);
				rc = -EINVAL;
			} else {
				/* Yes ! We can set it !!! */
				config.channelSet = (u16)(channel - 1);
				local->need_commit = 1;
			}
		}
		break;

		// Get frequency/channel
	case SIOCGIWFREQ:
#ifdef WEXT_USECHANNELS
		wrq->u.freq.m = ((int)status_rid.channel) + 1;
		wrq->u.freq.e = 0;
#else
		{
			int f = (int)status_rid.channel;
			wrq->u.freq.m = frequency_list[f] * 100000;
			wrq->u.freq.e = 1;
		}
#endif
		break;

		// Set desired network name (ESSID)
	case SIOCSIWESSID:
		if (wrq->u.data.pointer) {
			char	essid[IW_ESSID_MAX_SIZE + 1];
			SsidRid SSID_rid;		/* SSIDs */

			/* Reload the list of current SSID */
			readSsidRid(local, &SSID_rid);

			/* Check if we asked for `any' */
			if(wrq->u.data.flags == 0) {
				/* Just send an empty SSID list */
				memset(&SSID_rid, 0, sizeof(SSID_rid));
			} else {
				int	index = (wrq->u.data.flags &
						 IW_ENCODE_INDEX) - 1;

				/* Check the size of the string */
				if(wrq->u.data.length > IW_ESSID_MAX_SIZE+1) {
					rc = -E2BIG;
					break;
				}
				/* Check if index is valid */
				if((index < 0) || (index >= 4)) {
					rc = -EINVAL;
					break;
				}

				/* Set the SSID */
				memset(essid, 0, sizeof(essid));
				if (copy_from_user(essid,
					       wrq->u.data.pointer,
					       wrq->u.data.length)) {
					rc = -EFAULT;
					break;
				}
				memcpy(SSID_rid.ssids[index].ssid, essid,
				       sizeof(essid) - 1);
				SSID_rid.ssids[index].len = wrq->u.data.length - 1;
			}
			/* Write it to the card */
			writeSsidRid(local, &SSID_rid);
		}
		break;

		// Get current network name (ESSID)
	case SIOCGIWESSID:
		if (wrq->u.data.pointer) {
			char essid[IW_ESSID_MAX_SIZE + 1];

			/* Note : if wrq->u.data.flags != 0, we should
			 * get the relevant SSID from the SSID list... */

			/* Get the current SSID */
			memcpy(essid, status_rid.SSID, status_rid.SSIDlen);
			essid[status_rid.SSIDlen] = '\0';
			/* If none, we may want to get the one that was set */

			/* Push it out ! */
			wrq->u.data.length = strlen(essid) + 1;
			wrq->u.data.flags = 1; /* active */
			if (copy_to_user(wrq->u.data.pointer, essid, sizeof(essid)))
				rc = -EFAULT;
		}
		break;

	case SIOCSIWAP:
		if (wrq->u.ap_addr.sa_family != ARPHRD_ETHER)
			rc = -EINVAL;
		else {
			APListRid APList_rid;

			memset(&APList_rid, 0, sizeof(APList_rid));
			APList_rid.len = sizeof(APList_rid);
			memcpy(APList_rid.ap[0], wrq->u.ap_addr.sa_data, 6);
			writeAPListRid(local, &APList_rid);
			local->need_commit = 1;
		}
		break;

		// Get current Access Point (BSSID)
	case SIOCGIWAP:
		/* Tentative. This seems to work, wow, I'm lucky !!! */
		memcpy(wrq->u.ap_addr.sa_data, status_rid.bssid[0], 6);
		wrq->u.ap_addr.sa_family = ARPHRD_ETHER;
		break;

		// Set desired station name
	case SIOCSIWNICKN:
		if (wrq->u.data.pointer) {
			char	name[16 + 1];

			/* Check the size of the string */
			if(wrq->u.data.length > 16 + 1) {
				rc = -E2BIG;
				break;
			}
			memset(name, 0, sizeof(name));
			if (copy_from_user(name, wrq->u.data.pointer,
					   wrq->u.data.length)) {
				rc = -EFAULT;
				break;
			}
			memcpy(config.nodeName, name, 16);
			local->need_commit = 1;
		}
		break;

		// Get current station name
	case SIOCGIWNICKN:
		if (wrq->u.data.pointer) {
			char name[IW_ESSID_MAX_SIZE + 1];

			strncpy(name, config.nodeName, 16);
			name[16] = '\0';
			wrq->u.data.length = strlen(name) + 1;
			if (copy_to_user(wrq->u.data.pointer, name, sizeof(name)))
				rc = -EFAULT;
		}
		break;

		// Set the desired bit-rate
	case SIOCSIWRATE:
	{
		/* First : get a valid bit rate value */
		u8	brate = 0;
		int	i;

		/* Which type of value ? */
		if((wrq->u.bitrate.value < 8) &&
		   (wrq->u.bitrate.value >= 0)) {
			/* Setting by rate index */
			/* Find value in the magic rate table */
			brate = cap_rid.supportedRates[wrq->u.bitrate.value];
		} else {
			/* Setting by frequency value */
			u8	normvalue = (u8) (wrq->u.bitrate.value/500000);

			/* Check if rate is valid */
			for(i = 0 ; i < 8 ; i++) {
				if(normvalue == cap_rid.supportedRates[i]) {
					brate = normvalue;
					break;
				}
			}
		}
		/* -1 designed the max rate (mostly auto mode) */
		if(wrq->u.bitrate.value == -1) {
			/* Get the highest available rate */
			for(i = 0 ; i < 8 ; i++) {
				if(cap_rid.supportedRates[i] == 0)
					break;
			}
			if(i != 0)
				brate = cap_rid.supportedRates[i - 1];
		}
		/* Check that it is valid */
		if(brate == 0) {
			rc = -EINVAL;
			break;
		}

		/* Now, check if we want a fixed or auto value */
		if(wrq->u.bitrate.fixed == 0) {
			/* Fill all the rates up to this max rate */
			memset(config.rates, 0, 8);
			for(i = 0 ; i < 8 ; i++) {
				config.rates[i] = cap_rid.supportedRates[i];
				if(config.rates[i] == brate)
					break;
			}
			local->need_commit = 1;
		} else {
			/* Fixed mode */
			/* One rate, fixed */
			memset(config.rates, 0, 8);
			config.rates[0] = brate;
			local->need_commit = 1;
		}
		break;
	}

	// Get the current bit-rate
	case SIOCGIWRATE:
	{
		int brate = status_rid.currentXmitRate;
		wrq->u.bitrate.value = brate * 500000;
		/* If more than one rate, set auto */
		wrq->u.rts.fixed = (config.rates[1] == 0);
	}
	break;

	// Set the desired RTS threshold
	case SIOCSIWRTS:
	{
		int rthr = wrq->u.rts.value;
		if(wrq->u.rts.disabled)
			rthr = 2312;
		if((rthr < 0) || (rthr > 2312)) {
			rc = -EINVAL;
		} else {
			config.rtsThres = rthr;
			local->need_commit = 1;
		}
	}
	break;

	// Get the current RTS threshold
	case SIOCGIWRTS:
		wrq->u.rts.value = config.rtsThres;
		wrq->u.rts.disabled = (wrq->u.rts.value >= 2312);
		wrq->u.rts.fixed = 1;
		break;

		// Set the desired fragmentation threshold
	case SIOCSIWFRAG:
	{
		int fthr = wrq->u.frag.value;
		if(wrq->u.frag.disabled)
			fthr = 2312;
		if((fthr < 256) || (fthr > 2312)) {
			rc = -EINVAL;
		} else {
			fthr &= ~0x1;	/* Get an even value */
			config.fragThresh = (u16)fthr;
			local->need_commit = 1;
		}
	}
	break;

	// Get the current fragmentation threshold
	case SIOCGIWFRAG:
		wrq->u.frag.value = config.fragThresh;
		wrq->u.frag.disabled = (wrq->u.frag.value >= 2312);
		wrq->u.frag.fixed = 1;
		break;

		// Set mode of operation
	case SIOCSIWMODE:
		switch(wrq->u.mode) {
		case IW_MODE_ADHOC:
			config.opmode = MODE_STA_IBSS;
			local->need_commit = 1;
			break;
		case IW_MODE_INFRA:
			config.opmode = MODE_STA_ESS;
			local->need_commit = 1;
			break;
		case IW_MODE_MASTER:
			config.opmode = MODE_AP;
			local->need_commit = 1;
			break;
		case IW_MODE_REPEAT:
			config.opmode = MODE_AP_RPTR;
			local->need_commit = 1;
			break;
		default:
			rc = -EINVAL;
		}
		break;

		// Get mode of operation
	case SIOCGIWMODE:
		/* If not managed, assume it's ad-hoc */
		switch (config.opmode & 0xFF) {
		case MODE_STA_ESS:
			wrq->u.mode = IW_MODE_INFRA;
			break;
		case MODE_AP:
			wrq->u.mode = IW_MODE_MASTER;
			break;
		case MODE_AP_RPTR:
			wrq->u.mode = IW_MODE_REPEAT;
			break;
		default:
			wrq->u.mode = IW_MODE_ADHOC;
		}
		break;

		// Set WEP keys and mode
	case SIOCSIWENCODE:
		/* Is WEP supported ? */
		/* Older firmware doesn't support this...
		if(!(cap_rid.softCap & 2)) {
			rc = -EOPNOTSUPP;
			break;
		} */
		/* Basic checking: do we have a key to set ? */
		if (wrq->u.encoding.pointer != (caddr_t) 0) {
			wep_key_t key;
			int index = (wrq->u.encoding.flags & IW_ENCODE_INDEX) - 1;
			int current_index = get_wep_key(local, 0xffff);
			/* Check the size of the key */
			if (wrq->u.encoding.length > MAX_KEY_SIZE) {
				rc = -EINVAL;
				break;
			}
			/* Check the index (none -> use current) */
			if ((index < 0) || (index>=(cap_rid.softCap&0x80)?4:1))
				index = current_index;
			/* Set the length */
			if (wrq->u.encoding.length > MIN_KEY_SIZE)
				key.len = MAX_KEY_SIZE;
			else
				if (wrq->u.encoding.length > 0)
					key.len = MIN_KEY_SIZE;
				else
					/* Disable the key */
					key.len = 0;
			/* Check if the key is not marked as invalid */
			if(!(wrq->u.encoding.flags & IW_ENCODE_NOKEY)) {
				/* Cleanup */
				memset(key.key, 0, MAX_KEY_SIZE);
				/* Copy the key in the driver */
				if(copy_from_user(key.key,
						  wrq->u.encoding.pointer,
						  wrq->u.encoding.length)) {
					key.len = 0;
					rc = -EFAULT;
					break;
				}
				/* Send the key to the card */
				set_wep_key(local, index, key.key,
					    key.len, 1);
			}
			/* WE specify that if a valid key is set, encryption
			 * should be enabled (user may turn it off later)
			 * This is also how "iwconfig ethX key on" works */
			if((index == current_index) && (key.len > 0) &&
			   (config.authType == AUTH_OPEN)) {
				config.authType = AUTH_ENCRYPT;
				local->need_commit = 1;
			}
		} else {
			/* Do we want to just set the transmit key index ? */
			int index = (wrq->u.encoding.flags & IW_ENCODE_INDEX) - 1;
			if ((index>=0) && (index<(cap_rid.softCap&0x80)?4:1)) {
				set_wep_key(local, index, 0, 0, 1);
			} else
				/* Don't complain if only change the mode */
				if(!wrq->u.encoding.flags & IW_ENCODE_MODE) {
					rc = -EINVAL;
					break;
				}
		}
		/* Read the flags */
		if(wrq->u.encoding.flags & IW_ENCODE_DISABLED)
			config.authType = AUTH_OPEN;	// disable encryption
		if(wrq->u.encoding.flags & IW_ENCODE_RESTRICTED)
			config.authType = AUTH_SHAREDKEY;	// Only Both
		if(wrq->u.encoding.flags & IW_ENCODE_OPEN)
			config.authType = AUTH_ENCRYPT;	// Only Wep
		/* Commit the changes if needed */
		if(wrq->u.encoding.flags & IW_ENCODE_MODE)
			local->need_commit = 1;
		break;

		// Get the WEP keys and mode
	case SIOCGIWENCODE:
		/* Is it supported ? */
		if(!(cap_rid.softCap & 2)) {
			rc = -EOPNOTSUPP;
			break;
		}
		// Only super-user can see WEP key
		if (!capable(CAP_NET_ADMIN)) {
			rc = -EPERM;
			break;
		}

		// Basic checking...
		if (wrq->u.encoding.pointer != (caddr_t) 0) {
			char zeros[16];
			int index = (wrq->u.encoding.flags & IW_ENCODE_INDEX) - 1;

			memset(zeros,0, sizeof(zeros));
			/* Check encryption mode */
			wrq->u.encoding.flags = IW_ENCODE_NOKEY;
			/* Is WEP enabled ??? */
			switch(config.authType)	{
			case AUTH_ENCRYPT:
				wrq->u.encoding.flags |= IW_ENCODE_OPEN;
				break;
			case AUTH_SHAREDKEY:
				wrq->u.encoding.flags |= IW_ENCODE_RESTRICTED;
				break;
			default:
			case AUTH_OPEN:
				wrq->u.encoding.flags |= IW_ENCODE_DISABLED;
				break;
			}

			/* Which key do we want ? -1 -> tx index */
			if((index < 0) || (index >= (cap_rid.softCap&0x80)?4:1))
				index = get_wep_key(local, 0xffff);
			wrq->u.encoding.flags |= index + 1;
			/* Copy the key to the user buffer */
			wrq->u.encoding.length = get_wep_key(local, index);
			if (wrq->u.encoding.length > 16) {
				wrq->u.encoding.length=0;
			}

			if(copy_to_user(wrq->u.encoding.pointer, zeros,
					wrq->u.encoding.length))
				rc = -EFAULT;
		}
		break;

#if WIRELESS_EXT > 9
		// Get the current Tx-Power
	case SIOCGIWTXPOW:
		wrq->u.txpower.value = config.txPower;
		wrq->u.txpower.fixed = 1;	/* No power control */
		wrq->u.txpower.disabled = (local->flags & FLAG_RADIO_OFF);
		wrq->u.txpower.flags = IW_TXPOW_MWATT;
		break;
	case SIOCSIWTXPOW:
		if (wrq->u.txpower.disabled) {
			local->flags |= FLAG_RADIO_OFF;
			local->need_commit = 1;
			break;
		}
		if (wrq->u.txpower.flags != IW_TXPOW_MWATT) {
			rc = -EINVAL;
			break;
		}
		local->flags &= ~FLAG_RADIO_OFF;
		rc = -EINVAL;
		for (i = 0; cap_rid.txPowerLevels[i] && (i < 8); i++)
			if ((wrq->u.txpower.value==cap_rid.txPowerLevels[i])) {
				config.txPower = wrq->u.txpower.value;
				local->need_commit = 1;
				rc = 0;
				break;
			}
		break;
#endif /* WIRELESS_EXT > 9 */

#if WIRELESS_EXT > 10
	case SIOCSIWRETRY:
		if(wrq->u.retry.disabled) {
			rc = -EINVAL;
			break;
		}
		local->need_commit = 0;
		if(wrq->u.retry.flags & IW_RETRY_LIMIT) {
			if(wrq->u.retry.flags & IW_RETRY_MAX)
				config.longRetryLimit = wrq->u.retry.value;
			else if (wrq->u.retry.flags & IW_RETRY_MIN)
				config.shortRetryLimit = wrq->u.retry.value;
			else {
				/* No modifier : set both */
				config.longRetryLimit = wrq->u.retry.value;
				config.shortRetryLimit = wrq->u.retry.value;
			}
			local->need_commit = 1;
		}
		if(wrq->u.retry.flags & IW_RETRY_LIFETIME) {
			config.txLifetime = wrq->u.retry.value / 1024;
			local->need_commit = 1;
		}
		if(local->need_commit == 0) {
			rc = -EINVAL;
		}
		break;

	case SIOCGIWRETRY:
		wrq->u.retry.disabled = 0;      /* Can't be disabled */

		/* Note : by default, display the min retry number */
		if((wrq->u.retry.flags & IW_RETRY_TYPE) == IW_RETRY_LIFETIME) {
			wrq->u.retry.flags = IW_RETRY_LIFETIME;
			wrq->u.retry.value = (int)config.txLifetime * 1024;
		} else if((wrq->u.retry.flags & IW_RETRY_MAX)) {
			wrq->u.retry.flags = IW_RETRY_LIMIT | IW_RETRY_MAX;
			wrq->u.retry.value = (int)config.longRetryLimit;
		} else {
			wrq->u.retry.flags = IW_RETRY_LIMIT;
			wrq->u.retry.value = (int)config.shortRetryLimit;
			if((int)config.shortRetryLimit != (int)config.longRetryLimit)
				wrq->u.retry.flags |= IW_RETRY_MIN;
		}

		break;
#endif /* WIRELESS_EXT > 10 */

		// Get range of parameters
	case SIOCGIWRANGE:
		if (wrq->u.data.pointer) {
			struct iw_range range;
			int		i;
			int		k;

			wrq->u.data.length = sizeof(range);
			memset(&range, 0, sizeof(range));
			range.min_nwid = 0x0000;
			range.max_nwid = 0x0000;
			range.num_channels = 14;
			/* Should be based on cap_rid.country to give only
			 * what the current card support */
			k = 0;
			for(i = 0; i < 14; i++) {
				range.freq[k].i = i + 1; /* List index */
				range.freq[k].m = frequency_list[i] * 100000;
				range.freq[k++].e = 1;	/* Values in table in MHz -> * 10^5 * 10 */
			}
			range.num_frequency = k;

			/* Hum... Should put the right values there */
			range.max_qual.qual = 10;
			range.max_qual.level = 0x100 - 120;	/* -120 dBm */
			range.max_qual.noise = 0;
			range.sensitivity = 65535;

			for(i = 0 ; i < 8 ; i++) {
				range.bitrate[i] = cap_rid.supportedRates[i] * 500000;
				if(range.bitrate[i] == 0)
					break;
			}
			range.num_bitrates = i;

			/* Set an indication of the max TCP throughput
			 * in bit/s that we can expect using this interface.
			 * May be use for QoS stuff... Jean II */
			if(i > 2)
				range.throughput = 5 * 1000 * 1000;
			else
				range.throughput = 1.5 * 1000 * 1000;

			range.min_rts = 0;
			range.max_rts = 2312;
			range.min_frag = 256;
			range.max_frag = 2312;

			if(cap_rid.softCap & 2) {
				// WEP: RC4 40 bits
				range.encoding_size[0] = 5;
				// RC4 ~128 bits
				if (cap_rid.softCap & 0x100) {
					range.encoding_size[1] = 13;
					range.num_encoding_sizes = 2;
				} else
					range.num_encoding_sizes = 1;
				range.max_encoding_tokens = (cap_rid.softCap & 0x80) ? 4 : 1;
			} else {
				range.num_encoding_sizes = 0;
				range.max_encoding_tokens = 0;
			}
#if WIRELESS_EXT > 9
			range.min_pmp = 0;
			range.max_pmp = 5000000;	/* 5 secs */
			range.min_pmt = 0;
			range.max_pmt = 65535 * 1024;	/* ??? */
			range.pmp_flags = IW_POWER_PERIOD;
			range.pmt_flags = IW_POWER_TIMEOUT;
			range.pm_capa = IW_POWER_PERIOD | IW_POWER_TIMEOUT | IW_POWER_ALL_R;

			/* Transmit Power - values are in mW */
			for(i = 0 ; i < 8 ; i++) {
				range.txpower[i] = cap_rid.txPowerLevels[i];
				if(range.txpower[i] == 0)
					break;
			}
			range.num_txpower = i;
			range.txpower_capa = IW_TXPOW_MWATT;
#endif /* WIRELESS_EXT > 9 */
#if WIRELESS_EXT > 10
			range.we_version_source = 12;
			range.we_version_compiled = WIRELESS_EXT;
			range.retry_capa = IW_RETRY_LIMIT | IW_RETRY_LIFETIME;
			range.retry_flags = IW_RETRY_LIMIT;
			range.r_time_flags = IW_RETRY_LIFETIME;
			range.min_retry = 1;
			range.max_retry = 65535;
			range.min_r_time = 1024;
			range.max_r_time = 65535 * 1024;
#endif /* WIRELESS_EXT > 10 */
#if WIRELESS_EXT > 11
			/* Experimental measurements - boundary 11/5.5 Mb/s */
			/* Note : with or without the (local->rssi), results
			 * are somewhat different. - Jean II */
			range.avg_qual.qual = 6;
			if (local->rssi)
				range.avg_qual.level = 186;	/* -70 dBm */
			else
				range.avg_qual.level = 176;	/* -80 dBm */
			range.avg_qual.noise = 0;
#endif /* WIRELESS_EXT > 11 */

			if (copy_to_user(wrq->u.data.pointer, &range, sizeof(struct iw_range)))
				rc = -EFAULT;
		}
		break;

	case SIOCGIWPOWER:
	{
		int mode = config.powerSaveMode;
		if ((wrq->u.power.disabled = (mode == POWERSAVE_CAM)))
			break;
		if ((wrq->u.power.flags & IW_POWER_TYPE) == IW_POWER_TIMEOUT) {
			wrq->u.power.value = (int)config.fastListenDelay * 1024;
			wrq->u.power.flags = IW_POWER_TIMEOUT;
		} else {
			wrq->u.power.value = (int)config.fastListenInterval * 1024;
			wrq->u.power.flags = IW_POWER_PERIOD;
		}
		if ((config.rmode & 0xFF) == RXMODE_ADDR)
			wrq->u.power.flags |= IW_POWER_UNICAST_R;
		else
			wrq->u.power.flags |= IW_POWER_ALL_R;
	}
	break;

	case SIOCSIWPOWER:
		if (wrq->u.power.disabled) {
			if ((config.rmode & 0xFF) >= RXMODE_RFMON) {
				rc = -EINVAL;
				break;
			}
			config.powerSaveMode = POWERSAVE_CAM;
			config.rmode &= 0xFF00;
			config.rmode |= RXMODE_BC_MC_ADDR;
			local->need_commit = 1;
			break;
		}
		if ((wrq->u.power.flags & IW_POWER_TYPE) == IW_POWER_TIMEOUT) {
			config.fastListenDelay = (wrq->u.power.value + 500) / 1024;
			config.powerSaveMode = POWERSAVE_PSPCAM;
			local->need_commit = 1;
		} else if ((wrq->u.power.flags & IW_POWER_TYPE) == IW_POWER_PERIOD) {
			config.fastListenInterval = config.listenInterval = (wrq->u.power.value + 500) / 1024;
			config.powerSaveMode = POWERSAVE_PSPCAM;
			local->need_commit = 1;
		}
		switch (wrq->u.power.flags & IW_POWER_MODE) {
		case IW_POWER_UNICAST_R:
			if ((config.rmode & 0xFF) >= RXMODE_RFMON) {
				rc = -EINVAL;
				break;
			}
			config.rmode &= 0xFF00;
			config.rmode |= RXMODE_ADDR;
			local->need_commit = 1;
			break;
		case IW_POWER_ALL_R:
			if ((config.rmode & 0xFF) >= RXMODE_RFMON) {
				rc = -EINVAL;
				break;
			}
			config.rmode &= 0xFF00;
			config.rmode |= RXMODE_BC_MC_ADDR;
			local->need_commit = 1;
		case IW_POWER_ON:
			break;
		default:
			rc = -EINVAL;
		}
		break;

	case SIOCGIWSENS:
		wrq->u.sens.value = config.rssiThreshold;
		wrq->u.sens.disabled = (wrq->u.sens.value == 0);
		wrq->u.sens.fixed = 1;
		break;

	case SIOCSIWSENS:
		config.rssiThreshold = wrq->u.sens.disabled ? RSSI_DEFAULT : wrq->u.sens.value;
		local->need_commit = 1;
		break;

	case SIOCGIWAPLIST:
		if (wrq->u.data.pointer) {
			int i, rc;
			struct sockaddr s[IW_MAX_AP];
			struct iw_quality qual[IW_MAX_AP];
			BSSListRid BSSList;
			int loseSync = capable(CAP_NET_ADMIN) ? 1: -1;
			for (i = 0; i < IW_MAX_AP; i++) {
				if (readBSSListRid(local, loseSync, &BSSList))
					break;
				loseSync = 0;
				memcpy(s[i].sa_data, BSSList.bssid, 6);
				s[i].sa_family = ARPHRD_ETHER;
				if (local->rssi)
					qual[i].level = 0x100 - local->rssi[BSSList.rssi].rssidBm;
				else
					qual[i].level = (BSSList.rssi + 321) / 2;
				qual[i].qual = qual[i].noise = 0;
				qual[i].updated = 2;
				if (BSSList.index == 0xffff) break;
			}
			if (!i) {
				for (i = 0;
				     i < min(IW_MAX_AP, 4) &&
					     (status_rid.bssid[i][0]
					      & status_rid.bssid[i][1]
					      & status_rid.bssid[i][2]
					      & status_rid.bssid[i][3]
					      & status_rid.bssid[i][4]
					      & status_rid.bssid[i][5])!=-1 &&
					     (status_rid.bssid[i][0]
					      | status_rid.bssid[i][1]
					      | status_rid.bssid[i][2]
					      | status_rid.bssid[i][3]
					      | status_rid.bssid[i][4]
					      | status_rid.bssid[i][5]);
				     i++) {
					memcpy(s[i].sa_data,
					       status_rid.bssid[i], 6);
					s[i].sa_family = ARPHRD_ETHER;
				}
			} else {
				wrq->u.data.flags = 1; /* Should be define'd */
				if (copy_to_user(wrq->u.data.pointer
						 + sizeof(struct sockaddr)*i,
						 &qual,
						 sizeof(struct iw_quality)*i))
					rc = -EFAULT;
			}
			wrq->u.data.length = i;
			if (copy_to_user(wrq->u.data.pointer, &s,
					 sizeof(struct sockaddr)*i))
				rc = -EFAULT;
		}
		break;

#ifdef WIRELESS_SPY
		// Set the spy list
	case SIOCSIWSPY:
		if (wrq->u.data.length > IW_MAX_SPY)
		{
			rc = -E2BIG;
			break;
		}
		local->spy_number = wrq->u.data.length;
		if (local->spy_number > 0)
		{
			struct sockaddr address[IW_MAX_SPY];
			int i;

			if (copy_from_user(address, wrq->u.data.pointer,
					   sizeof(struct sockaddr) * local->spy_number)) {
				rc = -EFAULT;
				break;
			}
			for (i=0; i<local->spy_number; i++)
				memcpy(local->spy_address[i], address[i].sa_data, 6);
			memset(local->spy_stat, 0, sizeof(struct iw_quality) * IW_MAX_SPY);
		}
		break;

		// Get the spy list
	case SIOCGIWSPY:
		wrq->u.data.length = local->spy_number;
		if ((local->spy_number > 0) && (wrq->u.data.pointer))
		{
			struct sockaddr address[IW_MAX_SPY];
			int i;
			rc = verify_area(VERIFY_WRITE, wrq->u.data.pointer, (sizeof(struct iw_quality)+sizeof(struct sockaddr)) * IW_MAX_SPY);
			if (rc)
				break;
			for (i=0; i<local->spy_number; i++)
			{
				memcpy(address[i].sa_data, local->spy_address[i], 6);
				address[i].sa_family = AF_UNIX;
			}
			if (copy_to_user(wrq->u.data.pointer, address, sizeof(struct sockaddr) * local->spy_number)) {
				rc = -EFAULT;
				break;
			}
			if (copy_to_user(wrq->u.data.pointer + (sizeof(struct sockaddr)*local->spy_number), local->spy_stat, sizeof(struct iw_quality) * local->spy_number)) {
				rc = -EFAULT;
				break;
			}
			for (i=0; i<local->spy_number; i++)
				local->spy_stat[i].updated = 0;
		}
		break;
#endif /* WIRELESS_SPY */

#ifdef CISCO_EXT
	case SIOCGIWPRIV:
		if(wrq->u.data.pointer)
		{
			struct iw_priv_args   priv[] =
			{ /* cmd, set_args, get_args, name */
				{ AIROIOCTL, IW_PRIV_TYPE_BYTE | IW_PRIV_SIZE_FIXED | sizeof (aironet_ioctl), IW_PRIV_TYPE_BYTE | 2047, "airoioctl" },
				{ AIROIDIFC, IW_PRIV_TYPE_BYTE | IW_PRIV_SIZE_FIXED | sizeof (aironet_ioctl), IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, "airoidifc" },
			};

			/* Set the number of ioctl available */
			wrq->u.data.length = 2;

			/* Copy structure to the user buffer */
			if(copy_to_user(wrq->u.data.pointer, (u_char *) priv,
					sizeof(priv)))
				rc = -EFAULT;
		}
		break;
#endif /* CISCO_EXT */
#endif /* WIRELESS_EXT */

#ifdef CISCO_EXT
	case AIROIDIFC:
#ifdef AIROOLDIDIFC
	case AIROOLDIDIFC:
#endif
	{
		int val = AIROMAGIC;
		aironet_ioctl com;
		if (copy_from_user(&com,rq->ifr_data,sizeof(com)))
			rc = -EFAULT;
		else if (copy_to_user(com.data,(char *)&val,sizeof(val)))
			rc = -EFAULT;
	}
	break;

	case AIROIOCTL:
#ifdef AIROOLDIOCTL
	case AIROOLDIOCTL:
#endif
		/* Get the command struct and hand it off for evaluation by
		 * the proper subfunction
		 */
	{
		aironet_ioctl com;
		if (copy_from_user(&com,rq->ifr_data,sizeof(com))) {
			rc = -EFAULT;
			break;
		}

		/* Seperate R/W functions bracket legality here
		 */
		if ( com.command <= AIROGSTATSD32 )
			rc = readrids(dev,&com);
		else if ( com.command >= AIROPCAP && com.command <= AIROPLEAPUSR )
			rc = writerids(dev,&com);
		else if ( com.command >= AIROFLSHRST && com.command <= AIRORESTART )
			rc = flashcard(dev,&com);
		else
			rc = -EINVAL;      /* Bad command in ioctl */
	}
	break;
#endif /* CISCO_EXT */

	// All other calls are currently unsupported
	default:
		rc = -EOPNOTSUPP;
	}

#ifdef WIRELESS_EXT
	/* Some of the "SET" function may have modified some of the
	 * parameters. It's now time to commit them in the card */
	if(local->need_commit) {
		/* A classical optimisation here is to not commit any change
		 * if the card is not "opened". This is what we do in
		 * wvlan_cs (see for details).
		 * For that, we would need to have the config RID saved in
		 * the airo_info struct and make sure to not re-read it if
		 * local->need_commit != 0. Then, you need to patch "open"
		 * to do the final commit of all parameters...
		 * Jean II */
		Resp rsp;

		disable_MAC(local);
		local->config = config;	/* ???? config is local !!! */
		checkThrottle(&config);
		writeConfigRid(local, &config);
		enable_MAC(local, &rsp);

		local->need_commit = 0;
	}
#endif /* WIRELESS_EXT */

	return(rc);
}

#ifdef WIRELESS_EXT
/*
 * Get the Wireless stats out of the driver
 * Note : irq and spinlock protection will occur in the subroutines
 *
 * TODO :
 *	o Check if work in Ad-Hoc mode (otherwise, use SPY, as in wvlan_cs)
 *	o Find the noise level
 *
 * Jean
 */
struct iw_statistics *airo_get_wireless_stats(struct net_device *dev)
{
	struct airo_info *local = (struct airo_info*) dev->priv;
	StatusRid status_rid;
	StatsRid stats_rid;
	u32 *vals = stats_rid.vals;

	/* Get stats out of the card */
	readStatusRid(local, &status_rid);
	readStatsRid(local, &stats_rid, RID_STATS);

	/* The status */
	local->wstats.status = status_rid.mode;

	/* Signal quality and co. But where is the noise level ??? */
	local->wstats.qual.qual = status_rid.signalQuality;
	if (local->rssi)
		local->wstats.qual.level = 0x100 - local->rssi[status_rid.sigQuality].rssidBm;
	else
		local->wstats.qual.level = (status_rid.normalizedSignalStrength + 321) / 2;
	local->wstats.qual.noise = 0;
	local->wstats.qual.updated = 3;

	/* Packets discarded in the wireless adapter due to wireless
	 * specific problems */
	local->wstats.discard.nwid = vals[56] + vals[57] + vals[58];/* SSID Mismatch */
	local->wstats.discard.code = vals[6];/* RxWepErr */
#if WIRELESS_EXT > 11
	local->wstats.discard.fragment = vals[30];
	local->wstats.discard.retries = vals[10];
	local->wstats.discard.misc = vals[1] + vals[32];
	local->wstats.miss.beacon = vals[34];
#else /* WIRELESS_EXT > 11 */
	local->wstats.discard.misc = vals[1] + vals[30] + vals[32];
#endif /* WIRELESS_EXT > 11 */
	return (&local->wstats);
}
#endif /* WIRELESS_EXT */

#ifdef CISCO_EXT
/*
 * This just translates from driver IOCTL codes to the command codes to
 * feed to the radio's host interface. Things can be added/deleted
 * as needed.  This represents the READ side of control I/O to
 * the card
 */
static int readrids(struct net_device *dev, aironet_ioctl *comp) {
	unsigned short ridcode;
	unsigned char iobuf[2048];

	switch(comp->command)
	{
	case AIROGCAP:      ridcode = RID_CAPABILITIES; break;
	case AIROGCFG:      ridcode = RID_CONFIG;       break;
	case AIROGSLIST:    ridcode = RID_SSID;         break;
	case AIROGVLIST:    ridcode = RID_APLIST;       break;
	case AIROGDRVNAM:   ridcode = RID_DRVNAME;      break;
	case AIROGEHTENC:   ridcode = RID_ETHERENCAP;   break;
	case AIROGWEPKTMP:  ridcode = RID_WEP_TEMP;
		/* Only super-user can read WEP keys */
		if (!capable(CAP_NET_ADMIN))
			return -EPERM;
		break;
	case AIROGWEPKNV:   ridcode = RID_WEP_PERM;
		/* Only super-user can read WEP keys */
		if (!capable(CAP_NET_ADMIN))
			return -EPERM;
		break;
	case AIROGSTAT:     ridcode = RID_STATUS;       break;
	case AIROGSTATSD32: ridcode = RID_STATSDELTA;   break;
	case AIROGSTATSC32: ridcode = RID_STATS;        break;
	default:
		return -EINVAL;
		break;
	}

	PC4500_readrid((struct airo_info *)dev->priv,ridcode,iobuf,sizeof(iobuf));
	/* get the count of bytes in the rid  docs say 1st 2 bytes is it.
	 * then return it to the user
	 * 9/22/2000 Honor user given length
	 */

	if (copy_to_user(comp->data, iobuf,
			 min((int)comp->len, (int)sizeof(iobuf))))
		return -EFAULT;
	return 0;
}

/*
 * Danger Will Robinson write the rids here
 */

static int writerids(struct net_device *dev, aironet_ioctl *comp) {
	int  ridcode;
	Resp      rsp;
	static int (* writer)(struct airo_info *, u16 rid, const void *, int);
	unsigned char iobuf[2048];

	/* Only super-user can write RIDs */
	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	ridcode = 0;
	writer = do_writerid;

	switch(comp->command)
	{
	case AIROPSIDS:     ridcode = RID_SSID;         break;
	case AIROPCAP:      ridcode = RID_CAPABILITIES; break;
	case AIROPAPLIST:   ridcode = RID_APLIST;       break;
	case AIROPCFG:      ridcode = RID_CONFIG;       break;
	case AIROPWEPKEYNV: ridcode = RID_WEP_PERM;     break;
	case AIROPLEAPUSR:  ridcode = RID_LEAPUSERNAME; break;
	case AIROPLEAPPWD:  ridcode = RID_LEAPPASSWORD; break;
	case AIROPWEPKEY:   ridcode = RID_WEP_TEMP; writer = PC4500_writerid;
		break;

		/* this is not really a rid but a command given to the card
		 * same with MAC off
		 */
	case AIROPMACON:
		if (enable_MAC(dev->priv, &rsp) != 0)
			return -EIO;
		return 0;

		/*
		 * Evidently this code in the airo driver does not get a symbol
		 * as disable_MAC. it's probably so short the compiler does not gen one.
		 */
	case AIROPMACOFF:
		disable_MAC(dev->priv);
		return 0;

		/* This command merely clears the counts does not actually store any data
		 * only reads rid. But as it changes the cards state, I put it in the
		 * writerid routines.
		 */
	case AIROPSTCLR:
		ridcode = RID_STATSDELTACLEAR;

		PC4500_readrid(dev->priv,ridcode,iobuf,sizeof(iobuf));

		if (copy_to_user(comp->data, iobuf,
				 min((int)comp->len, (int)sizeof(iobuf))))
			return -EFAULT;
		return 0;

	default:
		return -EOPNOTSUPP;	/* Blarg! */
	}
	if(comp->len > sizeof(iobuf))
		return -EINVAL;

	if (copy_from_user(iobuf,comp->data,comp->len))
		return -EFAULT;
	if((*writer)((struct airo_info *)dev->priv, ridcode, iobuf,comp->len))
		return -EIO;
	return 0;
}

/*****************************************************************************
 * Ancillary flash / mod functions much black magic lurkes here              *
 *****************************************************************************
 */

/*
 * Flash command switch table
 */

int flashcard(struct net_device *dev, aironet_ioctl *comp) {
	int z;
	int cmdreset(struct airo_info *);
	int setflashmode(struct airo_info *);
	int flashgchar(struct airo_info *,int,int);
	int flashpchar(struct airo_info *,int,int);
	int flashputbuf(struct airo_info *);
	int flashrestart(struct airo_info *,struct net_device *);

	/* Only super-user can modify flash */
	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	switch(comp->command)
	{
	case AIROFLSHRST:
		return cmdreset((struct airo_info *)dev->priv);

	case AIROFLSHSTFL:
		if (!((struct airo_info *)dev->priv)->flash &&
			(((struct airo_info *)dev->priv)->flash = kmalloc (FLASHSIZE, GFP_KERNEL)) == NULL)
			return -ENOMEM;
		return setflashmode((struct airo_info *)dev->priv);

	case AIROFLSHGCHR: /* Get char from aux */
		if(comp->len != sizeof(int))
			return -EINVAL;
		if (copy_from_user(&z,comp->data,comp->len))
			return -EFAULT;
		return flashgchar((struct airo_info *)dev->priv,z,8000);

	case AIROFLSHPCHR: /* Send char to card. */
		if(comp->len != sizeof(int))
			return -EINVAL;
		if (copy_from_user(&z,comp->data,comp->len))
			return -EFAULT;
		return flashpchar((struct airo_info *)dev->priv,z,8000);

	case AIROFLPUTBUF: /* Send 32k to card */
		if (!((struct airo_info *)dev->priv)->flash)
			return -ENOMEM;
		if(comp->len > FLASHSIZE)
			return -EINVAL;
		if(copy_from_user(((struct airo_info *)dev->priv)->flash,comp->data,comp->len))
			return -EFAULT;

		flashputbuf((struct airo_info *)dev->priv);
		return 0;

	case AIRORESTART:
		if(flashrestart((struct airo_info *)dev->priv,dev))
			return -EIO;
		return 0;
	}
	return -EINVAL;
}

#define FLASH_COMMAND  0x7e7e

/*
 * STEP 1)
 * Disable MAC and do soft reset on
 * card.
 */

int cmdreset(struct airo_info *ai) {
	disable_MAC(ai);

	if(!waitbusy (ai)){
		printk(KERN_INFO "Waitbusy hang before RESET\n");
		return -EBUSY;
	}

	OUT4500(ai,COMMAND,CMD_SOFTRESET);

	set_current_state (TASK_UNINTERRUPTIBLE);
	schedule_timeout (HZ);          /* WAS 600 12/7/00 */

	if(!waitbusy (ai)){
		printk(KERN_INFO "Waitbusy hang AFTER RESET\n");
		return -EBUSY;
	}
	return 0;
}

/* STEP 2)
 * Put the card in legendary flash
 * mode
 */

int setflashmode (struct airo_info *ai) {
	OUT4500(ai, SWS0, FLASH_COMMAND);
	OUT4500(ai, SWS1, FLASH_COMMAND);
	OUT4500(ai, SWS0, FLASH_COMMAND);
	OUT4500(ai, COMMAND,0x10);
	set_current_state (TASK_UNINTERRUPTIBLE);
	schedule_timeout (HZ/2); /* 500ms delay */

	if(!waitbusy(ai)) {
		printk(KERN_INFO "Waitbusy hang after setflash mode\n");
		return -EIO;
	}
	return 0;
}

/* Put character to SWS0 wait for dwelltime
 * x 50us for  echo .
 */

int flashpchar(struct airo_info *ai,int byte,int dwelltime) {
	int echo;
	int waittime;

	byte |= 0x8000;

	if(dwelltime == 0 )
		dwelltime = 200;

	waittime=dwelltime;

	/* Wait for busy bit d15 to go false indicating buffer empty */
	while ((IN4500 (ai, SWS0) & 0x8000) && waittime > 0) {
		udelay (50);
		waittime -= 50;
	}

	/* timeout for busy clear wait */
	if(waittime <= 0 ){
		printk(KERN_INFO "flash putchar busywait timeout! \n");
		return -EBUSY;
	}

	/* Port is clear now write byte and wait for it to echo back */
	do {
		OUT4500(ai,SWS0,byte);
		udelay(50);
		dwelltime -= 50;
		echo = IN4500(ai,SWS1);
	} while (dwelltime >= 0 && echo != byte);

	OUT4500(ai,SWS1,0);

	return (echo == byte) ? 0 : -EIO;
}

/*
 * Get a character from the card matching matchbyte
 * Step 3)
 */
int flashgchar(struct airo_info *ai,int matchbyte,int dwelltime){
	int           rchar;
	unsigned char rbyte=0;

	do {
		rchar = IN4500(ai,SWS1);

		if(dwelltime && !(0x8000 & rchar)){
			dwelltime -= 10;
			mdelay(10);
			continue;
		}
		rbyte = 0xff & rchar;

		if( (rbyte == matchbyte) && (0x8000 & rchar) ){
			OUT4500(ai,SWS1,0);
			return 0;
		}
		if( rbyte == 0x81 || rbyte == 0x82 || rbyte == 0x83 || rbyte == 0x1a || 0xffff == rchar)
			break;
		OUT4500(ai,SWS1,0);

	}while(dwelltime > 0);
	return -EIO;
}

/*
 * Transfer 32k of firmware data from user buffer to our buffer and
 * send to the card
 */

int flashputbuf(struct airo_info *ai){
	int            nwords;

	/* Write stuff */
	OUT4500(ai,AUXPAGE,0x100);
	OUT4500(ai,AUXOFF,0);

	for(nwords=0;nwords != FLASHSIZE / 2;nwords++){
		OUT4500(ai,AUXDATA,ai->flash[nwords] & 0xffff);
	}

	OUT4500(ai,SWS0,0x8000);

	return 0;
}

/*
 *
 */
int flashrestart(struct airo_info *ai,struct net_device *dev){
	int    i,status;

	set_current_state (TASK_UNINTERRUPTIBLE);
	schedule_timeout (HZ);          /* Added 12/7/00 */
	status = setup_card(ai, dev->dev_addr,&((struct airo_info*)dev->priv)->config);

	for( i = 0; i < MAX_FIDS; i++ ) {
		ai->fids[i] = transmit_allocate( ai, 2312 );
	}

	set_current_state (TASK_UNINTERRUPTIBLE);
	schedule_timeout (HZ);          /* Added 12/7/00 */
	return status;
}
#endif /* CISCO_EXT */

/*
    This program is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License
    as published by the Free Software Foundation; either version 2
    of the License, or (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    In addition:

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions
    are met:

    1. Redistributions of source code must retain the above copyright
       notice, this list of conditions and the following disclaimer.
    2. Redistributions in binary form must reproduce the above copyright
       notice, this list of conditions and the following disclaimer in the
       documentation and/or other materials provided with the distribution.
    3. The name of the author may not be used to endorse or promote
       products derived from this software without specific prior written
       permission.

    THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
    IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
    WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
    ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
    INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
    (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
    SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
    HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
    STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
    IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
    POSSIBILITY OF SUCH DAMAGE.
*/

module_init(airo_init_module);
module_exit(airo_cleanup_module);
