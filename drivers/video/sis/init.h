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
#include <linux/config.h>
#include <linux/version.h>
#include <linux/types.h>
#include <asm/io.h>
#include <linux/fb.h>
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)
#include <linux/sisfb.h>
#else
#include <video/sisfb.h>
#endif
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

const USHORT SiS_DRAMType[17][5]={
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

const USHORT SiS_SDRDRAM_TYPE[13][5] =
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

const USHORT SiS_DDRDRAM_TYPE[4][5] =
{
	{ 2,12, 9,64,0x35},
	{ 2,12, 8,32,0x31},
	{ 2,11, 8,16,0x21},
	{ 2, 9, 8, 4,0x01}
};

const USHORT SiS_MDA_DAC[] =
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

const USHORT SiS_CGA_DAC[] =
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

const USHORT SiS_EGA_DAC[] =
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

const USHORT SiS_VGA_DAC[] =
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

void     SiS_SetReg1(USHORT, USHORT, USHORT);
void     SiS_SetReg2(SiS_Private *, USHORT, USHORT, USHORT);
void     SiS_SetReg3(USHORT, USHORT);
void     SiS_SetReg4(USHORT, ULONG);
void     SiS_SetReg5(USHORT, USHORT);
UCHAR    SiS_GetReg1(USHORT, USHORT);
UCHAR    SiS_GetReg2(USHORT);
ULONG    SiS_GetReg3(USHORT);
USHORT   SiS_GetReg4(USHORT);
void     SiS_ClearDAC(SiS_Private *SiS_Pr, ULONG);
void     SiS_SetMemoryClock(SiS_Private *SiS_Pr, UCHAR *ROMAddr,PSIS_HW_DEVICE_INFO HwDeviceExtension);
void     SiS_SetDRAMModeRegister(SiS_Private *SiS_Pr, UCHAR *ROMAddr,PSIS_HW_DEVICE_INFO HwDeviceExtension);
BOOLEAN  SiS_SearchVBModeID(SiS_Private *SiS_Pr, UCHAR *ROMAddr, USHORT *ModeNo);
void     SiS_IsLowResolution(SiS_Private *SiS_Pr, UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex);

#ifdef SIS300
void     SiS_SetDRAMSize_300(SiS_Private *SiS_Pr, PSIS_HW_DEVICE_INFO HwDeviceExtension);
USHORT   SiS_ChkBUSWidth_300(SiS_Private *SiS_Pr, ULONG FBAddress);
#endif

#ifdef SIS315H
UCHAR    SiS_Get310DRAMType(SiS_Private *SiS_Pr, UCHAR *ROMAddr,PSIS_HW_DEVICE_INFO HwDeviceExtension);
void     SiS_DDR_MRS(SiS_Private *SiS_Pr);
void     SiS_SDR_MRS(SiS_Private *SiS_Pr);
void     SiS_DisableRefresh(SiS_Private *SiS_Pr);
void     SiS_EnableRefresh(SiS_Private *SiS_Pr, UCHAR *ROMAddr);
void     SiS_SetDRAMSize_310(SiS_Private *SiS_Pr, PSIS_HW_DEVICE_INFO);
void     SiS_DisableChannelInterleaving(SiS_Private *SiS_Pr, int index,USHORT SiS_DDRDRAM_TYPE[][5]);
void     SiS_SetDRAMSizingType(SiS_Private *SiS_Pr, int index,USHORT DRAMTYPE_TABLE[][5]);
void     SiS_CheckBusWidth_310(SiS_Private *SiS_Pr, UCHAR *ROMAddress,ULONG FBAddress,
                               PSIS_HW_DEVICE_INFO HwDeviceExtension);
int      SiS_SetRank(SiS_Private *SiS_Pr, int index,UCHAR RankNo,USHORT DRAMTYPE_TABLE[][5]);
int      SiS_SetDDRChannel(SiS_Private *SiS_Pr, int index,UCHAR ChannelNo,
                           USHORT DRAMTYPE_TABLE[][5]);
int      SiS_CheckColumn(SiS_Private *SiS_Pr, int index,USHORT DRAMTYPE_TABLE[][5],ULONG FBAddress);
int      SiS_CheckBanks(SiS_Private *SiS_Pr, int index,USHORT DRAMTYPE_TABLE[][5],ULONG FBAddress);
int      SiS_CheckRank(SiS_Private *SiS_Pr, int RankNo,int index,USHORT DRAMTYPE_TABLE[][5],ULONG FBAddress);
int      SiS_CheckDDRRank(SiS_Private *SiS_Pr, int RankNo,int index,USHORT DRAMTYPE_TABLE[][5],ULONG FBAddress);
int      SiS_CheckRanks(SiS_Private *SiS_Pr, int RankNo,int index,USHORT DRAMTYPE_TABLE[][5],ULONG FBAddress);
int      SiS_CheckDDRRanks(SiS_Private *SiS_Pr, int RankNo,int index,USHORT DRAMTYPE_TABLE[][5],ULONG FBAddress);
int      SiS_SDRSizing(SiS_Private *SiS_Pr, ULONG FBAddress);
int      SiS_DDRSizing(SiS_Private *SiS_Pr, ULONG FBAddress);
int      Is315E(SiS_Private *SiS_Pr);
void     SiS_VerifyMclk(SiS_Private *SiS_Pr, ULONG FBAddr);
#endif

void     SiS_HandleCRT1(SiS_Private *SiS_Pr);
void     SiS_Handle301B_1400x1050(SiS_Private *SiS_Pr, USHORT ModeNo);
void     SiS_SetEnableDstn(SiS_Private *SiS_Pr);
void     SiS_Delay15us(SiS_Private *SiS_Pr);
BOOLEAN  SiS_SearchModeID(SiS_Private *SiS_Pr, UCHAR *ROMAddr, USHORT *ModeNo,USHORT *ModeIdIndex);
BOOLEAN  SiS_CheckMemorySize(SiS_Private *SiS_Pr, UCHAR *ROMAddr,PSIS_HW_DEVICE_INFO HwDeviceExtension,
                             USHORT ModeNo,USHORT ModeIdIndex);
UCHAR    SiS_GetModePtr(SiS_Private *SiS_Pr, UCHAR *ROMAddr, USHORT ModeNo,USHORT ModeIdIndex);
void     SiS_SetSeqRegs(SiS_Private *SiS_Pr, UCHAR *ROMAddr,USHORT StandTableIndex);
void     SiS_SetMiscRegs(SiS_Private *SiS_Pr, UCHAR *ROMAddr,USHORT StandTableIndex);
void     SiS_SetCRTCRegs(SiS_Private *SiS_Pr, UCHAR *ROMAddr,PSIS_HW_DEVICE_INFO HwDeviceExtension,
                         USHORT StandTableIndex);
void     SiS_SetATTRegs(SiS_Private *SiS_Pr, UCHAR *ROMAddr,USHORT StandTableIndex,
                        PSIS_HW_DEVICE_INFO HwDeviceExtension);
void     SiS_SetGRCRegs(SiS_Private *SiS_Pr, UCHAR *ROMAddr,USHORT StandTableIndex);
void     SiS_ClearExt1Regs(SiS_Private *SiS_Pr, PSIS_HW_DEVICE_INFO HwDeviceExtension);
void     SiS_SetSync(SiS_Private *SiS_Pr, UCHAR *ROMAddr,USHORT RefreshRateTableIndex);
void     SiS_SetCRT1CRTC(SiS_Private *SiS_Pr, UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex,
                         USHORT RefreshRateTableIndex,
			 PSIS_HW_DEVICE_INFO HwDeviceExtension);
BOOLEAN  SiS_GetLCDACRT1Ptr(SiS_Private *SiS_Pr, UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex,
                            USHORT RefreshRateTableIndex,USHORT *ResInfo,USHORT *DisplayType);
void     SiS_ResetCRT1VCLK(SiS_Private *SiS_Pr, UCHAR *ROMAddr,PSIS_HW_DEVICE_INFO HwDeviceExtension);
void     SiS_SetCRT1VCLK(SiS_Private *SiS_Pr, UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex,PSIS_HW_DEVICE_INFO,
                         USHORT RefreshRateTableIndex);
void     SiS_SetVCLKState(SiS_Private *SiS_Pr, UCHAR *ROMAddr,PSIS_HW_DEVICE_INFO, USHORT ModeNo,
                          USHORT RefreshRateTableIndex, USHORT ModeIdIndex);
void     SiS_LoadDAC(SiS_Private *SiS_Pr, PSIS_HW_DEVICE_INFO, UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex);
void     SiS_WriteDAC(SiS_Private *SiS_Pr, USHORT, USHORT, USHORT, USHORT, USHORT, USHORT);
void     SiS_DisplayOn(SiS_Private *SiS_Pr);
void 	 SiS_DisplayOff(SiS_Private *SiS_Pr);
void     SiS_SetCRT1ModeRegs(SiS_Private *SiS_Pr, UCHAR *ROMAddr,PSIS_HW_DEVICE_INFO,USHORT ModeNo,
                             USHORT ModeIdIndex,USHORT RefreshRateTableIndex);
void     SiS_GetVBType(SiS_Private *SiS_Pr, USHORT BaseAddr,PSIS_HW_DEVICE_INFO);
USHORT   SiS_ChkBUSWidth(SiS_Private *SiS_Pr, UCHAR *ROMAddr);
USHORT   SiS_GetModeIDLength(SiS_Private *SiS_Pr, UCHAR *ROMAddr, USHORT);
USHORT   SiS_GetRefindexLength(SiS_Private *SiS_Pr, UCHAR *ROMAddr, USHORT);
void     SiS_SetInterlace(SiS_Private *SiS_Pr, UCHAR *ROMAddr,USHORT ModeNo,USHORT RefreshRateTableIndex);
void     SiS_Set_LVDS_TRUMPION(SiS_Private *SiS_Pr, PSIS_HW_DEVICE_INFO HwDeviceExtension);
void     SiS_SetCRT1Offset(SiS_Private *SiS_Pr, UCHAR *ROMAddr,USHORT,USHORT,USHORT,PSIS_HW_DEVICE_INFO);
#ifdef SIS315H
void     SiS_SetCRT1FIFO_310(SiS_Private *SiS_Pr, UCHAR *ROMAddr,USHORT,USHORT,PSIS_HW_DEVICE_INFO);
#endif
#ifdef SIS300
void     SiS_SetCRT1FIFO_300(SiS_Private *SiS_Pr, UCHAR *ROMAddr,USHORT ModeNo,PSIS_HW_DEVICE_INFO,
                             USHORT RefreshRateTableIndex);
void     SiS_SetCRT1FIFO_630(SiS_Private *SiS_Pr, UCHAR *ROMAddr,USHORT ModeNo,PSIS_HW_DEVICE_INFO,
                             USHORT RefreshRateTableIndex);
USHORT   SiS_CalcDelay(SiS_Private *SiS_Pr, UCHAR *ROMAddr, USHORT VCLK,
                       USHORT colordepth, USHORT MCLK);
USHORT   SiS_DoCalcDelay(SiS_Private *SiS_Pr, USHORT MCLK, USHORT VCLK, USHORT colordepth, USHORT key);
USHORT   SiS_CalcDelay2(SiS_Private *SiS_Pr, UCHAR *ROMAddr, UCHAR,PSIS_HW_DEVICE_INFO HwDeviceExtension);
#endif
void     SiS_ClearBuffer(SiS_Private *SiS_Pr, PSIS_HW_DEVICE_INFO,USHORT ModeNo);
void     SiS_SetCRT1Group(SiS_Private *SiS_Pr, UCHAR *ROMAddr,PSIS_HW_DEVICE_INFO HwDeviceExtension,
                          USHORT ModeNo,USHORT ModeIdIndex,USHORT BaseAddr);
void     SiS_DetectMonitor(SiS_Private *SiS_Pr, PSIS_HW_DEVICE_INFO HwDeviceExtension,USHORT BaseAddr);
void     SiS_GetSenseStatus(SiS_Private *SiS_Pr, PSIS_HW_DEVICE_INFO HwDeviceExtension,UCHAR *ROMAddr);
USHORT   SiS_TestMonitorType(SiS_Private *SiS_Pr, UCHAR R_DAC,UCHAR G_DAC,UCHAR B_DAC);
USHORT   SiS_SenseCHTV(SiS_Private *SiS_Pr);
BOOLEAN  SiS_Sense(SiS_Private *SiS_Pr, USHORT tempbx,USHORT tempcx);
BOOLEAN  SiS_GetPanelID(SiS_Private *SiS_Pr, PSIS_HW_DEVICE_INFO);
BOOLEAN  SiS_GetLCDDDCInfo(SiS_Private *SiS_Pr, PSIS_HW_DEVICE_INFO);
USHORT   SiS_SenseLCD(SiS_Private *SiS_Pr, PSIS_HW_DEVICE_INFO);
void     SiSRegInit(SiS_Private *SiS_Pr, USHORT BaseAddr);
void     SiSInitPtr(SiS_Private *SiS_Pr, PSIS_HW_DEVICE_INFO HwDeviceExtension);
void     SiSSetLVDSetc(SiS_Private *SiS_Pr, PSIS_HW_DEVICE_INFO HwDeviceExtension,USHORT ModeNo);
void     SiSInitPCIetc(SiS_Private *SiS_Pr, PSIS_HW_DEVICE_INFO HwDeviceExtension);
void     SiSDetermineROMUsage(SiS_Private *SiS_Pr, PSIS_HW_DEVICE_INFO HwDeviceExtension, UCHAR *ROMAddr);

#ifdef LINUX_XF86
USHORT 		SiS_CheckBuildCustomMode(ScrnInfoPtr pScrn, DisplayModePtr mode, int VBFlags);
void    	SiS_SetPitch(SiS_Private *SiS_Pr, ScrnInfoPtr pScrn, UShort BaseAddr);
void    	SiS_SetPitchCRT1(SiS_Private *SiS_Pr, ScrnInfoPtr pScrn, UShort BaseAddr);
void    	SiS_SetPitchCRT2(SiS_Private *SiS_Pr, ScrnInfoPtr pScrn, UShort BaseAddr);
extern int      SiS_compute_vclk(int Clock, int *out_n, int *out_dn, int *out_div,
	     	 		    int *out_sbit, int *out_scale);
extern unsigned char SiS_GetSetBIOSScratch(ScrnInfoPtr pScrn, USHORT offset, unsigned char value);
extern unsigned char SiS_GetSetModeID(ScrnInfoPtr pScrn, unsigned char id);
extern USHORT 	     SiS_CalcModeIndex(ScrnInfoPtr pScrn, DisplayModePtr mode);
#endif

extern USHORT    SiS_GetOffset(SiS_Private *SiS_Pr, UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex,
                       USHORT RefreshRateTableIndex,PSIS_HW_DEVICE_INFO HwDeviceExtension);
extern USHORT    SiS_GetColorDepth(SiS_Private *SiS_Pr, UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex);
extern void      SiS_DisableBridge(SiS_Private *SiS_Pr, PSIS_HW_DEVICE_INFO HwDeviceExtension, USHORT BaseAddr);
extern BOOLEAN   SiS_SetCRT2Group301(SiS_Private *SiS_Pr, USHORT BaseAddr,UCHAR *ROMAddr,USHORT ModeNo,
                                     PSIS_HW_DEVICE_INFO HwDeviceExtension);
extern void      SiS_PresetScratchregister(SiS_Private *SiS_Pr, USHORT SiS_P3d4,
                                           PSIS_HW_DEVICE_INFO HwDeviceExtension);
extern void      SiS_UnLockCRT2(SiS_Private *SiS_Pr, PSIS_HW_DEVICE_INFO HwDeviceExtension,USHORT BaseAddr);
extern void      SiS_LockCRT2(SiS_Private *SiS_Pr, PSIS_HW_DEVICE_INFO HwDeviceExtension,USHORT BaseAddr);
extern BOOLEAN   SiS_BridgeIsOn(SiS_Private *SiS_Pr, USHORT BaseAddr);
extern BOOLEAN   SiS_BridgeIsEnable(SiS_Private *SiS_Pr, USHORT BaseAddr,PSIS_HW_DEVICE_INFO );
extern void      SiS_GetVBInfo(SiS_Private *SiS_Pr, USHORT BaseAddr,UCHAR *ROMAddr,USHORT ModeNo,
                               USHORT ModeIdIndex,PSIS_HW_DEVICE_INFO HwDeviceExtension, int chkcrt2mode);
extern BOOLEAN   SiS_GetLCDResInfo(SiS_Private *SiS_Pr, UCHAR *ROMAddr,USHORT ModeNo,
                                   USHORT ModeIdIndex, PSIS_HW_DEVICE_INFO HwDeviceExtension);
extern void      SiS_SetHiVision(SiS_Private *SiS_Pr, USHORT BaseAddr,PSIS_HW_DEVICE_INFO HwDeviceExtension);
extern USHORT    SiS_GetRatePtrCRT2(SiS_Private *SiS_Pr, UCHAR *ROMAddr, USHORT ModeNo,USHORT ModeIdIndex,
                                    PSIS_HW_DEVICE_INFO HwDeviceExtension);
extern void      SiS_WhatIsThis(SiS_Private *SiS_Pr, USHORT myvbinfo);
extern void      SiS_LongWait(SiS_Private *SiS_Pr);
extern void      SiS_SetRegOR(USHORT Port,USHORT Index,USHORT DataOR);
extern void      SiS_SetRegAND(USHORT Port,USHORT Index,USHORT DataAND);
extern void      SiS_SetRegANDOR(USHORT Port,USHORT Index,USHORT DataAND,USHORT DataOR);
extern USHORT    SiS_GetResInfo(SiS_Private *SiS_Pr, UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex);
extern void      SiS_SetCH700x(SiS_Private *SiS_Pr, USHORT tempax);
extern USHORT    SiS_GetCH700x(SiS_Private *SiS_Pr, USHORT tempax);
extern void      SiS_SetCH701x(SiS_Private *SiS_Pr, USHORT tempax);
extern USHORT    SiS_GetCH701x(SiS_Private *SiS_Pr, USHORT tempax);
extern void      SiS_SetCH70xx(SiS_Private *SiS_Pr, USHORT tempax);
extern USHORT    SiS_GetCH70xx(SiS_Private *SiS_Pr, USHORT tempax);
extern BOOLEAN   SiS_GetLVDSCRT1Ptr(SiS_Private *SiS_Pr, UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex,
                                    USHORT RefreshRateTableIndex,
		                    USHORT *ResInfo,USHORT *DisplayType);
extern USHORT    SiS_GetVCLK2Ptr(SiS_Private *SiS_Pr, UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex,
                                 USHORT RefreshRateTableIndex,
				 PSIS_HW_DEVICE_INFO HwDeviceExtension);
extern BOOLEAN   SiS_Is301B(SiS_Private *SiS_Pr, USHORT BaseAddr);
extern BOOLEAN   SiS_IsM650(SiS_Private *SiS_Pr, PSIS_HW_DEVICE_INFO HwDeviceExtension, USHORT BaseAddr);
extern BOOLEAN   SiS_LowModeStuff(SiS_Private *SiS_Pr, USHORT ModeNo,PSIS_HW_DEVICE_INFO HwDeviceExtension);
extern BOOLEAN   SiS_IsVAMode(SiS_Private *SiS_Pr, PSIS_HW_DEVICE_INFO HwDeviceExtension, USHORT BaseAddr);
extern BOOLEAN   SiS_IsDualEdge(SiS_Private *SiS_Pr, PSIS_HW_DEVICE_INFO HwDeviceExtension, USHORT BaseAddr);
extern USHORT    SiS_GetMCLK(SiS_Private *SiS_Pr, UCHAR *ROMAddr, PSIS_HW_DEVICE_INFO HwDeviceExtension);

#ifdef LINUX_KERNEL
int    sisfb_mode_rate_to_dclock(SiS_Private *SiS_Pr, PSIS_HW_DEVICE_INFO HwDeviceExtension,
			      unsigned char modeno, unsigned char rateindex);
int    sisfb_mode_rate_to_ddata(SiS_Private *SiS_Pr, PSIS_HW_DEVICE_INFO HwDeviceExtension,
			 unsigned char modeno, unsigned char rateindex,
			 ULONG *left_margin, ULONG *right_margin, 
			 ULONG *upper_margin, ULONG *lower_margin,
			 ULONG *hsync_len, ULONG *vsync_len,
			 ULONG *sync, ULONG *vmode);
#endif

#endif

