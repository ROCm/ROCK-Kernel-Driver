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

  Copyright (c) 2004 Mellanox Technologies Ltd.  All rights reserved.
*/

#ifndef H_SM_MAD_H
#define H_SM_MAD_H
/* -------------------------------------------------------------------------------------------------- */
/*  IB structures and parameters */
/* -------------------------------------------------------------------------------------------------- */
#include <mtl_common.h>
#include <ib_defs.h>

/* -------------------------------------------------------------------------------------------------- */
/*  PORTINFO */
/* -------------------------------------------------------------------------------------------------- */
typedef struct SM_MAD_PortInfo_st {
    u_int64_t       qwMKey;             /* 0,64 */
    IB_gid_prefix_t qwGIDPrefix;        /* 64,64 */
    IB_lid_t        wLID;               /* 128,16 */
    IB_lid_t        wMasterSMLID;       /* 144,16 */
    u_int32_t       dwCapMask;          /* 160,32 */
    u_int16_t       wDiagCode;          /* 192,16 */
    u_int16_t       wMKLease;           /* 208,16 */
    u_int8_t        cLocalPortNum;      /* 224,8 */
    u_int8_t        cLinkWidthEna;      /* 232,8 */
    u_int8_t        cLinkWidthSup;      /* 240,8 */
    u_int8_t        cLinkWidthAct;      /* 248,8 */
    u_int8_t        cLinkSpeedSup;      /* 256,4 */
    IB_port_state_t cPortState;         /* 260,4 */
    u_int8_t        cPhyState;          /* 264,4 */
    u_int8_t        cDownDefState;      /* 268,4 */
    u_int8_t        cMKProtect;         /* 272,2 */
    u_int8_t        cReserved1;         /* 274,3 */
    u_int8_t        cLMC;               /* 277,2 */
    u_int8_t        cLinkSpeedAct;      /* 280,4 */
    u_int8_t        cLinkSpeedEna;      /* 284,4 */
    u_int8_t        cNbMTU;             /* 288,4 */
    u_int8_t        cMasterSMSL;        /* 292,4 */
    u_int8_t        cVLCap;             /* 296,4 */
    u_int8_t        cReserved2;         /* 300,4 */
    u_int8_t        cVLHighLimit;       /* 304,8 */
    u_int8_t        cVLArbHighCap;      /* 312,8 */
    u_int8_t        cVLArbLowCap;       /* 320,8 */
    u_int8_t        cReserved3;         /* 328,4 */
    u_int8_t        cMTUCap;            /* 332,4 */
    u_int8_t        cVLStallCnt;        /* 336,3 */
    u_int8_t        cHOQLife;           /* 339,5 */
    u_int8_t        cOperVL;            /* 344,4 */
    u_int8_t        cPartEnfIn;         /* 348,1 */
    u_int8_t        cPartEnfOut;        /* 349,1 */
    u_int8_t        cFilterRawIn;       /* 350,1 */
    u_int8_t        cFilterRawOut;      /* 351,1 */
    u_int16_t       wMKViolations;      /* 352,16 */
    u_int16_t       wPKViolations;      /* 368,16 */
    u_int16_t       wQKViolations;      /* 384,16 */
    u_int8_t        bGUIDCap;           /* 400,8 */
    u_int8_t        cReserved4;         /* 408,3 */
    u_int8_t        cSubnetTO;          /* 411,5 */
    u_int8_t        cReserved5;         /* 416,3 */
    u_int8_t        cRespTimeValue;     /* 419,5 */
    u_int8_t        cLocalPhyErr;       /* 424,4 */
    u_int8_t        cOverrunErr;        /* 428,4 */
} SM_MAD_PortInfo_t;

#define IB_PORTINFO_PORTSTATE_DOWN		1

/* -------------------------------------------------------------------------------------------------- */


/* -------------------------------------------------------------------------------------------------- */
/*  NODEINFO  */
/* -------------------------------------------------------------------------------------------------- */
typedef struct SM_MAD_NodeInfo_st {
    u_int8_t        cBaseVersion;
    u_int8_t        cClassVersion;
    IB_node_type_t  cNodeType;
    u_int8_t        cNumPorts;
    IB_guid_t       qwNodeGUID;
    IB_guid_t       qwPortGUID;
    u_int16_t       wPartCap;
    u_int16_t       wDeviceID;
    u_int32_t       dwRevision;
    u_int8_t        cLocalPortNum;
    u_int32_t       dwVendorID;
} SM_MAD_NodeInfo_t;

typedef struct SM_MAD_GUIDInfo_st {
    IB_guid_t  guid[8];
} SM_MAD_GUIDInfo_t;

typedef struct SM_MAD_Pkey_table_st {
    u_int16_t   pkey[32];
} SM_MAD_Pkey_table_t;

/* -------------------------------------------------------------------------------------------------- */


/* -------------------------------------------------------------------------------------------------- */
/*  NODEDESC  */
/* -------------------------------------------------------------------------------------------------- */
typedef struct NodeDesc {
    char szNodeDesc[64];
} NODEDESC;



/* -------------------------------------------------------------------------------------------------- */
#define GUID_DUMMY						MAKE_ULONGLONG(0xFFFFFFFFFFFFFFFF)
#define GUID_INVALID					MAKE_ULONGLONG(0x0000000000000000)

/* -------------------------------------------------------------------------------------------------- */

#define IB_INVALID_LID					0x0000
#define IB_PERMISSIVE_LID				0xFFFF

#define IB_MAD_SIZE						256
#define IB_SMP_DATA_START                64                      

/* REV convert to enum */
#define IB_CLASS_SMP					0x01
#define IB_CLASS_DIR_ROUTE              0x81

#define IB_METHOD_GET                      0x01
#define IB_METHOD_SET                      0x02

typedef enum {
    IB_SMP_ATTRIB_NODEINFO=   0x0011,
    IB_SMP_ATTRIB_GUIDINFO=   0x0014,
    IB_SMP_ATTRIB_PORTINFO=   0x0015,
    IB_SMP_ATTRIB_PARTTABLE=  0x0016
} SM_MAD_attrib_t;

void MADHeaderBuild(u_int8_t    cMgtClass, 
                    u_int16_t   wClSp,
                    u_int8_t    cMethod,
                    u_int16_t   wAttrib,
                    u_int32_t   dwModif,
                    u_int8_t    *pSMPBuf) ;

void MadBufPrint(void *madbuf);
void NodeInfoStToMAD(SM_MAD_NodeInfo_t *pNodeInfo,u_int8_t *pSMPBuf);
void NodeInfoMADToSt(SM_MAD_NodeInfo_t *pNodeInfo,u_int8_t *pSMPBuf);
void NodeInfoPrint(SM_MAD_NodeInfo_t *pNodeInfo);
void GUIDInfoMADToSt(SM_MAD_GUIDInfo_t *pGuidTable,u_int8_t *pSMPBuf);
void GUIDInfoPrint(SM_MAD_GUIDInfo_t *pGuidTable);
void PKeyTableMADToSt(SM_MAD_Pkey_table_t *pKeyTable,u_int8_t *pSMPBuf);
void PKeyTablePrint(SM_MAD_Pkey_table_t *pKeyTable);
void PortInfoStToMAD(SM_MAD_PortInfo_t *pPortInfo,u_int8_t *pSMPBuf);
void PortInfoMADToSt(SM_MAD_PortInfo_t *pPortInfo,u_int8_t *pSMPBuf);
void PortInfoPrint(SM_MAD_PortInfo_t *pPortInfo);

#endif
