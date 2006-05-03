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
 ******************************************************************************/
#define ISNSP_VERSION           0x0001  // Current iSNS version as defined by
// the latest spec that we support

/* Swap Macros
 *
 * These are designed to be used on constants (such as the function codes
 * below) such that the swapping is done by the compiler at compile time
 * and not at run time.  Of course, they should also work on variables
 * in which case the swapping will occur at run time.
 */
#define WSWAP(x) (uint16_t)(((((uint16_t)x)<<8)&0xFF00) | \
                            ((((uint16_t)x)>>8)&0x00FF))
#define DWSWAP(x) (uint32_t)(((((uint32_t)x)<<24)&0xFF000000) | \
		             ((((uint32_t)x)<<8)&0x00FF0000) |  \
		             ((((uint32_t)x)>>8)&0x0000FF00) |  \
		             ((((uint32_t)x)>>24)&0x000000FF))

/*
 * Timeout Values
 *******************/
#define ISNS_RESTART_TOV	5

#define IOCB_ISNS_PT_PDU_TYPE(x)                      ((x) & 0x0F000000)
#define IOCB_ISNS_PT_PDU_INDEX(x)                     ((x) & (MAX_PDU_ENTRIES-1))

#define ISNS_ASYNCH_REQ_PDU                           0x01000000
#define ISNS_ASYNCH_RSP_PDU                           0x02000000
#define ISNS_REQ_RSP_PDU                              0x03000000


// Fake device indexes.  Used internally by the driver for indexing to other than a DDB entry
#define ISNS_DEVICE_INDEX                             MAX_DEV_DB_ENTRIES + 0

#define ISNS_CLEAR_FLAGS(ha) do {clear_bit(ISNS_FLAG_SCN_IN_PROGRESS |    \
                                       ISNS_FLAG_SCN_RESTART | 	      \
				       ISNS_FLAG_QUERY_SINGLE_OBJECT, \
				       &ha->isns_flags);} while(0);



// iSNS Message Function ID codes

#define ISNS_FCID_DevAttrReg      0x0001      // Device Attribute Registration Request
#define ISNS_FCID_DevAttrQry      0x0002      // Device Attribute Query Request
#define ISNS_FCID_DevGetNext      0x0003      // Device Get Next Request
#define ISNS_FCID_DevDereg        0x0004      // Device Deregister Request
#define ISNS_FCID_SCNReg          0x0005      // SCN Register Request
#define ISNS_FCID_SCNDereg        0x0006      // SCN Deregister Request
#define ISNS_FCID_SCNEvent        0x0007      // SCN Event
#define ISNS_FCID_SCN             0x0008      // State Change Notification
#define ISNS_FCID_DDReg           0x0009      // DD Register
#define ISNS_FCID_DDDereg         0x000A      // DD Deregister
#define ISNS_FCID_DDSReg          0x000B      // DDS Register
#define ISNS_FCID_DDSDereg        0x000C      // DDS Deregister
#define ISNS_FCID_ESI             0x000D      // Entity Status Inquiry
#define ISNS_FCID_Heartbeat       0x000E      // Name Service Heartbeat
//NOT USED              0x000F-0x0010
#define ISNS_FCID_RqstDomId       0x0011      // Request FC_DOMAIN_ID
#define ISNS_FCID_RlseDomId       0x0012      // Release FC_DOMAIN_ID
#define ISNS_FCID_GetDomId        0x0013      // Get FC_DOMAIN_IDs
//RESERVED              0x0014-0x00FF
//Vendor Specific       0x0100-0x01FF
//RESERVED              0x0200-0x8000


// iSNS Response Message Function ID codes

#define ISNS_FCID_DevAttrRegRsp   0x8001      // Device Attribute Registration Response
#define ISNS_FCID_DevAttrQryRsp   0x8002      // Device Attribute Query Response
#define ISNS_FCID_DevGetNextRsp   0x8003      // Device Get Next Response
#define ISNS_FCID_DevDeregRsp     0x8004      // Deregister Device Response
#define ISNS_FCID_SCNRegRsp       0x8005      // SCN Register Response
#define ISNS_FCID_SCNDeregRsp     0x8006      // SCN Deregister Response
#define ISNS_FCID_SCNEventRsp     0x8007      // SCN Event Response
#define ISNS_FCID_SCNRsp          0x8008      // SCN Response
#define ISNS_FCID_DDRegRsp        0x8009      // DD Register Response
#define ISNS_FCID_DDDeregRsp      0x800A      // DD Deregister Response
#define ISNS_FCID_DDSRegRsp       0x800B      // DDS Register Response
#define ISNS_FCID_DDSDeregRsp     0x800C      // DDS Deregister Response
#define ISNS_FCID_ESIRsp          0x800D      // Entity Status Inquiry Response
//NOT USED              0x800E-0x8010
#define ISNS_FCID_RqstDomIdRsp    0x8011      // Request FC_DOMAIN_ID Response
#define ISNS_FCID_RlseDomIdRsp    0x8012      // Release FC_DOMAIN_ID Response
#define ISNS_FCID_GetDomIdRsp     0x8013      // Get FC_DOMAIN_IDs Response
//RESERVED              0x8014-0x80FF
//Vendor Specific       0x8100-0x81FF
//RESERVED              0x8200-0xFFFF


// iSNS Error Codes

#define ISNS_ERR_SUCCESS                    0   // Successful
#define ISNS_ERR_UNKNOWN                    1   // Unknown Error
#define ISNS_ERR_MSG_FORMAT                 2   // Message Format Error
#define ISNS_ERR_INVALID_REG                3   // Invalid Registration
//RESERVED                                  4
#define ISNS_ERR_INVALID_QUERY              5   // Invalid Query
#define ISNS_ERR_SOURCE_UNKNOWN             6   // Source Unknown
#define ISNS_ERR_SOURCE_ABSENT              7   // Source Absent
#define ISNS_ERR_SOURCE_UNAUTHORIZED        8   // Source Unauthorized
#define ISNS_ERR_NO_SUCH_ENTRY              9   // No Such Entry
#define ISNS_ERR_VER_NOT_SUPPORTED          10  // Version Not Supported
#define ISNS_ERR_INTERNAL_ERROR             11  // Internal Error
#define ISNS_ERR_BUSY                       12  // Busy
#define ISNS_ERR_OPT_NOT_UNDERSTOOD         13  // Option Not Understood
#define ISNS_ERR_INVALID_UPDATE             14  // Invalid Update
#define ISNS_ERR_MSG_NOT_SUPPORTED          15  // Message (FUNCTION_ID) Not Supported
#define ISNS_ERR_SCN_EVENT_REJECTED         16  // SCN Event Rejected
#define ISNS_ERR_SCN_REG_REJECTED           17  // SCN Registration Rejected
#define ISNS_ERR_ATTR_NOT_IMPLEMENTED       18  // Attribute Not Implemented
#define ISNS_ERR_FC_DOMAIN_ID_NOT_AVAIL     19  // FC_DOMAIN_ID Not Available
#define ISNS_ERR_FC_DOMAIN_ID_NOT_ALLOC     20  // FC_DOMAIN_ID Not Allocated
#define ISNS_ERR_ESI_NOT_AVAILABLE          21  // ESI Not Available
#define ISNS_ERR_INVALID_DEREG              22  // Invalid Deregistration
#define ISNS_ERR_REG_FEATURES_NOT_SUPPORTED 23  // Registration Features Not Supported

#define ISNS_ERROR_CODE_TBL()	{  \
	"SUCCESSFUL"	        		, \
	"UNKNOWN ERROR"	        		, \
	"MESSAGE FORMAT ERROR"	        	, \
	"INVALID REGISTRATION"	        	, \
	"RESERVED"	        		, \
	"INVALID QUERY"	        		, \
	"SOURCE UNKNOWN"	        	, \
	"SOURCE ABSENT"	        		, \
	"SOURCE UNAUTHORIZED"	        	, \
	"NO SUCH ENTRY"	        		, \
	"VERSION NOT SUPPORTED"	        	, \
	"INTERNAL ERROR"	        	, \
	"BUSY"	        			, \
	"OPTION NOT UNDERSTOOD"	        	, \
	"INVALID UPDATE"	        	, \
	"MESSAGE (FUNCTION_ID) NOT SUPPORTED"	, \
	"SCN EVENT REJECTED"	        	, \
	"SCN REGISTRATION REJECTED"	        , \
	"ATTRIBUTE NOT IMPLEMENTED"	        , \
	"FC_DOMAIN_ID NOT AVAILABLE"	        , \
	"FC_DOMAIN_ID NOT ALLOCATED"	        , \
	"ESI NOT AVAILABLE"	        	, \
	"INVALID DEREGISTRATION"	        , \
	"REGISTRATION FEATURES NOT SUPPORTED"	, \
	NULL			  \
}


// iSNS Protocol Structures

typedef struct {
	uint16_t isnsp_version;
	uint16_t function_id;
	uint16_t pdu_length;   // Length of the payload (does not include header)
	uint16_t flags;
	uint16_t transaction_id;
	uint16_t sequence_id;
	uint8_t payload[0];   // Variable payload data
} ISNSP_MESSAGE_HEADER, *PISNSP_MESSAGE_HEADER;

typedef struct {
	uint32_t error_code;
	uint8_t attributes[0];
} ISNSP_RESPONSE_HEADER, *PISNSP_RESPONSE_HEADER;


// iSNS Message Flags Definitions

#define ISNSP_CLIENT_SENDER         0x8000
#define ISNSP_SERVER_SENDER         0x4000
#define ISNSP_AUTH_BLOCK_PRESENT    0x2000
#define ISNSP_REPLACE_FLAG          0x1000
#define ISNSP_LAST_PDU              0x0800
#define ISNSP_FIRST_PDU             0x0400

#define ISNSP_VALID_FLAGS_MASK  (ISNSP_CLIENT_SENDER | \
                                 ISNSP_SERVER_SENDER | \
                                 ISNSP_AUTH_BLOCK_PRESENT | \
                                 ISNSP_REPLACE_FLAG | \
                                 ISNSP_LAST_PDU | \
                                 ISNSP_FIRST_PDU)


// iSNS Attribute Structure

typedef struct {
	uint32_t tag;
	uint32_t length;
	uint8_t value[0];     // Variable length data
} ISNS_ATTRIBUTE, *PISNS_ATTRIBUTE;




// The following macro assumes that the attribute is wholly contained within
// the buffer in question and is valid (see VALIDATE_ATTR below).

static inline PISNS_ATTRIBUTE
NEXT_ATTR(PISNS_ATTRIBUTE pattr)
{
	return (PISNS_ATTRIBUTE) (&pattr->value[0] + be32_to_cpu(pattr->length));
}

static inline uint8_t
VALIDATE_ATTR(PISNS_ATTRIBUTE PAttr, uint8_t *buffer_end)
{
	// Ensure that the Length field of the current attribute is contained
	// within the buffer before trying to read it, and then be sure that
	// the entire attribute is contained within the buffer.

	if ((((unsigned long)&PAttr->length + sizeof(PAttr->length)) <= (unsigned long)buffer_end) &&
	    (unsigned long)NEXT_ATTR(PAttr) <= (unsigned long)buffer_end) {
		return(1);
	}

	return(0);
}


// iSNS-defined Attribute Tags

#define ISNS_ATTR_TAG_DELIMITER                     0
#define ISNS_ATTR_TAG_ENTITY_IDENTIFIER             1
#define ISNS_ATTR_TAG_ENTITY_PROTOCOL               2
#define ISNS_ATTR_TAG_MGMT_IP_ADDRESS               3
#define ISNS_ATTR_TAG_TIMESTAMP                     4
#define ISNS_ATTR_TAG_PROTOCOL_VERSION_RANGE        5
#define ISNS_ATTR_TAG_REGISTRATION_PERIOD           6
#define ISNS_ATTR_TAG_ENTITY_INDEX                  7
#define ISNS_ATTR_TAG_ENTITY_NEXT_INDEX             8
#define ISNS_ATTR_TAG_ENTITY_ISAKMP_PHASE_1         11
#define ISNS_ATTR_TAG_ENTITY_CERTIFICATE            12
#define ISNS_ATTR_TAG_PORTAL_IP_ADDRESS             16
#define ISNS_ATTR_TAG_PORTAL_PORT                   17
#define ISNS_ATTR_TAG_PORTAL_SYMBOLIC_NAME          18
#define ISNS_ATTR_TAG_ESI_INTERVAL                  19
#define ISNS_ATTR_TAG_ESI_PORT                      20
#define ISNS_ATTR_TAG_PORTAL_GROUP                  21
#define ISNS_ATTR_TAG_PORTAL_INDEX                  22
#define ISNS_ATTR_TAG_SCN_PORT                      23
#define ISNS_ATTR_TAG_PORTAL_NEXT_INDEX             24
#define ISNS_ATTR_TAG_PORTAL_SECURITY_BITMAP        27
#define ISNS_ATTR_TAG_PORTAL_ISAKMP_PHASE_1         28
#define ISNS_ATTR_TAG_PORTAL_ISAKMP_PHASE_2         29
#define ISNS_ATTR_TAG_PORTAL_CERTIFICATE            31
#define ISNS_ATTR_TAG_ISCSI_NAME                    32
#define ISNS_ATTR_TAG_ISCSI_NODE_TYPE               33
#define ISNS_ATTR_TAG_ISCSI_ALIAS                   34
#define ISNS_ATTR_TAG_ISCSI_SCN_BITMAP              35
#define ISNS_ATTR_TAG_ISCSI_NODE_INDEX              36
#define ISNS_ATTR_TAG_WWNN_TOKEN                    37
#define ISNS_ATTR_TAG_ISCSI_NODE_NEXT_INDEX         38
#define ISNS_ATTR_TAG_ISCSI_AUTH_METHOD             42
#define ISNS_ATTR_TAG_ISCSI_NODE_CERTIFICATE        43
#define ISNS_ATTR_TAG_PG_TAG                        48
#define ISNS_ATTR_TAG_PG_ISCSI_NAME                 49
#define ISNS_ATTR_TAG_PG_PORTAL_IP_ADDRESS          50
#define ISNS_ATTR_TAG_PG_PORTAL_PORT                51
#define ISNS_ATTR_TAG_PG_INDEX                      52
#define ISNS_ATTR_TAG_PG_NEXT_INDEX                 53
#define ISNS_ATTR_TAG_FC_PORT_NAME_WWPN             64
#define ISNS_ATTR_TAG_PORT_ID                       65
#define ISNS_ATTR_TAG_FC_PORT_TYPE                  66
#define ISNS_ATTR_TAG_SYMBOLIC_PORT_NAME            67
#define ISNS_ATTR_TAG_FABRIC_PORT_NAME              68
#define ISNS_ATTR_TAG_HARD_ADDRESS                  69
#define ISNS_ATTR_TAG_PORT_IP_ADDRESS               70
#define ISNS_ATTR_TAG_CLASS_OF_SERVICE              71
#define ISNS_ATTR_TAG_FC4_TYPES                     72
#define ISNS_ATTR_TAG_FC4_DESCRIPTOR                73
#define ISNS_ATTR_TAG_FC4_FEATURES                  74
#define ISNS_ATTR_TAG_IFCP_SCN_BITMAP               75
#define ISNS_ATTR_TAG_PORT_ROLE                     76
#define ISNS_ATTR_TAG_PERMANENT_PORT_NAME           77
#define ISNS_ATTR_TAG_PORT_CERTIFICATE              83
#define ISNS_ATTR_TAG_FC4_TYPE_CODE                 95
#define ISNS_ATTR_TAG_FC_NODE_NAME_WWNN             96
#define ISNS_ATTR_TAG_SYMBOLIC_NODE_NAME            97
#define ISNS_ATTR_TAG_NODE_IP_ADDRESS               98
#define ISNS_ATTR_TAG_NODE_IPA                      99
#define ISNS_ATTR_TAG_NODE_CERTIFICATE              100
#define ISNS_ATTR_TAG_PROXY_ISCSI_NAME              101
#define ISNS_ATTR_TAG_SWITCH_NAME                   128
#define ISNS_ATTR_TAG_PREFERRED_ID                  129
#define ISNS_ATTR_TAG_ASSIGNED_ID                   130
#define ISNS_ATTR_TAG_VIRTUAL_FABRIC_ID             131
#define ISNS_ATTR_TAG_VENDOR_OUI                    256
//Vendor-specific iSNS Server                       257-384
//Vendor-specific Entity                            385-512
//Vendor-specific Portal                            513-640
//Vendor-specific iSCSI Node                        641-768
//Vendor-specific FC Port Name                      769-896
//Vendor-specific FC Node Name                      897-1024
//Vendor-specific DDS                               1025-1280
//Vendor-Specific DD                                1281-1536
//Vendor-specific (other)                           1237-2048
#define ISNS_ATTR_TAG_DD_SET_ID                     2049
#define ISNS_ATTR_TAG_DD_SET_SYMBOLIC_NAME          2050
#define ISNS_ATTR_TAG_DD_SET_STATUS                 2051
#define ISNS_ATTR_TAG_DD_SET_NEXT_ID                2052
#define ISNS_ATTR_TAG_DD_ID                         2065
#define ISNS_ATTR_TAG_DD_SYMBOLIC_NAME              2066
#define ISNS_ATTR_TAG_DD_MEMBER_ISCSI_INDEX         2067
#define ISNS_ATTR_TAG_DD_MEMBER_ISCSI_NAME          2068
#define ISNS_ATTR_TAG_DD_MEMBER_IFCP_NODE           2069
#define ISNS_ATTR_TAG_DD_MEMBER_PORTAL_INDEX        2070
#define ISNS_ATTR_TAG_DD_MEMBER_PORTAL_IP_ADDRESS   2071
#define ISNS_ATTR_TAG_DD_MEMBER_PORTAL_PORT         2072
#define ISNS_ATTR_TAG_DD_FEATURES                   2078
#define ISNS_ATTR_TAG_DD_ID_NEXT_ID                 2079


// Definitions used for Entity Protocol

#define ENTITY_PROTOCOL_NEUTRAL                 1
#define ENTITY_PROTOCOL_ISCSI                   2
#define ENTITY_PROTOCOL_IFCP                    3


// Definitions used for iSCSI Node Type

#define ISCSI_NODE_TYPE_TARGET                  0x00000001
#define ISCSI_NODE_TYPE_INITIATOR               0x00000002
#define ISCSI_NODE_TYPE_CONTROL                 0x00000004


// Definitions used for iSCSI Node SCN Bitmap

#define ISCSI_SCN_DD_DDS_MEMBER_ADDED           0x00000001  // Management SCN only
#define ISCSI_SCN_DD_DDS_MEMBER_REMOVED         0x00000002  // Management SCN only
#define ISCSI_SCN_OBJECT_UPDATED                0x00000004
#define ISCSI_SCN_OBJECT_ADDED                  0x00000008
#define ISCSI_SCN_OBJECT_REMOVED                0x00000010
#define ISCSI_SCN_MANAGEMENT_SCN                0x00000020
#define ISCSI_SCN_TARGET_AND_SELF_INFO_ONLY     0x00000040
#define ISCSI_SCN_INITIATOR_AND_SELF_INFO_ONLY  0x00000080

#define ISCSI_SCN_OBJECT_MASK                   (ISCSI_SCN_OBJECT_UPDATED |     \
                                                 ISCSI_SCN_OBJECT_ADDED |       \
                                                 ISCSI_SCN_OBJECT_REMOVED)


// Definitions used for iSCSI Security Bitmap

#define ISNS_SECURITY_BITMAP_VALID              0x00000001
#define ISNS_SECURITY_IKE_IPSEC_ENABLED         0x00000002
#define ISNS_SECURITY_MAIN_MODE_ENABLED         0x00000004
#define ISNS_SECURITY_AGGRESSIVE_MODE_ENABLED   0x00000008
#define ISNS_SECURITY_PFS_ENABLED               0x00000010
#define ISNS_SECURITY_TRANSPORT_MODE_PREFERRED  0x00000020
#define ISNS_SECURITY_TUNNEL_MODE_PREFERRED     0x00000040


// Definitions used for Portal Port

#define PORTAL_PORT_NUMBER_MASK                 0x0000FFFF
#define PORTAL_PORT_TYPE_UDP                    0x00010000
