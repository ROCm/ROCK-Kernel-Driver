#ifndef _INIT_
#define _INIT_

#include "osdef.h"
#include "initdef.h"
#include "vgatypes.h"
#include "vstruct.h"

#ifdef TC
#include <stdio.h>
#include <string.h>
#include <conio.h>
#include <dos.h>
#include <stdlib.h>
#endif

#ifdef LINUX_XF86
#include "xf86.h"
#include "xf86Pci.h"
#include "xf86PciInfo.h"
#include "xf86_OSproc.h"
#include "sis.h"
#include "sis_regs.h"
#endif

#ifdef LINUX_KERNEL
#include <linux/types.h>
#include <asm/io.h>
#include <linux/sisfb.h>
#endif

#ifdef WIN2000
#include <stdio.h>
#include <string.h>
#include <miniport.h>
#include "dderror.h"
#include "devioctl.h"
#include "miniport.h"
#include "ntddvdeo.h"
#include "video.h"
#include "sisv.h"
#include "tools.h"
#endif

USHORT SiS_DRAMType[17][5]={
	{0x0C,0x0A,0x02,0x40,0x39},
	{0x0D,0x0A,0x01,0x40,0x48},
	{0x0C,0x09,0x02,0x20,0x35},
	{0x0D,0x09,0x01,0x20,0x44},
	{0x0C,0x08,0x02,0x10,0x31},
	{0x0D,0x08,0x01,0x10,0x40},
	{0x0C,0x0A,0x01,0x20,0x34},
	{0x0C,0x09,0x01,0x08,0x32},
	{0x0B,0x08,0x02,0x08,0x21},
	{0x0C,0x08,0x01,0x08,0x30},
	{0x0A,0x08,0x02,0x04,0x11},
	{0x0B,0x0A,0x01,0x10,0x28},
	{0x09,0x08,0x02,0x02,0x01},
	{0x0B,0x09,0x01,0x08,0x24},
	{0x0B,0x08,0x01,0x04,0x20},
	{0x0A,0x08,0x01,0x02,0x10},
	{0x09,0x08,0x01,0x01,0x00}
};

USHORT SiS_SDRDRAM_TYPE[13][5] =
{
	{ 2,12, 9,64,0x35},
	{ 1,13, 9,64,0x44},
	{ 2,12, 8,32,0x31},
	{ 2,11, 9,32,0x25},
	{ 1,12, 9,32,0x34},
	{ 1,13, 8,32,0x40},
	{ 2,11, 8,16,0x21},
	{ 1,12, 8,16,0x30},
	{ 1,11, 9,16,0x24},
	{ 1,11, 8, 8,0x20},
	{ 2, 9, 8, 4,0x01},
	{ 1,10, 8, 4,0x10},
	{ 1, 9, 8, 2,0x00}
};

USHORT SiS_DDRDRAM_TYPE[4][5] =
{
	{ 2,12, 9,64,0x35},
	{ 2,12, 8,32,0x31},
	{ 2,11, 8,16,0x21},
	{ 2, 9, 8, 4,0x01}
};

UCHAR SiS_ChannelAB, SiS_DataBusWidth;

USHORT SiS_MDA_DAC[] =
{
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x15,0x15,0x15,0x15,0x15,0x15,0x15,0x15,
        0x15,0x15,0x15,0x15,0x15,0x15,0x15,0x15,
        0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x15,0x15,0x15,0x15,0x15,0x15,0x15,0x15,
        0x15,0x15,0x15,0x15,0x15,0x15,0x15,0x15,
        0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F
};

USHORT SiS_CGA_DAC[] =
{
        0x00,0x10,0x04,0x14,0x01,0x11,0x09,0x15,
        0x00,0x10,0x04,0x14,0x01,0x11,0x09,0x15,
        0x2A,0x3A,0x2E,0x3E,0x2B,0x3B,0x2F,0x3F,
        0x2A,0x3A,0x2E,0x3E,0x2B,0x3B,0x2F,0x3F,
        0x00,0x10,0x04,0x14,0x01,0x11,0x09,0x15,
        0x00,0x10,0x04,0x14,0x01,0x11,0x09,0x15,
        0x2A,0x3A,0x2E,0x3E,0x2B,0x3B,0x2F,0x3F,
        0x2A,0x3A,0x2E,0x3E,0x2B,0x3B,0x2F,0x3F
};

USHORT SiS_EGA_DAC[] =
{
        0x00,0x10,0x04,0x14,0x01,0x11,0x05,0x15,
        0x20,0x30,0x24,0x34,0x21,0x31,0x25,0x35,
        0x08,0x18,0x0C,0x1C,0x09,0x19,0x0D,0x1D,
        0x28,0x38,0x2C,0x3C,0x29,0x39,0x2D,0x3D,
        0x02,0x12,0x06,0x16,0x03,0x13,0x07,0x17,
        0x22,0x32,0x26,0x36,0x23,0x33,0x27,0x37,
        0x0A,0x1A,0x0E,0x1E,0x0B,0x1B,0x0F,0x1F,
        0x2A,0x3A,0x2E,0x3E,0x2B,0x3B,0x2F,0x3F
};

USHORT SiS_VGA_DAC[] =
{
	0x00,0x10,0x04,0x14,0x01,0x11,0x09,0x15,
	0x2A,0x3A,0x2E,0x3E,0x2B,0x3B,0x2F,0x3F,
	0x00,0x05,0x08,0x0B,0x0E,0x11,0x14,0x18,
	0x1C,0x20,0x24,0x28,0x2D,0x32,0x38,0x3F,
	0x00,0x10,0x1F,0x2F,0x3F,0x1F,0x27,0x2F,
	0x37,0x3F,0x2D,0x31,0x36,0x3A,0x3F,0x00,
	0x07,0x0E,0x15,0x1C,0x0E,0x11,0x15,0x18,
	0x1C,0x14,0x16,0x18,0x1A,0x1C,0x00,0x04,
	0x08,0x0C,0x10,0x08,0x0A,0x0C,0x0E,0x10,
	0x0B,0x0C,0x0D,0x0F,0x10
};

USHORT   SiS_P3c4,SiS_P3d4,SiS_P3c0,SiS_P3ce,SiS_P3c2;
USHORT   SiS_P3ca,SiS_P3c6,SiS_P3c7,SiS_P3c8,SiS_P3c9,SiS_P3da;
USHORT   SiS_Part1Port,SiS_Part2Port;
USHORT   SiS_Part3Port,SiS_Part4Port,SiS_Part5Port;
USHORT   SiS_CRT1Mode;

USHORT   flag_clearbuffer;
int      SiS_RAMType;
USHORT   SiS_ModeType;
USHORT   SiS_IF_DEF_LVDS, SiS_IF_DEF_TRUMPION, SiS_IF_DEF_DSTN, SiS_IF_DEF_FSTN;
USHORT   SiS_IF_DEF_CH70xx, SiS_IF_DEF_HiVision;
USHORT	 SiS_Backup70xx=0xff;
USHORT   SiS_VBInfo, SiS_LCDResInfo, SiS_LCDTypeInfo, SiS_LCDInfo, SiS_VBType;
USHORT   SiS_VBExtInfo, SiS_HiVision;
USHORT   SiS_SelectCRT2Rate;

extern   USHORT   SiS_SetFlag;
extern   USHORT   SiS_DDC_Port;
extern   USHORT   Panel800x600,  Panel1024x768,  Panel1280x1024, Panel1600x1200;
extern   USHORT   Panel1280x960, Panel1400x1050, Panel320x480,   Panel1152x768;
extern   USHORT   Panel1152x864, Panel1280x768,  Panel1024x600,  Panel640x480;
extern   USHORT   PanelMinLVDS,  PanelMin301,    PanelMax;
extern   USHORT   SiS_ChrontelInit;

void     SiS_SetReg1(USHORT, USHORT, USHORT);
void     SiS_SetReg2(USHORT, USHORT, USHORT);
void     SiS_SetReg3(USHORT, USHORT);
void     SiS_SetReg4(USHORT, ULONG);
UCHAR    SiS_GetReg1(USHORT, USHORT);
UCHAR    SiS_GetReg2(USHORT);
ULONG    SiS_GetReg3(USHORT);
void     SiS_ClearDAC(ULONG);
void     SiS_SetMemoryClock(UCHAR *ROMAddr,PSIS_HW_DEVICE_INFO HwDeviceExtension);
void     SiS_SetDRAMModeRegister(UCHAR *ROMAddr,PSIS_HW_DEVICE_INFO HwDeviceExtension);
BOOLEAN  SiS_SearchVBModeID(UCHAR *ROMAddr, USHORT *ModeNo);
void     SiS_IsLowResolution(UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex);
ULONG    GetDRAMSize(PSIS_HW_DEVICE_INFO HwDeviceExtension);

#ifdef SIS300
void     InitTo300Pointer(PSIS_HW_DEVICE_INFO HwDeviceExtension);
void     SiS_SetDRAMSize_300(PSIS_HW_DEVICE_INFO HwDeviceExtension);
USHORT   SiS_ChkBUSWidth_300(ULONG FBAddress);
#endif

#ifdef SIS315H
void     InitTo310Pointer(PSIS_HW_DEVICE_INFO HwDeviceExtension);
UCHAR    SiS_Get310DRAMType(UCHAR *ROMAddr,PSIS_HW_DEVICE_INFO HwDeviceExtension);
void     SiS_DDR_MRS(void);
void     SiS_SDR_MRS(void);
void     SiS_DisableRefresh(void);
void     SiS_EnableRefresh(UCHAR *ROMAddr);
void     SiS_SetDRAMSize_310(PSIS_HW_DEVICE_INFO);
void     SiS_DisableChannelInterleaving(int index,USHORT SiS_DDRDRAM_TYPE[][5]);
void     SiS_SetDRAMSizingType(int index,USHORT DRAMTYPE_TABLE[][5]);
void     SiS_CheckBusWidth_310(UCHAR *ROMAddress,ULONG FBAddress,
                               PSIS_HW_DEVICE_INFO HwDeviceExtension);
int      SiS_SetRank(int index,UCHAR RankNo,UCHAR SiS_ChannelAB,USHORT DRAMTYPE_TABLE[][5]);
int      SiS_SetDDRChannel(int index,UCHAR ChannelNo,UCHAR SiS_ChannelAB,
                           USHORT DRAMTYPE_TABLE[][5]);
int      SiS_CheckColumn(int index,USHORT DRAMTYPE_TABLE[][5],ULONG FBAddress);
int      SiS_CheckBanks(int index,USHORT DRAMTYPE_TABLE[][5],ULONG FBAddress);
int      SiS_CheckRank(int RankNo,int index,USHORT DRAMTYPE_TABLE[][5],ULONG FBAddress);
int      SiS_CheckDDRRank(int RankNo,int index,USHORT DRAMTYPE_TABLE[][5],ULONG FBAddress);
int      SiS_CheckRanks(int RankNo,int index,USHORT DRAMTYPE_TABLE[][5],ULONG FBAddress);
int      SiS_CheckDDRRanks(int RankNo,int index,USHORT DRAMTYPE_TABLE[][5],ULONG FBAddress);
int      SiS_SDRSizing(ULONG FBAddress);
int      SiS_DDRSizing(ULONG FBAddress);
int      Is315E(void);
void     SiS_VerifyMclk(ULONG FBAddr);
#endif

void     SetEnableDstn(void);
void     SiS_Delay15us(ULONG);
BOOLEAN  SiS_SearchModeID(UCHAR *ROMAddr, USHORT *ModeNo,USHORT *ModeIdIndex);
BOOLEAN  SiS_CheckMemorySize(UCHAR *ROMAddr,PSIS_HW_DEVICE_INFO HwDeviceExtension,
                             USHORT ModeNo,USHORT ModeIdIndex);
UCHAR    SiS_GetModePtr(UCHAR *ROMAddr, USHORT ModeNo,USHORT ModeIdIndex);
void     SiS_SetSeqRegs(UCHAR *ROMAddr,USHORT StandTableIndex);
void     SiS_SetMiscRegs(UCHAR *ROMAddr,USHORT StandTableIndex);
void     SiS_SetCRTCRegs(UCHAR *ROMAddr,PSIS_HW_DEVICE_INFO HwDeviceExtension,
                         USHORT StandTableIndex);
void     SiS_SetATTRegs(UCHAR *ROMAddr,USHORT StandTableIndex,USHORT ModeNo,
                        PSIS_HW_DEVICE_INFO HwDeviceExtension);
void     SiS_SetGRCRegs(UCHAR *ROMAddr,USHORT StandTableIndex);
void     SiS_ClearExt1Regs(PSIS_HW_DEVICE_INFO HwDeviceExtension);
void     SiS_SetSync(UCHAR *ROMAddr,USHORT RefreshRateTableIndex);
void     SiS_SetCRT1CRTC(UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex,
                         USHORT RefreshRateTableIndex,
			 PSIS_HW_DEVICE_INFO HwDeviceExtension);
void     SiS_ResetCRT1VCLK(UCHAR *ROMAddr,PSIS_HW_DEVICE_INFO HwDeviceExtension);
void     SiS_SetCRT1VCLK(UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex,PSIS_HW_DEVICE_INFO,
                         USHORT RefreshRateTableIndex);
void     SiS_SetVCLKState(UCHAR *ROMAddr,PSIS_HW_DEVICE_INFO, USHORT ModeNo,
                          USHORT RefreshRateTableIndex, USHORT ModeIdIndex);
void     SiS_LoadDAC(UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex);
void     SiS_DisplayOn(void);
void 	 SiS_DisplayOff(void);
void     SiS_SetCRT1ModeRegs(UCHAR *ROMAddr,PSIS_HW_DEVICE_INFO,USHORT ModeNo,
                             USHORT ModeIdIndex,USHORT RefreshRateTableIndex);
void     SiS_WriteDAC(USHORT, USHORT, USHORT, USHORT);
void     SiS_GetVBType(USHORT BaseAddr,PSIS_HW_DEVICE_INFO);
USHORT   SiS_ChkBUSWidth(UCHAR *ROMAddr);
USHORT   SiS_GetModeIDLength(UCHAR *ROMAddr, USHORT);
USHORT   SiS_GetRefindexLength(UCHAR *ROMAddr, USHORT);
void     SiS_SetInterlace(UCHAR *ROMAddr,USHORT ModeNo,USHORT RefreshRateTableIndex);
USHORT   SiS_CalcDelay2(UCHAR *ROMAddr, UCHAR);
USHORT   SiS_CalcDelay(UCHAR *ROMAddr, USHORT);
void     SiS_Set_LVDS_TRUMPION(PSIS_HW_DEVICE_INFO HwDeviceExtension);
void     SiS_SetCRT1Offset(UCHAR *ROMAddr,USHORT,USHORT,USHORT,PSIS_HW_DEVICE_INFO);
#ifdef SIS315H
void     SiS_SetCRT1FIFO_310(UCHAR *ROMAddr,USHORT,USHORT,PSIS_HW_DEVICE_INFO);
#endif
#ifdef SIS300
void     SiS_SetCRT1FIFO_300(UCHAR *ROMAddr,USHORT ModeNo,PSIS_HW_DEVICE_INFO,
                             USHORT RefreshRateTableIndex);
#endif
void     SiS_ClearBuffer(PSIS_HW_DEVICE_INFO,USHORT ModeNo);
void     SiS_SetCRT1Group(UCHAR *ROMAddr,PSIS_HW_DEVICE_INFO HwDeviceExtension,
                          USHORT ModeNo,USHORT ModeIdIndex,USHORT BaseAddr);
void     SiS_DetectMonitor(PSIS_HW_DEVICE_INFO HwDeviceExtension,USHORT BaseAddr);
void     SiS_GetSenseStatus(PSIS_HW_DEVICE_INFO HwDeviceExtension,UCHAR *ROMAddr);
USHORT   SiS_TestMonitorType(UCHAR R_DAC,UCHAR G_DAC,UCHAR B_DAC);
USHORT   SiS_SenseCHTV(VOID);
BOOLEAN  SiS_Sense(USHORT Part4Port,USHORT tempbx,USHORT tempcx);
BOOLEAN  SiS_GetPanelID(VOID);
BOOLEAN  SiS_GetLCDDDCInfo(PSIS_HW_DEVICE_INFO);
USHORT   SiS_SenseLCD(PSIS_HW_DEVICE_INFO);
void     SiSRegInit(USHORT BaseAddr);
void     SiSInitPtr(PSIS_HW_DEVICE_INFO HwDeviceExtension);
void     SiSSetLVDSetc(PSIS_HW_DEVICE_INFO HwDeviceExtension,USHORT ModeNo);
void     SiSInitPCIetc(PSIS_HW_DEVICE_INFO HwDeviceExtension);

#ifdef LINUX_XF86
USHORT  	SiS_CalcModeIndex(ScrnInfoPtr pScrn, DisplayModePtr mode);
USHORT  	SiS_CheckCalcModeIndex(ScrnInfoPtr pScrn, DisplayModePtr mode, int VBFlags);
void    	SiS_SetPitch(ScrnInfoPtr pScrn, UShort BaseAddr);
void    	SiS_SetPitchCRT1(ScrnInfoPtr pScrn, UShort BaseAddr);
void    	SiS_SetPitchCRT2(ScrnInfoPtr pScrn, UShort BaseAddr);
unsigned char 	SiS_GetSetModeID(ScrnInfoPtr pScrn, unsigned char id);
#endif

extern USHORT    SiS_GetOffset(UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex,
                       USHORT RefreshRateTableIndex,PSIS_HW_DEVICE_INFO HwDeviceExtension);
extern USHORT    SiS_GetColorDepth(UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex);
extern void      SiS_DisableBridge(PSIS_HW_DEVICE_INFO HwDeviceExtension, USHORT BaseAddr);
extern BOOLEAN   SiS_SetCRT2Group301(USHORT BaseAddr,UCHAR *ROMAddr,USHORT ModeNo,
                                     PSIS_HW_DEVICE_INFO HwDeviceExtension);
extern void      SiS_PresetScratchregister(USHORT SiS_P3d4,
                                           PSIS_HW_DEVICE_INFO HwDeviceExtension);
extern void      SiS_UnLockCRT2(PSIS_HW_DEVICE_INFO HwDeviceExtension,USHORT BaseAddr);
extern void      SiS_LockCRT2(PSIS_HW_DEVICE_INFO HwDeviceExtension,USHORT BaseAddr);
extern BOOLEAN   SiS_BridgeIsOn(USHORT BaseAddr);
extern BOOLEAN   SiS_BridgeIsEnable(USHORT BaseAddr,PSIS_HW_DEVICE_INFO );
extern void      SiS_SetTVSystem301(VOID);
extern BOOLEAN   SiS_GetLCDDDCInfo301(PSIS_HW_DEVICE_INFO HwDeviceExtension);
extern BOOLEAN   SiS_GetSenseStatus301(PSIS_HW_DEVICE_INFO HwDeviceExtension,
                                       USHORT BaseAddr,UCHAR *ROMAddr);
extern USHORT    SiS_GetVCLKLen(UCHAR *ROMAddr,PSIS_HW_DEVICE_INFO HwDeviceExtension);
extern BOOLEAN   SiS_SetCRT2Group302(USHORT BaseAddr,UCHAR *ROMAddr,USHORT ModeNo,
                                     PSIS_HW_DEVICE_INFO HwDeviceExtension);
extern void      SiS_GetVBInfo301(USHORT BaseAddr,UCHAR *ROMAddr,USHORT ModeNo,
                                  USHORT ModeIdIndex,PSIS_HW_DEVICE_INFO HwDeviceExtension);
extern BOOLEAN   SiS_GetLCDResInfo301(UCHAR *ROMAddr,USHORT P3d4,USHORT ModeNo,
                                      USHORT ModeIdIndex, PSIS_HW_DEVICE_INFO HwDeviceExtension);
extern void      SiS_SetHiVision(USHORT BaseAddr,PSIS_HW_DEVICE_INFO HwDeviceExtension);
/* extern USHORT  SiS_VBInfo,LCDResInfo,LCDTypeInfo,LCDInfo; */  /* TW: redundant */
extern USHORT    SiS_GetRatePtrCRT2(UCHAR *ROMAddr, USHORT ModeNo,USHORT ModeIdIndex);
extern void      SiS_LongWait(VOID);
extern void      SiS_SetRegOR(USHORT Port,USHORT Index,USHORT DataOR);
extern void      SiS_SetRegAND(USHORT Port,USHORT Index,USHORT DataAND);
extern void      SiS_SetRegANDOR(USHORT Port,USHORT Index,USHORT DataAND,USHORT DataOR);
extern USHORT    SiS_GetResInfo(UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex);
extern void      SiS_SetCH700x(USHORT tempax);
extern USHORT    SiS_GetCH700x(USHORT tempax);
extern void      SiS_SetCH701x(USHORT tempax);
extern USHORT    SiS_GetCH701x(USHORT tempax);
extern void      SiS_SetCH70xx(USHORT tempax);
extern USHORT    SiS_GetCH70xx(USHORT tempax);
extern BOOLEAN   SiS_GetLVDSCRT1Ptr(UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex,
                                    USHORT RefreshRateTableIndex,
		                    USHORT *ResInfo,USHORT *DisplayType);
extern BOOLEAN   SiS_GetLCDACRT1Ptr(UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex,
                                    USHORT RefreshRateTableIndex,
		                    USHORT *ResInfo,USHORT *DisplayType);
extern USHORT    SiS_GetVCLK2Ptr(UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex,
                                 USHORT RefreshRateTableIndex,
				 PSIS_HW_DEVICE_INFO HwDeviceExtension);
extern BOOLEAN   SiS_Is301B(USHORT BaseAddr);
extern BOOLEAN   SiS_LowModeStuff(USHORT ModeNo,PSIS_HW_DEVICE_INFO HwDeviceExtension);

#endif

