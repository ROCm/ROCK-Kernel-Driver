/*
 *  linux/drivers/message/fusion/mptioctl.h
 *      Fusion MPT misc device (ioctl) driver.
 *      For use with PCI chip/adapter(s):
 *          LSIFC9xx/LSI409xx Fibre Channel
 *      running LSI Logic Fusion MPT (Message Passing Technology) firmware.
 *
 *  Credits:
 *      This driver would not exist if not for Alan Cox's development
 *      of the linux i2o driver.
 *
 *      A huge debt of gratitude is owed to David S. Miller (DaveM)
 *      for fixing much of the stupid and broken stuff in the early
 *      driver while porting to sparc64 platform.  THANK YOU!
 *
 *      (see also mptbase.c)
 *
 *  Copyright (c) 1999-2002 LSI Logic Corporation
 *  Originally By: Steven J. Ralston
 *  (mailto:sjralston1@netscape.net)
 *  (mailto:Pam.Delaney@lsil.com)
 *
 *  $Id: mptctl.h,v 1.10 2002/05/28 15:57:16 pdelaney Exp $
 */
/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*
    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; version 2 of the License.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    NO WARRANTY
    THE PROGRAM IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OR
    CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED INCLUDING, WITHOUT
    LIMITATION, ANY WARRANTIES OR CONDITIONS OF TITLE, NON-INFRINGEMENT,
    MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE. Each Recipient is
    solely responsible for determining the appropriateness of using and
    distributing the Program and assumes all risks associated with its
    exercise of rights under this Agreement, including but not limited to
    the risks and costs of program errors, damage to or loss of data,
    programs or equipment, and unavailability or interruption of operations.

    DISCLAIMER OF LIABILITY
    NEITHER RECIPIENT NOR ANY CONTRIBUTORS SHALL HAVE ANY LIABILITY FOR ANY
    DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
    DAMAGES (INCLUDING WITHOUT LIMITATION LOST PROFITS), HOWEVER CAUSED AND
    ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
    TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
    USE OR DISTRIBUTION OF THE PROGRAM OR THE EXERCISE OF ANY RIGHTS GRANTED
    HEREUNDER, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGES

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#ifndef MPTCTL_H_INCLUDED
#define MPTCTL_H_INCLUDED
/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/

#include "linux/version.h"


/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
/*
 *
 */
#define MPT_MISCDEV_BASENAME            "mptctl"
#define MPT_MISCDEV_PATHNAME            "/dev/" MPT_MISCDEV_BASENAME

#define MPT_PRODUCT_LENGTH              12

/*
 *  Generic MPT Control IOCTLs and structures
 */
#define MPT_MAGIC_NUMBER	'm'

#define MPTRWPERF		_IOWR(MPT_MAGIC_NUMBER,0,struct mpt_raw_r_w)

#define MPTFWDOWNLOAD		_IOWR(MPT_MAGIC_NUMBER,15,struct mpt_fw_xfer)
#define MPTCOMMAND		_IOWR(MPT_MAGIC_NUMBER,20,struct mpt_ioctl_command)

#if defined(__KERNEL__) && defined(__sparc__) && defined(__sparc_v9__)		/*{*/
#define MPTFWDOWNLOAD32		_IOWR(MPT_MAGIC_NUMBER,15,struct mpt_fw_xfer32)
#define MPTCOMMAND32		_IOWR(MPT_MAGIC_NUMBER,20,struct mpt_ioctl_command32)
#endif	/*}*/

#define MPTIOCINFO		_IOWR(MPT_MAGIC_NUMBER,17,struct mpt_ioctl_iocinfo)
#define MPTTARGETINFO		_IOWR(MPT_MAGIC_NUMBER,18,struct mpt_ioctl_targetinfo)
#define MPTTEST			_IOWR(MPT_MAGIC_NUMBER,19,struct mpt_ioctl_test)
#define MPTEVENTQUERY		_IOWR(MPT_MAGIC_NUMBER,21,struct mpt_ioctl_eventquery)
#define MPTEVENTENABLE		_IOWR(MPT_MAGIC_NUMBER,22,struct mpt_ioctl_eventenable)
#define MPTEVENTREPORT		_IOWR(MPT_MAGIC_NUMBER,23,struct mpt_ioctl_eventreport)
#define MPTHARDRESET		_IOWR(MPT_MAGIC_NUMBER,24,struct mpt_ioctl_diag_reset)
#define MPTFWREPLACE		_IOWR(MPT_MAGIC_NUMBER,25,struct mpt_ioctl_replace_fw)

/*
 * SPARC PLATFORM REMARK:
 * IOCTL data structures that contain pointers
 * will have different sizes in the driver and applications
 * (as the app. will not use 8-byte pointers).
 * Apps should use MPTFWDOWNLOAD and MPTCOMMAND.
 * The driver will convert data from
 * mpt_fw_xfer32 (mpt_ioctl_command32) to mpt_fw_xfer (mpt_ioctl_command)
 * internally.
 */
struct mpt_fw_xfer {
	unsigned int	 iocnum;	/* IOC unit number */
	unsigned int	 fwlen;
	void		*bufp;		/* Pointer to firmware buffer */
};

#if defined(__KERNEL__) && defined(__sparc__) && defined(__sparc_v9__)		/*{*/
struct mpt_fw_xfer32 {
	unsigned int iocnum;
	unsigned int fwlen;
	u32 bufp;
};
#endif	/*}*/

/*
 *  IOCTL header structure.
 *  iocnum - must be defined.
 *  port - must be defined for all IOCTL commands other than MPTIOCINFO
 *  maxDataSize - ignored on MPTCOMMAND commands
 *		- ignored on MPTFWREPLACE commands
 *		- on query commands, reports the maximum number of bytes to be returned
 *		  to the host driver (count includes the header).
 *		  That is, set to sizeof(struct mpt_ioctl_iocinfo) for fixed sized commands.
 *		  Set to sizeof(struct mpt_ioctl_targetinfo) + datasize for variable
 *			sized commands. (MPTTARGETINFO, MPTEVENTREPORT)
 */
typedef struct _mpt_ioctl_header {
	unsigned int	 iocnum;	/* IOC unit number */
	unsigned int	 port;		/* IOC port number */
	int		 maxDataSize;	/* Maximum Num. bytes to transfer on read */
} mpt_ioctl_header;

/*
 * Issue a diagnostic reset
 */
struct mpt_ioctl_diag_reset {
	mpt_ioctl_header hdr;
};


/*
 *  PCI bus/device/function information structure.
 */
struct mpt_ioctl_pci_info {
	union {
		struct {
			unsigned long  deviceNumber   :  5;
			unsigned long  functionNumber :  3;
			unsigned long  busNumber      : 24;
		} bits;
		unsigned long  asUlong;
	} u;
};

/*
 *  Adapter Information Page
 *  Read only.
 *  Data starts at offset 0xC
 */
#define MPT_IOCTL_INTERFACE_FC		(0x01)
#define MPT_IOCTL_INTERFACE_SCSI	(0x00)
#define MPT_IOCTL_VERSION_LENGTH	(32)

struct mpt_ioctl_iocinfo {
	mpt_ioctl_header hdr;
	int		 adapterType;	/* SCSI or FCP */
	int		 port;		/* port number */
	int		 pciId;		/* PCI Id. */
	int		 hwRev;		/* hardware revision */
	int		 subSystemDevice;	/* PCI subsystem Device ID */
	int		 subSystemVendor;	/* PCI subsystem Vendor ID */
	int		 numDevices;		/* number of devices */
	int		 FWVersion;		/* FW Version (integer) */
	int		 BIOSVersion;		/* BIOS Version (integer) */
	char		 driverVersion[MPT_IOCTL_VERSION_LENGTH];	/* Driver Version (string) */
	char		 busChangeEvent;
	char		 hostId;
	char		 rsvd[2];
	struct mpt_ioctl_pci_info  pciInfo; /* Added Rev 1 */
};

/*
 * Device Information Page
 * Report the number of, and ids of, all targets
 * on this IOC.  The ids array is a packed structure
 * of the known targetInfo.
 * bits 31-24: reserved
 *      23-16: LUN
 *      15- 8: Bus Number
 *       7- 0: Target ID
 */
struct mpt_ioctl_targetinfo {
	mpt_ioctl_header hdr;
	int		 numDevices;	/* Num targets on this ioc */
	int		 targetInfo[1];
};


/*
 * Event reporting IOCTL's.  These IOCTL's will
 * use the following defines:
 */
struct mpt_ioctl_eventquery {
	mpt_ioctl_header hdr;
	unsigned short	 eventEntries;
	unsigned short	 reserved;
	unsigned int	 eventTypes;
};

struct mpt_ioctl_eventenable {
	mpt_ioctl_header hdr;
	unsigned int	 eventTypes;
};

#ifndef __KERNEL__
typedef struct {
	uint	event;
	uint	eventContext;
	uint	data[2];
} MPT_IOCTL_EVENTS;
#endif

struct mpt_ioctl_eventreport {
	mpt_ioctl_header	hdr;
	MPT_IOCTL_EVENTS	eventData[1];
};

#define MPT_MAX_NAME	32
struct mpt_ioctl_test {
	mpt_ioctl_header hdr;
	u8		 name[MPT_MAX_NAME];
	int		 chip_type;
	u8		 product [MPT_PRODUCT_LENGTH];
};

/* Replace the FW image cached in host driver memory
 * newImageSize - image size in bytes
 * newImage - first byte of the new image
 */
typedef struct mpt_ioctl_replace_fw {
	mpt_ioctl_header hdr;
	int		 newImageSize;
	u8		 newImage[1];
} mpt_ioctl_replace_fw_t;

/* General MPT Pass through data strucutre
 *
 * iocnum
 * timeout - in seconds, command timeout. If 0, set by driver to
 *		default value.
 * replyFrameBufPtr - reply location
 * dataInBufPtr - destination for read
 * dataOutBufPtr - data source for write
 * senseDataPtr - sense data location
 * maxReplyBytes - maximum number of reply bytes to be sent to app.
 * dataInSize - num bytes for data transfer in (read)
 * dataOutSize - num bytes for data transfer out (write)
 * dataSgeOffset - offset in words from the start of the request message
 *		to the first SGL
 * MF[1];
 *
 * Remark:  Some config pages have bi-directional transfer,
 * both a read and a write. The basic structure allows for
 * a bidirectional set up. Normal messages will have one or
 * both of these buffers NULL.
 */
struct mpt_ioctl_command {
	mpt_ioctl_header hdr;
	int		timeout;	/* optional (seconds) */
	char		*replyFrameBufPtr;
	char		*dataInBufPtr;
	char		*dataOutBufPtr;
	char		*senseDataPtr;
	int		maxReplyBytes;
	int		dataInSize;
	int		dataOutSize;
	int		maxSenseBytes;
	int		dataSgeOffset;
	char		MF[1];
};

/*
 * SPARC PLATFORM: See earlier remark.
 */
#if defined(__KERNEL__) && defined(__sparc__) && defined(__sparc_v9__)		/*{*/
struct mpt_ioctl_command32 {
	mpt_ioctl_header hdr;
	int	timeout;
	u32	replyFrameBufPtr;
	u32	dataInBufPtr;
	u32	dataOutBufPtr;
	u32	senseDataPtr;
	int	maxReplyBytes;
	int	dataInSize;
	int	dataOutSize;
	int	maxSenseBytes;
	int	dataSgeOffset;
	char	MF[1];
};
#endif	/*}*/



/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/
 /*
  *	COMPAQ Specific IOCTL Defines and Structures
  */

#define CPQFCTS_IOC_MAGIC 'Z'

#define CPQFCTS_GETPCIINFO		_IOR(CPQFCTS_IOC_MAGIC, 1, cpqfc_pci_info_struct)
#define CPQFCTS_GETDRIVER		_IOR(CPQFCTS_IOC_MAGIC, 2, int)
#define CPQFCTS_CTLR_STATUS		_IOR(CPQFCTS_IOC_MAGIC, 3, struct _cpqfc_ctlr_status)
#define CPQFCTS_SCSI_IOCTL_FC_TARGET_ADDRESS	_IOR(CPQFCTS_IOC_MAGIC, 4, struct scsi_fctargaddress)
#define CPQFCTS_SCSI_PASSTHRU		_IOWR(CPQFCTS_IOC_MAGIC, 5, VENDOR_IOCTL_REQ)
#if defined(__sparc__) && defined(__sparc_v9__)
#define CPQFCTS_SCSI_PASSTHRU32		_IOWR(CPQFCTS_IOC_MAGIC, 5, VENDOR_IOCTL_REQ32)
#endif

typedef struct {
	unsigned short bus;
	unsigned short bus_type;
	unsigned short device_fn;
	u32 board_id;
	u32 slot_number;
	unsigned short vendor_id;
	unsigned short device_id;
	unsigned short class_code;
	unsigned short sub_vendor_id;
	unsigned short sub_device_id;
	u8 serial_number[81];
} cpqfc_pci_info_struct;


typedef struct scsi_fctargaddress {
	unsigned int host_port_id;
	u8 host_wwn[8];	/* WW Network Name */
} Scsi_FCTargAddress;

typedef struct _cpqfc_ctlr_status {
	u32 status;
	u32 offline_reason;
} cpqfc_ctlr_status;


/* Compaq SCSI I/O Passthru structures.
 */
#define MPT_COMPAQ_READ		0x26
#define MPT_COMPAQ_WRITE	0x27

typedef struct {
	int lc;		/* controller number */
	int node;	/* node number */
	int ld;		/* target logical id */
	u32 nexus;
	void *argp;
} VENDOR_IOCTL_REQ;

#if defined(__KERNEL__) && defined(__sparc__) && defined(__sparc_v9__)		/*{*/
typedef struct {
	int lc;		/* controller number */
	int node;	/* node number */
	int ld;		/* target logical id */
	u32 nexus;
	u32 argp;
} VENDOR_IOCTL_REQ32;
#endif

typedef struct {
	char cdb[16];		/* cdb */
	unsigned short bus;	/* bus number */
	unsigned short pdrive;	/* physical drive */
	int len;		/* data area size */
	int sense_len;		/* sense size */
	char sense_data[40];	/* sense buffer */
	void *bufp;		/* data buffer pointer */
	char rw_flag;
} cpqfc_passthru_t;

#if defined(__KERNEL__) && defined(__sparc__) && defined(__sparc_v9__)		/*{*/
typedef struct {
	char cdb[16];		/* cdb */
	unsigned short bus;	/* bus number */
	unsigned short pdrive;	/* physical drive */
	int len;		/* data area size */
	int sense_len;		/* sense size */
	char sense_data[40];	/* sense buffer */
	u32 bufp;		/* data buffer pointer */
	char rw_flag;
} cpqfc_passthru32_t;
#endif

/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/


/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/

#endif

