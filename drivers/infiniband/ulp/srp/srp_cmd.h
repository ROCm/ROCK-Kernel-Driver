/*
  This software is available to you under a choice of one of two
  licenses.  You may choose to be licensed under the terms of the GNU
  General Public License (GPL) Version 2, available at
  <http://www.fsf.org/copyleft/gpl.html>, or the OpenIB.org BSD
  license, available in the LICENSE.TXT file accompanying this
  software.  These details are also available at
  <http://openib.org/license.html>.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
  NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
  BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
  ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
  CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.

  Copyright (c) 2004 Topspin Communications.  All rights reserved.

  $Id: srp_cmd.h 33 2004-04-09 03:58:41Z roland $
*/

#ifndef _SRP_CMD_
#define _SRP_CMD_

#define SRP_SERVICE_ID 102

/*
 * SRP Command opcodes
 */
#define SRP_LOGIN_REQ   0x00
#define SRP_TSK_MGT 	0x01
#define SRP_CMD	    	0x02
#define SRP_I_LOGOUT	0x03
#define SRP_LOGIN_RESP	0xc0
#define SRP_RSP	    	0xc1
#define SRP_LOGIN_RJT	0xc2
#define SRP_T_LOGOUT	0x80
#define SRP_TPAR_REQ	0x81
#define SRP_AER_REQ 	0x82
#define SRP_TPAR_RESP	0x41
#define SRP_AER_RSP	    0x42

#define SCSI_REPORT_LUN 0xa0
#define SCSI_READ       0x28
#define SCSI_WRITE      0x2A
#define SCSI_TUR        0x00
#define SCSI_INQUIRY    0x12
#define MAX_SRP_LUN     255

/*
 * SCSI status codes
 */
#define STATUS_GOOD                       0x00
#define STATUS_CHECK_CONDITION            0x02
#define STATUS_CONDITION_MET              0x04
#define STATUS_BUSY                       0x08
#define STATUS_INTERMEDIATE               0x10
#define STATUS_INTERMEDIATE_CONDITION_MET 0x14
#define STATUS_RESERVATION_CONFLICT       0x18
#define STATUS_TASK_SET_FULL              0x28
#define STATUS_ACA_ACTIVE                 0x30
#define STATUS_TASK_ABORTED               0X40

/*
 * SRP LOGIN REJ Reason Code
 */
#define SRP_UNK_ERROR   	0x00010000
#define SRP_NO_CHAN	    	0x00010001
#define SRP_LARGE_IU    	0x00010002
#define SRP_NO_RDMA	    	0x00010003
#define SRP_NO_INDSUP		0x00010004
#define SRP_NO_MULTICHAN	0x00010005

/*
 * SRP LOGIN Multi-Channel  Action Code
 */
#define SRP_MULTICHAN_ERR	0x00
#define SRP_MULTICHAN_INDP	0x01

/*
 * SRP TASK Managment Flags
 */
#define SRP_ABORT_TASK		0x01
#define SRP_ABORT_TASK_SET	0x02
#define SRP_CLEAR_TASK_SET	0x04
#define SRP_LUN_RESET		0x08
#define SRP_TARGET_RESET	0x00
#define SRP_CLEAR_ACA		0x40

/*
 * SRP Cmd TASK Attribut Flags
 */
#define SRP_CMD_SIMPLE_Q	0x00
#define SRP_CMD_HEAD_OF_Q	0x01
#define SRP_CMD_ORDERED_Q	0x02
#define SRP_CMD_ACA		    0x04
/*
 * SRP Cmd Response Code
 */
#define NO_FAILURE              0x00
#define REQUEST_FIELD_INVLD     0x02
#define TSK_MGT_FUN_NOT_SUP     0x04
#define TSK_MGT_FAILED          0x05
#pragma pack(1)
/*
 *	SRP commands definations
 */

typedef struct buf_format_s {
	tUINT8   rsvd0;
#if defined(__LITTLE_ENDIAN_BITFIELD)
	tUINT8   rsvd_b0:1,
        	   	ddbd:1,
               	idbd:1,
	         rsvd_b3:5;
#elif defined (__BIG_ENDIAN_BITFIELD)
	tUINT8      rsvd_b3:5,
       	   	       idbd:1,
	   	           ddbd:1,
	   	        rsvd_b0:1;
#else
#error  "Please fix <asm/byteorder.h>"
#endif
}buf_format_t;

typedef
struct _srp_iu {
    tUINT8 opcode;
    tUINT8 rsvd[7];
    tUINT64 tag;
} srp_iu_t;

/*
 * Remote buffer header
 */
typedef struct srp_remote_buf_s {
    uint64_t r_data;            /* physical address on the remote node */
    int32_t r_key;              /* R_Key */
    unsigned int r_size;        /* size of remote buffer */
} srp_remote_buf_t;

typedef struct _srp_login_req_t {
	tUINT8	opcode;
	tUINT8	rsvd[7];
	tUINT64	tag;
	 int	request_I_T_IU;
	tUINT8	rsvd1[4];
	buf_format_t	req_buf_format;
#if defined(__LITTLE_ENDIAN_BITFIELD)
	tUINT8	multi_chan:2,
		rsvd3:6;
#elif defined (__BIG_ENDIAN_BITFIELD)
	tUINT8	rsvd3:6,
		multi_chan:2;
#endif
	tUINT8	rsvd4;
	tUINT8	rsvd5[4];
	tUINT8	initiator_port_id[16];
	tUINT8	target_port_id[16];
}srp_login_req_t;

typedef
struct _srp_login_resp_t {
	tUINT8	opcode;
	tUINT8	rsvd[3];
	tUINT32	request_limit_delta;
	tUINT64	tag;
	tUINT32	request_I_T_IU;
	tUINT32	request_T_I_IU;
	buf_format_t	sup_buf_format;
#if defined(__LITTLE_ENDIAN_BITFIELD)
	tUINT8	multi_chan:2,
		rsvd3:6;
#elif defined (__BIG_ENDIAN_BITFIELD)
	tUINT8	rsvd3:6,
		multi_chan:2;
#endif
	tUINT8	rsvd4[25];
}srp_login_resp_t;

typedef
struct _srp_login_rej {
	tUINT8	opcode;
	tUINT8	rsvd[3];
	tUINT32	reason_code;
	tUINT64	tag;
	tUINT8	rsvd1[8];
	buf_format_t	sup_buf_format;
	tUINT8	rsvd2[6];
} srp_login_rej_t;

struct srp_I_logout {
	tUINT8	opcode;
	tUINT8	rsvd[7];
	tUINT64	tag;

};

typedef struct _srp_t_logout_t {
	tUINT8	opcode;
	tUINT8	rsvd[3];
	tUINT32	reason_code;
	tUINT64	tag;

} srp_t_logout_t;

typedef
struct _srp_tm_t {
	tUINT8	opcode;
	tUINT8	rsvd[7];
	tUINT64	tag;
	tUINT32	rsvd1;
	tUINT8	lun[8];
	tUINT8	rsvd2[2];
	tUINT8	task_mgt_flags;
	tUINT8	rsvd3;
	tUINT64	cmd_tag;
	tUINT8	rsvd4[8];
}srp_tm_t;

#define CMD_TAG_OFFSET 1
#define CMD_LUN_OFFSET 4

typedef struct _srp_cmd_t {
	tUINT8	opcode;
	tUINT8	rsvd1[4];
#if defined(__LITTLE_ENDIAN_BITFIELD)
	tUINT8	difmt:4,
		dofmt:4;
#elif defined (__BIG_ENDIAN_BITFIELD)
	tUINT8	dofmt:4,
		difmt:4;
#else
#error  "Please fix <asm/byteorder.h>"
#endif
	tUINT8	docount;
	tUINT8	dicount;
	tUINT64	tag;
	tUINT8	rsvd3[4];
	tUINT8	lun[8];
	tUINT8	rsvd4;
#if defined(__LITTLE_ENDIAN_BITFIELD)
	tUINT8	task_attr:3,
		rsvd5:5;
#elif defined (__BIG_ENDIAN_BITFIELD)
	tUINT8	rsvd5:5,
		task_attr:3;
#endif
	tUINT8	rsvd6;
#if defined(__LITTLE_ENDIAN_BITFIELD)
	tUINT8	rsvd7:2,
		add_cdb_len:6;
#elif defined (__BIG_ENDIAN_BITFIELD)
	tUINT8	add_cdb_len:6,
		rsvd7:2;
#endif
	tUINT8	cdb[16];
} srp_cmd_t;

typedef struct _srp_cmd_indirect_data_buffer_descriptor {

	srp_remote_buf_t	indirect_table_descriptor;

	tUINT32				total_length; /* of the data transfer */

	srp_remote_buf_t	partial_memory_descriptor_list[0];

} srp_cmd_indirect_data_buffer_descriptor_t;

typedef struct _resp_data {
    tUINT8 rsvd[3];
    tUINT8 response_code;
} resp_data_t;


typedef
struct _srp_resp {
	tUINT8	opcode;
	tUINT8	rsvd1[3];
	tUINT32	request_limit_delta;
	tUINT64	tag;

    union {
        struct {
        	tUINT8	rsvd[2];
#if defined(__LITTLE_ENDIAN_BITFIELD)
        	tUINT8	rspvalid:1,
		    snsvalid:1,
		    doover:1,
		    dounder:1,
		    diover:1,
		    diunder:1,
		    rsvd0:2;
#elif defined (__BIG_ENDIAN_BITFIELD)
	        tUINT8	rsvd0:2,
		    diunder:1,
		    diover:1,
		    dounder:1,
		    doover:1,
		    snsvalid:1,
		    rspvalid:1;
#else
#error  "Please fix <asm/byteorder.h>"
#endif
	        tUINT8	status;
        } bit;

        tUINT32 word;
    } status;

	tUINT32	data_out_residual_count;
	tUINT32	data_in_residual_count;
	tUINT32	sense_len;
	tUINT32	response_len;

} srp_resp_t;

typedef struct srp_RPL_res_s {
        tUINT32    lun_list_len;
        tUINT32    res;
	union {
          tUINT64 l;
	  tUINT8 b[8];
	}u[MAX_SRP_LUN];
} report_lun_res_t;


typedef struct _INQUIRYDATA
{
#if defined(__LITTLE_ENDIAN_BITFIELD)
        tUINT8 DeviceType                :5;
        tUINT8 DeviceTypeQualifier       :3;
#elif defined (__BIG_ENDIAN_BITFIELD)
        tUINT8 DeviceTypeQualifier       :3;
        tUINT8 DeviceType                :5;
#endif
#if defined(__LITTLE_ENDIAN_BITFIELD)
        tUINT8 DeviceTypeModifier        :7;
        tUINT8 RemovableMedia            :1;
#elif defined (__BIG_ENDIAN_BITFIELD)
        tUINT8 RemovableMedia            :1;
        tUINT8 DeviceTypeModifier        :7;
#endif
    tUINT8 Versions;
#if defined(__LITTLE_ENDIAN_BITFIELD)
    	tUINT8 ResponseDataFormat	:4;
    	tUINT8 HiSup			:1;
    	tUINT8 NormACA			:1;
    	tUINT8 Obsolete			:1;
    	tUINT8 AERC			:1;
#elif defined (__BIG_ENDIAN_BITFIELD)
    	tUINT8 AERC			:1;
    	tUINT8 Obsolete			:1;
    	tUINT8 NormACA			:1;
    	tUINT8 HiSup			:1;
    	tUINT8 ResponseDataFormat	:4;
#endif
    tUINT8 AdditionalLength;
    tUINT8 Reserved;
#if defined(__LITTLE_ENDIAN_BITFIELD)
        tUINT8 BQue                     :1;
        tUINT8 EncServ                  :1;
        tUINT8 VS                       :1;
        tUINT8 MultiP            	:1;
        tUINT8 MChngr                   :1;
        tUINT8 obso2                    :1;
        tUINT8 Obso                     :1;
        tUINT8 ADDR16                   :1;
#elif defined (__BIG_ENDIAN_BITFIELD)
        tUINT8 ADDR16                   :1;
        tUINT8 Obso                     :1;
        tUINT8 obso2                    :1;
        tUINT8 MChngr                   :1;
        tUINT8 MultiP            	:1;
        tUINT8 VS                       :1;
        tUINT8 EncServ                  :1;
        tUINT8 BQue                     :1;
#endif
#if defined(__LITTLE_ENDIAN_BITFIELD)
        tUINT8 SoftReset                :1;
        tUINT8 CommandQueue             :1;
        tUINT8 Reserved2                :1;
        tUINT8 LinkedCommands           :1;
        tUINT8 Synchronous              :1;
        tUINT8 Wide16Bit                :1;
        tUINT8 Wide32Bit                :1;
        tUINT8 RelativeAddressing       :1;
#elif defined (__BIG_ENDIAN_BITFIELD)
        tUINT8 RelativeAddressing       :1;
        tUINT8 Wide32Bit                :1;
        tUINT8 Wide16Bit                :1;
        tUINT8 Synchronous              :1;
        tUINT8 LinkedCommands           :1;
        tUINT8 Reserved2                :1;
        tUINT8 CommandQueue             :1;
        tUINT8 SoftReset                :1;
#endif
    tUINT8 VendorId[8];
    tUINT8 ProductId[16];
    tUINT8 ProductRevisionLevel[4];
    tUINT8 VendorSpecific[20];
    tUINT8 Resv[2];
    tUINT8 VersionDesc[2];
    tUINT8 Reserved3[36];
}       inq_data_t;


/*
 * Device Identifier type.
 * Taken from SPC-3 spec, Table 234-240 (T10/1416-D Revision 03)
 */

/*
 * Union discriminator is IdentifierType field of
 * device_identification_descriptor_t
 */
#define	DEVICE_IDENTIFIER_TYPE_VENDOR_SPECIFIC 0
#define	DEVICE_IDENTIFIER_TYPE_T10 1
#define	DEVICE_IDENTIFIER_TYPE_EUI64 2
#define DEVICE_IDENTIFIER_TYPE_NAA 3
#define DEVICE_IDENTIFIER_TYPE_RELATIVE_TARGET_PORT 4
#define DEVICE_IDENTIFIER_TYPE_TARGET_PORT_GROUP 5
#define DEVICE_IDENTIFIER_TYPE_LOGICAL_UNIT_GROUP 6


typedef struct _T10_identifier_format {
	tUINT8 VendorId[8];
	tUINT8 VendorSpecificId[0];
} T10_identifier_format_t;

typedef struct _EUI64_identifier_format {
	tUINT8	IeeeVendorID[3];
	tUINT8	VendorSpecificId[5];
} EUI64_identifier_format_t;


typedef union _device_identifier_format_t {
	tUINT8						VendorSpecificId[0];
	T10_identifier_format_t		T10Id;
	EUI64_identifier_format_t	EUI64Id;
} device_identifier_format_t;

typedef struct _device_identifier_t {
	tUINT8	IdLength;
	device_identifier_format_t	Id;
} device_identifier_t;

/*
 * Device Identification descriptor type returned
 * on a Page 83 Inquiry.
 * Taken from SPC-3 spec, Table 230 (T10/1416-D Revision 03)
 */
typedef struct _device_identification_descriptor_t
{

/*
 * Values the CodeSet field can take -- Table 231
 */
#define CODE_SET_BINARY_IDENTIFIER 1
#define CODE_SET_ASCII_IDENTIFIER  2
#if defined(__LITTLE_ENDIAN_BITFIELD)
        tUINT8 CodeSet                :4;
        tUINT8 Reserved1       		  :4;
#elif defined (__BIG_ENDIAN_BITFIELD)
        tUINT8 Reserved1       		  :4;
        tUINT8 CodeSet                :4;
#endif

/*
 * Values for the Association field.
 * "The identifier field is associated with the addressed
 * physical or logical device"
 * "The identifier field is associated with the port that received the request"
 */
#define DEV_ID_ASSOCIATION_PORT_INDEPENDENT	 0
#define DEV_ID_ASSOCIATION_PORT_DEPENDENT	 1

#if defined(__LITTLE_ENDIAN_BITFIELD)
        tUINT8 IdentifierType         :4;
        tUINT8 Association     		  :2;
        tUINT8 Reserved2       		  :2;
#elif defined (__BIG_ENDIAN_BITFIELD)
        tUINT8 Reserved2       		  :2;
        tUINT8 Association     		  :2;
        tUINT8 IdentifierType         :4;
#endif

		tUINT8 Reserved3;
		device_identifier_t	 identifier;
} device_identification_descriptor_t;

/*
 * VPD struct is defined only for the Device Identification
 * Page -- Page Code == 0x83
 */

typedef struct _INQUIRYEVPD
{
#if defined(__LITTLE_ENDIAN_BITFIELD)
        tUINT8 DeviceType                :5;
        tUINT8 DeviceTypeQualifier       :3;
#elif defined (__BIG_ENDIAN_BITFIELD)
        tUINT8 DeviceTypeQualifier       :3;
        tUINT8 DeviceType                :5;
#endif
    tUINT8 						Page_code;
    tUINT8 						Resv;
    tUINT8 						Page_len;
    device_identification_descriptor_t descriptors[0];
}       inq_evpddata_t;

/*
 * FCP response and sense information buffer sizes
 */
#define RESP_LEN 4
#define FCP_UNDER_FLOW 0x08
#define FCP_OVER_FLOW 0x04
#define FCP_RSP_LEN_VALID 0x01

#define FCP_RSP_BUF_SIZE     8
#define FCP_SNS_BUF_SIZE     64
#define FCP_RSP_SNS_BUF_SIZE (FCP_RSP_BUF_SIZE + FCP_SNS_BUF_SIZE)

#define FCP_CDB_LEN 16

#define FCP_LUN_LEN          8

typedef struct fcp_cntl_s {
    tUINT8 reserved;
    tUINT8 task_codes;
    tUINT8 task_mgmt_flags;
    tUINT8 exec_mgmt_code;
} fcp_cntl_t;

/*
 * FCP_CMND IU
 */
typedef struct fcp_cmnd_s {
    tUINT8     lun[8];
    fcp_cntl_t cntl;
    tUINT8     cdb[FCP_CDB_LEN];
    tUINT32    data_len;
} fcp_cmnd_t;


/*
 * FCP_RSP IU
 */
typedef struct fcp_rsp_s {
    tUINT8  reserved[8];
    tUINT8  status[4];
    tUINT32 residual;
    tUINT32 sns_len;
    tUINT32 rsp_len;
    tUINT8  rsp_sns[FCP_RSP_SNS_BUF_SIZE];
} fcp_rsp_t;

#pragma pack()
#endif
