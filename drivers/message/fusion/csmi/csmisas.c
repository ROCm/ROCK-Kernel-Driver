/*
 *  linux/drivers/message/fusion/csmi/csmisas.c
 *      For use with LSI PCI chip/adapter(s)
 *      running LSI Fusion MPT (Message Passing Technology) firmware.
 *
 *  Copyright (c) 1999-2008 LSI Corporation
 *  (mailto:DL-MPTFusionLinux@lsi.com)
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
/*=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=*/

#define MPT_CSMI_DESCRIPTION "LSI Corporation: Fusion MPT Driver "MPT_LINUX_VERSION_COMMON
#define csmisas_is_this_sas_cntr(ioc) (ioc->bus_type == SAS) ? 1 : 0

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0))
#define __user
#include <asm/div64.h>
#endif

static int csmisas_do_raid(MPT_ADAPTER *ioc, u8 action, u8 PhysDiskNum, u8 VolumeBus,
    u8 VolumeId, pMpiRaidActionReply_t reply);
static u8  map_sas_status_to_csmi(u8 mpi_sas_status);

/**
 * reverse_byte_order64
 *
 * @data64
 *
 **/
static u64
reverse_byte_order64(u64 data64)
{
	int i;
	u64 rc;
	u8  *inWord = (u8*)&data64, *outWord = (u8*)&rc;

	for (i = 0 ; i < 8 ; i++)
		outWord[i] = inWord[7-i];

	return rc;
}

/**
 * csmisas_is_sata
 *
 * @phys_disk
 *
 **/
static int
csmisas_is_sata(RaidPhysDiskPage0_t *phys_disk)
{
	if ((phys_disk->ExtDiskIdentifier[0] == 'A') &&
	    (phys_disk->ExtDiskIdentifier[1] == 'T') &&
	    (phys_disk->ExtDiskIdentifier[2] == 'A'))
		return 1;
	else
		return 0;
}

/**
 * csmisas_is_end_device
 *
 * @attached
 *
 **/
static inline int
csmisas_is_end_device(struct mptsas_devinfo * attached)
{
	if ((attached->sas_address) &&
	    (attached->device_info &
	    MPI_SAS_DEVICE_INFO_END_DEVICE) &&
	    ((attached->device_info &
	    MPI_SAS_DEVICE_INFO_SSP_TARGET) |
	    (attached->device_info &
	    MPI_SAS_DEVICE_INFO_STP_TARGET) |
	    (attached->device_info &
	    MPI_SAS_DEVICE_INFO_SATA_DEVICE)))
		return 1;
	else
		return 0;
}

/**
 * csmisas_is_phys_disk
 *
 * returns (1) success (0) fail - not a phys disk
 **/
static int
csmisas_is_phys_disk(MPT_ADAPTER *ioc, int channel, int id)
{
	struct inactive_raid_component_info *component_info;
	int i;
	int rc = 0;

	if (!ioc->raid_data.pIocPg3)
		goto out;
	for (i = 0; i < ioc->raid_data.pIocPg3->NumPhysDisks; i++) {
		if ((id == ioc->raid_data.pIocPg3->PhysDisk[i].PhysDiskID) &&
		    (channel == ioc->raid_data.pIocPg3->PhysDisk[i].PhysDiskBus)) {
			rc = 1;
			goto out;
		}
	}

	/*
	 * Check inactive list for matching phys disks
	 */
	if (list_empty(&ioc->raid_data.inactive_list))
		goto out;

	down(&ioc->raid_data.inactive_list_mutex);
	list_for_each_entry(component_info, &ioc->raid_data.inactive_list,
	    list) {
		if ((component_info->d.PhysDiskID == id) &&
		    (component_info->d.PhysDiskBus == channel))
			rc = 1;
	}
	up(&ioc->raid_data.inactive_list_mutex);

 out:
	return rc;
}

/**
 * csmisas_raid_id_to_num
 *
 * Obtains the phys disk num for given H:C:T nexus
 *
 * input (channel/id)
 * output (phys disk number - used by SCSI_IO_PASSTHRU to access hidden component)
 *
 * returns - signed return means failure
 **/
static s8
csmisas_raid_id_to_num(MPT_ADAPTER *ioc, u8 channel, u8 id)
{
	struct inactive_raid_component_info *component_info;
	int i;
	s8 rc = -ENXIO;

	if (!ioc->raid_data.pIocPg3)
		goto out;
	for (i = 0; i < ioc->raid_data.pIocPg3->NumPhysDisks; i++) {
		if ((id == ioc->raid_data.pIocPg3->PhysDisk[i].PhysDiskID) &&
		    (channel == ioc->raid_data.pIocPg3->PhysDisk[i].PhysDiskBus)) {
			rc = ioc->raid_data.pIocPg3->PhysDisk[i].PhysDiskNum;
			goto out;
		}
	}

	/*
	 * Check inactive list for matching phys disks
	 */
	if (list_empty(&ioc->raid_data.inactive_list))
		goto out;

	down(&ioc->raid_data.inactive_list_mutex);
	list_for_each_entry(component_info, &ioc->raid_data.inactive_list,
	    list) {
		if ((component_info->d.PhysDiskID == id) &&
		    (component_info->d.PhysDiskBus == channel))
			rc = component_info->d.PhysDiskNum;
	}
	up(&ioc->raid_data.inactive_list_mutex);

 out:
	return rc;
}

/**
 * csmisas_get_device_component_by_os
 *
 * Obtain device component object by operating system mapping
 *
 * @ioc
 * @channel
 * @id
 *
 **/
static struct sas_device_info *
csmisas_get_device_component_by_os(MPT_ADAPTER *ioc, u8 channel, u8 id)
{
	struct sas_device_info *sas_info, *p;

	sas_info = NULL;

	down(&ioc->sas_device_info_mutex);
	list_for_each_entry(p, &ioc->sas_device_info_list, list) {
		if (p->os.channel == channel && p->os.id == id) {
			sas_info = p;
			goto out;
		}
	}

 out:
	up(&ioc->sas_device_info_mutex);
	return sas_info;
}

/**
 * csmisas_get_device_component
 *
 * Obtain device component object by firmware system mapping
 *
 * @ioc
 * @channel
 * @id
 *
 **/
static struct sas_device_info *
csmisas_get_device_component_by_fw(MPT_ADAPTER *ioc, u8 channel, u8 id)
{
	struct sas_device_info *sas_info, *p;

	sas_info = NULL;

	down(&ioc->sas_device_info_mutex);
	list_for_each_entry(p, &ioc->sas_device_info_list, list) {
		if (p->fw.channel == channel && p->fw.id == id) {
			sas_info = p;
			goto out;
		}
	}

 out:
	up(&ioc->sas_device_info_mutex);
	return sas_info;
}


/**
 * csmisas_get_device_component_by_sas_addr
 *
 * Obtain device component object by sas address
 *
 * @ioc
 * @channel
 * @id
 *
 **/
static struct sas_device_info *
csmisas_get_device_component_by_sas_addr(MPT_ADAPTER *ioc, u64 sas_address)
{
	struct sas_device_info *sas_info, *p;

	sas_info = NULL;

	down(&ioc->sas_device_info_mutex);
	list_for_each_entry(p, &ioc->sas_device_info_list, list) {
		if (p->sas_address == sas_address) {
			sas_info = p;
			goto out;
		}
	}

 out:
	up(&ioc->sas_device_info_mutex);
	return sas_info;
}

/**
 * csmisas_send_command_wait
 *
 * Send mf to firmware
 *
 * @ioc
 * @mf
 * @timeout - timeout
 *
 *	Return: 0 for success
 *	non-zero, failure
 **/
static int
csmisas_send_command_wait(MPT_ADAPTER *ioc, MPT_FRAME_HDR *mf, unsigned long timeout)
{
	int rc;
	unsigned long timeleft;

	timeout = max_t(unsigned long, MPT_IOCTL_DEFAULT_TIMEOUT, timeout);
	rc = 0;
	timeleft = 0;

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0))

	INITIALIZE_IOCTL_STATUS(ioc->ioctl_cmds.status)
	ioc->ioctl_cmds.wait_done = 0;
	ioc->ioctl_cmds.timer.expires = jiffies + (MPT_JIFFY * timeout);
	ioc->ioctl_cmds.status |= MPT_MGMT_STATUS_TIMER_ACTIVE;
	ADD_TIMER(&ioc->ioctl_cmds.timer);
	mpt_put_msg_frame(mptctl_id, ioc, mf);
	WAIT_EVENT(mptctl_wait, ioc->ioctl_cmds.wait_done);

#elif (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,16))

	INITIALIZE_IOCTL_STATUS(ioc->ioctl_cmds.status)
	ioc->ioctl_cmds.wait_done = 0;
	mpt_put_msg_frame(mptctl_id, ioc, mf);

	if ((wait_event_timeout(mptctl_wait,
	    ioc->ioctl_cmds.wait_done == 1, HZ * timeout) <=0) &&
	    ioc->ioctl_cmds.wait_done != 1 ) {
		mptctl_timeout_expired(ioc,mf);
		mpt_free_msg_frame(ioc, mf);
		rc = -1;
	}

#else

	SET_MGMT_MSG_CONTEXT(ioc->ioctl_cmds.msg_context,
	    mf->u.hdr.MsgContext);
	INITIALIZE_MGMT_STATUS(ioc->ioctl_cmds.status)
	mpt_put_msg_frame(mptctl_id, ioc, mf);
	timeleft = wait_for_completion_timeout(&ioc->ioctl_cmds.done, timeout*HZ);
	if (!(ioc->ioctl_cmds.status & MPT_MGMT_STATUS_COMMAND_GOOD)) {
		rc = -1;
		printk("%s: failed\n", __FUNCTION__);
		if (ioc->ioctl_cmds.status & MPT_MGMT_STATUS_DID_IOCRESET) {
			mpt_free_msg_frame(ioc, mf);
			CLEAR_MGMT_STATUS(ioc->ioctl_cmds.status)
			return rc;
		}
		if (!timeleft)
			mptctl_timeout_expired(ioc, mf);
	}
	SET_MGMT_MSG_CONTEXT(ioc->ioctl_cmds.msg_context, 0);
#endif
	return rc;
}

/**
 * csmisas_send_handshake_wait
 *
 * Handshake a mf to firmware
 *
 * @ioc
 * @mf
 * @mf_size
 * @timeout - timeout
 *
 *	Return: 0 for success
 *	non-zero, failure
 **/
static int
csmisas_send_handshake_wait(MPT_ADAPTER *ioc, MPT_FRAME_HDR *mf, unsigned long timeout)
{
	int rc;
	unsigned long timeleft;

	timeout = max_t(unsigned long, MPT_IOCTL_DEFAULT_TIMEOUT, timeout);
	rc = 0;
	timeleft = 0;

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0))

	INITIALIZE_IOCTL_STATUS(ioc->taskmgmt_cmds.status)
	ioc->taskmgmt_cmds.timer.expires = jiffies + (MPT_JIFFY*timeout);
	ioc->taskmgmt_cmds.status |= MPT_MGMT_STATUS_TIMER_ACTIVE;
	ioc->taskmgmt_cmds.wait_done = 0;
	ADD_TIMER(&ioc->taskmgmt_cmds.timer);
	rc = mpt_send_special_message(mptctl_taskmgmt_id, ioc,
	    sizeof(SCSITaskMgmt_t), (u32*)mf, timeout, CAN_SLEEP);
	if (rc != 0)
		return rc;
	WAIT_EVENT(mptctl_taskmgmt_wait, ioc->taskmgmt_cmds.wait_done);

#elif (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,16))

	INITIALIZE_IOCTL_STATUS(ioc->taskmgmt_cmds.status)
	ioc->taskmgmt_cmds.wait_done = 0;
	rc = mpt_send_special_message(mptctl_taskmgmt_id, ioc,
	    sizeof(SCSITaskMgmt_t), (u32*)mf, timeout, CAN_SLEEP);
	if (rc != 0)
		return rc;
	if ((wait_event_timeout(mptctl_taskmgmt_wait,
	    ioc->taskmgmt_cmds.wait_done == 1, HZ * timeout) <=0) &&
	    ioc->taskmgmt_cmds.wait_done != 1 ) {
		mptctl_timeout_expired(ioc, mf);
		mpt_free_msg_frame(ioc, mf);
		rc = -1;
	}

#else
	INITIALIZE_MGMT_STATUS(ioc->taskmgmt_cmds.status)
	mpt_put_msg_frame_hi_pri(mptctl_taskmgmt_id, ioc, mf);
	timeleft = wait_for_completion_timeout(&ioc->taskmgmt_cmds.done, timeout*HZ);
	if (!(ioc->ioctl_cmds.status & MPT_MGMT_STATUS_COMMAND_GOOD)) {
		rc = -1;
		printk("%s: failed\n", __FUNCTION__);
		mpt_clear_taskmgmt_in_progress_flag(ioc);
		if (ioc->ioctl_cmds.status & MPT_MGMT_STATUS_DID_IOCRESET) {
			mpt_free_msg_frame(ioc, mf);
			CLEAR_MGMT_STATUS(ioc->taskmgmt_cmds.status)
			return rc;
		}
		if (!timeleft)
			mptctl_timeout_expired(ioc, mf);
	}
#endif
	return rc;
}

/**
 *	csmisas_get_number_hotspares - returns num hot spares in this ioc
 *	@ioc: Pointer to MPT_ADAPTER structure
 *
 *	Return: number of hotspares
 *
 **/
static int
csmisas_get_number_hotspares(MPT_ADAPTER *ioc)
{
	ConfigPageHeader_t	 hdr;
	CONFIGPARMS		 cfg;
	IOCPage5_t		 *buffer = NULL;
	dma_addr_t		 dma_handle;
	int			 data_sz;
	int			 rc;

	memset(&hdr, 0, sizeof(ConfigPageHeader_t));
	memset(&cfg, 0, sizeof(CONFIGPARMS));

	rc = 0;
	data_sz = 0;
	hdr.PageNumber = 5;
	hdr.PageType = MPI_CONFIG_PAGETYPE_IOC;
	cfg.cfghdr.hdr = &hdr;
	cfg.physAddr = -1;
	cfg.action = MPI_CONFIG_ACTION_PAGE_HEADER;
	cfg.timeout = MPT_IOCTL_DEFAULT_TIMEOUT;

	if (mpt_config(ioc, &cfg) != 0)
		goto get_ioc_pg5;

	if (hdr.PageLength == 0)
		goto get_ioc_pg5;

	data_sz = hdr.PageLength * 4;
	buffer = (IOCPage5_t *) pci_alloc_consistent(ioc->pcidev,
		data_sz, &dma_handle);
	if (!buffer)
		goto get_ioc_pg5;

	memset((u8 *)buffer, 0, data_sz);
	cfg.physAddr = dma_handle;
	cfg.action = MPI_CONFIG_ACTION_PAGE_READ_CURRENT;

	if (mpt_config(ioc, &cfg) != 0)
		goto get_ioc_pg5;

	rc = buffer->NumHotSpares;

 get_ioc_pg5:

	if (buffer)
		pci_free_consistent(ioc->pcidev, data_sz,
		    (u8 *) buffer, dma_handle);

	return rc;
}


/**
 *	csmisas_get_ioc_pg5 - ioc Page 5 hot spares
 *	@ioc: Pointer to MPT_ADAPTER structure
 *	@pIocPage5: ioc page 5
 *	@data_size: expected data size(units=bytes)
 *
 *	Return: 0 for success
 *	-ENOMEM if no memory available
 *		-EPERM if not allowed due to ISR context
 *		-EAGAIN if no msg frames currently available
 *		-EFAULT for non-successful reply or no reply (timeout)
 **/
static int
csmisas_get_ioc_pg5(MPT_ADAPTER *ioc, IOCPage5_t *iocPage5, int data_size)
{
	ConfigPageHeader_t	 hdr;
	CONFIGPARMS		 cfg;
	IOCPage5_t		 *buffer = NULL;
	dma_addr_t		 dma_handle;
	int			 data_sz;
	int			 rc;

	memset(&hdr, 0, sizeof(ConfigPageHeader_t));
	memset(&cfg, 0, sizeof(CONFIGPARMS));

	rc = 0;
	data_sz = 0;
	hdr.PageNumber = 5;
	hdr.PageType = MPI_CONFIG_PAGETYPE_IOC;
	cfg.cfghdr.hdr = &hdr;
	cfg.physAddr = -1;
	cfg.action = MPI_CONFIG_ACTION_PAGE_HEADER;
	cfg.timeout = MPT_IOCTL_DEFAULT_TIMEOUT;

	if ((rc = mpt_config(ioc, &cfg)) != 0)
		goto get_ioc_pg5;

	if (hdr.PageLength == 0) {
		rc = -EFAULT;
		goto get_ioc_pg5;
	}

	data_sz = hdr.PageLength * 4;
	buffer = (IOCPage5_t *) pci_alloc_consistent(ioc->pcidev,
		data_sz, &dma_handle);
	if (!buffer) {
		rc = -ENOMEM;
		goto get_ioc_pg5;
	}

	memset((u8 *)buffer, 0, data_sz);
	cfg.physAddr = dma_handle;
	cfg.action = MPI_CONFIG_ACTION_PAGE_READ_CURRENT;

	if ((rc = mpt_config(ioc, &cfg)) != 0)
		goto get_ioc_pg5;

	memcpy(iocPage5, buffer, data_size);

 get_ioc_pg5:

	if (buffer)
		pci_free_consistent(ioc->pcidev, data_sz,
		    (u8 *) buffer, dma_handle);

	return rc;
}

/**
 *	csmisas_sas_device_pg0 - sas device page 0
 *	@ioc: Pointer to MPT_ADAPTER structure
 *	@mptsas_devinfo: structure found in mptsas.h
 *	@form, @form_specific - defines the Page Address field in the config page
 *		(pls refer to chapter 5.1 in the mpi spec)
 *
 *	Return: 0 for success
 *	-ENOMEM if no memory available
 *		-EPERM if not allowed due to ISR context
 *		-EAGAIN if no msg frames currently available
 *		-EFAULT for non-successful reply or no reply (timeout)
 **/
static int
csmisas_sas_device_pg0(MPT_ADAPTER *ioc, struct mptsas_devinfo *device_info,
		u32 form, u32 form_specific)
{
	ConfigExtendedPageHeader_t hdr;
	CONFIGPARMS cfg;
	SasDevicePage0_t *buffer;
	dma_addr_t dma_handle;
	u64 sas_address;
	int rc;

	rc = 0;
	hdr.PageVersion = MPI_SASDEVICE0_PAGEVERSION;
	hdr.ExtPageLength = 0;
	hdr.PageNumber = 0;
	hdr.Reserved1 = 0;
	hdr.Reserved2 = 0;
	hdr.PageType = MPI_CONFIG_PAGETYPE_EXTENDED;
	hdr.ExtPageType = MPI_CONFIG_EXTPAGETYPE_SAS_DEVICE;

	cfg.cfghdr.ehdr = &hdr;
	cfg.pageAddr = form + form_specific;
	cfg.physAddr = -1;
	cfg.action = MPI_CONFIG_ACTION_PAGE_HEADER;
	cfg.dir = 0;	/* read */
	cfg.timeout = 10;

	memset(device_info, 0, sizeof(struct mptsas_devinfo));
	if ((rc = mpt_config(ioc, &cfg)) != 0)
		goto out;

	if (!hdr.ExtPageLength) {
		rc = -ENXIO;
		goto out;
	}

	buffer = pci_alloc_consistent(ioc->pcidev,
	    hdr.ExtPageLength * 4, &dma_handle);
	if (!buffer) {
		rc = -ENOMEM;
		goto out;
	}

	cfg.physAddr = dma_handle;
	cfg.action = MPI_CONFIG_ACTION_PAGE_READ_CURRENT;

	if ((rc = mpt_config(ioc, &cfg)) != 0)
		goto out_free_consistent;

	device_info->handle = le16_to_cpu(buffer->DevHandle);
	device_info->handle_parent = le16_to_cpu(buffer->ParentDevHandle);
	device_info->handle_enclosure =
	    le16_to_cpu(buffer->EnclosureHandle);
	device_info->slot = le16_to_cpu(buffer->Slot);
	device_info->phy_id = buffer->PhyNum;
	device_info->port_id = buffer->PhysicalPort;
	device_info->id = buffer->TargetID;
	device_info->channel = buffer->Bus;
	memcpy(&sas_address, &buffer->SASAddress, sizeof(u64));
	device_info->sas_address = le64_to_cpu(sas_address);
	device_info->device_info =
	    le32_to_cpu(buffer->DeviceInfo);

 out_free_consistent:
	pci_free_consistent(ioc->pcidev, hdr.ExtPageLength * 4,
			    buffer, dma_handle);
 out:
	return rc;
}

/**
 * Routine for the CSMI Sas Get Driver Info command.
 *
 * Outputs:	None.
 * Return:	0 if successful
 *		-EFAULT if data unavailable
 *		-ENODEV if no such device/adapter
 **/
static int
csmisas_get_driver_info(unsigned long arg)
{

	CSMI_SAS_DRIVER_INFO_BUFFER __user *uarg = (void __user *) arg;
	CSMI_SAS_DRIVER_INFO_BUFFER	karg;
	MPT_ADAPTER	*ioc = NULL;
	int		iocnum;

	if (copy_from_user(&karg, uarg, sizeof(CSMI_SAS_DRIVER_INFO_BUFFER))) {
		printk(KERN_ERR "%s@%d::%s - "
	      "Unable to read in csmi_sas_get_driver_info_buffer struct @ %p\n",
		    __FILE__, __LINE__, __FUNCTION__, uarg);
		return -EFAULT;
	}

	if (((iocnum = mpt_verify_adapter(karg.IoctlHeader.IOControllerNumber,
	    &ioc)) < 0) || (ioc == NULL)) {
		printk(KERN_ERR "%s::%s() @%d - ioc%d not found!\n",
		    __FILE__, __FUNCTION__, __LINE__, iocnum);
		return -ENODEV;
	}

	if (!csmisas_is_this_sas_cntr(ioc)) {
		printk(KERN_ERR "%s::%s() @%d - ioc%d not SAS controller!\n",
		    __FILE__, __FUNCTION__, __LINE__, iocnum);
		return -ENODEV;
	}

	dcsmisasprintk(ioc, printk(KERN_DEBUG "%s enter.\n",__FUNCTION__));

	/* Fill in the data and return the structure to the calling
	 * program
	 */
	memcpy( karg.Information.szName, MPT_MISCDEV_BASENAME,
	    sizeof(MPT_MISCDEV_BASENAME));
	memcpy( karg.Information.szDescription, MPT_CSMI_DESCRIPTION,
	    sizeof(MPT_CSMI_DESCRIPTION));

	karg.Information.usMajorRevision = MPT_LINUX_MAJOR_VERSION;
	karg.Information.usMinorRevision = MPT_LINUX_MINOR_VERSION;
	karg.Information.usBuildRevision = MPT_LINUX_BUILD_VERSION;
	karg.Information.usReleaseRevision = MPT_LINUX_RELEASE_VERSION;

	karg.Information.usCSMIMajorRevision = CSMI_MAJOR_REVISION;
	karg.Information.usCSMIMinorRevision = CSMI_MINOR_REVISION;

	karg.IoctlHeader.ReturnCode = CSMI_SAS_STATUS_SUCCESS;

	/* Copy the data from kernel memory to user memory
	 */
	if (copy_to_user(uarg, &karg,
		sizeof(CSMI_SAS_DRIVER_INFO_BUFFER))) {
		printk(KERN_ERR "%s@%d::%s - "
		   "Unable to write out csmi_sas_get_driver_info_buffer @ %p\n",
		    __FILE__, __LINE__, __FUNCTION__, uarg);
		return -EFAULT;
	}

	dcsmisasprintk(ioc, printk(KERN_DEBUG "%s exit.\n",__FUNCTION__));
	return 0;
}

/**
 * Prototype Routine for the CSMI_SAS_GET_CNTLR_CONFIG command.
 *
 * Outputs:	None.
 * Return:	0 if successful
 *		-EFAULT if data unavailable
 *		-ENODEV if no such device/adapter
 **/
static int
csmisas_get_cntlr_config(unsigned long arg)
{

	CSMI_SAS_CNTLR_CONFIG_BUFFER __user *uarg = (void __user *) arg;
	CSMI_SAS_CNTLR_CONFIG_BUFFER	karg;
	MPT_ADAPTER	*ioc = NULL;
	int		iocnum;
	u64		mem_phys;

	if (copy_from_user(&karg, uarg, sizeof(CSMI_SAS_CNTLR_CONFIG_BUFFER))) {
		printk(KERN_ERR "%s@%d::%s - "
	     "Unable to read in csmi_sas_get_cntlr_config_buffer struct @ %p\n",
		    __FILE__, __LINE__, __FUNCTION__, uarg);
		return -EFAULT;
	}

	if (((iocnum = mpt_verify_adapter(karg.IoctlHeader.IOControllerNumber,
	    &ioc)) < 0) || (ioc == NULL)) {
		printk(KERN_ERR "%s::%s() @%d - ioc%d not found!\n",
		    __FILE__, __FUNCTION__, __LINE__, iocnum);
		karg.IoctlHeader.ReturnCode = CSMI_SAS_STATUS_INVALID_PARAMETER;
		return -ENODEV;
	}

	if (!csmisas_is_this_sas_cntr(ioc)) {
		printk(KERN_ERR "%s::%s() @%d - ioc%d not SAS controller!\n",
		    __FILE__, __FUNCTION__, __LINE__, iocnum);
		return -ENODEV;
	}

	dcsmisasprintk(ioc, printk(KERN_DEBUG "%s enter.\n",__FUNCTION__));

	/* Clear the struct before filling in data. */
	memset( &karg.Configuration, 0, sizeof(CSMI_SAS_CNTLR_CONFIG));

	/* Fill in the data and return the structure to the calling
	 * program
	 */

	karg.Configuration.uBaseIoAddress = ioc->pio_mem_phys;
	karg.Configuration.BaseMemoryAddress.uLowPart = ioc->mem_phys;
	if (sizeof(ioc->mem_phys) == sizeof(u64)) {
		mem_phys = ioc->mem_phys;
		karg.Configuration.BaseMemoryAddress.uHighPart =
		    (u32)(mem_phys >> 32);
	}

	karg.Configuration.uBoardID = (ioc->pcidev->subsystem_device << 16) |
	    (ioc->pcidev->subsystem_vendor);

	karg.Configuration.usSlotNumber =
	    (ioc->pci_slot_number = 0xff) ?
	    SLOT_NUMBER_UNKNOWN : ioc->pci_slot_number;
	karg.Configuration.bControllerClass = CSMI_SAS_CNTLR_CLASS_HBA;
	karg.Configuration.bIoBusType = CSMI_SAS_BUS_TYPE_PCI;
	karg.Configuration.BusAddress.PciAddress.bBusNumber =
	    ioc->pcidev->bus->number;
	karg.Configuration.BusAddress.PciAddress.bDeviceNumber =
	    PCI_SLOT(ioc->pcidev->devfn);
	karg.Configuration.BusAddress.PciAddress.bFunctionNumber =
	    PCI_FUNC(ioc->pcidev->devfn);
	karg.Configuration.BusAddress.PciAddress.bReserved = 0;
	memcpy( &karg.Configuration.szSerialNumber, ioc->board_tracer, 16 );
	karg.Configuration.usMajorRevision = ioc->facts.FWVersion.Struct.Major;
	karg.Configuration.usMinorRevision = ioc->facts.FWVersion.Struct.Minor;
	karg.Configuration.usBuildRevision = ioc->facts.FWVersion.Struct.Unit;
	karg.Configuration.usReleaseRevision = ioc->facts.FWVersion.Struct.Dev;
	karg.Configuration.usBIOSMajorRevision =
	    (ioc->biosVersion & 0xFF000000) >> 24;
	karg.Configuration.usBIOSMinorRevision =
	    (ioc->biosVersion & 0x00FF0000) >> 16;
	karg.Configuration.usBIOSBuildRevision =
	    (ioc->biosVersion & 0x0000FF00) >> 8;
	karg.Configuration.usBIOSReleaseRevision =
	    (ioc->biosVersion & 0x000000FF);
	karg.Configuration.uControllerFlags = CSMI_SAS_CNTLR_SAS_HBA |
	    CSMI_SAS_CNTLR_FWD_SUPPORT | CSMI_SAS_CNTLR_FWD_ONLINE |
	    CSMI_SAS_CNTLR_FWD_SRESET ;

	/*
	 * Enabling CSMI_SAS_CNTLR_SAS_RAID bit when IR fw detected
	 */
	if (ioc->ir_firmware)
		karg.Configuration.uControllerFlags |= CSMI_SAS_CNTLR_SAS_RAID;

	karg.IoctlHeader.ReturnCode = CSMI_SAS_STATUS_SUCCESS;

	/* All Rrom entries will be zero. Skip them. */
	/* bReserved will also be zeros. */
	/* Copy the data from kernel memory to user memory
	 */
	if (copy_to_user(uarg, &karg,
		sizeof(CSMI_SAS_DRIVER_INFO_BUFFER))) {
		printk(KERN_ERR "%s@%d::%s - "
		"Unable to write out csmi_sas_get_driver_info_buffer @ %p\n",
		    __FILE__, __LINE__, __FUNCTION__, uarg);
		return -EFAULT;
	}

	dcsmisasprintk(ioc, printk(KERN_DEBUG "%s exit.\n",__FUNCTION__));
	return 0;
}

/**
 * Prototype Routine for the CSMI Sas Get Controller Status command.
 *
 * Outputs:	None.
 * Return:	0 if successful
 *		-EFAULT if data unavailable
 *		-ENODEV if no such device/adapter
 **/
static int
csmisas_get_cntlr_status(unsigned long arg)
{

	CSMI_SAS_CNTLR_STATUS_BUFFER  __user *uarg = (void __user *) arg;
	MPT_ADAPTER		*ioc = NULL;
	CSMI_SAS_CNTLR_STATUS_BUFFER	karg;
	int			iocnum;
	int			rc;

	if (copy_from_user(&karg, uarg, sizeof(CSMI_SAS_CNTLR_STATUS_BUFFER))) {
		printk(KERN_ERR "%s@%d::%s - "
	     "Unable to read in csmi_sas_get_cntlr_status_buffer struct @ %p\n",
		    __FILE__, __LINE__, __FUNCTION__, uarg);
		return -EFAULT;
	}

	if (((iocnum = mpt_verify_adapter(karg.IoctlHeader.IOControllerNumber,
	    &ioc)) < 0) || (ioc == NULL)) {
		printk(KERN_ERR "%s::%s() @%d - ioc%d not found!\n",
		    __FILE__, __FUNCTION__, __LINE__, iocnum);
		return -ENODEV;
	}

	if (!csmisas_is_this_sas_cntr(ioc)) {
		printk(KERN_ERR "%s::%s() @%d - ioc%d not SAS controller!\n",
		    __FILE__, __FUNCTION__, __LINE__, iocnum);
		return -ENODEV;
	}

	dcsmisasprintk(ioc, printk(KERN_DEBUG "%s enter.\n",__FUNCTION__));

	/* Fill in the data and return the structure to the calling
	 * program
	 */

	rc = mpt_GetIocState(ioc, 1);
	switch (rc) {
	case MPI_IOC_STATE_OPERATIONAL:
		karg.Status.uStatus =  CSMI_SAS_CNTLR_STATUS_GOOD;
		karg.Status.uOfflineReason = 0;
		break;

	case MPI_IOC_STATE_FAULT:
		karg.Status.uStatus = CSMI_SAS_CNTLR_STATUS_FAILED;
		karg.Status.uOfflineReason = 0;
		break;

	case MPI_IOC_STATE_RESET:
	case MPI_IOC_STATE_READY:
	default:
		karg.Status.uStatus =  CSMI_SAS_CNTLR_STATUS_OFFLINE;
		karg.Status.uOfflineReason =
		    CSMI_SAS_OFFLINE_REASON_INITIALIZING;
		break;
	}

	memset(&karg.Status.bReserved, 0, 28);

	karg.IoctlHeader.ReturnCode = CSMI_SAS_STATUS_SUCCESS;

	/* Copy the data from kernel memory to user memory
	 */
	if (copy_to_user(uarg, &karg,
		sizeof(CSMI_SAS_CNTLR_STATUS_BUFFER))) {
		printk(KERN_ERR "%s@%d::%s - "
		    "Unable to write out csmi_sas_get_cntlr_status @ %p\n",
		    __FILE__, __LINE__, __FUNCTION__, uarg);
		return -EFAULT;
	}

	dcsmisasprintk(ioc, printk(KERN_DEBUG "%s exit.\n",__FUNCTION__));
	return 0;
}

/**
 * Prototype Routine for the CSMI Sas Get Phy Info command.
 *
 * Outputs:	None.
 * Return:	0 if successful
 *		-EFAULT if data unavailable
 *		-ENODEV if no such device/adapter
 **/
static int
csmisas_get_phy_info(unsigned long arg)
{
	CSMI_SAS_PHY_INFO_BUFFER __user *uarg = (void __user *) arg;
	CSMI_SAS_PHY_INFO_BUFFER  *karg;
	MPT_ADAPTER		*ioc = NULL;
	ConfigExtendedPageHeader_t  hdr;
	CONFIGPARMS		cfg;
	SasIOUnitPage0_t	*sasIoUnitPg0;
	dma_addr_t		sasIoUnitPg0_dma;
	int			sasIoUnitPg0_data_sz;
	SasPhyPage0_t		*sasPhyPg0;
	dma_addr_t		sasPhyPg0_dma;
	int			sasPhyPg0_data_sz;
	u16			protocol;
	int			iocnum;
	int			rc;
	int			ii;
	u64			sas_address;
	struct mptsas_devinfo	device_info;
	int			memory_pages;

	sasIoUnitPg0=NULL;
	sasPhyPg0=NULL;
	sasIoUnitPg0_data_sz=0;
	sasPhyPg0_data_sz=0;

	memory_pages = get_order(sizeof(CSMI_SAS_PHY_INFO_BUFFER));
	karg = (CSMI_SAS_PHY_INFO_BUFFER *)__get_free_pages(
		GFP_KERNEL, memory_pages);
	if (!karg){
		printk(KERN_ERR "%s@%d::%s() - "
			"Unable to malloc CSMI_SAS_PHY_INFO_BUFFER "
			"malloc_data_sz=%d memory_pages=%d\n",
			__FILE__, __LINE__, __FUNCTION__,
			(int)sizeof(CSMI_SAS_PHY_INFO_BUFFER), memory_pages);
		return -ENOMEM;
	}

	memset(karg, 0, sizeof(*karg));

	if (copy_from_user(karg, uarg, sizeof(CSMI_SAS_PHY_INFO_BUFFER))) {
		printk(KERN_ERR "%s@%d::%s - "
		"Unable to read in csmisas_get_phy_info_buffer struct @ %p\n",
		    __FILE__, __LINE__, __FUNCTION__, uarg);
		free_pages((unsigned long)karg, memory_pages);
		return -EFAULT;
	}

	if (((iocnum = mpt_verify_adapter(karg->IoctlHeader.IOControllerNumber,
	    &ioc)) < 0) || (ioc == NULL)) {
		printk(KERN_ERR "%s::%s() @%d - ioc%d not found!\n",
		    __FILE__, __FUNCTION__, __LINE__, iocnum);
		free_pages((unsigned long)karg, memory_pages);
		return -ENODEV;
	}

	if (!csmisas_is_this_sas_cntr(ioc)) {
		printk(KERN_ERR "%s::%s() @%d - ioc%d not SAS controller!\n",
		    __FILE__, __FUNCTION__, __LINE__, iocnum);
		free_pages((unsigned long)karg, memory_pages);
		return -ENODEV;
	}

	dcsmisasprintk(ioc, printk(KERN_DEBUG "%s enter.\n",__FUNCTION__));

	/* Fill in the data and return the structure to the calling
	 * program
	 */

	/* Issue a config request to get the number of phys
	 */
	hdr.PageVersion = MPI_SASIOUNITPAGE0_PAGEVERSION;
	hdr.ExtPageLength = 0;
	hdr.PageNumber = 0;
	hdr.Reserved1 = 0;
	hdr.Reserved2 = 0;
	hdr.PageType = MPI_CONFIG_PAGETYPE_EXTENDED;
	hdr.ExtPageType = MPI_CONFIG_EXTPAGETYPE_SAS_IO_UNIT;

	cfg.cfghdr.ehdr = &hdr;
	cfg.physAddr = -1;
	cfg.pageAddr = 0;
	cfg.action = MPI_CONFIG_ACTION_PAGE_HEADER;
	cfg.dir = 0;	/* read */
	cfg.timeout = MPT_IOCTL_DEFAULT_TIMEOUT;

	if ((rc = mpt_config(ioc, &cfg)) != 0) {
		/* Don't check if this failed.  Already in a
		 * failure case.
		 */
		dcsmisasprintk(ioc, printk(KERN_ERR
		    ": FAILED: MPI_SASIOUNITPAGE0_PAGEVERSION: HEADER\n"));
		dcsmisasprintk(ioc, printk(": rc=%x\n",rc));
		karg->IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
		goto sas_get_phy_info_exit;
	}

	if (hdr.ExtPageLength == 0) {
		/* Don't check if this failed.  Already in a
		 * failure case.
		 */
		dcsmisasprintk(ioc, printk(KERN_ERR ": hdr.ExtPageLength == 0\n"));
		karg->IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
		goto sas_get_phy_info_exit;
	}

	sasIoUnitPg0_data_sz = hdr.ExtPageLength * 4;
	rc = -ENOMEM;

	sasIoUnitPg0 = (SasIOUnitPage0_t *) pci_alloc_consistent(ioc->pcidev,
	    sasIoUnitPg0_data_sz, &sasIoUnitPg0_dma);

	if (!sasIoUnitPg0) {
		dcsmisasprintk(ioc, printk(KERN_ERR ": pci_alloc_consistent: FAILED\n"));
		karg->IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
		goto sas_get_phy_info_exit;
	}

	memset((u8 *)sasIoUnitPg0, 0, sasIoUnitPg0_data_sz);
	cfg.physAddr = sasIoUnitPg0_dma;
	cfg.action = MPI_CONFIG_ACTION_PAGE_READ_CURRENT;

	if ((rc = mpt_config(ioc, &cfg)) != 0) {

		/* Don't check if this failed.  Already in a
		 * failure case.
		 */
		dcsmisasprintk(ioc, printk(KERN_ERR
		    ": FAILED: MPI_SASIOUNITPAGE0_PAGEVERSION: PAGE\n"));
		dcsmisasprintk(ioc, printk(KERN_ERR ": rc=%x\n",rc));
		karg->IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
		goto sas_get_phy_info_exit;
	}

	/* Number of Phys. */
	karg->Information.bNumberOfPhys = sasIoUnitPg0->NumPhys;

	/* Fill in information for each phy. */
	for (ii = 0; ii < karg->Information.bNumberOfPhys; ii++) {

/* EDM : dump IO Unit Page 0 data*/
		dcsmisasprintk(ioc, printk(KERN_DEBUG "---- IO UNIT PAGE 0 ------------\n"));
		dcsmisasprintk(ioc, printk(KERN_DEBUG "Handle=0x%X\n",
		    le16_to_cpu(sasIoUnitPg0->PhyData[ii].AttachedDeviceHandle)));
		dcsmisasprintk(ioc, printk(KERN_DEBUG "Controller Handle=0x%X\n",
		    le16_to_cpu(sasIoUnitPg0->PhyData[ii].ControllerDevHandle)));
		dcsmisasprintk(ioc, printk(KERN_DEBUG "Port=0x%X\n",
		    sasIoUnitPg0->PhyData[ii].Port));
		dcsmisasprintk(ioc, printk(KERN_DEBUG "Port Flags=0x%X\n",
		    sasIoUnitPg0->PhyData[ii].PortFlags));
		dcsmisasprintk(ioc, printk(KERN_DEBUG "PHY Flags=0x%X\n",
		    sasIoUnitPg0->PhyData[ii].PhyFlags));
		dcsmisasprintk(ioc, printk(KERN_DEBUG "Negotiated Link Rate=0x%X\n",
		    sasIoUnitPg0->PhyData[ii].NegotiatedLinkRate));
		dcsmisasprintk(ioc, printk(KERN_DEBUG "Controller PHY Device Info=0x%X\n",
		    le32_to_cpu(sasIoUnitPg0->PhyData[ii].ControllerPhyDeviceInfo)));
		dcsmisasprintk(ioc, printk(KERN_DEBUG "DiscoveryStatus=0x%X\n",
		    le32_to_cpu(sasIoUnitPg0->PhyData[ii].DiscoveryStatus)));
		dcsmisasprintk(ioc, printk(KERN_DEBUG "\n"));
/* EDM : debug data */

		/* PHY stuff. */
		karg->Information.Phy[ii].bPortIdentifier =
		    sasIoUnitPg0->PhyData[ii].Port;

		/* Get the negotiated link rate for the phy. */
		switch (sasIoUnitPg0->PhyData[ii].NegotiatedLinkRate) {

		case MPI_SAS_IOUNIT0_RATE_PHY_DISABLED:
			karg->Information.Phy[ii].bNegotiatedLinkRate =
			    CSMI_SAS_PHY_DISABLED;
			break;

		case MPI_SAS_IOUNIT0_RATE_FAILED_SPEED_NEGOTIATION:
			karg->Information.Phy[ii].bNegotiatedLinkRate =
			    CSMI_SAS_LINK_RATE_FAILED;
			break;

		case MPI_SAS_IOUNIT0_RATE_SATA_OOB_COMPLETE:
			break;

		case MPI_SAS_IOUNIT0_RATE_1_5:
			karg->Information.Phy[ii].bNegotiatedLinkRate =
			    CSMI_SAS_LINK_RATE_1_5_GBPS;
			break;

		case MPI_SAS_IOUNIT0_RATE_3_0:
			karg->Information.Phy[ii].bNegotiatedLinkRate =
			    CSMI_SAS_LINK_RATE_3_0_GBPS;
			break;

		case MPI_SAS_IOUNIT0_RATE_UNKNOWN:
		default:
			karg->Information.Phy[ii].bNegotiatedLinkRate =
			    CSMI_SAS_LINK_RATE_UNKNOWN;
			break;
		}

		if (sasIoUnitPg0->PhyData[ii].PortFlags &
		    MPI_SAS_IOUNIT0_PORT_FLAGS_DISCOVERY_IN_PROGRESS) {
			karg->Information.Phy[ii].bAutoDiscover =
			    CSMI_SAS_DISCOVER_IN_PROGRESS;
		} else {
			karg->Information.Phy[ii].bAutoDiscover =
			    CSMI_SAS_DISCOVER_COMPLETE;
		}

		/* Issue a config request to get
		 * phy information.
		 */
		hdr.PageVersion = MPI_SASPHY0_PAGEVERSION;
		hdr.ExtPageLength = 0;
		hdr.PageNumber = 0;
		hdr.Reserved1 = 0;
		hdr.Reserved2 = 0;
		hdr.PageType = MPI_CONFIG_PAGETYPE_EXTENDED;
		hdr.ExtPageType = MPI_CONFIG_EXTPAGETYPE_SAS_PHY;

		cfg.cfghdr.ehdr = &hdr;
		cfg.physAddr = -1;
		cfg.pageAddr = ii;
		cfg.action = MPI_CONFIG_ACTION_PAGE_HEADER;
		cfg.dir = 0;	/* read */
		cfg.timeout = MPT_IOCTL_DEFAULT_TIMEOUT;

		if ((rc = mpt_config(ioc, &cfg)) != 0) {
			dcsmisasprintk(ioc, printk(KERN_ERR
			    ": FAILED: MPI_SASPHY0_PAGEVERSION: HEADER\n"));
			dcsmisasprintk(ioc, printk(": rc=%x\n",rc));
			karg->IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
			goto sas_get_phy_info_exit;
		}

		if (hdr.ExtPageLength == 0) {
			dcsmisasprintk(ioc, printk(KERN_ERR ": pci_alloc_consistent: FAILED\n"));
			karg->IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
			goto sas_get_phy_info_exit;
		}

		sasPhyPg0_data_sz = hdr.ExtPageLength * 4;
		rc = -ENOMEM;

		sasPhyPg0 = (SasPhyPage0_t *) pci_alloc_consistent(
		    ioc->pcidev, sasPhyPg0_data_sz, &sasPhyPg0_dma);

		if (! sasPhyPg0) {
			dcsmisasprintk(ioc, printk(KERN_ERR ": pci_alloc_consistent: FAILED\n"));
			karg->IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
			goto sas_get_phy_info_exit;
		}

		memset((u8 *)sasPhyPg0, 0, sasPhyPg0_data_sz);
		cfg.physAddr = sasPhyPg0_dma;
		cfg.action = MPI_CONFIG_ACTION_PAGE_READ_CURRENT;

		if ((rc = mpt_config(ioc, &cfg)) != 0) {
			dcsmisasprintk(ioc, printk(KERN_ERR
			    ": FAILED: MPI_SASPHY0_PAGEVERSION: PAGE\n"));
			dcsmisasprintk(ioc, printk(KERN_ERR ": rc=%x\n",rc));
			karg->IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
			pci_free_consistent(ioc->pcidev, sasPhyPg0_data_sz,
			    (u8 *) sasPhyPg0, sasPhyPg0_dma);
			goto sas_get_phy_info_exit;
		}

/* EDM : dump PHY Page 0 data*/
		memcpy(&sas_address, &sasPhyPg0->SASAddress, sizeof(u64));
		dcsmisasprintk(ioc, printk(KERN_DEBUG "---- SAS PHY PAGE 0 ------------\n"));
		dcsmisasprintk(ioc, printk(KERN_DEBUG "Handle=0x%X\n",
		    le16_to_cpu(sasPhyPg0->AttachedDevHandle)));
		dcsmisasprintk(ioc, printk(KERN_DEBUG "SAS Address=0x%llX\n",
		    (unsigned long long)sas_address));
		dcsmisasprintk(ioc, printk(KERN_DEBUG "Attached PHY Identifier=0x%X\n",
		    sasPhyPg0->AttachedPhyIdentifier));
		dcsmisasprintk(ioc, printk(KERN_DEBUG "Attached Device Info=0x%X\n",
		    le32_to_cpu(sasPhyPg0->AttachedDeviceInfo)));
		dcsmisasprintk(ioc, printk(KERN_DEBUG "Programmed Link Rate=0x%X\n",
		    sasPhyPg0->ProgrammedLinkRate));
		dcsmisasprintk(ioc, printk(KERN_DEBUG "Hardware Link Rate=0x%X\n",
		    sasPhyPg0->HwLinkRate));
		dcsmisasprintk(ioc, printk(KERN_DEBUG "Change Count=0x%X\n",
		    sasPhyPg0->ChangeCount));
		dcsmisasprintk(ioc, printk(KERN_DEBUG "PHY Info=0x%X\n",
		    le32_to_cpu(sasPhyPg0->PhyInfo)));
		dcsmisasprintk(ioc, printk(KERN_DEBUG "\n"));
/* EDM : debug data */

		/* save the data */

		/* Set Max hardware link rate.
		 * This value is hard coded
		 * because the HW link rate
		 * is currently being
		 * overwritten in FW.
		 */

		/* Set Max hardware link rate. */
		switch (sasPhyPg0->HwLinkRate &
		    MPI_SAS_PHY0_PRATE_MAX_RATE_MASK) {

		case MPI_SAS_PHY0_HWRATE_MAX_RATE_1_5:
			karg->Information.Phy[ii].bMaximumLinkRate =
			    CSMI_SAS_LINK_RATE_1_5_GBPS;
			break;

		case MPI_SAS_PHY0_PRATE_MAX_RATE_3_0:
			karg->Information.Phy[ii].bMaximumLinkRate =
			    CSMI_SAS_LINK_RATE_3_0_GBPS;
			break;
		default:
			break;
		}

		/* Set Max programmed link rate. */
		switch (sasPhyPg0->ProgrammedLinkRate &
		    MPI_SAS_PHY0_PRATE_MAX_RATE_MASK) {

		case MPI_SAS_PHY0_PRATE_MAX_RATE_1_5:
			karg->Information.Phy[ii].bMaximumLinkRate |=
			    (CSMI_SAS_PROGRAMMED_LINK_RATE_1_5_GBPS << 4);
			break;

		case MPI_SAS_PHY0_PRATE_MAX_RATE_3_0:
			karg->Information.Phy[ii].bMaximumLinkRate |=
			    (CSMI_SAS_PROGRAMMED_LINK_RATE_3_0_GBPS << 4);
			break;
		default:
			break;
		}

		/* Set Min hardware link rate. */
		switch (sasPhyPg0->HwLinkRate &
		    MPI_SAS_PHY0_HWRATE_MIN_RATE_MASK) {

		case MPI_SAS_PHY0_HWRATE_MIN_RATE_1_5:
			karg->Information.Phy[ii].bMinimumLinkRate =
			    CSMI_SAS_LINK_RATE_1_5_GBPS;
			break;

		case MPI_SAS_PHY0_PRATE_MIN_RATE_3_0:
			karg->Information.Phy[ii].bMinimumLinkRate =
			    CSMI_SAS_LINK_RATE_3_0_GBPS;
			break;
		default:
			break;
		}

		/* Set Min programmed link rate. */
		switch (sasPhyPg0->ProgrammedLinkRate &
		    MPI_SAS_PHY0_PRATE_MIN_RATE_MASK) {

		case MPI_SAS_PHY0_PRATE_MIN_RATE_1_5:
			karg->Information.Phy[ii].bMinimumLinkRate |=
			    (CSMI_SAS_PROGRAMMED_LINK_RATE_1_5_GBPS << 4);
			break;

		case MPI_SAS_PHY0_PRATE_MIN_RATE_3_0:
			karg->Information.Phy[ii].bMinimumLinkRate |=
			    (CSMI_SAS_PROGRAMMED_LINK_RATE_3_0_GBPS << 4);
			break;
		default:
			break;
		}

		karg->Information.Phy[ii].bPhyChangeCount = sasPhyPg0->ChangeCount;
		if( sasPhyPg0->PhyInfo & MPI_SAS_PHY0_PHYINFO_VIRTUAL_PHY )
			karg->Information.Phy[ii].bPhyFeatures = CSMI_SAS_PHY_VIRTUAL_SMP;

		/* Fill in Attached Device
		 * Initiator Port Protocol.
		 * Bits 6:3
		 * More than one bit can be set.
		 */
		protocol = le32_to_cpu(sasPhyPg0->AttachedDeviceInfo) & 0x78;
		karg->Information.Phy[ii].Attached.bInitiatorPortProtocol = 0;
		if (protocol & MPI_SAS_DEVICE_INFO_SSP_INITIATOR)
		      karg->Information.Phy[ii].Attached.bInitiatorPortProtocol =
			    CSMI_SAS_PROTOCOL_SSP;
		if (protocol & MPI_SAS_DEVICE_INFO_STP_INITIATOR)
		     karg->Information.Phy[ii].Attached.bInitiatorPortProtocol |=
			    CSMI_SAS_PROTOCOL_STP;
		if (protocol & MPI_SAS_DEVICE_INFO_SMP_INITIATOR)
		     karg->Information.Phy[ii].Attached.bInitiatorPortProtocol |=
			    CSMI_SAS_PROTOCOL_SMP;
		if (protocol & MPI_SAS_DEVICE_INFO_SATA_HOST)
		     karg->Information.Phy[ii].Attached.bInitiatorPortProtocol |=
			    CSMI_SAS_PROTOCOL_SATA;

		/* Fill in Phy Target Port
		 * Protocol. Bits 10:7
		 * More than one bit can be set.
		 */
		protocol = le32_to_cpu(sasPhyPg0->AttachedDeviceInfo) & 0x780;
		karg->Information.Phy[ii].Attached.bTargetPortProtocol = 0;
		if (protocol & MPI_SAS_DEVICE_INFO_SSP_TARGET)
			karg->Information.Phy[ii].Attached.bTargetPortProtocol |=
			    CSMI_SAS_PROTOCOL_SSP;
		if (protocol & MPI_SAS_DEVICE_INFO_STP_TARGET)
			karg->Information.Phy[ii].Attached.bTargetPortProtocol |=
			    CSMI_SAS_PROTOCOL_STP;
		if (protocol & MPI_SAS_DEVICE_INFO_SMP_TARGET)
			karg->Information.Phy[ii].Attached.bTargetPortProtocol |=
			    CSMI_SAS_PROTOCOL_SMP;
		if (protocol & MPI_SAS_DEVICE_INFO_SATA_DEVICE)
			karg->Information.Phy[ii].Attached.bTargetPortProtocol |=
			    CSMI_SAS_PROTOCOL_SATA;


		/* Fill in Attached device type */
		switch (le32_to_cpu(sasPhyPg0->AttachedDeviceInfo) &
		    MPI_SAS_DEVICE_INFO_MASK_DEVICE_TYPE) {

		case MPI_SAS_DEVICE_INFO_NO_DEVICE:
			karg->Information.Phy[ii].Attached.bDeviceType =
			    CSMI_SAS_NO_DEVICE_ATTACHED;
			break;

		case MPI_SAS_DEVICE_INFO_END_DEVICE:
			karg->Information.Phy[ii].Attached.bDeviceType =
			    CSMI_SAS_END_DEVICE;
			break;

		case MPI_SAS_DEVICE_INFO_EDGE_EXPANDER:
			karg->Information.Phy[ii].Attached.bDeviceType =
			    CSMI_SAS_EDGE_EXPANDER_DEVICE;
			break;

		case MPI_SAS_DEVICE_INFO_FANOUT_EXPANDER:
			karg->Information.Phy[ii].Attached.bDeviceType =
			    CSMI_SAS_FANOUT_EXPANDER_DEVICE;
			break;
		}

		/* Identify Info. */
		switch (le32_to_cpu(sasIoUnitPg0->PhyData[ii].ControllerPhyDeviceInfo) &
		    MPI_SAS_DEVICE_INFO_MASK_DEVICE_TYPE) {

		case MPI_SAS_DEVICE_INFO_NO_DEVICE:
			karg->Information.Phy[ii].Identify.bDeviceType =
			    CSMI_SAS_NO_DEVICE_ATTACHED;
			break;

		case MPI_SAS_DEVICE_INFO_END_DEVICE:
			karg->Information.Phy[ii].Identify.bDeviceType =
			    CSMI_SAS_END_DEVICE;
			break;

		case MPI_SAS_DEVICE_INFO_EDGE_EXPANDER:
			karg->Information.Phy[ii].Identify.bDeviceType =
			    CSMI_SAS_EDGE_EXPANDER_DEVICE;
			break;

		case MPI_SAS_DEVICE_INFO_FANOUT_EXPANDER:
			karg->Information.Phy[ii].Identify.bDeviceType =
			    CSMI_SAS_FANOUT_EXPANDER_DEVICE;
			break;
		}

		/* Fill in Phy Initiator Port Protocol. Bits 6:3
		 * More than one bit can be set, fall through cases.
		 */
		protocol = le32_to_cpu(
		    sasIoUnitPg0->PhyData[ii].ControllerPhyDeviceInfo) & 0x78;
		karg->Information.Phy[ii].Identify.bInitiatorPortProtocol = 0;
		if( protocol & MPI_SAS_DEVICE_INFO_SSP_INITIATOR )
		     karg->Information.Phy[ii].Identify.bInitiatorPortProtocol |=
			    CSMI_SAS_PROTOCOL_SSP;
		if( protocol & MPI_SAS_DEVICE_INFO_STP_INITIATOR )
		     karg->Information.Phy[ii].Identify.bInitiatorPortProtocol |=
			    CSMI_SAS_PROTOCOL_STP;
		if( protocol & MPI_SAS_DEVICE_INFO_SMP_INITIATOR )
		     karg->Information.Phy[ii].Identify.bInitiatorPortProtocol |=
			    CSMI_SAS_PROTOCOL_SMP;
		if( protocol & MPI_SAS_DEVICE_INFO_SATA_HOST )
		     karg->Information.Phy[ii].Identify.bInitiatorPortProtocol |=
			    CSMI_SAS_PROTOCOL_SATA;

		/* Fill in Phy Target Port Protocol. Bits 10:7
		 * More than one bit can be set, fall through cases.
		 */
		protocol = le32_to_cpu(
		    sasIoUnitPg0->PhyData[ii].ControllerPhyDeviceInfo) & 0x780;
		karg->Information.Phy[ii].Identify.bTargetPortProtocol = 0;
		if( protocol & MPI_SAS_DEVICE_INFO_SSP_TARGET )
			karg->Information.Phy[ii].Identify.bTargetPortProtocol |=
			    CSMI_SAS_PROTOCOL_SSP;
		if( protocol & MPI_SAS_DEVICE_INFO_STP_TARGET )
			karg->Information.Phy[ii].Identify.bTargetPortProtocol |=
			    CSMI_SAS_PROTOCOL_STP;
		if( protocol & MPI_SAS_DEVICE_INFO_SMP_TARGET )
			karg->Information.Phy[ii].Identify.bTargetPortProtocol |=
			    CSMI_SAS_PROTOCOL_SMP;
		if( protocol & MPI_SAS_DEVICE_INFO_SATA_DEVICE )
			karg->Information.Phy[ii].Identify.bTargetPortProtocol |=
			    CSMI_SAS_PROTOCOL_SATA;

		/* Setup SAS Address for the attached device */
		if (sasPhyPg0->AttachedDevHandle) {
			sas_address = reverse_byte_order64(sas_address);
			memcpy(karg->Information.Phy[ii].Attached.bSASAddress,
			    &sas_address, sizeof(u64));
			karg->Information.Phy[ii].Attached.bPhyIdentifier =
			    sasPhyPg0->AttachedPhyIdentifier;
		}

		/* Setup SAS Address for the parent device */
		csmisas_sas_device_pg0(ioc, &device_info,
		    (MPI_SAS_DEVICE_PGAD_FORM_HANDLE <<
		    MPI_SAS_DEVICE_PGAD_FORM_SHIFT),
		    sasIoUnitPg0->PhyData[ii].ControllerDevHandle);
		sas_address = reverse_byte_order64(device_info.sas_address);
		memcpy(karg->Information.Phy[ii].Identify.bSASAddress,
		    &sas_address, sizeof(u64));
		karg->Information.Phy[ii].Identify.bPhyIdentifier = ii;

		pci_free_consistent(ioc->pcidev, sasPhyPg0_data_sz,
		    (u8 *) sasPhyPg0, sasPhyPg0_dma);
	}

sas_get_phy_info_exit:

	if (sasIoUnitPg0)
		pci_free_consistent(ioc->pcidev, sasIoUnitPg0_data_sz,
		    (u8 *) sasIoUnitPg0, sasIoUnitPg0_dma);

	/* Copy the data from kernel memory to user memory
	 */
	if (copy_to_user(uarg, karg,
	    sizeof(CSMI_SAS_PHY_INFO_BUFFER))) {
		printk(KERN_ERR "%s@%d::%s - "
		    "Unable to write out csmisas_get_phy_info_buffer @ %p\n",
		    __FILE__, __LINE__, __FUNCTION__, uarg);
		free_pages((unsigned long)karg, memory_pages);
		return -EFAULT;
	}

	free_pages((unsigned long)karg, memory_pages);
	dcsmisasprintk(ioc, printk(KERN_DEBUG "%s exit.\n",__FUNCTION__));
	return 0;
}

/**
 * Prototype Routine for the CSMI SAS Set PHY Info command.
 *
 * Outputs:	None.
 * Return:	0 if successful
 *		-EFAULT if data unavailable
 *		-ENODEV if no such device/adapter
 **/
static int
csmisas_set_phy_info(unsigned long arg)
{
	CSMI_SAS_SET_PHY_INFO_BUFFER __user *uarg = (void __user *) arg;
	CSMI_SAS_SET_PHY_INFO_BUFFER	 karg;
	MPT_ADAPTER			*ioc = NULL;
	int				iocnum;

	if (copy_from_user(&karg, uarg, sizeof(CSMI_SAS_SET_PHY_INFO_BUFFER))) {
		printk(KERN_ERR "%s@%d::%s() - "
		    "Unable to read in csmi_sas_set_phy_info struct @ %p\n",
		    __FILE__, __LINE__, __FUNCTION__, uarg);
		return -EFAULT;
	}

	if (((iocnum = mpt_verify_adapter(karg.IoctlHeader.IOControllerNumber,
	    &ioc)) < 0) || (ioc == NULL)) {
		printk(KERN_ERR "%s::%s() @%d - ioc%d not found!\n",
		    __FILE__, __FUNCTION__, __LINE__, iocnum);
		return -ENODEV;
	}

	if (!csmisas_is_this_sas_cntr(ioc)) {
		printk(KERN_ERR "%s::%s() @%d - ioc%d not SAS controller!\n",
		    __FILE__, __FUNCTION__, __LINE__, iocnum);
		return -ENODEV;
	}

	dcsmisasprintk(ioc, printk(KERN_DEBUG "%s enter.\n",__FUNCTION__));

/* TODO - implement IOCTL here */
	karg.IoctlHeader.ReturnCode = CSMI_SAS_STATUS_BAD_CNTL_CODE;
	dcsmisasprintk(ioc, printk(KERN_DEBUG ": not implemented\n"));

// cim_set_phy_info_exit:

	/* Copy the data from kernel memory to user memory
	 */
	if (copy_to_user(uarg, &karg,
				sizeof(CSMI_SAS_SET_PHY_INFO_BUFFER))) {
		printk(KERN_ERR "%s@%d::%s() - "
			"Unable to write out csmi_sas_set_phy_info @ %p\n",
				__FILE__, __LINE__, __FUNCTION__, uarg);
		return -EFAULT;
	}

	dcsmisasprintk(ioc, printk(KERN_DEBUG "%s exit.\n",__FUNCTION__));
	return 0;

}

/**
 * Prototype Routine for the CSMI Sas Get SCSI Address command.
 *
 * Outputs:	None.
 * Return:	0 if successful
 *		-EFAULT if data unavailable
 *		-ENODEV if no such device/adapter
 **/
static int
csmisas_get_scsi_address(unsigned long arg)
{
	CSMI_SAS_GET_SCSI_ADDRESS_BUFFER __user *uarg = (void __user *) arg;
	CSMI_SAS_GET_SCSI_ADDRESS_BUFFER	 karg;
	MPT_ADAPTER		*ioc = NULL;
	int			iocnum;
	u64			sas_address;
	struct sas_device_info	*sas_info;

	if (copy_from_user(&karg, uarg,
	    sizeof(CSMI_SAS_GET_SCSI_ADDRESS_BUFFER))) {
		printk(KERN_ERR "%s@%d::%s() - "
		    "Unable to read in csmi_sas_get_scsi_address struct @ %p\n",
		    __FILE__, __LINE__, __FUNCTION__, uarg);
		return -EFAULT;
	}

	if (((iocnum = mpt_verify_adapter(karg.IoctlHeader.IOControllerNumber,
	    &ioc)) < 0) || (ioc == NULL)) {
		printk(KERN_ERR "%s::%s() @%d - ioc%d not found!\n",
		    __FILE__, __FUNCTION__, __LINE__, iocnum);
		return -ENODEV;
	}

	if (!csmisas_is_this_sas_cntr(ioc)) {
		printk(KERN_ERR "%s::%s() @%d - ioc%d not SAS controller!\n",
		    __FILE__, __FUNCTION__, __LINE__, iocnum);
		return -ENODEV;
	}

	dcsmisasprintk(ioc, printk(KERN_DEBUG "%s enter.\n",__FUNCTION__));

	/* reverse byte order the sas address */
	memcpy(&sas_address, karg.bSASAddress, sizeof(u64));
	sas_address = reverse_byte_order64(sas_address);

	/* Search the list for the matching SAS address. */
	karg.IoctlHeader.ReturnCode = CSMI_SAS_NO_SCSI_ADDRESS;
	karg.bPathId = 0;
	karg.bTargetId = 0;
	karg.bLun = 0;

	sas_info = csmisas_get_device_component_by_sas_addr(ioc, sas_address);
	if (!sas_info || sas_info->is_cached || sas_info->is_logical_volume)
		goto csmisas_get_scsi_address_exit;

	karg.bPathId = sas_info->os.channel;
	karg.bTargetId = sas_info->os.id;
	karg.IoctlHeader.ReturnCode = CSMI_SAS_STATUS_SUCCESS;

 csmisas_get_scsi_address_exit:

	/* Copy the data from kernel memory to user memory
	 */
	if (copy_to_user(uarg, &karg,
	    sizeof(CSMI_SAS_GET_SCSI_ADDRESS_BUFFER))) {
		printk(KERN_ERR "%s@%d::%s() - "
		    "Unable to write out csmi_sas_get_scsi_address @ %p\n",
		    __FILE__, __LINE__, __FUNCTION__, uarg);
		return -EFAULT;
	}

	dcsmisasprintk(ioc, printk(KERN_DEBUG "%s exit.\n",__FUNCTION__));
	return 0;
}

/**
 * Prototype Routine for the CSMI Sas Get SCSI Address command.
 *
 * Outputs:	None.
 * Return:	0 if successful
 *		-EFAULT if data unavailable
 *		-ENODEV if no such device/adapter
 **/
static int
csmisas_get_sata_signature(unsigned long arg)
{
	CSMI_SAS_SATA_SIGNATURE_BUFFER  __user *uarg = (void __user *) arg;
	CSMI_SAS_SATA_SIGNATURE_BUFFER	 karg;
	MPT_ADAPTER			*ioc = NULL;
	int				iocnum;
	int				rc, jj;
	ConfigExtendedPageHeader_t	hdr;
	CONFIGPARMS			cfg;
	SasPhyPage0_t			*sasPhyPg0;
	dma_addr_t			sasPhyPg0_dma;
	int				sasPhyPg0_data_sz;
	SasDevicePage1_t		*sasDevicePg1;
	dma_addr_t			sasDevicePg1_dma;
	int				sasDevicePg1_data_sz;
	u8				phyId;
	u64				sas_address;

	sasPhyPg0=NULL;
	sasPhyPg0_data_sz=0;
	sasDevicePg1=NULL;
	sasDevicePg1_data_sz=0;

	if (copy_from_user(&karg, uarg,
	     sizeof(CSMI_SAS_SATA_SIGNATURE_BUFFER))) {
		printk(KERN_ERR "%s@%d::%s() - "
		    "Unable to read in csmi_sas_sata_signature struct @ %p\n",
		    __FILE__, __LINE__, __FUNCTION__, uarg);
		return -EFAULT;
	}

	if (((iocnum = mpt_verify_adapter(karg.IoctlHeader.IOControllerNumber,
	    &ioc)) < 0) || (ioc == NULL)) {
		printk(KERN_ERR "%s::%s() @%d - ioc%d not found!\n",
		     __FILE__, __FUNCTION__, __LINE__, iocnum);
		return -ENODEV;
	}

	if (!csmisas_is_this_sas_cntr(ioc)) {
		printk(KERN_ERR "%s::%s() @%d - ioc%d not SAS controller!\n",
		    __FILE__, __FUNCTION__, __LINE__, iocnum);
		return -ENODEV;
	}

	dcsmisasprintk(ioc, printk(KERN_DEBUG "%s enter.\n",__FUNCTION__));
	phyId = karg.Signature.bPhyIdentifier;
	if (phyId >= ioc->num_ports) {
		karg.IoctlHeader.ReturnCode = CSMI_SAS_PHY_DOES_NOT_EXIST;
		dcsmisasprintk(ioc, printk(KERN_WARNING ": phyId >= ioc->num_ports\n"));
		goto cim_sata_signature_exit;
	}

	/* Default to success.*/
	karg.IoctlHeader.ReturnCode = CSMI_SAS_STATUS_SUCCESS;

	/* Issue a config request to get the devHandle of the attached device
	 */

	/* Issue a config request to get phy information. */
	hdr.PageVersion = MPI_SASPHY0_PAGEVERSION;
	hdr.ExtPageLength = 0;
	hdr.PageNumber = 0;
	hdr.Reserved1 = 0;
	hdr.Reserved2 = 0;
	hdr.PageType = MPI_CONFIG_PAGETYPE_EXTENDED;
	hdr.ExtPageType = MPI_CONFIG_EXTPAGETYPE_SAS_PHY;

	cfg.cfghdr.ehdr = &hdr;
	cfg.physAddr = -1;
	cfg.pageAddr = phyId;
	cfg.action = MPI_CONFIG_ACTION_PAGE_HEADER;
	cfg.dir = 0;	/* read */
	cfg.timeout = MPT_IOCTL_DEFAULT_TIMEOUT;

	if ((rc = mpt_config(ioc, &cfg)) != 0) {
		/* Don't check if this failed.  Already in a
		 * failure case.
		 */
		dcsmisasprintk(ioc, printk(KERN_ERR
		    ": FAILED: MPI_SASPHY0_PAGEVERSION: HEADER\n"));
		dcsmisasprintk(ioc, printk(KERN_ERR ": rc=%x\n",rc));
		karg.IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
		goto cim_sata_signature_exit;
	}

	if (hdr.ExtPageLength == 0) {
		/* Don't check if this failed.  Already in a
		 * failure case.
		 */
		dcsmisasprintk(ioc, printk(KERN_ERR ": hdr.ExtPageLength == 0\n"));
		karg.IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
		goto cim_sata_signature_exit;
	}


	sasPhyPg0_data_sz = hdr.ExtPageLength * 4;
	rc = -ENOMEM;

	sasPhyPg0 = (SasPhyPage0_t *) pci_alloc_consistent(ioc->pcidev,
	    sasPhyPg0_data_sz, &sasPhyPg0_dma);

	if (! sasPhyPg0) {
		dcsmisasprintk(ioc, printk(KERN_ERR ": pci_alloc_consistent: FAILED\n"));
		karg.IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
		goto cim_sata_signature_exit;
	}

	memset((u8 *)sasPhyPg0, 0, sasPhyPg0_data_sz);
	cfg.physAddr = sasPhyPg0_dma;
	cfg.action = MPI_CONFIG_ACTION_PAGE_READ_CURRENT;

	if ((rc = mpt_config(ioc, &cfg)) != 0) {
		/* Don't check if this failed.  Already in a
		 * failure case.
		 */
		dcsmisasprintk(ioc, printk(KERN_ERR
		    ": FAILED: MPI_SASPHY0_PAGEVERSION: PAGE\n"));
		dcsmisasprintk(ioc, printk(KERN_ERR ": rc=%x\n",rc));
		karg.IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
		goto cim_sata_signature_exit;
	}

	/* Make sure a SATA device is attached. */
	if ((le32_to_cpu(sasPhyPg0->AttachedDeviceInfo) &
	    MPI_SAS_DEVICE_INFO_SATA_DEVICE) == 0) {
		dcsmisasprintk(ioc, printk(KERN_WARNING ": NOT A SATA DEVICE\n"));
		karg.IoctlHeader.ReturnCode = CSMI_SAS_NO_SATA_DEVICE;
		goto cim_sata_signature_exit;
	}

	/* Get device page 1 for FIS  signature. */
	hdr.PageVersion = MPI_SASDEVICE1_PAGEVERSION;
	hdr.ExtPageLength = 0;
	hdr.PageNumber = 1 /* page number 1 */;
	hdr.Reserved1 = 0;
	hdr.Reserved2 = 0;
	hdr.PageType = MPI_CONFIG_PAGETYPE_EXTENDED;
	hdr.ExtPageType = MPI_CONFIG_EXTPAGETYPE_SAS_DEVICE;

	cfg.cfghdr.ehdr = &hdr;
	cfg.physAddr = -1;

	cfg.pageAddr = ((MPI_SAS_DEVICE_PGAD_FORM_HANDLE <<
	    MPI_SAS_DEVICE_PGAD_FORM_SHIFT) |
	    le16_to_cpu(sasPhyPg0->AttachedDevHandle));
	cfg.action = MPI_CONFIG_ACTION_PAGE_HEADER;
	cfg.dir = 0;	/* read */
	cfg.timeout = MPT_IOCTL_DEFAULT_TIMEOUT;

	if ((rc = mpt_config(ioc, &cfg)) != 0) {
		dcsmisasprintk(ioc, printk(KERN_ERR
		    ": FAILED: MPI_SASDEVICE1_PAGEVERSION: HEADER\n"));
		dcsmisasprintk(ioc, printk(KERN_ERR ": rc=%x\n",rc));
		karg.IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
		goto cim_sata_signature_exit;
	}

	if (hdr.ExtPageLength == 0) {
		dcsmisasprintk(ioc, printk(KERN_ERR ": hdr.ExtPageLength == 0\n"));
		karg.IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
		goto cim_sata_signature_exit;
	}

	sasDevicePg1_data_sz = hdr.ExtPageLength * 4;
	rc = -ENOMEM;

	sasDevicePg1 = (SasDevicePage1_t *) pci_alloc_consistent
	    (ioc->pcidev, sasDevicePg1_data_sz, &sasDevicePg1_dma);

	if (! sasDevicePg1) {
		dcsmisasprintk(ioc, printk(KERN_ERR ": pci_alloc_consistent: FAILED\n"));
		karg.IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
		goto cim_sata_signature_exit;
	}

	memset((u8 *)sasDevicePg1, 0, sasDevicePg1_data_sz);
	cfg.physAddr = sasDevicePg1_dma;
	cfg.action = MPI_CONFIG_ACTION_PAGE_READ_CURRENT;

	if ((rc = mpt_config(ioc, &cfg)) != 0) {
		dcsmisasprintk(ioc, printk(KERN_ERR
		    ": FAILED: MPI_SASDEVICE1_PAGEVERSION: PAGE\n"));
		dcsmisasprintk(ioc, printk(KERN_ERR ": rc=%x\n",rc));
		karg.IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
		goto cim_sata_signature_exit;
	}

/* EDM : dump Device Page 1 data*/
	dcsmisasprintk(ioc, printk(KERN_DEBUG "---- SAS DEVICE PAGE 1 ---------\n"));
	dcsmisasprintk(ioc, printk(KERN_DEBUG "Handle=0x%x\n",sasDevicePg1->DevHandle));
	memcpy(&sas_address, &sasDevicePg1->SASAddress, sizeof(u64));
	dcsmisasprintk(ioc, printk(KERN_DEBUG "SAS Address=0x%llX\n",
	    (unsigned long long)sas_address));
	dcsmisasprintk(ioc, printk(KERN_DEBUG "\n"));
	dcsmisasprintk(ioc, printk(KERN_DEBUG "Target ID=0x%x\n",sasDevicePg1->TargetID));
	dcsmisasprintk(ioc, printk(KERN_DEBUG "Bus=0x%x\n",sasDevicePg1->Bus));
	dcsmisasprintk(ioc, printk(KERN_DEBUG "Initial Reg Device FIS="));
	for(jj=0;jj<20;jj++)
		dcsmisasprintk(ioc, printk("%02x ",
		((u8 *)&sasDevicePg1->InitialRegDeviceFIS)[jj]));
	dcsmisasprintk(ioc, printk(KERN_DEBUG "\n\n"));
/* EDM : debug data */

	memcpy(karg.Signature.bSignatureFIS,
		sasDevicePg1->InitialRegDeviceFIS,20);

 cim_sata_signature_exit:

	if (sasPhyPg0)
		pci_free_consistent(ioc->pcidev, sasPhyPg0_data_sz,
		    (u8 *) sasPhyPg0, sasPhyPg0_dma);

	if (sasDevicePg1)
		pci_free_consistent(ioc->pcidev, sasDevicePg1_data_sz,
		    (u8 *) sasDevicePg1, sasDevicePg1_dma);

	/* Copy the data from kernel memory to user memory
	 */
	if (copy_to_user(uarg, &karg,
	    sizeof(CSMI_SAS_SATA_SIGNATURE_BUFFER))) {
		printk(KERN_ERR "%s@%d::%s() - "
		    "Unable to write out csmi_sas_sata_signature @ %p\n",
		    __FILE__, __LINE__, __FUNCTION__, uarg);
		return -EFAULT;
	}

	dcsmisasprintk(ioc, printk(KERN_DEBUG "%s exit.\n",__FUNCTION__));
	return 0;
}

/**
 * Prototype Routine for the CSMI Sas Get SCSI Address command.
 *
 * Outputs:	None.
 * Return:	0 if successful
 *		-EFAULT if data unavailable
 *		-ENODEV if no such device/adapter
 **/
static int
csmisas_get_device_address(unsigned long arg)
{
	CSMI_SAS_GET_DEVICE_ADDRESS_BUFFER __user *uarg = (void __user *) arg;
	CSMI_SAS_GET_DEVICE_ADDRESS_BUFFER	 karg;
	MPT_ADAPTER		*ioc = NULL;
	int			iocnum;
	struct sas_device_info	*sas_info;
	u64			sas_address;

	if (copy_from_user(&karg, uarg,
	    sizeof(CSMI_SAS_GET_DEVICE_ADDRESS_BUFFER))) {
		printk(KERN_ERR "%s@%d::%s() - "
	   "Unable to read in csmi_sas_get_device_address_buffer struct @ %p\n",
		    __FILE__, __LINE__, __FUNCTION__, uarg);
		return -EFAULT;
	}

	if (((iocnum = mpt_verify_adapter(karg.IoctlHeader.IOControllerNumber,
	    &ioc)) < 0) || (ioc == NULL)) {
		printk(KERN_ERR "%s::%s() @%d - ioc%d not found!\n",
		    __FILE__, __FUNCTION__, __LINE__, iocnum);
		return -ENODEV;
	}

	if (!csmisas_is_this_sas_cntr(ioc)) {
		printk(KERN_ERR "%s::%s() @%d - ioc%d not SAS controller!\n",
		    __FILE__, __FUNCTION__, __LINE__, iocnum);
		return -ENODEV;
	}

	dcsmisasprintk(ioc, printk(KERN_DEBUG "%s enter.\n",__FUNCTION__));

	karg.IoctlHeader.ReturnCode = CSMI_SAS_NO_DEVICE_ADDRESS;
	memset(karg.bSASAddress, 0, sizeof(u64));
	memset(karg.bSASLun, 0, sizeof(karg.bSASLun));

	/* Search the list for the matching SAS address. */
	sas_info = csmisas_get_device_component_by_os(ioc, karg.bPathId,
	    karg.bTargetId);
	if (!sas_info || sas_info->is_cached || sas_info->is_logical_volume)
		goto csmisas_get_device_address_exit;

	sas_address = reverse_byte_order64(sas_info->sas_address);
	memcpy(karg.bSASAddress, &sas_address, sizeof(u64));
	karg.IoctlHeader.ReturnCode = CSMI_SAS_STATUS_SUCCESS;

 csmisas_get_device_address_exit:

	/* Copy the data from kernel memory to user memory
	 */
	if (copy_to_user(uarg, &karg,
	    sizeof(CSMI_SAS_GET_DEVICE_ADDRESS_BUFFER))) {
		printk(KERN_ERR "%s@%d::%s() - "
		"Unable to write out csmi_sas_get_device_address_buffer @ %p\n",
		    __FILE__, __LINE__, __FUNCTION__, uarg);
		return -EFAULT;
	}

	dcsmisasprintk(ioc, printk(KERN_DEBUG "%s exit.\n",__FUNCTION__));
	return 0;
}

/**
 * Prototype Routine for the CSMI Sas Get Link Errors command.
 *
 * Outputs:	None.
 * Return:	0 if successful
 *		-EFAULT if data unavailable
 *		-ENODEV if no such device/adapter
 **/
static int
csmisas_get_link_errors(unsigned long arg)
{
	CSMI_SAS_LINK_ERRORS_BUFFER __user *uarg = (void __user *) arg;
	CSMI_SAS_LINK_ERRORS_BUFFER	 karg;
	MPT_ADAPTER			*ioc = NULL;
	MPT_FRAME_HDR			*mf = NULL;
	MPIHeader_t			*mpi_hdr;
	int				iocnum;
	int				rc;
	ConfigExtendedPageHeader_t	hdr;
	CONFIGPARMS			cfg;
	SasPhyPage1_t			*sasPhyPage1;
	dma_addr_t			sasPhyPage1_dma;
	int				sasPhyPage1_data_sz;
	SasIoUnitControlRequest_t	*sasIoUnitCntrReq;
	SasIoUnitControlReply_t		*sasIoUnitCntrReply;
	u8				phyId;
	u16				ioc_status;
	u32				MsgContext;

	sasPhyPage1=NULL;
	sasPhyPage1_data_sz=0;

	if (copy_from_user(&karg, uarg,
	     sizeof(CSMI_SAS_LINK_ERRORS_BUFFER))) {
		printk(KERN_ERR "%s@%d::%s() - "
		    "Unable to read in csmisas_get_link_errors struct @ %p\n",
		    __FILE__, __LINE__, __FUNCTION__, uarg);
		return -EFAULT;
	}

	if (((iocnum = mpt_verify_adapter(karg.IoctlHeader.IOControllerNumber,
	    &ioc)) < 0) || (ioc == NULL)) {
		printk(KERN_ERR "%s::%s() @%d - ioc%d not found!\n",
		     __FILE__, __FUNCTION__, __LINE__, iocnum);
		return -ENODEV;
	}

	if (!csmisas_is_this_sas_cntr(ioc)) {
		printk(KERN_ERR "%s::%s() @%d - ioc%d not SAS controller!\n",
		    __FILE__, __FUNCTION__, __LINE__, iocnum);
		return -ENODEV;
	}

	dcsmisasprintk(ioc, printk(KERN_DEBUG "%s enter.\n",__FUNCTION__));
	phyId = karg.Information.bPhyIdentifier;
	if (phyId >= ioc->num_ports) {
		karg.IoctlHeader.ReturnCode = CSMI_SAS_PHY_DOES_NOT_EXIST;
		dcsmisasprintk(ioc, printk(KERN_WARNING ": phyId >= ioc->num_ports\n"));
		goto cim_get_link_errors_exit;
	}

	/* Default to success.*/
	karg.IoctlHeader.ReturnCode = CSMI_SAS_STATUS_SUCCESS;

	/* Issue a config request to get the devHandle of the attached device
	 */

	/* Issue a config request to get phy information. */
	hdr.PageVersion = MPI_SASPHY1_PAGEVERSION;
	hdr.ExtPageLength = 0;
	hdr.PageNumber = 1 /* page number 1*/;
	hdr.Reserved1 = 0;
	hdr.Reserved2 = 0;
	hdr.PageType = MPI_CONFIG_PAGETYPE_EXTENDED;
	hdr.ExtPageType = MPI_CONFIG_EXTPAGETYPE_SAS_PHY;

	cfg.cfghdr.ehdr = &hdr;
	cfg.physAddr = -1;
	cfg.pageAddr = phyId;
	cfg.action = MPI_CONFIG_ACTION_PAGE_HEADER;
	cfg.dir = 0;	/* read */
	cfg.timeout = MPT_IOCTL_DEFAULT_TIMEOUT;

	if ((rc = mpt_config(ioc, &cfg)) != 0) {
		/* Don't check if this failed.  Already in a
		 * failure case.
		 */
		dcsmisasprintk(ioc, printk(KERN_ERR
		    ": FAILED: MPI_SASPHY1_PAGEVERSION: HEADER\n"));
		dcsmisasprintk(ioc, printk(KERN_ERR ": rc=%x\n",rc));
		karg.IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
		goto cim_get_link_errors_exit;
	}

	if (hdr.ExtPageLength == 0) {
		/* Don't check if this failed.  Already in a
		 * failure case.
		 */
		dcsmisasprintk(ioc, printk(KERN_ERR ": hdr.ExtPageLength == 0\n"));
		karg.IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
		goto cim_get_link_errors_exit;
	}


	sasPhyPage1_data_sz = hdr.ExtPageLength * 4;
	rc = -ENOMEM;

	sasPhyPage1 = (SasPhyPage1_t *) pci_alloc_consistent(ioc->pcidev,
	    sasPhyPage1_data_sz, &sasPhyPage1_dma);

	if (! sasPhyPage1) {
		dcsmisasprintk(ioc, printk(KERN_ERR ": pci_alloc_consistent: FAILED\n"));
		karg.IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
		goto cim_get_link_errors_exit;
	}

	memset((u8 *)sasPhyPage1, 0, sasPhyPage1_data_sz);
	cfg.physAddr = sasPhyPage1_dma;
	cfg.action = MPI_CONFIG_ACTION_PAGE_READ_CURRENT;

	if ((rc = mpt_config(ioc, &cfg)) != 0) {
		/* Don't check if this failed.  Already in a
		 * failure case.
		 */
		dcsmisasprintk(ioc, printk(KERN_ERR ": FAILED: MPI_SASPHY1_PAGEVERSION: PAGE\n"));
		dcsmisasprintk(ioc, printk(KERN_ERR ": rc=%x\n",rc));
		karg.IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
		goto cim_get_link_errors_exit;
	}

/* EDM : dump PHY Page 1 data*/
	dcsmisasprintk(ioc, printk(KERN_DEBUG "---- SAS PHY PAGE 1 ------------\n"));
	dcsmisasprintk(ioc, printk(KERN_DEBUG "Invalid Dword Count=0x%x\n",
	    sasPhyPage1->InvalidDwordCount));
	dcsmisasprintk(ioc, printk(KERN_DEBUG "Running Disparity Error Count=0x%x\n",
	    sasPhyPage1->RunningDisparityErrorCount));
	dcsmisasprintk(ioc, printk(KERN_DEBUG "Loss Dword Synch Count=0x%x\n",
	    sasPhyPage1->LossDwordSynchCount));
	dcsmisasprintk(ioc, printk(KERN_DEBUG "PHY Reset Problem Count=0x%x\n",
	    sasPhyPage1->PhyResetProblemCount));
	dcsmisasprintk(ioc, printk(KERN_DEBUG "\n\n"));
/* EDM : debug data */

	karg.Information.uInvalidDwordCount =
		le32_to_cpu(sasPhyPage1->InvalidDwordCount);
	karg.Information.uRunningDisparityErrorCount =
		le32_to_cpu(sasPhyPage1->RunningDisparityErrorCount);
	karg.Information.uLossOfDwordSyncCount =
		le32_to_cpu(sasPhyPage1->LossDwordSynchCount);
	karg.Information.uPhyResetProblemCount =
		le32_to_cpu(sasPhyPage1->PhyResetProblemCount);

	if (karg.Information.bResetCounts ==
	    CSMI_SAS_LINK_ERROR_DONT_RESET_COUNTS ) {
		goto cim_get_link_errors_exit;
	}

	/* Clear Error log
	 *
	 * Issue IOUNIT Control Reqeust Message
	 */

	/* Get a MF for this command.
	 */
	if ((mf = mpt_get_msg_frame(mptctl_id, ioc)) == NULL) {
		dcsmisasprintk(ioc, printk(KERN_ERR ": no msg frames!\n"));
		karg.IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
		goto cim_get_link_errors_exit;
        }

	mpi_hdr = (MPIHeader_t *) mf;
	MsgContext = mpi_hdr->MsgContext;
	sasIoUnitCntrReq = (SasIoUnitControlRequest_t *)mf;
	memset(sasIoUnitCntrReq,0,sizeof(SasIoUnitControlRequest_t));
	sasIoUnitCntrReq->Function = MPI_FUNCTION_SAS_IO_UNIT_CONTROL;
	sasIoUnitCntrReq->MsgContext = MsgContext;
	sasIoUnitCntrReq->PhyNum = phyId;
	sasIoUnitCntrReq->Operation = MPI_SAS_OP_PHY_CLEAR_ERROR_LOG;

	if (csmisas_send_command_wait(ioc, mf, karg.IoctlHeader.Timeout) != 0) {
		karg.IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
		goto cim_get_link_errors_exit;
	}

	/* process the completed Reply Message Frame */
	if (ioc->ioctl_cmds.status & MPT_MGMT_STATUS_RF_VALID) {

		sasIoUnitCntrReply =
		    (SasIoUnitControlReply_t *)ioc->ioctl_cmds.reply;
		ioc_status = le16_to_cpu(sasIoUnitCntrReply->IOCStatus)
		    & MPI_IOCSTATUS_MASK;

		if (ioc_status != MPI_IOCSTATUS_SUCCESS) {
			dcsmisasprintk(ioc, printk(KERN_DEBUG ": SAS IO Unit Control: "));
			dcsmisasprintk(ioc, printk("IOCStatus=0x%X IOCLogInfo=0x%X\n",
			    sasIoUnitCntrReply->IOCStatus,
			    sasIoUnitCntrReply->IOCLogInfo));
		}
	}

 cim_get_link_errors_exit:

	if (sasPhyPage1)
		pci_free_consistent(ioc->pcidev, sasPhyPage1_data_sz,
		    (u8 *) sasPhyPage1, sasPhyPage1_dma);

	/* Copy the data from kernel memory to user memory
	 */
	if (copy_to_user(uarg, &karg,
	    sizeof(CSMI_SAS_LINK_ERRORS_BUFFER))) {
		printk(KERN_ERR "%s@%d::%s() - "
		    "Unable to write out csmisas_get_link_errors @ %p\n",
		    __FILE__, __LINE__, __FUNCTION__, uarg);
		return -EFAULT;
	}

	dcsmisasprintk(ioc, printk(KERN_DEBUG "%s exit.\n",__FUNCTION__));
	return 0;

}

/**
 * Prototype Routine for the CSMI SAS SMP Passthru command.
 *
 * Outputs:	None.
 * Return:	0 if successful
 *		-EFAULT if data unavailable
 *		-ENODEV if no such device/adapter
 **/
static int
csmisas_smp_passthru(unsigned long arg)
{
	CSMI_SAS_SMP_PASSTHRU_BUFFER __user *uarg = (void __user *) arg;
	MPT_ADAPTER			*ioc;
	CSMI_SAS_SMP_PASSTHRU_BUFFER	 *karg;
	pSmpPassthroughRequest_t	smpReq;
	pSmpPassthroughReply_t		smpReply;
	MPT_FRAME_HDR			*mf = NULL;
	MPIHeader_t			*mpi_hdr;
	char				*psge;
	int				iocnum, flagsLength;
	void *				request_data;
	dma_addr_t			request_data_dma;
	u32				request_data_sz;
	void *				response_data;
	dma_addr_t			response_data_dma;
	u32				response_data_sz;
	u16				ioc_status;
	u64				sas_address;
	u32				MsgContext;
	int				malloc_data_sz;
	int				memory_pages;

	malloc_data_sz = sizeof(CSMI_SAS_SMP_PASSTHRU_BUFFER);
	memory_pages = get_order(malloc_data_sz);
	karg = (CSMI_SAS_SMP_PASSTHRU_BUFFER *)__get_free_pages(
		GFP_KERNEL, memory_pages);
	if (!karg){
		printk(KERN_ERR "%s@%d::%s() - "
			"Unable to malloc CSMI_SAS_SMP_PASSTHRU_BUFFER "
			"malloc_data_sz=%d memory_pages=%d\n",
			__FILE__, __LINE__, __FUNCTION__,
			malloc_data_sz, memory_pages);
		return -ENOMEM;
	}

	if (copy_from_user(karg, uarg, sizeof(CSMI_SAS_SMP_PASSTHRU_BUFFER))) {
		printk(KERN_ERR "%s@%d::%s() - "
		    "Unable to read in csmi_sas_smp_passthru struct @ %p\n",
		    __FILE__, __LINE__, __FUNCTION__, uarg);
		free_pages((unsigned long)karg, memory_pages);
		return -EFAULT;
	}

	request_data = NULL;
	response_data = NULL;
	response_data_sz = sizeof(CSMI_SAS_SMP_RESPONSE);
	request_data_sz  = karg->Parameters.uRequestLength;

	if (((iocnum = mpt_verify_adapter(karg->IoctlHeader.IOControllerNumber,
	    &ioc)) < 0) || (ioc == NULL)) {
		printk(KERN_ERR "%s::%s() @%d - ioc%d not found!\n",
		    __FILE__, __FUNCTION__, __LINE__, iocnum);
		free_pages((unsigned long)karg, memory_pages);
		return -ENODEV;
	}

	if (ioc->ioc_reset_in_progress) {
		printk(KERN_ERR "%s@%d::%s - "
		    "Busy with IOC Reset \n",
		    __FILE__, __LINE__,__FUNCTION__);
		free_pages((unsigned long)karg, memory_pages);
		return -EBUSY;
	}

	if (!csmisas_is_this_sas_cntr(ioc)) {
		printk(KERN_ERR "%s::%s() @%d - ioc%d not SAS controller!\n",
		    __FILE__, __FUNCTION__, __LINE__, iocnum);
		free_pages((unsigned long)karg, memory_pages);
		return -ENODEV;
	}

	dcsmisasprintk(ioc, printk(KERN_DEBUG "%s enter.\n",__FUNCTION__));

	/* Default to success.*/
	karg->IoctlHeader.ReturnCode = CSMI_SAS_STATUS_SUCCESS;

	/* Do some error checking on the request. */
	if (karg->Parameters.bPortIdentifier == CSMI_SAS_IGNORE_PORT) {
		karg->IoctlHeader.ReturnCode = CSMI_SAS_SELECT_PHY_OR_PORT;
		goto cim_smp_passthru_exit;
	}

	if ((request_data_sz > 0xFFFF) || (!request_data_sz)) {
		karg->IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
		goto cim_smp_passthru_exit;
	}

	/* Get a free request frame and save the message context.
	 */
	if ((mf = mpt_get_msg_frame(mptctl_id, ioc)) == NULL) {
		dcsmisasprintk(ioc, printk(KERN_ERR ": no msg frames!\n"));
		karg->IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
		goto cim_smp_passthru_exit;
        }

	mpi_hdr = (MPIHeader_t *) mf;
	MsgContext = mpi_hdr->MsgContext;
	smpReq = (pSmpPassthroughRequest_t ) mf;

	memset(smpReq,0,ioc->req_sz);

	memcpy(&sas_address, karg->Parameters.bDestinationSASAddress,
	    sizeof(u64));
	sas_address = cpu_to_le64(reverse_byte_order64(sas_address));
	memcpy(&smpReq->SASAddress, &sas_address, sizeof(u64));

	/* Fill in smp request. */
	smpReq->PhysicalPort = karg->Parameters.bPortIdentifier;
	smpReq->Function = MPI_FUNCTION_SMP_PASSTHROUGH;
	smpReq->RequestDataLength = cpu_to_le16(request_data_sz);
	smpReq->ConnectionRate = karg->Parameters.bConnectionRate;
	smpReq->MsgContext = MsgContext;
	smpReq->Reserved2 = 0;
	smpReq->Reserved3 = 0;

	/*
	 * Prepare the necessary pointers to run
	 * through the SGL generation
	 */

	psge = (char *)&smpReq->SGL;

	/* setup the *Request* payload SGE */
	flagsLength = MPI_SGE_FLAGS_SIMPLE_ELEMENT |
		MPI_SGE_FLAGS_SYSTEM_ADDRESS |
		MPI_SGE_FLAGS_HOST_TO_IOC |
		MPI_SGE_FLAGS_END_OF_BUFFER;

	flagsLength = flagsLength << MPI_SGE_FLAGS_SHIFT;
	flagsLength |= request_data_sz;

	request_data = pci_alloc_consistent(
	    ioc->pcidev, request_data_sz, &request_data_dma);

	if (!request_data) {
		dcsmisasprintk(ioc, printk(KERN_ERR ": pci_alloc_consistent: FAILED\n"));
		karg->IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
		mpt_free_msg_frame(ioc, mf);
		goto cim_smp_passthru_exit;
	}

	ioc->add_sge(psge, flagsLength, request_data_dma);
	psge += ioc->SGE_size;

	memcpy(request_data, &karg->Parameters.Request, request_data_sz);

	/* setup the *Response* payload SGE */
	response_data = pci_alloc_consistent(
	    ioc->pcidev, response_data_sz, &response_data_dma);

	if (!response_data) {
		dcsmisasprintk(ioc, printk(KERN_ERR ": pci_alloc_consistent: FAILED\n"));
		karg->IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
		mpt_free_msg_frame(ioc, mf);
		goto cim_smp_passthru_exit;
	}

	flagsLength = MPI_SGE_FLAGS_SIMPLE_ELEMENT |
		MPI_SGE_FLAGS_SYSTEM_ADDRESS |
		MPI_SGE_FLAGS_IOC_TO_HOST |
		MPI_SGE_FLAGS_END_OF_BUFFER;

	flagsLength = flagsLength << MPI_SGE_FLAGS_SHIFT;
	flagsLength |= response_data_sz;

	ioc->add_sge(psge, flagsLength, response_data_dma);

	if (csmisas_send_command_wait(ioc, mf, karg->IoctlHeader.Timeout) != 0) {
		karg->IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
		goto cim_smp_passthru_exit;
	}

	if ((ioc->ioctl_cmds.status & MPT_MGMT_STATUS_RF_VALID) == 0) {
		dcsmisasprintk(ioc, printk(KERN_DEBUG ": SMP Passthru: oh no, there is no reply!!"));
		karg->IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
		goto cim_smp_passthru_exit;
	}

	/* process the completed Reply Message Frame */
	smpReply = (pSmpPassthroughReply_t )ioc->ioctl_cmds.reply;
	ioc_status = le16_to_cpu(smpReply->IOCStatus) & MPI_IOCSTATUS_MASK;

	if ((ioc_status != MPI_IOCSTATUS_SUCCESS) &&
	    (ioc_status != MPI_IOCSTATUS_SCSI_DATA_UNDERRUN)) {
		karg->IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
		dcsmisasprintk(ioc, printk(KERN_DEBUG ": SMP Passthru: "));
		dcsmisasprintk(ioc, printk("IOCStatus=0x%X IOCLogInfo=0x%X SASStatus=0x%X\n",
		    le16_to_cpu(smpReply->IOCStatus),
		    le32_to_cpu(smpReply->IOCLogInfo),
		    smpReply->SASStatus));
		goto cim_smp_passthru_exit;
	}

	karg->Parameters.bConnectionStatus =
	    map_sas_status_to_csmi(smpReply->SASStatus);


	if (le16_to_cpu(smpReply->ResponseDataLength)) {
		karg->Parameters.uResponseBytes = le16_to_cpu(smpReply->ResponseDataLength);
		memcpy(&karg->Parameters.Response,
		    response_data, le16_to_cpu(smpReply->ResponseDataLength));
	}

 cim_smp_passthru_exit:

	if (request_data)
		pci_free_consistent(ioc->pcidev, request_data_sz,
		    (u8 *)request_data, request_data_dma);

	if (response_data)
		pci_free_consistent(ioc->pcidev, response_data_sz,
		    (u8 *)response_data, response_data_dma);


	/* Copy the data from kernel memory to user memory
	 */
	if (copy_to_user(uarg, karg,
	    sizeof(CSMI_SAS_SMP_PASSTHRU_BUFFER))) {
		printk(KERN_ERR "%s@%d::%s() - "
			"Unable to write out csmi_sas_smp_passthru @ %p\n",
				__FILE__, __LINE__, __FUNCTION__, uarg);
		free_pages((unsigned long)karg, memory_pages);
		return -EFAULT;
	}

	free_pages((unsigned long)karg, memory_pages);
	dcsmisasprintk(ioc, printk(KERN_DEBUG ": %s exit.\n",__FUNCTION__));
	return 0;
}

/**
 * Prototype Routine for the CSMI SAS SSP Passthru command.
 *
 * Outputs:	None.
 * Return:	0 if successful
 *		-EFAULT if data unavailable
 *		-ENODEV if no such device/adapter
 **/
static int csmisas_ssp_passthru(unsigned long arg)
{
	CSMI_SAS_SSP_PASSTHRU_BUFFER __user *uarg = (void __user *) arg;
	CSMI_SAS_SSP_PASSTHRU_BUFFER	 karg_hdr, * karg;
	MPT_ADAPTER			*ioc = NULL;
	pSCSIIORequest_t		pScsiRequest;
	pSCSIIOReply_t			pScsiReply;
	MPT_FRAME_HDR			*mf = NULL;
	MPIHeader_t 			*mpi_hdr;
	int				iocnum,ii;
	u64				sas_address;
	u16				req_idx;
	char				*psge;
	int				flagsLength;
	void *				request_data;
	dma_addr_t			request_data_dma;
	u32				request_data_sz;
	int				malloc_data_sz;
	int				memory_pages;
	u16				ioc_status;
	u8 				volume_id;
	u8				volume_bus;
	u8				is_hidden_raid_component;
	u8				channel;
	u8				id;
	struct sas_device_info		*sas_info;
	u8				skey, asc, ascq;
	u32				MsgContext;

	if (copy_from_user(&karg_hdr, uarg, sizeof(CSMI_SAS_SSP_PASSTHRU_BUFFER))) {
		printk(KERN_ERR "%s@%d::%s() - "
		    "Unable to read in csmi_sas_ssp_passthru struct @ %p\n",
		    __FILE__, __LINE__, __FUNCTION__, uarg);
		return -EFAULT;
	}

	request_data = NULL;
	request_data_sz = karg_hdr.Parameters.uDataLength;
	channel = 0;
	id = 0;
	volume_id = 0;
	volume_bus = 0;
	is_hidden_raid_component = 0;

	malloc_data_sz = (request_data_sz +
	    offsetof(CSMI_SAS_SSP_PASSTHRU_BUFFER, bDataBuffer));
	memory_pages = get_order(malloc_data_sz);
	karg = (CSMI_SAS_SSP_PASSTHRU_BUFFER *)__get_free_pages(
		GFP_KERNEL, memory_pages);
	if (!karg){
		printk(KERN_ERR "%s@%d::%s() - "
			"Unable to malloc SAS_SSP_PASSTHRU_BUFFER "
			"malloc_data_sz=%d memory_pages=%d\n",
			__FILE__, __LINE__, __FUNCTION__,
			malloc_data_sz, memory_pages);
		return -ENOMEM;
	}

	memset(karg, 0, sizeof(*karg));

	if (copy_from_user(karg, uarg, request_data_sz +
	    offsetof(CSMI_SAS_SSP_PASSTHRU_BUFFER,bDataBuffer))) {
		printk(KERN_ERR "%s@%d::%s() - "
		    "Unable to read in csmi_sas_ssp_passthru struct @ %p\n",
		    __FILE__, __LINE__, __FUNCTION__, uarg);
		free_pages((unsigned long)karg, memory_pages);
		return -EFAULT;
	}

	/*
	 * some checks of the incoming frame
	 */
	if ( offsetof(CSMI_SAS_SSP_PASSTHRU_BUFFER,bDataBuffer) +
	    request_data_sz - sizeof(IOCTL_HEADER) >
	    karg->IoctlHeader.Length ) {
		karg->IoctlHeader.ReturnCode = CSMI_SAS_STATUS_INVALID_PARAMETER;
		dcsmisasprintk(ioc, printk(KERN_ERR
		    "%s::%s()"
		    " @%d - expected datalen incorrect!\n",
		    __FILE__, __FUNCTION__, __LINE__));
		goto cim_ssp_passthru_exit;
	}

	if (((iocnum = mpt_verify_adapter(karg->IoctlHeader.IOControllerNumber,
	    &ioc)) < 0) || (ioc == NULL)) {
		karg->IoctlHeader.ReturnCode = CSMI_SAS_STATUS_INVALID_PARAMETER;
		printk(KERN_ERR "%s::%s() @%d - ioc%d not found!\n",
		    __FILE__, __FUNCTION__, __LINE__, iocnum);
		goto cim_ssp_passthru_exit;
	}

	if (ioc->ioc_reset_in_progress) {
		printk(KERN_ERR "%s@%d::%s - "
		    "Busy with IOC Reset \n",
		    __FILE__, __LINE__,__FUNCTION__);
		return -EBUSY;
	}

	if (!csmisas_is_this_sas_cntr(ioc)) {
		karg->IoctlHeader.ReturnCode = CSMI_SAS_STATUS_INVALID_PARAMETER;
		printk(KERN_ERR "%s::%s()@%d - ioc%d not SAS controller!\n",
		    __FILE__, __FUNCTION__, __LINE__, iocnum);
		goto cim_ssp_passthru_exit;
	}

	dcsmisasprintk(ioc, printk(KERN_DEBUG "%s enter.\n",__FUNCTION__));

	/* Default to success.
	 */
	karg->IoctlHeader.ReturnCode = CSMI_SAS_STATUS_SUCCESS;

	/* Neither a phy nor a port has been selected.
	 */
	if ((karg->Parameters.bPhyIdentifier == CSMI_SAS_USE_PORT_IDENTIFIER) &&
		(karg->Parameters.bPortIdentifier == CSMI_SAS_IGNORE_PORT)) {
		karg->IoctlHeader.ReturnCode = CSMI_SAS_SELECT_PHY_OR_PORT;
		dcsmisasprintk(ioc, printk(KERN_ERR
		    "%s::%s()"
		    " @%d - incorrect bPhyIdentifier and bPortIdentifier!\n",
		    __FILE__, __FUNCTION__, __LINE__));
		goto cim_ssp_passthru_exit;
	}

	/* A phy has been selected. Verify that it's valid.
	 */
	if (karg->Parameters.bPortIdentifier == CSMI_SAS_IGNORE_PORT) {

		/* Is the phy in range? */
		if (karg->Parameters.bPhyIdentifier >= ioc->num_ports) {
			dcsmisasprintk(ioc, printk(KERN_WARNING ": phyId >= ioc->num_ports (%d %d)\n",
			    karg->Parameters.bPhyIdentifier,
			    ioc->num_ports));
			karg->IoctlHeader.ReturnCode =
			    CSMI_SAS_PHY_DOES_NOT_EXIST;
			goto cim_ssp_passthru_exit;
		}
	}

	if(karg->Parameters.bAdditionalCDBLength) {
	/* TODO - SCSI IO (32) Request Message support
	 */
		dcsmisasprintk(ioc, printk(KERN_DEBUG ": greater than 16-byte cdb "
		    "is not supported!\n"));
		    karg->IoctlHeader.ReturnCode =
			CSMI_SAS_STATUS_INVALID_PARAMETER;
		goto cim_ssp_passthru_exit;
	}

	/* we will use SAS address to resolve the scsi adddressing
	 */
	memcpy(&sas_address, karg->Parameters.bDestinationSASAddress,
	    sizeof(u64));
	sas_address = reverse_byte_order64(sas_address);

	/* Search the list for the matching SAS address.
	 */
	sas_info = csmisas_get_device_component_by_sas_addr(ioc, sas_address);
	if (!sas_info || sas_info->is_cached) {
		/*
		 *Invalid SAS address
		 */
		karg->IoctlHeader.ReturnCode =
		    CSMI_SAS_STATUS_INVALID_PARAMETER;
		dcsmisasprintk(ioc, printk(KERN_ERR
		    "%s::%s() @%d - couldn't find associated "
		    "SASAddress=%llX!\n", __FILE__, __FUNCTION__, __LINE__,
		    (unsigned long long)sas_address));
		goto cim_ssp_passthru_exit;
	}

	id = sas_info->fw.id;
	channel = sas_info->fw.channel;

	if (csmisas_is_phys_disk(ioc, channel, id)) {
		id = csmisas_raid_id_to_num(ioc, channel, id);
		channel = 0;
		is_hidden_raid_component = 1;
	}

	/* Get a free request frame and save the message context.
	 */
	if ((mf = mpt_get_msg_frame(mptctl_id, ioc)) == NULL) {
		dcsmisasprintk(ioc, printk(KERN_ERR ": no msg frames!\n"));
		karg->IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
		goto cim_ssp_passthru_exit;
        }

	mpi_hdr = (MPIHeader_t *) mf;
	MsgContext = mpi_hdr->MsgContext;
	pScsiRequest = (pSCSIIORequest_t) mf;
	req_idx = le16_to_cpu(mf->u.frame.hwhdr.msgctxu.fld.req_idx);

	memset(pScsiRequest,0,sizeof(SCSIIORequest_t));

	/* Fill in SCSI IO (16) request.
	 */

	pScsiRequest->Function = (is_hidden_raid_component == 1) ?
	    MPI_FUNCTION_RAID_SCSI_IO_PASSTHROUGH : MPI_FUNCTION_SCSI_IO_REQUEST;
	pScsiRequest->TargetID = id;
	pScsiRequest->Bus = channel;
	memcpy(pScsiRequest->LUN, &karg->Parameters.bLun, 8);
	pScsiRequest->CDBLength = karg->Parameters.bCDBLength;
	pScsiRequest->DataLength = cpu_to_le32(request_data_sz);
	pScsiRequest->MsgContext = MsgContext;
	memcpy(pScsiRequest->CDB, karg->Parameters.bCDB,
	    pScsiRequest->CDBLength);

	dcsmisasprintk(ioc, printk(KERN_DEBUG "\tchannel = %d id = %d ",
	    sas_info->fw.channel, sas_info->fw.id));
	dcsmisasprintk(ioc, if(is_hidden_raid_component)
	    printk(KERN_DEBUG "num_id = %d ", id));
	dcsmisasprintk(ioc, printk(KERN_DEBUG "\n"));
	dcsmisasprintk(ioc, printk(KERN_DEBUG "\tcdb_len = %d request_len = %d\n",
	    pScsiRequest->CDBLength, request_data_sz));
	dcsmisasprintk(ioc, printk(KERN_DEBUG "\t"));
	dcsmisasprintk(ioc, for (ii = 0; ii < pScsiRequest->CDBLength; ++ii)
	    printk(" %02x", pScsiRequest->CDB[ii]));
	dcsmisasprintk(ioc, printk(KERN_DEBUG "\n"));

	/* direction
	 */
	if (karg->Parameters.uFlags & CSMI_SAS_SSP_READ) {
		pScsiRequest->Control = cpu_to_le32(MPI_SCSIIO_CONTROL_READ);
	} else if (karg->Parameters.uFlags & CSMI_SAS_SSP_WRITE) {
		pScsiRequest->Control = cpu_to_le32(MPI_SCSIIO_CONTROL_WRITE);
	} else if ((karg->Parameters.uFlags & CSMI_SAS_SSP_UNSPECIFIED) &&
	    (!karg->Parameters.uDataLength)) {
		/* no data transfer
		 */
		pScsiRequest->Control = cpu_to_le32(MPI_SCSIIO_CONTROL_NODATATRANSFER);
	} else {
		/* no direction specified
		 */
		pScsiRequest->Control = cpu_to_le32(MPI_SCSIIO_CONTROL_READ);
		pScsiRequest->MsgFlags =
		    MPI_SCSIIO_MSGFLGS_CMD_DETERMINES_DATA_DIR;
	}

	pScsiRequest->MsgFlags &= ~MPI_SCSIIO_MSGFLGS_SENSE_WIDTH;
	if (ioc->sg_addr_size == sizeof(u64))
		pScsiRequest->MsgFlags |= MPI_SCSIIO_MSGFLGS_SENSE_WIDTH_64;

	/* task attributes
	 */
	if((karg->Parameters.uFlags && 0xFF) == 0) {
		pScsiRequest->Control |= cpu_to_le32(MPI_SCSIIO_CONTROL_SIMPLEQ);
	} else if (karg->Parameters.uFlags &
	    CSMI_SAS_SSP_TASK_ATTRIBUTE_HEAD_OF_QUEUE) {
		pScsiRequest->Control |= cpu_to_le32(MPI_SCSIIO_CONTROL_HEADOFQ);
	} else if (karg->Parameters.uFlags &
	    CSMI_SAS_SSP_TASK_ATTRIBUTE_ORDERED) {
		pScsiRequest->Control |= cpu_to_le32(MPI_SCSIIO_CONTROL_ORDEREDQ);
	} else if (karg->Parameters.uFlags &
	    CSMI_SAS_SSP_TASK_ATTRIBUTE_ACA) {
		pScsiRequest->Control |= cpu_to_le32(MPI_SCSIIO_CONTROL_ACAQ);
	} else {
		pScsiRequest->Control |= cpu_to_le32(MPI_SCSIIO_CONTROL_UNTAGGED);
	}

	/* setup sense
	 */
	pScsiRequest->SenseBufferLength = MPT_SENSE_BUFFER_SIZE;
	pScsiRequest->SenseBufferLowAddr = cpu_to_le32(ioc->sense_buf_low_dma +
	    (req_idx * MPT_SENSE_BUFFER_ALLOC));

	/* setup databuffer sg, assuming we fit everything one contiguous buffer
	 */
	psge = (char *)&pScsiRequest->SGL;

	if (karg->Parameters.uFlags & CSMI_SAS_SSP_WRITE) {
		flagsLength = MPT_SGE_FLAGS_SSIMPLE_WRITE;
	} else if (karg->Parameters.uFlags & CSMI_SAS_SSP_READ) {
		flagsLength = MPT_SGE_FLAGS_SSIMPLE_READ;
	}else {
		flagsLength = ( MPI_SGE_FLAGS_SIMPLE_ELEMENT |
				MPI_SGE_FLAGS_DIRECTION )
				<< MPI_SGE_FLAGS_SHIFT;
	}
	flagsLength |= request_data_sz;

	if ( request_data_sz > 0) {
		request_data = pci_alloc_consistent(
		    ioc->pcidev, request_data_sz, &request_data_dma);

		if (request_data == NULL) {
			dcsmisasprintk(ioc, printk(KERN_ERR ": pci_alloc_consistent: FAILED "
			    "request_data_sz=%d\n", request_data_sz));
			karg->IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
			mpt_free_msg_frame(ioc, mf);
			goto cim_ssp_passthru_exit;
		}

		ioc->add_sge(psge, flagsLength, request_data_dma);
		if (karg->Parameters.uFlags & CSMI_SAS_SSP_WRITE)
			memcpy(request_data, karg->bDataBuffer, request_data_sz);
	} else {
		ioc->add_sge(psge, flagsLength, (dma_addr_t) -1);
	}

	if (csmisas_send_command_wait(ioc, mf, karg->IoctlHeader.Timeout) != 0) {
		karg->IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
		goto cim_ssp_passthru_exit;
	}

	memset(&karg->Status,0,sizeof(CSMI_SAS_SSP_PASSTHRU_STATUS));
	karg->Status.bConnectionStatus = CSMI_SAS_OPEN_ACCEPT;
	karg->Status.bDataPresent = CSMI_SAS_SSP_NO_DATA_PRESENT;
	karg->Status.bStatus = GOOD;
	karg->Status.bResponseLength[0] = 0;
	karg->Status.bResponseLength[1] = 0;
	karg->Status.uDataBytes = request_data_sz;

	/* process the completed Reply Message Frame */
	if (ioc->ioctl_cmds.status & MPT_MGMT_STATUS_RF_VALID) {

		pScsiReply = (pSCSIIOReply_t ) ioc->ioctl_cmds.reply;
		karg->Status.bStatus = pScsiReply->SCSIStatus;
		karg->Status.uDataBytes = min(le32_to_cpu(pScsiReply->TransferCount),
		    request_data_sz);
		ioc_status = le16_to_cpu(pScsiReply->IOCStatus) & MPI_IOCSTATUS_MASK;

		if (pScsiReply->SCSIState ==
		    MPI_SCSI_STATE_AUTOSENSE_VALID) {
			karg->Status.bConnectionStatus =
			    CSMI_SAS_SSP_SENSE_DATA_PRESENT;
			karg->Status.bResponseLength[0] =
				(u8)le32_to_cpu(pScsiReply->SenseCount) & 0xFF;
			memcpy(karg->Status.bResponse,
			    ioc->ioctl_cmds.sense, le32_to_cpu(pScsiReply->SenseCount));

			skey = ioc->ioctl_cmds.sense[2] & 0x0F;
			asc = ioc->ioctl_cmds.sense[12];
			ascq = ioc->ioctl_cmds.sense[13];

			dcsmisasprintk(ioc, printk(KERN_DEBUG "\t [sense_key,asc,ascq]: "
			    "[0x%02x,0x%02x,0x%02x]\n",
			    skey, asc, ascq));

		} else if(pScsiReply->SCSIState ==
		    MPI_SCSI_STATE_RESPONSE_INFO_VALID) {
			karg->Status.bDataPresent =
			    CSMI_SAS_SSP_RESPONSE_DATA_PRESENT;
			karg->Status.bResponseLength[0] =
				sizeof(pScsiReply->ResponseInfo);
			for (ii=0;ii<sizeof(pScsiReply->ResponseInfo);ii++) {
				karg->Status.bResponse[ii] =
				((u8*)&pScsiReply->ResponseInfo)[
				    (sizeof(pScsiReply->ResponseInfo)-1)-ii];
			}
		} else if ((ioc_status != MPI_IOCSTATUS_SUCCESS) &&
		    (ioc_status !=  MPI_IOCSTATUS_SCSI_RECOVERED_ERROR) &&
		    (ioc_status != MPI_IOCSTATUS_SCSI_DATA_UNDERRUN)) {
			karg->IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
			dcsmisasprintk(ioc, printk(KERN_DEBUG ": SCSI IO : "));
			dcsmisasprintk(ioc, printk("IOCStatus=0x%X IOCLogInfo=0x%X\n",
			    pScsiReply->IOCStatus,
			    pScsiReply->IOCLogInfo));
		}
	}

	if ((karg->Status.uDataBytes) && (request_data) &&
	    (karg->Parameters.uFlags & CSMI_SAS_SSP_READ)) {
		if (copy_to_user((void __user *)uarg->bDataBuffer,
		    request_data, karg->Status.uDataBytes)) {
			printk(KERN_ERR "%s@%d::%s - "
			    "Unable to write data to user %p\n",
			    __FILE__, __LINE__,__FUNCTION__,
			    (void*)karg->bDataBuffer);
			karg->IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
		}
	}

 cim_ssp_passthru_exit:


	if (request_data)
		pci_free_consistent(ioc->pcidev, request_data_sz,
		    (u8 *)request_data, request_data_dma);

	/* Copy the data from kernel memory to user memory
	 */
	if (copy_to_user(uarg, karg,
	    offsetof(CSMI_SAS_SSP_PASSTHRU_BUFFER, bDataBuffer))) {
		printk(KERN_ERR "%s@%d::%s() - "
			"Unable to write out csmi_sas_ssp_passthru @ %p\n",
				__FILE__, __LINE__, __FUNCTION__, uarg);
		free_pages((unsigned long)karg, memory_pages);
		return -EFAULT;
	}

	dcsmisasprintk(ioc, printk(KERN_DEBUG "%s exit.\n",__FUNCTION__));
	free_pages((unsigned long)karg, memory_pages);
	return 0;
}

/**
 * Prototype Routine for the CSMI SAS STP Passthru command.
 *
 * Outputs:	None.
 * Return:	0 if successful
 *		-EFAULT if data unavailable
 *		-ENODEV if no such device/adapter
 **/
static int
csmisas_stp_passthru(unsigned long arg)
{
	CSMI_SAS_STP_PASSTHRU_BUFFER __user *uarg = (void __user *) arg;
	CSMI_SAS_STP_PASSTHRU_BUFFER	karg_hdr, *karg;
	MPT_ADAPTER			*ioc = NULL;
	pSataPassthroughRequest_t  	pSataRequest;
	pSataPassthroughReply_t		pSataReply;
	MPT_FRAME_HDR			*mf = NULL;
	MPIHeader_t 			*mpi_hdr;
	int				iocnum;
	u32				data_sz;
	u64				sas_address;
	u16				req_idx;
	char				*psge;
	int				flagsLength;
	void *				request_data;
	dma_addr_t			request_data_dma;
	u32				request_data_sz;
	int				malloc_data_sz;
	int				memory_pages;
	u8				channel;
	u8				id;
	u8 				volume_id;
	u8				volume_bus;
	struct sas_device_info		*sas_info;
	u16				ioc_status;
	u32				MsgContext;

	if (copy_from_user(&karg_hdr, uarg, sizeof(CSMI_SAS_STP_PASSTHRU_BUFFER))) {
		printk(KERN_ERR "%s@%d::%s() - "
		    "Unable to read struct @ %p\n",
		    __FILE__, __LINE__, __FUNCTION__, uarg);
		return -EFAULT;
	}

	request_data=NULL;
	request_data_sz = karg_hdr.Parameters.uDataLength;
	volume_id = 0;
	volume_bus = 0;
	channel = 0;
	id = 0;

	malloc_data_sz = (request_data_sz +
	    offsetof(CSMI_SAS_STP_PASSTHRU_BUFFER, bDataBuffer));
	memory_pages = get_order(malloc_data_sz);
	karg = (CSMI_SAS_STP_PASSTHRU_BUFFER *)__get_free_pages(
		GFP_KERNEL, memory_pages);
	if (!karg){
		printk(KERN_ERR "%s@%d::%s() - "
			"Unable to malloc CSMI_SAS_STP_PASSTHRU_BUFFER "
			"malloc_data_sz=%d memory_pages=%d\n",
			__FILE__, __LINE__, __FUNCTION__, 
			malloc_data_sz, memory_pages);
		return -ENOMEM;
	}

	memset(karg, 0, sizeof(*karg));

	if (copy_from_user(karg, uarg, malloc_data_sz)) {
		printk(KERN_ERR "%s@%d::%s() - "
		    "Unable to read in csmi_sas_ssp_passthru struct @ %p\n",
		    __FILE__, __LINE__, __FUNCTION__, uarg);
		free_pages((unsigned long)karg, memory_pages);
		return -EFAULT;
	}

	if (((iocnum = mpt_verify_adapter(karg->IoctlHeader.IOControllerNumber,
	    &ioc)) < 0) || (ioc == NULL)) {
		printk(KERN_ERR "%s::%s @%d - ioc%d not found!\n",
		    __FILE__, __FUNCTION__, __LINE__, iocnum);
		free_pages((unsigned long)karg, memory_pages);
		return -ENODEV;
	}

	if (ioc->ioc_reset_in_progress) {
		printk(KERN_ERR "%s@%d::%s - "
		    "Busy with IOC Reset \n",
		    __FILE__, __LINE__,__FUNCTION__);
		free_pages((unsigned long)karg, memory_pages);
		return -EBUSY;
	}

	if (!csmisas_is_this_sas_cntr(ioc)) {
		printk(KERN_ERR "%s::%s() @%d - ioc%d not SAS controller!\n",
		    __FILE__, __FUNCTION__, __LINE__, iocnum);
		free_pages((unsigned long)karg, memory_pages);
		return -ENODEV;
	}

	dcsmisasprintk(ioc, printk(KERN_DEBUG "%s enter.\n",__FUNCTION__));

	/* Default to success.
	 */
	karg->IoctlHeader.ReturnCode = CSMI_SAS_STATUS_SUCCESS;

	/* Neither a phy nor a port has been selected.
	 */
	if ((karg->Parameters.bPhyIdentifier == CSMI_SAS_USE_PORT_IDENTIFIER) &&
		(karg->Parameters.bPortIdentifier == CSMI_SAS_IGNORE_PORT)) {
		karg->IoctlHeader.ReturnCode = CSMI_SAS_SELECT_PHY_OR_PORT;
		dcsmisasprintk(ioc, printk(KERN_ERR
		    "%s::%s() @%d - incorrect bPhyIdentifier and bPortIdentifier!\n",
		    __FILE__,__FUNCTION__, __LINE__));
		goto cim_stp_passthru_exit;
	}

	/* A phy has been selected. Verify that it's valid.
	 */
	if (karg->Parameters.bPortIdentifier == CSMI_SAS_IGNORE_PORT) {

		/* Is the phy in range? */
		if (karg->Parameters.bPhyIdentifier >= ioc->num_ports) {
			karg->IoctlHeader.ReturnCode =
			    CSMI_SAS_PHY_DOES_NOT_EXIST;
			goto cim_stp_passthru_exit;
		}
	}

	data_sz = sizeof(CSMI_SAS_STP_PASSTHRU_BUFFER) -
	    sizeof(IOCTL_HEADER) - sizeof(u8*) +
	    request_data_sz;

	if ( data_sz > karg->IoctlHeader.Length ) {
		karg->IoctlHeader.ReturnCode =
		    CSMI_SAS_STATUS_INVALID_PARAMETER;
		dcsmisasprintk(ioc, printk(KERN_ERR
		    "%s::%s() @%d - expected datalen incorrect!\n",
		    __FILE__, __FUNCTION__,__LINE__));
		goto cim_stp_passthru_exit;
	}


	/* we will use SAS address to resolve the scsi adddressing
	 */
	memcpy(&sas_address, karg->Parameters.bDestinationSASAddress,
	    sizeof(u64));
	sas_address = reverse_byte_order64(sas_address);

	/* Search the list for the matching SAS address.
	 */
	sas_info = csmisas_get_device_component_by_sas_addr(ioc, sas_address);
	if (!sas_info || sas_info->is_cached || sas_info->is_logical_volume) {
		/*
		 *Invalid SAS address
		 */
		karg->IoctlHeader.ReturnCode =
		    CSMI_SAS_STATUS_INVALID_PARAMETER;
		dcsmisasprintk(ioc, printk(KERN_ERR
		    "%s::%s() @%d - couldn't find associated "
		    "SASAddress=%llX!\n", __FILE__, __FUNCTION__, __LINE__,
		    (unsigned long long)sas_address));
		goto cim_stp_passthru_exit;
	}

	id = sas_info->fw.id;
	channel = sas_info->fw.channel;

	/* check that this is an STP or SATA target device
	 */
	if ( !(sas_info->device_info & MPI_SAS_DEVICE_INFO_STP_TARGET ) &&
	     !(sas_info->device_info & MPI_SAS_DEVICE_INFO_SATA_DEVICE )) {
		karg->IoctlHeader.ReturnCode =
		    CSMI_SAS_STATUS_INVALID_PARAMETER;
		goto cim_stp_passthru_exit;
	}

	/* Get a free request frame and save the message context.
	 */
	if ((mf = mpt_get_msg_frame(mptctl_id, ioc)) == NULL) {
		dcsmisasprintk(ioc, printk(KERN_ERR ": no msg frames!\n"));
		karg->IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
		goto cim_stp_passthru_exit;
        }

	mpi_hdr = (MPIHeader_t *) mf;
	MsgContext = mpi_hdr->MsgContext;
	pSataRequest = (pSataPassthroughRequest_t) mf;
	req_idx = le16_to_cpu(mf->u.frame.hwhdr.msgctxu.fld.req_idx);

	memset(pSataRequest,0,sizeof(pSataPassthroughRequest_t));

	pSataRequest->TargetID = id;
	pSataRequest->Bus = channel;
	pSataRequest->Function = MPI_FUNCTION_SATA_PASSTHROUGH;
	pSataRequest->PassthroughFlags = cpu_to_le16(karg->Parameters.uFlags);
	pSataRequest->ConnectionRate = karg->Parameters.bConnectionRate;
	pSataRequest->MsgContext = MsgContext;
	pSataRequest->DataLength = cpu_to_le32(request_data_sz);
	pSataRequest->MsgFlags = 0;
	memcpy( pSataRequest->CommandFIS,karg->Parameters.bCommandFIS, 20);

	psge = (char *)&pSataRequest->SGL;
	if (karg->Parameters.uFlags & CSMI_SAS_STP_WRITE) {
		flagsLength = MPT_SGE_FLAGS_SSIMPLE_WRITE;
	} else if (karg->Parameters.uFlags & CSMI_SAS_STP_READ) {
		flagsLength = MPT_SGE_FLAGS_SSIMPLE_READ;
	}else {
		flagsLength = ( MPI_SGE_FLAGS_SIMPLE_ELEMENT |
				MPI_SGE_FLAGS_DIRECTION )
				<< MPI_SGE_FLAGS_SHIFT;
	}

	flagsLength |= request_data_sz;
	if (request_data_sz > 0) {
		request_data = pci_alloc_consistent(
		    ioc->pcidev, request_data_sz, &request_data_dma);

		if (request_data == NULL) {
			dcsmisasprintk(ioc, printk(KERN_ERR ": pci_alloc_consistent: FAILED\n"));
			karg->IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
			mpt_free_msg_frame(ioc, mf);
			goto cim_stp_passthru_exit;
		}

		ioc->add_sge(psge, flagsLength, request_data_dma);
		if (karg->Parameters.uFlags & CSMI_SAS_SSP_WRITE)
			memcpy(request_data, karg->bDataBuffer, request_data_sz);
	} else {
		ioc->add_sge(psge, flagsLength, (dma_addr_t) -1);
	}

	if (csmisas_send_command_wait(ioc, mf, karg->IoctlHeader.Timeout) != 0) {
		karg->IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
		goto cim_stp_passthru_exit;
	}

	memset(&karg->Status,0,sizeof(CSMI_SAS_STP_PASSTHRU_STATUS));

	if ((ioc->ioctl_cmds.status & MPT_MGMT_STATUS_RF_VALID) == 0) {
		dcsmisasprintk(ioc, printk(KERN_DEBUG  ": STP Passthru: oh no, there is no reply!!"));
		karg->IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
		goto cim_stp_passthru_exit;
	}

	/* process the completed Reply Message Frame */
	pSataReply = (pSataPassthroughReply_t ) ioc->ioctl_cmds.reply;
	ioc_status = le16_to_cpu(pSataReply->IOCStatus) & MPI_IOCSTATUS_MASK;

	if (ioc_status != MPI_IOCSTATUS_SUCCESS &&
	    ioc_status != MPI_IOCSTATUS_SCSI_DATA_UNDERRUN) {
		karg->IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
		dcsmisasprintk(ioc, printk(KERN_DEBUG ": STP Passthru: "));
		dcsmisasprintk(ioc, printk("IOCStatus=0x%X IOCLogInfo=0x%X SASStatus=0x%X\n",
		    le16_to_cpu(pSataReply->IOCStatus),
		    le32_to_cpu(pSataReply->IOCLogInfo),
		    pSataReply->SASStatus));
	}

	karg->Status.bConnectionStatus =
	    map_sas_status_to_csmi(pSataReply->SASStatus);

	memcpy(karg->Status.bStatusFIS,pSataReply->StatusFIS, 20);

	/*
	 * for now, just zero out uSCR array,
	 * then copy the one dword returned
	 * in the reply frame into uSCR[0]
	 */
	memset( karg->Status.uSCR, 0, 64);
	karg->Status.uSCR[0] = le32_to_cpu(pSataReply->StatusControlRegisters);

	if((le32_to_cpu(pSataReply->TransferCount)) && (request_data) &&
	    (karg->Parameters.uFlags & CSMI_SAS_STP_READ)) {
		karg->Status.uDataBytes =
		    min(le32_to_cpu(pSataReply->TransferCount),request_data_sz);
		if (copy_to_user((void __user *)uarg->bDataBuffer,
		    request_data, karg->Status.uDataBytes)) {
			printk(KERN_ERR "%s::%s() @%d - "
			    "Unable to write data to user %p\n",
			    __FILE__, __FUNCTION__, __LINE__,
			    (void*)karg->bDataBuffer);
			karg->IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
		}
	}

 cim_stp_passthru_exit:

	if (request_data)
		pci_free_consistent(ioc->pcidev, request_data_sz,
		    (u8 *)request_data, request_data_dma);

	/* Copy th data from kernel memory to user memory
	 */
	if (copy_to_user(uarg, karg,
	    offsetof(CSMI_SAS_STP_PASSTHRU_BUFFER, bDataBuffer))) {
		printk(KERN_ERR "%s@%d::%s() - "
			"Unable to write out csmi_sas_ssp_passthru @ %p\n",
				__FILE__, __LINE__, __FUNCTION__, uarg);
		free_pages((unsigned long)karg, memory_pages);
		return -EFAULT;
	}

	free_pages((unsigned long)karg, memory_pages);
	dcsmisasprintk(ioc, printk(KERN_DEBUG ": %s exit.\n",__FUNCTION__));
	return 0;
}

/**
 * Prototype Routine for the CSMI SAS Firmware Download command.
 *
 * Outputs:	None.
 * Return:	0 if successful
 *		-EFAULT if data unavailable
 *		-ENODEV if no such device/adapter
 **/
static int
csmisas_firmware_download(unsigned long arg)
{
	CSMI_SAS_FIRMWARE_DOWNLOAD_BUFFER __user *uarg = (void __user *) arg;
	CSMI_SAS_FIRMWARE_DOWNLOAD_BUFFER	 karg;
	MPT_ADAPTER			*ioc = NULL;
	int				iocnum;
	pMpiFwHeader_t			pFwHeader=NULL;

	if (copy_from_user(&karg, uarg,
		sizeof(CSMI_SAS_FIRMWARE_DOWNLOAD_BUFFER))) {
		printk(KERN_ERR "%s@%d::%s() - "
		    "Unable to read in csmi_sas_firmware_download struct @ %p\n",
		    __FILE__, __LINE__, __FUNCTION__, uarg);
		return -EFAULT;
	}

	if (((iocnum = mpt_verify_adapter(karg.IoctlHeader.IOControllerNumber,
	    &ioc)) < 0) || (ioc == NULL)) {
		printk(KERN_ERR "%s::%s() @%d - ioc%d not found!\n",
		    __FILE__, __FUNCTION__, __LINE__, iocnum);
		return -ENODEV;
	}

	if (!csmisas_is_this_sas_cntr(ioc)) {
		printk(KERN_ERR "%s::%s() @%d - ioc%d not SAS controller!\n",
		    __FILE__, __FUNCTION__, __LINE__, iocnum);
		return -ENODEV;
	}

	dcsmisasprintk(ioc, printk(KERN_DEBUG "%s enter.\n",__FUNCTION__));

	/* Default to success.*/
	karg.IoctlHeader.ReturnCode = CSMI_SAS_STATUS_SUCCESS;
	karg.Information.usStatus = CSMI_SAS_FWD_SUCCESS;
	karg.Information.usSeverity = CSMI_SAS_FWD_INFORMATION;

	/* some checks of the incoming frame */
	if ((karg.Information.uBufferLength +
	    sizeof(CSMI_SAS_FIRMWARE_DOWNLOAD)) >
	    karg.IoctlHeader.Length) {
		karg.IoctlHeader.ReturnCode =
		    CSMI_SAS_STATUS_INVALID_PARAMETER;
		karg.Information.usStatus = CSMI_SAS_FWD_FAILED;
		goto cim_firmware_download_exit;
	}

	if ( karg.Information.uDownloadFlags &
	    (CSMI_SAS_FWD_SOFT_RESET | CSMI_SAS_FWD_VALIDATE)) {
		karg.IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
		karg.Information.usStatus = CSMI_SAS_FWD_REJECT;
		karg.Information.usSeverity = CSMI_SAS_FWD_ERROR;
		goto cim_firmware_download_exit;
	}

	/* now we need to alloc memory so we can pull in the
	 * fw image attached to end of incoming packet.
	 */
	pFwHeader = kmalloc(karg.Information.uBufferLength, GFP_KERNEL);
	if (!pFwHeader){
		karg.IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
		karg.Information.usStatus = CSMI_SAS_FWD_REJECT;
		karg.Information.usSeverity = CSMI_SAS_FWD_ERROR;
		goto cim_firmware_download_exit;
	}
	memset(pFwHeader, 0, sizeof(*pFwHeader));

	if (copy_from_user(pFwHeader, uarg->bDataBuffer,
		karg.Information.uBufferLength)) {
		printk(KERN_ERR "%s@%d::%s() - "
		    "Unable to read in pFwHeader @ %p\n",
		    __FILE__, __LINE__, __FUNCTION__, uarg);
		return -EFAULT;
	}

	if ( !((pFwHeader->Signature0 == MPI_FW_HEADER_SIGNATURE_0) &&
	    (pFwHeader->Signature1 == MPI_FW_HEADER_SIGNATURE_1) &&
	    (pFwHeader->Signature2 == MPI_FW_HEADER_SIGNATURE_2))) {
		// the signature check failed
		karg.IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
		karg.Information.usStatus = CSMI_SAS_FWD_REJECT;
		karg.Information.usSeverity = CSMI_SAS_FWD_ERROR;
		goto cim_firmware_download_exit;
	}

	if ( mptctl_do_fw_download(karg.IoctlHeader.IOControllerNumber,
	    uarg->bDataBuffer, karg.Information.uBufferLength)
	    != 0) {
		karg.IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
		karg.Information.usStatus = CSMI_SAS_FWD_FAILED;
		karg.Information.usSeverity = CSMI_SAS_FWD_FATAL;
		goto cim_firmware_download_exit;
	}

	if((karg.Information.uDownloadFlags & CSMI_SAS_FWD_SOFT_RESET) ||
	    (karg.Information.uDownloadFlags & CSMI_SAS_FWD_HARD_RESET)) {
		if (mpt_HardResetHandler(ioc, CAN_SLEEP) != 0) {
			karg.IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
			karg.Information.usStatus = CSMI_SAS_FWD_FAILED;
			karg.Information.usSeverity = CSMI_SAS_FWD_FATAL;
		}
	}

 cim_firmware_download_exit:

	if(pFwHeader)
		kfree(pFwHeader);

	/* Copy the data from kernel memory to user memory
	 */
	if (copy_to_user(uarg, &karg,
				sizeof(CSMI_SAS_FIRMWARE_DOWNLOAD_BUFFER))) {
		printk(KERN_ERR "%s@%d::%s() - "
			"Unable to write out csmi_sas_firmware_download @ %p\n",
				__FILE__, __LINE__, __FUNCTION__, uarg);
		return -EFAULT;
	}

	dcsmisasprintk(ioc, printk(KERN_DEBUG "%s exit.\n",__FUNCTION__));
	return 0;
}

/**
 * Prototype Routine for the CSMI SAS Get RAID Info command.
 *
 * Outputs:	None.
 * Return:	0 if successful
 *		-EFAULT if data unavailable
 *		-ENODEV if no such device/adapter
 **/
static int
csmisas_get_raid_info(unsigned long arg)
{
	CSMI_SAS_RAID_INFO_BUFFER __user *uarg =  (void __user *) arg;
	CSMI_SAS_RAID_INFO_BUFFER	 karg;
	MPT_ADAPTER			*ioc = NULL;
	int				iocnum;
	u32				raidFlags;
	u8				maxRaidTypes;
	u8				maxDrivesPerSet;

	if (copy_from_user(&karg, uarg, sizeof(CSMI_SAS_RAID_INFO_BUFFER))) {
		printk(KERN_ERR "%s@%d::%s() - "
		    "Unable to read in csmi_sas_get_raid_info struct @ %p\n",
		    __FILE__, __LINE__, __FUNCTION__, uarg);
		return -EFAULT;
	}

	if (((iocnum = mpt_verify_adapter(karg.IoctlHeader.IOControllerNumber,
	    &ioc)) < 0) || (ioc == NULL)) {
		printk(KERN_ERR "%s::%s() @%d - ioc%d not found!\n",
		    __FILE__, __FUNCTION__, __LINE__, iocnum);
		return -ENODEV;
	}

	if (!csmisas_is_this_sas_cntr(ioc)) {
		printk(KERN_ERR "%s::%s() @%d - ioc%d not SAS controller!\n",
		    __FILE__, __FUNCTION__, __LINE__, iocnum);
		return -ENODEV;
	}

	dcsmisasprintk(ioc, printk(KERN_DEBUG "%s enter.\n",__FUNCTION__));

	karg.IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
	if (!ioc->raid_data.pIocPg2)
		goto csmisas_get_raid_info_out;
	karg.Information.uNumRaidSets =
	    ioc->raid_data.pIocPg2->NumActiveVolumes;
	karg.Information.uMaxRaidSets = ioc->raid_data.pIocPg2->MaxVolumes;
	if( ioc->raid_data.pIocPg6 ) {
		// get absolute maximum for all RAID sets
		maxDrivesPerSet = ioc->raid_data.pIocPg6->MaxDrivesIS;
		maxDrivesPerSet = max(ioc->raid_data.pIocPg6->MaxDrivesIM,
		    maxDrivesPerSet);
		maxDrivesPerSet = max(ioc->raid_data.pIocPg6->MaxDrivesIME,
		    maxDrivesPerSet);
		karg.Information.uMaxDrivesPerSet = maxDrivesPerSet;
	}
	else
		karg.Information.uMaxDrivesPerSet = 8;
	// For bMaxRaidSets, count bits set in bits 0-6 of CapabilitiesFlags
	raidFlags = ioc->raid_data.pIocPg2->CapabilitiesFlags & 0x0000007F;
	for( maxRaidTypes=0; raidFlags; maxRaidTypes++ )
		raidFlags &= raidFlags - 1;
	karg.Information.bMaxRaidTypes = maxRaidTypes;
	// ulMinRaidSetBlocks hard coded to 1MB until available from config page
	karg.Information.ulMinRaidSetBlocks.uLowPart = 2048;
	karg.Information.ulMinRaidSetBlocks.uHighPart = 0;
	karg.Information.ulMaxRaidSetBlocks.uLowPart = 0xffffffff;
	if( ioc->raid_data.pIocPg2->CapabilitiesFlags &
	    MPI_IOCPAGE2_CAP_FLAGS_RAID_64_BIT_ADDRESSING )
		karg.Information.ulMaxRaidSetBlocks.uHighPart = 0xffffffff;
	else
		karg.Information.ulMaxRaidSetBlocks.uHighPart = 0;
	karg.Information.uMaxPhysicalDrives =
	    ioc->raid_data.pIocPg2->MaxPhysDisks;
	karg.Information.uMaxExtents = 1;
	karg.Information.uMaxModules = 0;
	karg.Information.uMaxTransformationMemory = 0;
	karg.Information.uChangeCount = ioc->csmi_change_count;
	karg.IoctlHeader.ReturnCode = CSMI_SAS_STATUS_SUCCESS;

csmisas_get_raid_info_out:

	/* Copy the data from kernel memory to user memory
	 */
	if (copy_to_user(uarg, &karg,
				sizeof(CSMI_SAS_RAID_INFO_BUFFER))) {
		printk(KERN_ERR "%s@%d::%s() - "
			"Unable to write out csmi_sas_get_raid_info @ %p\n",
				__FILE__, __LINE__, __FUNCTION__, uarg);
		return -EFAULT;
	}

	dcsmisasprintk(ioc, printk(KERN_DEBUG "%s exit.\n",__FUNCTION__));
	return 0;
}

/**
 *	csmisas_do_raid - Format and Issue a RAID volume request message.
 *	@ioc: Pointer to MPT_ADAPTER structure
 *	@action: What do be done.
 *	@PhysDiskNum: Logical target id.
 *	@VolumeBus: Target locations bus.
 *	@VolumeId: Volume id
 *
 *	Returns: < 0 on a fatal error
 *		0 on success
 *
 *	Remark: Wait to return until reply processed by the ISR.
 **/
static int
csmisas_do_raid(MPT_ADAPTER *ioc, u8 action, u8 PhysDiskNum, u8 VolumeBus, u8 VolumeId, pMpiRaidActionReply_t reply)
{
	MpiRaidActionRequest_t	*pReq;
	MpiRaidActionReply_t	*pReply;
	MPT_FRAME_HDR		*mf;

	/* Get and Populate a free Frame
	 */
	if ((mf = mpt_get_msg_frame(mptctl_id, ioc)) == NULL) {
		dcsmisasprintk(ioc, printk(KERN_ERR ": no msg frames!\n"));
		return -EAGAIN;
	}
	pReq = (MpiRaidActionRequest_t *)mf;
	pReq->Action = action;
	pReq->Reserved1 = 0;
	pReq->ChainOffset = 0;
	pReq->Function = MPI_FUNCTION_RAID_ACTION;
	pReq->VolumeID = VolumeId;
	pReq->VolumeBus = VolumeBus;
	pReq->PhysDiskNum = PhysDiskNum;
	pReq->MsgFlags = 0;
	pReq->Reserved2 = 0;
	pReq->ActionDataWord = 0; /* Reserved for this action */
	//pReq->ActionDataSGE = 0;

	ioc->add_sge((char *)&pReq->ActionDataSGE,
		MPT_SGE_FLAGS_SSIMPLE_READ | 0, (dma_addr_t) -1);

	if (csmisas_send_command_wait(ioc, mf, MPT_IOCTL_DEFAULT_TIMEOUT) != 0)
		return -ENODATA;

	if ((ioc->ioctl_cmds.status & MPT_MGMT_STATUS_RF_VALID) &&
	    (reply != NULL)){
		pReply = (MpiRaidActionReply_t *)&(ioc->ioctl_cmds.reply);
		memcpy(reply, pReply,
			min(ioc->reply_sz,
			4*pReply->MsgLength));
	}

	return 0;
}

/**
 * csmisas_raid_inq
 * @ioc = per host instance
 * @opcode = MPI_FUNCTION_RAID_SCSI_IO_PASSTHROUGH or
 *  	     MPI_FUNCTION_SCSI_IO_REQUEST
 * @id = target id
 * @bus = target bus
 * @inq_vpd = inquiry data, returned
 * @inq_vpd_sz = maximum size of inquiry data
 *
 * Return = 0(sucess), non-zero(failure)
 **/
static int
csmisas_raid_inq(MPT_ADAPTER *ioc, u8 opcode, u8 bus, u8 id, u8 inq_vpd_page,
    u8 * inq_vpd, u32 inq_vpd_sz)
{
	MPT_FRAME_HDR		*mf = NULL;
	MPIHeader_t 		*mpi_hdr;
	pSCSIIORequest_t	pScsiRequest;
	u16			req_idx;
	char			*psge;
	u8 			inq_vpd_cdb[6];
	u8 			*request_data=NULL;
	dma_addr_t		request_data_dma;
	u32			request_data_sz;
	int			rc = 0;
	u32			MsgContext;

	request_data_sz = inq_vpd_sz;

	/* fill-in cdb */
	memset(inq_vpd_cdb, 0, sizeof(inq_vpd_cdb));
	inq_vpd_cdb[0] = 0x12;
	if (inq_vpd_page) {
		inq_vpd_cdb[1] = 0x01; /* evpd bit */
		inq_vpd_cdb[2] = inq_vpd_page;
	}
	inq_vpd_cdb[3] = (u8)(request_data_sz >> 8);
	inq_vpd_cdb[4] = (u8)request_data_sz;

	/* Get a free request frame and save the message context.
	 */
	if ((mf = mpt_get_msg_frame(mptctl_id, ioc)) == NULL) {
		dcsmisasprintk(ioc, printk(KERN_ERR ": no msg frames!\n"));
		goto csmisas_raid_inq_exit;
	}

	mpi_hdr = (MPIHeader_t *) mf;
	MsgContext = mpi_hdr->MsgContext;
	pScsiRequest = (pSCSIIORequest_t) mf;
	req_idx = le16_to_cpu(mf->u.frame.hwhdr.msgctxu.fld.req_idx);

	memset(pScsiRequest,0,sizeof(SCSIIORequest_t));
	pScsiRequest->Function = opcode;
	pScsiRequest->TargetID = id;
	pScsiRequest->Bus = bus;
	pScsiRequest->CDBLength = 6;
	pScsiRequest->DataLength = cpu_to_le32(request_data_sz);
	pScsiRequest->MsgContext = MsgContext;
	memcpy(pScsiRequest->CDB,inq_vpd_cdb,pScsiRequest->CDBLength);
	pScsiRequest->Control = cpu_to_le32(MPI_SCSIIO_CONTROL_READ);
	pScsiRequest->Control |= cpu_to_le32(MPI_SCSIIO_CONTROL_SIMPLEQ);
	pScsiRequest->MsgFlags &= ~MPI_SCSIIO_MSGFLGS_SENSE_WIDTH;
	if (ioc->sg_addr_size == sizeof(u64))
		pScsiRequest->MsgFlags |= MPI_SCSIIO_MSGFLGS_SENSE_WIDTH_64;

	/* setup sense
	 */
	pScsiRequest->SenseBufferLength = MPT_SENSE_BUFFER_SIZE;
	pScsiRequest->SenseBufferLowAddr = cpu_to_le32(ioc->sense_buf_low_dma +
	    (req_idx * MPT_SENSE_BUFFER_ALLOC));

	request_data = pci_alloc_consistent(
	    ioc->pcidev, request_data_sz, &request_data_dma);

	if (request_data == NULL) {
		mpt_free_msg_frame(ioc, mf);
		rc=-1;
		goto csmisas_raid_inq_exit;
	}

	memset(request_data,0,request_data_sz);
	psge = (char *)&pScsiRequest->SGL;
	ioc->add_sge(psge, (MPT_SGE_FLAGS_SSIMPLE_READ | 0xFC) ,
	    request_data_dma);

	if (csmisas_send_command_wait(ioc, mf, MPT_IOCTL_DEFAULT_TIMEOUT) != 0) {
		rc=-1;
		goto csmisas_raid_inq_exit;
	}

	/* copy the request_data */
	memcpy(inq_vpd, request_data, request_data_sz);

 csmisas_raid_inq_exit:

	if (request_data)
		pci_free_consistent(ioc->pcidev, request_data_sz,
		    request_data, request_data_dma);

	return rc;
}

/**
 * Prototype Routine for the CSMI SAS Get RAID Config command.
 *
 * Outputs:	None.
 * Return:	0 if successful
 *		-EFAULT if data unavailable
 *		-ENODEV if no such device/adapter
 **/
static int
csmisas_get_raid_config(unsigned long arg)
{
	CSMI_SAS_RAID_CONFIG_BUFFER __user *uarg = (void __user *) arg;
	CSMI_SAS_RAID_CONFIG_BUFFER	 karg,*pKarg=NULL;
	CONFIGPARMS		 	cfg;
	ConfigPageHeader_t	 	header;
	MPT_ADAPTER			*ioc = NULL;
	int				iocnum;
	u8				volumeID, VolumeBus;
	u8				physDiskNum, physDiskNumMax;
	int			 	volumepage0sz = 0;
	int				physdiskpage0sz = 0, ioc_page5_sz = 0;
	dma_addr_t			volume0_dma, physdisk0_dma;
	dma_addr_t			ioc_page5_dma = 0;
	pRaidVolumePage0_t		pVolume0 = NULL;
	pRaidPhysDiskPage0_t		pPhysDisk0 = NULL;
	pMpiRaidActionReply_t 		pRaidActionReply = NULL;
	u32				device_info = 0;
	pIOCPage5_t			pIocPage5 = NULL;
	int 				i, idx, csmi_sas_raid_config_buffer_sz;
	int				memory_pages;
	int				copy_buffer_sz = 0;
	u64				totalMaxLBA, tmpTotalMaxLBA;
	u64				sas_address;
	struct sas_device_info		*sas_info;

	if (copy_from_user(&karg, uarg, sizeof(IOCTL_HEADER))) {
		printk(KERN_ERR "%s@%d::%s() - "
		    "Unable to read in csmisas_get_raid_config struct @ %p\n",
		    __FILE__, __LINE__, __FUNCTION__, uarg);
		return -EFAULT;
	}

	csmi_sas_raid_config_buffer_sz = karg.IoctlHeader.Length;
	memory_pages = get_order(csmi_sas_raid_config_buffer_sz);
	pKarg = (CSMI_SAS_RAID_CONFIG_BUFFER *)__get_free_pages(
		GFP_KERNEL, memory_pages);
	if (!pKarg){
		printk(KERN_ERR "%s@%d::%s() - "
		    "Unable to malloc RAID_CONFIG_BUFFER "
			"csmi_sas_raid_config_buffer_sz=%d memory_pages=%d\n",
			__FILE__, __LINE__, __FUNCTION__, 
			csmi_sas_raid_config_buffer_sz, memory_pages);
		return -ENOMEM;
	}
	memset(pKarg, 0, sizeof(*pKarg));

	if (copy_from_user(pKarg, uarg, csmi_sas_raid_config_buffer_sz)) {
		printk(KERN_ERR "%s@%d::%s() - "
		    "Unable to read in csmisas_get_raid_config struct @ %p\n",
		    __FILE__, __LINE__, __FUNCTION__, uarg);
		free_pages((unsigned long)pKarg, memory_pages);
		return -EFAULT;
	}

	if (((iocnum = mpt_verify_adapter(pKarg->IoctlHeader.IOControllerNumber,
	    &ioc)) < 0) || (ioc == NULL)) {
		printk(KERN_ERR "%s::%s() @%d - ioc%d not found!\n",
		    __FILE__, __FUNCTION__, __LINE__, iocnum);
		free_pages((unsigned long)pKarg, memory_pages);
		return -ENODEV;
	}

	if (!csmisas_is_this_sas_cntr(ioc)) {
		printk(KERN_ERR "%s::%s() @%d - ioc%d not SAS controller!\n",
		    __FILE__, __FUNCTION__, __LINE__, iocnum);
		free_pages((unsigned long)pKarg, memory_pages);
		return -ENODEV;
	}

	dcsmisasprintk(ioc, printk(KERN_DEBUG "%s enter.\n",__FUNCTION__));

	if (pKarg->Configuration.uChangeCount != 0 &&
		pKarg->Configuration.uChangeCount != ioc->csmi_change_count ) {
		pKarg->IoctlHeader.ReturnCode =
		    CSMI_SAS_STATUS_INVALID_PARAMETER;
		pKarg->Configuration.uFailureCode =
		    CSMI_SAS_FAIL_CODE_CHANGE_COUNT_INVALID;
		goto cim_get_raid_config_exit;
	}

	if (!ioc->raid_data.pIocPg2) {
		karg.IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
		goto cim_get_raid_config_exit;
	}

	/*
	 * Check to see if the input uRaidSetIndex is
	 * greater than the number of RAID sets
	 */
	if (pKarg->Configuration.uRaidSetIndex >=
	    ioc->raid_data.pIocPg2->NumActiveVolumes) {
		pKarg->IoctlHeader.ReturnCode = CSMI_SAS_RAID_SET_OUT_OF_RANGE;
		goto cim_get_raid_config_exit;
	}

	/*
	 * get RAID Volume Page 0
	 */
	volumeID = ioc->raid_data.pIocPg2->RaidVolume[pKarg->Configuration.uRaidSetIndex].VolumeID;
	VolumeBus = ioc->raid_data.pIocPg2->RaidVolume[pKarg->Configuration.uRaidSetIndex].VolumeBus;

	header.PageVersion = 0;
	header.PageLength = 0;
	header.PageNumber = 0;
	header.PageType = MPI_CONFIG_PAGETYPE_RAID_VOLUME;
	cfg.cfghdr.hdr = &header;
	cfg.physAddr = -1;
	cfg.pageAddr = (VolumeBus << 8) + volumeID;
	cfg.action = MPI_CONFIG_ACTION_PAGE_HEADER;
	cfg.dir = 0;
	cfg.timeout = MPT_IOCTL_DEFAULT_TIMEOUT;
	if (mpt_config(ioc, &cfg) != 0) {
		pKarg->IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
		goto cim_get_raid_config_exit;
	}

	if (header.PageLength == 0) {
		pKarg->IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
		goto cim_get_raid_config_exit;
	}

	volumepage0sz = header.PageLength * 4;
	pVolume0 = pci_alloc_consistent(ioc->pcidev, volumepage0sz,
	    &volume0_dma);
	if (!pVolume0) {
		pKarg->IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
		goto cim_get_raid_config_exit;
	}

	cfg.action = MPI_CONFIG_ACTION_PAGE_READ_CURRENT;
	cfg.physAddr = volume0_dma;
	if (mpt_config(ioc, &cfg) != 0) {
		pKarg->IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
		goto cim_get_raid_config_exit;
	}

	totalMaxLBA = (u64)le32_to_cpu(pVolume0->MaxLBA) |
	    ((u64)le32_to_cpu(pVolume0->MaxLBAHigh)) << 32;
	tmpTotalMaxLBA = totalMaxLBA + 1;
	do_div(tmpTotalMaxLBA, 2048);
	pKarg->Configuration.bDriveCount = 0;
	pKarg->Configuration.uCapacity = tmpTotalMaxLBA;
	pKarg->Configuration.uStripeSize =
		le32_to_cpu(pVolume0->StripeSize)/2;

	switch(pVolume0->VolumeType) {
	case MPI_RAID_VOL_TYPE_IS:
		pKarg->Configuration.bRaidType = CSMI_SAS_RAID_TYPE_0;
		break;
	case MPI_RAID_VOL_TYPE_IME:
		pKarg->Configuration.bRaidType = CSMI_SAS_RAID_TYPE_10;
		break;
	case MPI_RAID_VOL_TYPE_IM:
		pKarg->Configuration.bRaidType = CSMI_SAS_RAID_TYPE_1;
		break;
	default:
		pKarg->Configuration.bRaidType = CSMI_SAS_RAID_TYPE_OTHER;
		break;
	}

	switch (pVolume0->VolumeStatus.State) {
	case MPI_RAIDVOL0_STATUS_STATE_OPTIMAL:
		pKarg->Configuration.bStatus = CSMI_SAS_RAID_SET_STATUS_OK;
		break;
	case MPI_RAIDVOL0_STATUS_STATE_DEGRADED:
		/* Volume is degraded, check if Resyncing or Inactive */
		pKarg->Configuration.bStatus = CSMI_SAS_RAID_SET_STATUS_DEGRADED;
		break;
	case MPI_RAIDVOL0_STATUS_STATE_FAILED:
		pKarg->Configuration.bStatus = CSMI_SAS_RAID_SET_STATUS_FAILED;
		break;
	}

	/* check flags */
	if (pVolume0->VolumeStatus.Flags &
	    MPI_RAIDVOL0_STATUS_FLAG_VOLUME_INACTIVE)
		pKarg->Configuration.bStatus = CSMI_SAS_RAID_SET_STATUS_OFFLINE;
	else if (pVolume0->VolumeStatus.Flags &
	    MPI_RAIDVOL0_STATUS_FLAG_RESYNC_IN_PROGRESS)
		pKarg->Configuration.bStatus = CSMI_SAS_RAID_SET_STATUS_REBUILDING;

	pKarg->Configuration.bInformation = 0;  /* default */
	if(pVolume0->VolumeStatus.Flags &
	    MPI_RAIDVOL0_STATUS_FLAG_RESYNC_IN_PROGRESS ) {

		uint64_t 	* ptrUint64;
		uint64_t	totalBlocks64, blocksRemaining64;
		uint32_t	totalBlocks32, blocksRemaining32;

		/* get percentage complete */
		pRaidActionReply = kmalloc( sizeof(MPI_RAID_VOL_INDICATOR) +
		    offsetof(MSG_RAID_ACTION_REPLY,ActionData),
		    GFP_KERNEL);

		if (!pRaidActionReply){
			printk(KERN_ERR "%s@%d::%s() - "
			    "Unable to malloc @ %p\n",
			    __FILE__, __LINE__, __FUNCTION__,pKarg);
			goto cim_get_raid_config_exit;
		}
		memset(pRaidActionReply, 0, sizeof(*pRaidActionReply));

		csmisas_do_raid(ioc,
		    MPI_RAID_ACTION_INDICATOR_STRUCT,
		    0, VolumeBus, volumeID, pRaidActionReply);

		ptrUint64       = (uint64_t *)&pRaidActionReply->ActionData;
		totalBlocks64     = *ptrUint64;
		ptrUint64++;
		blocksRemaining64 = *ptrUint64;
		while(totalBlocks64 > 0xFFFFFFFFUL){
			totalBlocks64 = totalBlocks64 >> 1;
			blocksRemaining64 = blocksRemaining64 >> 1;
		}
		totalBlocks32 = (uint32_t)totalBlocks64;
		blocksRemaining32 = (uint32_t)blocksRemaining64;

		if(totalBlocks32)
			pKarg->Configuration.bInformation =
			    (totalBlocks32 - blocksRemaining32) /
			    (totalBlocks32 / 100);

		kfree(pRaidActionReply);
	}

	/* fill-in more information depending on data type */
	if (pKarg->Configuration.bDataType ==
	    CSMI_SAS_RAID_DATA_ADDITIONAL_DATA) {
		pKarg->Configuration.Data->bLabel[0] = '\0';
		pKarg->Configuration.Data->bRaidSetLun[1] = 0;
		pKarg->Configuration.Data->bWriteProtection =
			CSMI_SAS_RAID_SET_WRITE_PROTECT_UNKNOWN;
		pKarg->Configuration.Data->bCacheSetting =
			CSMI_SAS_RAID_SET_CACHE_UNKNOWN;
		pKarg->Configuration.Data->bCacheRatio = 0;
		pKarg->Configuration.Data->usBlockSize = 512;
		pKarg->Configuration.Data->ulRaidSetExtentOffset.uLowPart = 0;
		pKarg->Configuration.Data->ulRaidSetExtentOffset.uHighPart = 0;
		pKarg->Configuration.Data->ulRaidSetBlocks.uLowPart =
		    le32_to_cpu(pVolume0->MaxLBA);
		pKarg->Configuration.Data->ulRaidSetBlocks.uHighPart =
		    le32_to_cpu(pVolume0->MaxLBAHigh);
		if (pVolume0->VolumeType == MPI_RAID_VOL_TYPE_IS ||
		    pVolume0->VolumeType == MPI_RAID_VOL_TYPE_IME ) {
			pKarg->Configuration.Data->uStripeSizeInBlocks =
			    le32_to_cpu(pVolume0->StripeSize);
		} else {
			pKarg->Configuration.Data->uStripeSizeInBlocks = 0;
		}
		pKarg->Configuration.Data->uSectorsPerTrack = 128;
		for (i=0; i<16; i++) {
			// unsupported
			pKarg->Configuration.Data->bApplicationScratchPad[i] =
			    0xFF;
		}
		pKarg->Configuration.Data->uNumberOfHeads = 16;

		tmpTotalMaxLBA = totalMaxLBA;
		do_div(tmpTotalMaxLBA,
		    (pKarg->Configuration.Data->uNumberOfHeads *
		     pKarg->Configuration.Data->uSectorsPerTrack));
		pKarg->Configuration.Data->uNumberOfTracks = tmpTotalMaxLBA;
	} else if ( pKarg->Configuration.bDataType ==
	    CSMI_SAS_RAID_DATA_DEVICE_ID ) {
		/* Send inquiry to get VPD Page 0x83 */
		u32 vpd_page_sz;
		vpd_page_sz = csmi_sas_raid_config_buffer_sz -
		    offsetof(CSMI_SAS_RAID_CONFIG,DeviceId);
		if (csmisas_raid_inq(ioc, MPI_FUNCTION_SCSI_IO_REQUEST,
			VolumeBus, volumeID, 0x83,
			(u8*)&pKarg->Configuration.DeviceId->bDeviceIdentificationVPDPage,
			vpd_page_sz) != 0) {
			pKarg->IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
			goto cim_get_raid_config_exit;
		}
	} else {
		/* suppress drive information */
		if (pKarg->Configuration.bDriveCount ==
			CSMI_SAS_RAID_DRIVE_COUNT_SUPRESSED) {
			pKarg->IoctlHeader.ReturnCode = CSMI_SAS_STATUS_SUCCESS;
			goto cim_get_raid_config_exit;
		}
	}

	/* get hotspare info, used later in this function */
	if (pVolume0->VolumeSettings.HotSparePool) {
		/* Read and save IOC Page 5
		 */
		header.PageVersion = 0;
		header.PageLength = 0;
		header.PageNumber = 5;
		header.PageType = MPI_CONFIG_PAGETYPE_IOC;
		cfg.cfghdr.hdr = &header;
		cfg.physAddr = -1;
		cfg.pageAddr = 0;
		cfg.action = MPI_CONFIG_ACTION_PAGE_HEADER;
		cfg.dir = 0;
		cfg.timeout = MPT_IOCTL_DEFAULT_TIMEOUT;
		if ((mpt_config(ioc, &cfg) == 0) && (header.PageLength)) {
			ioc_page5_sz = header.PageLength * 4;
			pIocPage5 = pci_alloc_consistent(ioc->pcidev,
			    ioc_page5_sz,
			    &ioc_page5_dma);
			memset(pIocPage5,0,ioc_page5_sz);
			if (ioc_page5_dma) {
				cfg.physAddr = ioc_page5_dma;
				cfg.action =
				    MPI_CONFIG_ACTION_PAGE_READ_CURRENT;
				mpt_config(ioc, &cfg);
			}
		}
	}

	/*
	 * get RAID Physical Disk Page 0
	 */
	header.PageVersion = 0;
	header.PageLength = 0;
	header.PageNumber = 0;
	header.PageType = MPI_CONFIG_PAGETYPE_RAID_PHYSDISK;
	cfg.cfghdr.hdr = &header;
	cfg.physAddr = -1;
	cfg.pageAddr = 0;
	cfg.action = MPI_CONFIG_ACTION_PAGE_HEADER;
	cfg.dir = 0;
	cfg.timeout = MPT_IOCTL_DEFAULT_TIMEOUT;
	if (mpt_config(ioc, &cfg) != 0) {
		pKarg->IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
		goto cim_get_raid_config_exit;
	}

	if (header.PageLength == 0) {
		pKarg->IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
		goto cim_get_raid_config_exit;
	}

	physdiskpage0sz = header.PageLength * 4;
	pPhysDisk0 = pci_alloc_consistent(ioc->pcidev, physdiskpage0sz,
	    &physdisk0_dma);
	if (!pPhysDisk0) {
		pKarg->IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
		goto cim_get_raid_config_exit;
	}
	cfg.physAddr = physdisk0_dma;

	physDiskNumMax = (csmi_sas_raid_config_buffer_sz -
	    offsetof(CSMI_SAS_RAID_CONFIG,Drives))
	    / sizeof(CSMI_SAS_RAID_DRIVES);

	tmpTotalMaxLBA = totalMaxLBA;
	if (pVolume0->VolumeType == MPI_RAID_VOL_TYPE_IS) {
		do_div(tmpTotalMaxLBA, pVolume0->NumPhysDisks);
		dcsmisasprintk(ioc, printk(KERN_DEBUG "IS Volume tmpTotalMaxLBA=%llX\n",
		(unsigned long long)tmpTotalMaxLBA));
	}
	else if (pVolume0->VolumeType == MPI_RAID_VOL_TYPE_IME) {
		do_div(tmpTotalMaxLBA, pVolume0->NumPhysDisks * 2);
		dcsmisasprintk(ioc, printk(KERN_DEBUG "IME Volume tmpTotalMaxLBA=%llX\n",
		(unsigned long long)tmpTotalMaxLBA));
	} else {
		dcsmisasprintk(ioc, printk(KERN_DEBUG "IM Volume tmpTotalMaxLBA=%llX\n",
		(unsigned long long)tmpTotalMaxLBA));
	}

	for (i=0; i< min(pVolume0->NumPhysDisks, physDiskNumMax); i++) {

		physDiskNum = pVolume0->PhysDisk[i].PhysDiskNum;
		cfg.action = MPI_CONFIG_ACTION_PAGE_READ_CURRENT;
		cfg.pageAddr = physDiskNum;
		if (mpt_config(ioc, &cfg) != 0){
			pKarg->IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
			goto cim_get_raid_config_exit;
		}

	        pKarg->Configuration.bDriveCount++;
		if (pKarg->Configuration.bDataType != CSMI_SAS_RAID_DATA_DRIVES)
			continue;

		/* Search the list for the matching SAS address. */
		sas_info = csmisas_get_device_component_by_fw(ioc, pPhysDisk0->PhysDiskBus,
		    pPhysDisk0->PhysDiskID);
		if (sas_info) {
			sas_address = reverse_byte_order64(sas_info->sas_address);
			memcpy(pKarg->Configuration.Drives[i].bSASAddress,
			   &sas_address,sizeof(u64));
			if (!device_info)
				device_info = sas_info->device_info;
		}

		memcpy(pKarg->Configuration.Drives[i].bModel,
		    pPhysDisk0->InquiryData.VendorID,
		    offsetof(RAID_PHYS_DISK0_INQUIRY_DATA,ProductRevLevel));
		memcpy(pKarg->Configuration.Drives[i].bFirmware,
			pPhysDisk0->InquiryData.ProductRevLevel,
			sizeof(pPhysDisk0->InquiryData.ProductRevLevel));
		if (csmisas_is_sata(pPhysDisk0)) {
			memcpy(&pKarg->Configuration.Drives[i].bSerialNumber,
				&pPhysDisk0->ExtDiskIdentifier[4],
				4);
			memcpy(&pKarg->Configuration.Drives[i].bSerialNumber[4],
				&pPhysDisk0->DiskIdentifier,
				sizeof(pPhysDisk0->DiskIdentifier));
		} else {
			memcpy(pKarg->Configuration.Drives[i].bSerialNumber,
				pPhysDisk0->DiskIdentifier,
				sizeof(pPhysDisk0->DiskIdentifier));
		}

		pKarg->Configuration.Drives[i].bDriveUsage =
		    (pPhysDisk0->PhysDiskStatus.Flags &
		    MPI_PHYSDISK0_STATUS_FLAG_INACTIVE_VOLUME) ?
		    CSMI_SAS_DRIVE_CONFIG_NOT_USED :
		    CSMI_SAS_DRIVE_CONFIG_MEMBER;

		pKarg->Configuration.Drives[i].bDriveStatus =
		    CSMI_SAS_DRIVE_STATUS_OK;
		if (pPhysDisk0->PhysDiskStatus.State ==
		    MPI_PHYSDISK0_STATUS_OFFLINE_REQUESTED) {
			pKarg->Configuration.Drives[i].bDriveStatus =
			    CSMI_SAS_DRIVE_STATUS_OFFLINE;
		} else if(pPhysDisk0->PhysDiskStatus.State) {
			pKarg->Configuration.Drives[i].bDriveStatus =
			    CSMI_SAS_DRIVE_STATUS_FAILED;
			if(pKarg->Configuration.bStatus ==
			    CSMI_SAS_RAID_SET_STATUS_DEGRADED)
				pKarg->Configuration.bInformation = i;
		} else if((pVolume0->VolumeStatus.Flags &
		    MPI_RAIDVOL0_STATUS_FLAG_RESYNC_IN_PROGRESS) &&
		    (pPhysDisk0->PhysDiskStatus.Flags &
		    MPI_PHYSDISK0_STATUS_FLAG_OUT_OF_SYNC))
			pKarg->Configuration.Drives[i].bDriveStatus =
			    CSMI_SAS_DRIVE_STATUS_REBUILDING;
		else if(pPhysDisk0->ErrorData.SmartCount ||
		    (pPhysDisk0->PhysDiskStatus.Flags &
		    MPI_PHYSDISK0_STATUS_FLAG_OUT_OF_SYNC))
			pKarg->Configuration.Drives[i].bDriveStatus =
			CSMI_SAS_DRIVE_STATUS_DEGRADED;

		memset(pKarg->Configuration.Drives[i].bSASLun,
		    0, sizeof(pKarg->Configuration.Drives[i].bSASLun));
		if (csmisas_is_sata(pPhysDisk0)) {
			pKarg->Configuration.Drives[i].bDriveType =
			CSMI_SAS_DRIVE_TYPE_SATA;
		} else { /* drive in a volume can only be SAS/SATA */
			pKarg->Configuration.Drives[i].bDriveType =
				CSMI_SAS_DRIVE_TYPE_SINGLE_PORT_SAS;
			if (mpt_raid_phys_disk_get_num_paths(ioc,
			    pVolume0->PhysDisk[i].PhysDiskNum) > 1)
					pKarg->Configuration.Drives[i].bDriveType =
					    CSMI_SAS_DRIVE_TYPE_DUAL_PORT_SAS;
		}

		pKarg->Configuration.Drives[i].usBlockSize = 512;
			pKarg->Configuration.Drives[i].uDriveIndex =
			    pPhysDisk0->PhysDiskNum;
		pKarg->Configuration.Drives[i].ulTotalUserBlocks.uLowPart =
		    (u32)tmpTotalMaxLBA;
		pKarg->Configuration.Drives[i].ulTotalUserBlocks.uHighPart =
		    (u32)(tmpTotalMaxLBA >> 32);
	}

	/* adding hot spare info at the end */
	if ((pVolume0->VolumeSettings.HotSparePool) && (pIocPage5) &&
	    (pVolume0->VolumeType != MPI_RAID_VOL_TYPE_IS)) {
		for (idx = 0, i = pVolume0->NumPhysDisks ;
		    idx < pIocPage5->NumHotSpares ; idx++) {
			if (i >= physDiskNumMax)
				break;
			if ((pVolume0->VolumeSettings.HotSparePool &
			    pIocPage5->HotSpare[idx].HotSparePool) == 0)
				continue;
			if(pIocPage5->HotSpare[idx].Flags !=
			    MPI_IOC_PAGE_5_HOT_SPARE_ACTIVE)
			    continue;
			physDiskNum = pIocPage5->HotSpare[idx].PhysDiskNum;
			cfg.action = MPI_CONFIG_ACTION_PAGE_READ_CURRENT;
			cfg.pageAddr = physDiskNum;
			if (mpt_config(ioc, &cfg) != 0)
				continue;

			/* don't mix SSP hot spare
			 * in SATA volume
			 */
			if (!csmisas_is_sata(pPhysDisk0) &&
			    (device_info &
			    MPI_SAS_DEVICE_INFO_SATA_DEVICE))
				continue;

			/* don't mix SATA hot spare
			 * in SSP volume
			 */
			if (csmisas_is_sata(pPhysDisk0) &&
			    (device_info &
			    MPI_SAS_DEVICE_INFO_SSP_TARGET))
				continue;

			/* capacity check for IM volumes*/
			if ((pVolume0->VolumeType ==
			    MPI_RAID_VOL_TYPE_IM) &&
			    (totalMaxLBA +
			    (64*2*1024) /* metadata = 64MB*/ >
			    le32_to_cpu(pPhysDisk0->MaxLBA)))
				continue;

			tmpTotalMaxLBA = totalMaxLBA;
			do_div(tmpTotalMaxLBA, pVolume0->NumPhysDisks);
			/* capacity check for IME volumes*/
			if ((pVolume0->VolumeType ==
			    MPI_RAID_VOL_TYPE_IME) &&
			    (((totalMaxLBA +
			    pVolume0->NumPhysDisks) * 2) +
			    (64*2*1024 ) /*metadata = 64MB*/ >
			    le32_to_cpu(pPhysDisk0->MaxLBA)))
				continue;

		        pKarg->Configuration.bDriveCount++;
			if (pKarg->Configuration.bDataType !=
			    CSMI_SAS_RAID_DATA_DRIVES) {
				i++;
				continue;
			}

			/* Search the list for the matching SAS address. */
			sas_info = csmisas_get_device_component_by_fw(ioc,
			    pPhysDisk0->PhysDiskBus, pPhysDisk0->PhysDiskID);
			if (sas_info) {
				sas_address = reverse_byte_order64(sas_info->sas_address);
				memcpy(pKarg->Configuration.Drives[i].bSASAddress,
				   &sas_address,sizeof(u64));
			}

			memcpy(pKarg->Configuration.Drives[i].bModel,
			    pPhysDisk0->InquiryData.VendorID,
			    offsetof(RAID_PHYS_DISK0_INQUIRY_DATA,ProductRevLevel));
			memcpy(pKarg->Configuration.Drives[i].bFirmware,
				pPhysDisk0->InquiryData.ProductRevLevel,
				sizeof(pPhysDisk0->InquiryData.ProductRevLevel));
			if (csmisas_is_sata(pPhysDisk0)) {
				memcpy(&pKarg->Configuration.Drives[i].bSerialNumber,
					&pPhysDisk0->ExtDiskIdentifier[4],
					4);
				memcpy(&pKarg->Configuration.Drives[i].bSerialNumber[4],
					&pPhysDisk0->DiskIdentifier,
					sizeof(pPhysDisk0->DiskIdentifier));
			} else {
				memcpy(pKarg->Configuration.Drives[i].bSerialNumber,
					pPhysDisk0->DiskIdentifier,
					sizeof(pPhysDisk0->DiskIdentifier));
			}
			pKarg->Configuration.Drives[i].bDriveStatus =
			    CSMI_SAS_DRIVE_STATUS_OK;
			if(pPhysDisk0->PhysDiskStatus.State)
				pKarg->Configuration.Drives[i].bDriveStatus =
				    CSMI_SAS_DRIVE_STATUS_FAILED;
			else if(pPhysDisk0->ErrorData.SmartCount)
				pKarg->Configuration.Drives[i].bDriveStatus =
				    CSMI_SAS_DRIVE_STATUS_DEGRADED;
			pKarg->Configuration.Drives[i].bDriveUsage =
			    CSMI_SAS_DRIVE_CONFIG_SPARE;
			pKarg->Configuration.Drives[i].usBlockSize = 512;
			pKarg->Configuration.Drives[i].uDriveIndex =
			    pPhysDisk0->PhysDiskNum;
			if (csmisas_is_sata(pPhysDisk0)) {
				pKarg->Configuration.Drives[i].bDriveType =
				CSMI_SAS_DRIVE_TYPE_SATA;
			} else { /* drive in a volume can only be SAS/SATA */
				pKarg->Configuration.Drives[i].bDriveType =
					CSMI_SAS_DRIVE_TYPE_SINGLE_PORT_SAS;
				if (mpt_raid_phys_disk_get_num_paths(ioc,
				    pVolume0->PhysDisk[i].PhysDiskNum) > 1)
					pKarg->Configuration.Drives[i].bDriveType =
					    CSMI_SAS_DRIVE_TYPE_DUAL_PORT_SAS;
			}
			i++;
		}
	}

	// Only return data on the first 240 drives
	if( pKarg->Configuration.bDriveCount > 0xF0 )
		pKarg->Configuration.bDriveCount =
		    CSMI_SAS_RAID_DRIVE_COUNT_TOO_BIG;

	pKarg->IoctlHeader.ReturnCode = CSMI_SAS_STATUS_SUCCESS;

 cim_get_raid_config_exit:

	if (pVolume0 != NULL)
		pci_free_consistent(ioc->pcidev, volumepage0sz, pVolume0,
		    volume0_dma);

	if(pPhysDisk0 != NULL)
		pci_free_consistent(ioc->pcidev, physdiskpage0sz, pPhysDisk0,
		    physdisk0_dma);

	if(pIocPage5 != NULL)
		pci_free_consistent(ioc->pcidev, ioc_page5_sz, pIocPage5,
		    ioc_page5_dma);

	/* Copy the data from kernel memory to user memory
	 */

	/* find the buffer size to copy depending on how much is filled-in */
	switch (pKarg->Configuration.bDataType) {
	case CSMI_SAS_RAID_DATA_ADDITIONAL_DATA:
		copy_buffer_sz = sizeof(IOCTL_HEADER) +
		    offsetof(CSMI_SAS_RAID_CONFIG,Data) +
		    sizeof(CSMI_SAS_RAID_SET_ADDITIONAL_DATA);
		break;
	case CSMI_SAS_RAID_DATA_DRIVES:
		if (pKarg->Configuration.bDriveCount ==
		    CSMI_SAS_RAID_DRIVE_COUNT_SUPRESSED)
			copy_buffer_sz = sizeof(IOCTL_HEADER) +
			    offsetof(CSMI_SAS_RAID_CONFIG,Drives);
	        else
			copy_buffer_sz = sizeof(IOCTL_HEADER) +
			    offsetof(CSMI_SAS_RAID_CONFIG,Drives) +
			    (pKarg->Configuration.bDriveCount *
			    sizeof(CSMI_SAS_RAID_DRIVES));
		break;
	case CSMI_SAS_RAID_DATA_DEVICE_ID:
		copy_buffer_sz = csmi_sas_raid_config_buffer_sz;
		break;
	}

	if (copy_to_user(uarg, pKarg, copy_buffer_sz)) {
		printk(KERN_ERR "%s@%d::%s() - "
		       "Unable to write out csmi_sas_get_raid_config @ %p\n",
		   	   __FILE__, __LINE__, __FUNCTION__, uarg);
		free_pages((unsigned long)pKarg, memory_pages);
		return -EFAULT;
	}

	dcsmisasprintk(ioc, printk(KERN_DEBUG "%s exit.\n",__FUNCTION__));
	free_pages((unsigned long)pKarg, memory_pages);
	return 0;
}

/**
 * Prototype Routine for the CSMI SAS Get RAID Features command.
 *
 * Outputs:	None.
 * Return:	0 if successful
 *		-EFAULT if data unavailable
 *		-ENODEV if no such device/adapter
 **/
static int
csmisas_get_raid_features(unsigned long arg)
{
	CSMI_SAS_RAID_FEATURES_BUFFER __user *uarg = (void __user *) arg;
	CSMI_SAS_RAID_FEATURES_BUFFER karg, *pKarg=NULL;
	int csmi_sas_raid_features_buffer_sz, iocnum;
	int				memory_pages;
	MPT_ADAPTER		*ioc = NULL;

	if (copy_from_user(&karg, uarg, sizeof(IOCTL_HEADER))) {
		printk(KERN_ERR "%s@%d::%s() - "
		    "Unable to read in csmi_sas_get_raid_features struct @ %p\n",
		    __FILE__, __LINE__, __FUNCTION__, uarg);
		return -EFAULT;
	}

	csmi_sas_raid_features_buffer_sz = karg.IoctlHeader.Length;
	memory_pages = get_order(csmi_sas_raid_features_buffer_sz);
	pKarg = (CSMI_SAS_RAID_FEATURES_BUFFER *)__get_free_pages(
		GFP_KERNEL, memory_pages);
	if (!pKarg){
		printk(KERN_ERR "%s@%d::%s() - "
		    "Unable to malloc RAID_FEATURES_BUFFER "
			"csmi_sas_raid_features_buffer_sz=%d memory_pages=%d\n",
			__FILE__, __LINE__, __FUNCTION__, 
			csmi_sas_raid_features_buffer_sz, memory_pages);
		return -ENOMEM;
	}
	memset(pKarg, 0, sizeof(*pKarg));

	if (copy_from_user(pKarg, uarg, csmi_sas_raid_features_buffer_sz)) {
		printk(KERN_ERR "%s@%d::%s() - "
		    "Unable to read in csmi_sas_get_raid_features struct @ %p\n",
		    __FILE__, __LINE__, __FUNCTION__, uarg);
		free_pages((unsigned long)pKarg, memory_pages);
		return -EFAULT;
	}

	if (((iocnum = mpt_verify_adapter(pKarg->IoctlHeader.IOControllerNumber,
	    &ioc)) < 0) || (ioc == NULL)) {
		printk(KERN_ERR "%s::%s() @%d - ioc%d not found!\n",
		    __FILE__, __FUNCTION__, __LINE__, iocnum);
		free_pages((unsigned long)pKarg, memory_pages);
		return -ENODEV;
	}

	if (!csmisas_is_this_sas_cntr(ioc)) {
		printk(KERN_ERR "%s::%s() @%d - ioc%d not SAS controller!\n",
		    __FILE__, __FUNCTION__, __LINE__, iocnum);
		free_pages((unsigned long)pKarg, memory_pages);
		return -ENODEV;
	}

	dcsmisasprintk(ioc, printk(KERN_DEBUG "%s enter.\n",__FUNCTION__));

	if (pKarg->Information.uChangeCount != 0 &&
	    pKarg->Information.uChangeCount != ioc->csmi_change_count ) {
		pKarg->IoctlHeader.ReturnCode =
		    CSMI_SAS_STATUS_INVALID_PARAMETER;
		pKarg->Information.uFailureCode =
		    CSMI_SAS_FAIL_CODE_CHANGE_COUNT_INVALID;
		goto cim_get_raid_features_exit;
	}

	pKarg->Information.uFeatures = CSMI_SAS_RAID_FEATURE_REBUILD |
	    CSMI_SAS_RAID_FEATURE_SURFACE_SCAN |
	    CSMI_SAS_RAID_FEATURE_SPARES_SHARED;
	pKarg->Information.bDefaultTransformPriority =
	    CSMI_SAS_PRIORITY_UNKNOWN;
	pKarg->Information.bTransformPriority = CSMI_SAS_PRIORITY_UNKNOWN;
	pKarg->Information.bDefaultRebuildPriority = CSMI_SAS_PRIORITY_UNKNOWN;
	pKarg->Information.bRebuildPriority =
	    pKarg->Information.bDefaultRebuildPriority;
	pKarg->Information.bDefaultSurfaceScanPriority =
	    CSMI_SAS_PRIORITY_UNKNOWN;
	pKarg->Information.bSurfaceScanPriority = CSMI_SAS_PRIORITY_UNKNOWN;
	pKarg->Information.uRaidSetTransformationRules = 0;

	 /* IS */
	pKarg->Information.RaidType[0].bRaidType = CSMI_SAS_RAID_TYPE_0;
	pKarg->Information.RaidType[0].uSupportedStripeSizeMap = 0x80;

	/* IM */
	pKarg->Information.RaidType[1].bRaidType = CSMI_SAS_RAID_TYPE_1;
	pKarg->Information.RaidType[1].uSupportedStripeSizeMap = 0;

	/* IME */
	pKarg->Information.RaidType[2].bRaidType = CSMI_SAS_RAID_TYPE_1E;
	pKarg->Information.RaidType[2].uSupportedStripeSizeMap = 0x80;

	pKarg->Information.RaidType[3].bRaidType = CSMI_SAS_RAID_TYPE_END;
	pKarg->Information.bCacheRatiosSupported[0] =
	    CSMI_SAS_RAID_CACHE_RATIO_END;

 cim_get_raid_features_exit:

	/*
	 * Copy the data from kernel memory to user memory
	 */
	if (copy_to_user(uarg, pKarg,
	    sizeof(CSMI_SAS_RAID_FEATURES_BUFFER))) {
		printk(KERN_ERR "%s@%d::%s() - "
		"Unable to write out csmi_sas_get_raid_features @ %p\n",
		__FILE__, __LINE__, __FUNCTION__, uarg);
		free_pages((unsigned long)pKarg, memory_pages);
		return -EFAULT;
	}

	dcsmisasprintk(ioc, printk(KERN_DEBUG "%s exit.\n",__FUNCTION__));
	free_pages((unsigned long)pKarg, memory_pages);
	return 0;
}

/**
 * Prototype Routine for the CSMI SAS Set RAID Control command.
 *
 * Outputs:	None.
 * Return:	0 if successful
 *		-EFAULT if data unavailable
 *		-ENODEV if no such device/adapter
 **/
static int
csmisas_set_raid_control(unsigned long arg)
{
	CSMI_SAS_RAID_CONTROL_BUFFER __user *uarg = (void __user *) arg;
	CSMI_SAS_RAID_CONTROL_BUFFER karg, *pKarg=NULL;
	int csmi_sas_raid_control_buffer_sz, iocnum;
	int				memory_pages;
	MPT_ADAPTER	*ioc = NULL;

	if (copy_from_user(&karg, uarg, sizeof(IOCTL_HEADER))) {
		printk(KERN_ERR "%s@%d::%s() - "
		    "Unable to read in csmi_sas_set_raid_control struct @ %p\n",
		    __FILE__, __LINE__, __FUNCTION__, uarg);
		return -EFAULT;
	}

	csmi_sas_raid_control_buffer_sz = karg.IoctlHeader.Length;
	memory_pages = get_order(csmi_sas_raid_control_buffer_sz);
	pKarg = (CSMI_SAS_RAID_CONTROL_BUFFER *)__get_free_pages(
		GFP_KERNEL, memory_pages);
	if (!pKarg){
		printk(KERN_ERR "%s@%d::%s() - "
		    "Unable to malloc RAID_CONTROL_BUFFER "
			"csmi_sas_raid_control_buffer_sz=%d memory_pages=%d\n",
			__FILE__, __LINE__, __FUNCTION__, 
			csmi_sas_raid_control_buffer_sz, memory_pages);
		return -ENOMEM;
	}
	memset(pKarg, 0, sizeof(*pKarg));

	if (copy_from_user(pKarg, uarg, csmi_sas_raid_control_buffer_sz)) {
		printk(KERN_ERR "%s@%d::%s() - "
		    "Unable to read in csmi_sas_set_raid_control struct @ %p\n",
		    __FILE__, __LINE__, __FUNCTION__, uarg);
		free_pages((unsigned long)pKarg, memory_pages);
		return -EFAULT;
	}

	if (((iocnum = mpt_verify_adapter(pKarg->IoctlHeader.IOControllerNumber,
	    &ioc)) < 0) || (ioc == NULL)) {
		printk(KERN_ERR "%s::%s() @%d - ioc%d not found!\n",
		    __FILE__, __FUNCTION__, __LINE__, iocnum);
		free_pages((unsigned long)pKarg, memory_pages);
		return -ENODEV;
	}

	if (!csmisas_is_this_sas_cntr(ioc)) {
		printk(KERN_ERR "%s::%s() @%d - ioc%d not SAS controller!\n",
		    __FILE__, __FUNCTION__, __LINE__, iocnum);
		free_pages((unsigned long)pKarg, memory_pages);
		return -ENODEV;
	}

	dcsmisasprintk(ioc, printk(KERN_DEBUG "%s enter.\n",__FUNCTION__));

	if (pKarg->Information.uChangeCount != 0 &&
		pKarg->Information.uChangeCount != ioc->csmi_change_count ) {
		pKarg->IoctlHeader.ReturnCode =
		    CSMI_SAS_STATUS_INVALID_PARAMETER;
		pKarg->Information.uFailureCode =
		    CSMI_SAS_FAIL_CODE_CHANGE_COUNT_INVALID;
		goto cim_set_raid_control_exit;
	}

	if (pKarg->Information.bTransformPriority !=
	    CSMI_SAS_PRIORITY_UNCHANGED) {
		pKarg->IoctlHeader.ReturnCode =
		    CSMI_SAS_STATUS_INVALID_PARAMETER;
		pKarg->Information.uFailureCode =
		    CSMI_SAS_FAIL_CODE_TRANSFORM_PRIORITY_INVALID;
		goto cim_set_raid_control_exit;
	}

	if (pKarg->Information.bRebuildPriority !=
	    CSMI_SAS_PRIORITY_AUTO &&
		pKarg->Information.bRebuildPriority !=
		CSMI_SAS_PRIORITY_UNCHANGED) {
		pKarg->IoctlHeader.ReturnCode =
		    CSMI_SAS_STATUS_INVALID_PARAMETER;
		pKarg->Information.uFailureCode =
		    CSMI_SAS_FAIL_CODE_REBUILD_PRIORITY_INVALID;
		goto cim_set_raid_control_exit;
	}

	if (pKarg->Information.bCacheRatioFlag ==
	    CSMI_SAS_RAID_CACHE_RATIO_DISABLE) {
		pKarg->IoctlHeader.ReturnCode =
		    CSMI_SAS_STATUS_INVALID_PARAMETER;
		pKarg->Information.uFailureCode =
		    CSMI_SAS_FAIL_CODE_CACHE_RATIO_INVALID;
		goto cim_set_raid_control_exit;
	}

	if( !strcmp(pKarg->Information.bClearConfiguration,
		CSMI_SAS_RAID_CLEAR_CONFIGURATION_SIGNATURE) ) {
		pKarg->IoctlHeader.ReturnCode =
			CSMI_SAS_STATUS_INVALID_PARAMETER;
		pKarg->Information.uFailureCode =
			CSMI_SAS_FAIL_CODE_CLEAR_CONFIGURATION_INVALID;
		goto cim_set_raid_control_exit;
	}

	pKarg->Information.bFailureDescription[0] = '\0';

 cim_set_raid_control_exit:

	/*
	 * Copy the data from kernel memory to user memory
	 */
	if (copy_to_user(uarg, pKarg,
		sizeof(CSMI_SAS_RAID_CONTROL_BUFFER))) {
		printk(KERN_ERR "%s@%d::%s() - "
		"Unable to write out csmi_sas_set_raid_control @ %p\n",
		__FILE__, __LINE__, __FUNCTION__, uarg);
		free_pages((unsigned long)pKarg, memory_pages);
		return -EFAULT;
	}

	dcsmisasprintk(ioc, printk(KERN_DEBUG "%s exit.\n",__FUNCTION__));
	free_pages((unsigned long)pKarg, memory_pages);
	return 0;
}

/**
 * Prototype Routine for the CSMI SAS Get Raid Element.
 *
 * Outputs:	None.
 * Return:	0 if successful
 *		-EFAULT if data unavailable
 *		-ENODEV if no such device/adapter
 **/
static int
csmisas_get_raid_element(unsigned long arg)
{
	CSMI_SAS_RAID_ELEMENT_BUFFER __user *uarg = (void __user *) arg;
	CSMI_SAS_RAID_ELEMENT_BUFFER	 karg;
	MPT_ADAPTER			*ioc = NULL;
	int				iocnum;

	if (copy_from_user(&karg, uarg, sizeof(CSMI_SAS_RAID_ELEMENT_BUFFER))) {
		printk(KERN_ERR "%s@%d::%s() - "
		    "Unable to read in csmisas_get_raid_element struct @ %p\n",
		    __FILE__, __LINE__, __FUNCTION__, uarg);
		return -EFAULT;
	}

	if (((iocnum = mpt_verify_adapter(karg.IoctlHeader.IOControllerNumber,
	    &ioc)) < 0) || (ioc == NULL)) {
		printk(KERN_ERR "%s::%s() @%d - ioc%d not found!\n",
		    __FILE__, __FUNCTION__, __LINE__, iocnum);
		return -ENODEV;
	}

	if (!csmisas_is_this_sas_cntr(ioc)) {
		printk(KERN_ERR "%s::%s() @%d - ioc%d not SAS controller!\n",
		    __FILE__, __FUNCTION__, __LINE__, iocnum);
		return -ENODEV;
	}

	dcsmisasprintk(ioc, printk(KERN_DEBUG "%s enter.\n",__FUNCTION__));

/* TODO - implement IOCTL here */
	karg.IoctlHeader.ReturnCode = CSMI_SAS_STATUS_BAD_CNTL_CODE;
	dcsmisasprintk(ioc, printk(KERN_DEBUG ": not implemented\n"));

// csmisas_get_raid_element_exit:

	/* Copy the data from kernel memory to user memory
	 */
	if (copy_to_user(uarg, &karg,
				sizeof(CSMI_SAS_RAID_ELEMENT_BUFFER))) {
		printk(KERN_ERR "%s@%d::%s() - "
			"Unable to write out csmisas_get_raid_element @ %p\n",
				__FILE__, __LINE__, __FUNCTION__, uarg);
		return -EFAULT;
	}

	dcsmisasprintk(ioc, printk(KERN_DEBUG "%s exit.\n",__FUNCTION__));
	return 0;

}

/**
 * Prototype Routine for the CSMI SAS Set Raid Operation
 *
 * Outputs:	None.
 * Return:	0 if successful
 *		-EFAULT if data unavailable
 *		-ENODEV if no such device/adapter
 **/
static int
csmisas_set_raid_operation(unsigned long arg)
{
	CSMI_SAS_RAID_SET_OPERATION_BUFFER __user *uarg = (void __user *) arg;
	CSMI_SAS_RAID_SET_OPERATION_BUFFER	 karg;
	MPT_ADAPTER			*ioc = NULL;
	int				iocnum;

	if (copy_from_user(&karg, uarg, sizeof(CSMI_SAS_RAID_SET_OPERATION_BUFFER))) {
		printk(KERN_ERR "%s@%d::%s() - "
		    "Unable to read in csmi_set_raid_operation struct @ %p\n",
		    __FILE__, __LINE__, __FUNCTION__, uarg);
		return -EFAULT;
	}

	if (((iocnum = mpt_verify_adapter(karg.IoctlHeader.IOControllerNumber,
	    &ioc)) < 0) || (ioc == NULL)) {
		printk(KERN_ERR "%s::%s() @%d - ioc%d not found!\n",
		    __FILE__, __FUNCTION__, __LINE__, iocnum);
		return -ENODEV;
	}

	if (!csmisas_is_this_sas_cntr(ioc)) {
		printk(KERN_ERR "%s::%s() @%d - ioc%d not SAS controller!\n",
		    __FILE__, __FUNCTION__, __LINE__, iocnum);
		return -ENODEV;
	}

	dcsmisasprintk(ioc, printk(KERN_DEBUG "%s enter.\n",__FUNCTION__));

/* TODO - implement IOCTL here */
	karg.IoctlHeader.ReturnCode = CSMI_SAS_STATUS_BAD_CNTL_CODE;
	dcsmisasprintk(ioc, printk(KERN_DEBUG ": not implemented\n"));

// cim_set_raid_operation:

	/* Copy the data from kernel memory to user memory
	 */
	if (copy_to_user(uarg, &karg,
				sizeof(CSMI_SAS_RAID_SET_OPERATION_BUFFER))) {
		printk(KERN_ERR "%s@%d::%s() - "
			"Unable to write out csmi_set_raid_operation @ %p\n",
				__FILE__, __LINE__, __FUNCTION__, uarg);
		return -EFAULT;
	}

	dcsmisasprintk(ioc, printk(KERN_DEBUG "%s exit.\n",__FUNCTION__));
	return 0;

}


/**
 * Prototype Routine for the CSMI SAS Task Managment Config command.
 *
 * Outputs:	None.
 * Return:	0 if successful
 *		-EFAULT if data unavailable
 *		-ENODEV if no such device/adapter
 **/
static int
csmisas_task_managment(unsigned long arg)
{
	CSMI_SAS_SSP_TASK_IU_BUFFER __user *uarg = (void __user *) arg;
	CSMI_SAS_SSP_TASK_IU_BUFFER	 karg;
	pSCSITaskMgmt_t			pScsiTm;
	pSCSITaskMgmtReply_t		pScsiTmReply;
	MPT_ADAPTER			*ioc = NULL;
	MPT_SCSI_HOST			*hd;
	MPT_FRAME_HDR			*mf = NULL;
	MPIHeader_t			*mpi_hdr;
	int				iocnum;
	u8				taskType;
	u8				channel;
	u8				id;
	u8				queueTag;
	u32				TaskMsgContext = 0;
	int				i;
	u8 				found_qtag;
	struct sas_device_info		*sas_info;
	u16				ioc_status;
	u32				MsgContext;

	if (copy_from_user(&karg, uarg, sizeof(CSMI_SAS_SSP_TASK_IU_BUFFER))) {
		printk(KERN_ERR "%s@%d::%s() - "
		    "Unable to read in csmi_sas_task_managment struct @ %p\n",
		    __FILE__, __LINE__, __FUNCTION__, uarg);
		return -EFAULT;
	}

	if (((iocnum = mpt_verify_adapter(karg.IoctlHeader.IOControllerNumber,
	    &ioc)) < 0) || (ioc == NULL)) {
		printk(KERN_ERR "%s::%s() @%d - ioc%d not found!\n",
		    __FILE__, __FUNCTION__, __LINE__, iocnum);
		return -ENODEV;
	}

	if (!csmisas_is_this_sas_cntr(ioc)) {
		printk(KERN_ERR "%s::%s() @%d - ioc%d not SAS controller!\n",
		    __FILE__, __FUNCTION__, __LINE__, iocnum);
		return -ENODEV;
	}

	dcsmisasprintk(ioc, printk(KERN_DEBUG "%s enter.\n",__FUNCTION__));

	karg.IoctlHeader.ReturnCode = CSMI_SAS_STATUS_INVALID_PARAMETER;

	sas_info = csmisas_get_device_component_by_os(ioc,
	    karg.Parameters.bPathId, karg.Parameters.bTargetId);
	if (!sas_info || sas_info->is_cached || sas_info->is_logical_volume)
		goto cim_get_task_managment_exit;

	channel = sas_info->fw.channel;
	id = sas_info->fw.id;
	queueTag = (u8)karg.Parameters.uQueueTag & 0xFF;
	hd = (MPT_SCSI_HOST *) ioc->sh->hostdata;

	/* try to catch an error
	 */
	if ((karg.Parameters.uFlags & CSMI_SAS_TASK_IU) &&
	    (karg.Parameters.uFlags & CSMI_SAS_HARD_RESET_SEQUENCE))
		goto cim_get_task_managment_exit;

	if (karg.Parameters.uFlags & CSMI_SAS_TASK_IU) {
		switch (karg.Parameters.bTaskManagementFunction) {

		case CSMI_SAS_SSP_ABORT_TASK:
			taskType = MPI_SCSITASKMGMT_TASKTYPE_ABORT_TASK;
			break;
		case CSMI_SAS_SSP_ABORT_TASK_SET:
			taskType = MPI_SCSITASKMGMT_TASKTYPE_ABRT_TASK_SET;
			break;
		case CSMI_SAS_SSP_CLEAR_TASK_SET:
			taskType = MPI_SCSITASKMGMT_TASKTYPE_CLEAR_TASK_SET;
			break;
		case CSMI_SAS_SSP_LOGICAL_UNIT_RESET:
			taskType = MPI_SCSITASKMGMT_TASKTYPE_LOGICAL_UNIT_RESET;
			break;
		case CSMI_SAS_SSP_CLEAR_ACA:
		case CSMI_SAS_SSP_QUERY_TASK:
		default:
			goto cim_get_task_managment_exit;
		}
	} else if (karg.Parameters.uFlags & CSMI_SAS_HARD_RESET_SEQUENCE)
		taskType = MPI_SCSITASKMGMT_TASKTYPE_TARGET_RESET;
	else
		goto cim_get_task_managment_exit;

	switch (karg.Parameters.uInformation) {
		case CSMI_SAS_SSP_TEST:
			dcsmisasprintk(ioc, printk(KERN_DEBUG "TM request for test purposes\n"));
			break;
		case CSMI_SAS_SSP_EXCEEDED:
			dcsmisasprintk(ioc, printk(KERN_DEBUG "TM request due to timeout\n"));
			break;
		case CSMI_SAS_SSP_DEMAND:
			dcsmisasprintk(ioc, printk(KERN_DEBUG "TM request demanded by app\n"));
			break;
		case CSMI_SAS_SSP_TRIGGER:
			dcsmisasprintk(ioc, printk(KERN_DEBUG "TM request sent to trigger event\n"));
			break;
	}

	switch (taskType) {

	case MPI_SCSITASKMGMT_TASKTYPE_ABORT_TASK:
	/*
	 * look up qtag in the ScsiLookup[] table
	 */
		for (i = 0, found_qtag = 0; i < hd->ioc->req_depth; i++) {
			if ((ioc->ScsiLookup[i]) &&
			    (ioc->ScsiLookup[i]->tag == queueTag)) {
				mf = MPT_INDEX_2_MFPTR(hd->ioc, i);
				TaskMsgContext =
				    mf->u.frame.hwhdr.msgctxu.MsgContext;
				found_qtag=1;
				break;
			}
		}

		if(!found_qtag)
			goto cim_get_task_managment_exit;

	case MPI_SCSITASKMGMT_TASKTYPE_LOGICAL_UNIT_RESET:
	case MPI_SCSITASKMGMT_TASKTYPE_ABRT_TASK_SET:
	case MPI_SCSITASKMGMT_TASKTYPE_CLEAR_TASK_SET:
	/* for now, this should work
	 */
	case MPI_SCSITASKMGMT_TASKTYPE_TARGET_RESET:

		/* Single threading ....
		 */
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,15))
		mutex_lock(&ioc->taskmgmt_cmds.mutex);
		if (mpt_set_taskmgmt_in_progress_flag(ioc) != 0) {
			mutex_unlock(&ioc->taskmgmt_cmds.mutex);
			karg.IoctlHeader.ReturnCode =
			    CSMI_SAS_STATUS_FAILED;
			goto cim_get_task_managment_exit;
		}
#elif (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,0))
		if (mptctl_set_tm_flags(hd) != 0) {
			karg.IoctlHeader.ReturnCode =
			    CSMI_SAS_STATUS_FAILED;
			goto cim_get_task_managment_exit;
		}
#endif
		/* Send request
		 */
		if ((mf = mpt_get_msg_frame(mptctl_taskmgmt_id, ioc)) == NULL) {
			dcsmisasprintk(ioc, printk(KERN_ERR ": no msg frames!\n"));
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,15))
			mutex_unlock(&ioc->taskmgmt_cmds.mutex);
			mpt_clear_taskmgmt_in_progress_flag(ioc);
#elif (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,0))
			mptctl_free_tm_flags(ioc);
#endif
			karg.IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
			goto cim_get_task_managment_exit;
		}

		mpi_hdr = (MPIHeader_t *) mf;
		MsgContext = mpi_hdr->MsgContext;
		pScsiTm = (pSCSITaskMgmt_t ) mf;

		memset(pScsiTm,0,sizeof(SCSITaskMgmt_t));
		pScsiTm->TaskType = taskType;
		pScsiTm->Bus = channel;
		pScsiTm->TargetID = id;
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,15))
		int_to_scsilun(karg.Parameters.bLun,
		    (struct scsi_lun *)pScsiTm->LUN);
#else
		pScsiTm->LUN[1] = karg.Parameters.bLun;
#endif
		pScsiTm->MsgContext = MsgContext;
		pScsiTm->TaskMsgContext = TaskMsgContext;
		pScsiTm->Function = MPI_FUNCTION_SCSI_TASK_MGMT;

		if (csmisas_send_handshake_wait(ioc, mf,
		    karg.IoctlHeader.Timeout) != 0)  {
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,15))
			mutex_unlock(&ioc->taskmgmt_cmds.mutex);
#endif
			karg.IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
			goto cim_get_task_managment_exit;
		}

		if (ioc->ioctl_cmds.status & MPT_MGMT_STATUS_RF_VALID) {

			pScsiTmReply =
			    (pSCSITaskMgmtReply_t ) ioc->ioctl_cmds.reply;

			ioc_status = le16_to_cpu(pScsiTmReply->IOCStatus)
			    & MPI_IOCSTATUS_MASK;

			memset(&karg.Status,0,
			    sizeof(CSMI_SAS_SSP_PASSTHRU_STATUS));

			if(ioc_status == MPI_IOCSTATUS_SUCCESS) {
				karg.IoctlHeader.ReturnCode =
				    CSMI_SAS_STATUS_SUCCESS;
				karg.Status.bSSPStatus =
				    CSMI_SAS_SSP_STATUS_COMPLETED;
			}else if(ioc_status == MPI_IOCSTATUS_INSUFFICIENT_RESOURCES) {
				karg.IoctlHeader.ReturnCode =
				    CSMI_SAS_STATUS_SUCCESS;
				karg.Status.bSSPStatus =
				    CSMI_SAS_SSP_STATUS_RETRY;
			}else {
				karg.IoctlHeader.ReturnCode =
				    CSMI_SAS_STATUS_FAILED;
				karg.Status.bSSPStatus =
				    CSMI_SAS_SSP_STATUS_FATAL_ERROR;
			}
		} else
			karg.IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;

		break;

	default:
		karg.IoctlHeader.ReturnCode = CSMI_SAS_STATUS_INVALID_PARAMETER;
		break;
	}

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,15))
	mutex_unlock(&ioc->taskmgmt_cmds.mutex);
#endif

 cim_get_task_managment_exit:

	/* Copy the data from kernel memory to user memory
	 */
	if (copy_to_user(uarg, &karg,
				sizeof(CSMI_SAS_SSP_TASK_IU_BUFFER))) {
		printk(KERN_ERR "%s@%d::%s() - "
			"Unable to write out csmi_sas_task_managment @ %p\n",
				__FILE__, __LINE__, __FUNCTION__, uarg);
		return -EFAULT;
	}

	dcsmisasprintk(ioc, printk(KERN_DEBUG "%s exit.\n",__FUNCTION__));
	return 0;
}

/**
 *	map_sas_status_to_csmi - Conversion  for Connection Status
 *	@mpi_sas_status: Sas status returned by the firmware
 *
 *	Returns converted connection status
 *
 **/
static u8
map_sas_status_to_csmi(u8 mpi_sas_status)
{
	u8  csmi_connect_status;

	switch (mpi_sas_status) {

	case MPI_SASSTATUS_SUCCESS:
		csmi_connect_status = CSMI_SAS_OPEN_ACCEPT;
		break;

	case MPI_SASSTATUS_UTC_BAD_DEST:
		csmi_connect_status = CSMI_SAS_OPEN_REJECT_BAD_DESTINATION;
		break;

	case MPI_SASSTATUS_UTC_CONNECT_RATE_NOT_SUPPORTED:
		csmi_connect_status = CSMI_SAS_OPEN_REJECT_RATE_NOT_SUPPORTED;
		break;

	case MPI_SASSTATUS_UTC_PROTOCOL_NOT_SUPPORTED:
		csmi_connect_status =
		    CSMI_SAS_OPEN_REJECT_PROTOCOL_NOT_SUPPORTED;
		break;

	case MPI_SASSTATUS_UTC_STP_RESOURCES_BUSY:
		csmi_connect_status = CSMI_SAS_OPEN_REJECT_STP_RESOURCES_BUSY;
		break;

	case MPI_SASSTATUS_UTC_WRONG_DESTINATION:
		csmi_connect_status = CSMI_SAS_OPEN_REJECT_WRONG_DESTINATION;
		break;

	case MPI_SASSTATUS_SDSF_NAK_RECEIVED:
		csmi_connect_status = CSMI_SAS_OPEN_REJECT_RETRY;
		break;

	case MPI_SASSTATUS_SDSF_CONNECTION_FAILED:
		csmi_connect_status = CSMI_SAS_OPEN_REJECT_PATHWAY_BLOCKED;
		break;

	case MPI_SASSTATUS_INITIATOR_RESPONSE_TIMEOUT:
		csmi_connect_status =  CSMI_SAS_OPEN_REJECT_NO_DESTINATION;
		break;

	case MPI_SASSTATUS_UNKNOWN_ERROR:
	case MPI_SASSTATUS_INVALID_FRAME:
	case MPI_SASSTATUS_UTC_BREAK_RECEIVED:
	case MPI_SASSTATUS_UTC_PORT_LAYER_REQUEST:
	case MPI_SASSTATUS_SHORT_INFORMATION_UNIT:
	case MPI_SASSTATUS_LONG_INFORMATION_UNIT:
	case MPI_SASSTATUS_XFER_RDY_INCORRECT_WRITE_DATA:
	case MPI_SASSTATUS_XFER_RDY_REQUEST_OFFSET_ERROR:
	case MPI_SASSTATUS_XFER_RDY_NOT_EXPECTED:
	case MPI_SASSTATUS_DATA_INCORRECT_DATA_LENGTH:
	case MPI_SASSTATUS_DATA_TOO_MUCH_READ_DATA:
	case MPI_SASSTATUS_DATA_OFFSET_ERROR:
		csmi_connect_status = CSMI_SAS_OPEN_REJECT_RESERVE_STOP;
		break;

	default:
		csmi_connect_status = CSMI_SAS_OPEN_REJECT_RESERVE_STOP;
		break;
	}

	return csmi_connect_status;
}

/**
 *                      csmisas_phy_reset
 *	Issues a phy link reset or phy hard reset
 *
 *	@ioc - Pointer to MPT_ADAPTER structure
 *	@PhyNum - phy number
 *	@opcode - {MPI_SAS_OP_PHY_LINK_RESET,MPI_SAS_OP_PHY_HARD_RESET}
 *
 *	Returns: 0 for success, non-zero error
 **/
static int
csmisas_phy_reset(MPT_ADAPTER *ioc, u8 PhyNum, u8 opcode)
{
	SasIoUnitControlRequest_t	*sasIoUnitCntrReq;
	SasIoUnitControlReply_t		*sasIoUnitCntrReply;
	MPT_FRAME_HDR			*mf = NULL;
	MPIHeader_t			*mpi_hdr;
	u16 				ioc_status;
	u32				MsgContext;

	if ((opcode != MPI_SAS_OP_PHY_LINK_RESET) &&
	    (opcode != MPI_SAS_OP_PHY_HARD_RESET))
	    return -1;

	/* Get a MF for this command.
	 */
	if ((mf = mpt_get_msg_frame(mptctl_id, ioc)) == NULL) {
		dcsmisasprintk(ioc, printk(KERN_ERR ": no msg frames!\n"));
		return -1;
        }

	mpi_hdr = (MPIHeader_t *) mf;
	MsgContext =  mpi_hdr->MsgContext;
	sasIoUnitCntrReq = (SasIoUnitControlRequest_t *)mf;
	memset(sasIoUnitCntrReq,0,sizeof(SasIoUnitControlRequest_t));
	sasIoUnitCntrReq->Function = MPI_FUNCTION_SAS_IO_UNIT_CONTROL;
	sasIoUnitCntrReq->MsgContext = MsgContext;
	sasIoUnitCntrReq->Operation = opcode;
	sasIoUnitCntrReq->PhyNum = PhyNum;

	if (csmisas_send_command_wait(ioc, mf, MPT_IOCTL_DEFAULT_TIMEOUT) != 0)
		return -1;

	if ((ioc->ioctl_cmds.status & MPT_MGMT_STATUS_RF_VALID) == 0)
		return -1;

	/* process the completed Reply Message Frame */
	sasIoUnitCntrReply = (SasIoUnitControlReply_t *)ioc->ioctl_cmds.reply;
	ioc_status = le16_to_cpu(sasIoUnitCntrReply->IOCStatus)
	    & MPI_IOCSTATUS_MASK;
	if (ioc_status != MPI_IOCSTATUS_SUCCESS) {
		printk(KERN_DEBUG "%s: IOCStatus=0x%X IOCLogInfo=0x%X\n",
		    __FUNCTION__,
		    sasIoUnitCntrReply->IOCStatus,
		    sasIoUnitCntrReply->IOCLogInfo);
		return -1;
	}
	return 0;
}

/** Prototype Routine for the CSMI SAS Phy Control command.
 *
 * Outputs:	None.
 * Return:	0 if successful
 *		-EFAULT if data unavailable
 *		-ENODEV if no such device/adapter
 **/
static int
csmisas_phy_control(unsigned long arg)
{
	CSMI_SAS_PHY_CONTROL_BUFFER __user *uarg = (void __user *) arg;
	IOCTL_HEADER			ioctl_header;
	PCSMI_SAS_PHY_CONTROL_BUFFER	karg;
	SasIOUnitPage0_t		*sasIoUnitPg0=NULL;
	dma_addr_t			sasIoUnitPg0_dma;
	int				sasIoUnitPg0_data_sz=0;
	SasIOUnitPage1_t		*sasIoUnitPg1=NULL;
	dma_addr_t			sasIoUnitPg1_dma;
	int				sasIoUnitPg1_data_sz=0;
	ConfigExtendedPageHeader_t  	hdr;
	CONFIGPARMS			cfg;
	MPT_ADAPTER			*ioc = NULL;
	int				iocnum;
	int 				csmi_sas_phy_control_buffer_sz;
	int				memory_pages;

	if (copy_from_user(&ioctl_header, uarg, sizeof(IOCTL_HEADER))) {
		printk(KERN_ERR "%s@%d::%s() - "
		    "Unable to read in IOCTL_HEADER"
		    "struct @ %p\n", __FILE__, __LINE__, __FUNCTION__, uarg);
		return -EFAULT;
	}

	csmi_sas_phy_control_buffer_sz = ioctl_header.Length;
	memory_pages = get_order(csmi_sas_phy_control_buffer_sz);
	karg = (PCSMI_SAS_PHY_CONTROL_BUFFER)__get_free_pages(
		GFP_KERNEL, memory_pages);
	if (!karg){
		printk(KERN_ERR "%s@%d::%s() - "
		    "Unable to malloc SAS_PHY_CONTROL_BUFFER "
			"csmi_sas_phy_control_buffer_sz=%d memory_pages=%d\n",
			__FILE__, __LINE__, __FUNCTION__, 
			csmi_sas_phy_control_buffer_sz, memory_pages);
		return -ENOMEM;
	}
	memset(karg, 0, sizeof(*karg));

	if (copy_from_user(karg, uarg, csmi_sas_phy_control_buffer_sz)) {
		printk(KERN_ERR "%s@%d::%s() - "
		    "Unable to read in csmi_sas_phy_control_buffer "
		    "struct @ %p\n", __FILE__, __LINE__, __FUNCTION__, uarg);
		free_pages((unsigned long)karg, memory_pages);
		return -EFAULT;
	}

	if (((iocnum = mpt_verify_adapter(ioctl_header.IOControllerNumber,
	    &ioc)) < 0) || (ioc == NULL)) {
		printk(KERN_ERR "%s::%s() @%d - ioc%d not found!\n",
		    __FILE__, __FUNCTION__, __LINE__, iocnum);
		free_pages((unsigned long)karg, memory_pages);
		return -ENODEV;
	}

	if (!csmisas_is_this_sas_cntr(ioc)) {
		printk(KERN_ERR "%s::%s() @%d - ioc%d not SAS controller!\n",
		    __FILE__, __FUNCTION__, __LINE__, iocnum);
		free_pages((unsigned long)karg, memory_pages);
		return -ENODEV;
	}

	dcsmisasprintk(ioc, printk(KERN_DEBUG "%s enter.\n",__FUNCTION__));

	if (karg->bPhyIdentifier >= ioc->num_ports) {
		karg->IoctlHeader.ReturnCode =
		   CSMI_SAS_STATUS_INVALID_PARAMETER;
		goto cim_sas_phy_control_exit;
	}

	/*
	 *  Retreive SAS IOUNIT PAGE 0
	 */

	hdr.PageVersion = MPI_SASIOUNITPAGE0_PAGEVERSION;
	hdr.ExtPageLength = 0;
	hdr.PageNumber = 0;
	hdr.Reserved1 = 0;
	hdr.Reserved2 = 0;
	hdr.PageType = MPI_CONFIG_PAGETYPE_EXTENDED;
	hdr.ExtPageType = MPI_CONFIG_EXTPAGETYPE_SAS_IO_UNIT;

	cfg.cfghdr.ehdr = &hdr;
	cfg.physAddr = -1;
	cfg.pageAddr = 0;
	cfg.action = MPI_CONFIG_ACTION_PAGE_HEADER;
	cfg.dir = 0;	/* read */
	cfg.timeout = MPT_IOCTL_DEFAULT_TIMEOUT;

	if (mpt_config(ioc, &cfg) != 0) {
		dcsmisasprintk(ioc, printk(KERN_ERR
		    ": FAILED: READ MPI_SASIOUNITPAGE0: HEADER\n"));
		karg->IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
		goto cim_sas_phy_control_exit;
	}

	if (hdr.ExtPageLength == 0) {
		dcsmisasprintk(ioc, printk(KERN_ERR ": hdr.ExtPageLength == 0\n"));
		karg->IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
		goto cim_sas_phy_control_exit;
	}

	sasIoUnitPg0_data_sz = hdr.ExtPageLength * 4;
	sasIoUnitPg0 = (SasIOUnitPage0_t *) pci_alloc_consistent(ioc->pcidev,
	    sasIoUnitPg0_data_sz, &sasIoUnitPg0_dma);

	if (!sasIoUnitPg0) {
		dcsmisasprintk(ioc, printk(KERN_ERR ": pci_alloc_consistent: FAILED\n"));
		karg->IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
		goto cim_sas_phy_control_exit;
	}

	memset((u8 *)sasIoUnitPg0, 0, sasIoUnitPg0_data_sz);
	cfg.physAddr = sasIoUnitPg0_dma;
	cfg.action = MPI_CONFIG_ACTION_PAGE_READ_CURRENT;

	if (mpt_config(ioc, &cfg) != 0) {
		dcsmisasprintk(ioc, printk(KERN_ERR
		    ": FAILED: READ MPI_SASIOUNITPAGE0: CURRENT\n"));
		karg->IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
		goto cim_sas_phy_control_exit;
	}

	/*
	 *  Retreive SAS IOUNIT PAGE 1
	 */

	hdr.PageVersion = MPI_SASIOUNITPAGE0_PAGEVERSION;
	hdr.ExtPageLength = 0;
	hdr.PageNumber = 1;
	hdr.Reserved1 = 0;
	hdr.Reserved2 = 0;
	hdr.PageType = MPI_CONFIG_PAGETYPE_EXTENDED;
	hdr.ExtPageType = MPI_CONFIG_EXTPAGETYPE_SAS_IO_UNIT;

	cfg.cfghdr.ehdr = &hdr;
	cfg.physAddr = -1;
	cfg.pageAddr = 0;
	cfg.action = MPI_CONFIG_ACTION_PAGE_HEADER;
	cfg.dir = 0;	/* read */
	cfg.timeout = MPT_IOCTL_DEFAULT_TIMEOUT;

	if (mpt_config(ioc, &cfg) != 0) {
		dcsmisasprintk(ioc, printk(KERN_ERR
		    ": FAILED: READ MPI_SASIOUNITPAGE1: HEADER\n"));
		karg->IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
		goto cim_sas_phy_control_exit;
	}

	if (hdr.ExtPageLength == 0) {
		dcsmisasprintk(ioc, printk(KERN_ERR ": hdr.ExtPageLength == 0\n"));
		karg->IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
		goto cim_sas_phy_control_exit;
	}

	sasIoUnitPg1_data_sz = hdr.ExtPageLength * 4;
	sasIoUnitPg1 = (SasIOUnitPage1_t *) pci_alloc_consistent(ioc->pcidev,
	    sasIoUnitPg1_data_sz, &sasIoUnitPg1_dma);

	if (!sasIoUnitPg1) {
		dcsmisasprintk(ioc, printk(KERN_ERR ": pci_alloc_consistent: FAILED\n"));
		karg->IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
		goto cim_sas_phy_control_exit;
	}

	memset((u8 *)sasIoUnitPg1, 0, sasIoUnitPg1_data_sz);
	cfg.physAddr = sasIoUnitPg1_dma;
	cfg.action = MPI_CONFIG_ACTION_PAGE_READ_CURRENT;

	if (mpt_config(ioc, &cfg) != 0) {
		dcsmisasprintk(ioc, printk(KERN_ERR
		    ": FAILED:  READ MPI_SASIOUNITPAGE1: CURRENT\n"));
		karg->IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
		goto cim_sas_phy_control_exit;
	}

	switch (karg->uFunction) {

	case CSMI_SAS_PC_LINK_RESET:
	case CSMI_SAS_PC_HARD_RESET:
	{
		u8 opcode = (karg->uFunction==CSMI_SAS_PC_LINK_RESET) ?
		    MPI_SAS_OP_PHY_LINK_RESET : MPI_SAS_OP_PHY_HARD_RESET;

		if((karg->uLinkFlags & CSMI_SAS_PHY_ACTIVATE_CONTROL) &&
		    (karg->usLengthOfControl >= sizeof(CSMI_SAS_PHY_CONTROL)) &&
		    (karg->bNumberOfControls > 0)){
			if(karg->Control[0].bRate ==
			   CSMI_SAS_LINK_RATE_1_5_GBPS) {
				sasIoUnitPg1->PhyData[karg->bPhyIdentifier].MaxMinLinkRate =
				MPI_SAS_IOUNIT1_MAX_RATE_1_5 |
				MPI_SAS_IOUNIT1_MIN_RATE_1_5;
			}
			else if(karg->Control[0].bRate ==
			   CSMI_SAS_LINK_RATE_3_0_GBPS) {
				sasIoUnitPg1->PhyData[karg->bPhyIdentifier].MaxMinLinkRate =
				MPI_SAS_IOUNIT1_MAX_RATE_3_0 |
				MPI_SAS_IOUNIT1_MIN_RATE_3_0;
			}
			sasIoUnitPg1->PhyData[karg->bPhyIdentifier].PhyFlags &=
			    ~MPI_SAS_IOUNIT1_PHY_FLAGS_PHY_DISABLE;
			cfg.dir = 1;
			cfg.action = MPI_CONFIG_ACTION_PAGE_WRITE_NVRAM;
			if (mpt_config(ioc, &cfg) != 0) {
				dcsmisasprintk(ioc, printk(KERN_ERR
			    ": FAILED: WRITE MPI_SASIOUNITPAGE1 NVRAM\n"));
				karg->IoctlHeader.ReturnCode =
				   CSMI_SAS_STATUS_FAILED;
				goto cim_sas_phy_control_exit;
			}
			cfg.action = MPI_CONFIG_ACTION_PAGE_WRITE_CURRENT;
			if (mpt_config(ioc, &cfg) != 0) {
				dcsmisasprintk(ioc, printk(KERN_ERR
			 ": FAILED: WRITE MPI_SASIOUNITPAGE1 CURRENT\n"));
				karg->IoctlHeader.ReturnCode =
				   CSMI_SAS_STATUS_FAILED;
				goto cim_sas_phy_control_exit;
			}
		}
		if (csmisas_phy_reset(ioc,
		    karg->bPhyIdentifier, opcode) != 0) {
			dcsmisasprintk(ioc, printk(KERN_ERR
			    ": FAILED: csmisas_phy_reset\n"));
			karg->IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
			goto cim_sas_phy_control_exit;
		}
		break;

	}
	case CSMI_SAS_PC_PHY_DISABLE:
		if(karg->usLengthOfControl || karg->bNumberOfControls) {
			karg->IoctlHeader.ReturnCode =
			    CSMI_SAS_STATUS_INVALID_PARAMETER;
			break;
		}
		sasIoUnitPg1->PhyData[karg->bPhyIdentifier].PhyFlags |=
		    MPI_SAS_IOUNIT1_PHY_FLAGS_PHY_DISABLE;
		cfg.dir = 1;
		cfg.action = MPI_CONFIG_ACTION_PAGE_WRITE_NVRAM;
		if (mpt_config(ioc, &cfg) != 0) {
			dcsmisasprintk(ioc, printk(KERN_ERR
			    ": FAILED: WRITE MPI_SASIOUNITPAGE1 NVRAM\n"));
			karg->IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
			goto cim_sas_phy_control_exit;
		}
		cfg.action = MPI_CONFIG_ACTION_PAGE_WRITE_CURRENT;
		if (mpt_config(ioc, &cfg) != 0) {
			dcsmisasprintk(ioc, printk(KERN_ERR
			    ": FAILED: WRITE MPI_SASIOUNITPAGE1 CURRENT\n"));
			karg->IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
			goto cim_sas_phy_control_exit;
		}
		if (csmisas_phy_reset(ioc,
		    karg->bPhyIdentifier, MPI_SAS_OP_PHY_HARD_RESET) != 0) {
			dcsmisasprintk(ioc, printk(KERN_ERR
			    ": FAILED: csmisas_phy_reset\n"));
			karg->IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;
			goto cim_sas_phy_control_exit;
		}
		break;

	case CSMI_SAS_PC_GET_PHY_SETTINGS:
		if(karg->usLengthOfControl || karg->bNumberOfControls) {
			karg->IoctlHeader.ReturnCode =
			    CSMI_SAS_STATUS_INVALID_PARAMETER;
			break;
		}
		if(csmi_sas_phy_control_buffer_sz <
		    offsetof(CSMI_SAS_PHY_CONTROL_BUFFER,Control) +
		    (4* sizeof(CSMI_SAS_PHY_CONTROL))) {
			karg->IoctlHeader.ReturnCode =
			    CSMI_SAS_STATUS_INVALID_PARAMETER;
			break;
		}
		karg->usLengthOfControl = sizeof(CSMI_SAS_PHY_CONTROL);
		karg->bNumberOfControls = 4;
		karg->Control[0].bType = CSMI_SAS_SAS;
		karg->Control[0].bRate = CSMI_SAS_LINK_RATE_1_5_GBPS;
		karg->Control[1].bType = CSMI_SAS_SAS;
		karg->Control[1].bRate = CSMI_SAS_LINK_RATE_3_0_GBPS;
		karg->Control[2].bType = CSMI_SAS_SATA;
		karg->Control[2].bRate = CSMI_SAS_LINK_RATE_1_5_GBPS;
		karg->Control[3].bType = CSMI_SAS_SATA;
		karg->Control[3].bRate = CSMI_SAS_LINK_RATE_3_0_GBPS;
		karg->IoctlHeader.ReturnCode = CSMI_SAS_STATUS_SUCCESS;
		break;
	default:
		break;
	}

 cim_sas_phy_control_exit:

	if (sasIoUnitPg0)
		pci_free_consistent(ioc->pcidev, sasIoUnitPg0_data_sz,
		    (u8 *) sasIoUnitPg0, sasIoUnitPg0_dma);

	if (sasIoUnitPg1)
		pci_free_consistent(ioc->pcidev, sasIoUnitPg1_data_sz,
		    (u8 *) sasIoUnitPg1, sasIoUnitPg1_dma);

	/* Copy the data from kernel memory to user memory
	 */
	if (copy_to_user(uarg, karg,csmi_sas_phy_control_buffer_sz)) {
		printk(KERN_ERR "%s@%d::%s() - "
		    "Unable to write out csmi_sas_phy_control_buffer @ %p\n",
		    __FILE__, __LINE__, __FUNCTION__, uarg);
		free_pages((unsigned long)karg, memory_pages);
		return -EFAULT;
	}

	dcsmisasprintk(ioc, printk(KERN_DEBUG "%s exit.\n",__FUNCTION__));
	free_pages((unsigned long)karg, memory_pages);
	return 0;
}

/**
 *	csmisas_get_manuf_pg_7 - Fetch Manufacturing config Page7.
 * @ioc: Pointer to MPT_ADAPTER structure
 * @mfgpage7_buffer: pointer to ManufacturingPage7_t that returns config
 *                    page data
 * @mfg_size - max size of buffer
 *
 *	Return: 0 for success
 *	-ENOMEM if no memory available
 *		-EPERM if not allowed due to ISR context
 *		-EAGAIN if no msg frames currently available
 *		-EFAULT for non-successful reply or no reply (timeout)
 **/
static int
csmisas_get_manuf_pg_7(MPT_ADAPTER *ioc, ManufacturingPage7_t *mfgpage7_buffer, int mfg_size)
{
	ConfigPageHeader_t hdr;
	CONFIGPARMS	cfg;
	ManufacturingPage7_t *mfgPage7 = NULL;
	dma_addr_t	mfgPage7_dma;
	int		data_sz = 0;
	int		rc;

	/* Get Manufacturing Page 7 header */
	hdr.PageVersion = MPI_MANUFACTURING0_PAGEVERSION;
	hdr.PageLength = 0;
	hdr.PageNumber = 7;
	hdr.PageType = MPI_CONFIG_PAGETYPE_MANUFACTURING;
	cfg.cfghdr.hdr = &hdr;
	cfg.physAddr = -1;
	cfg.action = MPI_CONFIG_ACTION_PAGE_HEADER;
	cfg.dir = 0;
	cfg.pageAddr = 0;
	cfg.timeout = 0;

	if ((rc = mpt_config(ioc, &cfg)) != 0)
		goto csmisas_get_manuf_pg_7_exit;

	if (hdr.PageLength == 0) {
		rc = -EFAULT;
		goto csmisas_get_manuf_pg_7_exit;
	}

	data_sz = hdr.PageLength * 4;
	mfgPage7 = pci_alloc_consistent(ioc->pcidev, data_sz, &mfgPage7_dma);
	if (!mfgPage7) {
		rc = -ENOMEM;
		goto csmisas_get_manuf_pg_7_exit;
	}

	memset((u8 *)mfgPage7, 0, data_sz);
	cfg.physAddr = mfgPage7_dma;
	cfg.action = MPI_CONFIG_ACTION_PAGE_READ_CURRENT;

	if ((rc = mpt_config(ioc, &cfg)) != 0)
		goto csmisas_get_manuf_pg_7_exit;

	/* copy buffer back to user */
	memcpy(mfgpage7_buffer, mfgPage7, min(data_sz, mfg_size));

 csmisas_get_manuf_pg_7_exit:

	if (mfgPage7)
		pci_free_consistent(ioc->pcidev, data_sz, (u8 *)mfgPage7,
		    mfgPage7_dma);

	return rc;
}

/**
 * Prototype Routine for the CSMI SAS Get Connector info command.
 *
 * Outputs:	None.
 * Return:	0 if successful
 *		-EFAULT if data unavailable
 *		-ENODEV if no such device/adapter
 **/
static int
csmisas_get_connector_info(unsigned long arg)
{
	CSMI_SAS_CONNECTOR_INFO_BUFFER __user *uarg = (void __user *) arg;
	CSMI_SAS_CONNECTOR_INFO_BUFFER	 karg;
	MPT_ADAPTER			*ioc = NULL;
	ManufacturingPage7_t    	*mfgPg7 = NULL;
	int				mfgPg7_sz;
	int				iocnum;
	int				i;

	if (copy_from_user(&karg, uarg,
		sizeof(CSMI_SAS_CONNECTOR_INFO_BUFFER))) {
		printk(KERN_ERR "%s@%d::%s() - "
		   "Unable to read in csmi_sas_connector_info_buffer"
		   " struct @ %p\n",
		   __FILE__, __LINE__, __FUNCTION__, uarg);
		return -EFAULT;
	}

	if (((iocnum = mpt_verify_adapter(karg.IoctlHeader.IOControllerNumber,
	    &ioc)) < 0) || (ioc == NULL)) {
		printk(KERN_ERR "%s::%s() @%d - ioc%d not found!\n",
		    __FILE__, __FUNCTION__, __LINE__, iocnum);
		return -ENODEV;
	}

	if (!csmisas_is_this_sas_cntr(ioc)) {
		printk(KERN_ERR "%s::%s() @%d - ioc%d not SAS controller!\n",
		    __FILE__, __FUNCTION__, __LINE__, iocnum);
		return -ENODEV;
	}

	dcsmisasprintk(ioc, printk(KERN_DEBUG "%s enter.\n",__FUNCTION__));

	karg.IoctlHeader.ReturnCode = CSMI_SAS_STATUS_SUCCESS;

	/* `32` is the sizeof MPI_MANPAGE7_CONNECTOR_INFO */
	for (i = 0; i < 32; i++) {
		karg.Reference[i].uPinout = CSMI_SAS_CON_UNKNOWN;
		strcpy(karg.Reference[i].bConnector,"");
		karg.Reference[i].bLocation = CSMI_SAS_CON_UNKNOWN;
	}

	mfgPg7_sz = offsetof(CONFIG_PAGE_MANUFACTURING_7,ConnectorInfo) +
	    (ioc->num_ports * sizeof(MPI_MANPAGE7_CONNECTOR_INFO));
	mfgPg7 = kmalloc(mfgPg7_sz, GFP_KERNEL);
	if (!mfgPg7){
		printk(KERN_ERR "%s@%d::%s() - "
		    "Unable to malloc @ %p\n",
		    __FILE__, __LINE__, __FUNCTION__, mfgPg7);
		return -EFAULT;
	}
	memset(mfgPg7, 0, mfgPg7_sz);

	if (!csmisas_get_manuf_pg_7(ioc, mfgPg7, mfgPg7_sz)) {
		for (i = 0; i < ioc->num_ports; i++) {
			karg.Reference[i].uPinout =
			    le32_to_cpu(mfgPg7->ConnectorInfo[i].Pinout);
			/*endian conversion , this is u8 * 16 ?? */
			strncpy(karg.Reference[i].bConnector,
			    mfgPg7->ConnectorInfo[i].Connector, 16);
			karg.Reference[i].bLocation =
			    mfgPg7->ConnectorInfo[i].Location;
		}
	}

	kfree(mfgPg7);

	/* Copy the data from kernel memory to user memory
	 */
	if (copy_to_user(uarg, &karg,
		sizeof(CSMI_SAS_CONNECTOR_INFO_BUFFER))) {
		printk(KERN_ERR "%s@%d::%s() - "
		"Unable to write out csmi_sas_connector_info_buffer @"
	       "%p\n",
		__FILE__, __LINE__, __FUNCTION__, uarg);
		return -EFAULT;
	}

	dcsmisasprintk(ioc, printk(KERN_DEBUG "%s exit.\n",__FUNCTION__));
	return 0;
}

/**
 *                 csmisas_fill_location_data
 *
 * Outputs:	None.
 * Return:	0 if successful
 **/
static int
csmisas_fill_location_data(MPT_ADAPTER *ioc, u8 bus, u8 id, u8 opcode,
	CSMI_SAS_LOCATION_IDENTIFIER * location_ident)
{

	ConfigExtendedPageHeader_t 	hdr;
	CONFIGPARMS			cfg;
	int				rc;
	SasDevicePage0_t		*sasDevicePg0=NULL;
	SasEnclosurePage0_t		*sasEnclosurePg0=NULL;
	dma_addr_t			sasDevicePg0_dma,sasEnclosurePg0_dma;
	int				sasDevicePg0_data_sz=0;
	int				sasEnclosurePg0_data_sz=0;
	u64				sas_address;

	dcsmisasprintk(ioc, printk(KERN_DEBUG "%s enter.\n",__FUNCTION__));
	memset (location_ident, 0, sizeof(*location_ident));

	/* SAS Device Page 0 */
	hdr.PageVersion = MPI_SASDEVICE0_PAGEVERSION;
	hdr.ExtPageLength = 0;
	hdr.PageNumber = 0;
	hdr.Reserved1 = 0;
	hdr.Reserved2 = 0;
	hdr.PageType = MPI_CONFIG_PAGETYPE_EXTENDED;
	hdr.ExtPageType = MPI_CONFIG_EXTPAGETYPE_SAS_DEVICE;

	cfg.cfghdr.ehdr = &hdr;
	cfg.physAddr = -1;
	cfg.action = MPI_CONFIG_ACTION_PAGE_HEADER;
	cfg.dir = 0;	/* read */
	cfg.timeout = MPT_IOCTL_DEFAULT_TIMEOUT;

	if ((rc = mpt_config(ioc, &cfg)) != 0) {
		rc=-1;
		goto fill_location_data_exit;
	}

	if (hdr.ExtPageLength == 0) {
		rc=-1;
		goto fill_location_data_exit;
	}

	sasDevicePg0_data_sz = hdr.ExtPageLength * 4;
	sasDevicePg0 = (SasDevicePage0_t *) pci_alloc_consistent(
	    ioc->pcidev, sasDevicePg0_data_sz, &sasDevicePg0_dma);
	if (!sasDevicePg0) {
		rc=-1;
		goto fill_location_data_exit;
	}

	memset((u8 *)sasDevicePg0, 0, sasDevicePg0_data_sz);
	cfg.physAddr = sasDevicePg0_dma;
	cfg.action = MPI_CONFIG_ACTION_PAGE_READ_CURRENT;
	cfg.pageAddr = (bus << 8) + id
	    + (MPI_SAS_DEVICE_PGAD_FORM_BUS_TARGET_ID <<
	       	MPI_SAS_DEVICE_PGAD_FORM_SHIFT);

	if ((rc = mpt_config(ioc, &cfg)) != 0) {
		rc=-1;
		goto fill_location_data_exit;
	}

	location_ident->bLocationFlags |= CSMI_SAS_LOCATE_SAS_ADDRESS_VALID;
	memcpy(&sas_address, &sasDevicePg0->SASAddress, sizeof(u64));
	sas_address = reverse_byte_order64(sas_address);
	memcpy(location_ident->bSASAddress, &sas_address, sizeof(u64));

	location_ident->bLocationFlags |= CSMI_SAS_LOCATE_SAS_LUN_VALID;
	memset(location_ident->bSASLun, 0, sizeof(location_ident->bSASLun));

	/* SAS Enclosure Page 0 */
	hdr.PageVersion = MPI_SASENCLOSURE0_PAGEVERSION;
	hdr.ExtPageLength = 0;
	hdr.PageNumber = 0;
	hdr.Reserved1 = 0;
	hdr.Reserved2 = 0;
	hdr.PageType = MPI_CONFIG_PAGETYPE_EXTENDED;
	hdr.ExtPageType = MPI_CONFIG_EXTPAGETYPE_ENCLOSURE;

	cfg.cfghdr.ehdr = &hdr;
	cfg.physAddr = -1;
	cfg.action = MPI_CONFIG_ACTION_PAGE_HEADER;
	cfg.dir = 0;	/* read */
	cfg.timeout = MPT_IOCTL_DEFAULT_TIMEOUT;

	if ((rc = mpt_config(ioc, &cfg)) != 0) {
		rc=0;
		goto fill_location_data_exit;
	}

	if (hdr.ExtPageLength == 0) {
		rc=0;
		goto fill_location_data_exit;
	}

	sasEnclosurePg0_data_sz = hdr.ExtPageLength * 4;
	sasEnclosurePg0 = (SasEnclosurePage0_t *) pci_alloc_consistent(
	    ioc->pcidev, sasEnclosurePg0_data_sz, &sasEnclosurePg0_dma);
	if (!sasEnclosurePg0) {
		rc=0;
		goto fill_location_data_exit;
	}
	cfg.physAddr = sasEnclosurePg0_dma;
	cfg.action = MPI_CONFIG_ACTION_PAGE_READ_CURRENT;
	cfg.pageAddr = sasDevicePg0->EnclosureHandle
	    + (MPI_SAS_ENCLOS_PGAD_FORM_HANDLE << MPI_SAS_ENCLOS_PGAD_FORM_SHIFT);

	if ((rc = mpt_config(ioc, &cfg)) != 0) {
		rc=0;
		goto fill_location_data_exit;
	}

	location_ident->bLocationFlags |= CSMI_SAS_LOCATE_ENCLOSURE_IDENTIFIER_VALID;
	memcpy(&sas_address, &sasEnclosurePg0->EnclosureLogicalID, sizeof(u64));
	sas_address = reverse_byte_order64(sas_address);
	if (sas_address)
		memcpy(location_ident->bEnclosureIdentifier, &sas_address, sizeof(u64));
	else
		strcpy(location_ident->bEnclosureIdentifier,"Internal");

// bBayPrefix - not supported

// TODO - We need to look at sasEnclosurePg0-.Flags , to determine
//	whether SEP BUS/TargetID is valid.  Ifs its a SES device, then
//	issue internal inquiry to (bus/id) to gather the Enclosure name.
//	If the device is SMP, then issue SMP_MANUFACTURING to get enclosure name
//	If its direct attached, there is no enclosure name
	location_ident->bLocationFlags |= CSMI_SAS_LOCATE_ENCLOSURE_NAME_VALID;
	strcpy(location_ident->bEnclosureName,"Not Supported");

	location_ident->bLocationFlags |= CSMI_SAS_LOCATE_LOCATION_STATE_VALID;
	location_ident->bLocationState = CSMI_SAS_LOCATE_UNKNOWN;

	location_ident->bLocationFlags |= CSMI_SAS_LOCATE_BAY_IDENTIFIER_VALID;
	location_ident->bBayIdentifier = le16_to_cpu(sasDevicePg0->Slot);


// TODO - illuminating LEDs,
// karg->bIdentify = CSMI_SAS_LOCATE_FORCE_OFF, CSMI_SAS_LOCATE_FORCE_ON
// We can enable/disable LEDs by SCSI Enclosure Processor MPI request message
// printk("Flags=0x%x\n",sasEnclosurePg0->Flags);

/* check sasEnclosurePg0->Flags -
 * to validate whether we need to send the SEPRequest
 * bit:5 should be set
 * bit:3-0 any bit should be set.  If zero, then SEPRequest will fail
*/

/* MPI_FUNCTION_SCSI_ENCLOSURE_PROCESSOR
 * Look in mpi_init.h
 * SEPRequest_t = structure
 *
 * SEPRequest_t->Action should be set to MPI_SEP_REQ_ACTION_WRITE_STATUS
 *
 * SEPRequest_t->Flags should be set to
 * MPI_SEP_REQ_FLAGS_ENCLOSURE_SLOT_ADDRESS, to pass along enclosure/slot ids
 *
 * SEPRequest_t->SlotStatus |= MPI_SEP_REQ_SLOTSTATUS_IDENTIFY_REQUEST - this
 * will illuminate the LEDs
 */

fill_location_data_exit:

	if (sasDevicePg0 != NULL)
		pci_free_consistent(ioc->pcidev, sasDevicePg0_data_sz,
		    sasDevicePg0, sasDevicePg0_dma);

	if (sasEnclosurePg0 != NULL)
		pci_free_consistent(ioc->pcidev, sasEnclosurePg0_data_sz,
		    sasEnclosurePg0, sasEnclosurePg0_dma);

	dcsmisasprintk(ioc, printk(KERN_DEBUG "%s exit.\n",__FUNCTION__));
	return rc;
}

static int
csmisas_fill_location_data_raid(MPT_ADAPTER *ioc, PCSMI_SAS_GET_LOCATION_BUFFER karg, u8 VolumeBus,
	u8 volumeID)
{
	pRaidVolumePage0_t		pVolume0 = NULL;
	pRaidPhysDiskPage0_t		pPhysDisk0 = NULL;
	CONFIGPARMS			cfg;
	ConfigPageHeader_t		header;
	u8				physDiskNumMax;
	int				volumepage0sz = 0, physdiskpage0sz = 0;
	dma_addr_t			volume0_dma, physdisk0_dma;
	int 				csmi_sas_get_location_sz;
	int				rc = 0, i, idx;
	int 				num_hotpares;
	u64				totalMaxLBA, tmpTotalMaxLBA;
	IOCPage5_t 			*iocPage5 = NULL;
	u32				device_info = 0;
	struct sas_device_info		*sas_info;

	int 				sz;

	csmi_sas_get_location_sz = karg->IoctlHeader.Length;
	physDiskNumMax = (csmi_sas_get_location_sz -
	    offsetof(CSMI_SAS_GET_LOCATION_BUFFER,Location))
	    / sizeof(CSMI_SAS_LOCATION_IDENTIFIER);
	karg->bNumberOfLocationIdentifiers=0;

	/*
	 * get RAID Volume Page 0
	 */

	header.PageVersion = 0;
	header.PageLength = 0;
	header.PageNumber = 0;
	header.PageType = MPI_CONFIG_PAGETYPE_RAID_VOLUME;
	cfg.cfghdr.hdr = &header;
	cfg.physAddr = -1;
	cfg.pageAddr = (VolumeBus << 8) + volumeID;
	cfg.action = MPI_CONFIG_ACTION_PAGE_HEADER;
	cfg.dir = 0;
	cfg.timeout = MPT_IOCTL_DEFAULT_TIMEOUT;
	if (mpt_config(ioc, &cfg) != 0) {
		rc = -1;
		goto sas_fill_location_data_raid_exit;
	}

	if (header.PageLength == 0) {
		rc = -1;
		goto sas_fill_location_data_raid_exit;
	}

	volumepage0sz = header.PageLength * 4;
	pVolume0 = pci_alloc_consistent(ioc->pcidev, volumepage0sz,
	    &volume0_dma);
	if (!pVolume0) {
		rc = -1;
		goto sas_fill_location_data_raid_exit;
	}

	cfg.action = MPI_CONFIG_ACTION_PAGE_READ_CURRENT;
	cfg.physAddr = volume0_dma;
	if (mpt_config(ioc, &cfg) != 0){
		rc = -1;
		goto sas_fill_location_data_raid_exit;
	}

	totalMaxLBA = (u64)le32_to_cpu(pVolume0->MaxLBA) |
	    ((u64)le32_to_cpu(pVolume0->MaxLBAHigh)) << 32;

	/*
	 * get RAID Physical Disk Page 0
	 */
	header.PageVersion = 0;
	header.PageLength = 0;
	header.PageNumber = 0;
	header.PageType = MPI_CONFIG_PAGETYPE_RAID_PHYSDISK;
	cfg.cfghdr.hdr = &header;
	cfg.physAddr = -1;
	cfg.pageAddr = 0;
	cfg.action = MPI_CONFIG_ACTION_PAGE_HEADER;
	cfg.dir = 0;
	cfg.timeout = MPT_IOCTL_DEFAULT_TIMEOUT;
	if (mpt_config(ioc, &cfg) != 0) {
		rc = -1;
		goto sas_fill_location_data_raid_exit;
	}

	if (header.PageLength == 0) {
		rc = -1;
		goto sas_fill_location_data_raid_exit;
	}

	physdiskpage0sz = header.PageLength * 4;
	pPhysDisk0 = pci_alloc_consistent(ioc->pcidev, physdiskpage0sz,
	    &physdisk0_dma);
	if (!pPhysDisk0) {
		rc = -1;
		goto sas_fill_location_data_raid_exit;
	}
	cfg.physAddr = physdisk0_dma;

	for (i=0; i < min(pVolume0->NumPhysDisks, physDiskNumMax); i++) {

		/* obtain a refresh of pPhysDisk0 */
		cfg.action = MPI_CONFIG_ACTION_PAGE_READ_CURRENT;
		cfg.pageAddr = pVolume0->PhysDisk[i].PhysDiskNum;
		if (mpt_config(ioc, &cfg) != 0){
			rc = -1;
			goto sas_fill_location_data_raid_exit;
		}

		if((csmisas_fill_location_data(ioc, pPhysDisk0->PhysDiskBus,
		    pPhysDisk0->PhysDiskID, karg->bIdentify,
		    &karg->Location[karg->bNumberOfLocationIdentifiers])) == 0)
			karg->bNumberOfLocationIdentifiers++;

		if (device_info)
			continue;
		sas_info = csmisas_get_device_component_by_fw(ioc,
		    pPhysDisk0->PhysDiskBus, pPhysDisk0->PhysDiskID);
		if (!sas_info || sas_info->is_cached)
			continue;
		device_info = sas_info->device_info;
	}

	if (pVolume0->VolumeType == MPI_RAID_VOL_TYPE_IS)
		goto sas_fill_location_data_raid_exit;

	/*
	 * hot spare support
	 *
	 */

	num_hotpares = csmisas_get_number_hotspares(ioc);

	if (num_hotpares) {

		sz = offsetof(IOCPage5_t, HotSpare) +
		    num_hotpares * sizeof(IOC_5_HOT_SPARE);
		iocPage5 = kmalloc(sz, GFP_KERNEL);

		if (!iocPage5)
			goto sas_fill_location_data_raid_exit;
		memset(iocPage5, 0, sizeof(*iocPage5));

		if (csmisas_get_ioc_pg5(ioc, iocPage5, sz) != 0)
			goto sas_fill_location_data_raid_exit;

		for(i = 0, idx = pVolume0->NumPhysDisks ; i < num_hotpares;
		    i++, idx++) {

			if (idx >= physDiskNumMax)
				break;

			/* obtain a refresh of pPhysDisk0 */
			cfg.action = MPI_CONFIG_ACTION_PAGE_READ_CURRENT;
			cfg.pageAddr = iocPage5->HotSpare[i].PhysDiskNum;
			if (mpt_config(ioc, &cfg) != 0)
				goto sas_fill_location_data_raid_exit;

			/* Search the list for the matching SAS address. */
			sas_info = csmisas_get_device_component_by_fw(ioc,
			    pPhysDisk0->PhysDiskBus, pPhysDisk0->PhysDiskID);

			if (!sas_info || sas_info->is_cached)
				continue;

			/* don't mix SSP hot spare
			 * in SATA volume
			 */
			if (!csmisas_is_sata(pPhysDisk0) &&
			    (device_info &
			    MPI_SAS_DEVICE_INFO_SATA_DEVICE))
				continue;

			/* don't mix SATA hot spare
			 * in SSP volume
			 */
			if (csmisas_is_sata(pPhysDisk0) &&
			    (device_info &
			    MPI_SAS_DEVICE_INFO_SSP_TARGET))
				continue;

			/* capacity check for IM volumes*/
			if ((pVolume0->VolumeType ==
			    MPI_RAID_VOL_TYPE_IM) &&
			    (totalMaxLBA +
			    (64*2*1024) /* metadata = 64MB*/ >
			    le32_to_cpu(pPhysDisk0->MaxLBA)))
				continue;

			tmpTotalMaxLBA = totalMaxLBA;
			do_div(tmpTotalMaxLBA, pVolume0->NumPhysDisks);
			/* capacity check for IME volumes*/
			if ((pVolume0->VolumeType ==
				MPI_RAID_VOL_TYPE_IME) &&
			    ((tmpTotalMaxLBA * 2) +
			     (64*2*1024 ) /*metadata = 64MB*/ >
			    le32_to_cpu(pPhysDisk0->MaxLBA)))
				continue;

			if((csmisas_fill_location_data(ioc,
			    pPhysDisk0->PhysDiskBus, pPhysDisk0->PhysDiskID,
			    karg->bIdentify,
			    &karg->Location[karg->bNumberOfLocationIdentifiers])) == 0)
				karg->bNumberOfLocationIdentifiers++;
		}
	}


 sas_fill_location_data_raid_exit:

	kfree(iocPage5);

	if (pVolume0)
		pci_free_consistent(ioc->pcidev, volumepage0sz, pVolume0,
		    volume0_dma);

	if(pPhysDisk0)
		pci_free_consistent(ioc->pcidev, physdiskpage0sz, pPhysDisk0,
		    physdisk0_dma);

	return rc;
}

/**
 * Prototype Routine for the CSMI SAS Get location command.
 *
 * Outputs:	None.
 * Return:	0 if successful
 *		-EFAULT if data unavailable
 *		-ENODEV if no such device/adapter
 */
static int
csmisas_get_location(unsigned long arg)
{
	CSMI_SAS_GET_LOCATION_BUFFER __user *uarg = (void __user *) arg;
	PCSMI_SAS_GET_LOCATION_BUFFER	karg;
	IOCTL_HEADER			ioctl_header;
	MPT_ADAPTER			*ioc = NULL;
	int				iocnum,i;
	int				csmi_sas_get_location_sz;
	int				memory_pages;
	struct sas_device_info		*sas_info;

	if (copy_from_user(&ioctl_header, uarg, sizeof(IOCTL_HEADER))) {
		printk(KERN_ERR "%s@%d::%s() - "
		    "Unable to read in IOCTL_HEADER"
		    "struct @ %p\n", __FILE__, __LINE__, __FUNCTION__, uarg);
		return -EFAULT;
	}

	csmi_sas_get_location_sz = ioctl_header.Length;
	memory_pages = get_order(csmi_sas_get_location_sz);
	karg = (PCSMI_SAS_GET_LOCATION_BUFFER)__get_free_pages(
		GFP_KERNEL, memory_pages);
	if (!karg){
		printk(KERN_ERR "%s@%d::%s() - "
			"Unable to malloc GET_LOCATION_BUFFER "
			"csmi_sas_get_location_sz=%d memory_pages=%d\n",
			__FILE__, __LINE__, __FUNCTION__, 
			csmi_sas_get_location_sz, memory_pages);
		return -ENOMEM;
	}
	memset(karg, 0, sizeof(*karg));

	if (copy_from_user(karg, uarg, csmi_sas_get_location_sz)) {
		printk(KERN_ERR "%s@%d::%s() - "
		    "Unable to read in csmi_sas_phy_control_buffer "
		    "struct @ %p\n", __FILE__, __LINE__, __FUNCTION__, uarg);
		free_pages((unsigned long)karg, memory_pages);
		return -EFAULT;
	}

	if (((iocnum = mpt_verify_adapter(karg->IoctlHeader.IOControllerNumber,
	    &ioc)) < 0) || (ioc == NULL)) {
		printk(KERN_ERR "%s::%s() @%d - ioc%d not found!\n",
		    __FILE__, __FUNCTION__, __LINE__, iocnum);
		free_pages((unsigned long)karg, memory_pages);
		return -ENODEV;
	}

	if (!csmisas_is_this_sas_cntr(ioc)) {
		printk(KERN_ERR "%s::%s() @%d - ioc%d not SAS controller!\n",
		    __FILE__, __FUNCTION__, __LINE__, iocnum);
		free_pages((unsigned long)karg, memory_pages);
		return -ENODEV;
	}

	dcsmisasprintk(ioc, printk(KERN_DEBUG "%s enter.\n",__FUNCTION__));

	karg->IoctlHeader.ReturnCode = CSMI_SAS_STATUS_INVALID_PARAMETER;
	if(karg->bLengthOfLocationIdentifier !=
	    sizeof(CSMI_SAS_LOCATION_IDENTIFIER))
		goto cim_sas_get_location_exit;

	sas_info = csmisas_get_device_component_by_os(ioc, karg->bPathId,
	    karg->bTargetId);
	if (!sas_info)
		goto cim_sas_get_location_exit;

	/* RAID SUPPORT */
	if (ioc->raid_data.pIocPg2 && sas_info->is_logical_volume) {
		for (i=0; i<ioc->raid_data.pIocPg2->NumActiveVolumes; i++){
			if (sas_info->fw.id ==
			    ioc->raid_data.pIocPg2->RaidVolume[i].VolumeID &&
			    sas_info->fw.channel ==
			    ioc->raid_data.pIocPg2->RaidVolume[i].VolumeBus) {
				if(csmisas_fill_location_data_raid(ioc, karg,
				    ioc->raid_data.pIocPg2->RaidVolume[i].VolumeBus,
				    ioc->raid_data.pIocPg2->RaidVolume[i].VolumeID) == 0)
					karg->IoctlHeader.ReturnCode =
					    CSMI_SAS_STATUS_SUCCESS;
				else
					karg->IoctlHeader.ReturnCode =
					    CSMI_SAS_STATUS_FAILED;
				goto cim_sas_get_location_exit;
			}
		}
	}

	/* NON-RAID SUPPORT */
	if (sas_info->is_cached || sas_info->is_logical_volume)
		goto cim_sas_get_location_exit;

	/* make sure there's enough room to populate the Location[] struct */
	if ((csmi_sas_get_location_sz -
	    offsetof(CSMI_SAS_GET_LOCATION_BUFFER,Location)) <
	    sizeof(CSMI_SAS_LOCATION_IDENTIFIER))
		goto cim_sas_get_location_exit;

	karg->bNumberOfLocationIdentifiers=1;
	karg->Location[0].bLocationFlags=0;
	if((csmisas_fill_location_data(ioc, sas_info->fw.channel,
	    sas_info->fw.id, karg->bIdentify, &karg->Location[0])) == 0)
		karg->IoctlHeader.ReturnCode = CSMI_SAS_STATUS_SUCCESS;
	else
		karg->IoctlHeader.ReturnCode = CSMI_SAS_STATUS_FAILED;

 cim_sas_get_location_exit:

	/* Copy the data from kernel memory to user memory
	 */
	if (copy_to_user(uarg, karg, csmi_sas_get_location_sz)) {
		printk(KERN_ERR "%s@%d::%s() - "
		    "Unable to write out csmi_sas_get_location_buffer "
		    "@ %p\n",__FILE__, __LINE__, __FUNCTION__, uarg);
		free_pages((unsigned long)karg, memory_pages);
		return -EFAULT;
	}

	dcsmisasprintk(ioc, printk(KERN_DEBUG "%s exit.\n",__FUNCTION__));
	free_pages((unsigned long)karg, memory_pages);
	return 0;
}
