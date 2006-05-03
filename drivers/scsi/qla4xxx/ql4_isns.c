/******************************************************************************
 *     Copyright (C)  2003 -2005 QLogic Corporation
 * QLogic ISP4xxx Device Driver
 *
 * This program includes a device driver for Linux 2.6.x that may be
 * distributed with QLogic hardware specific firmware binary file.
 * You may modify and redistribute the device driver code under the
 * GNU General Public License as published by the Free Software Foundation
 * (version 2 or a later version) and/or under the following terms,
 * as applicable:
 *
 * 	1. Redistribution of source code must retain the above copyright
 *         notice, this list of conditions and the following disclaimer.
 * 	2. Redistribution in binary form must reproduce the above copyright
 *         notice, this list of conditions and the following disclaimer in
 *         the documentation and/or other materials provided with the
 *         distribution.
 * 	3. The name of QLogic Corporation may not be used to endorse or
 *         promote products derived from this software without specific
 *         prior written permission
 * 	
 * You may redistribute the hardware specific firmware binary file under
 * the following terms:
 * 	1. Redistribution of source code (only if applicable), must
 *         retain the above copyright notice, this list of conditions and
 *         the following disclaimer.
 * 	2. Redistribution in binary form must reproduce the above
 *         copyright notice, this list of conditions and the following
 *         disclaimer in the documentation and/or other materials provided
 *         with the distribution.
 * 	3. The name of QLogic Corporation may not be used to endorse or
 *         promote products derived from this software without specific
 *         prior written permission
 *
 * REGARDLESS OF WHAT LICENSING MECHANISM IS USED OR APPLICABLE,
 * THIS PROGRAM IS PROVIDED BY QLOGIC CORPORATION "AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * USER ACKNOWLEDGES AND AGREES THAT USE OF THIS PROGRAM WILL NOT CREATE
 * OR GIVE GROUNDS FOR A LICENSE BY IMPLICATION, ESTOPPEL, OR OTHERWISE
 * IN ANY INTELLECTUAL PROPERTY RIGHTS (PATENT, COPYRIGHT, TRADE SECRET,
 * MASK WORK, OR OTHER PROPRIETARY RIGHT) EMBODIED IN ANY OTHER QLOGIC
 * HARDWARE OR SOFTWARE EITHER SOLELY OR IN COMBINATION WITH THIS PROGRAM
 *
 ******************************************************************************
 *             Please see release.txt for revision history.                   *
 *                                                                            *
 ******************************************************************************
 * Function Table of Contents:
 *	qla4xxx_strtolower
 *	qla4xxx_isns_build_entity_id
 *	qla4xxx_isns_reenable
 *	qla4xxx_isns_enable_callback
 *	qla4xxx_isns_restart_service
 *	qla4xxx_isns_restart_service_completion
 *	qla4xxx_isns_init_isns_reg_attr_list
 *	qla4xxx_isns_init_isns_dereg_attr_list
 *	qla4xxx_isns_init_isns_scn_reg_attr_list
 *	qla4xxx_isns_init_isns_scn_dereg_attr_list
 *	qla4xxx_isns_init_isns_dev_get_next_attr_list
 *	qla4xxx_isns_init_isns_dev_attr_qry_attr_list
 *	qla4xxx_isns_init_attributes
 *	qla4xxx_isns_append_attribute
 *	qla4xxx_isns_build_iocb_handle
 *	qla4xxx_isns_get_server_request
 *	qla4xxx_isns_build_scn_registration_packet
 *	qla4xxx_isns_build_scn_deregistration_packet
 *	qla4xxx_isns_build_registration_packet
 *	qla4xxx_isns_build_deregistration_packet
 *	qla4xxx_isns_build_request_packet
 *	qla4xxx_isns_build_server_request_response_packet
 *	qla4xxx_isns_build_dev_get_next_packet
 *	qla4xxx_isns_build_dev_attr_qry_packet
 *	qla4xxx_isns_parse_get_next_response
 *	qla4xxx_isns_parse_query_response
 *	qla4xxx_isns_process_response
 *	qla4xxx_isns_reassemble_pdu
 *	qla4xxx_isns_scn
 *	qla4xxx_isns_esi
 *	qla4xxx_isns_server_request_error
 *	qla4xxx_isns_parse_and_dispatch_server_request
 *	qla4xxx_isns_parse_and_dispatch_server_response
 *	qla4xxx_isns_dev_attr_reg
 *	qla4xxx_isns_dev_attr_reg_rsp
 *	qla4xxx_isns_scn_reg
 *	qla4xxx_isns_scn_reg_rsp
 *	qla4xxx_isns_dev_attr_qry
 *	qla4xxx_isns_dev_attr_qry_rsp
 *	qla4xxx_isns_dev_get_next
 *	qla4xxx_isns_dev_get_next_rsp
 *	qla4xxx_isns_dev_dereg
 *	qla4xxx_isns_dev_dereg_rsp
 *	qla4xxx_isns_scn_dereg
 *	qla4xxx_isns_scn_dereg_rsp
 ****************************************************************************/

#include "ql4_def.h"

void     qla4xxx_isns_enable_callback(scsi_qla_host_t *, uint32_t, uint32_t, uint32_t, uint32_t);
uint8_t  qla4xxx_isns_restart_service(scsi_qla_host_t *);
uint32_t qla4xxx_isns_build_iocb_handle(scsi_qla_host_t *, uint32_t, PDU_ENTRY *);
uint8_t  qla4xxx_isns_get_server_request(scsi_qla_host_t *, uint32_t, uint16_t);
uint8_t  qla4xxx_isns_reassemble_pdu(scsi_qla_host_t *, uint8_t *, uint32_t *);
uint8_t  qla4xxx_isns_parse_and_dispatch_server_request(scsi_qla_host_t *, uint8_t *, uint32_t, uint16_t);
uint8_t  qla4xxx_isns_parse_and_dispatch_server_response(scsi_qla_host_t *, uint8_t *, uint32_t);
uint8_t  qla4xxx_isns_build_scn_registration_packet(scsi_qla_host_t *ha,
							   uint8_t *buffer,
							   uint32_t buffer_size,
							   uint32_t *packet_size);
uint8_t  qla4xxx_isns_build_scn_deregistration_packet(scsi_qla_host_t *ha,
							     uint8_t *buffer,
							     uint32_t buffer_size,
							     uint32_t *packet_size);
uint8_t  qla4xxx_isns_build_registration_packet(scsi_qla_host_t *ha,
						       uint8_t *buff,
						       uint32_t buff_size,
						       uint8_t *isns_entity_id,
						       uint8_t *ip_addr,
						       uint32_t port_number,
						       uint32_t scn_port,
						       uint32_t esi_port,
						       uint8_t *local_alias,
						       uint32_t *packet_size);
uint8_t  qla4xxx_isns_build_deregistration_packet(scsi_qla_host_t *ha,
							 uint8_t *buff,
							 uint32_t buff_size,
							 uint8_t *isns_entity_id,
							 uint8_t *ip_addr,
							 uint32_t port_number,
							 uint32_t *packet_size);
uint8_t  qla4xxx_isns_build_request_packet(scsi_qla_host_t *ha,
						  uint8_t *buff,
						  uint32_t buff_size,
						  uint16_t function_id,
						  uint16_t tx_id,
						  uint8_t  use_replace_flag,
						  ATTRIBUTE_LIST *attr_list,
						  uint32_t *packet_size);
uint8_t  qla4xxx_isns_append_attribute(scsi_qla_host_t *ha,
					      uint8_t **buffer,
					      uint8_t *buffer_end,
					      ATTRIBUTE_LIST *attr_list);
uint8_t  qla4xxx_isns_dev_attr_reg(scsi_qla_host_t *);

uint8_t  qla4xxx_isns_dev_attr_reg_rsp(scsi_qla_host_t *ha,
					      uint8_t *buffer,
					      uint32_t buffer_size);
uint8_t  qla4xxx_isns_dev_attr_qry_rsp(scsi_qla_host_t *ha,
					      uint8_t *buffer,
					      uint32_t buffer_size);
uint8_t  qla4xxx_isns_dev_get_next_rsp(scsi_qla_host_t *ha,
					      uint8_t *buffer,
					      uint32_t buffer_size);
uint8_t  qla4xxx_isns_dev_dereg_rsp(scsi_qla_host_t *ha,
					   uint8_t *buffer,
					   uint32_t buffer_size);
uint8_t  qla4xxx_isns_scn_reg_rsp(scsi_qla_host_t *ha,
					 uint8_t *buffer,
					 uint32_t buffer_size);
uint8_t  qla4xxx_isns_scn_dereg_rsp(scsi_qla_host_t *ha,
					   uint8_t *buffer,
					   uint32_t buffer_size);

uint8_t  qla4xxx_isns_scn_dereg(scsi_qla_host_t *);
uint8_t  qla4xxx_isns_scn_reg(scsi_qla_host_t *ha);
uint8_t  qla4xxx_isns_dev_get_next (scsi_qla_host_t *ha,
					   uint8_t *last_iscsi_name);


const char *isns_error_code_msg[] = ISNS_ERROR_CODE_TBL();

static void
qla4xxx_strtolower(uint8_t *str)
{
	uint8_t *tmp;
	for (tmp = str; *tmp != '\0'; tmp++) {
		if (*tmp >= 'A' && *tmp <= 'Z')
			*tmp += 'a' - 'A';
	}
}

void
qla4xxx_isns_build_entity_id(scsi_qla_host_t *ha)
{
	sprintf(ha->isns_entity_id, "%s.%d", ha->serial_number, ha->function_number);
	qla4xxx_strtolower(ha->isns_entity_id);
}

uint8_t
qla4xxx_isns_reenable(scsi_qla_host_t *ha,
		      uint32_t isns_ip_addr,
		      uint16_t isns_server_port_num)
{
	set_bit(ISNS_FLAG_REREGISTER, &ha->isns_flags);
	ISNS_CLEAR_FLAGS(ha);

	if (qla4xxx_isns_enable(ha, isns_ip_addr, isns_server_port_num)
	    != QLA_SUCCESS) {
		QL4PRINT(QLP2, printk("scsi%d: %s: Failed!\n",
				      ha->host_no, __func__));
		return(QLA_ERROR);
	}

	return(QLA_SUCCESS);
}

/* interrupt context, hardware lock set */
void
qla4xxx_isns_enable_callback(scsi_qla_host_t *ha,
			     uint32_t svr,
			     uint32_t scn,
			     uint32_t esi,
			     uint32_t nsh)
{
	ha->isns_connection_id   = (uint16_t) svr & 0x0000FFFF;
	ha->isns_scn_conn_id     = (uint16_t) scn & 0x0000FFFF;
	ha->isns_esi_conn_id     = (uint16_t) esi & 0x0000FFFF;
	ha->isns_nsh_conn_id     = (uint16_t) nsh & 0x0000FFFF;

	ha->isns_remote_port_num = (uint16_t) (svr >> 16);
	ha->isns_scn_port_num    = (uint16_t) (scn >> 16);
	ha->isns_esi_port_num    = (uint16_t) (esi >> 16);
	ha->isns_nsh_port_num    = (uint16_t) (nsh >> 16);

	QL4PRINT(QLP20,
		 printk("scsi%d: %s: iSNS Server TCP Connect succeeded %d\n",
			ha->host_no, __func__, svr));
	QL4PRINT(QLP20,
		 printk("scsi%d: %s: Remote iSNS Server %d ConnID %x\n",
			ha->host_no, __func__,
			ha->isns_remote_port_num,
			ha->isns_connection_id));
	QL4PRINT(QLP20,
		 printk("scsi%d: %s: Local  SCN  Listen %d ConnID %x\n",
			ha->host_no, __func__,
			ha->isns_scn_port_num,
			ha->isns_scn_conn_id));
	QL4PRINT(QLP20,
		 printk("scsi%d: %s: Local  ESI  Listen %d ConnID %x\n",
			ha->host_no, __func__,
			ha->isns_esi_port_num,
			ha->isns_esi_conn_id));
	QL4PRINT(QLP20,
		 printk("scsi%d: %s: Local  HSN  Listen %d ConnID %x\n",
			ha->host_no, __func__,
			ha->isns_nsh_port_num,
			ha->isns_nsh_conn_id));

	if (ha->isns_connection_id == (uint16_t)-1) {
		QL4PRINT(QLP2,
			 printk("scsi%d: %s: iSNS server refused connection\n",
				ha->host_no, __func__));

		qla4xxx_isns_restart_service(ha);
		return;
	}

	set_bit(ISNS_FLAG_ISNS_SRV_ENABLED, &ha->isns_flags);

	if (test_bit(ISNS_FLAG_REREGISTER, &ha->isns_flags)) {
		if (qla4xxx_isns_scn_dereg(ha) != QLA_SUCCESS) {
			QL4PRINT(QLP2,
				 printk("scsi%d: %s: qla4xxx_isns_scn_dereg failed!\n",
					ha->host_no, __func__));
			return;
		}
	}
	else {
		if (qla4xxx_isns_dev_attr_reg(ha) != QLA_SUCCESS) {
			QL4PRINT(QLP2,
				 printk("scsi%d: %s: qla4xxx_isns_dev_attr_reg failed!\n",
					ha->host_no, __func__));
			return;
		}
	}
}


uint8_t
qla4xxx_isns_restart_service(scsi_qla_host_t *ha)
{
	qla4xxx_isns_disable(ha);
	set_bit(ISNS_FLAG_RESTART_SERVICE, &ha->isns_flags);
	ISNS_CLEAR_FLAGS(ha);

	/* Set timer for restart to complete */
	atomic_set(&ha->isns_restart_timer, ISNS_RESTART_TOV);
	return(QLA_SUCCESS);
}

uint8_t
qla4xxx_isns_restart_service_completion(scsi_qla_host_t *ha,
					uint32_t isns_ip_addr,
					uint16_t isns_server_port_num)
{
	QL4PRINT(QLP20, printk("scsi%d: %s: isns_ip_addr %08x\n",
			       ha->host_no, __func__, isns_ip_addr));

	if (qla4xxx_isns_enable(ha, isns_ip_addr, isns_server_port_num)
	    != QLA_SUCCESS) {
		QL4PRINT(QLP2, printk("scsi%d: %s: failed!\n",
				      ha->host_no, __func__));
		return(QLA_ERROR);
	}
	else {
		set_bit(ISNS_FLAG_REREGISTER, &ha->isns_flags);
		ISNS_CLEAR_FLAGS(ha);
		return(QLA_SUCCESS);
	}
}


static void
qla4xxx_isns_init_isns_reg_attr_list(scsi_qla_host_t *ha)
{
	ATTRIBUTE_LIST isns_reg_attr_list[] = {
		/* Source attribute */
		{ ISNS_ATTR_TAG_ISCSI_NAME,        ISNS_ATTR_TYPE_STRING,  (unsigned long) ha->name_string},
		{ ISNS_ATTR_TAG_ENTITY_IDENTIFIER, ISNS_ATTR_TYPE_STRING,  -1},
		/* Entity ID. */
		{ ISNS_ATTR_TAG_DELIMITER,         ISNS_ATTR_TYPE_EMPTY,   0},
		/* Operating attributes to register */
		{ ISNS_ATTR_TAG_ENTITY_IDENTIFIER, ISNS_ATTR_TYPE_STRING,  -1},
		{ ISNS_ATTR_TAG_ENTITY_PROTOCOL,   ISNS_ATTR_TYPE_ULONG,   cpu_to_be32(ENTITY_PROTOCOL_ISCSI)},
		{ ISNS_ATTR_TAG_PORTAL_IP_ADDRESS, ISNS_ATTR_TYPE_ADDRESS, -1},
		{ ISNS_ATTR_TAG_PORTAL_PORT,       ISNS_ATTR_TYPE_ULONG,   -1},
		{ ISNS_ATTR_TAG_SCN_PORT,          ISNS_ATTR_TYPE_ULONG,   -1},
		{ ISNS_ATTR_TAG_ESI_PORT,          ISNS_ATTR_TYPE_ULONG,   -1},
		{ ISNS_ATTR_TAG_ISCSI_NAME,        ISNS_ATTR_TYPE_STRING,  (unsigned long) ha->name_string},
		{ ISNS_ATTR_TAG_ISCSI_NODE_TYPE,   ISNS_ATTR_TYPE_ULONG,   cpu_to_be32(ISCSI_NODE_TYPE_INITIATOR)},
		{ ISNS_ATTR_TAG_ISCSI_ALIAS,       ISNS_ATTR_TYPE_STRING,  -1},		// Friendly machine name?

		{ 0, 0, 0}		// Terminating NULL entry
	};

	memcpy(ha->isns_reg_attr_list,          isns_reg_attr_list,          sizeof(isns_reg_attr_list));
}

static void
qla4xxx_isns_init_isns_dereg_attr_list(scsi_qla_host_t *ha)
{
	ATTRIBUTE_LIST isns_dereg_attr_list[] = {
		// Source attribute
		{ ISNS_ATTR_TAG_ISCSI_NAME,        ISNS_ATTR_TYPE_STRING,  (unsigned long) ha->name_string},
		// No key attribute for DevDereg
		{ ISNS_ATTR_TAG_DELIMITER,         ISNS_ATTR_TYPE_EMPTY,    0},
		// Operating attributes
		{ ISNS_ATTR_TAG_ENTITY_IDENTIFIER, ISNS_ATTR_TYPE_STRING,  -1},		// FQDN
#if 0
		{ ISNS_ATTR_TAG_PORTAL_IP_ADDRESS, ISNS_ATTR_TYPE_ADDRESS, -1},
		{ ISNS_ATTR_TAG_PORTAL_PORT,       ISNS_ATTR_TYPE_ULONG,   -1},
		{ ISNS_ATTR_TAG_ISCSI_NAME,        ISNS_ATTR_TYPE_STRING,  (unsigned long) ha->name_string},
#endif

		{ 0, 0, 0}		// Terminating NULL entry
	};

	memcpy(ha->isns_dereg_attr_list,        isns_dereg_attr_list,        sizeof(isns_dereg_attr_list));
}

static void
qla4xxx_isns_init_isns_scn_reg_attr_list(scsi_qla_host_t *ha)
{
	ATTRIBUTE_LIST isns_scn_reg_attr_list[] = {
		// Source attribute
		{ ISNS_ATTR_TAG_ISCSI_NAME,        ISNS_ATTR_TYPE_STRING,  (unsigned long) ha->name_string},
		// Key attributes
		{ ISNS_ATTR_TAG_ISCSI_NAME,        ISNS_ATTR_TYPE_STRING,  (unsigned long) ha->name_string},
		// Required delimiter to indicate division between key and operating attrs.
		{ ISNS_ATTR_TAG_DELIMITER,         ISNS_ATTR_TYPE_EMPTY,   0},
		// Operating attributes
		{ ISNS_ATTR_TAG_ISCSI_SCN_BITMAP,  ISNS_ATTR_TYPE_ULONG,   cpu_to_be32(ISCSI_SCN_OBJECT_UPDATED |
										  ISCSI_SCN_OBJECT_ADDED |
										  ISCSI_SCN_OBJECT_REMOVED |
										  ISCSI_SCN_TARGET_AND_SELF_INFO_ONLY)},

		{ 0, 0, 0}		// Terminating NULL entry
	};

	memcpy(ha->isns_scn_reg_attr_list,      isns_scn_reg_attr_list,      sizeof(isns_scn_reg_attr_list));
}

static void
qla4xxx_isns_init_isns_scn_dereg_attr_list(scsi_qla_host_t *ha)
{
	ATTRIBUTE_LIST isns_scn_dereg_attr_list[] = {
		// Source attribute
		{ ISNS_ATTR_TAG_ISCSI_NAME,        ISNS_ATTR_TYPE_STRING, (unsigned long) ha->name_string},
		// Key attributes
		{ ISNS_ATTR_TAG_ISCSI_NAME,        ISNS_ATTR_TYPE_STRING, (unsigned long) ha->name_string},

		{ 0, 0, 0}		// Terminating NULL entry
	};

	memcpy(ha->isns_scn_dereg_attr_list,    isns_scn_dereg_attr_list,    sizeof(isns_scn_dereg_attr_list));
}

static void
qla4xxx_isns_init_isns_dev_get_next_attr_list(scsi_qla_host_t *ha)
{
	ATTRIBUTE_LIST isns_dev_get_next_attr_list[] = {
		// Source attribute
		{ ISNS_ATTR_TAG_ISCSI_NAME,        ISNS_ATTR_TYPE_STRING, (unsigned long) ha->name_string},
		// Key attributes
		{ ISNS_ATTR_TAG_ISCSI_NAME,        ISNS_ATTR_TYPE_STRING, -1},
		// Required delimiter to indicate division between key and operating attrs.
		{ ISNS_ATTR_TAG_DELIMITER,         ISNS_ATTR_TYPE_EMPTY,  0},
		// Operating attributes (attributes of object matching key attribute to return)
		{ ISNS_ATTR_TAG_ISCSI_NODE_TYPE,   ISNS_ATTR_TYPE_ULONG,  cpu_to_be32(ISCSI_NODE_TYPE_TARGET)},

		{ 0, 0, 0}		// Terminating NULL entry
	};

	memcpy(ha->isns_dev_get_next_attr_list, isns_dev_get_next_attr_list, sizeof(isns_dev_get_next_attr_list));
}

static void
qla4xxx_isns_init_isns_dev_attr_qry_attr_list(scsi_qla_host_t *ha)
{
	ATTRIBUTE_LIST isns_dev_attr_qry_attr_list[] = {
		// Source attribute
		{ ISNS_ATTR_TAG_ISCSI_NAME,        ISNS_ATTR_TYPE_STRING, (unsigned long) ha->name_string},
		// Key attributes
		{ ISNS_ATTR_TAG_ISCSI_NAME,        ISNS_ATTR_TYPE_STRING, -1},
		// Required delimiter to indicate division between key and operating attrs.
		{ ISNS_ATTR_TAG_DELIMITER,         ISNS_ATTR_TYPE_EMPTY,  0},
		// Operating attributes (attributes of objects matching key attributes to return)
		{ ISNS_ATTR_TAG_ENTITY_PROTOCOL,   ISNS_ATTR_TYPE_EMPTY,  0},
		{ ISNS_ATTR_TAG_ISCSI_NAME,        ISNS_ATTR_TYPE_EMPTY,  0},
		{ ISNS_ATTR_TAG_ISCSI_NODE_TYPE,   ISNS_ATTR_TYPE_EMPTY,  0},
		{ ISNS_ATTR_TAG_ISCSI_ALIAS,       ISNS_ATTR_TYPE_EMPTY,  0},	// Friendly name
		{ ISNS_ATTR_TAG_PORTAL_SYMBOLIC_NAME, ISNS_ATTR_TYPE_EMPTY, 0},
		{ ISNS_ATTR_TAG_PORTAL_IP_ADDRESS, ISNS_ATTR_TYPE_EMPTY,  0},
		{ ISNS_ATTR_TAG_PORTAL_PORT,       ISNS_ATTR_TYPE_EMPTY,  0},
		{ ISNS_ATTR_TAG_PORTAL_SECURITY_BITMAP, ISNS_ATTR_TYPE_EMPTY, 0},
		{ ISNS_ATTR_TAG_DD_ID,             ISNS_ATTR_TYPE_EMPTY,  0},

		{ 0, 0, 0}		// Terminating NULL entry
	};

	memcpy(ha->isns_dev_attr_qry_attr_list, isns_dev_attr_qry_attr_list, sizeof(isns_dev_attr_qry_attr_list));
}

uint8_t
qla4xxx_isns_init_attributes (scsi_qla_host_t *ha)
{
	/* Separate these calls to minimize stack usage */

	qla4xxx_isns_init_isns_reg_attr_list(ha);
	qla4xxx_isns_init_isns_dereg_attr_list(ha);
	qla4xxx_isns_init_isns_scn_reg_attr_list(ha);
	qla4xxx_isns_init_isns_scn_dereg_attr_list(ha);
	qla4xxx_isns_init_isns_dev_get_next_attr_list(ha);
	qla4xxx_isns_init_isns_dev_attr_qry_attr_list(ha);

#if 0
	{
		ATTRIBUTE_LIST asRegUpdateAddObjectsAttrList[] = {
			// Source attribute
			{ ISNS_ATTR_TAG_ISCSI_NAME,        ISNS_ATTR_TYPE_STRING, (unsigned long) ha->name_string},
			// We are adding objects to an Entity so specify the Entity as the Key
			{ ISNS_ATTR_TAG_ENTITY_IDENTIFIER, ISNS_ATTR_TYPE_STRING, -1},	// FQDN
			{ ISNS_ATTR_TAG_DELIMITER,         ISNS_ATTR_TYPE_EMPTY,  0},
			// Operating attributes to register
			{ ISNS_ATTR_TAG_ISCSI_NAME,        ISNS_ATTR_TYPE_STRING, (unsigned long) ha->name_string},
			{ ISNS_ATTR_TAG_ISCSI_NODE_TYPE,   ISNS_ATTR_TYPE_ULONG,  cpu_to_be32(ISCSI_NODE_TYPE_INITIATOR)},
			{ ISNS_ATTR_TAG_ISCSI_ALIAS,       ISNS_ATTR_TYPE_STRING, -1},	    // Friendly machine name?

			{ 0, 0, 0}	// Terminating NULL entry
		};

		ATTRIBUTE_LIST asRegUpdateNodeAttrList[] = {
			// Source attribute
			{ ISNS_ATTR_TAG_ISCSI_NAME,        ISNS_ATTR_TYPE_STRING, (unsigned long) ha->name_string},
			// We updating attributes of a Node so specify the Node as the Key
			{ ISNS_ATTR_TAG_ISCSI_NAME,        ISNS_ATTR_TYPE_STRING, (unsigned long) ha->name_string},
			{ ISNS_ATTR_TAG_DELIMITER,         ISNS_ATTR_TYPE_EMPTY,  0},
			// Operating attributes to update
			{ ISNS_ATTR_TAG_ISCSI_NAME,        ISNS_ATTR_TYPE_STRING, (unsigned long) ha->name_string},
			{ ISNS_ATTR_TAG_ISCSI_ALIAS,       ISNS_ATTR_TYPE_STRING, -1},	    // Friendly machine name?

			{ 0, 0, 0}	// Terminating NULL entry
		};

		ATTRIBUTE_LIST asRegReplaceNodeAttrList[] = {
			// Source attribute
			{ ISNS_ATTR_TAG_ISCSI_NAME,        ISNS_ATTR_TYPE_STRING, (unsigned long) ha->name_string},
			// We updating attributes of a Node so specify the Node as the Key
			{ ISNS_ATTR_TAG_ISCSI_NAME,        ISNS_ATTR_TYPE_STRING, (unsigned long) ha->name_string},
			{ ISNS_ATTR_TAG_DELIMITER,         ISNS_ATTR_TYPE_EMPTY,  0},
			// Operating attributes to update
			{ ISNS_ATTR_TAG_ISCSI_NAME,        ISNS_ATTR_TYPE_STRING, (unsigned long) ha->name_string},
			{ ISNS_ATTR_TAG_ISCSI_NODE_TYPE,   ISNS_ATTR_TYPE_ULONG,  cpu_to_be32(ISCSI_NODE_TYPE_INITIATOR)},
			{ ISNS_ATTR_TAG_ISCSI_ALIAS,       ISNS_ATTR_TYPE_STRING, -1},	    // Friendly machine name?

			{ 0, 0, 0}	// Terminating NULL entry
		};

		ATTRIBUTE_LIST asRegUpdateEntityAttrList[] = {
			// Source attribute
			{ ISNS_ATTR_TAG_ISCSI_NAME,        ISNS_ATTR_TYPE_STRING,  (unsigned long) ha->name_string},
			// We updating attributes of an Entity so specify the Entity as the Key
			{ ISNS_ATTR_TAG_ENTITY_IDENTIFIER, ISNS_ATTR_TYPE_STRING,  -1},	 // FQDN
			{ ISNS_ATTR_TAG_DELIMITER,         ISNS_ATTR_TYPE_EMPTY,   0},
			// Operating attributes to update
			{ ISNS_ATTR_TAG_ENTITY_IDENTIFIER, ISNS_ATTR_TYPE_STRING,  -1},	 // FQDN
			{ ISNS_ATTR_TAG_MGMT_IP_ADDRESS,   ISNS_ATTR_TYPE_ADDRESS, -1},

			{ 0, 0, 0}	// Terminating NULL entry
		};

		memcpy(ha->asRegUpdateAddObjectsAttrList, asRegUpdateAddObjectsAttrList, sizeof(asRegUpdateAddObjectsAttrList));
		memcpy(ha->asRegUpdateNodeAttrList,       asRegUpdateNodeAttrList,       sizeof(asRegUpdateNodeAttrList));
		memcpy(ha->asRegReplaceNodeAttrList,      asRegReplaceNodeAttrList,      sizeof(asRegReplaceNodeAttrList));
		memcpy(ha->asRegUpdateEntityAttrList,     asRegUpdateEntityAttrList,     sizeof(asRegUpdateEntityAttrList));
	}
#endif

	return(QLA_SUCCESS);
}

uint8_t
qla4xxx_isns_append_attribute(scsi_qla_host_t *ha,
			      uint8_t **buffer,
			      uint8_t *buffer_end,
			      ATTRIBUTE_LIST *attribute)
{

	ISNS_ATTRIBUTE *isns_attr;
	uint32_t data_len;
	uint8_t *local;

	isns_attr =  (ISNS_ATTRIBUTE *) *buffer;

	switch (attribute->type) {
	case ISNS_ATTR_TYPE_EMPTY:
		data_len = 0;
		if ((&isns_attr->value[0] + data_len) > buffer_end) {
			return(QLA_ERROR);
		}
		isns_attr->tag = cpu_to_be32(attribute->isns_tag);
		isns_attr->length = cpu_to_be32(data_len);
		break;

	case ISNS_ATTR_TYPE_STRING:
		/*
		 * Length must include NULL terminator.
		 * Note also that all iSNS strings must be UTF-8 encoded.
		 * You should encode your strings for UTF-8 before registering
		 * them with the iSNS server.
		 */
		data_len = strlen ((uint8_t *) attribute->data) + sizeof(uint8_t);
		if (data_len % 4) {
			data_len += (4 - (data_len % 4)); // Pad to 4 byte boundary.
		}

		if ((&isns_attr->value[0] + data_len) > buffer_end) {
			return(QLA_ERROR);
		}
		isns_attr->tag = cpu_to_be32(attribute->isns_tag);
		isns_attr->length = cpu_to_be32(data_len);
		memset(isns_attr->value, 0, data_len);
		strcpy (&isns_attr->value[0], (uint8_t *) attribute->data);
		break;

	case ISNS_ATTR_TYPE_ULONG:
		data_len = sizeof(uint32_t);
		if ((isns_attr->value + data_len) > buffer_end) {
			return(QLA_ERROR);
		}
		isns_attr->tag = cpu_to_be32(attribute->isns_tag);
		isns_attr->length = cpu_to_be32(data_len);
		*(uint32_t *) isns_attr->value = (uint32_t) attribute->data;
		break;

	case ISNS_ATTR_TYPE_ADDRESS:
		local = (uint8_t *) attribute->data;
		data_len = 16;	     // Size of an IPv6 address
		if ((isns_attr->value + data_len) > buffer_end) {
			return(QLA_ERROR);
		}
		isns_attr->tag = cpu_to_be32(attribute->isns_tag);
		isns_attr->length = cpu_to_be32(data_len);
		// Prepend IP Address with 0xFFFF to indicate this is an IPv4
		// only address. IPv6 addresses not supported by driver.
		memset(isns_attr->value, 0, 16);
		isns_attr->value[10] = 0xFF;
		isns_attr->value[11] = 0xFF;
		isns_attr->value[12] = local[0];
		isns_attr->value[13] = local[1];
		isns_attr->value[14] = local[2];
		isns_attr->value[15] = local[3];
		break;

	default:
		return(QLA_ERROR);

	}

	*buffer = &isns_attr->value[0] + data_len;

	return(QLA_SUCCESS);
}


uint32_t
qla4xxx_isns_build_iocb_handle(scsi_qla_host_t *ha,
			       uint32_t type,
			       PDU_ENTRY *pdu_entry)
{
	uint32_t handle;

	handle = (IOCB_ISNS_PT_PDU_TYPE(type) |
		  (((uint8_t *)pdu_entry - (uint8_t *)ha->pdu_queue)
		   / sizeof(PDU_ENTRY)));

	QL4PRINT(QLP20, printk("scsi%d: %s: type %x PDU %p = handle %x\n",
			       ha->host_no, __func__,
			       type, pdu_entry, handle));
	return(handle);
}

/*
 * Remarks:
 *      hardware_lock locked upon entry
 */
uint8_t
qla4xxx_isns_get_server_request(scsi_qla_host_t *ha,
				uint32_t pdu_buff_len,
				uint16_t connection_id)
{
	PDU_ENTRY *pdu_entry;

	pdu_entry = qla4xxx_get_pdu(ha, MAX(pdu_buff_len, PAGE_SIZE));
	if (pdu_entry == NULL) {
		QL4PRINT(QLP2, printk("scsi%d: %s: get_pdu failed\n",
				      ha->host_no, __func__));
		return(QLA_ERROR);
	}

	pdu_entry->SendBuffLen = 0;
	pdu_entry->RecvBuffLen = pdu_entry->BuffLen;

	QL4PRINT(QLP19, printk("PDU (0x%p) 0x%x ->\n", pdu_entry->Buff, pdu_entry->SendBuffLen));
	qla4xxx_dump_bytes(QLP19, pdu_entry->Buff, pdu_entry->SendBuffLen);

	if (qla4xxx_send_passthru0_iocb(ha, ISNS_DEVICE_INDEX, connection_id,
					pdu_entry->DmaBuff,
					pdu_entry->SendBuffLen,
					pdu_entry->RecvBuffLen,
					PT_FLAG_ISNS_PDU | PT_FLAG_WAIT_4_RESPONSE,
					qla4xxx_isns_build_iocb_handle(ha, /*ISNS_REQ_RSP_PDU*/ISNS_ASYNCH_REQ_PDU, pdu_entry))
	    != QLA_SUCCESS) {
		QL4PRINT(QLP2, printk("scsi%d: %s: send_passthru_iocb failed\n",
				      ha->host_no, __func__));
		qla4xxx_free_pdu(ha, pdu_entry);
		return(QLA_ERROR);
	}

	return(QLA_SUCCESS);
}

uint8_t
qla4xxx_isns_build_scn_registration_packet(scsi_qla_host_t *ha,
					   uint8_t *buffer,
					   uint32_t buffer_size,
					   uint32_t *packet_size)
{
	/*
	 * Fill in all of the run time requested data in the attribute array
	 * then call iSNSBuildRequestPacket to do the actual work.
	 */

	return(qla4xxx_isns_build_request_packet(ha, buffer, buffer_size,
						 ISNS_FCID_SCNReg,
						 ha->isns_transaction_id,
						 0,
						 ha->isns_scn_reg_attr_list,
						 packet_size));
}


uint8_t
qla4xxx_isns_build_scn_deregistration_packet(scsi_qla_host_t *ha,
					     uint8_t *buffer,
					     uint32_t buffer_size,
					     uint32_t *packet_size)
{
	/*
	 * Fill in all of the run time requested data in the attribute array
	 * then call iSNSBuildRequestPacket to do the actual work.
	 */

	return(qla4xxx_isns_build_request_packet(ha, buffer, buffer_size,
						 ISNS_FCID_SCNDereg,
						 ha->isns_transaction_id,
						 0,
						 ha->isns_scn_dereg_attr_list,
						 packet_size));
}

uint8_t
qla4xxx_isns_build_registration_packet(scsi_qla_host_t *ha,
				       uint8_t *buff,
				       uint32_t buff_size,
				       uint8_t *isns_entity_id,
				       uint8_t *ip_addr,
				       uint32_t port_number,
				       uint32_t scn_port,
				       uint32_t esi_port,
				       uint8_t *local_alias,
				       uint32_t *packet_size)
{
	/*
	 * Fill in all of the run time requested data in the attribute array,
	 * then call build_request_packet to do the actual work.
	 */
	ha->isns_reg_attr_list[1].data = (unsigned long) isns_entity_id;
	ha->isns_reg_attr_list[3].data = (unsigned long) isns_entity_id;
	ha->isns_reg_attr_list[5].data = (unsigned long) ip_addr;
	ha->isns_reg_attr_list[6].data = cpu_to_be32(port_number);
	ha->isns_reg_attr_list[7].data = cpu_to_be32(scn_port);
	ha->isns_reg_attr_list[8].data = cpu_to_be32(esi_port);
	if (local_alias && local_alias[0]) {
		ha->isns_reg_attr_list[11].data = (unsigned long) local_alias;
	}
	else {
		ha->isns_reg_attr_list[11].data = (unsigned long) "<No alias specified>";
	}

	return(qla4xxx_isns_build_request_packet(ha, buff, buff_size,
						 ISNS_FCID_DevAttrReg,
						 ha->isns_transaction_id,
						 0,
						 ha->isns_reg_attr_list,
						 packet_size));
}

uint8_t
qla4xxx_isns_build_deregistration_packet(scsi_qla_host_t *ha,
					 uint8_t *buff,
					 uint32_t buff_size,
					 uint8_t *isns_entity_id,
					 uint8_t *ip_addr,
					 uint32_t port_number,
					 uint32_t *packet_size)
{
	/*
	 * Fill in all of the run time requested data in the attribute array,
	 * then call build_request_packet to do the actual work.
	 */
	ha->isns_dereg_attr_list[2].data = (unsigned long) isns_entity_id;
	#if 0
	ha->isns_dereg_attr_list[3].data = (unsigned long) ip_addr;
	ha->isns_dereg_attr_list[4].data = (unsigned long) cpu_to_be32(port_number);
	#endif

	return(qla4xxx_isns_build_request_packet(ha, buff, buff_size,
						 ISNS_FCID_DevDereg,
						 ha->isns_transaction_id,
						 0,
						 ha->isns_dereg_attr_list,
						 packet_size));
}

uint8_t
qla4xxx_isns_build_request_packet(scsi_qla_host_t *ha,
				  uint8_t *buffer,
				  uint32_t buffer_size,
				  uint16_t function_id,
				  uint16_t tx_id,
				  uint8_t  use_replace_flag,
				  ATTRIBUTE_LIST *attr_list,
				  uint32_t *packet_size)
{
	ISNSP_MESSAGE_HEADER *isns_message;
	uint8_t  *ptr;
	uint8_t  *buffer_end;
	uint8_t  *payload_start;
	uint32_t i;
	uint8_t  success;

	/*
	 * Ensure that the buffer size is at a minimum sufficient to hold the
	 * message header plus at least one attribute.
	 */
	if (buffer_size < (sizeof(*isns_message) + sizeof(*attr_list))) {
		QL4PRINT(QLP12, printk("scsi%d: %s: Insufficient buffer size "
				       "%d, need %d\n",
				       ha->host_no, __func__, buffer_size,
				       (unsigned int) (sizeof(*isns_message) +
						       sizeof(*attr_list))));

		return(QLA_ERROR);
	}

	isns_message = (ISNSP_MESSAGE_HEADER *) buffer;
	buffer_end = (uint8_t *) ((unsigned long) buffer + buffer_size);

	/* Initialize message header contents */
	isns_message->isnsp_version = cpu_to_be16(ISNSP_VERSION);
	isns_message->function_id   = cpu_to_be16(function_id);
	if (use_replace_flag) {
		isns_message->flags = cpu_to_be16(ISNSP_CLIENT_SENDER |
                                                  ISNSP_FIRST_PDU |
						  ISNSP_LAST_PDU |
						  ISNSP_REPLACE_FLAG);
	}
	else {
		isns_message->flags = cpu_to_be16(ISNSP_CLIENT_SENDER |
					    ISNSP_FIRST_PDU |
					    ISNSP_LAST_PDU);
	}

	isns_message->transaction_id = cpu_to_be16(tx_id);
	isns_message->sequence_id    = 0; // First and only packet in this message

	ptr = payload_start = &isns_message->payload[0];

	/*
	 * Now that most of the message header has been initialized (we'll fill
	 * in the size when we're finished), let's append the desired attributes
	 * to the request packet.
	 */
	success = 1;
	for (i = 0; attr_list[i].type && success; i++) {
		success = (qla4xxx_isns_append_attribute (ha, &ptr, buffer_end,
							  &attr_list[i])
			   == QLA_SUCCESS);
	}

	if (!success) {
		QL4PRINT(QLP2, printk("scsi%d: %s: Ran out of buffer space\n",
				      ha->host_no, __func__));
		return(QLA_ERROR);
	}

	/*
	 * We've successfully finished building the request packet.
	 * Set the size field.
	 */
	isns_message->pdu_length = cpu_to_be16((unsigned long) ptr -
                                               (unsigned long) payload_start);

	*packet_size = (uint32_t) ((unsigned long) ptr -
				   (unsigned long) buffer);

	return(QLA_SUCCESS);
}

uint8_t
qla4xxx_isns_build_server_request_response_packet(scsi_qla_host_t *ha,
						  uint8_t * buffer,
						  uint32_t buffer_size,
						  uint16_t function_id,	 //cpu
						  uint32_t error_code,	 //cpu
						  uint16_t transaction_id, //cpu
						  uint32_t *packet_size)
{
	ISNSP_MESSAGE_HEADER * isns_message;
	ISNSP_RESPONSE_HEADER * isns_response;
	uint8_t *ptr;
	uint8_t *buffer_end;
	uint8_t *payload_start;

	// Ensure that the buffer size is at a minimum sufficient to hold the
	// message headers.

	if (buffer_size < (sizeof(ISNSP_MESSAGE_HEADER) + sizeof(ISNSP_RESPONSE_HEADER))) {
		QL4PRINT(QLP2, printk("scsi%d: %s: Insufficient buffer size %x\n",
				      ha->host_no, __func__, buffer_size));
		return(QLA_ERROR);
	}

	isns_message = (ISNSP_MESSAGE_HEADER *) buffer;
	isns_response = (ISNSP_RESPONSE_HEADER *) &isns_message->payload[0];
	payload_start = ( uint8_t *) isns_response;
	buffer_end = ( uint8_t *) (buffer + buffer_size);

	// Initialize message header contents.

	isns_message->isnsp_version = cpu_to_be16(ISNSP_VERSION);
	isns_message->function_id = (function_id);
	//isns_message->function_id = cpu_to_be16(function_id);
	isns_message->flags = cpu_to_be16(ISNSP_CLIENT_SENDER |
				    ISNSP_FIRST_PDU |
				    ISNSP_LAST_PDU);
	isns_message->transaction_id =(transaction_id);
	//isns_message->transaction_id = cpu_to_be16(transaction_id);
	isns_message->sequence_id = 0;	 // First and only packet in this message

	isns_response->error_code = cpu_to_be32(error_code);

	ptr = &isns_response->attributes[0];

	// We've successfully finished building the request packet.
	// Set the size field.

	//QLASSERT (!((ptr - payload_start) % 4));

	isns_message->pdu_length = cpu_to_be16((unsigned long) ptr -
                                               (unsigned long) payload_start);

	*packet_size = (unsigned long) ptr - (unsigned long) buffer;

	return(QLA_SUCCESS);
}

uint8_t
qla4xxx_isns_build_dev_get_next_packet (scsi_qla_host_t *ha,
					uint8_t * buffer,
					uint32_t buffer_size,
					uint8_t * last_iscsi_name,
					uint32_t *packet_size)
{
	// Fill in all of the run time requested data in the attribute array
	// then call qla4xxx_isns_build_request_packet to do the actual work.

	if (last_iscsi_name && last_iscsi_name[0]) {
		ha->isns_dev_get_next_attr_list[1].type = ISNS_ATTR_TYPE_STRING;
		ha->isns_dev_get_next_attr_list[1].data = (unsigned long) last_iscsi_name;
	}
	else {
		ha->isns_dev_get_next_attr_list[1].type = ISNS_ATTR_TYPE_EMPTY;
		ha->isns_dev_get_next_attr_list[1].data = 0;
	}

	return(qla4xxx_isns_build_request_packet(ha, buffer, buffer_size,
						 ISNS_FCID_DevGetNext,
						 ha->isns_transaction_id,
						 0,
						 ha->isns_dev_get_next_attr_list,
						 packet_size));
}

uint8_t
qla4xxx_isns_build_dev_attr_qry_packet (scsi_qla_host_t *ha,
					uint8_t *buffer,
					uint32_t buffer_size,
					uint8_t *object_iscsi_name,
					uint32_t *packet_size)
{
	// Fill in all of the run time requested data in the attribute array
	// then call qla4xxx_isns_build_request_packet to do the actual work.

	ha->isns_dev_attr_qry_attr_list[1].data = (unsigned long) object_iscsi_name;

	return(qla4xxx_isns_build_request_packet(ha, buffer, buffer_size,
						 ISNS_FCID_DevAttrQry,
						 ha->isns_transaction_id, 0,
						 ha->isns_dev_attr_qry_attr_list,
						 packet_size));
}

uint8_t
qla4xxx_isns_parse_get_next_response(scsi_qla_host_t *ha,
				     uint8_t *buffer,
				     uint32_t buffer_size,
				     uint32_t *isns_error, // cpu, w.r.t. PPC byte order
				     uint8_t *last_iscsi_name,
				     uint32_t last_iscsi_name_size,
				     uint8_t *IsTarget)
{
	ISNSP_MESSAGE_HEADER *isns_message;
	ISNSP_RESPONSE_HEADER *isns_response;
	ISNS_ATTRIBUTE *isns_attr;
	uint8_t *buffer_end;

	*IsTarget = 0;

	isns_message = (ISNSP_MESSAGE_HEADER *) buffer;
	buffer_end = ( uint8_t *) (( uint8_t *) &isns_message->payload[0] +
				   be16_to_cpu(isns_message->pdu_length));

	// Validate pdu_length specified in the iSNS message header.

	if (((unsigned long) buffer_end -
	     (unsigned long) buffer) > buffer_size) {
		QL4PRINT(QLP2, printk("scsi%d: %s: Invalid length field in "
				      "iSNS response from iSNS server\n",
				      ha->host_no, __func__));
		return(QLA_ERROR);
	}

	// It is safe to assume from this point on that the pdu_length value
	// (and thus our idea about the end of the buffer) is valid.

	// Ensure that we have the correct function_id.

	if (be16_to_cpu(isns_message->function_id) != ISNS_FCID_DevGetNextRsp) {
		QL4PRINT(QLP2, printk("scsi%d: %s: Invalid Function ID (0x%04x) "
				      "in iSNS response from iSNS server\n",
				      ha->host_no, __func__,
				      be16_to_cpu(isns_message->function_id)));
		return(QLA_ERROR);
	}

	isns_response = (ISNSP_RESPONSE_HEADER *) &isns_message->payload[0];

	*isns_error = be32_to_cpu(isns_response->error_code);
	if (*isns_error) {
		QL4PRINT(QLP2, printk("scsi%d: %s: iSNS Error code: %d\n",
				      ha->host_no, __func__, *isns_error));

		if (*isns_error == ISNS_ERR_NO_SUCH_ENTRY) {
			QL4PRINT(QLP2, printk("scsi%d: %s: No more targets.\n",
					      ha->host_no, __func__));
			set_bit(ISNS_FLAG_DEV_SCAN_DONE, &ha->isns_flags);
		}
		else {
			QL4PRINT(QLP2, printk("scsi%d: %s: Get Next failed. Error code %x\n",
					      ha->host_no, __func__, *isns_error));
		}
		return(QLA_ERROR);
	}

	isns_attr = (ISNS_ATTRIBUTE *) &isns_response->attributes[0];

	// Save the returned key attribute for the next DevGetNext request.

	if (VALIDATE_ATTR(isns_attr, buffer_end) &&
	    be32_to_cpu(isns_attr->tag) == ISNS_ATTR_TAG_ISCSI_NAME) {
		strncpy(last_iscsi_name, &isns_attr->value[0], last_iscsi_name_size);
	}
	else {
		QL4PRINT(QLP2, printk("scsi%d: %s: Bad Key attribute in DevGetNextRsp\n",
				      ha->host_no, __func__));
		return(QLA_ERROR);
	}

	// Point to next attribute.

	isns_attr = NEXT_ATTR(isns_attr);

	if (VALIDATE_ATTR(isns_attr, buffer_end) &&
	    be32_to_cpu(isns_attr->tag) == ISNS_ATTR_TAG_DELIMITER) {
		;	// Do nothing.
	}
	else {
		QL4PRINT(QLP2, printk("scsi%d: %s: No delimiter in DevGetNextRsp\n",
				      ha->host_no, __func__));
		return(QLA_ERROR);
	}

	*IsTarget = 1;

	// Point to next attribute.

	isns_attr = NEXT_ATTR(isns_attr);

	if (VALIDATE_ATTR(isns_attr, buffer_end) &&
	    be32_to_cpu(isns_attr->tag) == ISNS_ATTR_TAG_ISCSI_NODE_TYPE) {
		if (be32_to_cpu(*(uint32_t *) &isns_attr->value[0]) & ISCSI_NODE_TYPE_TARGET) {
			*IsTarget = 1;
		}
	}
	#if 0
	else {
		QL4PRINT(QLP2, printk("scsi%d: %s: Bad operating attr in DevGetNextRsp (%d)\n",
				      ha->host_no, __func__, be16_to_cpu(isns_attr->tag)));
		return(QLA_ERROR);
	}
	#endif

	return(QLA_SUCCESS);
}

uint8_t
qla4xxx_isns_parse_query_response (scsi_qla_host_t *ha,
				   uint8_t *buffer,
				   uint32_t buffer_size,
				   uint32_t *isns_error,    // cpu
				   ISNS_DISCOVERED_TARGET *isns_discovered_target,
				   uint8_t *IsTarget,
				   uint8_t *last_iscsi_name)
{
	ISNSP_MESSAGE_HEADER *isns_message;
	ISNSP_RESPONSE_HEADER *isns_response;
	ISNS_ATTRIBUTE *isns_attr;
	uint8_t *buffer_end;
	uint8_t *tmpptr;
	uint16_t wTmp;
	uint32_t ulTmp;
	uint32_t i;

	isns_message = (ISNSP_MESSAGE_HEADER *) buffer;
	buffer_end = &isns_message->payload[0] +
		be16_to_cpu(isns_message->pdu_length);

	// Validate pdu_length specified in the iSNS message header.

	if (((unsigned long) buffer_end -
	     (unsigned long) buffer) > buffer_size) {
		QL4PRINT(QLP2, printk("scsi%d: %s: Invalid length field in "
				      "iSNS response from iSNS server\n",
				      ha->host_no, __func__));
		return(QLA_ERROR);
	}

	// It is safe to assume from this point on that the pdu_length value
	// (and thus our idea about the end of the buffer) is valid.

	// Ensure that we have the correct function_id.

	if (be16_to_cpu(isns_message->function_id) != ISNS_FCID_DevAttrQryRsp) {
		QL4PRINT(QLP2, printk("scsi%d: %s: Invalid Function ID %04x in iSNS response\n",
				      ha->host_no, __func__,
				      be16_to_cpu(isns_message->function_id)));
		return(QLA_ERROR);
	}

	isns_response = (ISNSP_RESPONSE_HEADER *) &isns_message->payload[0];

	QL4PRINT(QLP20, printk("-----------------------------\n"));
	QL4PRINT(QLP20, printk("scsi%d: %s: DevAttrQry response from iSNS server:\n",
			       ha->host_no, __func__));

	*isns_error = be32_to_cpu(isns_response->error_code);
	if (*isns_error) {
		QL4PRINT(QLP2, printk("scsi%d: %s: iSNS Query failed.  error_code %x.\n",
				      ha->host_no, __func__, *isns_error));
		return(QLA_ERROR);
	}

	QL4PRINT(QLP20, printk("scsi%d: %s: Attributes:\n", ha->host_no, __func__));

	isns_attr = (ISNS_ATTRIBUTE *) &isns_response->attributes[0];

	// Skip key and delimiter attributes.

	while (VALIDATE_ATTR(isns_attr, buffer_end) &&
	       be32_to_cpu(isns_attr->tag) != ISNS_ATTR_TAG_DELIMITER) {
		// Point to next attribute.
		if (be32_to_cpu(isns_attr->tag) == ISNS_ATTR_TAG_ISCSI_NAME) {
			// Note that this string is in UTF-8 format.  In production code,
			// it would be necessary to convert from UTF-8 before using the
			// string.
			QL4PRINT(QLP20, printk("scsi%d: %s: MsgTag iSCSI Name: \"%s\"\n",
					       ha->host_no, __func__, &isns_attr->value[0]));
			if (strlen (isns_attr->value) > 256)
				return(QLA_ERROR);
			strcpy (last_iscsi_name, (uint8_t *) &isns_attr->value[0]);
		}
		isns_attr = NEXT_ATTR(isns_attr);
	}

	if (!VALIDATE_ATTR(isns_attr, buffer_end) ||
	    be32_to_cpu(isns_attr->tag) != ISNS_ATTR_TAG_DELIMITER) {
		// There was no delimiter attribute in the response.
		return(QLA_ERROR);
	}

	// Skip delimiter attribute.
	isns_attr = NEXT_ATTR(isns_attr);

	while (VALIDATE_ATTR(isns_attr, buffer_end)) {
		// We only need to parse for the operating attributes that we
		// requested in the DevAttrQuery.

		switch (be32_to_cpu(isns_attr->tag)) {
		case ISNS_ATTR_TAG_ENTITY_PROTOCOL:
			if (be32_to_cpu(*(uint32_t *) isns_attr->value) != ENTITY_PROTOCOL_ISCSI) {
				QL4PRINT(QLP2, printk("scsi%d: %s: Entity does not support iSCSI protocol\n", ha->host_no, __func__));
			}
			break;

		case ISNS_ATTR_TAG_ISCSI_NODE_TYPE:
			switch (be32_to_cpu(*(uint32_t *) isns_attr->value)) {
			case ISCSI_NODE_TYPE_TARGET:
				QL4PRINT(QLP20, printk("scsi%d: %s: iSCSI node type Target\n", ha->host_no, __func__));
				*IsTarget = 1;
				break;
			case ISCSI_NODE_TYPE_INITIATOR:
				QL4PRINT(QLP20, printk("scsi%d: %s: iSCSI node type Initiator\n", ha->host_no, __func__));
				*IsTarget = 0;
				break;
			case ISCSI_NODE_TYPE_CONTROL:
				QL4PRINT(QLP20, printk("scsi%d: %s: iSCSI node type Control\n", ha->host_no, __func__));
				*IsTarget = 0;
				break;
			default:
				QL4PRINT(QLP20, printk("scsi%d: %s: iSCSI node type unknown\n", ha->host_no, __func__));
				*IsTarget = 0;
				break;
			}
			break;

		case ISNS_ATTR_TAG_MGMT_IP_ADDRESS:
			// WARNING: This doesn't handle IPv6 addresses.
			tmpptr = &isns_attr->value[0];
			for (i = 0; i < 8; i++) {
				if (tmpptr[i])
					return(QLA_ERROR);
			}

			for (i = 8; i < 12; i++) {
				if (tmpptr[i] != 0 && tmpptr[i] != 0xFF)
					return(QLA_ERROR);
			}

			QL4PRINT(QLP20, printk("scsi%d: %s: Management IP address: %u.%u.%u.%u\n",
					       ha->host_no, __func__, tmpptr[12],
					       tmpptr[13], tmpptr[14], tmpptr[15]));
			break;

		case ISNS_ATTR_TAG_PORTAL_IP_ADDRESS:
			// WARNING: This doesn't handle IPv6 addresses.
			tmpptr = &isns_attr->value[0];
			for (i = 0; i < 8; i++) {
				if (tmpptr[i])
					return(QLA_ERROR);
			}

			for (i = 8; i < 12; i++) {
				if (tmpptr[i] != 0 && tmpptr[i] != 0xFF)
					return(QLA_ERROR);
			}

			QL4PRINT(QLP20, printk("scsi%d: %s: Portal IP address: %u.%u.%u.%u\n",
					       ha->host_no, __func__, tmpptr[12],
					       tmpptr[13], tmpptr[14], tmpptr[15]));

			if (isns_discovered_target->NumPortals >= ISNS_MAX_PORTALS)
				break;
			memcpy(isns_discovered_target->Portal[isns_discovered_target->NumPortals].IPAddr,
			       &tmpptr[12], 4);
			break;

		case ISNS_ATTR_TAG_PORTAL_PORT:
			wTmp = (uint16_t) (be32_to_cpu(*(uint32_t *) isns_attr->value));
			QL4PRINT(QLP20, printk("scsi%d: %s: Portal port: %u\n",
					       ha->host_no, __func__, be32_to_cpu(*(uint32_t *) isns_attr->value)));
			if (isns_discovered_target->NumPortals >= ISNS_MAX_PORTALS)
				break;
			isns_discovered_target->Portal[isns_discovered_target->NumPortals].PortNumber = wTmp;
			isns_discovered_target->NumPortals++;
			break;

		case ISNS_ATTR_TAG_PORTAL_SYMBOLIC_NAME:
			// Note that this string is in UTF-8 format.  In production code,
			// it would be necessary to convert from UTF-8 before using the
			// string.
			QL4PRINT(QLP20, printk("scsi%d: %s: Portal Symbolic Name: \"%s\"\n",
					       ha->host_no, __func__, &isns_attr->value[0]));
#if 0
			if (isns_discovered_target->NumPortals >= ISNS_MAX_PORTALS)
				break;
			qlstrncpy(isns_discovered_target->Portal[isns_discovered_target->NumPortals].SymbolicName,
				  (uint8_t *) isns_attr->value, 32);
			isns_discovered_target->Portal[isns_discovered_target->NumPortals].SymbolicName[31] = 0;
#endif
			break;

		case ISNS_ATTR_TAG_SCN_PORT:
			QL4PRINT(QLP20, printk("scsi%d: %s: SCN port: %u\n",
					       ha->host_no, __func__,
					       be32_to_cpu(*(uint32_t *) isns_attr->value)));
			break;

		case ISNS_ATTR_TAG_ESI_PORT:
			QL4PRINT(QLP20, printk("scsi%d: %s: ESI port: %u\n",
					       ha->host_no, __func__,
					       be32_to_cpu(*(uint32_t *) isns_attr->value)));
			break;

		case ISNS_ATTR_TAG_ESI_INTERVAL:
			QL4PRINT(QLP20, printk("scsi%d: %s: ESI Interval: %u\n",
					       ha->host_no, __func__,
					       be32_to_cpu(*(uint32_t *) isns_attr->value)));
			break;

		case ISNS_ATTR_TAG_REGISTRATION_PERIOD:
			QL4PRINT(QLP20, printk("scsi%d: %s: Entity Registration Period: %u\n",
					       ha->host_no, __func__,
					       be32_to_cpu(*(uint32_t *) isns_attr->value)));
			break;

		case ISNS_ATTR_TAG_PORTAL_SECURITY_BITMAP:
			ulTmp = be32_to_cpu(*(uint32_t *) isns_attr->value);

			QL4PRINT(QLP20, printk("scsi%d: %s: Portal Security Bitmap:\n", ha->host_no, __func__));
			if (ulTmp & ISNS_SECURITY_BITMAP_VALID) {
				QL4PRINT(QLP20, printk("scsi%d: %s:\tISNS_SECURITY_BITMAP_VALID\n", ha->host_no, __func__));
			}
			if (ulTmp & ISNS_SECURITY_IKE_IPSEC_ENABLED) {
				QL4PRINT(QLP20, printk("scsi%d: %s:\tISNS_SECURITY_IKE_IPSEC_ENABLED\n", ha->host_no, __func__));
			}
			if (ulTmp & ISNS_SECURITY_MAIN_MODE_ENABLED) {
				QL4PRINT(QLP20, printk("scsi%d: %s:\tISNS_SECURITY_MAIN_MODE_ENABLED\n", ha->host_no, __func__));
			}
			if (ulTmp & ISNS_SECURITY_AGGRESSIVE_MODE_ENABLED) {
				QL4PRINT(QLP20, printk("scsi%d: %s:\tISNS_SECURITY_AGGRESSIVE_MODE_ENABLED\n", ha->host_no, __func__));
			}
			if (ulTmp & ISNS_SECURITY_PFS_ENABLED) {
				QL4PRINT(QLP20, printk("scsi%d: %s:\tISNS_SECURITY_PFS_ENABLED\n", ha->host_no, __func__));
			}
			if (ulTmp & ISNS_SECURITY_TRANSPORT_MODE_PREFERRED) {
				QL4PRINT(QLP20, printk("scsi%d: %s:\tISNS_SECURITY_TRANSPORT_MODE_PREFERRED\n", ha->host_no, __func__));
			}
			if (ulTmp & ISNS_SECURITY_TUNNEL_MODE_PREFERRED) {
				QL4PRINT(QLP20, printk("scsi%d: %s:\tISNS_SECURITY_TUNNEL_MODE_PREFERRED\n", ha->host_no, __func__));
			}
			// isns_discovered_target->SecurityBitmap = ulTmp;
			break;

		case ISNS_ATTR_TAG_ENTITY_IDENTIFIER:
			// Note that this string is in UTF-8 format.  In production code,
			// it would be necessary to convert from UTF-8 before using the
			// string.
			QL4PRINT(QLP20, printk("scsi%d: %s: Entity Identifier: \"%s\"\n",
					       ha->host_no, __func__, isns_attr->value));
			break;

		case ISNS_ATTR_TAG_ISCSI_NAME:
			// Note that this string is in UTF-8 format.  In production code,
			// it would be necessary to convert from UTF-8 before using the
			// string.
			QL4PRINT(QLP20, printk("scsi%d: %s: iSCSI Name: \"%s\"\n",
					       ha->host_no, __func__, isns_attr->value));
			if (strlen (isns_attr->value) > 256)
				return(QLA_ERROR);
			strcpy (isns_discovered_target->NameString, ( uint8_t *) isns_attr->value);
			break;

		case ISNS_ATTR_TAG_ISCSI_ALIAS:
			// Note that this string is in UTF-8 format.  In production code,
			// it would be necessary to convert from UTF-8 before using the
			// string.
			QL4PRINT(QLP20, printk("scsi%d: %s: Alias: \"%s\"\n",
					       ha->host_no, __func__, isns_attr->value));
			if (strlen (isns_attr->value) <= 32)
				strcpy (isns_discovered_target->Alias, ( uint8_t *) isns_attr->value);
			break;

		case ISNS_ATTR_TAG_DD_ID:
			ulTmp = be32_to_cpu(*(uint32_t *) isns_attr->value);
			QL4PRINT(QLP20, printk("scsi%d: %s: DD ID: %u\n",
					       ha->host_no, __func__,
					       be32_to_cpu(*(uint32_t *) isns_attr->value)));
			isns_discovered_target->DDID = ulTmp;
			break;

		default:
			//QLASSERT (0);
			break;
		}

		// Point to next attribute.

		isns_attr = NEXT_ATTR(isns_attr);
	}

	return(QLA_SUCCESS);
}

uint8_t
qla4xxx_isns_process_response(scsi_qla_host_t *ha, PASSTHRU_STATUS_ENTRY *sts_entry)
{
	uint32_t handle = le32_to_cpu(sts_entry->handle);
	uint32_t inResidual = le32_to_cpu(sts_entry->inResidual);
	uint16_t connectionID = le16_to_cpu(sts_entry->connectionID);
	PDU_ENTRY *pdu_entry = (PDU_ENTRY *) &ha->pdu_queue[IOCB_ISNS_PT_PDU_INDEX(handle)];
	uint32_t pdu_type = IOCB_ISNS_PT_PDU_TYPE(handle);
	uint8_t status = QLA_SUCCESS;

	ENTER("qla4xxx_passthru_status_entry");

	QL4PRINT(QLP20,
		 printk("scsi%d: %s isns_flags 0x%lx to=0x%x "
			"IOCS=0x%02x OutResidual/Len=0x%x/0x%x "
			"InResidual/Len=0x%x/0x%x\n",
			ha->host_no, __func__,
			ha->isns_flags,
			le16_to_cpu(sts_entry->timeout),
			sts_entry->completionStatus,
			le32_to_cpu(sts_entry->outResidual),
			pdu_entry->SendBuffLen,
			inResidual,
			pdu_entry->RecvBuffLen));

	if (pdu_entry->RecvBuffLen - inResidual) {
		QL4PRINT(QLP19, printk("PDU (0x%p) <-\n", pdu_entry->Buff));
		qla4xxx_dump_bytes(QLP19, pdu_entry->Buff, (pdu_entry->RecvBuffLen - inResidual));
	}


	if (sts_entry->completionStatus != PASSTHRU_STATUS_COMPLETE) {

		qla4xxx_free_pdu(ha, pdu_entry);
		set_bit(DPC_ISNS_RESTART, &ha->dpc_flags);
		goto exit_pt_sts;
	}

	switch (pdu_type) {
	case ISNS_ASYNCH_RSP_PDU:
		qla4xxx_free_pdu(ha, pdu_entry);
		break;

	case ISNS_ASYNCH_REQ_PDU:
		pdu_entry->RecvBuffLen -= inResidual;

		QL4PRINT(QLP19, printk("scsi%d: %s ISNS_ASYNCH_REQ_PDU  PDU Buff=%p, PDU RecvLen=0x%X\n",
				       ha->host_no, __func__, pdu_entry->Buff, pdu_entry->RecvBuffLen));

		if (qla4xxx_isns_reassemble_pdu(ha, pdu_entry->Buff,
						&pdu_entry->RecvBuffLen)
		    != QLA_SUCCESS) {
			QL4PRINT(QLP2,
				 printk("scsi%d: %s ISNS_ASYNCH_REQ_PDU "
					"reassemble_pdu failed!\n",
					ha->host_no, __func__));
			goto exit_pt_sts;
		}

		if (qla4xxx_isns_parse_and_dispatch_server_request(ha,
								   pdu_entry->Buff,
								   pdu_entry->RecvBuffLen,
								   connectionID)
		    != QLA_SUCCESS) {
			QL4PRINT(QLP2,
				 printk("scsi%d: %s ISNS_ASYNCH_REQ_PDU "
					"parse_and_dispatch_server_request failed!\n",
					ha->host_no, __func__));
		}
		qla4xxx_free_pdu(ha, pdu_entry);
		break;

	case ISNS_REQ_RSP_PDU:
		pdu_entry->RecvBuffLen -= inResidual;

		QL4PRINT(QLP19, printk("scsi%d: %s ISNS_REQ_RSP_PDU  PDU Buff=%p, PDU RecvLen=0x%X\n",
				       ha->host_no, __func__, pdu_entry->Buff, pdu_entry->RecvBuffLen));


		if (qla4xxx_isns_reassemble_pdu(ha, pdu_entry->Buff,
						&pdu_entry->RecvBuffLen)
		    != QLA_SUCCESS) {
			QL4PRINT(QLP2,
				 printk("scsi%d: %s ISNS_REQ_RSP_PDU "
					"reassemble_pdu failed!\n",
					ha->host_no, __func__));
			goto exit_pt_sts;
		}

		if (qla4xxx_isns_parse_and_dispatch_server_response(ha,
								    pdu_entry->Buff,
								    pdu_entry->RecvBuffLen)
		    != QLA_SUCCESS) {
			QL4PRINT(QLP2,
				 printk("scsi%d: %s ISNS_REQ_RSP_PDU "
					"parse_and_dispatch_server_response failed!\n",
					ha->host_no, __func__));
		}
		qla4xxx_free_pdu(ha, pdu_entry);
		break;
	default:
		QL4PRINT(QLP2,
			 printk("scsi%d: %s iSNS handle 0x%x invalid\n",
				ha->host_no, __func__, handle));
		status = QLA_ERROR;
		break;
	}

	exit_pt_sts:
	LEAVE("qla4xxx_passthru_status_entry");
	return(status);
}

uint8_t
qla4xxx_isns_reassemble_pdu(scsi_qla_host_t *ha, uint8_t *buffer, uint32_t *buffer_size)
{
	uint16_t copy_size = 0;
	uint32_t new_pdu_length = 0;
	uint32_t bytes_remaining;
	uint32_t pdu_size;
	uint8_t *dest_ptr = NULL;
	uint8_t *src_ptr = NULL;
	ISNSP_MESSAGE_HEADER *isns_message;
	uint32_t i;

	// We have read all the PDU's for this message.  Now reassemble them
	// into a single PDU.
	if (buffer == NULL || buffer_size == 0) {
		return(QLA_ERROR);
	}

	if (*buffer_size == 0) {
		QL4PRINT(QLP2,
			 printk(KERN_WARNING "scsi%d: %s: Length 0.  "
				"Nothing to reassemble\n",
				ha->host_no, __func__));
		return(QLA_ERROR);
	}

	new_pdu_length = 0;
	bytes_remaining = *buffer_size;
	isns_message = (ISNSP_MESSAGE_HEADER *) buffer;

	if ((!be16_to_cpu(isns_message->flags) & ISNSP_FIRST_PDU)) {
		QL4PRINT(QLP2,
			 printk(KERN_WARNING "scsi%d: %s: ISNSP_FIRST_PDU "
				"not set, discard PDU\n",
				ha->host_no, __func__));
		*buffer_size = 0;
		return(QLA_ERROR);
	}

	// First, calculate the size of the payload for the collapsed PDU
	do {
		if (bytes_remaining < sizeof(ISNSP_MESSAGE_HEADER)) {
			QL4PRINT(QLP2,
				 printk(KERN_WARNING "scsi%d: %s: Length 0.  "
					"bytes_remaining < "
					"sizeof(ISNSP_MESSAGE_HEADER).  "
					"BytesRemaining %x, discard PDU\n",
					ha->host_no, __func__,
					bytes_remaining));
			*buffer_size = 0;
			return(QLA_ERROR);
		}
		else if (be16_to_cpu(isns_message->isnsp_version) !=
			 ISNSP_VERSION) {

			QL4PRINT(QLP2,
				 printk(KERN_WARNING "scsi%d: %s: Bad Version "
					"number in iSNS Message Header "
					"(%04x, expecting %04x), discard PDU\n",
					ha->host_no, __func__,
					be16_to_cpu(isns_message->isnsp_version),
					ISNSP_VERSION));
			*buffer_size = 0;
			return(QLA_ERROR);
		}
		else if (bytes_remaining < sizeof(ISNSP_MESSAGE_HEADER) +
			 be16_to_cpu(isns_message->pdu_length)) {

			QL4PRINT(QLP2,
				 printk(KERN_WARNING "scsi%d: %s: Short PDU "
					"in sequence. BytesRemaining %x, "
					"discard PDU\n",
					ha->host_no, __func__,
					bytes_remaining));
			*buffer_size = 0;
			return(QLA_ERROR);
		}
		else if ((bytes_remaining == sizeof(ISNSP_MESSAGE_HEADER) +
                          be16_to_cpu(isns_message->pdu_length)) &&
                         (!(be16_to_cpu(isns_message->flags) & ISNSP_LAST_PDU))) {

			QL4PRINT(QLP2,
				 printk(KERN_WARNING "scsi%d: %s: "
					"Last PDU Flag not set at end "
					"of sequence. discard PDU\n",
					ha->host_no, __func__));
			*buffer_size = 0;
			return(QLA_ERROR);
		}

		new_pdu_length += be16_to_cpu(isns_message->pdu_length);
		pdu_size = sizeof(ISNSP_MESSAGE_HEADER) +
			be16_to_cpu(isns_message->pdu_length);

		isns_message = (ISNSP_MESSAGE_HEADER *) ((uint8_t *)
							 isns_message + pdu_size);

		bytes_remaining = bytes_remaining > pdu_size ?
			bytes_remaining - pdu_size : 0;
	}
	while (bytes_remaining);

	dest_ptr = buffer;
	bytes_remaining = *buffer_size;
	isns_message = (ISNSP_MESSAGE_HEADER *) buffer;
	i = 0;
	QL4PRINT(QLP19, printk("scsi%d: %s: PDU%d=%p payloadLength=%04x\n",
			       ha->host_no, __func__, i, dest_ptr,
			       be16_to_cpu(isns_message->pdu_length)));

	while (bytes_remaining) {
		// If this is the first PDU perform no copy,
		// otherwise copy just the payload.

		pdu_size = sizeof(ISNSP_MESSAGE_HEADER) +
			be16_to_cpu(isns_message->pdu_length);

		bytes_remaining = bytes_remaining > pdu_size ?
			bytes_remaining - pdu_size : 0;
		
		copy_size = be16_to_cpu(isns_message->pdu_length);

		if (dest_ptr == buffer) {
			src_ptr = (uint8_t *) isns_message;
			copy_size += sizeof(ISNSP_MESSAGE_HEADER);
		}
		else {
			src_ptr = (uint8_t *) isns_message->payload;
			memcpy(dest_ptr, src_ptr, copy_size);
		}
		
		QL4PRINT(QLP19,
			 printk("scsi%d: %s: PDU%d %p <= %p (%04x)\n",
				ha->host_no, __func__, i, dest_ptr,
				src_ptr, copy_size));

		dest_ptr += copy_size;
		isns_message = (ISNSP_MESSAGE_HEADER *) ((uint8_t *)
							 isns_message + pdu_size);
		i++;

	}

	isns_message = (ISNSP_MESSAGE_HEADER *) buffer;

	// Update pdu_length field in reassembled PDU to reflect actual
	// combined PDU payload length.
	isns_message->pdu_length = cpu_to_be16(new_pdu_length);

	// Also set LAST_PDU flag in reassembled PDU
	isns_message->flags |= cpu_to_be16(ISNSP_LAST_PDU);

	// Return number of bytes in buffer to caller.
	*buffer_size = new_pdu_length + sizeof(ISNSP_MESSAGE_HEADER);
	return(QLA_SUCCESS);
}

uint8_t
qla4xxx_isns_scn (scsi_qla_host_t *ha,
		  uint8_t * req_buffer,
		  uint32_t req_buffer_size,
		  uint16_t ConnectionId)
{
	ISNSP_MESSAGE_HEADER * isns_req_message;
	ISNSP_MESSAGE_HEADER * isns_rsp_message;
	ISNSP_RESPONSE_HEADER * isns_response;
	PDU_ENTRY * pdu_entry;
	ISNS_ATTRIBUTE * attr;
	uint8_t * req_buffer_end;
	uint8_t * rsp_buffer_end;
	uint8_t * payload_start;
	uint8_t * ptr;
	uint32_t packet_size;
	uint32_t copy_size;

	isns_req_message = (ISNSP_MESSAGE_HEADER *) req_buffer;

	if ((pdu_entry = qla4xxx_get_pdu (ha, PAGE_SIZE)) == NULL) {
		QL4PRINT(QLP2, printk("scsi%d: %s: qla4xxx_get_pdu failed\n",
				      ha->host_no, __func__));
		return(QLA_ERROR);
	}

	// First, setup the response packet.
	if (qla4xxx_isns_build_server_request_response_packet(ha,
							      pdu_entry->Buff,
							      pdu_entry->BuffLen,
							      (be16_to_cpu(isns_req_message->function_id) | 0x8000),
							      ISNS_ERR_SUCCESS,
							      be16_to_cpu(isns_req_message->transaction_id),
							      &packet_size)
	    != QLA_SUCCESS) {
		QL4PRINT(QLP2,
			 printk("scsi%d: %s: qla4xxx_isns_build_server_"
				"request_response_packet failed\n",
				ha->host_no, __func__));
		qla4xxx_free_pdu (ha, pdu_entry);
		return(QLA_ERROR);
	}
	isns_rsp_message = (ISNSP_MESSAGE_HEADER *) pdu_entry->Buff;
	isns_response = (ISNSP_RESPONSE_HEADER *) &isns_rsp_message->payload[0];
	payload_start = (uint8_t *) isns_response;
	rsp_buffer_end = (uint8_t *) (pdu_entry->Buff + pdu_entry->BuffLen);

	ptr = &isns_response->attributes[0];

	req_buffer_end = (uint8_t *) ((uint8_t *) &isns_req_message->payload[0] +
				      be16_to_cpu(isns_req_message->pdu_length));

	// Point to the source attribute in the request.  We need to return only
	// this attribute in the SCN Response.
	attr = (ISNS_ATTRIBUTE *) &isns_req_message->payload[0];
	if (!VALIDATE_ATTR(attr, req_buffer_end)) {
		isns_response->error_code = cpu_to_be32(ISNS_ERR_MSG_FORMAT);
		QL4PRINT(QLP2, printk("scsi%d: %s: Malformed packet\n",
				      ha->host_no, __func__));
	}

	// Validate that this is an iSCSI Name attribute.
	if (be32_to_cpu(attr->tag) != ISNS_ATTR_TAG_ISCSI_NAME) {
		QL4PRINT(QLP2, printk("scsi%d: %s: Did not find iSCSN Name attribute\n",
				      ha->host_no, __func__));
	}

	// Copy source attribute to return buffer.
	copy_size = sizeof(ISNS_ATTRIBUTE) + be32_to_cpu(attr->length);

	if (ptr + copy_size < rsp_buffer_end) {
		// Attribute will fit in the response buffer.  Go ahead
		// and copy it.
		memcpy(ptr, attr, copy_size);
		ptr += copy_size;
	}
	else {
		QL4PRINT(QLP2, printk("scsi%d: %s: Insufficient buffer size\n",
				      ha->host_no, __func__));
	}

	// We've successfully finished building the response packet.
	// Set the size field.

	//QLASSERT (!((ptr - payload_start) % 4));

	isns_rsp_message->pdu_length = cpu_to_be16((unsigned long) ptr -
                                                   (unsigned long) payload_start);

	packet_size = (unsigned long) ptr - (unsigned long) pdu_entry->Buff;

	pdu_entry->SendBuffLen = packet_size;
	pdu_entry->RecvBuffLen = 0;

	QL4PRINT(QLP20, printk("---------------------------\n"));
	QL4PRINT(QLP20, printk("scsi%d: %s:                            sending  %d SCNRsp\n",
			       ha->host_no, __func__,
			       be16_to_cpu(isns_rsp_message->transaction_id)));
	QL4PRINT(QLP19, printk("PDU (0x%p) 0x%x ->\n", pdu_entry->Buff, pdu_entry->SendBuffLen));
	qla4xxx_dump_bytes(QLP19, pdu_entry->Buff, pdu_entry->SendBuffLen);

	if (qla4xxx_send_passthru0_iocb (ha, ISNS_DEVICE_INDEX, ConnectionId,
					 pdu_entry->DmaBuff,
					 pdu_entry->SendBuffLen,
					 pdu_entry->RecvBuffLen,
					 PT_FLAG_ISNS_PDU,
					 qla4xxx_isns_build_iocb_handle(ha, ISNS_ASYNCH_RSP_PDU, pdu_entry))
	    != QLA_SUCCESS) {
		QL4PRINT(QLP2, printk("scsi%d: %s:  qla4xxx_send_passthru0_iocb failed\n",
				      ha->host_no, __func__));
		qla4xxx_free_pdu (ha, pdu_entry);
		return(QLA_ERROR);
	}

	if (test_bit(ISNS_FLAG_SCN_IN_PROGRESS, &ha->isns_flags)) {
		set_bit(ISNS_FLAG_SCN_RESTART, &ha->isns_flags);
	}
	else {
		set_bit(ISNS_FLAG_SCN_IN_PROGRESS, &ha->isns_flags);
		clear_bit(ISNS_FLAG_SCN_RESTART, &ha->isns_flags);
		ha->isns_num_discovered_targets = 0;
		if (qla4xxx_isns_dev_get_next (ha, NULL) != QLA_SUCCESS) {
			QL4PRINT(QLP2, printk("scsi%d: %s: qla4xxx_isns_dev_get_next failed\n",
					      ha->host_no, __func__));
			ISNS_CLEAR_FLAGS(ha);
		}
	}

	return(QLA_SUCCESS);
}

uint8_t
qla4xxx_isns_esi (scsi_qla_host_t *ha,
		  uint8_t *req_buffer,
		  uint32_t req_buffer_size,
		  uint16_t ConnectionId)
{
	ISNSP_MESSAGE_HEADER *isns_req_message;
	ISNSP_MESSAGE_HEADER *isns_rsp_message;
	ISNSP_RESPONSE_HEADER *isns_response;
	PDU_ENTRY * pdu_entry;
	ISNS_ATTRIBUTE *attr;
	uint8_t * req_buffer_end;
	uint8_t * rsp_buffer_end;
	uint8_t * payload_start;
	uint8_t * ptr;
	uint32_t packet_size;
	uint32_t copy_size;

	isns_req_message = (ISNSP_MESSAGE_HEADER *) req_buffer;

	if ((pdu_entry = qla4xxx_get_pdu (ha, req_buffer_size + sizeof(uint32_t))) == NULL) {
		QL4PRINT(QLP2, printk("scsi%d: %s:  qla4xxx_get_pdu failed\n",
				      ha->host_no, __func__));
		return(QLA_ERROR);
	}

	// First, setup the response packet.
	if (qla4xxx_isns_build_server_request_response_packet(ha,
							      pdu_entry->Buff,
							      pdu_entry->BuffLen,
							      (be16_to_cpu(isns_req_message->function_id) | 0x8000),
							      ISNS_ERR_SUCCESS,
							      be16_to_cpu(isns_req_message->transaction_id),
							      &packet_size)
	    != QLA_SUCCESS) {
		QL4PRINT(QLP2, printk("scsi%d: %s:  qla4xxx_isns_build_server_request_response_packet failed\n",
				      ha->host_no, __func__));
		qla4xxx_free_pdu (ha, pdu_entry);
		return(QLA_ERROR);
	}
	isns_rsp_message = (ISNSP_MESSAGE_HEADER *) pdu_entry->Buff;
	isns_response = (ISNSP_RESPONSE_HEADER *) &isns_rsp_message->payload[0];
	payload_start = ( uint8_t *) isns_response;
	rsp_buffer_end = ( uint8_t *) (pdu_entry->Buff + pdu_entry->BuffLen);

	ptr = &isns_response->attributes[0];

	req_buffer_end =
		( uint8_t *) (( uint8_t *) &isns_req_message->payload[0] +
                              be16_to_cpu(isns_req_message->pdu_length));

	// Point to the source attribute in the request.  We need to return
	// all attributes in the ESI Response.
	attr = (ISNS_ATTRIBUTE *) &isns_req_message->payload[0];

	// Copy source attributes to return buffer.
	copy_size = req_buffer_end - ( uint8_t *) attr;

	if (ptr + copy_size < rsp_buffer_end) {
		// Attributes will fit in the response buffer.  Go ahead
		// and copy them.
		memcpy(ptr, attr, copy_size);
		ptr += copy_size;
	}
	else {
		QL4PRINT(QLP2,
			 printk("scsi%d: %s: Insufficient buffer size\n",
				ha->host_no, __func__));
	}

	// We've successfully finished building the response packet.
	// Set the size field.

	//QLASSERT (!((ptr - payload_start) % 4));

	isns_rsp_message->pdu_length = cpu_to_be16((unsigned long) ptr -
                                                   (unsigned long) payload_start);

	packet_size = (unsigned long) ptr - (unsigned long) pdu_entry->Buff;

	pdu_entry->SendBuffLen = packet_size;
	pdu_entry->RecvBuffLen = 0;

	QL4PRINT(QLP20, printk("---------------------------\n"));
	QL4PRINT(QLP20,
		 printk("scsi%d: %s:                            sending  %d ESIRsp\n",
			ha->host_no, __func__,
			be16_to_cpu(isns_rsp_message->transaction_id)));
	QL4PRINT(QLP19, printk("PDU (0x%p) 0x%x ->\n", pdu_entry->Buff, pdu_entry->SendBuffLen));
	qla4xxx_dump_bytes(QLP19, pdu_entry->Buff, pdu_entry->SendBuffLen);

	if (qla4xxx_send_passthru0_iocb(ha, ISNS_DEVICE_INDEX,
					ConnectionId,
					pdu_entry->DmaBuff,
					pdu_entry->SendBuffLen,
					pdu_entry->RecvBuffLen,
					PT_FLAG_ISNS_PDU,
					qla4xxx_isns_build_iocb_handle (ha, ISNS_ASYNCH_RSP_PDU, pdu_entry))
	    != QLA_SUCCESS) {
		QL4PRINT(QLP2, printk("scsi%d: %s:  qla4xxx_send_passthru0_iocb failed\n",
				      ha->host_no, __func__));
		qla4xxx_free_pdu (ha, pdu_entry);
		return(QLA_ERROR);
	}

	return(QLA_SUCCESS);
}


uint8_t
qla4xxx_isns_server_request_error(scsi_qla_host_t *ha,
				  uint8_t *buffer,
				  uint32_t buffer_size,
				  uint16_t connection_id,
				  uint32_t error_code)	 //cpu
{
	PDU_ENTRY *pdu_entry;
	ISNSP_MESSAGE_HEADER *isns_message;
	uint16_t function_id;
	uint32_t packet_size;

	isns_message = (ISNSP_MESSAGE_HEADER *) buffer;
	function_id = be16_to_cpu(isns_message->function_id);

	// Return "Message Format Error"
	if ((pdu_entry = qla4xxx_get_pdu(ha, sizeof(ISNSP_MESSAGE_HEADER) +
					 sizeof(uint32_t))) == NULL) {
		QL4PRINT(QLP2, printk("scsi%d: %s:  qla4xxx_get_pdu failed\n",
				      ha->host_no, __func__));
		return(QLA_ERROR);
	}

	if (qla4xxx_isns_build_server_request_response_packet(
		ha, pdu_entry->Buff, pdu_entry->BuffLen,
                (be16_to_cpu(isns_message->function_id) | 0x8000),
                error_code,
                be16_to_cpu(isns_message->transaction_id),
                &packet_size) != QLA_SUCCESS) {

		QL4PRINT(QLP2, printk("scsi%d: %s:  qla4xxx_isns_build_server_"
				      "request_response_packet failed\n",
				      ha->host_no, __func__));
		qla4xxx_free_pdu(ha, pdu_entry);
		return(QLA_ERROR);
	}

	pdu_entry->SendBuffLen = packet_size;
	pdu_entry->RecvBuffLen = 0;

	QL4PRINT(QLP19, printk("PDU (0x%p) 0x%x ->\n", pdu_entry->Buff, pdu_entry->SendBuffLen));
	qla4xxx_dump_bytes(QLP19, pdu_entry->Buff, pdu_entry->SendBuffLen);

	if (qla4xxx_send_passthru0_iocb(
		ha, ISNS_DEVICE_INDEX, connection_id,
					pdu_entry->DmaBuff,
					pdu_entry->SendBuffLen,
					pdu_entry->RecvBuffLen, PT_FLAG_ISNS_PDU,
					qla4xxx_isns_build_iocb_handle(ha, ISNS_ASYNCH_RSP_PDU, pdu_entry))
	    != QLA_SUCCESS) {

		QL4PRINT(QLP2, printk("scsi%d: %s:  qla4xxx_send_passthru0_iocb "
				      "failed\n, ha->host_no, __func__",
				      ha->host_no, __func__));
		qla4xxx_free_pdu(ha, pdu_entry);
		return(QLA_ERROR);
	}
	return(QLA_SUCCESS);
}


uint8_t
qla4xxx_isns_parse_and_dispatch_server_request(scsi_qla_host_t *ha,
					       uint8_t *buffer,
					       uint32_t buffer_size,
					       uint16_t connection_id)
{
	ISNSP_MESSAGE_HEADER *isns_message;
	uint16_t function_id;
	uint16_t transaction_id;

	isns_message = (ISNSP_MESSAGE_HEADER *) buffer;
	function_id = be16_to_cpu(isns_message->function_id);
	transaction_id = be16_to_cpu(isns_message->transaction_id);

	// Validate pdu_length specified in the iSNS message header.
	if ((offsetof (ISNSP_MESSAGE_HEADER, payload) +
             be16_to_cpu(isns_message->pdu_length)) > buffer_size) {

		QL4PRINT(QLP2, printk("scsi%d: %s: Invalid message size %u %u\n",
				      ha->host_no, __func__,
				      (uint32_t) (offsetof(ISNSP_MESSAGE_HEADER, payload) +
                                       be16_to_cpu(isns_message->pdu_length)),
				      buffer_size));

		if (function_id <= ISNS_FCID_ESI) {
			return(qla4xxx_isns_server_request_error(ha, buffer,
								 buffer_size,
								 connection_id,
								 ISNS_ERR_MSG_FORMAT));
		}
		return(QLA_ERROR);
	}

	// It is safe to assume from this point on that the pdu_length value
	// (and thus our idea about the end of the buffer) is valid.

	switch (function_id) {
	case ISNS_FCID_SCN:
		QL4PRINT(QLP2, printk("scsi%d: %s:  received %d SCN\n",
				      ha->host_no, __func__,
				      transaction_id));
		return(qla4xxx_isns_scn(ha, buffer, buffer_size, connection_id));
		break;

	case ISNS_FCID_ESI:
		QL4PRINT(QLP2, printk("scsi%d: %s:  received %d ESI\n",
				      ha->host_no, __func__,
				      transaction_id));
		return(qla4xxx_isns_esi(ha, buffer, buffer_size, connection_id));
		break;

	default:
		QL4PRINT(QLP2, printk("scsi%d: %s:  received %d Unknown iSNS ServerRequest %x\n",
				      ha->host_no, __func__,
				      transaction_id, function_id));
		if (function_id <= ISNS_FCID_ESI) {
			// Return "Message Not Supported"
			return(qla4xxx_isns_server_request_error (ha,
								  buffer,
								  buffer_size,
								  connection_id,
								  ISNS_ERR_MSG_NOT_SUPPORTED));
		}
		return(QLA_ERROR);
		break;
	}
	return(QLA_SUCCESS);


}

uint8_t
qla4xxx_isns_parse_and_dispatch_server_response(scsi_qla_host_t *ha,
						uint8_t *buffer,
						uint32_t buffer_size)
{
	ISNSP_MESSAGE_HEADER *isns_message;
	ISNSP_RESPONSE_HEADER *isns_response;
	ISNS_ATTRIBUTE *isns_attr;
	uint16_t function_id;
	uint16_t transaction_id;
	uint8_t *buffer_end;

	isns_message = (ISNSP_MESSAGE_HEADER *) buffer;
	buffer_end = (uint8_t *) ((uint8_t *) isns_message->payload +
				  be16_to_cpu(isns_message->pdu_length));

	isns_attr = (ISNS_ATTRIBUTE *) isns_message->payload;

	/* Validate pdu_length specified in the iSNS message header. */
	if (((uint32_t *) buffer_end - (uint32_t *) buffer) > buffer_size) {
		QL4PRINT(QLP2,
			 printk("scsi%d: %s: Invalid message size %u %u\n",
				ha->host_no, __func__,
				(unsigned int) ((uint32_t *) buffer_end - (uint32_t *) buffer),
				buffer_size));
		return(QLA_ERROR);
	}

	transaction_id = be16_to_cpu(isns_message->transaction_id);
	function_id = be16_to_cpu(isns_message->function_id);
	/*
	 * It is safe to assume from this point on that the pdu_length value
	 * (and thus our idea about the end of the buffer) is valid.
	 */
	if (transaction_id > ha->isns_transaction_id) {

		QL4PRINT(QLP2, printk("scsi%d: %s: Invalid message transaction "
				      "ID recv %x exp %x\n",
				      ha->host_no, __func__,
				      transaction_id,
				      ha->isns_transaction_id));
		qla4xxx_dump_bytes(QLP2, buffer, buffer_size);

		set_bit(DPC_ISNS_RESTART, &ha->dpc_flags);
		return(QLA_ERROR);
	}


	isns_response = (ISNSP_RESPONSE_HEADER *) &isns_message->payload[0];

	//QL4PRINT(QLP20, printk("---------------------------\n"));
	//QL4PRINT(QLP20, printk("scsi%d: %s: received function_id %x\n",
	//		       ha->host_no, __func__, function_id));

	switch (function_id) {
	case ISNS_FCID_DevAttrRegRsp:
		QL4PRINT(QLP20, printk("scsi%d: %s: received %d DevAttrRegRsp\n",
				       ha->host_no, __func__,
				       transaction_id));
		return(qla4xxx_isns_dev_attr_reg_rsp(ha, buffer, buffer_size));

	case ISNS_FCID_DevAttrQryRsp:
		QL4PRINT(QLP20, printk("scsi%d: %s: received %d DevAttrQryRsp\n",
				       ha->host_no, __func__,
				       transaction_id));
		return(qla4xxx_isns_dev_attr_qry_rsp(ha, buffer, buffer_size));

	case ISNS_FCID_DevGetNextRsp:
		QL4PRINT(QLP20, printk("scsi%d: %s: received %d DevGetNextRsp\n",
				       ha->host_no, __func__,
				       transaction_id));
		return(qla4xxx_isns_dev_get_next_rsp(ha, buffer, buffer_size));

	case ISNS_FCID_DevDeregRsp:
		QL4PRINT(QLP20, printk("scsi%d: %s: received %d DevDeregRsp\n",
				       ha->host_no, __func__,
				       transaction_id));
		return(qla4xxx_isns_dev_dereg_rsp(ha, buffer, buffer_size));

	case ISNS_FCID_SCNRegRsp:
		QL4PRINT(QLP20, printk("scsi%d: %s: received %d SCNRegRsp\n",
				       ha->host_no, __func__,
				       transaction_id));
		return(qla4xxx_isns_scn_reg_rsp(ha, buffer, buffer_size));

	case ISNS_FCID_SCNDeregRsp:
		QL4PRINT(QLP20, printk("scsi%d: %s: received %d SCNDeregRsp\n",
				       ha->host_no, __func__,
				       transaction_id));
		return(qla4xxx_isns_scn_dereg_rsp(ha, buffer, buffer_size));

	default:
		QL4PRINT(QLP2, printk("scsi%d: %s: Received %d Unknown iSNS function_id %x\n",
				      ha->host_no, __func__,
				      transaction_id, function_id));
		break;
	}
	return(QLA_SUCCESS);
}

uint8_t
qla4xxx_isns_dev_attr_reg(scsi_qla_host_t *ha)
{
	PDU_ENTRY *pdu_entry;
	uint32_t  packet_size;

	pdu_entry = qla4xxx_get_pdu(ha, PAGE_SIZE);
	if (pdu_entry == NULL) {
		QL4PRINT(QLP2, printk("scsi%d: %s: get pdu failed\n",
				      ha->host_no, __func__));
		return(QLA_ERROR);
	}

	if (qla4xxx_isns_build_registration_packet(ha, pdu_entry->Buff,
						   pdu_entry->BuffLen,
						   ha->isns_entity_id,
						   ha->ip_address,
						   ha->isns_remote_port_num,
						   ha->isns_scn_port_num,
						   ha->isns_esi_port_num,
						   ha->alias, &packet_size)
	    != QLA_SUCCESS) {
		QL4PRINT(QLP2,
			 printk("scsi%d: %s: "
				"qla4xxx_isns_build_registration_packet failed\n",
				ha->host_no, __func__));
		qla4xxx_free_pdu(ha, pdu_entry);
		return(QLA_ERROR);
	}

	pdu_entry->SendBuffLen = packet_size;
	pdu_entry->RecvBuffLen = pdu_entry->BuffLen;

	QL4PRINT(QLP20, printk("---------------------------\n"));
	QL4PRINT(QLP20, printk("scsi%d: %s:                    sending %d DevAttrReg\n",
			       ha->host_no, __func__, ha->isns_transaction_id));

	QL4PRINT(QLP19, printk("PDU (0x%p) 0x%x ->\n", pdu_entry->Buff, pdu_entry->SendBuffLen));
	qla4xxx_dump_bytes(QLP19, pdu_entry->Buff, pdu_entry->SendBuffLen);

	QL4PRINT(QLP20, printk("scsi%d: %s: Registering iSNS . . .\n",
			       ha->host_no, __func__));

	if (qla4xxx_send_passthru0_iocb(
		ha, ISNS_DEVICE_INDEX,
					ISNS_DEFAULT_SERVER_CONN_ID,
					pdu_entry->DmaBuff,
					pdu_entry->SendBuffLen,
					pdu_entry->RecvBuffLen,
					PT_FLAG_ISNS_PDU|PT_FLAG_WAIT_4_RESPONSE,
					qla4xxx_isns_build_iocb_handle(ha, ISNS_REQ_RSP_PDU, pdu_entry))
	    != QLA_SUCCESS) {
		QL4PRINT(QLP2,
			 printk("scsi%d: %s: "
				"qla4xxx_send_passthru0_iocb failed\n",
				ha->host_no, __func__));
		qla4xxx_free_pdu(ha, pdu_entry);
		return(QLA_ERROR);
	}

	ha->isns_transaction_id++;
	return(QLA_SUCCESS);
}

uint8_t
qla4xxx_isns_dev_attr_reg_rsp(scsi_qla_host_t *ha,
			      uint8_t *buffer,
			      uint32_t buffer_size)
{
	ISNSP_MESSAGE_HEADER *isns_message;
	ISNSP_RESPONSE_HEADER *isns_response;
	uint32_t error_code;

	isns_message = (ISNSP_MESSAGE_HEADER *) buffer;
	isns_response = (ISNSP_RESPONSE_HEADER *) &isns_message->payload[0];
	error_code = be32_to_cpu(isns_response->error_code);

	if (error_code) {
		QL4PRINT(QLP2, printk("scsi%d: %s: iSNS DevAttrReg failed, "
				      "error code (%x) \"%s\"\n",
				      ha->host_no, __func__,
				      error_code,
				      isns_error_code_msg[error_code]));
		clear_bit(ISNS_FLAG_ISNS_SRV_REGISTERED, &ha->isns_flags);
		return(QLA_ERROR);
	}

	set_bit(ISNS_FLAG_ISNS_SRV_REGISTERED, &ha->isns_flags);
	if (qla4xxx_isns_scn_reg(ha) != QLA_SUCCESS) {
		QL4PRINT(QLP2, printk("scsi%d: %s: qla4xxx_isns_scn_reg failed\n",
				      ha->host_no, __func__));
		return(QLA_ERROR);
	}
	return(QLA_SUCCESS);
}

uint8_t
qla4xxx_isns_scn_reg(scsi_qla_host_t *ha)
{
	PDU_ENTRY *isns_pdu_entry;
	uint32_t packet_size;

	if ((isns_pdu_entry = qla4xxx_get_pdu (ha, PAGE_SIZE)) == NULL) {
		QL4PRINT(QLP2, printk("scsi%d: %s: qla4xxx_get_pdu failed\n",
				      ha->host_no, __func__));
		return(QLA_ERROR);
	}

	if (qla4xxx_isns_build_scn_registration_packet(
		ha, isns_pdu_entry->Buff, isns_pdu_entry->BuffLen,
						       &packet_size) != QLA_SUCCESS) {

		QL4PRINT(QLP2, printk("scsi%d: %s: qla4xxx_isns_build_scn_"
				      "registration_packet failed\n",
				      ha->host_no, __func__));
		qla4xxx_free_pdu(ha, isns_pdu_entry);
		return(QLA_ERROR);
	}

	isns_pdu_entry->SendBuffLen = packet_size;
	isns_pdu_entry->RecvBuffLen = isns_pdu_entry->BuffLen;
	
	QL4PRINT(QLP20, printk("---------------------------\n"));
	QL4PRINT(QLP20, printk("scsi%d :%s:                        sending  %d SCNReg\n",
			       ha->host_no, __func__, ha->isns_transaction_id));
	QL4PRINT(QLP19, printk("PDU (0x%p) 0x%x ->\n", isns_pdu_entry->Buff, isns_pdu_entry->SendBuffLen));
	qla4xxx_dump_bytes(QLP19, isns_pdu_entry->Buff, isns_pdu_entry->SendBuffLen);

	if (qla4xxx_send_passthru0_iocb(
		ha, ISNS_DEVICE_INDEX,
					ISNS_DEFAULT_SERVER_CONN_ID,
					isns_pdu_entry->DmaBuff,
					isns_pdu_entry->SendBuffLen,
					isns_pdu_entry->RecvBuffLen,
					PT_FLAG_ISNS_PDU | PT_FLAG_WAIT_4_RESPONSE,
					qla4xxx_isns_build_iocb_handle(ha, ISNS_REQ_RSP_PDU, isns_pdu_entry))
	    != QLA_SUCCESS) {

		QL4PRINT(QLP2, printk("scsi%d: %s: qla4xxx_send_passthru0_iocb failed\n",
				      ha->host_no, __func__));
		qla4xxx_free_pdu(ha, isns_pdu_entry);
		return(QLA_ERROR);
	}
	ha->isns_transaction_id++;
	return(QLA_SUCCESS);
}

uint8_t
qla4xxx_isns_scn_reg_rsp(scsi_qla_host_t *ha,
			 uint8_t *buffer,
			 uint32_t buffer_size)
{
	ISNSP_MESSAGE_HEADER *isns_message;
	ISNSP_RESPONSE_HEADER *isns_response;

	isns_message = (ISNSP_MESSAGE_HEADER *) buffer;
	isns_response = (ISNSP_RESPONSE_HEADER *) isns_message->payload;

	if (isns_response->error_code) {
		QL4PRINT(QLP2,
			 printk("scsi%d: %s: iSNS SCNReg failed, error code %x\n",
				ha->host_no, __func__,
				be32_to_cpu(isns_response->error_code)));
		clear_bit(ISNS_FLAG_ISNS_SCN_REGISTERED, &ha->isns_flags);
		return(QLA_ERROR);
	}

	set_bit(ISNS_FLAG_ISNS_SCN_REGISTERED, &ha->isns_flags);

	ha->isns_num_discovered_targets = 0;
	if (qla4xxx_isns_dev_get_next(ha, NULL) != QLA_SUCCESS) {
		QL4PRINT(QLP2,
			 printk("scsi%d: %s: qla4xxx_isns_dev_get_next failed\n",
				      ha->host_no, __func__));
	}

	return(QLA_SUCCESS);
}

uint8_t
qla4xxx_isns_dev_attr_qry(scsi_qla_host_t *ha,
			  uint8_t *last_iscsi_name)
{
	PDU_ENTRY *pdu_entry;
	uint32_t packet_size;

	if ((pdu_entry = qla4xxx_get_pdu(ha, PAGE_SIZE)) == NULL) {
		QL4PRINT(QLP2, printk("scsi%d: %s:  qla4xxx_get_pdu failed\n",
				      ha->host_no, __func__));
		return(QLA_ERROR);
	}

	if (qla4xxx_isns_build_dev_attr_qry_packet(ha, pdu_entry->Buff,
						   pdu_entry->BuffLen,
						   last_iscsi_name,
						   &packet_size)
	    != QLA_SUCCESS) {
		QL4PRINT(QLP2,
			 printk("scsi%d: %s:  qla4xxx_isns_build_dev_attr_qry_packet failed\n",
				ha->host_no, __func__));
		qla4xxx_free_pdu (ha, pdu_entry);
		return(QLA_ERROR);
	}

	pdu_entry->SendBuffLen = packet_size;
	pdu_entry->RecvBuffLen = pdu_entry->BuffLen;

	QL4PRINT(QLP20, printk("---------------------------\n"));
	QL4PRINT(QLP20,
		 printk("scsi%d: %s:                     sending  %d DevAttrQry\n",
			ha->host_no, __func__, ha->isns_transaction_id));
	QL4PRINT(QLP19, printk("PDU (0x%p) 0x%x ->\n", pdu_entry->Buff, pdu_entry->SendBuffLen));
	qla4xxx_dump_bytes(QLP19, pdu_entry->Buff, pdu_entry->SendBuffLen);

	if (qla4xxx_send_passthru0_iocb(
		ha, ISNS_DEVICE_INDEX,
					ISNS_DEFAULT_SERVER_CONN_ID,
					pdu_entry->DmaBuff,
					pdu_entry->SendBuffLen,
					pdu_entry->RecvBuffLen,
					PT_FLAG_ISNS_PDU | PT_FLAG_WAIT_4_RESPONSE,
					qla4xxx_isns_build_iocb_handle (ha, ISNS_REQ_RSP_PDU, pdu_entry))
	    != QLA_SUCCESS) {

		QL4PRINT(QLP2, printk("scsi%d: %s:  qla4xxx_send_passthru0_iocb "
				      "failed\n", ha->host_no, __func__));
		qla4xxx_free_pdu (ha, pdu_entry);
		return(QLA_ERROR);
	}
	ha->isns_transaction_id++;
	return(QLA_SUCCESS);
}

uint8_t
qla4xxx_isns_dev_attr_qry_rsp(scsi_qla_host_t *ha,
			      uint8_t *buffer,
			      uint32_t buffer_size)
{
	uint8_t *last_iscsi_name = NULL;
	ISNS_DISCOVERED_TARGET *discovered_target = NULL;
	uint32_t isns_error;
	int i;
	uint8_t bIsTarget = 1;
	uint8_t bFound = 0;
	uint8_t status = QLA_SUCCESS;

	if (test_bit(ISNS_FLAG_SCN_RESTART, &ha->isns_flags)) {
		clear_bit(ISNS_FLAG_SCN_RESTART, &ha->isns_flags);
		ha->isns_num_discovered_targets = 0;
		if (qla4xxx_isns_dev_get_next(ha, NULL) != QLA_SUCCESS) {
			QL4PRINT(QLP2, printk("scsi%d: %s: qla4xxx_isns_dev_get_next failed\n",
					      ha->host_no, __func__));
			goto exit_qry_rsp_clear_flags;
		}
		goto exit_qry_rsp;
	}

	last_iscsi_name = kmalloc(256, GFP_ATOMIC);
	discovered_target = kmalloc(sizeof(*discovered_target), GFP_ATOMIC);
	if (!last_iscsi_name || !discovered_target) {
		QL4PRINT(QLP2, printk("scsi%d: %s: failed to allocate memory\n",
				      ha->host_no, __func__));
		status = QLA_ERROR;
		goto exit_qry_rsp;
	}

	memset(last_iscsi_name, 0, 256);
	memset(discovered_target, 0, sizeof(ISNS_DISCOVERED_TARGET));
	if (qla4xxx_isns_parse_query_response(ha, buffer, buffer_size,
					      &isns_error,
					      discovered_target,
					      &bIsTarget,
					      last_iscsi_name)
	    == QLA_SUCCESS) {

		if (bIsTarget &&
		    discovered_target->NameString[0] &&
		    discovered_target->NumPortals) {

			for (i = 0; i < ha->isns_num_discovered_targets; i++) {
				if (!strcmp(discovered_target->NameString,
					    ha->isns_disc_tgt_databasev[i].NameString)) {
					QL4PRINT(QLP2, printk("scsi%d: %s: found at index %x\n",
							      ha->host_no, __func__, i));
					memcpy(&ha->isns_disc_tgt_databasev[i],
					       discovered_target,
					       sizeof(ISNS_DISCOVERED_TARGET));
					ha->isns_disc_tgt_databasev[i] = *discovered_target;
					bFound = 1;
					break;
				}
			}
			if (!bFound && i < MAX_ISNS_DISCOVERED_TARGETS) {
				QL4PRINT(QLP20,
					 printk("scsi%d: %s: not already present, "
						"put in index %x\n",
						ha->host_no, __func__, i));
				memcpy(&ha->isns_disc_tgt_databasev[i],
				       discovered_target,
				       sizeof(ISNS_DISCOVERED_TARGET));
				ha->isns_num_discovered_targets++;
			}
		}
	}

	if (test_bit(ISNS_FLAG_QUERY_SINGLE_OBJECT, &ha->isns_flags)) {
		goto exit_qry_rsp_clear_flags;
	}
	else if (last_iscsi_name[0] == 0) {
		goto exit_qry_rsp_clear_flags;
	}
	else {
		if (qla4xxx_isns_dev_get_next (ha, last_iscsi_name) != QLA_SUCCESS) {
			QL4PRINT(QLP2,
				 printk("scsi%d: %s: "
					"qla4xxx_isns_dev_get_next failed\n",
					ha->host_no, __func__));
			goto exit_qry_rsp_clear_flags;
		}
	}

	goto exit_qry_rsp;

	exit_qry_rsp_clear_flags:
	ISNS_CLEAR_FLAGS(ha);

	exit_qry_rsp:
	if (last_iscsi_name) kfree(last_iscsi_name);
	if (discovered_target) kfree (discovered_target);
	return(status);
}

uint8_t
qla4xxx_isns_dev_get_next(scsi_qla_host_t *ha,
			  uint8_t *last_iscsi_name)
{
	PDU_ENTRY *pdu_entry;
	uint32_t packet_size;

	if ((pdu_entry = qla4xxx_get_pdu(ha, PAGE_SIZE)) == NULL) {
		QL4PRINT(QLP2, printk("scsi%d: %s:  qla4xxx_get_pdu failed\n",
				      ha->host_no, __func__));
		return(QLA_ERROR);
	}

	if (qla4xxx_isns_build_dev_get_next_packet (ha, pdu_entry->Buff,
						    pdu_entry->BuffLen,
						    last_iscsi_name,
						    &packet_size)
	    != QLA_SUCCESS) {
		QL4PRINT(QLP2, printk("scsi%d: %s:  qla4xxx_isns_build_dev_get_next_packet failed\n",
				      ha->host_no, __func__));
		qla4xxx_free_pdu (ha, pdu_entry);
		return(QLA_ERROR);
	}

	pdu_entry->SendBuffLen = packet_size;
	pdu_entry->RecvBuffLen = pdu_entry->BuffLen;

	QL4PRINT(QLP20, printk("---------------------------\n"));
	QL4PRINT(QLP20, printk("scsi%d: %s:                     sending  %d DevGetNext\n",
			       ha->host_no, __func__, ha->isns_transaction_id));
	QL4PRINT(QLP19, printk("PDU (0x%p) 0x%x ->\n", pdu_entry->Buff, pdu_entry->SendBuffLen));
	qla4xxx_dump_bytes(QLP19, pdu_entry->Buff, pdu_entry->SendBuffLen);

	if (qla4xxx_send_passthru0_iocb(
		ha, ISNS_DEVICE_INDEX,
					ISNS_DEFAULT_SERVER_CONN_ID,
					pdu_entry->DmaBuff,
					pdu_entry->SendBuffLen,
					pdu_entry->RecvBuffLen,
					PT_FLAG_ISNS_PDU | PT_FLAG_WAIT_4_RESPONSE,
					qla4xxx_isns_build_iocb_handle(ha, ISNS_REQ_RSP_PDU, pdu_entry))
	    != QLA_SUCCESS) {

		QL4PRINT(QLP2, printk("scsi%d: %s:  qla4xxx_send_passthru0_iocb failed\n",
				      ha->host_no, __func__));
		qla4xxx_free_pdu (ha, pdu_entry);
		return(QLA_ERROR);
	}
	ha->isns_transaction_id++;
	return(QLA_SUCCESS);
}

uint8_t
qla4xxx_isns_dev_get_next_rsp(scsi_qla_host_t *ha,
			      uint8_t *buffer,
			      uint32_t buffer_size)
{
	uint32_t isns_error = 0;
	uint8_t bIsTarget;
	static uint8_t last_iscsi_name[256];

	if (test_bit(ISNS_FLAG_SCN_RESTART, &ha->isns_flags)) {
		clear_bit(ISNS_FLAG_SCN_RESTART, &ha->isns_flags);
		ha->isns_num_discovered_targets = 0;
		if (qla4xxx_isns_dev_get_next(ha, NULL) != QLA_SUCCESS) {
			QL4PRINT(QLP2, printk("scsi%d: %s: qla4xxx_isns_dev_get_next failed\n",
					      ha->host_no, __func__));
			goto exit_get_next_rsp;
		}
		return(QLA_SUCCESS);
	}

	if (qla4xxx_isns_parse_get_next_response(ha, buffer, buffer_size,
						 &isns_error, &last_iscsi_name[0],
						 sizeof(last_iscsi_name) - 1,
						 &bIsTarget)
	    != QLA_SUCCESS) {
		if (isns_error != ISNS_ERR_NO_SUCH_ENTRY) {
			QL4PRINT(QLP2, printk("scsi%d: %s: qla4xxx_isns_parse_get_next_response failed\n",
					      ha->host_no, __func__));
		}
		goto exit_get_next_rsp;
	}

    #if 1
	if (bIsTarget) {
		if (qla4xxx_isns_dev_attr_qry(ha, &last_iscsi_name[0]) != QLA_SUCCESS) {
			QL4PRINT(QLP2, printk("scsi%d: %s: qla4xxx_isns_dev_attr_qry failed\n",
					      ha->host_no, __func__));
			goto exit_get_next_rsp;
		}
	}
	else {
		if (qla4xxx_isns_dev_get_next(ha, &last_iscsi_name[0]) != QLA_SUCCESS) {
			QL4PRINT(QLP2, printk("scsi%d: %s: qla4xxx_isns_dev_get_next failed\n",
					      ha->host_no, __func__));
			goto exit_get_next_rsp;
		}
	}
    #else
	if (qla4xxx_isns_dev_attr_qry(ha, &last_iscsi_name[0]) != QLA_SUCCESS) {
		QL4PRINT(QLP2, printk("scsi%d: %s: qla4xxx_isns_dev_attr_qry failed\n",
				      ha->host_no, __func__));
		goto exit_get_next_rsp;
	}
    #endif

	return(QLA_SUCCESS);

	exit_get_next_rsp:
	clear_bit(ISNS_FLAG_SCN_IN_PROGRESS, &ha->isns_flags);
	clear_bit(ISNS_FLAG_SCN_RESTART, &ha->isns_flags);
	return(QLA_SUCCESS);
}

uint8_t
qla4xxx_isns_dev_dereg(scsi_qla_host_t *ha)
{
	PDU_ENTRY *pdu_entry;
	uint32_t packet_size;

	if ((pdu_entry = qla4xxx_get_pdu (ha, PAGE_SIZE)) == NULL) {
		QL4PRINT(QLP2, printk("scsi%d: %s:  qla4xxx_get_pdu failed\n",
				      ha->host_no, __func__));
		return(QLA_ERROR);
	}

	if (qla4xxx_isns_build_deregistration_packet(ha, pdu_entry->Buff,
						     pdu_entry->BuffLen,
						     ha->isns_entity_id,
						     ha->isns_ip_address,
						     ha->isns_server_port_number,
						     &packet_size)
	    != QLA_SUCCESS) {
		QL4PRINT(QLP2,
			 printk("scsi%d: %s:  QLiSNSBuildDeregistrationPacket "
				"failed\n", ha->host_no, __func__));
		qla4xxx_free_pdu (ha, pdu_entry);
		return(QLA_ERROR);
	}

	pdu_entry->SendBuffLen = packet_size;
	pdu_entry->RecvBuffLen = pdu_entry->BuffLen;

	QL4PRINT(QLP20, printk("---------------------------\n"));
	QL4PRINT(QLP20,
		 printk("scsi%d: %s:                       sending  %d DevDereg\n",
			ha->host_no, __func__, ha->isns_transaction_id));
	QL4PRINT(QLP19, printk("PDU (0x%p) 0x%x ->\n", pdu_entry->Buff, pdu_entry->SendBuffLen));
	qla4xxx_dump_bytes(QLP19, pdu_entry->Buff, pdu_entry->SendBuffLen);

	if (qla4xxx_send_passthru0_iocb(
		ha, ISNS_DEVICE_INDEX,
					ISNS_DEFAULT_SERVER_CONN_ID,
					pdu_entry->DmaBuff,
					pdu_entry->SendBuffLen,
					pdu_entry->RecvBuffLen,
					PT_FLAG_ISNS_PDU | PT_FLAG_WAIT_4_RESPONSE,
					qla4xxx_isns_build_iocb_handle(ha, ISNS_REQ_RSP_PDU, pdu_entry))
	    != QLA_SUCCESS) {

		QL4PRINT(QLP2, printk("scsi%d: %s:  qla4xxx_send_passthru0_iocb failed\n",
				      ha->host_no, __func__));
		qla4xxx_free_pdu(ha, pdu_entry);
		return(QLA_ERROR);
	}
	ha->isns_transaction_id++;
	return(QLA_SUCCESS);
}


uint8_t
qla4xxx_isns_dev_dereg_rsp(scsi_qla_host_t *ha,
			   uint8_t *buffer,
			   uint32_t buffer_size)
{
	ISNSP_MESSAGE_HEADER * isns_message;
	ISNSP_RESPONSE_HEADER * isns_response;

	isns_message = (ISNSP_MESSAGE_HEADER *) buffer;
	isns_response = (ISNSP_RESPONSE_HEADER *) &isns_message->payload[0];

	clear_bit(ISNS_FLAG_ISNS_SRV_REGISTERED, &ha->isns_flags);

	if (be32_to_cpu(isns_response->error_code)) {
		QL4PRINT(QLP10, printk("scsi%d: %s: iSNS SCNDereg rsp code %x\n",
				       ha->host_no, __func__,
				       be32_to_cpu(isns_response->error_code)));
	}

	if (test_bit(ISNS_FLAG_REREGISTER, &ha->isns_flags)) {
		clear_bit(ISNS_FLAG_REREGISTER, &ha->isns_flags);

		if (qla4xxx_isns_dev_attr_reg(ha) != QLA_SUCCESS) {
			QL4PRINT(QLP2, printk("scsi%d: %s: qla4xxx_isns_dev_attr_reg failed\n",
					      ha->host_no, __func__));
			return(QLA_ERROR);
		}
	}
	return(QLA_SUCCESS);
}

uint8_t
qla4xxx_isns_scn_dereg(scsi_qla_host_t *ha)
{
	PDU_ENTRY *pdu_entry;
	uint32_t packet_size;

	if ((pdu_entry = qla4xxx_get_pdu(ha, PAGE_SIZE)) == NULL) {
		QL4PRINT(QLP2, printk("scsi%d: %s:  qla4xxx_get_pdu failed\n",
				      ha->host_no, __func__));
		return(QLA_ERROR);
	}

	if (qla4xxx_isns_build_scn_deregistration_packet(ha, pdu_entry->Buff,
							 pdu_entry->BuffLen,
							 &packet_size)
	    != QLA_SUCCESS) {
		QL4PRINT(QLP2, printk("scsi%d: %s:  qla4xxx_isns_build_scn_"
				      "deregistration_packet failed\n",
				      ha->host_no, __func__));
		qla4xxx_free_pdu (ha, pdu_entry);
		return(QLA_ERROR);
	}

	pdu_entry->SendBuffLen = packet_size;
	pdu_entry->RecvBuffLen = pdu_entry->BuffLen;

	QL4PRINT(QLP20, printk("---------------------------\n"));
	QL4PRINT(QLP20, printk("scsi%d: %s:                       sending  %d SCNDereg\n",
			       ha->host_no, __func__, ha->isns_transaction_id));
	QL4PRINT(QLP19, printk("PDU (0x%p) 0x%x ->\n", pdu_entry->Buff, pdu_entry->SendBuffLen));
	qla4xxx_dump_bytes(QLP19, pdu_entry->Buff, pdu_entry->SendBuffLen);

	clear_bit(ISNS_FLAG_DEV_SCAN_DONE, &ha->isns_flags);

	if (qla4xxx_send_passthru0_iocb(ha, ISNS_DEVICE_INDEX,
					 ISNS_DEFAULT_SERVER_CONN_ID,
					 pdu_entry->DmaBuff,
					 pdu_entry->SendBuffLen,
					 pdu_entry->RecvBuffLen,
					 PT_FLAG_ISNS_PDU | PT_FLAG_WAIT_4_RESPONSE,
					 qla4xxx_isns_build_iocb_handle (ha, ISNS_REQ_RSP_PDU, pdu_entry))
	    != QLA_SUCCESS) {

		QL4PRINT(QLP2, printk("scsi%d: %s:  qla4xxx_send_passthru0_iocb "
				      "failed\n", ha->host_no, __func__));
		qla4xxx_free_pdu (ha, pdu_entry);
		return(QLA_ERROR);
	}
	ha->isns_transaction_id++;
	return(QLA_SUCCESS);
}

uint8_t
qla4xxx_isns_scn_dereg_rsp(scsi_qla_host_t *ha,
			   uint8_t *buffer,
			   uint32_t buffer_size)
{
	ISNSP_MESSAGE_HEADER *isns_message;
	ISNSP_RESPONSE_HEADER *isns_response;

	isns_message = (ISNSP_MESSAGE_HEADER *) buffer;
	isns_response = (ISNSP_RESPONSE_HEADER *) &isns_message->payload[0];

	clear_bit(ISNS_FLAG_ISNS_SCN_REGISTERED, &ha->isns_flags);

	if (be32_to_cpu(isns_response->error_code)) {
		QL4PRINT(QLP10, printk("scsi%d: %s: iSNS SCNDereg rsp code %x\n",
				       ha->host_no, __func__,
				       be32_to_cpu(isns_response->error_code)));
	}

	if (test_bit(ISNS_FLAG_REREGISTER, &ha->isns_flags)) {
		if (qla4xxx_isns_dev_dereg(ha) != QLA_SUCCESS) {
			QL4PRINT(QLP2, printk("scsi%d: %s: QLiSNSDevDereg failed\n",
					      ha->host_no, __func__));
			return(QLA_ERROR);
		}
	}
	return(QLA_SUCCESS);
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
