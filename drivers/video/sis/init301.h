/* $XFree86: xc/programs/Xserver/hw/xfree86/drivers/sis/init301.h,v 1.4 2000/12/02 01:16:17 dawes Exp $ */
#ifndef  _INIT301_
#define  _INIT301_

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
#include "sis.h"
#include "sis_regs.h"
#endif

#ifdef LINUX_KERNEL
#include <asm/io.h>
#include <linux/types.h>
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
#endif

USHORT   SiS_SetFlag;
USHORT   SiS_RVBHCFACT,SiS_RVBHCMAX,SiS_RVBHRS;
USHORT   SiS_VGAVT,SiS_VGAHT;
USHORT   SiS_VT,SiS_HT;
USHORT   SiS_VGAVDE,SiS_VGAHDE;
USHORT   SiS_VDE,SiS_HDE;
USHORT   SiS_NewFlickerMode,SiS_RY1COE,SiS_RY2COE,SiS_RY3COE,SiS_RY4COE;
USHORT   SiS_LCDHDES,SiS_LCDVDES;
USHORT   SiS_DDC_Port, SiS_DDC_Index,SiS_DDC_Data, SiS_DDC_Clk;
USHORT   SiS_DDC_DataShift, SiS_DDC_DeviceAddr, SiS_DDC_Flag;
USHORT   SiS_DDC_ReadAddr, SiS_DDC_Buffer;

USHORT   Panel800x600,   Panel1024x768,  Panel1280x1024, Panel1600x1200;
USHORT   Panel1280x960,  Panel1400x1050, Panel320x480,   Panel1152x768;
USHORT   Panel1280x768,  Panel1024x600,  Panel640x480,   Panel1152x864;
USHORT   PanelMax, PanelMinLVDS, PanelMin301;

USHORT   SiS_ChrontelInit;

extern   USHORT   SiS_CRT1Mode;
extern   USHORT   SiS_P3c4,SiS_P3d4;
extern   USHORT   SiS_P3ca;
extern   USHORT   SiS_P3c9;
extern   USHORT   SiS_P3da;
extern   USHORT   SiS_Part1Port,SiS_Part2Port;
extern   USHORT   SiS_Part3Port,SiS_Part4Port,SiS_Part5Port;
extern   USHORT   SiS_MDA_DAC[];
extern   USHORT   SiS_CGA_DAC[];
extern   USHORT   SiS_EGA_DAC[];
extern   USHORT   SiS_VGA_DAC[];
extern   USHORT   SiS_ModeType;
extern   USHORT   SiS_SelectCRT2Rate;
extern   USHORT   SiS_IF_DEF_LVDS;
extern   USHORT   SiS_IF_DEF_TRUMPION;
extern   USHORT   SiS_IF_DEF_CH70xx;
extern   USHORT   SiS_Backup70xx;
extern   USHORT   SiS_IF_DEF_HiVision;
extern   USHORT   SiS_IF_DEF_DSTN;   /*add for dstn*/
extern   USHORT   SiS_IF_DEF_FSTN;   /*add for fstn*/
extern   USHORT   SiS_VBInfo;
extern   USHORT   SiS_VBType;
extern   USHORT   SiS_VBExtInfo;
extern   USHORT   SiS_LCDResInfo;
extern   USHORT   SiS_LCDTypeInfo;
extern   USHORT   SiS_LCDInfo;
extern   USHORT   SiS_HiVision;

extern   BOOLEAN  SiS_SearchVBModeID(UCHAR *RomAddr, USHORT *);

BOOLEAN  SiS_Is301B(USHORT BaseAddr);
BOOLEAN  SiS_IsDisableCRT2(USHORT BaseAddr);
BOOLEAN  SiS_IsVAMode(PSIS_HW_DEVICE_INFO HwDeviceExtension, USHORT BaseAddr);
BOOLEAN  SiS_IsDualEdge(PSIS_HW_DEVICE_INFO HwDeviceExtension, USHORT BaseAddr);
BOOLEAN  SiS_CRT2IsLCD(USHORT BaseAddr);

void     SiS_SetDefCRT2ExtRegs(USHORT BaseAddr);
USHORT   SiS_GetRatePtrCRT2(UCHAR *ROMAddr, USHORT ModeNo,USHORT ModeIdIndex);
BOOLEAN  SiS_AdjustCRT2Rate(UCHAR *ROMAddr,USHORT ModeNo,USHORT MODEIdIndex,USHORT RefreshRateTableIndex,USHORT *i);
void     SiS_SaveCRT2Info(USHORT ModeNo);
void     SiS_GetCRT2Data(UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex,USHORT RefreshRateTableIndex,
                         PSIS_HW_DEVICE_INFO HwDeviceExtension);
void     SiS_GetCRT2DataLVDS(UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex,USHORT RefreshRateTableIndex,
                             PSIS_HW_DEVICE_INFO HwDeviceExtension);
void     SiS_GetCRT2PtrA(UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex,USHORT RefreshRateTableIndex,
                         USHORT *CRT2Index,USHORT *ResIndex);
void     SiS_GetCRT2Part2Ptr(UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex,
		             USHORT RefreshRateTableIndex,USHORT *CRT2Index,
		             USHORT *ResIndex);
void     SiS_GetCRT2Data301(UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex,USHORT RefreshRateTableIndex,
                            PSIS_HW_DEVICE_INFO HwDeviceExtension);
USHORT   SiS_GetResInfo(UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex);
void     SiS_GetCRT2ResInfo(UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex,PSIS_HW_DEVICE_INFO HwDeviceExtension);
void     SiS_GetRAMDAC2DATA(UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex,USHORT RefreshRateTableIndex,
                            PSIS_HW_DEVICE_INFO HwDeviceExtension);
void     SiS_GetCRT2Ptr(UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex,USHORT RefreshRateTableIndex,
		        USHORT *CRT2Index,USHORT *ResIndex,PSIS_HW_DEVICE_INFO HwDeviceExtension);
void     SiS_SetCRT2ModeRegs(USHORT BaseAddr,USHORT ModeNo,USHORT ModeIdIndex, PSIS_HW_DEVICE_INFO );
void     SiS_SetHiVision(USHORT BaseAddr,PSIS_HW_DEVICE_INFO HwDeviceExtension);

void     SiS_GetLVDSDesData(UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex,USHORT RefreshRateTableIndex,
                            PSIS_HW_DEVICE_INFO HwDeviceExtension);
void     SiS_SetCRT2Offset(USHORT Part1Port,UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex,
                           USHORT RefreshRateTableIndex,PSIS_HW_DEVICE_INFO HwDeviceExtension);
USHORT   SiS_GetOffset(UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex,USHORT RefreshRateTableIndex,
                       PSIS_HW_DEVICE_INFO HwDeviceExtension);
USHORT   SiS_GetColorDepth(UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex);
USHORT   SiS_GetMCLK(UCHAR *ROMAddr, PSIS_HW_DEVICE_INFO HwDeviceExtension);
USHORT   SiS_CalcDelayVB(void);
USHORT   SiS_GetVCLK2Ptr(UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex,
                         USHORT RefreshRateTableIndex,PSIS_HW_DEVICE_INFO HwDeviceExtension);
void     SiS_SetCRT2Sync(USHORT BaseAddr,UCHAR *ROMAddr,USHORT ModeNo,USHORT RefreshRateTableIndex,
                         PSIS_HW_DEVICE_INFO HwDeviceExtension);
void     SiS_SetRegANDOR(USHORT Port,USHORT Index,USHORT DataAND,USHORT DataOR);
void     SiS_SetRegOR(USHORT Port,USHORT Index,USHORT DataOR);
void     SiS_SetRegAND(USHORT Port,USHORT Index,USHORT DataAND);
USHORT   SiS_GetVGAHT2(void);
void     SiS_SetGroup2(USHORT BaseAddr,UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex,
                       USHORT RefreshRateTableIndex,PSIS_HW_DEVICE_INFO HwDeviceExtension);
void     SiS_SetGroup3(USHORT BaseAddr,UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex,
                       PSIS_HW_DEVICE_INFO HwDeviceExtension);
void     SiS_SetGroup4(USHORT  BaseAddr,UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex,
                       USHORT RefreshRateTableIndex,PSIS_HW_DEVICE_INFO HwDeviceExtension);
void     SiS_SetGroup5(USHORT  BaseAddr,UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex);
void     SiS_SetCRT2VCLK(USHORT BaseAddr,UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex,
                         USHORT RefreshRateTableIndex,PSIS_HW_DEVICE_INFO HwDeviceExtension);
void     SiS_EnableCRT2(void);
void     SiS_LoadDAC2(UCHAR *ROMAddr,USHORT Part5Port,USHORT ModeNo,USHORT ModeIdIndex);
void     SiS_WriteDAC2(USHORT Pdata,USHORT dl, USHORT ah, USHORT al, USHORT dh);
void     SiS_GetVBInfo301(USHORT BaseAddr,UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex,
                          PSIS_HW_DEVICE_INFO HwDeviceExtension);
BOOLEAN  SiS_GetLCDResInfo(UCHAR *ROMAddr,USHORT P3d4,USHORT ModeNo,USHORT ModeIdIndex);
BOOLEAN  SiS_BridgeIsOn(USHORT BaseAddr,PSIS_HW_DEVICE_INFO);
BOOLEAN  SiS_BridgeIsEnable(USHORT BaseAddr,PSIS_HW_DEVICE_INFO);
BOOLEAN  SiS_BridgeInSlave(void);
void     SiS_PresetScratchregister(USHORT SiS_P3d4,PSIS_HW_DEVICE_INFO HwDeviceExtension);
void     SiS_SetTVSystem(VOID);
void     SiS_LongWait(VOID);
USHORT   SiS_GetQueueConfig(VOID);
void     SiS_VBLongWait(VOID);
USHORT   SiS_GetVCLKLen(UCHAR *ROMAddr);
void     SiS_WaitVBRetrace(PSIS_HW_DEVICE_INFO HwDeviceExtension);
void     SiS_WaitRetrace1(PSIS_HW_DEVICE_INFO HwDeviceExtension);
void     SiS_WaitRetrace2(PSIS_HW_DEVICE_INFO HwDeviceExtension);
void     SiS_SetCRT2ECLK(UCHAR *ROMAddr, USHORT ModeNo,USHORT ModeIdIndex,USHORT RefreshRateTableIndex,
                         PSIS_HW_DEVICE_INFO HwDeviceExtension);
void     SiS_GetLVDSDesPtr(UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex,USHORT RefreshRateTableIndex,
                           USHORT *PanelIndex,USHORT *ResIndex,PSIS_HW_DEVICE_INFO HwDeviceExtension);
void     SiS_GetLVDSDesPtrA(UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex,USHORT RefreshRateTableIndex,
                            USHORT *PanelIndex,USHORT *ResIndex);
void     SiS_SetTPData(VOID);
void     SiS_ModCRT1CRTC(UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex,USHORT RefreshRateTableIndex,
			 PSIS_HW_DEVICE_INFO HwDeviceExtension);
extern BOOLEAN  SiS_GetLVDSCRT1Ptr(UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex,USHORT RefreshRateTableIndex,
		       USHORT *ResInfo,USHORT *DisplayType);
void     SiS_SetCHTVReg(UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex,USHORT RefreshRateTableIndex);
void     SiS_GetCHTVRegPtr(UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex,USHORT RefreshRateTableIndex);
void     SiS_SetCH700x(USHORT tempax);
USHORT   SiS_GetCH700x(USHORT tempax);
void     SiS_SetCH701x(USHORT tempax);
USHORT   SiS_GetCH701x(USHORT tempax);
void     SiS_SetCH70xx(USHORT tempax);
USHORT   SiS_GetCH70xx(USHORT tempax);
void     SiS_SetCH70xxANDOR(USHORT tempax,USHORT tempbh);
void     SiS_SetSwitchDDC2(void);
USHORT   SiS_SetStart(void);
USHORT   SiS_SetStop(void);
void     SiS_DDC2Delay(USHORT delaytime);
USHORT   SiS_SetSCLKLow(void);
USHORT   SiS_SetSCLKHigh(void);
USHORT   SiS_ReadDDC2Data(USHORT tempax);
USHORT   SiS_WriteDDC2Data(USHORT tempax);
USHORT   SiS_CheckACK(void);
#ifdef SIS315H
void     SiS_OEM310Setting(PSIS_HW_DEVICE_INFO HwDeviceExtension,USHORT BaseAddr,
                           UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex);
void     SiS_OEMLCD(PSIS_HW_DEVICE_INFO HwDeviceExtension,USHORT BaseAddr,
                    UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex);
#endif
#ifdef SIS300
void     SiS_OEM300Setting(PSIS_HW_DEVICE_INFO HwDeviceExtension,USHORT BaseAddr,
                           UCHAR *ROMAddr,USHORT ModeNo);
#endif
USHORT   GetRevisionID(PSIS_HW_DEVICE_INFO HwDeviceExtension);
BOOLEAN  SiS_LowModeStuff(USHORT ModeNo,PSIS_HW_DEVICE_INFO HwDeviceExtension);

BOOLEAN  SiS_GetLCDResInfo301(UCHAR *ROMAddr,USHORT SiS_P3d4, USHORT ModeNo, USHORT ModeIdIndex,
                             PSIS_HW_DEVICE_INFO HwDeviceExtension);
/* void    SiS_CHACRT1CRTC(UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex,
                        USHORT RefreshRateTableIndex); */
BOOLEAN  SiS_GetLCDACRT1Ptr(UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex,
		   USHORT RefreshRateTableIndex,USHORT *ResInfo,
		   USHORT *DisplayType);
/* 310 series OEM */
USHORT 	GetLCDPtrIndex (void);
USHORT  GetTVPtrIndex(void);
void    SetDelayComp(PSIS_HW_DEVICE_INFO HwDeviceExtension,USHORT BaseAddr,
             	     UCHAR *ROMAddr,USHORT ModeNo);
void    SetAntiFlicker(PSIS_HW_DEVICE_INFO HwDeviceExtension,USHORT BaseAddr,
               	       UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex);
void    SetEdgeEnhance (PSIS_HW_DEVICE_INFO HwDeviceExtension,USHORT BaseAddr,
                        UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex);
void    SetYFilter(PSIS_HW_DEVICE_INFO HwDeviceExtension,USHORT BaseAddr,
           	   UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex);
void	SetPhaseIncr(PSIS_HW_DEVICE_INFO HwDeviceExtension,USHORT BaseAddr,
             	     UCHAR *ROMAddr,USHORT ModeNo);
/* 300 series OEM */
USHORT 	GetOEMLCDPtr(PSIS_HW_DEVICE_INFO HwDeviceExtension, int Flag);
USHORT 	GetOEMTVPtr(void);
void	SetOEMTVDelay(PSIS_HW_DEVICE_INFO HwDeviceExtension,USHORT BaseAddr,
              	      UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex);
void	SetOEMLCDDelay(PSIS_HW_DEVICE_INFO HwDeviceExtension,USHORT BaseAddr,
               	       UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex);
void	SetOEMAntiFlicker(PSIS_HW_DEVICE_INFO HwDeviceExtension,
                          USHORT BaseAddr,UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex);
void  	SetOEMPhaseIncr(PSIS_HW_DEVICE_INFO HwDeviceExtension,USHORT BaseAddr,
                        UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex);
void	SetOEMYFilter(PSIS_HW_DEVICE_INFO HwDeviceExtension,USHORT BaseAddr,
              	      UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex);

extern   void     SiS_SetReg1(USHORT, USHORT, USHORT);
extern   void     SiS_SetReg3(USHORT, USHORT);
extern   UCHAR    SiS_GetReg1(USHORT, USHORT);
extern   UCHAR    SiS_GetReg2(USHORT);
extern   BOOLEAN  SiS_SearchModeID(UCHAR *ROMAddr, USHORT *ModeNo,USHORT *ModeIdIndex);
extern   BOOLEAN  SiS_GetRatePtr(ULONG, USHORT);
extern   void     SiS_SetReg4(USHORT, ULONG);
extern   ULONG    SiS_GetReg3(USHORT);
extern   void     SiS_DisplayOff(void);
extern   void     SiS_DisplayOn(void);
extern   UCHAR    SiS_GetModePtr(UCHAR *ROMAddr, USHORT ModeNo,USHORT ModeIdIndex);
#ifdef SIS315H
extern   UCHAR    SiS_Get310DRAMType(UCHAR *ROMAddr,PSIS_HW_DEVICE_INFO HwDeviceExtension);
#endif

BOOLEAN  SiS_SetCRT2Group301(USHORT BaseAddr,UCHAR *ROMAddr,USHORT ModeNo,
                             PSIS_HW_DEVICE_INFO HwDeviceExtension);
void     SiS_SetGroup1(USHORT BaseAddr,UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex,
                       PSIS_HW_DEVICE_INFO HwDeviceExtension,USHORT RefreshRateTableIndex);
void     SiS_SetGroup1_LVDS(USHORT BaseAddr,UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex,
                            PSIS_HW_DEVICE_INFO HwDeviceExtension,USHORT RefreshRateTableIndex);
void     SiS_SetGroup1_LCDA(USHORT  BaseAddr,UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex,
                            PSIS_HW_DEVICE_INFO HwDeviceExtension,USHORT RefreshRateTableIndex);/*301b*/
void     SiS_SetGroup1_301(USHORT BaseAddr,UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex,
                           PSIS_HW_DEVICE_INFO HwDeviceExtension,USHORT RefreshRateTableIndex);
#ifdef SIS300
void     SiS_SetCRT2FIFO_300(UCHAR *ROMAddr,USHORT ModeNo,
                             PSIS_HW_DEVICE_INFO HwDeviceExtension);
#endif
#ifdef SIS315H
void     SiS_SetCRT2FIFO_310(UCHAR *ROMAddr,USHORT ModeNo,
                             PSIS_HW_DEVICE_INFO HwDeviceExtension);
void     SiS_CRT2AutoThreshold(USHORT  BaseAddr);
#endif
BOOLEAN  SiS_GetLCDDDCInfo(PSIS_HW_DEVICE_INFO HwDeviceExtension);
void     SiS_UnLockCRT2(PSIS_HW_DEVICE_INFO,USHORT BaseAddr);
void     SiS_LockCRT2(PSIS_HW_DEVICE_INFO,USHORT BaseAddr);
void     SiS_DisableBridge(PSIS_HW_DEVICE_INFO,USHORT  BaseAddr);
void     SiS_EnableBridge(PSIS_HW_DEVICE_INFO,USHORT BaseAddr);
void     SiS_SetPanelDelay(UCHAR* ROMAddr,PSIS_HW_DEVICE_INFO,USHORT DelayTime);
void     SiS_ShortDelay(USHORT delay);
void     SiS_LongDelay(USHORT delay);
void     SiS_GenericDelay(USHORT delay);
void     SiS_VBWait(void);

/* TW: New functions (with temporary names) */
void     SiS_Chrontel701xOn(void);
void     SiS_Chrontel701xOn2(PSIS_HW_DEVICE_INFO HwDeviceExtension,
                           USHORT BaseAddr);
void     SiS_Chrontel701xOff(void);
void     SiS_Chrontel701xOff2(void);
void     SiS_ChrontelFlip0x48(void);
void     SiS_ChrontelDoSomething4(PSIS_HW_DEVICE_INFO HwDeviceExtension, USHORT BaseAddr);
void     SiS_ChrontelDoSomething3(USHORT ModeNo, PSIS_HW_DEVICE_INFO HwDeviceExtension, USHORT BaseAddr);
void     SiS_ChrontelDoSomething2(PSIS_HW_DEVICE_INFO HwDeviceExtension, USHORT BaseAddr);
void     SiS_ChrontelDoSomething1(PSIS_HW_DEVICE_INFO HwDeviceExtension, USHORT BaseAddr);
BOOLEAN  SiS_WeHaveBacklightCtrl(PSIS_HW_DEVICE_INFO HwDeviceExtension, USHORT BaseAddr);
#if 0
BOOLEAN  SiS_IsSomethingCR5F(PSIS_HW_DEVICE_INFO HwDeviceExtension, USHORT BaseAddr);
#endif
BOOLEAN  SiS_IsYPbPr(PSIS_HW_DEVICE_INFO HwDeviceExtension, USHORT BaseAddr);
BOOLEAN  SiS_IsTVOrSomething(PSIS_HW_DEVICE_INFO HwDeviceExtension, USHORT BaseAddr);
BOOLEAN  SiS_IsLCDOrLCDA(PSIS_HW_DEVICE_INFO HwDeviceExtension, USHORT BaseAddr);
void     SiS_SetCHTVForLCD(PSIS_HW_DEVICE_INFO HwDeviceExtension, USHORT BaseAddr);
void     SiS_Chrontel19f2(void);
BOOLEAN  SiS_CR36BIOSWord23b(PSIS_HW_DEVICE_INFO HwDeviceExtension);
BOOLEAN  SiS_CR36BIOSWord23d(PSIS_HW_DEVICE_INFO HwDeviceExtension);
BOOLEAN  SiS_IsSR13_CR30(PSIS_HW_DEVICE_INFO HwDeviceExtension);

/* TW end */

#endif
