/*
 * lsi_defs.h	: Information common to all LSI's intelligent controllers
 */

#ifndef LSI_COMMON_DEFS
#define LSI_COMMON_DEFS

/*
 * Definitions common for all LSI modules
 */

#define MAX_CDB_LEN	  		   	10
#define MAX_EXT_CDB_LEN				16
#define MAX_REQ_SENSE_LEN			0x20

#define	LSI_MAX_LOGICAL_DRIVES_64LD		(64+1)
#define LSI_MAX_PHYSICAL_DEVICES		60
#define LSI_MAX_CHANNELS			16
#define LSI_MAX_DEVICES_PER_CHANNEL		16

#define ADDR_LO(addr)	(((unsigned long)(addr)) & 0xffffffff)
#define ADDR_HI(addr)	((((ulong)(addr)) & 0xffffffff00000000ULL) >> 32)
#define ADDR_64(hi, lo)	((((uint64_t)(hi)) << 32) | (lo))

typedef enum { MRAID_FALSE, MRAID_TRUE } bool_t;
typedef enum { MRAID_SUCCESS, MRAID_FAILURE, MRAID_BUSY } status_t;

/**
 * con_log() - console log routine
 * @param level		: indicates the severity of the message.
 * @fparam mt		: format string
 *
 * con_log displays the error messages on the console based on the current
 * debug level. Also it attaches the appropriate kernel severity level with
 * the message.
 *
 *
 * consolge messages debug levels
 */
#define	CL_ANN		0	/* print unconditionally, announcements */
#define CL_DLEVEL1	1	/* debug level 1, informative */
#define CL_DLEVEL2	2	/* debug level 2, verbose */
#define CL_DLEVEL3	3	/* debug level 3, very verbose */

#define	con_log(level, fmt) if (LSI_DBGLVL >= level) printk fmt;

/*
 * Definitions & Declarations needed to use common management module
 */

#define MEGAIOC_MAGIC		'm'
#define MEGAIOCCMD		_IOWR(MEGAIOC_MAGIC, 0, mimd_t)

#define MEGAIOC_QNADAP		'm'	/* Query # of adapters		*/
#define MEGAIOC_QDRVRVER	'e'	/* Query driver version		*/
#define MEGAIOC_QADAPINFO   	'g'	/* Query adapter information	*/

#define USCSICMD		0x80
#define VENDOR_SPECIFIC_CMDS	0xE0
#define MEGA_INTERNAL_CMD	(VENDOR_SPECIFIC_CMDS + 0x01)

#define UIOC_RD			0x00001
#define UIOC_WR			0x00002

#define MBOX_CMD		0x00000	
#define GET_DRIVER_VER		0x10000
#define GET_N_ADAP		0x20000
#define GET_ADAP_INFO		0x30000
#define GET_CAP			0x40000
#define GET_STATS		0x50000
#define GET_IOCTL_VERSION	0x01

#define MAX_LSI_CMN_ADAPS	16

#define EXT_IOCTL_SIGN_SZ	16
#define EXT_IOCTL_SIGN		"$$_EXTD_IOCTL_$$"

#define	MBOX_LEGACY		0x00		/* ioctl has legacy mbox*/
#define MBOX_HPE		0x01		/* ioctl has hpe mbox	*/

#define	APPTYPE_MIMD		0x00		/* old existing apps	*/
#define APPTYPE_UIOC		0x01		/* new apps using uioc	*/

#define IOCTL_ISSUE		0x00000001	/* Issue ioctl		*/
#define IOCTL_ABORT		0x00000002	/* Abort previous ioctl	*/

#define DRVRTYPE_MBOX		0x00000001	/* regular mbox driver	*/
#define DRVRTYPE_HPE		0x00000002	/* new hpe driver	*/

#define LC_SUCCESS		0x00000000	/* Generic success code	*/
#define	LC_EFULL		0x00000001	/* Exceeded max capacity*/
#define LC_EINVAL		0x00000002	/* Invalid argument	*/
#define LC_EEXISTS		0x00000004	/* Already exists	*/
#define LC_ENOADP		0x00000008	/* Adp not found	*/
#define LC_ENOTSUPP		0x00000010	/* Op not supported	*/
#define LC_ETIME		0x00000020	/* Op timedout		*/
#define LC_PENDING		0x00000040	/* ioctl pending @ lld	*/
#define LC_UNKNOWN		0x00000080	/* Unknown err frm lld	*/
#define LC_ENOMEM		0x00000100	/* Mem alloc failed	*/

#define MKADAP(adapno)	(MEGAIOC_MAGIC << 8 | (adapno) )
#define GETADAP(mkadap)	((mkadap) ^ MEGAIOC_MAGIC << 8)

/**
 * struct uioc_t - the common ioctl packet structure
 *
 * @signature	: Must be "$$_EXTD_IOCTL_$$"
 * @mb_type	: Type of the mail box (MB_LEGACY or MB_HPE)
 * @app_type	: Type of the issuing application (existing or new)
 * @opcode	: Opcode of the command
 * @adapno	: Adapter number
 * @cmdbuf	: Pointer to buffer - can point to mbox or plain data buffer
 * @xferlen	: xferlen for DCMD and non mailbox commands
 * @data_dir	: Direction of the data transfer
 * @status	: Status from the driver
 *
 * Note		: All LSI drivers understand only this packet. Any other
 *		: format sent by applications would be converted to this.
 *		: All addresses are 64 bits (even on 32-bit platforms).
 */

typedef void(*DONE)(void);

#define EXT_IOCTL_PACKET_SZ		128
#define IOC_VALID_FIELDS_SZ		(46+sizeof(DONE))

/* 
 * FIXME: reserve 32 bytes for driver at the bottom
 */
typedef struct uioc {

/* User Apps: */
	
	uint8_t		signature[EXT_IOCTL_SIGN_SZ];
	uint8_t		mb_type;
	uint8_t		app_type;
	uint32_t	opcode;
	uint32_t	adapno;
	uint64_t	cmdbuf;
	uint32_t	xferlen;
	uint32_t	data_dir;
	uint32_t	status;

/* Driver Data: */	
	void (*done)(struct uioc*);

	uint8_t		reserved[EXT_IOCTL_PACKET_SZ - IOC_VALID_FIELDS_SZ];

} __attribute__ ((packed)) uioc_t;

/**
 * struct mraid_hba_info - information about the controller
 *
 * @param pci_vendor_id		: PCI vendor id
 * @param pci_device_id		: PCI device id
 * @param subsystem_vendor_id	: PCI subsystem vendor id
 * @param subsystem_device_id	: PCI subsystem device id
 * @param baseport		: base port of hba memory
 * @param pci_bus		: PCI bus
 * @param pci_dev_fn		: PCI device/function values
 * @param irq			: interrupt vector for the device
 * @param reserved1		: filler to align a even boundary
 * @param reserved		: reserved space
 *
 * Extended information of 256 bytes about the controller. Align on the single
 * byte boundary so that 32-bit applications can be run on 64-bit platform
 * drivers withoug re-compilation.
 * NOTE: reduce the number of reserved bytes whenever new field are added, so
 * that total size of the structure remains 256 bytes.
 */
typedef struct mraid_hba_info {

	uint16_t	pci_vendor_id;
	uint16_t	pci_device_id;
	uint16_t	subsys_vendor_id;
	uint16_t	subsys_device_id;

	uint64_t	baseport;
	uint8_t		pci_bus;
	uint8_t		pci_dev_fn;
	uint8_t		pci_slot;
	uint8_t		irq;

	uint8_t		reserved1;
	uint32_t	unique_id;
	uint32_t	host_no;

	uint8_t		num_ldrv;

	uint8_t		reserved[256 - 30];

} __attribute__ ((packed)) mraid_hba_info_t;

typedef struct mcontroller {

	uint64_t	base;
	uint8_t		irq;
	uint8_t		numldrv;
	uint8_t		pcibus;
	uint16_t	pcidev;
	uint8_t		pcifun;
	uint16_t	pciid;
	uint16_t	pcivendor;
	uint8_t		pcislot;
	uint32_t	uid;

} __attribute__ ((packed)) mcontroller_t;

/**
 * mraid_mmadp_t: Structure that drivers pass during (un)registration
 *
 * @unique_id	: Any unique id (usually PCI bus+dev+fn)
 * @drvr_type	: megaraid or hpe (DRVRTYPE_MBOX or DRVRTYPE_HPE)
 * @drv_data	: Driver specific; not touched by the common module
 * @dev		: pci dev; used for allocating dma'ble memory
 * @issue_uioc	: Driver supplied routine to issue uioc_t commands
 *		: issue_uioc(drvr_data, kioc, ISSUE/ABORT, uioc_done)
 */

typedef struct mraid_mmadp {

/* Filled by driver */

	uint32_t		unique_id;
	uint32_t		drvr_type;
	ulong			drvr_data;
	uint8_t			timeout;

	struct pci_dev*		pdev;

	int(*issue_uioc)(ulong, uioc_t*, uint32_t);

/* Maintained by common module */

	uioc_t			kioc;
	struct semaphore	kioc_mtx;

	caddr_t			memblk;
	dma_addr_t		memblk_dmah;

	caddr_t			int_data;
	uint32_t		int_data_len;
	dma_addr_t		int_data_dmah;
	caddr_t			int_data_user;

	caddr_t			int_pthru;
	uint32_t		int_pthru_len;
	dma_addr_t		int_pthru_dmah;
	caddr_t			int_pthru_user;

} mraid_mmadp_t;

uint32_t mraid_mm_register_adp( mraid_mmadp_t* );
uint32_t mraid_mm_unregister_adp( uint32_t );

#endif /*LSI_COMMON_DEFS*/
