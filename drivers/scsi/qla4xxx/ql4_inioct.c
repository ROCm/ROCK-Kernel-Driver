/******************************************************************************
 *                  QLOGIC LINUX SOFTWARE                                     *
 *                                                                            *
 * QLogic ISP4xxx device driver for Linux 2.4.x                               *
 * Copyright (C) 2004 Qlogic Corporation                                      *
 * (www.qlogic.com)                                                           *
 *                                                                            *
 * This program is free software; you can redistribute it and/or modify it    *
 * under the terms of the GNU General Public License as published by the      *
 * Free Software Foundation; either version 2, or (at your option) any        *
 * later version.                                                             *
 *                                                                            *
 * This program is distributed in the hope that it will be useful, but        *
 * WITHOUT ANY WARRANTY; without even the implied warranty of                 *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU          *
 * General Public License for more details.                                   *
 *                                                                            *
 ******************************************************************************
 *             Please see release.txt for revision history.                   *
 *                                                                            *
 ******************************************************************************
 * Function Table of Contents:
 *	qla4intioctl_logout_iscsi
 *	qla4intioctl_ping
 *	__xlate_sys_info
 *	__xlate_driver_info
 *	__xlate_init_fw_ctrl_blk
 *	__xlate_dev_db
 *	__xlate_chap
 *	qla4intioctl_get_flash
 *	qla4intioctl_get_driver_debug_level
 *	qla4intioctl_get_host_no
 *	qla4intioctl_get_data
 *	qla4intioctl_set_flash
 *	qla4intioctl_set_driver_debug_level
 *	qla4intioctl_set_data
 *	qla4intioctl_hba_reset
 *	qla4intioctl_copy_fw_flash
 *	qla4xxx_iocb_pass_done
 *	qla4intioctl_iocb_passthru
 ****************************************************************************/
#include "ql4_def.h"
#include "ql4_ioctl.h"


// KRH: (BEGIN) Define these locally, for now
/*
 * Sub codes for Get Data.
 * Use in combination with INT_GET_DATA as the ioctl code
 */
#define INT_SC_GET_DRIVER_DEBUG_LEVEL   2
#define INT_SC_GET_HOST_NO 		3

/*
 * Sub codes for Set Data.
 * Use in combination with INT_SET_DATA as the ioctl code
 */
#define INT_SC_SET_DRIVER_DEBUG_LEVEL	2

/*
 * Sub codes for Reset
 * Use in combination with INT_CC_HBA_RESET as the ioctl code
 */
#define INT_SC_HBA_RESET			0
#define INT_SC_FIRMWARE_RESET			1
#define INT_SC_TARGET_WARM_RESET		2
#define INT_SC_LUN_RESET			3
//KRH: (END)

/* Defines for byte-order translation direction */
#define GET_DATA	0
#define SET_DATA	1

ioctl_tbl_row_t IOCTL_SCMD_IGET_DATA_TBL[] =
{
	{INT_SC_GET_FLASH, "INT_SC_GET_FLASH"},
	{INT_SC_GET_DRIVER_DEBUG_LEVEL, "INT_SC_GET_DRIVER_DEBUG_LEVEL"},
	{INT_SC_GET_HOST_NO, "INT_SC_GET_HOST_NO"},
	{0, "UNKNOWN"}
};

ioctl_tbl_row_t IOCTL_SCMD_ISET_DATA_TBL[] =
{
	{INT_SC_SET_FLASH, "INT_SC_SET_FLASH"},
	{INT_SC_SET_DRIVER_DEBUG_LEVEL, "INT_SC_SET_DRIVER_DEBUG_LEVEL"},
	{0, "UNKNOWN"}
};


/**************************************************************************
 * qla4intioctl_logout_iscsi
 *	This routine requests that the specified device either login or
 *	logout, depending on the option specified.
 *
 * Input:
 *	ha    = adapter structure pointer.
 *	ioctl = IOCTL structure pointer.
 *
 * Output:
 *	None
 *
 * Returns:
 *	QLA_SUCCESS = success
 *	QLA_ERROR   = error
 *
 * Context:
 *	Kernel context.
 **************************************************************************/
int
qla4intioctl_logout_iscsi(scsi_qla_host_t *ha, EXT_IOCTL_ISCSI *ioctl)
{
	int status = 0;
	INT_LOGOUT_ISCSI logout;

	ENTER(__func__);
	QL4PRINT(QLP4,
	    printk("scsi%d: %s: inst %d entered.\n",
	    ha->host_no, __func__, ha->instance));

	if (ioctl->RequestLen > sizeof(INT_LOGOUT_ISCSI)) {
		QL4PRINT(QLP2|QLP4,
		    printk("scsi%d: %s: memory area too small\n",
		    ha->host_no, __func__));

		ioctl->Status = EXT_STATUS_BUFFER_TOO_SMALL;
		goto exit_logout;
	}

	/* --- Copy logout structure from user space --- */
	if ((status = copy_from_user((void *)&logout,
	    Q64BIT_TO_PTR(ioctl->RequestAdr), sizeof(INT_LOGOUT_ISCSI))) != 0) {
		QL4PRINT(QLP2|QLP4,
		    printk("scsi%d: %s: unable to copy data from "
		    "user's memory area\n", ha->host_no, __func__));

		ioctl->Status = EXT_STATUS_COPY_ERR;
		goto exit_logout;
	}

	/* --- Execute command --- */
	if (logout.Options == INT_DEF_CLOSE_SESSION) {
		if (qla4xxx_logout_device(ha, logout.TargetID,
		    logout.ConnectionID) == QLA_SUCCESS) {
			QL4PRINT(QLP4,
			    printk("scsi%d: %s: CLOSE_SESSION SUCCEEDED!, "
			    "target %d\n", ha->host_no, __func__,
			    logout.TargetID));

			ioctl->Status = EXT_STATUS_OK;
		} else {
			QL4PRINT(QLP2|QLP4,
			    printk("scsi%d: %s: CLOSE_SESSION FAILED!, "
			    "target %d\n", ha->host_no, __func__,
			    logout.TargetID));

			ioctl->Status = EXT_STATUS_ERR;
		}

	} else if (logout.Options == INT_DEF_RELOGIN_CONNECTION) {
		if (qla4xxx_login_device(ha, logout.TargetID,
		    logout.ConnectionID) == QLA_SUCCESS) {
			QL4PRINT(QLP4,
			    printk("scsi%d: %s: RELOGIN_CONNECTION "
			    "SUCCEEDED!, target %d\n",
			    ha->host_no, __func__, logout.TargetID));

			ioctl->Status = EXT_STATUS_OK;
		} else {
			QL4PRINT(QLP2|QLP4,
			    printk("scsi%d: %s: RELOGIN_CONNECTION "
			    "FAILED!, target %d\n",
			    ha->host_no, __func__, logout.TargetID));

			ioctl->Status = EXT_STATUS_ERR;
		}

	} else if (logout.Options == INT_DEF_DELETE_DDB) {
		if (qla4xxx_delete_device(ha, logout.TargetID,
		    logout.ConnectionID) == QLA_SUCCESS) {
			QL4PRINT(QLP4,
			    printk("scsi%d: %s: DELETE_DDB "
			    "SUCCEEDED!, target %d\n",
			    ha->host_no, __func__, logout.TargetID));

			ioctl->Status = EXT_STATUS_OK;
		} else {
			QL4PRINT(QLP2|QLP4,
			    printk("scsi%d: %s: DELETE_DDB FAILED!, "
			    "target %d\n",
			    ha->host_no, __func__, logout.TargetID));

			ioctl->Status = EXT_STATUS_ERR;
		}
	}

exit_logout:
	QL4PRINT(QLP4,
	    printk("scsi%d: %s: inst %d exiting.\n",
	    ha->host_no, __func__, ha->instance));
	LEAVE(__func__);

	return(status);
}

/**************************************************************************
 * qla4intioctl_ping
 *	This routine requests that the HBA PING the specified IP Address.
 *
 * Input:
 *	ha    = adapter structure pointer.
 *	ioctl = IOCTL structure pointer.
 *
 * Output:
 *	None
 *
 * Returns:
 *	QLA_SUCCESS = success
 *	QLA_ERROR   = error
 *
 * Context:
 *	Kernel context.
 **************************************************************************/
int
qla4intioctl_ping(scsi_qla_host_t *ha, EXT_IOCTL_ISCSI *ioctl)
{
	int		status = 0;
	INT_PING	ping;
	uint32_t	mbox_cmd[MBOX_REG_COUNT];
	uint32_t	mbox_sts[MBOX_REG_COUNT];

	ENTER(__func__);
	QL4PRINT(QLP4,
	    printk("scsi%d: %s: inst %d entered.\n",
	    ha->host_no, __func__, ha->instance));

	/*
	 * Copy user's data to local buffer
	 */
	if ((status = copy_from_user((uint8_t *)&ping,
	    Q64BIT_TO_PTR(ioctl->RequestAdr), sizeof(ping))) != 0) {
		QL4PRINT(QLP2|QLP4,
		    printk("scsi%d: %s: unable to copy data from "
		    "user's memory area\n", ha->host_no, __func__));

		ioctl->Status = EXT_STATUS_COPY_ERR;
		goto exit_ping;
	}

	/*
	 * Debug Print Statement
	 */
	if (ping.IPAddr.Type == EXT_DEF_TYPE_ISCSI_IP) {
		QL4PRINT(QLP4,
		    printk("scsi%d: %s: %d.%d.%d.%d\n",
		    ha->host_no, __func__,
		    ping.IPAddr.IPAddress[0],
		    ping.IPAddr.IPAddress[1],
		    ping.IPAddr.IPAddress[2],
		    ping.IPAddr.IPAddress[3]));
	} else {
		QL4PRINT(QLP4,
		    printk("scsi%d: %s: %d.%d.%d.%d. %d.%d.%d.%d. "
		    "%d.%d.%d.%d. %d.%d.%d.%d\n",
		    ha->host_no, __func__,
		    ping.IPAddr.IPAddress[0], ping.IPAddr.IPAddress[1],
		    ping.IPAddr.IPAddress[2], ping.IPAddr.IPAddress[3],
		    ping.IPAddr.IPAddress[4], ping.IPAddr.IPAddress[5],
		    ping.IPAddr.IPAddress[6], ping.IPAddr.IPAddress[7],
		    ping.IPAddr.IPAddress[8], ping.IPAddr.IPAddress[9],
		    ping.IPAddr.IPAddress[10], ping.IPAddr.IPAddress[11],
		    ping.IPAddr.IPAddress[12], ping.IPAddr.IPAddress[13],
		    ping.IPAddr.IPAddress[14], ping.IPAddr.IPAddress[15]));
	}

	/*
	 * Issue Mailbox Command
	 */
	memset(&mbox_cmd, 0, sizeof(mbox_cmd));
	memset(&mbox_sts, 0, sizeof(mbox_sts));
	mbox_cmd[0] = MBOX_CMD_PING;
	mbox_cmd[1] = ping.PacketCount;
	memcpy(&mbox_cmd[2], &ping.IPAddr.IPAddress, EXT_DEF_IP_ADDR_SIZE);

	if (qla4xxx_mailbox_command(ha, 6, 1, &mbox_cmd[0], &mbox_sts[0]) ==
	    QLA_ERROR) {
		QL4PRINT(QLP2|QLP4,
		    printk("scsi%d: %s: command failed \n",
		    ha->host_no, __func__));

		ioctl->Status = EXT_STATUS_ERR;
		ioctl->DetailStatus = mbox_sts[0];
		goto exit_ping;
	}

	ioctl->Status = EXT_STATUS_OK;
exit_ping:

	QL4PRINT(QLP4,
	    printk("scsi%d: %s: inst %d exiting.\n",
	    ha->host_no, __func__, ha->instance));
	LEAVE(__func__);

	return(status);
}

#if BYTE_ORDER_SUPPORT_ENABLED
static void
__xlate_sys_info(FLASH_SYS_INFO *from, FLASH_SYS_INFO *to,
    uint8_t direction)
{
	switch (direction) {
	case GET_DATA:
		from->cookie            = le32_to_cpu(to->cookie);
		from->physAddrCount     = le32_to_cpu(to->physAddrCount);
		memcpy(from->physAddr,  to->physAddr, sizeof(from->physAddr));
		memcpy(from->vendorId,  to->vendorId, sizeof(from->vendorId));
		memcpy(from->productId, to->productId, sizeof(from->productId));
		from->serialNumber      = le32_to_cpu(to->serialNumber);
		from->pciDeviceVendor   = le32_to_cpu(to->pciDeviceVendor);
		from->pciDeviceId       = le32_to_cpu(to->pciDeviceId);
		from->pciSubsysVendor   = le32_to_cpu(to->pciSubsysVendor);
		from->pciSubsysId       = le32_to_cpu(to->pciSubsysId);
		from->crumbs            = le32_to_cpu(to->crumbs);
		from->enterpriseNumber  = le32_to_cpu(to->enterpriseNumber);
		from->mtu               = le32_to_cpu(to->mtu);
		from->reserved0         = le32_to_cpu(to->reserved0);
		from->crumbs2           = le32_to_cpu(to->crumbs2);
		memcpy(from->acSerialNumber, to->acSerialNumber,
		    sizeof(from->acSerialNumber));
		from->crumbs3           = le32_to_cpu(to->crumbs3);
		memcpy(from->reserved1, to->reserved1, sizeof(from->reserved1));
		break;

	case SET_DATA:
		from->cookie            = cpu_to_le32(to->cookie);
		from->physAddrCount     = cpu_to_le32(to->physAddrCount);
		memcpy(from->physAddr,  to->physAddr, sizeof(from->physAddr));
		memcpy(from->vendorId,  to->vendorId, sizeof(from->vendorId));
		memcpy(from->productId, to->productId, sizeof(from->productId));
		from->serialNumber      = cpu_to_le32(to->serialNumber);
		from->pciDeviceVendor   = cpu_to_le32(to->pciDeviceVendor);
		from->pciDeviceId       = cpu_to_le32(to->pciDeviceId);
		from->pciSubsysVendor   = cpu_to_le32(to->pciSubsysVendor);
		from->pciSubsysId       = cpu_to_le32(to->pciSubsysId);
		from->crumbs            = cpu_to_le32(to->crumbs);
		from->enterpriseNumber  = cpu_to_le32(to->enterpriseNumber);
		from->mtu               = cpu_to_le32(to->mtu);
		from->reserved0         = cpu_to_le32(to->reserved0);
		from->crumbs2           = cpu_to_le32(to->crumbs2);
		memcpy(from->acSerialNumber, to->acSerialNumber,
		    sizeof(from->acSerialNumber));
		from->crumbs3           = cpu_to_le32(to->crumbs3);
		memcpy(from->reserved1, to->reserved1, sizeof(from->reserved1));
		break;
	}
}

static void
__xlate_driver_info(INT_FLASH_DRIVER_PARAM *from,
    INT_FLASH_DRIVER_PARAM *to, uint8_t direction)
{
	switch (direction) {
	case GET_DATA:
		from->DiscoveryTimeOut = le16_to_cpu(to->DiscoveryTimeOut);
		from->PortDownTimeout = le16_to_cpu(to->PortDownTimeout);
		memcpy(from->Reserved, to->Reserved, sizeof(from->Reserved));
		break;

	case SET_DATA:
		from->DiscoveryTimeOut = cpu_to_le32(to->DiscoveryTimeOut);
		from->PortDownTimeout = cpu_to_le32(to->PortDownTimeout);
		memcpy(from->Reserved, to->Reserved, sizeof(from->Reserved));
		break;
	}
}

static void
__xlate_init_fw_ctrl_blk(INIT_FW_CTRL_BLK *from,
    INIT_FW_CTRL_BLK *to, uint8_t direction)
{
	switch (direction) {
	case GET_DATA:
		from->Version           = to->Version;
		from->Control           = to->Control;
		from->FwOptions         = le16_to_cpu(to->FwOptions);
		from->ExecThrottle      = le16_to_cpu(to->ExecThrottle);
		from->RetryCount        = to->RetryCount;
		from->RetryDelay        = to->RetryDelay;
		from->MaxEthFrPayloadSize = le16_to_cpu(to->MaxEthFrPayloadSize);
		from->AddFwOptions      = le16_to_cpu(to->AddFwOptions);
		from->HeartbeatInterval = to->HeartbeatInterval;
		from->InstanceNumber    = to->InstanceNumber;
		from->RES2              = le16_to_cpu(to->RES2);
		from->ReqQConsumerIndex = le16_to_cpu(to->ReqQConsumerIndex);
		from->ComplQProducerIndex = le16_to_cpu(to->ComplQProducerIndex);
		from->ReqQLen           = le16_to_cpu(to->ReqQLen);
		from->ComplQLen         = le16_to_cpu(to->ComplQLen);
		from->ReqQAddrLo        = le32_to_cpu(to->ReqQAddrLo);
		from->ReqQAddrHi        = le32_to_cpu(to->ReqQAddrHi);
		from->ComplQAddrLo      = le32_to_cpu(to->ComplQAddrLo);
		from->ComplQAddrHi      = le32_to_cpu(to->ComplQAddrHi);
		from->ShadowRegBufAddrLo= le32_to_cpu(to->ShadowRegBufAddrLo);
		from->ShadowRegBufAddrHi= le32_to_cpu(to->ShadowRegBufAddrHi);
		from->iSCSIOptions      = le16_to_cpu(to->iSCSIOptions);
		from->TCPOptions        = le16_to_cpu(to->TCPOptions);
		from->IPOptions         = le16_to_cpu(to->IPOptions);
		from->MaxPDUSize        = le16_to_cpu(to->MaxPDUSize);
		from->RcvMarkerInt      = le16_to_cpu(to->RcvMarkerInt);
		from->SndMarkerInt      = le16_to_cpu(to->SndMarkerInt);
		from->InitMarkerlessInt = le16_to_cpu(to->InitMarkerlessInt);
		from->FirstBurstSize    = le16_to_cpu(to->FirstBurstSize);
		from->DefaultTime2Wait  = le16_to_cpu(to->DefaultTime2Wait);
		from->DefaultTime2Retain= le16_to_cpu(to->DefaultTime2Retain);
		from->MaxOutStndngR2T   = le16_to_cpu(to->MaxOutStndngR2T);
		from->KeepAliveTimeout  = le16_to_cpu(to->KeepAliveTimeout);
		from->PortNumber        = le16_to_cpu(to->PortNumber);
		from->MaxBurstSize      = le16_to_cpu(to->MaxBurstSize);
		from->RES4              = le32_to_cpu(to->RES4);
		memcpy(from->IPAddr, to->IPAddr, sizeof(from->IPAddr));
		memcpy(from->RES5, to->RES5, sizeof(from->RES5));
		memcpy(from->SubnetMask, to->SubnetMask,
		    sizeof(from->SubnetMask));
		memcpy(from->RES6, to->RES6, sizeof(from->RES6));
		memcpy(from->GatewayIPAddr, to->GatewayIPAddr,
		    sizeof(from->GatewayIPAddr));
		memcpy(from->RES7, to->RES7, sizeof(from->RES7));
		memcpy(from->PriDNSIPAddr, to->PriDNSIPAddr,
		    sizeof(from->PriDNSIPAddr));
		memcpy(from->SecDNSIPAddr, to->SecDNSIPAddr,
		    sizeof(from->SecDNSIPAddr));
		memcpy(from->RES8, to->RES8, sizeof(from->RES8));
		memcpy(from->Alias, to->Alias, sizeof(from->Alias));
		memcpy(from->TargAddr, to->TargAddr, sizeof(from->TargAddr));
		memcpy(from->CHAPNameSecretsTable, to->CHAPNameSecretsTable,
		    sizeof(from->CHAPNameSecretsTable));
		memcpy(from->EthernetMACAddr, to->EthernetMACAddr,
		    sizeof(from->EthernetMACAddr));
		from->TargetPortalGroup = le16_to_cpu(to->TargetPortalGroup);
		from->SendScale         = to->SendScale;
		from->RecvScale         = to->RecvScale;
		from->TypeOfService     = to->TypeOfService;
		from->Time2Live         = to->Time2Live;
		from->VLANPriority      = le16_to_cpu(to->VLANPriority);
		from->Reserved8         = le16_to_cpu(to->Reserved8);
		memcpy(from->SecIPAddr, to->SecIPAddr, sizeof(from->SecIPAddr));
		memcpy(from->Reserved9, to->Reserved9, sizeof(from->Reserved9));
		memcpy(from->iSNSIPAddr, to->iSNSIPAddr,
		    sizeof(from->iSNSIPAddr));
		memcpy(from->Reserved10, to->Reserved10,
		    sizeof(from->Reserved10));
		from->iSNSClientPortNumber =
		    le16_to_cpu(to->iSNSClientPortNumber);
		from->iSNSServerPortNumber =
		    le16_to_cpu(to->iSNSServerPortNumber);
		from->iSNSSCNPortNumber = le16_to_cpu(to->iSNSSCNPortNumber);
		from->iSNSESIPortNumber = le16_to_cpu(to->iSNSESIPortNumber);
		memcpy(from->SLPDAIPAddr, to->SLPDAIPAddr,
		    sizeof(from->SLPDAIPAddr));
		memcpy(from->Reserved11, to->Reserved11,
		    sizeof(from->Reserved11));
		memcpy(from->iSCSINameString, to->iSCSINameString,
		    sizeof(from->iSCSINameString));
		break;

	case SET_DATA:
		from->Version           = to->Version;
		from->Control           = to->Control;
		from->FwOptions         = cpu_to_le16(to->FwOptions);
		from->ExecThrottle      = cpu_to_le16(to->ExecThrottle);
		from->RetryCount        = to->RetryCount;
		from->RetryDelay        = to->RetryDelay;
		from->MaxEthFrPayloadSize = cpu_to_le16(to->MaxEthFrPayloadSize);
		from->AddFwOptions      = cpu_to_le16(to->AddFwOptions);
		from->HeartbeatInterval = to->HeartbeatInterval;
		from->InstanceNumber    = to->InstanceNumber;
		from->RES2              = cpu_to_le16(to->RES2);
		from->ReqQConsumerIndex = cpu_to_le16(to->ReqQConsumerIndex);
		from->ComplQProducerIndex = cpu_to_le16(to->ComplQProducerIndex);
		from->ReqQLen           = cpu_to_le16(to->ReqQLen);
		from->ComplQLen         = cpu_to_le16(to->ComplQLen);
		from->ReqQAddrLo        = cpu_to_le32(to->ReqQAddrLo);
		from->ReqQAddrHi        = cpu_to_le32(to->ReqQAddrHi);
		from->ComplQAddrLo      = cpu_to_le32(to->ComplQAddrLo);
		from->ComplQAddrHi      = cpu_to_le32(to->ComplQAddrHi);
		from->ShadowRegBufAddrLo= cpu_to_le32(to->ShadowRegBufAddrLo);
		from->ShadowRegBufAddrHi= cpu_to_le32(to->ShadowRegBufAddrHi);
		from->iSCSIOptions      = cpu_to_le16(to->iSCSIOptions);
		from->TCPOptions        = cpu_to_le16(to->TCPOptions);
		from->IPOptions         = cpu_to_le16(to->IPOptions);
		from->MaxPDUSize        = cpu_to_le16(to->MaxPDUSize);
		from->RcvMarkerInt      = cpu_to_le16(to->RcvMarkerInt);
		from->SndMarkerInt      = cpu_to_le16(to->SndMarkerInt);
		from->InitMarkerlessInt = cpu_to_le16(to->InitMarkerlessInt);
		from->FirstBurstSize    = cpu_to_le16(to->FirstBurstSize);
		from->DefaultTime2Wait  = cpu_to_le16(to->DefaultTime2Wait);
		from->DefaultTime2Retain= cpu_to_le16(to->DefaultTime2Retain);
		from->MaxOutStndngR2T   = cpu_to_le16(to->MaxOutStndngR2T);
		from->KeepAliveTimeout  = cpu_to_le16(to->KeepAliveTimeout);
		from->PortNumber        = cpu_to_le16(to->PortNumber);
		from->MaxBurstSize      = cpu_to_le16(to->MaxBurstSize);
		from->RES4              = cpu_to_le32(to->RES4);
		memcpy(from->IPAddr, to->IPAddr, sizeof(from->IPAddr));
		memcpy(from->RES5, to->RES5, sizeof(from->RES5));
		memcpy(from->SubnetMask, to->SubnetMask,
		    sizeof(from->SubnetMask));
		memcpy(from->RES6, to->RES6, sizeof(from->RES6));
		memcpy(from->GatewayIPAddr, to->GatewayIPAddr,
		    sizeof(from->GatewayIPAddr));
		memcpy(from->RES7, to->RES7, sizeof(from->RES7));
		memcpy(from->PriDNSIPAddr, to->PriDNSIPAddr,
		    sizeof(from->PriDNSIPAddr));
		memcpy(from->SecDNSIPAddr, to->SecDNSIPAddr,
		    sizeof(from->SecDNSIPAddr));
		memcpy(from->RES8, to->RES8, sizeof(from->RES8));
		memcpy(from->Alias, to->Alias, sizeof(from->Alias));
		memcpy(from->TargAddr, to->TargAddr, sizeof(from->TargAddr));
		memcpy(from->CHAPNameSecretsTable, to->CHAPNameSecretsTable,
		    sizeof(from->CHAPNameSecretsTable));
		memcpy(from->EthernetMACAddr, to->EthernetMACAddr,
		    sizeof(from->EthernetMACAddr));
		from->TargetPortalGroup = cpu_to_le16(to->TargetPortalGroup);
		from->SendScale         = to->SendScale;
		from->RecvScale         = to->RecvScale;
		from->TypeOfService     = to->TypeOfService;
		from->Time2Live         = to->Time2Live;
		from->VLANPriority      = cpu_to_le16(to->VLANPriority);
		from->Reserved8         = cpu_to_le16(to->Reserved8);
		memcpy(from->SecIPAddr, to->SecIPAddr, sizeof(from->SecIPAddr));
		memcpy(from->Reserved9, to->Reserved9, sizeof(from->Reserved9));
		memcpy(from->iSNSIPAddr, to->iSNSIPAddr,
		    sizeof(from->iSNSIPAddr));
		memcpy(from->Reserved10, to->Reserved10,
		    sizeof(from->Reserved10));
		from->iSNSClientPortNumber =
		    cpu_to_le16(to->iSNSClientPortNumber);
		from->iSNSServerPortNumber =
		    cpu_to_le16(to->iSNSServerPortNumber);
		from->iSNSSCNPortNumber = cpu_to_le16(to->iSNSSCNPortNumber);
		from->iSNSESIPortNumber = cpu_to_le16(to->iSNSESIPortNumber);
		memcpy(from->SLPDAIPAddr, to->SLPDAIPAddr,
		    sizeof(from->SLPDAIPAddr));
		memcpy(from->Reserved11, to->Reserved11,
		    sizeof(from->Reserved11));
		memcpy(from->iSCSINameString, to->iSCSINameString,
		    sizeof(from->iSCSINameString));
		break;
	}
}

static void
__xlate_dev_db(DEV_DB_ENTRY *from, DEV_DB_ENTRY *to,
    uint8_t direction)
{
	switch (direction) {
	case GET_DATA:
		from->options           = to->options;
		from->control           = to->control;
		from->exeThrottle       = le16_to_cpu(to->exeThrottle);
		from->exeCount          = le16_to_cpu(to->exeCount);
		from->retryCount        = to->retryCount;
		from->retryDelay        = to->retryDelay;
		from->iSCSIOptions      = le16_to_cpu(to->iSCSIOptions);
		from->TCPOptions        = le16_to_cpu(to->TCPOptions);
		from->IPOptions         = le16_to_cpu(to->IPOptions);
		from->maxPDUSize        = le16_to_cpu(to->maxPDUSize);
		from->rcvMarkerInt      = le16_to_cpu(to->rcvMarkerInt);
		from->sndMarkerInt      = le16_to_cpu(to->sndMarkerInt);
		from->iSCSIMaxSndDataSegLen =
		    le16_to_cpu(to->iSCSIMaxSndDataSegLen);
		from->firstBurstSize    = le16_to_cpu(to->firstBurstSize);
		from->minTime2Wait      = le16_to_cpu(to->minTime2Wait);
		from->maxTime2Retain    = le16_to_cpu(to->maxTime2Retain);
		from->maxOutstndngR2T   = le16_to_cpu(to->maxOutstndngR2T);
		from->keepAliveTimeout  = le16_to_cpu(to->keepAliveTimeout);
		memcpy(from->ISID, to->ISID, sizeof(from->ISID));
		from->TSID              = le16_to_cpu(to->TSID);
		from->portNumber        = le16_to_cpu(to->portNumber);
		from->maxBurstSize      = le16_to_cpu(to->maxBurstSize);
		from->taskMngmntTimeout = le16_to_cpu(to->taskMngmntTimeout);
		from->reserved1         = le16_to_cpu(to->reserved1);
		memcpy(from->ipAddr, to->ipAddr, sizeof(from->ipAddr));
		memcpy(from->iSCSIAlias, to->iSCSIAlias,
		    sizeof(from->iSCSIAlias));
		memcpy(from->targetAddr, to->targetAddr,
		    sizeof(from->targetAddr));
		memcpy(from->userID, to->userID, sizeof(from->userID));
		memcpy(from->password, to->password, sizeof(from->password));
		memcpy(from->iscsiName, to->iscsiName, sizeof(from->iscsiName));
		from->ddbLink           = le16_to_cpu(to->ddbLink);
		from->CHAPTableIndex    = le16_to_cpu(to->CHAPTableIndex);
		memcpy(from->reserved2, to->reserved2, sizeof(from->reserved2));
		from->Cookie            = le16_to_cpu(to->Cookie);
		break;

	case SET_DATA:
		from->options           = to->options;
		from->control           = to->control;
		from->exeThrottle       = cpu_to_le16(to->exeThrottle);
		from->exeCount          = cpu_to_le16(to->exeCount);
		from->retryCount        = to->retryCount;
		from->retryDelay        = to->retryDelay;
		from->iSCSIOptions      = cpu_to_le16(to->iSCSIOptions);
		from->TCPOptions        = cpu_to_le16(to->TCPOptions);
		from->IPOptions         = cpu_to_le16(to->IPOptions);
		from->maxPDUSize        = cpu_to_le16(to->maxPDUSize);
		from->rcvMarkerInt      = cpu_to_le16(to->rcvMarkerInt);
		from->sndMarkerInt      = cpu_to_le16(to->sndMarkerInt);
		from->iSCSIMaxSndDataSegLen =
		    cpu_to_le16(to->iSCSIMaxSndDataSegLen);
		from->firstBurstSize    = cpu_to_le16(to->firstBurstSize);
		from->minTime2Wait      = cpu_to_le16(to->minTime2Wait);
		from->maxTime2Retain    = cpu_to_le16(to->maxTime2Retain);
		from->maxOutstndngR2T   = cpu_to_le16(to->maxOutstndngR2T);
		from->keepAliveTimeout  = cpu_to_le16(to->keepAliveTimeout);
		memcpy(from->ISID, to->ISID, sizeof(from->ISID));
		from->TSID              = cpu_to_le16(to->TSID);
		from->portNumber        = cpu_to_le16(to->portNumber);
		from->maxBurstSize      = cpu_to_le16(to->maxBurstSize);
		from->taskMngmntTimeout = cpu_to_le16(to->taskMngmntTimeout);
		from->reserved1         = cpu_to_le16(to->reserved1);
		memcpy(from->ipAddr, to->ipAddr, sizeof(from->ipAddr));
		memcpy(from->iSCSIAlias, to->iSCSIAlias,
		    sizeof(from->iSCSIAlias));
		memcpy(from->targetAddr, to->targetAddr,
		    sizeof(from->targetAddr));
		memcpy(from->userID, to->userID, sizeof(from->userID));
		memcpy(from->password, to->password, sizeof(from->password));
		memcpy(from->iscsiName, to->iscsiName, sizeof(from->iscsiName));
		from->ddbLink           = cpu_to_le16(to->ddbLink);
		from->CHAPTableIndex    = cpu_to_le16(to->CHAPTableIndex);
		memcpy(from->reserved2, to->reserved2, sizeof(from->reserved2));
		from->Cookie            = cpu_to_le16(to->Cookie);
		break;
	}
}

static void
__xlate_chap(CHAP_ENTRY *from, CHAP_ENTRY *to, uint8_t direction)
{
	switch (direction) {
	case GET_DATA:
		from->link              = le16_to_cpu(to->link);
		from->flags             = to->flags;
		from->secretLength      = to->secretLength;
		memcpy(from->secret, to->secret, sizeof(from->secret));
		memcpy(from->user_name, to->user_name, sizeof(from->user_name));
		from->reserved          = le16_to_cpu(to->reserved);
		from->cookie            = le16_to_cpu(to->cookie);
		break;

	case SET_DATA:
		from->link              = cpu_to_le16(to->link);
		from->flags             = to->flags;
		from->secretLength      = to->secretLength;
		memcpy(from->secret, to->secret, sizeof(from->secret));
		memcpy(from->user_name, to->user_name, sizeof(from->user_name));
		from->reserved          = cpu_to_le16(to->reserved);
		from->cookie            = cpu_to_le16(to->cookie);
		break;
	}
}
#endif

/**************************************************************************
 * qla4intioctl_get_flash
 *	This routine reads the requested area of FLASH.
 *
 * Input:
 *	ha    = adapter structure pointer.
 *	ioctl = IOCTL structure pointer.
 *
 * Output:
 *	None
 *
 * Returns:
 *	QLA_SUCCESS = success
 *	QLA_ERROR   = error
 *
 * Context:
 *	Kernel context.
 **************************************************************************/
static int
qla4intioctl_get_flash(scsi_qla_host_t *ha, EXT_IOCTL_ISCSI *ioctl)
{
	int		status = 0;
	void		*local_dma_bufv = NULL;
	dma_addr_t	local_dma_bufp;
	INT_ACCESS_FLASH *paccess_flash = NULL;
	uint32_t	mbox_cmd[MBOX_REG_COUNT];
	uint32_t	mbox_sts[MBOX_REG_COUNT];


	ENTER(__func__);
	QL4PRINT(QLP4,
	    printk("scsi%d: %s: inst %d entered.\n",
	    ha->host_no, __func__, ha->instance));

	/*
	 * Allocate local flash buffer
	 */
	if (qla4xxx_get_ioctl_scrap_mem(ha, (void **)&paccess_flash,
	    sizeof(INT_ACCESS_FLASH))) {
		/* not enough memory */
		ioctl->Status = EXT_STATUS_NO_MEMORY;
		QL4PRINT(QLP2|QLP4,
		    printk("%s(%d): inst=%d scrap not big enough. "
		    "size requested=%ld.\n",
		    __func__, ha->host_no, ha->instance,
		    (ulong)sizeof(INT_ACCESS_FLASH)));
		goto exit_get_flash;
	}

	/*
	 * Copy user's data to local flash buffer
	 */
	if ((status = copy_from_user((uint8_t *)paccess_flash,
	    Q64BIT_TO_PTR(ioctl->RequestAdr), sizeof(INT_ACCESS_FLASH))) != 0) {
		QL4PRINT(QLP2|QLP4,
		    printk("scsi%d: %s: unable to copy data from user's "
		    "memory area\n", ha->host_no, __func__));

		ioctl->Status = EXT_STATUS_COPY_ERR;
		goto exit_get_flash;
	}

	/*
	 * Allocate DMA memory
	 */
	local_dma_bufv = pci_alloc_consistent(ha->pdev, paccess_flash->DataLen,
	    &local_dma_bufp);
	if (local_dma_bufv == NULL) {
		QL4PRINT(QLP2|QLP4,
		    printk("scsi%d: %s: unable to allocate dma memory\n",
		    ha->host_no, __func__));

		ioctl->Status = EXT_STATUS_NO_MEMORY;
		goto exit_get_flash;
	}

	QL4PRINT(QLP4,
	    printk("scsi%d: %s: offset=%08x, len=%08x\n",
	    ha->host_no, __func__,
	    paccess_flash->DataOffset, paccess_flash->DataLen));

	/*
	 * Issue Mailbox Command
	 */
	memset(&mbox_cmd, 0, sizeof(mbox_cmd));
	memset(&mbox_sts, 0, sizeof(mbox_sts));
	mbox_cmd[0] = MBOX_CMD_READ_FLASH;
	mbox_cmd[1] = LSDW(local_dma_bufp);
	mbox_cmd[2] = MSDW(local_dma_bufp);
	mbox_cmd[3] = paccess_flash->DataOffset;
	mbox_cmd[4] = paccess_flash->DataLen;

	if (qla4xxx_mailbox_command(ha, 5, 2, &mbox_cmd[0], &mbox_sts[0]) ==
	    QLA_ERROR) {
		QL4PRINT(QLP2|QLP4,
		    printk("scsi%d: %s: command failed \n",
		    ha->host_no, __func__));

		ioctl->Status = EXT_STATUS_ERR;
		ioctl->DetailStatus = mbox_sts[0];
		goto exit_get_flash;
	}

	//FIXME: For byte-order support, this entire structure must be translated
#if BYTE_ORDER_SUPPORT_ENABLED
	/*
	 * Copy data from DMA buffer into access_flash->FlashData buffer
	 * (in the process, translating for byte-order support, if necessary)
	 */
	switch (paccess_flash->DataOffset & INT_ISCSI_PAGE_MASK) {
	case INT_ISCSI_FW_IMAGE2_FLASH_OFFSET:
	case INT_ISCSI_FW_IMAGE1_FLASH_OFFSET:
		break;
	case INT_ISCSI_SYSINFO_FLASH_OFFSET:
		__xlate_sys_info((FLASH_SYS_INFO *) local_dma_bufv,
		    (FLASH_SYS_INFO *) &paccess_flash->FlashData[0],
		    ioctl->SubCode);
		break;
	case INT_ISCSI_DRIVER_FLASH_OFFSET:
		__xlate_driver_info((INT_FLASH_DRIVER_PARAM *) local_dma_bufv,
		    (INT_FLASH_DRIVER_PARAM *) &paccess_flash->FlashData[0],
		    ioctl->SubCode);
		break;
	case INT_ISCSI_INITFW_FLASH_OFFSET:
		__xlate_init_fw_ctrl_blk((INIT_FW_CTRL_BLK *) local_dma_bufv,
		    (INIT_FW_CTRL_BLK *) &paccess_flash->FlashData[0],
		    ioctl->SubCode);
		break;
	case INT_ISCSI_DDB_FLASH_OFFSET:
		__xlate_dev_db((DEV_DB_ENTRY *)local_dma_bufv,
		    (DEV_DB_ENTRY *) &paccess_flash->FlashData[0],
		    ioctl->SubCode);
		break;
	case INT_ISCSI_CHAP_FLASH_OFFSET:
		__xlate_chap((CHAP_ENTRY *) local_dma_bufv,
		    (CHAP_ENTRY *) &paccess_flash->FlashData[0],
		    ioctl->SubCode);
		break;
	}
#else
	memcpy(&paccess_flash->FlashData[0], local_dma_bufv,
	    MIN(paccess_flash->DataLen, sizeof(paccess_flash->FlashData)));

#endif

	/*
	 * Copy local DMA buffer to user's response data area
	 */
	if ((status = copy_to_user(Q64BIT_TO_PTR(ioctl->ResponseAdr),
	    paccess_flash, sizeof(*paccess_flash))) != 0) {
		QL4PRINT(QLP2|QLP4,
		    printk("scsi%d: %s: unable to copy data to user's "
		    "memory area\n", ha->host_no, __func__));

		ioctl->Status = EXT_STATUS_COPY_ERR;
		goto exit_get_flash;
	}

	ioctl->Status = EXT_STATUS_OK;
	ioctl->ResponseLen = paccess_flash->DataLen;

	QL4PRINT(QLP4|QLP10,
	    printk("INT_ACCESS_FLASH buffer (1st 60h bytes only):\n"));
	qla4xxx_dump_bytes(QLP4|QLP10, paccess_flash, 0x60);

exit_get_flash:

	if (local_dma_bufv)
		pci_free_consistent(ha->pdev,
		    paccess_flash->DataLen, local_dma_bufv, local_dma_bufp);

	qla4xxx_free_ioctl_scrap_mem(ha);

	QL4PRINT(QLP4,
	    printk("scsi%d: %s: inst %d exiting.\n",
	    ha->host_no, __func__, ha->instance));
	LEAVE(__func__);

	return(status);
}

/**************************************************************************
 * qla4intioctl_get_driver_debug_level
 *	This routine retrieves the driver's debug print level.
 *
 * Input:
 *	ha    = adapter structure pointer.
 *	ioctl = IOCTL structure pointer.
 *
 * Output:
 *	None
 *
 * Returns:
 *	QLA_SUCCESS = success
 *	QLA_ERROR   = error
 *
 * Context:
 *	Kernel context.
 **************************************************************************/
static int
qla4intioctl_get_driver_debug_level(scsi_qla_host_t *ha, EXT_IOCTL_ISCSI *ioctl)
{
	int status = 0;
	uint32_t dbg_level;


	ENTER(__func__);
	QL4PRINT(QLP4,
	    printk("scsi%d: %s: inst %d entered.\n",
	    ha->host_no, __func__, ha->instance));

	if (qla4xxx_get_debug_level(&dbg_level) == QLA_ERROR) {
		QL4PRINT(QLP2|QLP4,
		    printk("scsi%d: %s: unable to set debug level, "
		    "debug driver not loaded!\n",
		    ha->host_no, __func__));

		ioctl->Status = EXT_STATUS_ERR;
		goto exit_get_driver_debug_level;
	}

	if ((status = copy_to_user(Q64BIT_TO_PTR(ioctl->ResponseAdr),
	    &dbg_level, sizeof(dbg_level))) != 0) {
		QL4PRINT(QLP2|QLP4,
		    printk("scsi%d: %s: failed to copy data\n",
		    ha->host_no, __func__));

		ioctl->Status = EXT_STATUS_COPY_ERR;
		goto exit_get_driver_debug_level;
	}

	QL4PRINT(QLP4,
	    printk("scsi%d: %s: debug level is %04x\n",
	    ha->host_no, __func__, dbg_level));

	ioctl->Status = EXT_STATUS_OK;

exit_get_driver_debug_level:
	QL4PRINT(QLP4,
	    printk("scsi%d: %s: inst %d exiting.\n",
	    ha->host_no, __func__, ha->instance));
	LEAVE(__func__);

	return(status);
}

/**************************************************************************
 * qla4intioctl_get_host_no
 *	This routine retrieves the host number for the specified adapter
 *	instance.
 *
 * Input:
 *	ha    = adapter structure pointer.
 *	ioctl = IOCTL structure pointer.
 *
 * Output:
 *	None
 *
 * Returns:
 *	QLA_SUCCESS = success
 *	QLA_ERROR   = error
 *
 * Context:
 *	Kernel context.
 **************************************************************************/
static int
qla4intioctl_get_host_no(scsi_qla_host_t *ha, EXT_IOCTL_ISCSI *ioctl)
{
	int status = 0;


	ENTER(__func__);
	QL4PRINT(QLP4,
	    printk("scsi%d: %s: inst %d entered.\n",
	    ha->host_no, __func__, ha->instance));

	if ((status = copy_to_user(Q64BIT_TO_PTR(ioctl->ResponseAdr),
	    &(ha->host_no), sizeof(ha->host_no))) != 0) {
		QL4PRINT(QLP2|QLP4,
		    printk("scsi%d: %s: failed to copy data\n",
		    ha->host_no, __func__));

		ioctl->Status = EXT_STATUS_COPY_ERR;
	} else {
		ioctl->Status = EXT_STATUS_OK;
	}

	QL4PRINT(QLP4,
	    printk("scsi%d: %s: inst %d exiting.\n",
	    ha->host_no, __func__, ha->instance));
	LEAVE(__func__);

	return(status);
}

/**************************************************************************
 * qla4intioctl_get_data
 *	This routine calls get data IOCTLs based on the IOCTL Sub Code.
 *
 * Input:
 *	ha    = adapter structure pointer.
 *	ioctl = IOCTL structure pointer.
 *
 * Output:
 *	None
 *
 * Returns:
 *	QLA_SUCCESS = success
 *	QLA_ERROR   = error
 *    	-EINVAL     = if the command is invalid
 *
 * Context:
 *	Kernel context.
 **************************************************************************/
int
qla4intioctl_get_data(scsi_qla_host_t *ha, EXT_IOCTL_ISCSI *ioctl)
{
	int	status = 0;

	switch (ioctl->SubCode) {
	case INT_SC_GET_FLASH:
		status = qla4intioctl_get_flash(ha, ioctl);
		break;
	case INT_SC_GET_DRIVER_DEBUG_LEVEL:
		status = qla4intioctl_get_driver_debug_level(ha, ioctl);
		break;
	case INT_SC_GET_HOST_NO:
		status = qla4intioctl_get_host_no(ha, ioctl);
		break;
	default:
		QL4PRINT(QLP2|QLP4,
		    printk("scsi%d: %s: unsupported internal get data "
		    "sub-command code (%X)\n",
		    ha->host_no, __func__, ioctl->SubCode));

		ioctl->Status = EXT_STATUS_UNSUPPORTED_SUBCODE;
		break;
	}

	return status;
}

/**************************************************************************
 * qla4intioctl_set_flash
 *	This routine writes the requested area of FLASH.
 *
 * Input:
 *	ha    = adapter structure pointer.
 *	ioctl = IOCTL structure pointer.
 *
 * Output:
 *	None
 *
 * Returns:
 *	QLA_SUCCESS = success
 *	QLA_ERROR   = error
 *
 * Context:
 *	Kernel context.
 **************************************************************************/
static int
qla4intioctl_set_flash(scsi_qla_host_t *ha, EXT_IOCTL_ISCSI *ioctl)
{
	int		status = 0;
	INT_ACCESS_FLASH *paccess_flash;
	uint32_t	mbox_cmd[MBOX_REG_COUNT];
	uint32_t	mbox_sts[MBOX_REG_COUNT];


	ENTER(__func__);
	QL4PRINT(QLP4,
	    printk("scsi%d: %s: inst %d entered.\n",
	    ha->host_no, __func__, ha->instance));

	/*
	 * Allocate local flash buffer
	 */
	if (qla4xxx_get_ioctl_scrap_mem(ha, (void **)&paccess_flash,
	    sizeof(INT_ACCESS_FLASH))) {
		/* not enough memory */
		ioctl->Status = EXT_STATUS_NO_MEMORY;
		QL4PRINT(QLP2|QLP4,
		    printk("%s(%d): inst=%d scrap not big enough. "
		    "size requested=%ld.\n",
		    __func__, ha->host_no, ha->instance,
		    (ulong)sizeof(INT_ACCESS_FLASH)));
		goto exit_set_flash;
	}

	/*
	 * Copy user's data to local DMA buffer
	 */
	if ((status = copy_from_user((uint8_t *)paccess_flash,
	    Q64BIT_TO_PTR(ioctl->RequestAdr), sizeof(INT_ACCESS_FLASH))) != 0) {
		QL4PRINT(QLP2|QLP4,
		    printk("scsi%d: %s: unable to copy data from user's "
		    "memory area\n", ha->host_no, __func__));

		ioctl->Status = EXT_STATUS_COPY_ERR;
		goto exit_set_flash;
	}

	/*
	 * Resizr IOCTL DMA memory, if necesary
	 */
	if ((paccess_flash->DataLen != 0) &&
	    (ha->ioctl_dma_buf_len < paccess_flash->DataLen)) {
		if (qla4xxx_resize_ioctl_dma_buf(ha, paccess_flash->DataLen) !=
		    QLA_SUCCESS) {
			QL4PRINT(QLP2|QLP4,
			    printk("scsi%d: %s: unable to allocate memory "
			    "for dma buffer.\n",
			    ha->host_no, __func__));
			ioctl->Status = EXT_STATUS_NO_MEMORY;
			goto exit_set_flash;
		}
	}

	//FIXME: For byte-order support, this entire structure must be translated
#if BYTE_ORDER_SUPPORT_ENABLED
	/*
	 * Copy data from DMA buffer into access_flash->FlashData buffer
	 * (in the process, translating for byte-order support, if necessary)
	 */
	switch (paccess_flash->DataOffset & INT_ISCSI_PAGE_MASK) {
	case INT_ISCSI_FW_IMAGE2_FLASH_OFFSET:
	case INT_ISCSI_FW_IMAGE1_FLASH_OFFSET:
		break;
	case INT_ISCSI_SYSINFO_FLASH_OFFSET:
		__xlate_sys_info((FLASH_SYS_INFO *)&paccess_flash->FlashData[0],
		    (FLASH_SYS_INFO *) ha->ioctl_dma_bufv, SET_DATA);
		break;
	case INT_ISCSI_DRIVER_FLASH_OFFSET:
		__xlate_driver_info(
		    (INT_FLASH_DRIVER_PARAM *) &paccess_flash->FlashData[0],
		    (INT_FLASH_DRIVER_PARAM *) ha->ioctl_dma_bufv,
		    SET_DATA);
		break;
	case INT_ISCSI_INITFW_FLASH_OFFSET:
		__xlate_init_fw_ctrl_blk(
		    (INIT_FW_CTRL_BLK *) &paccess_flash->FlashData[0],
		    (INIT_FW_CTRL_BLK *) ha->ioctl_dma_bufv, SET_DATA);
		break;
	case INT_ISCSI_DDB_FLASH_OFFSET:
		__xlate_dev_db((DEV_DB_ENTRY *) &paccess_flash->FlashData[0],
		    (DEV_DB_ENTRY *) ha->ioctl_dma_bufv, SET_DATA);
		break;
	case INT_ISCSI_CHAP_FLASH_OFFSET:
		__xlate_chap((CHAP_ENTRY *) &paccess_flash->FlashData[0],
		    (CHAP_ENTRY *) ha->ioctl_dma_bufv, SET_DATA);
		break;
	}
#else
	memcpy(ha->ioctl_dma_bufv, &paccess_flash->FlashData[0],
	    MIN(ha->ioctl_dma_buf_len, sizeof(paccess_flash->FlashData)));

#endif

	/*
	 * Issue Mailbox Command
	 */
	memset(&mbox_cmd, 0, sizeof(mbox_cmd));
	memset(&mbox_sts, 0, sizeof(mbox_sts));
	mbox_cmd[0] = MBOX_CMD_WRITE_FLASH;
	mbox_cmd[1] = LSDW(ha->ioctl_dma_bufp);
	mbox_cmd[2] = MSDW(ha->ioctl_dma_bufp);
	mbox_cmd[3] = paccess_flash->DataOffset;
	mbox_cmd[4] = paccess_flash->DataLen;
	mbox_cmd[5] = paccess_flash->Options;

	if (qla4xxx_mailbox_command(ha, 6, 2, &mbox_cmd[0], &mbox_sts[0]) ==
	    QLA_ERROR) {
		QL4PRINT(QLP2|QLP4,
		    printk("scsi%d: %s: command failed \n",
		    ha->host_no, __func__));

		ioctl->Status = EXT_STATUS_ERR;
		ioctl->DetailStatus = mbox_sts[0];
		ioctl->VendorSpecificStatus[0] = mbox_sts[1];
		goto exit_set_flash;
	}

	ioctl->Status = EXT_STATUS_OK;
	ioctl->ResponseLen = paccess_flash->DataLen;
	QL4PRINT(QLP4|QLP10,
	    printk("scsi%d): INT_ACCESS_FLASH buffer (1st 60h bytes only:\n",
	    ha->host_no));
	qla4xxx_dump_bytes(QLP4|QLP10, ha->ioctl_dma_bufv, 0x60);

exit_set_flash:
	/*
	 * Free Memory
	 */
	qla4xxx_free_ioctl_scrap_mem(ha);

	QL4PRINT(QLP4,
	    printk("scsi%d: %s: inst %d exiting.\n",
	    ha->host_no, __func__, ha->instance));
	LEAVE(__func__);

	return(status);
}

/**************************************************************************
 * qla4intioctl_set_driver_debug_level
 *	This routine sets the driver's debug print level.
 *
 * Input:
 *	ha    = adapter structure pointer.
 *	ioctl = IOCTL structure pointer.
 *
 * Output:
 *	None
 *
 * Returns:
 *	QLA_SUCCESS = success
 *	QLA_ERROR   = error
 *
 * Context:
 *	Kernel context.
 **************************************************************************/
static int
qla4intioctl_set_driver_debug_level(scsi_qla_host_t *ha, EXT_IOCTL_ISCSI *ioctl)
{
	int status = 0;
	uint32_t dbg_level;


	ENTER(__func__);
	QL4PRINT(QLP4,
	    printk("scsi%d: %s: inst %d entered.\n",
	    ha->host_no, __func__, ha->instance));

	if ((status = copy_from_user(&dbg_level,
	    Q64BIT_TO_PTR(ioctl->RequestAdr), sizeof(dbg_level))) != 0) {
		QL4PRINT(QLP2|QLP4,
		    printk("scsi%d: %s: failed to copy data\n",
		    ha->host_no, __func__));

		ioctl->Status = EXT_STATUS_COPY_ERR;
		goto exit_set_driver_debug_level;
	}

	if (qla4xxx_set_debug_level(dbg_level) == QLA_ERROR) {
		QL4PRINT(QLP2|QLP4,
		    printk("scsi%d: %s: unable to set debug level, "
		    "debug driver not loaded!\n",
		    ha->host_no, __func__));

		ioctl->Status = EXT_STATUS_ERR;
		goto exit_set_driver_debug_level;
	}

	QL4PRINT(QLP4,
	    printk("scsi%d: %s: debug level set to 0x%04X\n",
	    ha->host_no, __func__, dbg_level));

	ioctl->Status = EXT_STATUS_OK;

exit_set_driver_debug_level:
	QL4PRINT(QLP4,
	    printk("scsi%d: %s: inst %d exiting.\n",
	    ha->host_no, __func__, ha->instance));
	LEAVE(__func__);

	return(status);
}

/**************************************************************************
 * qla4intioctl_set_data
 *	This routine calls set data IOCTLs based on the IOCTL Sub Code.
 *
 * Input:
 *	ha    = adapter structure pointer.
 *	ioctl = IOCTL structure pointer.
 *
 * Output:
 *	None
 *
 * Returns:
 *	QLA_SUCCESS = success
 *	QLA_ERROR   = error
 *    	-EINVAL     = if the command is invalid
 *
 * Context:
 *	Kernel context.
 **************************************************************************/
int
qla4intioctl_set_data(scsi_qla_host_t *ha, EXT_IOCTL_ISCSI *ioctl)
{
	int	status = 0;

	switch (ioctl->SubCode) {
	case INT_SC_SET_FLASH:
		status = qla4intioctl_set_flash(ha, ioctl);
		break;
	case INT_SC_SET_DRIVER_DEBUG_LEVEL:
		status = qla4intioctl_set_driver_debug_level(ha, ioctl);
		break;
	default:
		QL4PRINT(QLP2,
			 printk("scsi%d: %s: unsupported internal set data "
				"sub-command code (%X)\n",
				ha->host_no, __func__, ioctl->SubCode));

		ioctl->Status = EXT_STATUS_UNSUPPORTED_SUBCODE;
		break;
	}

	return status;
}

/**************************************************************************
 * qla4intioctl_hba_reset
 *	This routine resets the specified HBA.
 *
 * Input:
 *	ha    = adapter structure pointer.
 *	ioctl = IOCTL structure pointer.
 *
 * Output:
 *	None
 *
 * Returns:
 *	QLA_SUCCESS = success
 *	QLA_ERROR   = error
 *
 * Context:
 *	Kernel context.
 **************************************************************************/
int
qla4intioctl_hba_reset(scsi_qla_host_t *ha, EXT_IOCTL_ISCSI *ioctl)
{
	uint8_t		status = 0;
	u_long		wait_count;


	ENTER(__func__);
	QL4PRINT(QLP4,
	    printk("scsi%d: %s: inst %d entered.\n",
	    ha->host_no, __func__, ha->instance));

	switch (ioctl->SubCode) {
	case INT_SC_HBA_RESET:
	case INT_SC_FIRMWARE_RESET:
		set_bit(DPC_RESET_HA_DESTROY_DDB_LIST, &ha->dpc_flags);

		/* Wait a fixed amount of time for reset to complete */
		wait_count = jiffies + ADAPTER_RESET_TOV * HZ;
		while (test_bit(DPC_RESET_HA_DESTROY_DDB_LIST,
		    &ha->dpc_flags) != 0) {
			if (wait_count <= jiffies)
				break;

			/* wait for 1 second */
			set_current_state(TASK_UNINTERRUPTIBLE);
			schedule_timeout(1*HZ);
		}

		if (test_bit(AF_ONLINE, &ha->flags)) {
			QL4PRINT(QLP4, printk("scsi%d: %s: Succeeded\n",
			    ha->host_no, __func__));
			ioctl->Status = EXT_STATUS_OK;
		} else {
			QL4PRINT(QLP2|QLP4, printk("scsi%d: %s: FAILED\n",
			    ha->host_no, __func__));
			ioctl->Status = EXT_STATUS_ERR;
		}

		break;

	case INT_SC_TARGET_WARM_RESET:
	case INT_SC_LUN_RESET:
	default:
		QL4PRINT(QLP2|QLP4, printk("scsi%d: %s: not supported.\n",
		    ha->host_no, __func__));
		ioctl->Status = EXT_STATUS_UNSUPPORTED_SUBCODE;
		break;
	}

	QL4PRINT(QLP4,
	    printk("scsi%d: %s: inst %d exiting.\n",
	    ha->host_no, __func__, ha->instance));
	LEAVE(__func__);

	return(status);
}

/**************************************************************************
 * qla4intioctl_copy_fw_flash
 *	This routine requests copying the FW image in FLASH from primary-to-
 *	secondary or secondary-to-primary.
 *
 * Input:
 *	ha    = adapter structure pointer.
 *	ioctl = IOCTL structure pointer.
 *
 * Output:
 *	None
 *
 * Returns:
 *	QLA_SUCCESS = success
 *	QLA_ERROR   = error
 *
 * Context:
 *	Kernel context.
 **************************************************************************/
int
qla4intioctl_copy_fw_flash(scsi_qla_host_t *ha, EXT_IOCTL_ISCSI *ioctl)
{
	int status = 0;
	INT_COPY_FW_FLASH copy_flash;
	uint32_t mbox_cmd[MBOX_REG_COUNT];
	uint32_t mbox_sts[MBOX_REG_COUNT];


	ENTER(__func__);
	QL4PRINT(QLP4,
	    printk("scsi%d: %s: inst %d entered.\n",
	    ha->host_no, __func__, ha->instance));

	if ((status = copy_from_user((uint8_t *)&copy_flash,
	    Q64BIT_TO_PTR(ioctl->RequestAdr), ioctl->RequestLen)) != 0) {
		QL4PRINT(QLP2|QLP4,
		    printk("scsi%d: %s: unable to copy data from user's "
		    "memory area\n", ha->host_no, __func__));

		ioctl->Status = EXT_STATUS_COPY_ERR;
		goto exit_copy_flash;
	}

	memset(&mbox_cmd, 0, sizeof(mbox_cmd));
	memset(&mbox_sts, 0, sizeof(mbox_sts));
	mbox_cmd[0] = MBOX_CMD_COPY_FLASH;
	mbox_cmd[1] = copy_flash.Options;

	if (qla4xxx_mailbox_command(ha, 2, 2, &mbox_cmd[0], &mbox_sts[0]) ==
	    QLA_SUCCESS) {
		QL4PRINT(QLP4|QLP10,
		    printk("scsi%d: %s: Succeeded\n",
		    ha->host_no, __func__));

		ioctl->Status = EXT_STATUS_OK;
	} else {
		QL4PRINT(QLP4|QLP10,
		    printk("scsi%d: %s: FAILED\n",
		    ha->host_no, __func__));

		ioctl->Status = EXT_STATUS_ERR;
		ioctl->DetailStatus = mbox_sts[0];
		ioctl->VendorSpecificStatus[0] = mbox_sts[1];
	}

exit_copy_flash:
	QL4PRINT(QLP4,
	    printk("scsi%d: %s: inst %d exiting.\n",
	    ha->host_no, __func__, ha->instance));
	LEAVE(__func__);

	return(status);
}

/**************************************************************************
 * qla4xxx_iocb_pass_done
 *	This routine resets the ioctl progress flag and wakes up the ioctl
 * 	completion semaphore.
 *
 * Input:
 *	ha    = adapter structure pointer.
 *   	sts_entry - pointer to passthru status buffer.
 *
 * Output:
 *	None
 *
 * Returns:
 *	None
 *
 * Context:
 *	Interrupt context.
 **************************************************************************/
void
qla4xxx_iocb_pass_done(scsi_qla_host_t *ha, PASSTHRU_STATUS_ENTRY *sts_entry)
{
	INT_IOCB_PASSTHRU *iocb;


	ENTER(__func__);
	QL4PRINT(QLP4,
	    printk("scsi%d: %s: inst %d entered.\n",
	    ha->host_no, __func__, ha->instance));

	/* --- Copy passthru status buffer to iocb passthru buffer ---*/
	iocb = (INT_IOCB_PASSTHRU *)(ulong)le32_to_cpu(sts_entry->handle);
	memcpy(iocb->IOCBStatusBuffer, sts_entry,
	    MIN(sizeof(iocb->IOCBStatusBuffer), sizeof(*sts_entry)));

	/* --- Reset IOCTL flags and wakeup semaphore.
	 *     But first check to see if IOCTL has already
	 *     timed out because we don't want to get the
	 *     up/down semaphore counters off.             --- */
	if (ha->ioctl->ioctl_iocb_pass_in_progress == 1) {
		ha->ioctl->ioctl_iocb_pass_in_progress = 0;
		ha->ioctl->ioctl_tov = 0;

		QL4PRINT(QLP4|QLP10,
		    printk("%s: UP count=%d\n", __func__,
		    atomic_read(&ha->ioctl->ioctl_cmpl_sem.count)));
		up(&ha->ioctl->ioctl_cmpl_sem);
	}

	QL4PRINT(QLP4,
	    printk("scsi%d: %s: inst %d exiting.\n",
	    ha->host_no, __func__, ha->instance));
	LEAVE(__func__);

	return;
}

/**************************************************************************
 * qla4intioctl_iocb_passthru
 *	This routine
 *	
 *
 * Input:
 *	ha    = adapter structure pointer.
 *	ioctl = IOCTL structure pointer.
 *
 * Output:
 *	None
 *
 * Returns:
 *	QLA_SUCCESS = success
 *	QLA_ERROR   = error
 *
 * Context:
 *	Kernel context.
 **************************************************************************/
int
qla4intioctl_iocb_passthru(scsi_qla_host_t *ha, EXT_IOCTL_ISCSI *ioctl)
{
	int			status = 0;
	INT_IOCB_PASSTHRU	*iocb;
	INT_IOCB_PASSTHRU	*iocb_dma;
	PASSTHRU0_ENTRY		*passthru_entry;
	unsigned long		flags;
	DATA_SEG_A64		*data_seg;


	ENTER("qla4intioctl_iocb_passthru");
	QL4PRINT(QLP3, printk("scsi%d: %s:\n", ha->host_no, __func__));

	/* --- Use internal DMA buffer for iocb structure --- */

	if (ha->ioctl_dma_buf_len < sizeof(*iocb))
		qla4xxx_resize_ioctl_dma_buf(ha, sizeof(*iocb));

	if (!ha->ioctl_dma_bufv || ha->ioctl_dma_buf_len < sizeof(*iocb)) {
		QL4PRINT(QLP2|QLP4,
		    printk("scsi%d: %s: dma buffer inaccessible.\n",
		    ha->host_no, __func__));
		ioctl->Status = EXT_STATUS_BUFFER_TOO_SMALL;
		goto exit_iocb_passthru;
	}

	iocb = (INT_IOCB_PASSTHRU *) ha->ioctl_dma_bufv;
	iocb_dma = (INT_IOCB_PASSTHRU *)(unsigned long)ha->ioctl_dma_bufp;

	/* --- Copy IOCB_PASSTHRU structure from user space --- */
	if ((status = copy_from_user((uint8_t *)iocb,
	    Q64BIT_TO_PTR(ioctl->RequestAdr), ioctl->RequestLen)) != 0) {
		QL4PRINT(QLP2|QLP4,
		    printk("scsi%d: %s: unable to copy data from user's "
		    "memory area\n", ha->host_no, __func__));

		ioctl->Status = EXT_STATUS_COPY_ERR;
		goto exit_iocb_passthru;
	}


	/* --- Get pointer to the passthru queue entry --- */
	spin_lock_irqsave(&ha->hardware_lock, flags);
	if (qla4xxx_get_req_pkt(ha, (QUEUE_ENTRY **) &passthru_entry) !=
	    QLA_SUCCESS) {
		QL4PRINT(QLP2|QLP4,
		    printk("scsi%d: %s: request queue full, try again later\n",
		    ha->host_no, __func__));

		ioctl->Status = EXT_STATUS_HBA_QUEUE_FULL;
		spin_unlock_irqrestore(&ha->hardware_lock, flags);
		goto exit_iocb_passthru;
	}

	/* --- Fill in passthru queue entry --- */
	if (iocb->SendDMAOffset) {
		data_seg = (DATA_SEG_A64 *)(iocb->IOCBCmdBuffer +
		    iocb->SendDMAOffset);
		data_seg->base.addrHigh =
		    cpu_to_le32(MSDW((ulong)&iocb_dma->SendData[0]));
		data_seg->base.addrLow =
		    cpu_to_le32(LSDW((ulong)&iocb_dma->SendData[0]));
	}

	if (iocb->RspDMAOffset) {
		data_seg =
		    (DATA_SEG_A64 *)(iocb->IOCBCmdBuffer + iocb->RspDMAOffset);
		data_seg->base.addrHigh =
		    cpu_to_le32(MSDW((ulong)&iocb_dma->RspData[0]));
		data_seg->base.addrLow =
		    cpu_to_le32(LSDW((ulong)&iocb_dma->RspData[0]));
	}

	memcpy(passthru_entry, iocb->IOCBCmdBuffer,
	    MIN(sizeof(*passthru_entry), sizeof(iocb->IOCBCmdBuffer)));
	passthru_entry->handle = (uint32_t) (unsigned long) iocb;
	passthru_entry->hdr.systemDefined = SD_PASSTHRU_IOCB;

	if (passthru_entry->hdr.entryType != ET_PASSTHRU0)
		passthru_entry->timeout = MBOX_TOV;

	QL4PRINT(QLP4|QLP10,
	    printk(KERN_INFO
	    "scsi%d: Passthru0 IOCB type %x count %x In (%x) %p\n",
	    ha->host_no, passthru_entry->hdr.entryType,
	    passthru_entry->hdr.entryCount, ha->request_in, passthru_entry));

	QL4PRINT(QLP4|QLP10,
	    printk(KERN_INFO "scsi%d: Dump Passthru entry %p: \n",
	    ha->host_no, passthru_entry));
	qla4xxx_dump_bytes(QLP4|QLP10, passthru_entry, sizeof(*passthru_entry));

	/* ---- Prepare for receiving completion ---- */
	ha->ioctl->ioctl_iocb_pass_in_progress = 1;
	ha->ioctl->ioctl_tov = passthru_entry->timeout * HZ;
	qla4xxx_ioctl_sem_init(ha);

	/* ---- Send command to adapter ---- */
	ha->ioctl->ioctl_cmpl_timer.expires = jiffies + ha->ioctl->ioctl_tov;
	add_timer(&ha->ioctl->ioctl_cmpl_timer);

	WRT_REG_DWORD(&ha->reg->req_q_in, ha->request_in);
	PCI_POSTING(&ha->reg->req_q_in);
	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	QL4PRINT(QLP4|QLP10, printk("%s: DOWN count=%d\n",
	    __func__, atomic_read(&ha->ioctl->ioctl_cmpl_sem.count)));

	down(&ha->ioctl->ioctl_cmpl_sem);

	/*******************************************************
	 *						       *
	 *             Passthru Completion                     *
	 *						       *
	 *******************************************************/
	del_timer(&ha->ioctl->ioctl_cmpl_timer);

	/* ---- Check for timeout --- */
	if (ha->ioctl->ioctl_iocb_pass_in_progress == 1) {
		QL4PRINT(QLP2|QLP4,
		    printk("scsi%d: %s: ERROR = command timeout.\n",
		    ha->host_no, __func__));

		ha->ioctl->ioctl_iocb_pass_in_progress = 0;
		ioctl->Status = EXT_STATUS_ERR;
		goto exit_iocb_passthru;
	}

	/* ---- Copy IOCB Passthru structure with updated status buffer
	 *      to user space ---- */
	if ((status = copy_to_user(Q64BIT_TO_PTR(ioctl->ResponseAdr),
	    iocb, sizeof(INT_IOCB_PASSTHRU))) != 0) {
		QL4PRINT(QLP2|QLP4,
		    printk("scsi%d: %s: unable to copy passthru struct "
		    "to user's memory area.\n",
		    ha->host_no, __func__));

		ioctl->Status = EXT_STATUS_COPY_ERR;
		goto exit_iocb_passthru;
	}

	QL4PRINT(QLP4|QLP10, printk("Dump iocb structure (OUT)\n"));
	qla4xxx_print_iocb_passthru(QLP4|QLP10, ha, iocb);

	QL4PRINT(QLP4, printk("scsi%d: %s: Succeeded\n",
	    ha->host_no, __func__));

	ioctl->Status = EXT_STATUS_OK;

exit_iocb_passthru:
	QL4PRINT(QLP4,
	    printk("scsi%d: %s: inst %d exiting.\n",
	    ha->host_no, __func__, ha->instance));
	LEAVE(__func__);

	return(status);
}

/**************************************************************************
 * qla4xxx_resize_ioctl_dma_buf
 *	This routine deallocates the dma_buf of the previous size and re-
 *	allocates the dma_buf with the given size.
 *
 * Input:
 *	ha      - Pointer to host adapter structure
 *	size    - Size of dma buffer to allocate
 *
 * Output:
 *	dma_buf - virt_addr, phys_addr, and buf_len values filled in
 *
 * Returns:
 *	QLA_SUCCESS - Successfully re-allocates memory
 *	QLA_ERROR   - Failed to re-allocate memory
 *
 * Context:
 *	Kernel context.
 **************************************************************************/
uint8_t
qla4xxx_resize_ioctl_dma_buf(scsi_qla_host_t *ha, uint32_t size)
{
	uint8_t status = 0;

	ENTER(__func__);
	QL4PRINT(QLP4,
	    printk("scsi%d: %s: inst %d entered.\n",
	    ha->host_no, __func__, ha->instance));

	if (ha->ioctl_dma_buf_len) {
		QL4PRINT(QLP3|QLP4,
		    printk("scsi%d: %s: deallocate old dma_buf, size=0x%x\n",
		    ha->host_no, __func__, ha->ioctl_dma_buf_len));
		pci_free_consistent(ha->pdev, ha->ioctl_dma_buf_len,
		    ha->ioctl_dma_bufv, ha->ioctl_dma_bufp);
		ha->ioctl_dma_buf_len = 0;
		ha->ioctl_dma_bufv = 0;
		ha->ioctl_dma_bufp = 0;
	}

	QL4PRINT(QLP3|QLP4,
	    printk("scsi%d: %s: allocate new ioctl_dma_buf, size=0x%x\n",
	    ha->host_no, __func__, size));
	
	ha->ioctl_dma_bufv = pci_alloc_consistent(ha->pdev, size,
	    &ha->ioctl_dma_bufp);
	if (ha->ioctl_dma_bufv == NULL) {
		QL4PRINT(QLP2|QLP4,
		    printk("scsi%d: %s: ERROR allocating new ioctl_dma_buf, "
		    "size=0x%x\n", ha->host_no, __func__, size));
	} else {
		ha->ioctl_dma_buf_len = size;
	}

	QL4PRINT(QLP4,
	    printk("scsi%d: %s: inst %d exiting.\n",
	    ha->host_no, __func__, ha->instance));
	LEAVE(__func__);

	return(status);
}



/*
 * Overrides for Emacs so that we almost follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-indent-level: 2
 * c-brace-imaginary-offset: 0
 * c-brace-offset: -2
 * c-argdecl-indent: 2
 * c-label-offset: -2
 * c-continued-statement-offset: 2
 * c-continued-brace-offset: 0
 * indent-tabs-mode: nil
 * tab-width: 8
 * End:
 */


