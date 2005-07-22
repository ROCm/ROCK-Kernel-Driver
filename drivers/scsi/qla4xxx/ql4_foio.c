/********************************************************************************
*                  QLOGIC LINUX SOFTWARE
*
* QLogic ISP4xxx device driver for Linux 2.6.x
* Copyright (C) 2003-2004 QLogic Corporation
* (www.qlogic.com)
*
* This program is free software; you can redistribute it and/or modify it
* under the terms of the GNU General Public License as published by the
* Free Software Foundation; either version 2, or (at your option) any
* later version.
*
* This program is distributed in the hope that it will be useful, but
* WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
* General Public License for more details.
*
******************************************************************************/

/****************************************
 *	Issues requests for failover module
 ****************************************/
 
// #include "qla_os.h"
#include "ql4_def.h"

// #include "qlfo.h"
/*
#include "qlfolimits.h"
#include "ql4_foln.h"
*/

/*
 *  Function Prototypes.
 */

int qla4xxx_issue_scsi_inquiry(scsi_qla_host_t *ha, 
	fc_port_t *fcport, fc_lun_t *fclun );
int qla4xxx_test_active_lun(fc_port_t *fcport, fc_lun_t *fclun);
int qla4xxx_get_wwuln_from_device(mp_host_t *host, fc_lun_t *fclun, 
	char	*evpd_buf, int wwlun_size);
fc_lun_t * qla4xxx_cfg_lun(scsi_qla_host_t *ha, fc_port_t *fcport, uint16_t lun,
                inq_cmd_rsp_t *inq, dma_addr_t inq_dma);
void
qla4xxx_lun_discovery(scsi_qla_host_t *ha, fc_port_t *fcport);
static int
qla4xxx_inquiry(scsi_qla_host_t *ha,
    fc_port_t *fcport, uint16_t lun, inq_cmd_rsp_t *inq, dma_addr_t inq_dma);
int qla4xxx_rpt_lun_discovery(scsi_qla_host_t *ha, fc_port_t *fcport,
    inq_cmd_rsp_t *inq, dma_addr_t inq_dma);
static int qla4xxx_report_lun(scsi_qla_host_t *ha, fc_port_t *fcport,
		   rpt_lun_cmd_rsp_t *rlc, dma_addr_t rlc_dma);
	
int
qla4xxx_spinup(scsi_qla_host_t *ha, fc_port_t *fcport, uint16_t lun); 
				
/*
 * qla4xxx_get_wwuln_from_device
 *	Issue SCSI inquiry page code 0x83 command for LUN WWLUN_NAME.
 *
 * Input:
 *	ha = adapter block pointer.
 *	fcport = FC port structure pointer.
 *
 * Return:
 *	0  - Failed to get the lun_wwlun_name
 *      Otherwise : wwlun_size
 *
 * Context:
 *	Kernel context.
 */

int
qla4xxx_get_wwuln_from_device(mp_host_t *host, fc_lun_t *fclun, 
	char	*evpd_buf, int wwlun_size)
{

	evpd_inq_cmd_rsp_t	*pkt;
	int		rval, rval1; 
	dma_addr_t	phys_address = 0;
	int		retries;
	uint8_t	comp_status;
	uint8_t	scsi_status;
	uint8_t	iscsi_flags;
	scsi_qla_host_t *ha;
	ddb_entry_t	*ddb_entry = fclun->fcport->ddbptr;

	ENTER(__func__);
	//printk("%s entered\n",__func__);

	rval = 0; /* failure */

	if (atomic_read(&fclun->fcport->state) == FCS_DEVICE_DEAD){
		DEBUG(printk("%s leaving: Port is marked DEAD\n",__func__);)
		return rval;
	}

	memset(evpd_buf, 0 ,wwlun_size);
	ha = host->ha;
	pkt = pci_alloc_consistent(ha->pdev,
	    sizeof(evpd_inq_cmd_rsp_t), &phys_address);

	if (pkt == NULL) {
		printk(KERN_WARNING
		    "scsi(%d): Memory Allocation failed - INQ\n",
		    ha->host_no);
		ha->mem_err++;
		return rval;
	}

	for (retries = 3; retries; retries--) {
		memset(pkt, 0, sizeof(evpd_inq_cmd_rsp_t));
		pkt->p.cmd.hdr.entryType = ET_COMMAND;
		pkt->p.cmd.hdr.entryCount = 1;
		
		pkt->p.cmd.lun[1]  = LSB(cpu_to_le16(fclun->lun)); /*SAMII compliant lun*/
		pkt->p.cmd.lun[2]  = MSB(cpu_to_le16(fclun->lun));
		pkt->p.cmd.target = cpu_to_le16(ddb_entry->fw_ddb_index);
		pkt->p.cmd.control_flags =(CF_READ | CF_SIMPLE_TAG);
		pkt->p.cmd.cdb[0] = INQUIRY;
		pkt->p.cmd.cdb[1] = INQ_EVPD_SET;
		pkt->p.cmd.cdb[2] = INQ_DEV_IDEN_PAGE; 
		pkt->p.cmd.cdb[4] = VITAL_PRODUCT_DATA_SIZE;
		pkt->p.cmd.dataSegCnt = __constant_cpu_to_le16(1);
		pkt->p.cmd.timeout = __constant_cpu_to_le16(10);
		pkt->p.cmd.ttlByteCnt =
		    __constant_cpu_to_le32(VITAL_PRODUCT_DATA_SIZE);
		pkt->p.cmd.dataseg[0].base.addrLow = cpu_to_le32(
		    LSDW(phys_address + sizeof(STATUS_ENTRY)));
		pkt->p.cmd.dataseg[0].base.addrHigh = cpu_to_le32(
		    MSDW(phys_address + sizeof(STATUS_ENTRY)));
		pkt->p.cmd.dataseg[0].count =
		    __constant_cpu_to_le32(VITAL_PRODUCT_DATA_SIZE);
		/* If in connection mode, bump sequence number */
		if ((ha->firmware_options & FWOPT_SESSION_MODE) != 0) {
		 	ddb_entry->CmdSn++;
		}
		pkt->p.cmd.cmdSeqNum = cpu_to_le32(ddb_entry->CmdSn);	

		rval1 = qla4xxx_issue_iocb(ha, pkt,
			    phys_address, sizeof(evpd_inq_cmd_rsp_t));

		comp_status = pkt->p.rsp.completionStatus;
		scsi_status = pkt->p.rsp.scsiStatus;
		iscsi_flags = pkt->p.rsp.iscsiFlags;

		DEBUG2(printk("%s: lun (%d) inquiry page 0x83- "
		    " comp status 0x%x, "
		    "scsi status 0x%x, iscsi flags=0x%x, rval=%d\n"
		    ,__func__,
		    fclun->lun, comp_status, scsi_status, iscsi_flags,
		    rval1);)
		DEBUG2(printk("pkt resp len %d, bidi len %d \n",
			pkt->p.rsp.residualByteCnt,
			pkt->p.rsp.bidiResidualByteCnt);)


		if (rval1 != QLA_SUCCESS || comp_status != SCS_COMPLETE ||
		    scsi_status & SCSISTAT_CHECK_CONDITION) {

			if (scsi_status & SCSISTAT_CHECK_CONDITION) {
				DEBUG2(printk("scsi(%d): INQ "
				    "SCSISTAT_CHECK_CONDITION Sense Data "
				    "%02x %02x %02x %02x %02x %02x %02x %02x\n",
				    ha->host_no,
				    pkt->p.rsp.senseData[0],
				    pkt->p.rsp.senseData[1],
				    pkt->p.rsp.senseData[2],
				    pkt->p.rsp.senseData[3],
				    pkt->p.rsp.senseData[4],
				    pkt->p.rsp.senseData[5],
				    pkt->p.rsp.senseData[6],
				    pkt->p.rsp.senseData[7]));
			}

			/* Device underrun, treat as OK. */
			if (rval1 == QLA_SUCCESS &&
			    comp_status == SCS_DATA_UNDERRUN &&
			    iscsi_flags & ISCSI_FLAG_RESIDUAL_UNDER) {

				/* rval1 = QLA_SUCCESS; */
				break;
			}
		} else {
			rval1 = QLA_SUCCESS;
			break;
		}
	} 

	if (rval1 == QLA_SUCCESS &&
	    pkt->inq[1] == INQ_DEV_IDEN_PAGE ) {

		if( pkt->inq[7] <= WWLUN_SIZE ){
			memcpy(evpd_buf,&pkt->inq[8], pkt->inq[7]);
			rval = pkt->inq[7] ; /* lun wwlun_size */
			DEBUG2(printk("%s : Lun(%d)  WWLUN size %d\n",__func__,
			    fclun->lun,pkt->inq[7]);)
		} else {
			memcpy(evpd_buf,&pkt->inq[8], WWLUN_SIZE);
			rval = WWLUN_SIZE;
			printk(KERN_INFO "%s : Lun(%d)  WWLUN may "
			    "not be complete, Buffer too small" 
			    " need: %d provided: %d\n",__func__,
			    fclun->lun,pkt->inq[7],WWLUN_SIZE);
		}
		DEBUG2(qla4xxx_dump_buffer(evpd_buf, rval);)
	} else {
		if (scsi_status & SCSISTAT_CHECK_CONDITION) {
			/*
			 * ILLEGAL REQUEST - 0x05
			 * INVALID FIELD IN CDB - 24 : 00
			 */
			if(pkt->p.rsp.senseData[2] == 0x05 && 
			    pkt->p.rsp.senseData[12] == 0x24 &&
			    pkt->p.rsp.senseData[13] == 0x00 ) {

				DEBUG2(printk(KERN_INFO "%s Lun(%d) does not"
				    " support Inquiry Page Code-0x83\n",					
				    __func__,fclun->lun);)
			} else {
				DEBUG2(printk(KERN_INFO "%s Lun(%d) does not"
				    " support Inquiry Page Code-0x83\n",	
				    __func__,fclun->lun);)
				DEBUG2(printk( KERN_INFO "Unhandled check " 
				    "condition sense_data[2]=0x%x"  		
				    " sense_data[12]=0x%x "
				    "sense_data[13]=0x%x\n",
				    pkt->p.rsp.senseData[2],
				    pkt->p.rsp.senseData[12],
				    pkt->p.rsp.senseData[13]);)
			
			}

		} else {
			/* Unable to issue Inquiry Page 0x83 */
			DEBUG2(printk(KERN_INFO
			    "%s Failed to issue Inquiry Page 0x83 -- lun (%d) "
			    "cs=0x%x ss=0x%x, rval=%d\n",
			    __func__, fclun->lun, comp_status, scsi_status,
			    rval);)
		}
		rval = 0 ;
	}

	pci_free_consistent(ha->pdev, sizeof(evpd_inq_cmd_rsp_t), 
	    			pkt, phys_address);

	//printk("%s exit\n",__func__);
	LEAVE(__func__);

	return rval;
}

/*
 * qla4xxx_inquiry
 *	Issue SCSI inquiry command.
 *
 * Input:
 *	ha = adapter block pointer.
 *	fcport = FC port structure pointer.
 *
 * Return:
 *	0  - Success
 *  BIT_0 - error
 *
 * Context:
 *	Kernel context.
 */
static int
qla4xxx_inquiry(scsi_qla_host_t *ha, fc_port_t *fcport,
		uint16_t lun, inq_cmd_rsp_t *inq, dma_addr_t inq_dma)
{
	int rval, rval1;
	uint16_t retries;
	uint8_t comp_status;
	uint8_t scsi_status;
	uint8_t iscsi_flags;
	ddb_entry_t *ddb_entry = fcport->ddbptr;

	rval = QLA_ERROR;

	for (retries = 3; retries; retries--) {
		memset(inq, 0, sizeof(inq_cmd_rsp_t));
		inq->p.cmd.hdr.entryType = ET_COMMAND;
		
		/* rlc->p.cmd.handle = 1; */
		/* 8 byte lun number */
		inq->p.cmd.lun[1]            = LSB(cpu_to_le16(lun)); /*SAMII compliant lun*/
		inq->p.cmd.lun[2]            = MSB(cpu_to_le16(lun));
		inq->p.cmd.hdr.entryCount = 1;
		inq->p.cmd.target = cpu_to_le16(ddb_entry->fw_ddb_index);
		inq->p.cmd.control_flags =(CF_READ | CF_SIMPLE_TAG);
		inq->p.cmd.cdb[0] = INQUIRY;
		inq->p.cmd.cdb[4] = INQ_DATA_SIZE;
		inq->p.cmd.dataSegCnt = __constant_cpu_to_le16(1);
		inq->p.cmd.timeout = __constant_cpu_to_le16(10);
		inq->p.cmd.ttlByteCnt =
		    __constant_cpu_to_le32(INQ_DATA_SIZE);
		inq->p.cmd.dataseg[0].base.addrLow = cpu_to_le32(
		    LSDW(inq_dma + sizeof(STATUS_ENTRY)));
		inq->p.cmd.dataseg[0].base.addrHigh = cpu_to_le32(
		    MSDW(inq_dma + sizeof(STATUS_ENTRY)));
		inq->p.cmd.dataseg[0].count =
		    __constant_cpu_to_le32(INQ_DATA_SIZE);
		/*  rlc->p.cmd.lun[8];	 always lun 0 */
		/* If in connection mode, bump sequence number */
		if ((ha->firmware_options & FWOPT_SESSION_MODE) != 0) {
		 	ddb_entry->CmdSn++;
		}
		inq->p.cmd.cmdSeqNum = cpu_to_le32(ddb_entry->CmdSn);	
		
		DEBUG2(printk("scsi(%d): Lun Inquiry - fcport=[%04x/%p],"
		    " lun (%d)\n",
		    ha->host_no, fcport->loop_id, fcport, lun));

		rval1 = qla4xxx_issue_iocb(ha, inq, inq_dma,
		    sizeof(inq_cmd_rsp_t));

		comp_status = inq->p.rsp.completionStatus;
		scsi_status = inq->p.rsp.scsiStatus;
		iscsi_flags = inq->p.rsp.iscsiFlags;

		DEBUG2(printk("scsi(%d): lun (%d) inquiry - "
		    "inq[0]= 0x%x, comp status 0x%x, scsi status 0x%x, "
		    "rval=%d\n",
		    ha->host_no, lun, inq->inq[0], comp_status, scsi_status,
		    rval1));

		if (rval1 != QLA_SUCCESS || comp_status != SCS_COMPLETE ||
		    scsi_status & SCSISTAT_CHECK_CONDITION) {

			DEBUG2(printk("scsi(%d): INQ failed to issue iocb! "
			    "fcport=[%04x/%p] rval=%x cs=%x ss=%x\n",
			    ha->host_no, fcport->loop_id, fcport, rval1,
			    comp_status, scsi_status));


			if (scsi_status & SCSISTAT_CHECK_CONDITION) {
				DEBUG2(printk("scsi(%d): INQ "
				    "SCSISTAT_CHECK_CONDITION Sense Data "
				    "%02x %02x %02x %02x %02x %02x %02x %02x\n",
				    ha->host_no,
				    inq->p.rsp.senseData[0],
				    inq->p.rsp.senseData[1],
				    inq->p.rsp.senseData[2],
				    inq->p.rsp.senseData[3],
				    inq->p.rsp.senseData[4],
				    inq->p.rsp.senseData[5],
				    inq->p.rsp.senseData[6],
				    inq->p.rsp.senseData[7]));
			}

			/* Device underrun, treat as OK. */
			if (rval1 == QLA_SUCCESS &&
			    comp_status == SCS_DATA_UNDERRUN &&
			    iscsi_flags & ISCSI_FLAG_RESIDUAL_UNDER) {

				rval = QLA_SUCCESS;
				break;
			}
		} else {
			rval = QLA_SUCCESS;
			break;
		}
	}

	return (rval);
}

int
qla4xxx_issue_scsi_inquiry(scsi_qla_host_t *ha, 
	fc_port_t *fcport, fc_lun_t *fclun )
{
	inq_cmd_rsp_t	*pkt;
	dma_addr_t	phys_address = 0;
	int		ret = 0;
	
	pkt = pci_alloc_consistent(ha->pdev,
	    sizeof(inq_cmd_rsp_t), &phys_address);

	if (pkt == NULL) {
		printk(KERN_WARNING
		    "scsi(%d): Memory Allocation failed - INQ\n", ha->host_no);
		ha->mem_err++;
		return BIT_0;
	}

	if ( qla4xxx_inquiry(ha, fcport,
		fclun->lun, pkt, phys_address) != QLA_SUCCESS) {
			 
		DEBUG2(printk("%s: Failed lun inquiry - "
			"inq[0]= 0x%x, "
			"\n",
			__func__,pkt->inq[0]);)
		ret = 1;
	} else {
		fclun->device_type = pkt->inq[0];
	}

	pci_free_consistent(ha->pdev, sizeof(inq_cmd_rsp_t), pkt, phys_address);

	return (ret);
}

int
qla4xxx_test_active_lun(fc_port_t *fcport, fc_lun_t *fclun) 
{
	tur_cmd_rsp_t	*pkt;
	int		rval = 0 ; 
	dma_addr_t	phys_address = 0;
	int		retry;
	uint8_t	comp_status;
	uint8_t	scsi_status;
	uint8_t iscsi_flags;
	ddb_entry_t	*ddb_entry = fcport->ddbptr;
	scsi_qla_host_t *ha;
	uint16_t	lun = 0;

	ENTER(__func__);


	ha = fcport->ha;
	if (atomic_read(&fcport->state) == FCS_DEVICE_DEAD){
		DEBUG2(printk("scsi(%d) %s leaving: Port loop_id 0x%02x is marked DEAD\n",
			ha->host_no,__func__,fcport->loop_id);)
		return rval;
	}
	
	if ( fclun == NULL ){
		DEBUG2(printk("scsi(%d) %s Bad fclun ptr on entry.\n",
			ha->host_no,__func__);)
		return rval;
	}
	
	lun = fclun->lun;

	pkt = pci_alloc_consistent(ha->pdev,
	    sizeof(tur_cmd_rsp_t), &phys_address);

	if (pkt == NULL) {
		printk(KERN_WARNING
		    "scsi(%d): Memory Allocation failed - TUR\n",
		    ha->host_no);
		ha->mem_err++;
		return rval;
	}

	retry = 4;
	do {
		memset(pkt, 0, sizeof(tur_cmd_rsp_t));
		pkt->p.cmd.hdr.entryType = ET_COMMAND;
		/* 8 byte lun number */
		pkt->p.cmd.lun[1]            = LSB(cpu_to_le16(lun)); /*SAMII compliant lun*/
		pkt->p.cmd.lun[2]            = MSB(cpu_to_le16(lun));
		
		/* rlc->p.cmd.handle = 1; */
		pkt->p.cmd.hdr.entryCount = 1;
		pkt->p.cmd.target = cpu_to_le16(ddb_entry->fw_ddb_index);
		pkt->p.cmd.control_flags = (CF_NO_DATA | CF_SIMPLE_TAG);
		pkt->p.cmd.cdb[0] = TEST_UNIT_READY;
		pkt->p.cmd.dataSegCnt = __constant_cpu_to_le16(0);
		pkt->p.cmd.timeout = __constant_cpu_to_le16(10);
		pkt->p.cmd.ttlByteCnt = __constant_cpu_to_le32(0);
		/* If in connection mode, bump sequence number */
		if ((ha->firmware_options & FWOPT_SESSION_MODE) != 0)
		 	ddb_entry->CmdSn++;
		pkt->p.cmd.cmdSeqNum = cpu_to_le32(ddb_entry->CmdSn);	

		rval = qla4xxx_issue_iocb(ha, pkt, phys_address,
		    sizeof(tur_cmd_rsp_t));
			
		comp_status = pkt->p.rsp.completionStatus;
		scsi_status = pkt->p.rsp.scsiStatus;
		iscsi_flags = pkt->p.rsp.iscsiFlags;

#if 0

		if (rval != QLA_SUCCESS || comp_status != SCS_COMPLETE ||
		    (scsi_status & SCSISTAT_CHECK_CONDITION) ) {
			/* Device underrun, treat as OK. */
			if (rval == QLA_SUCCESS &&
			    comp_status == SCS_DATA_UNDERRUN &&
			    iscsi_flags & ISCSI_FLAG_RESIDUAL_UNDER) {
				rval = QLA_SUCCESS;
				break;
			}
		}
#endif
		
		/* Port Logged Out, so don't retry */
		if (comp_status ==  SCS_DEVICE_LOGGED_OUT ||
		    comp_status ==  SCS_INCOMPLETE ||
		    comp_status ==  SCS_DEVICE_UNAVAILABLE ||
		    comp_status ==  SCS_DEVICE_CONFIG_CHANGED )
			break;

		DEBUG(printk("scsi(%ld:%04x:%d) %s: TEST UNIT READY - comp "
		    "status 0x%x, scsi status 0x%x, rval=%d\n", ha->host_no,
		    fcport->loop_id, lun,__func__, comp_status, scsi_status,
		    rval));

		if ((scsi_status & SCSISTAT_CHECK_CONDITION)) {
			DEBUG2(printk("%s: check status bytes = "
			    "0x%02x 0x%02x 0x%02x\n", __func__,
			    pkt->p.rsp.senseData[2],
			    pkt->p.rsp.senseData[12],
			    pkt->p.rsp.senseData[13]));

			if (pkt->p.rsp.senseData[2] == NOT_READY && 
			    pkt->p.rsp.senseData[12] == 0x4 &&
			    pkt->p.rsp.senseData[13] == 0x2) 
				break;
		}
	} while ((rval != QLA_SUCCESS || comp_status != SCS_COMPLETE ||
	    (scsi_status & SCSISTAT_CHECK_CONDITION)) && retry--);

	if (rval == QLA_SUCCESS &&
	    (!((scsi_status & SCSISTAT_CHECK_CONDITION) &&
		(pkt->p.rsp.senseData[2] == NOT_READY &&
		    pkt->p.rsp.senseData[12] == 0x4 &&
		    pkt->p.rsp.senseData[13] == 0x2)) &&
	     comp_status == SCS_COMPLETE)) {
		
		DEBUG2(printk("scsi(%d) %s - Lun (0x%02x:%d) set to ACTIVE.\n",
		    ha->host_no, __func__, fcport->loop_id, lun));

		/* We found an active path */
		fclun->flags |= FLF_ACTIVE_LUN;
		rval = 1;
	} else {
		DEBUG2(printk("scsi(%d) %s - Lun (0x%02x:%d) set to "
		    "INACTIVE.\n", ha->host_no, __func__,
		    fcport->loop_id, lun));
		/* fcport->flags &= ~(FCF_MSA_PORT_ACTIVE); */
		fclun->flags &= ~(FLF_ACTIVE_LUN);
	}

	pci_free_consistent(ha->pdev, sizeof(tur_cmd_rsp_t), pkt, phys_address);

	LEAVE(__func__);

	return rval;
}

#if MSA1000_SUPPORTED
static fc_lun_t *
qla4xxx_find_data_lun(fc_port_t *fcport) 
{
	scsi_qla_host_t *ha;
	fc_lun_t	*fclun, *ret_fclun;

	ha = fcport->ha;
	ret_fclun = NULL;

	/* Go thur all luns and find a good data lun */
	list_for_each_entry(fclun, &fcport->fcluns, list) {
		fclun->flags &= ~FLF_VISIBLE_LUN;
		if (fclun->device_type == 0xff)
			qla4xxx_issue_scsi_inquiry(ha, fcport, fclun);
		if (fclun->device_type == 0xc)
			fclun->flags |= FLF_VISIBLE_LUN;
		else if (fclun->device_type == TYPE_DISK) {
			ret_fclun = fclun;
		}
	}
	return (ret_fclun);
}

/*
 * qla4xxx_test_active_port
 *	Determines if the port is in active or standby mode. First, we
 *	need to locate a storage lun then do a TUR on it. 
 *
 * Input:
 *	fcport = port structure pointer.
 *	
 *
 * Return:
 *	0  - Standby or error
 *  1 - Active
 *
 * Context:
 *	Kernel context.
 */
int
qla4xxx_test_active_port(fc_port_t *fcport) 
{
	tur_cmd_rsp_t	*pkt;
	int		rval = 0 ; 
	dma_addr_t	phys_address = 0;
	int		retry;
	uint16_t	comp_status;
	uint16_t	scsi_status;
	scsi_qla_host_t *ha;
	uint16_t	lun = 0;
	fc_lun_t	*fclun;

	ENTER(__func__);

	ha = fcport->ha;
	if (atomic_read(&fcport->state) == FCS_DEVICE_DEAD) {
		DEBUG2(printk("scsi(%ld) %s leaving: Port 0x%02x is marked "
		    "DEAD\n", ha->host_no,__func__,fcport->loop_id);)
		return rval;
	}

	if ((fclun = qla4xxx_find_data_lun(fcport)) == NULL) {
		DEBUG2(printk(KERN_INFO "%s leaving: Couldn't find data lun\n",
		    __func__);)
		return rval;
	} 
	lun = fclun->lun;

	pkt = pci_alloc_consistent(ha->pdev, sizeof(tur_cmd_rsp_t),
	    &phys_address);

	if (pkt == NULL) {
		printk(KERN_WARNING
		    "scsi(%ld): Memory Allocation failed - TUR\n",
		    ha->host_no);
		ha->mem_err++;
		return rval;
	}

	retry = 4;
	do {
		memset(pkt, 0, sizeof(tur_cmd_rsp_t));
		//pkt->p.cmd.entry_type = COMMAND_A64_TYPE;
		//pkt->p.cmd.entry_count = 1;
		//pkt->p.cmd.lun = cpu_to_le16(lun);
		// SET_TARGET_ID(ha, pkt->p.cmd.target, fcport->loop_id);
		pkt->p.cmd.hdr.entryType = ET_COMMAND;
		/* 8 byte lun number */
		pkt->p.cmd.lun[1]            = LSB(cpu_to_le16(lun)); /*SAMII compliant lun*/
		pkt->p.cmd.lun[2]            = MSB(cpu_to_le16(lun));
		pkt->p.cmd.target = cpu_to_le16(ddb_entry->fw_ddb_index);

		pkt->p.cmd.control_flags = CF_SIMPLE_TAG;
		pkt->p.cmd.scsi_cdb[0] = TEST_UNIT_READY;

		pkt->p.cmd.dataSegCnt = __constant_cpu_to_le16(0);
		pkt->p.cmd.timeout = __constant_cpu_to_le16(10);
		pkt->p.cmd.ttlByteCnt = __constant_cpu_to_le32(0);

		rval = qla4xxx_issue_iocb(ha, pkt, phys_address,
		    sizeof(tur_cmd_rsp_t));

		comp_status = le16_to_cpu(pkt->p.rsp.comp_status);
		scsi_status = le16_to_cpu(pkt->p.rsp.scsi_status);

 		/* Port Logged Out, so don't retry */
		if (comp_status == CS_PORT_LOGGED_OUT ||
		    comp_status == CS_PORT_CONFIG_CHG ||
		    comp_status == CS_PORT_BUSY ||
		    comp_status == CS_INCOMPLETE ||
		    comp_status == CS_PORT_UNAVAILABLE)
			break;

		DEBUG(printk("scsi(%ld:%04x:%d) %s: TEST UNIT READY - comp "
		    "status 0x%x, scsi status 0x%x, rval=%d\n", ha->host_no,
		    fcport->loop_id, lun,__func__, comp_status, scsi_status,
		    rval));
		if ((scsi_status & SS_CHECK_CONDITION)) {
			DEBUG2(printk("%s: check status bytes = "
			    "0x%02x 0x%02x 0x%02x\n", __func__,
			    pkt->p.rsp.req_sense_data[2],
			    pkt->p.rsp.req_sense_data[12],
			    pkt->p.rsp.req_sense_data[13]));

			if (pkt->p.rsp.req_sense_data[2] == NOT_READY &&
			    pkt->p.rsp.req_sense_data[12] == 0x4 &&
			    pkt->p.rsp.req_sense_data[13] == 0x2)
				break;
		}
	} while ((rval != QLA_SUCCESS || comp_status != CS_COMPLETE ||
	    (scsi_status & SS_CHECK_CONDITION)) && retry--);

	if (rval == QLA_SUCCESS &&
	    (!((scsi_status & SS_CHECK_CONDITION) &&
		(pkt->p.rsp.req_sense_data[2] == NOT_READY &&
		    pkt->p.rsp.req_sense_data[12] == 0x4 &&
		    pkt->p.rsp.req_sense_data[13] == 0x2 ) ) &&
	     comp_status == CS_COMPLETE)) {
		DEBUG2(printk("scsi(%ld) %s - Port (0x%04x) set to ACTIVE.\n",
		    ha->host_no, __func__, fcport->loop_id));
		/* We found an active path */
       		fcport->flags |= FCF_MSA_PORT_ACTIVE;
		rval = 1;
	} else {
		DEBUG2(printk("scsi(%ld) %s - Port (0x%04x) set to INACTIVE.\n",
		    ha->host_no, __func__, fcport->loop_id));
       		fcport->flags &= ~(FCF_MSA_PORT_ACTIVE);
	}

	pci_free_consistent(ha->pdev, sizeof(tur_cmd_rsp_t), pkt, phys_address);

	LEAVE(__func__);

	return rval;
}
#endif
/*
 * qla4xxx_cfg_lun
 *	Configures LUN into fcport LUN list.
 *
 * Input:
 *	fcport:		FC port structure pointer.
 *	lun:		LUN number.
 *
 * Context:
 *	Kernel context.
 */
fc_lun_t *
qla4xxx_cfg_lun(scsi_qla_host_t *ha, fc_port_t *fcport, uint16_t lun,
                inq_cmd_rsp_t *inq, dma_addr_t inq_dma)
{
	fc_lun_t *fclun;

	/* Bypass LUNs that failed. */
	if (qla4xxx_failover_enabled(ha)) {
	if (qla4xxx_inquiry(ha, fcport, lun, inq, inq_dma) != QLA_SUCCESS) {
		DEBUG2(printk("scsi(%d): Failed inquiry - loop id=0x%04x "
		    "lun=%d\n", ha->host_no, fcport->loop_id, lun));

		return (NULL);
	}
	}

	switch (inq->inq[0]) {
	case TYPE_DISK:
	case TYPE_PROCESSOR:
	case TYPE_WORM:
	case TYPE_ROM:
	case TYPE_SCANNER:
	case TYPE_MOD:
	case TYPE_MEDIUM_CHANGER:
	case TYPE_ENCLOSURE:
	case 0x20:
	case 0x0C:
		break;
	case TYPE_TAPE:
		fcport->flags |= FCF_TAPE_PRESENT;
		break;
	default:
		DEBUG2(printk("scsi(%d): Unsupported lun type -- "
		    "loop id=0x%04x lun=%d type=%x\n",
		    ha->host_no, fcport->loop_id, lun, inq->inq[0]));
		return (NULL);
	}

	fcport->device_type = inq->inq[0];
	
	/* Does this port require special failover handling? */
	if (qla4xxx_failover_enabled(ha)) {
		fcport->cfg_id = qla4xxx_cfg_lookup_device(&inq->inq[0]);
		qla4xxx_set_device_flags(ha, fcport);
	}
	fclun = qla4xxx_add_fclun(fcport, lun);

	if (fclun != NULL) {
		atomic_set(&fcport->state, FCS_ONLINE);
	}

	return (fclun);
}

/*
 * qla4xxx_lun_discovery
 *	Issue SCSI inquiry command for LUN discovery.
 *
 * Input:
 *	ha:		adapter state pointer.
 *	fcport:		FC port structure pointer.
 *
 * Context:
 *	Kernel context.
 */
void
qla4xxx_lun_discovery(scsi_qla_host_t *ha, fc_port_t *fcport)
{
	inq_cmd_rsp_t	*inq;
	dma_addr_t	inq_dma;
	uint16_t	lun;

	inq = pci_alloc_consistent(ha->pdev, sizeof(inq_cmd_rsp_t), &inq_dma);
	if (inq == NULL) {
		printk(KERN_WARNING
		    "Memory Allocation failed - INQ\n");
		return;
	}

	/* If report LUN works, exit. */
	if (qla4xxx_rpt_lun_discovery(ha, fcport, inq, inq_dma) !=
	    QLA_SUCCESS) {
		for (lun = 0; lun < MAX_LUNS; lun++) {
			/* Configure LUN. */
			qla4xxx_cfg_lun(ha, fcport, lun, inq, inq_dma);
		}
	}

	pci_free_consistent(ha->pdev, sizeof(inq_cmd_rsp_t), inq, inq_dma);
}

/*
 * qla4xxx_rpt_lun_discovery
 *	Issue SCSI report LUN command for LUN discovery.
 *
 * Input:
 *	ha:		adapter state pointer.
 *	fcport:		FC port structure pointer.
 *
 * Returns:
 *	qla2x00 local function return status code.
 *
 * Context:
 *	Kernel context.
 */
int
qla4xxx_rpt_lun_discovery(scsi_qla_host_t *ha, fc_port_t *fcport,
    inq_cmd_rsp_t *inq, dma_addr_t inq_dma)
{
	int			rval;
	uint32_t		len, cnt;
	uint16_t		lun;
	rpt_lun_cmd_rsp_t	*rlc;
	dma_addr_t		rlc_dma;

	/* Assume a failed status */
	rval = QLA_ERROR;

	/* No point in continuing if the device doesn't support RLC */
	if ((fcport->flags & FCF_RLC_SUPPORT) == 0)
		return (rval);

	rlc = pci_alloc_consistent(ha->pdev, sizeof(rpt_lun_cmd_rsp_t),
	    &rlc_dma);
	if (rlc == NULL) {
		printk(KERN_WARNING
			"Memory Allocation failed - RLC");
		return QLA_ERROR;
	}
	rval = qla4xxx_report_lun(ha, fcport, rlc, rlc_dma);
	if (rval != QLA_SUCCESS) {
		pci_free_consistent(ha->pdev, sizeof(rpt_lun_cmd_rsp_t), rlc,
		    rlc_dma);
		return (rval);
	}

	/* Always add a fc_lun_t structure for lun 0 -- mid-layer requirement */
	qla4xxx_add_fclun(fcport, 0);

	/* Configure LUN list. */
	len = be32_to_cpu(rlc->list.hdr.len);
	len /= 8;
	for (cnt = 0; cnt < len; cnt++) {
		lun = CHAR_TO_SHORT(rlc->list.lst[cnt].lsb,
		    rlc->list.lst[cnt].msb.b);

		DEBUG2(printk("scsi(%d): RLC lun = (%d)\n", ha->host_no, lun));

		/* We only support 0 through MAX_LUNS-1 range */
		if (lun < MAX_LUNS) {
			qla4xxx_cfg_lun(ha, fcport, lun, inq, inq_dma);
		}
	}
		atomic_set(&fcport->state, FCS_ONLINE);

	pci_free_consistent(ha->pdev, sizeof(rpt_lun_cmd_rsp_t), rlc, rlc_dma);

	return (rval);
}

/*
 * qla4xxx_report_lun
 *	Issue SCSI report LUN command.
 *
 * Input:
 *	ha:		adapter state pointer.
 *	fcport:		FC port structure pointer.
 *	mem:		pointer to dma memory object for report LUN IOCB
 *			packet.
 *
 * Returns:
 *	qla2x00 local function return status code.
 *
 * Context:
 *	Kernel context.
 */
static int
qla4xxx_report_lun(scsi_qla_host_t *ha, fc_port_t *fcport,
		   rpt_lun_cmd_rsp_t *rlc, dma_addr_t rlc_dma)
{
	int rval;
	uint16_t retries;
	uint8_t comp_status;
	uint8_t scsi_status;
	uint8_t iscsi_flags;
	ddb_entry_t	*ddb_entry = fcport->ddbptr;

	rval = QLA_ERROR;

	for (retries = 3; retries; retries--) {
		memset(rlc, 0, sizeof(rpt_lun_cmd_rsp_t));
		rlc->p.cmd.hdr.entryType = ET_COMMAND;
		
		/* rlc->p.cmd.handle = 1; */
		rlc->p.cmd.hdr.entryCount = 1;
		rlc->p.cmd.target = cpu_to_le16(ddb_entry->fw_ddb_index);
		rlc->p.cmd.control_flags = (CF_READ | CF_SIMPLE_TAG);
		rlc->p.cmd.cdb[0] = REPORT_LUNS;
		rlc->p.cmd.cdb[8] = MSB(sizeof(rpt_lun_lst_t));
		rlc->p.cmd.cdb[9] = LSB(sizeof(rpt_lun_lst_t));
		rlc->p.cmd.dataSegCnt = __constant_cpu_to_le16(1);
		rlc->p.cmd.timeout = __constant_cpu_to_le16(10);
		rlc->p.cmd.ttlByteCnt =
		    __constant_cpu_to_le32(sizeof(rpt_lun_lst_t));
		rlc->p.cmd.dataseg[0].base.addrLow = cpu_to_le32(
		    LSDW(rlc_dma + sizeof(STATUS_ENTRY)));
		rlc->p.cmd.dataseg[0].base.addrHigh = cpu_to_le32(
		    MSDW(rlc_dma + sizeof(STATUS_ENTRY)));
		rlc->p.cmd.dataseg[0].count =
		    __constant_cpu_to_le32(sizeof(rpt_lun_lst_t));
		/*  rlc->p.cmd.lun[8];	 always lun 0 */
		/* If in connection mode, bump sequence number */
		if ((ha->firmware_options & FWOPT_SESSION_MODE) != 0)
		 	ddb_entry->CmdSn++;
		rlc->p.cmd.cmdSeqNum = cpu_to_le32(ddb_entry->CmdSn);	

		rval = qla4xxx_issue_iocb(ha, rlc, rlc_dma,
		    sizeof(rpt_lun_cmd_rsp_t));
			
		comp_status = rlc->p.rsp.completionStatus;
		scsi_status = rlc->p.rsp.scsiStatus;
		iscsi_flags = rlc->p.rsp.iscsiFlags;

		if (rval != QLA_SUCCESS ||
		    comp_status != SCS_COMPLETE ||
		    scsi_status & SCSISTAT_CHECK_CONDITION) {

			/* Device underrun, treat as OK. */
			if (rval == QLA_SUCCESS &&
			    comp_status == SCS_DATA_UNDERRUN &&
			    iscsi_flags & ISCSI_FLAG_RESIDUAL_UNDER) {

				rval = QLA_SUCCESS;
				break;
			}

			DEBUG2(printk("scsi(%d): RLC failed to issue iocb! "
			    "fcport=[%04x/%p] rval=%x cs=%x ss=%x\n",
			    ha->host_no, fcport->loop_id, fcport, rval,
			    comp_status, scsi_status));

			rval = QLA_ERROR;
			if (scsi_status & SCSISTAT_CHECK_CONDITION) {
				DEBUG2(printk("scsi(%d): RLC "
				    "SCSISTAT_CHECK_CONDITION Sense Data "
				    "%02x %02x %02x %02x %02x %02x %02x %02x\n",
				    ha->host_no,
				    rlc->p.rsp.senseData[0],
				    rlc->p.rsp.senseData[1],
				    rlc->p.rsp.senseData[2],
				    rlc->p.rsp.senseData[3],
				    rlc->p.rsp.senseData[4],
				    rlc->p.rsp.senseData[5],
				    rlc->p.rsp.senseData[6],
				    rlc->p.rsp.senseData[7]));
				if (rlc->p.rsp.senseData[2] ==
				    ILLEGAL_REQUEST) {
					fcport->flags &= ~(FCF_RLC_SUPPORT);
					break;
				}
			}
		} else {
			break;
		}
	}

	return (rval);
}

#if MSA1000_SUPPORTED
static int
qla4xxx_spinup(scsi_qla_host_t *ha, fc_port_t *fcport, uint16_t lun) 
{
	inq_cmd_rsp_t	*pkt;
	int		rval = QLA_SUCCESS;
	int		count, retry;
	dma_addr_t	phys_address = 0;
	uint16_t	comp_status = CS_COMPLETE;
	uint16_t	scsi_status = 0;

	ENTER(__func__);

	pkt = pci_alloc_consistent(ha->pdev,
	    sizeof(inq_cmd_rsp_t), &phys_address);

	if (pkt == NULL) {
		printk(KERN_WARNING
		    "scsi(%ld): Memory Allocation failed - INQ\n",
		    ha->host_no);
		return( QLA_FUNCTION_FAILED);
	}

	count = 5; 
	retry = 5;
	if (atomic_read(&fcport->state) != FCS_ONLINE) {
		DEBUG2(printk("scsi(%ld) %s leaving: Port 0x%02x is not ONLINE\n",
			ha->host_no,__func__,fcport->loop_id);)
		rval =  QLA_FUNCTION_FAILED;
	}
	else do {
		/* issue spinup */
		memset(pkt, 0, sizeof(inq_cmd_rsp_t));
		pkt->p.cmd.entry_type = COMMAND_A64_TYPE;
		pkt->p.cmd.entry_count = 1;
		/* 8 byte lun number */
		inq->p.cmd.lun[1]            = LSB(cpu_to_le16(lun)); /*SAMII compliant lun*/
		inq->p.cmd.lun[2]            = MSB(cpu_to_le16(lun));
		SET_TARGET_ID(ha, pkt->p.cmd.target, fcport->loop_id);
		/* no direction for this command */
		pkt->p.cmd.control_flags =
		    __constant_cpu_to_le16(CF_SIMPLE_TAG);
		pkt->p.cmd.scsi_cdb[0] = START_STOP;
		pkt->p.cmd.scsi_cdb[4] = 1;	/* start spin cycle */
		pkt->p.cmd.dseg_count = __constant_cpu_to_le16(0);
		pkt->p.cmd.timeout = __constant_cpu_to_le16(20);
		pkt->p.cmd.byte_count = __constant_cpu_to_le32(0);

		rval = qla4xxx_issue_iocb(ha, pkt,
		    phys_address, sizeof(inq_cmd_rsp_t));

		comp_status = le16_to_cpu(pkt->p.rsp.comp_status);
		scsi_status = le16_to_cpu(pkt->p.rsp.scsi_status);

 		/* Port Logged Out, so don't retry */
		if( 	comp_status == CS_PORT_LOGGED_OUT  ||
			comp_status == CS_PORT_CONFIG_CHG ||
			comp_status == CS_PORT_BUSY ||
			comp_status == CS_INCOMPLETE ||
			comp_status == CS_PORT_UNAVAILABLE ) {
			break;
		}

		if ( (scsi_status & SS_CHECK_CONDITION) ) {
			DEBUG2(printk("%s(%ld): SS_CHECK_CONDITION "
			    "Sense Data "
			    "%02x %02x %02x %02x "
			    "%02x %02x %02x %02x\n",
			    __func__,
			    ha->host_no,
			    pkt->p.rsp.req_sense_data[0],
			    pkt->p.rsp.req_sense_data[1],
			    pkt->p.rsp.req_sense_data[2],
			    pkt->p.rsp.req_sense_data[3],
			    pkt->p.rsp.req_sense_data[4],
			    pkt->p.rsp.req_sense_data[5],
			    pkt->p.rsp.req_sense_data[6],
			    pkt->p.rsp.req_sense_data[7]);)
				if (pkt->p.rsp.req_sense_data[2] ==
				    NOT_READY  &&
				    (pkt->p.rsp.req_sense_data[12] == 4 ) &&
				    (pkt->p.rsp.req_sense_data[13] == 3 ) ) {

					current->state = TASK_UNINTERRUPTIBLE;
					schedule_timeout(HZ);
					printk(".");
					count--;
				} else
					retry--;
		}

		printk(KERN_INFO 
			"qla_fo(%ld): Sending Start - count %d, retry=%d"
		    "comp status 0x%x, "
		    "scsi status 0x%x, rval=%d\n",
				ha->host_no,
		    count,
		    retry,
		    comp_status,
		    scsi_status, 
		    rval);

		if ((rval != QLA_SUCCESS) || (comp_status != CS_COMPLETE))
			retry--;

	} while ( count && retry  &&
		 (rval != QLA_SUCCESS ||
		  comp_status != CS_COMPLETE ||
		(scsi_status & SS_CHECK_CONDITION)));


	if (rval != QLA_SUCCESS ||
	    comp_status != CS_COMPLETE ||
	    (scsi_status & SS_CHECK_CONDITION)) {

		DEBUG(printk("qla_fo(%ld): Failed spinup - "
		    "comp status 0x%x, "
		    "scsi status 0x%x. loop_id=%d\n",
				ha->host_no,
		    comp_status,
		    scsi_status, 
		    fcport->loop_id);)
				rval =  QLA_FUNCTION_FAILED;
	}

	pci_free_consistent(ha->pdev, sizeof(inq_cmd_rsp_t),
	    pkt, phys_address);


	LEAVE(__func__);

	return( rval );

}
#endif




