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

#include <sm_mad.h>

//--------------------------------------------------------------------------------------------------
//#define FUNCTION_NAME "MyMemCopy"
//--------------------------------------------------------------------------------------------------
static void MyMemCopy(u_int8_t *pDst,u_int8_t *pSrc, u_int16_t nLen)
{
#ifndef MT_BIG_ENDIAN
	for(pSrc+=nLen;nLen--;*(pDst++)=*(--pSrc));
#else
	for(;nLen--;*(pDst++)=*(pSrc++));

#endif
}
//--------------------------------------------------------------------------------------------------

void MadBufPrint( void *madbuf)
{
    int i;
    u_int8_t *iterator;
    iterator = (u_int8_t *)madbuf;

    MTL_DEBUG3("MadBufPrint START\n");
    for (i = 0; i < 16; i++) {
        MTL_DEBUG3("%02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x\n",
                   *iterator, *(iterator+1), *(iterator+2), *(iterator+3), 
                   *(iterator+4), *(iterator+5), *(iterator+6), *(iterator+7),
                   *(iterator+8), *(iterator+9), *(iterator+10), *(iterator+11),
                   *(iterator+12), *(iterator+13), *(iterator+14), *(iterator+15));
        iterator += 16;
    }
    MTL_DEBUG3("MadBufPrint END\n");

}
//--------------------------------------------------------------------------------------------------
//#define FUNCTION_NAME "FieldToBuf"
//--------------------------------------------------------------------------------------------------
static void FieldToBuf(u_int8_t cField, u_int8_t *pBuf, u_int16_t nOffset, u_int16_t nLen)
{
	pBuf[(nOffset>>3)] &= ~(((0x1<<nLen)-1)<<(8-nLen-(nOffset & 0x7)));
	pBuf[(nOffset>>3)] |= ((cField & ((0x1<<nLen)-1))<<(8-nLen-(nOffset & 0x7)));
}
//--------------------------------------------------------------------------------------------------

//--------------------------------------------------------------------------------------------------
//#define FUNCTION_NAME "BufToField"
//--------------------------------------------------------------------------------------------------
static u_int8_t BufToField(u_int8_t *pBuf,u_int16_t nOffset, u_int16_t nLen)
{
	return((pBuf[(nOffset>>3)]>>(8-nLen-(nOffset & 0x7))) & ((0x1<<nLen)-1));
}
//--------------------------------------------------------------------------------------------------

//--------------------------------------------------------------------------------------------------
//#define FUNCTION_NAME "ZeroToBuf"
//--------------------------------------------------------------------------------------------------
static void ZeroToBuf(u_int8_t *pBuf,u_int16_t nOffset, u_int16_t nLen)
{
	pBuf[(nOffset>>3)] &= ~(((0x1<<nLen)-1)<<(8-nLen-(nOffset & 0x7)));
}
//--------------------------------------------------------------------------------------------------

//--------------------------------------------------------------------------------------------------
//#define FUNCTION_NAME "MADHeaderBuild"
//--------------------------------------------------------------------------------------------------
void MADHeaderBuild(u_int8_t    cMgtClass, 
                    u_int16_t   wClSp,
                    u_int8_t    cMethod,
                    u_int16_t   wAttrib,
                    u_int32_t   dwModif,
                    u_int8_t    *pSMPBuf)
{
    u_int64_t qwMKey;
    u_int16_t i;
    u_int16_t wDrSLID,wDrDLID;
    u_int16_t start_null_index = 32;

    qwMKey=0; //REV
    wDrSLID=IB_PERMISSIVE_LID; //REV
    wDrDLID=IB_PERMISSIVE_LID; //REV

    pSMPBuf[0]=0x1;                                     //Base Version
    pSMPBuf[1]=cMgtClass;                               //Management Class
    pSMPBuf[2]=0x1;                                     //Class Version
    pSMPBuf[3]=cMethod;                                 //Method (MSb is 0 - request)
    pSMPBuf[5]=pSMPBuf[4]=0;                            //Status
    MyMemCopy((u_int8_t *)pSMPBuf+6,(u_int8_t *)&wClSp,2);      //Class Specific
                                                        //Transaction ID is filled by packet sending function
    MyMemCopy(pSMPBuf+16,(u_int8_t *)&wAttrib,2);   //Attribute
    pSMPBuf[18]=pSMPBuf[19]=0;
    MyMemCopy(pSMPBuf+20,(u_int8_t *)&dwModif,4);   //Attribute Modifier
    MyMemCopy(pSMPBuf+24,(u_int8_t *)&qwMKey,8);    //MKEY
    if (cMgtClass == IB_CLASS_DIR_ROUTE) {
        MyMemCopy(pSMPBuf+32,(u_int8_t *)&wDrSLID,2);   //DrSLID
        MyMemCopy(pSMPBuf+34,(u_int8_t *)&wDrDLID,2);   //DrDLID
        start_null_index = 36;
    } 

    MTL_DEBUG4(MT_FLFMT("MADHeaderBuild: Class = 0x%02x, Method=0x%02x, Attrib=0x%02x%02x, Attr Mod= 0x%02x%02x%02x%02x"),
               cMgtClass,cMethod, *(pSMPBuf+16), *(pSMPBuf+17),*(pSMPBuf+20), 
               *(pSMPBuf+21), *(pSMPBuf+22), *(pSMPBuf+23));
    for (i=start_null_index;i<IB_SMP_DATA_START;pSMPBuf[i++]=0);       //Reserved3
}
//--------------------------------------------------------------------------------------------------

//--------------------------------------------------------------------------------------------------
//#define FUNCTION_NAME "NodeInfoStToMAD"
//--------------------------------------------------------------------------------------------------
void NodeInfoStToMAD(SM_MAD_NodeInfo_t *pNodeInfo,u_int8_t *pSMPBuf)
{
    pSMPBuf[IB_SMP_DATA_START]=pNodeInfo->cBaseVersion;
    pSMPBuf[IB_SMP_DATA_START+1]=pNodeInfo->cClassVersion;
    pSMPBuf[IB_SMP_DATA_START+2]=pNodeInfo->cNodeType;
    pSMPBuf[IB_SMP_DATA_START+3]=pNodeInfo->cNumPorts;
    //reserved 64 bit
    memset(pSMPBuf+4,(u_int8_t)0,8);
    memcpy(pSMPBuf+IB_SMP_DATA_START+12,(u_int8_t *)&(pNodeInfo->qwNodeGUID),8);
    memcpy(pSMPBuf+IB_SMP_DATA_START+20,(u_int8_t *)&(pNodeInfo->qwPortGUID),8);
    MyMemCopy(pSMPBuf+IB_SMP_DATA_START+28,(u_int8_t *)&(pNodeInfo->wPartCap),2);
    MyMemCopy(pSMPBuf+IB_SMP_DATA_START+30,(u_int8_t *)&(pNodeInfo->wDeviceID),2);
    MyMemCopy(pSMPBuf+IB_SMP_DATA_START+32,(u_int8_t *)&(pNodeInfo->dwRevision),4);
    pSMPBuf[IB_SMP_DATA_START+36]=pNodeInfo->cLocalPortNum;
    MyMemCopy(pSMPBuf+IB_SMP_DATA_START+37,(u_int8_t *)&(pNodeInfo->dwVendorID),3); //REV will not work when BIG_ENDIAN
    memset(pSMPBuf+40,(u_int8_t)0,216);
}
//--------------------------------------------------------------------------------------------------


//--------------------------------------------------------------------------------------------------
//#define FUNCTION_NAME "NodeInfoMADToSt"
//--------------------------------------------------------------------------------------------------
void NodeInfoMADToSt(SM_MAD_NodeInfo_t *pNodeInfo,u_int8_t *pSMPBuf)
{
    pNodeInfo->cBaseVersion=pSMPBuf[IB_SMP_DATA_START];
    pNodeInfo->cClassVersion=pSMPBuf[IB_SMP_DATA_START+1];
    pNodeInfo->cNodeType=(IB_node_type_t)pSMPBuf[IB_SMP_DATA_START+2];
    pNodeInfo->cNumPorts=pSMPBuf[IB_SMP_DATA_START+3];
    //reserved 64 bit
    memcpy((u_int8_t *)&(pNodeInfo->qwNodeGUID),pSMPBuf+IB_SMP_DATA_START+12,8);
    memcpy((u_int8_t *)&(pNodeInfo->qwPortGUID),pSMPBuf+IB_SMP_DATA_START+20,8);
    MyMemCopy((u_int8_t *)&(pNodeInfo->wPartCap),pSMPBuf+IB_SMP_DATA_START+28,2);
    MyMemCopy((u_int8_t *)&(pNodeInfo->wDeviceID),pSMPBuf+IB_SMP_DATA_START+30,2);
    MyMemCopy((u_int8_t *)&(pNodeInfo->dwRevision),pSMPBuf+IB_SMP_DATA_START+32,4);
    pNodeInfo->cLocalPortNum=pSMPBuf[IB_SMP_DATA_START+36];
    MyMemCopy((u_int8_t *)&(pNodeInfo->dwVendorID),pSMPBuf+IB_SMP_DATA_START+37,3); //REV will not work when BIG_ENDIAN
}
//--------------------------------------------------------------------------------------------------

//--------------------------------------------------------------------------------------------------
//#define FUNCTION_NAME "NodeInfoPrint"
//--------------------------------------------------------------------------------------------------
void NodeInfoPrint(SM_MAD_NodeInfo_t *pNodeInfo)
{
    MTL_DEBUG3(MT_FLFMT("cBaseVersion: 0x%02X"),pNodeInfo->cBaseVersion);
    MTL_DEBUG3(MT_FLFMT("cClassVersion: 0x%02X"),pNodeInfo->cClassVersion);
    MTL_DEBUG3(MT_FLFMT("cNodeType: 0x%02X"),pNodeInfo->cNodeType);
    MTL_DEBUG3(MT_FLFMT("cNumPorts: 0x%02X"),pNodeInfo->cNumPorts);
    MTL_DEBUG3(MT_FLFMT("qwNodeGUID = 0x%02x%02x%02x%02x%02x%02x%02x%02x"),  
      pNodeInfo->qwNodeGUID[0],pNodeInfo->qwNodeGUID[1],pNodeInfo->qwNodeGUID[2],pNodeInfo->qwNodeGUID[3],
      pNodeInfo->qwNodeGUID[4],pNodeInfo->qwNodeGUID[5],pNodeInfo->qwNodeGUID[6],pNodeInfo->qwNodeGUID[7]); //REV show all bytes
    MTL_DEBUG3(MT_FLFMT("qwPortGUID = 0x%02x%02x%02x%02x%02x%02x%02x%02x"),  
      pNodeInfo->qwPortGUID[0],pNodeInfo->qwPortGUID[1],pNodeInfo->qwPortGUID[2],pNodeInfo->qwPortGUID[3],
      pNodeInfo->qwPortGUID[4],pNodeInfo->qwPortGUID[5],pNodeInfo->qwPortGUID[6],pNodeInfo->qwPortGUID[7]); //REV show all bytes
    MTL_DEBUG3(MT_FLFMT("wPartCap: 0x%04X"),pNodeInfo->wPartCap);
    MTL_DEBUG3(MT_FLFMT("wDeviceID: 0x%04X"),pNodeInfo->wDeviceID);
    MTL_DEBUG3(MT_FLFMT("dwRevision: 0x%08X"),pNodeInfo->dwRevision);
    MTL_DEBUG3(MT_FLFMT("cLocalPortNum: 0x%02X"),pNodeInfo->cLocalPortNum);
    MTL_DEBUG3(MT_FLFMT("dwVendorID: 0x%08X"),pNodeInfo->dwVendorID);
}
//--------------------------------------------------------------------------------------------------

//--------------------------------------------------------------------------------------------------
//#define FUNCTION_NAME "GUIDInfoMADToSt"
//--------------------------------------------------------------------------------------------------
void GUIDInfoMADToSt(SM_MAD_GUIDInfo_t *pGuidTable,u_int8_t *pSMPBuf)
{
    u_int16_t i;

    for (i=0;i<8;i++)
         memcpy((u_int8_t *)&(pGuidTable->guid[i]), pSMPBuf+IB_SMP_DATA_START + (i*8), 8);
}
//--------------------------------------------------------------------------------------------------


//--------------------------------------------------------------------------------------------------
//#define FUNCTION_NAME "NodeDescPrint"
//--------------------------------------------------------------------------------------------------
void GUIDInfoPrint(SM_MAD_GUIDInfo_t *pGuidTable)
{
    u_int16_t i;

    for (i=0;i<8;i++) {
       MTL_DEBUG3(MT_FLFMT("GUID[%d] = 0x%02x%02x%02x%02x%02x%02x%02x%02x"), i, 
         pGuidTable->guid[i][0],pGuidTable->guid[i][1],pGuidTable->guid[i][2],pGuidTable->guid[i][3],
         pGuidTable->guid[i][4],pGuidTable->guid[i][5],pGuidTable->guid[i][6],pGuidTable->guid[i][7]); //REV show all bytes
    	}
}
//--------------------------------------------------------------------------------------------------


//--------------------------------------------------------------------------------------------------
//#define FUNCTION_NAME "PKeyTableMADToSt"
//--------------------------------------------------------------------------------------------------
void PKeyTableMADToSt(SM_MAD_Pkey_table_t *pKeyTable,u_int8_t *pSMPBuf)
{
    u_int16_t i;

    for (i=0;i<32;i++)
         MyMemCopy((u_int8_t *)&(pKeyTable->pkey[i]), pSMPBuf+IB_SMP_DATA_START + (i*2), 2);
}
//--------------------------------------------------------------------------------------------------


//--------------------------------------------------------------------------------------------------
//#define FUNCTION_NAME "PKeyTablePrint"
//--------------------------------------------------------------------------------------------------
void PKeyTablePrint(SM_MAD_Pkey_table_t *pKeyTable)
{
    u_int16_t i;

    for (i=0;i<32;i++) {
       MTL_DEBUG3(MT_FLFMT("PKey[%d] = 0x%X"), i, pKeyTable->pkey[i]); //REV show all bytes
    	}
}
//--------------------------------------------------------------------------------------------------


//--------------------------------------------------------------------------------------------------
//#define FUNCTION_NAME "PortInfoStToMAD"
//--------------------------------------------------------------------------------------------------
void PortInfoStToMAD(SM_MAD_PortInfo_t *pPortInfo,u_int8_t *pSMPBuf)
{
    MyMemCopy(pSMPBuf+IB_SMP_DATA_START,(u_int8_t *)&(pPortInfo->qwMKey),8);
    memcpy(pSMPBuf+IB_SMP_DATA_START+8,(u_int8_t *)&(pPortInfo->qwGIDPrefix),8);
    MyMemCopy(pSMPBuf+IB_SMP_DATA_START+16,(u_int8_t *)&(pPortInfo->wLID),2);
    MyMemCopy(pSMPBuf+IB_SMP_DATA_START+18,(u_int8_t *)&(pPortInfo->wMasterSMLID),2);
    MyMemCopy(pSMPBuf+IB_SMP_DATA_START+20,(u_int8_t *)&(pPortInfo->dwCapMask),4);
    MyMemCopy(pSMPBuf+IB_SMP_DATA_START+24,(u_int8_t *)&(pPortInfo->wDiagCode),2);
    MyMemCopy(pSMPBuf+IB_SMP_DATA_START+26,(u_int8_t *)&(pPortInfo->wMKLease),2);
    FieldToBuf(pPortInfo->cLocalPortNum,pSMPBuf+IB_SMP_DATA_START,224,8);
    FieldToBuf(pPortInfo->cLinkWidthEna,pSMPBuf+IB_SMP_DATA_START,232,8);
    FieldToBuf(pPortInfo->cLinkWidthSup,pSMPBuf+IB_SMP_DATA_START,240,8);
    FieldToBuf(pPortInfo->cLinkWidthAct,pSMPBuf+IB_SMP_DATA_START,248,8);
    FieldToBuf(pPortInfo->cLinkSpeedSup,pSMPBuf+IB_SMP_DATA_START,256,4);
    FieldToBuf((u_int8_t)pPortInfo->cPortState,pSMPBuf+IB_SMP_DATA_START,260,4);
    FieldToBuf(pPortInfo->cPhyState,pSMPBuf+IB_SMP_DATA_START,264,4);
    FieldToBuf(pPortInfo->cDownDefState,pSMPBuf+IB_SMP_DATA_START,268,4);
    FieldToBuf(pPortInfo->cMKProtect,pSMPBuf+IB_SMP_DATA_START,272,2);
    ZeroToBuf(pSMPBuf+IB_SMP_DATA_START,274,3);
    FieldToBuf(pPortInfo->cLMC,pSMPBuf+IB_SMP_DATA_START,277,3);
    FieldToBuf(pPortInfo->cLinkSpeedAct,pSMPBuf+IB_SMP_DATA_START,280,4);
    FieldToBuf(pPortInfo->cLinkSpeedEna,pSMPBuf+IB_SMP_DATA_START,284,4);
    FieldToBuf(pPortInfo->cNbMTU,pSMPBuf+IB_SMP_DATA_START,288,4);
    FieldToBuf(pPortInfo->cMasterSMSL,pSMPBuf+IB_SMP_DATA_START,292,4);
    FieldToBuf(pPortInfo->cVLCap,pSMPBuf+IB_SMP_DATA_START,296,4);
    ZeroToBuf(pSMPBuf+IB_SMP_DATA_START,300,4);
    FieldToBuf(pPortInfo->cVLHighLimit,pSMPBuf+IB_SMP_DATA_START,304,8);
    FieldToBuf(pPortInfo->cVLArbHighCap,pSMPBuf+IB_SMP_DATA_START,312,8);
    FieldToBuf(pPortInfo->cVLArbLowCap,pSMPBuf+IB_SMP_DATA_START,320,8);
    ZeroToBuf(pSMPBuf+IB_SMP_DATA_START,328,4);
    FieldToBuf(pPortInfo->cMTUCap,pSMPBuf+IB_SMP_DATA_START,332,4);
    FieldToBuf(pPortInfo->cVLStallCnt,pSMPBuf+IB_SMP_DATA_START,336,3);
    FieldToBuf(pPortInfo->cHOQLife,pSMPBuf+IB_SMP_DATA_START,339,5);
    FieldToBuf(pPortInfo->cOperVL,pSMPBuf+IB_SMP_DATA_START,344,4);
    FieldToBuf(pPortInfo->cPartEnfIn,pSMPBuf+IB_SMP_DATA_START,348,1);
    FieldToBuf(pPortInfo->cPartEnfOut,pSMPBuf+IB_SMP_DATA_START,349,1);
    FieldToBuf(pPortInfo->cFilterRawIn,pSMPBuf+IB_SMP_DATA_START,350,1);
    FieldToBuf(pPortInfo->cFilterRawOut,pSMPBuf+IB_SMP_DATA_START,351,1);
    MyMemCopy(pSMPBuf+IB_SMP_DATA_START+44,(u_int8_t *)&(pPortInfo->wMKViolations),2);
    MyMemCopy(pSMPBuf+IB_SMP_DATA_START+46,(u_int8_t *)&(pPortInfo->wPKViolations),2);
    MyMemCopy(pSMPBuf+IB_SMP_DATA_START+48,(u_int8_t *)&(pPortInfo->wQKViolations),2);
    FieldToBuf(pPortInfo->bGUIDCap,pSMPBuf+IB_SMP_DATA_START,400,8);
    ZeroToBuf(pSMPBuf+IB_SMP_DATA_START,408,3);
    FieldToBuf(pPortInfo->cSubnetTO,pSMPBuf+IB_SMP_DATA_START,411,5);
    ZeroToBuf(pSMPBuf+IB_SMP_DATA_START,416,3);
    FieldToBuf(pPortInfo->cRespTimeValue,pSMPBuf+IB_SMP_DATA_START,419,5);
    FieldToBuf(pPortInfo->cLocalPhyErr,pSMPBuf+IB_SMP_DATA_START,424,4);
    FieldToBuf(pPortInfo->cOverrunErr,pSMPBuf+IB_SMP_DATA_START,428,4);
}
//--------------------------------------------------------------------------------------------------


//--------------------------------------------------------------------------------------------------
//#define FUNCTION_NAME "PortInfoMADToSt"
//--------------------------------------------------------------------------------------------------
void PortInfoMADToSt(SM_MAD_PortInfo_t *pPortInfo,u_int8_t *pSMPBuf)
{
    //parse pSMPBuf into PortInfo struct
    MyMemCopy((u_int8_t *)&(pPortInfo->qwMKey),pSMPBuf+IB_SMP_DATA_START,8);
    memcpy((u_int8_t *)&(pPortInfo->qwGIDPrefix),pSMPBuf+IB_SMP_DATA_START+8,8);
    MyMemCopy((u_int8_t *)&(pPortInfo->wLID),pSMPBuf+IB_SMP_DATA_START+16,2);
    MyMemCopy((u_int8_t *)&(pPortInfo->wMasterSMLID),pSMPBuf+IB_SMP_DATA_START+18,2);
    MyMemCopy((u_int8_t *)&(pPortInfo->dwCapMask),pSMPBuf+IB_SMP_DATA_START+20,4);
    MyMemCopy((u_int8_t *)&(pPortInfo->wDiagCode),pSMPBuf+IB_SMP_DATA_START+24,2);
    MyMemCopy((u_int8_t *)&(pPortInfo->wMKLease),pSMPBuf+IB_SMP_DATA_START+26,2);
    pPortInfo->cLocalPortNum=BufToField(pSMPBuf+IB_SMP_DATA_START,224,8);
    pPortInfo->cLinkWidthEna=BufToField(pSMPBuf+IB_SMP_DATA_START,232,8);
    pPortInfo->cLinkWidthSup=BufToField(pSMPBuf+IB_SMP_DATA_START,240,8);
    pPortInfo->cLinkWidthAct=BufToField(pSMPBuf+IB_SMP_DATA_START,248,8);
    pPortInfo->cLinkSpeedSup=BufToField(pSMPBuf+IB_SMP_DATA_START,256,4);
    pPortInfo->cPortState=(IB_port_state_t)BufToField(pSMPBuf+IB_SMP_DATA_START,260,4);
    pPortInfo->cPhyState=BufToField(pSMPBuf+IB_SMP_DATA_START,264,4);
    pPortInfo->cDownDefState=BufToField(pSMPBuf+IB_SMP_DATA_START,268,4);
    pPortInfo->cMKProtect=BufToField(pSMPBuf+IB_SMP_DATA_START,272,2);
    pPortInfo->cReserved1=BufToField(pSMPBuf+IB_SMP_DATA_START,274,3);
    pPortInfo->cLMC=BufToField(pSMPBuf+IB_SMP_DATA_START,277,3);
    pPortInfo->cLinkSpeedAct=BufToField(pSMPBuf+IB_SMP_DATA_START,280,4);
    pPortInfo->cLinkSpeedEna=BufToField(pSMPBuf+IB_SMP_DATA_START,284,4);
    pPortInfo->cNbMTU=BufToField(pSMPBuf+IB_SMP_DATA_START,288,4);
    pPortInfo->cMasterSMSL=BufToField(pSMPBuf+IB_SMP_DATA_START,292,4);
    pPortInfo->cVLCap=BufToField(pSMPBuf+IB_SMP_DATA_START,296,4);
    pPortInfo->cReserved2=BufToField(pSMPBuf+IB_SMP_DATA_START,300,4);
    pPortInfo->cVLHighLimit=BufToField(pSMPBuf+IB_SMP_DATA_START,304,8);
    pPortInfo->cVLArbHighCap=BufToField(pSMPBuf+IB_SMP_DATA_START,312,8);
    pPortInfo->cVLArbLowCap=BufToField(pSMPBuf+IB_SMP_DATA_START,320,8);
    pPortInfo->cReserved3=BufToField(pSMPBuf+IB_SMP_DATA_START,328,4);
    pPortInfo->cMTUCap=BufToField(pSMPBuf+IB_SMP_DATA_START,332,4);
    pPortInfo->cVLStallCnt=BufToField(pSMPBuf+IB_SMP_DATA_START,336,3);
    pPortInfo->cHOQLife=BufToField(pSMPBuf+IB_SMP_DATA_START,339,5);
    pPortInfo->cOperVL=BufToField(pSMPBuf+IB_SMP_DATA_START,344,4);
    pPortInfo->cPartEnfIn=BufToField(pSMPBuf+IB_SMP_DATA_START,348,1);
    pPortInfo->cPartEnfOut=BufToField(pSMPBuf+IB_SMP_DATA_START,349,1);
    pPortInfo->cFilterRawIn=BufToField(pSMPBuf+IB_SMP_DATA_START,350,1);
    pPortInfo->cFilterRawOut=BufToField(pSMPBuf+IB_SMP_DATA_START,351,1);
    MyMemCopy((u_int8_t *)&(pPortInfo->wMKViolations),pSMPBuf+IB_SMP_DATA_START+44,2);
    MyMemCopy((u_int8_t *)&(pPortInfo->wPKViolations),pSMPBuf+IB_SMP_DATA_START+46,2);
    MyMemCopy((u_int8_t *)&(pPortInfo->wQKViolations),pSMPBuf+IB_SMP_DATA_START+48,2);
    pPortInfo->bGUIDCap=BufToField(pSMPBuf+IB_SMP_DATA_START,400,8);
    pPortInfo->cReserved4=BufToField(pSMPBuf+IB_SMP_DATA_START,408,3);
    pPortInfo->cSubnetTO=BufToField(pSMPBuf+IB_SMP_DATA_START,411,5);
    pPortInfo->cReserved5=BufToField(pSMPBuf+IB_SMP_DATA_START,416,3);
    pPortInfo->cRespTimeValue=BufToField(pSMPBuf+IB_SMP_DATA_START,419,5);
    pPortInfo->cLocalPhyErr=BufToField(pSMPBuf+IB_SMP_DATA_START,424,4);
    pPortInfo->cOverrunErr=BufToField(pSMPBuf+IB_SMP_DATA_START,428,4);
}
//--------------------------------------------------------------------------------------------------

//--------------------------------------------------------------------------------------------------
//#define FUNCTION_NAME "PortInfoPrint"
//--------------------------------------------------------------------------------------------------
void PortInfoPrint(SM_MAD_PortInfo_t *pPortInfo)
{
#ifndef VXWORKS_OS // vxworks doesn't printf 64 bits int right.
    //MTL_DEBUG3("  qwMKey: 0x"U64_FMT" \n",pPortInfo->qwMKey);
#else
    MTL_DEBUG3(MT_FLFMT("qwMKey: %08lX%08lX"),*(unsigned long *)&pPortInfo->qwMKey,*(((unsigned long *)&pPortInfo->qwMKey +1)));
#endif //VXWORKS_OS
    MTL_DEBUG3(MT_FLFMT("qwGIDPrefix = 0x%02x%02x%02x%02x%02x%02x%02x%02x"),  
      pPortInfo->qwGIDPrefix[0],pPortInfo->qwGIDPrefix[1],pPortInfo->qwGIDPrefix[2],pPortInfo->qwGIDPrefix[3],
      pPortInfo->qwGIDPrefix[4],pPortInfo->qwGIDPrefix[5],pPortInfo->qwGIDPrefix[6],pPortInfo->qwGIDPrefix[7]); //REV show all bytes
    MTL_DEBUG3(MT_FLFMT("wLID:0x%04X"),pPortInfo->wLID);
    MTL_DEBUG3(MT_FLFMT("wMasterSMLID:0x%04X"),pPortInfo->wMasterSMLID);
    MTL_DEBUG3(MT_FLFMT("dwCapMask:0x%08X"),pPortInfo->dwCapMask);
    MTL_DEBUG3(MT_FLFMT("wDiagCode:0x%04X"),pPortInfo->wDiagCode);
    MTL_DEBUG3(MT_FLFMT("wMKLease:0x%04X"),pPortInfo->wMKLease);
    MTL_DEBUG3(MT_FLFMT("cLocalPortNum:0x%02X"),pPortInfo->cLocalPortNum);
    MTL_DEBUG3(MT_FLFMT("cLinkWidthEna:0x%02X"),pPortInfo->cLinkWidthEna);
    MTL_DEBUG3(MT_FLFMT("cLinkWidthSup:0x%02X"),pPortInfo->cLinkWidthSup);
    MTL_DEBUG3(MT_FLFMT("cLinkWidthAct:0x%02X"),pPortInfo->cLinkWidthAct);
    MTL_DEBUG3(MT_FLFMT("cLinkSpeedSup:0x%02X"),pPortInfo->cLinkSpeedSup);
    MTL_DEBUG3(MT_FLFMT("cPortState:0x%02X"),pPortInfo->cPortState);
    MTL_DEBUG3(MT_FLFMT("cPhyState:0x%02X"),pPortInfo->cPhyState);
    MTL_DEBUG3(MT_FLFMT("cDownDefState:0x%02X"),pPortInfo->cDownDefState);
    MTL_DEBUG3(MT_FLFMT("cMKProtect:0x%02X"),pPortInfo->cMKProtect);
    MTL_DEBUG3(MT_FLFMT("cReserved1:0x%02X"),pPortInfo->cReserved1);
    MTL_DEBUG3(MT_FLFMT("cLMC:0x%02X"),pPortInfo->cLMC);
    MTL_DEBUG3(MT_FLFMT("cLinkSpeedAct:0x%02X"),pPortInfo->cLinkSpeedAct);
    MTL_DEBUG3(MT_FLFMT("cLinkSpeedEna:0x%02X"),pPortInfo->cLinkSpeedEna);
    MTL_DEBUG3(MT_FLFMT("cNbMTU:0x%02X"),pPortInfo->cNbMTU);
    MTL_DEBUG3(MT_FLFMT("cMasterSMSL:0x%02X"),pPortInfo->cMasterSMSL);
    MTL_DEBUG3(MT_FLFMT("cVLCap:0x%02X"),pPortInfo->cVLCap);
    MTL_DEBUG3(MT_FLFMT("cReserved2:0x%02X"),pPortInfo->cReserved2);
    MTL_DEBUG3(MT_FLFMT("cVLHighLimit:0x%02X"),pPortInfo->cVLHighLimit);
    MTL_DEBUG3(MT_FLFMT("cVLArbHighCap:0x%02X"),pPortInfo->cVLArbHighCap);
    MTL_DEBUG3(MT_FLFMT("cVLArbLowCap:0x%02X"),pPortInfo->cVLArbLowCap);
    MTL_DEBUG3(MT_FLFMT("cReserved3:0x%02X"),pPortInfo->cReserved3);
    MTL_DEBUG3(MT_FLFMT("cMTUCap:0x%02X"),pPortInfo->cMTUCap);
    MTL_DEBUG3(MT_FLFMT("cVLStallCnt:0x%02X"),pPortInfo->cVLStallCnt);
    MTL_DEBUG3(MT_FLFMT("cHOQLife:0x%02X"),pPortInfo->cHOQLife);
    MTL_DEBUG3(MT_FLFMT("cOperVL:0x%02X"),pPortInfo->cOperVL);
    MTL_DEBUG3(MT_FLFMT("cPartEnfIn:0x%02X"),pPortInfo->cPartEnfIn);
    MTL_DEBUG3(MT_FLFMT("cPartEnfOut:0x%02X"),pPortInfo->cPartEnfOut);
    MTL_DEBUG3(MT_FLFMT("cFilterRawIn:0x%02X"),pPortInfo->cFilterRawIn);
    MTL_DEBUG3(MT_FLFMT("cFilterRawOut:0x%02X"),pPortInfo->cFilterRawOut);
    MTL_DEBUG3(MT_FLFMT("wMKViolations:0x%04X"),pPortInfo->wMKViolations);
    MTL_DEBUG3(MT_FLFMT("wPKViolations:0x%04X"),pPortInfo->wPKViolations);
    MTL_DEBUG3(MT_FLFMT("wQKViolations:0x%04X"),pPortInfo->wQKViolations);
    MTL_DEBUG3(MT_FLFMT("bGUIDCap:0x%02X"),pPortInfo->bGUIDCap);
    MTL_DEBUG3(MT_FLFMT("cReserved4:0x%02X"),pPortInfo->cReserved4);
    MTL_DEBUG3(MT_FLFMT("cSubnetTO:0x%02X"),pPortInfo->cSubnetTO);
    MTL_DEBUG3(MT_FLFMT("cReserved5:0x%02X"),pPortInfo->cReserved5);
    MTL_DEBUG3(MT_FLFMT("cRespTimeValue:0x%02X"),pPortInfo->cRespTimeValue);
    MTL_DEBUG3(MT_FLFMT("cLocalPhyErr:0x%02X"),pPortInfo->cLocalPhyErr);
    MTL_DEBUG3(MT_FLFMT("cOverrunErr:0x%02X"),pPortInfo->cOverrunErr);
}
//--------------------------------------------------------------------------------------------------

//--------------------------------------------------------------------------------------------------
