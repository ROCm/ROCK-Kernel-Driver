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
#include <linux/config.h>
#include <linux/version.h>
#include <asm/io.h>
#include <linux/types.h>
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
#endif

#if 0
extern   const USHORT   SiS_MDA_DAC[];
extern   const USHORT   SiS_CGA_DAC[];
extern   const USHORT   SiS_EGA_DAC[];
extern   const USHORT   SiS_VGA_DAC[];
#endif

extern   BOOLEAN  SiS_SearchVBModeID(SiS_Private *SiS_Pr, UCHAR *RomAddr, USHORT *);

BOOLEAN  SiS_Is301B(SiS_Private *SiS_Pr, USHORT BaseAddr);
BOOLEAN  SiS_IsNotM650or651(SiS_Private *SiS_Pr, PSIS_HW_DEVICE_INFO HwDeviceExtension, USHORT BaseAddr);
BOOLEAN  SiS_IsDisableCRT2(SiS_Private *SiS_Pr, USHORT BaseAddr);
BOOLEAN  SiS_IsVAMode(SiS_Private *SiS_Pr, PSIS_HW_DEVICE_INFO HwDeviceExtension, USHORT BaseAddr);
BOOLEAN  SiS_IsDualEdge(SiS_Private *SiS_Pr, PSIS_HW_DEVICE_INFO HwDeviceExtension, USHORT BaseAddr);
BOOLEAN  SiS_CRT2IsLCD(SiS_Private *SiS_Pr, USHORT BaseAddr, PSIS_HW_DEVICE_INFO HwDeviceExtension);
void     SiS_SetDefCRT2ExtRegs(SiS_Private *SiS_Pr, USHORT BaseAddr);
USHORT   SiS_GetRatePtrCRT2(SiS_Private *SiS_Pr, UCHAR *ROMAddr, USHORT ModeNo,USHORT ModeIdIndex,
                            PSIS_HW_DEVICE_INFO HwDeviceExtension);
BOOLEAN  SiS_AdjustCRT2Rate(SiS_Private *SiS_Pr, UCHAR *ROMAddr,USHORT ModeNo,USHORT MODEIdIndex,
                            USHORT RefreshRateTableIndex,USHORT *i,PSIS_HW_DEVICE_INFO HwDeviceExtension);
void     SiS_SaveCRT2Info(SiS_Private *SiS_Pr, USHORT ModeNo);
void     SiS_GetCRT2Data(SiS_Private *SiS_Pr, UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex,
		         USHORT RefreshRateTableIndex,PSIS_HW_DEVICE_INFO HwDeviceExtension);
void     SiS_GetCRT2DataLVDS(SiS_Private *SiS_Pr, UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex,
                             USHORT RefreshRateTableIndex,PSIS_HW_DEVICE_INFO HwDeviceExtension);
#ifdef SIS315H			     
void     SiS_GetCRT2PtrA(SiS_Private *SiS_Pr, UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex,
                         USHORT RefreshRateTableIndex,USHORT *CRT2Index,USHORT *ResIndex);
#endif
void     SiS_GetCRT2Part2Ptr(SiS_Private *SiS_Pr, UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex,
		             USHORT RefreshRateTableIndex,USHORT *CRT2Index, USHORT *ResIndex);
void     SiS_GetCRT2Data301(SiS_Private *SiS_Pr, UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex,
                            USHORT RefreshRateTableIndex,PSIS_HW_DEVICE_INFO HwDeviceExtension);
USHORT   SiS_GetResInfo(SiS_Private *SiS_Pr, UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex);
void     SiS_GetCRT2ResInfo(SiS_Private *SiS_Pr, UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex,
                            PSIS_HW_DEVICE_INFO HwDeviceExtension);
void     SiS_GetRAMDAC2DATA(SiS_Private *SiS_Pr, UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex,
                            USHORT RefreshRateTableIndex,PSIS_HW_DEVICE_INFO HwDeviceExtension);
void     SiS_GetCRT2Ptr(SiS_Private *SiS_Pr, UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex,
                        USHORT RefreshRateTableIndex,USHORT *CRT2Index,USHORT *ResIndex,
			PSIS_HW_DEVICE_INFO HwDeviceExtension);
void     SiS_SetCRT2ModeRegs(SiS_Private *SiS_Pr, USHORT BaseAddr,USHORT ModeNo,USHORT ModeIdIndex,
                             PSIS_HW_DEVICE_INFO );
void     SiS_SetHiVision(SiS_Private *SiS_Pr, USHORT BaseAddr,PSIS_HW_DEVICE_INFO HwDeviceExtension);
void     SiS_GetLVDSDesData(SiS_Private *SiS_Pr, UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex,
			    USHORT RefreshRateTableIndex,PSIS_HW_DEVICE_INFO HwDeviceExtension);
void     SiS_SetCRT2Offset(SiS_Private *SiS_Pr, UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex,
                           USHORT RefreshRateTableIndex,PSIS_HW_DEVICE_INFO HwDeviceExtension);
USHORT   SiS_GetOffset(SiS_Private *SiS_Pr, UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex,
                       USHORT RefreshRateTableIndex,PSIS_HW_DEVICE_INFO HwDeviceExtension);
USHORT   SiS_GetColorDepth(SiS_Private *SiS_Pr, UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex);
USHORT   SiS_GetMCLK(SiS_Private *SiS_Pr, UCHAR *ROMAddr, PSIS_HW_DEVICE_INFO HwDeviceExtension);
USHORT   SiS_CalcDelayVB(SiS_Private *SiS_Pr);
USHORT   SiS_GetVCLK2Ptr(SiS_Private *SiS_Pr, UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex,
                         USHORT RefreshRateTableIndex,PSIS_HW_DEVICE_INFO HwDeviceExtension);
void     SiS_SetCRT2Sync(SiS_Private *SiS_Pr, USHORT BaseAddr,UCHAR *ROMAddr,USHORT ModeNo,
                         USHORT RefreshRateTableIndex,PSIS_HW_DEVICE_INFO HwDeviceExtension);
void     SiS_SetRegANDOR(USHORT Port,USHORT Index,USHORT DataAND,USHORT DataOR);
void     SiS_SetRegOR(USHORT Port,USHORT Index,USHORT DataOR);
void     SiS_SetRegAND(USHORT Port,USHORT Index,USHORT DataAND);
USHORT   SiS_GetVGAHT2(SiS_Private *SiS_Pr);
void     SiS_Set300Part2Regs(SiS_Private *SiS_Pr, PSIS_HW_DEVICE_INFO HwDeviceExtension,
    	 		     USHORT ModeIdIndex, USHORT RefreshRateTableIndex,
			     USHORT BaseAddr, USHORT ModeNo);
void     SiS_SetGroup2(SiS_Private *SiS_Pr, USHORT BaseAddr,UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex,
                       USHORT RefreshRateTableIndex,PSIS_HW_DEVICE_INFO HwDeviceExtension);
void     SiS_SetGroup3(SiS_Private *SiS_Pr, USHORT BaseAddr,UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex,
                       PSIS_HW_DEVICE_INFO HwDeviceExtension);
void     SiS_SetGroup4(SiS_Private *SiS_Pr, USHORT  BaseAddr,UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex,
                       USHORT RefreshRateTableIndex,PSIS_HW_DEVICE_INFO HwDeviceExtension);
void     SiS_SetGroup5(SiS_Private *SiS_Pr, PSIS_HW_DEVICE_INFO, USHORT  BaseAddr,UCHAR *ROMAddr,USHORT ModeNo,
                       USHORT ModeIdIndex);
void     SiS_FinalizeLCD(SiS_Private *SiS_Pr, USHORT BaseAddr,UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex,
                         PSIS_HW_DEVICE_INFO HwDeviceExtension);
void     SiS_SetCRT2VCLK(SiS_Private *SiS_Pr, USHORT BaseAddr,UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex,
                         USHORT RefreshRateTableIndex,PSIS_HW_DEVICE_INFO HwDeviceExtension);
void     SiS_EnableCRT2(SiS_Private *SiS_Pr);
void     SiS_GetVBInfo(SiS_Private *SiS_Pr, USHORT BaseAddr,UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex,
                       PSIS_HW_DEVICE_INFO HwDeviceExtension, int checkcrt2mode);
BOOLEAN  SiS_BridgeIsOn(SiS_Private *SiS_Pr, USHORT BaseAddr,PSIS_HW_DEVICE_INFO);
BOOLEAN  SiS_BridgeIsEnable(SiS_Private *SiS_Pr, USHORT BaseAddr,PSIS_HW_DEVICE_INFO);
BOOLEAN  SiS_BridgeInSlave(SiS_Private *SiS_Pr);
void     SiS_PresetScratchregister(SiS_Private *SiS_Pr, PSIS_HW_DEVICE_INFO HwDeviceExtension);
void     SiS_SetTVSystem(SiS_Private *SiS_Pr);
void     SiS_LongWait(SiS_Private *SiS_Pr);
USHORT   SiS_GetQueueConfig(SiS_Private *SiS_Pr);
void     SiS_VBLongWait(SiS_Private *SiS_Pr);
USHORT   SiS_GetVCLKLen(SiS_Private *SiS_Pr, UCHAR *ROMAddr);
void     SiS_WaitVBRetrace(SiS_Private *SiS_Pr, PSIS_HW_DEVICE_INFO HwDeviceExtension);
void     SiS_WaitRetrace1(SiS_Private *SiS_Pr, PSIS_HW_DEVICE_INFO HwDeviceExtension);
void     SiS_WaitRetrace2(SiS_Private *SiS_Pr, PSIS_HW_DEVICE_INFO HwDeviceExtension);
void     SiS_WaitRetraceDDC(SiS_Private *SiS_Pr);
void     SiS_SetCRT2ECLK(SiS_Private *SiS_Pr, UCHAR *ROMAddr, USHORT ModeNo,USHORT ModeIdIndex,
                         USHORT RefreshRateTableIndex,PSIS_HW_DEVICE_INFO HwDeviceExtension);
void     SiS_GetLVDSDesPtr(SiS_Private *SiS_Pr, UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex,
                           USHORT RefreshRateTableIndex,USHORT *PanelIndex,USHORT *ResIndex,
			   PSIS_HW_DEVICE_INFO HwDeviceExtension);
#ifdef SIS315H			   
void     SiS_GetLVDSDesPtrA(SiS_Private *SiS_Pr, UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex,
                            USHORT RefreshRateTableIndex,USHORT *PanelIndex,USHORT *ResIndex);
#endif			    
void     SiS_SetTPData(SiS_Private *SiS_Pr);
void     SiS_WhatIsThis(SiS_Private *SiS_Pr, USHORT myvbinfo);
void     SiS_ModCRT1CRTC(SiS_Private *SiS_Pr, UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex,
                         USHORT RefreshRateTableIndex,PSIS_HW_DEVICE_INFO HwDeviceExtension);
void     SiS_SetCHTVReg(SiS_Private *SiS_Pr, UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex,
                        USHORT RefreshRateTableIndex);
void     SiS_GetCHTVRegPtr(SiS_Private *SiS_Pr, UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex,
                           USHORT RefreshRateTableIndex);
void     SiS_SetCH700x(SiS_Private *SiS_Pr, USHORT tempax);
USHORT   SiS_GetCH700x(SiS_Private *SiS_Pr, USHORT tempax);
void     SiS_SetCH701x(SiS_Private *SiS_Pr, USHORT tempax);
USHORT   SiS_GetCH701x(SiS_Private *SiS_Pr, USHORT tempax);
void     SiS_SetCH70xx(SiS_Private *SiS_Pr, USHORT tempax);
USHORT   SiS_GetCH70xx(SiS_Private *SiS_Pr, USHORT tempax);
#ifdef LINUX_XF86
USHORT   SiS_I2C_GetByte(SiS_Private *SiS_Pr);
Bool     SiS_I2C_PutByte(SiS_Private *SiS_Pr, USHORT data);
Bool     SiS_I2C_Address(SiS_Private *SiS_Pr, USHORT addr);
void     SiS_I2C_Stop(SiS_Private *SiS_Pr);
#endif
void     SiS_SetCH70xxANDOR(SiS_Private *SiS_Pr, USHORT tempax,USHORT tempbh);
void     SiS_SetSwitchDDC2(SiS_Private *SiS_Pr);
USHORT   SiS_SetStart(SiS_Private *SiS_Pr);
USHORT   SiS_SetStop(SiS_Private *SiS_Pr);
void     SiS_DDC2Delay(SiS_Private *SiS_Pr, USHORT delaytime);
USHORT   SiS_SetSCLKLow(SiS_Private *SiS_Pr);
USHORT   SiS_SetSCLKHigh(SiS_Private *SiS_Pr);
USHORT   SiS_ReadDDC2Data(SiS_Private *SiS_Pr, USHORT tempax);
USHORT   SiS_WriteDDC2Data(SiS_Private *SiS_Pr, USHORT tempax);
USHORT   SiS_CheckACK(SiS_Private *SiS_Pr);
USHORT   SiS_ReadLCDDDC(SiS_Private *SiS_Pr, USHORT length, unsigned char *buffer);
#ifdef LINUX_XF86
USHORT   SiS_SenseLCDDDC(SiS_Private *SiS_Pr, SISPtr pSiS);
USHORT   SiS_SenseVGA2DDC(SiS_Private *SiS_Pr, SISPtr pSiS);
#endif
#ifdef SIS315H
void     SiS_OEM310Setting(SiS_Private *SiS_Pr, PSIS_HW_DEVICE_INFO HwDeviceExtension,USHORT BaseAddr,
                           UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex);
void     SiS_OEMLCD(SiS_Private *SiS_Pr, PSIS_HW_DEVICE_INFO HwDeviceExtension,USHORT BaseAddr,
                    UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex);
#endif
#ifdef SIS300
void     SiS_OEM300Setting(SiS_Private *SiS_Pr, PSIS_HW_DEVICE_INFO HwDeviceExtension,USHORT BaseAddr,
                           UCHAR *ROMAddr,USHORT ModeNo);
#endif
BOOLEAN  SiS_LowModeStuff(SiS_Private *SiS_Pr, USHORT ModeNo,PSIS_HW_DEVICE_INFO HwDeviceExtension);

BOOLEAN  SiS_GetLCDResInfo(SiS_Private *SiS_Pr, UCHAR *ROMAddr,USHORT ModeNo, USHORT ModeIdIndex,
                           PSIS_HW_DEVICE_INFO HwDeviceExtension);
/* void    SiS_CHACRT1CRTC(SiS_Private *SiS_Pr, UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex,
                        USHORT RefreshRateTableIndex); */

BOOLEAN  SiS_SetCRT2Group301(SiS_Private *SiS_Pr, USHORT BaseAddr,UCHAR *ROMAddr,USHORT ModeNo,
                             PSIS_HW_DEVICE_INFO HwDeviceExtension);
void     SiS_SetGroup1(SiS_Private *SiS_Pr, USHORT BaseAddr,UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex,
                       PSIS_HW_DEVICE_INFO HwDeviceExtension,USHORT RefreshRateTableIndex);
void     SiS_SetGroup1_LVDS(SiS_Private *SiS_Pr, USHORT BaseAddr,UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex,
                            PSIS_HW_DEVICE_INFO HwDeviceExtension,USHORT RefreshRateTableIndex);
#ifdef SIS315H			    
void     SiS_SetGroup1_LCDA(SiS_Private *SiS_Pr, USHORT  BaseAddr,UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex,
                            PSIS_HW_DEVICE_INFO HwDeviceExtension,USHORT RefreshRateTableIndex);
#endif			    
void     SiS_SetGroup1_301(SiS_Private *SiS_Pr, USHORT BaseAddr,UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex,
                           PSIS_HW_DEVICE_INFO HwDeviceExtension,USHORT RefreshRateTableIndex);
#ifdef SIS300
void     SiS_SetCRT2FIFO_300(SiS_Private *SiS_Pr, UCHAR *ROMAddr,USHORT ModeNo,
                             PSIS_HW_DEVICE_INFO HwDeviceExtension);
#endif
#ifdef SIS315H
void     SiS_SetCRT2FIFO_310(SiS_Private *SiS_Pr, UCHAR *ROMAddr,USHORT ModeNo,
                             PSIS_HW_DEVICE_INFO HwDeviceExtension);
void     SiS_CRT2AutoThreshold(SiS_Private *SiS_Pr, USHORT  BaseAddr);
#endif
BOOLEAN  SiS_GetLCDDDCInfo(SiS_Private *SiS_Pr, PSIS_HW_DEVICE_INFO HwDeviceExtension);
void     SiS_UnLockCRT2(SiS_Private *SiS_Pr, PSIS_HW_DEVICE_INFO,USHORT BaseAddr);
void     SiS_LockCRT2(SiS_Private *SiS_Pr, PSIS_HW_DEVICE_INFO,USHORT BaseAddr);
void     SiS_DisableBridge(SiS_Private *SiS_Pr, PSIS_HW_DEVICE_INFO,USHORT  BaseAddr);
void     SiS_EnableBridge(SiS_Private *SiS_Pr, PSIS_HW_DEVICE_INFO,USHORT BaseAddr);
void     SiS_SetPanelDelay(SiS_Private *SiS_Pr, UCHAR* ROMAddr,PSIS_HW_DEVICE_INFO,USHORT DelayTime);
void     SiS_SetPanelDelayLoop(SiS_Private *SiS_Pr, UCHAR *ROMAddr, PSIS_HW_DEVICE_INFO HwDeviceExtension,
                               USHORT DelayTime, USHORT DelayLoop);
void     SiS_ShortDelay(SiS_Private *SiS_Pr, USHORT delay);
void     SiS_LongDelay(SiS_Private *SiS_Pr, USHORT delay);
void     SiS_GenericDelay(SiS_Private *SiS_Pr, USHORT delay);
void     SiS_VBWait(SiS_Private *SiS_Pr);

void     SiS_SiS30xBLOn(SiS_Private *SiS_Pr, PSIS_HW_DEVICE_INFO HwDeviceExtension);
void     SiS_SiS30xBLOff(SiS_Private *SiS_Pr, PSIS_HW_DEVICE_INFO HwDeviceExtension);

void     SiS_Chrontel701xBLOn(SiS_Private *SiS_Pr);
void     SiS_Chrontel701xBLOff(SiS_Private *SiS_Pr);
#ifdef SIS315H
void     SiS_Chrontel701xOn(SiS_Private *SiS_Pr, PSIS_HW_DEVICE_INFO HwDeviceExtension,
                            USHORT BaseAddr);
void     SiS_Chrontel701xOff(SiS_Private *SiS_Pr);
void     SiS_ChrontelResetDB(SiS_Private *SiS_Pr, PSIS_HW_DEVICE_INFO HwDeviceExtension, USHORT BaseAddr);
void     SiS_ChrontelDoSomething4(SiS_Private *SiS_Pr, PSIS_HW_DEVICE_INFO HwDeviceExtension, USHORT BaseAddr);
void     SiS_ChrontelDoSomething3(SiS_Private *SiS_Pr, USHORT ModeNo, PSIS_HW_DEVICE_INFO HwDeviceExtension, USHORT BaseAddr);
void     SiS_ChrontelDoSomething2(SiS_Private *SiS_Pr, PSIS_HW_DEVICE_INFO HwDeviceExtension, USHORT BaseAddr);
void     SiS_ChrontelDoSomething1(SiS_Private *SiS_Pr, PSIS_HW_DEVICE_INFO HwDeviceExtension, USHORT BaseAddr);
BOOLEAN  SiS_WeHaveBacklightCtrl(SiS_Private *SiS_Pr, PSIS_HW_DEVICE_INFO HwDeviceExtension, USHORT BaseAddr);
void     SiS_ChrontelPowerSequencing(SiS_Private *SiS_Pr);
void     SiS_SetCH701xForLCD(SiS_Private *SiS_Pr, PSIS_HW_DEVICE_INFO HwDeviceExtension, USHORT BaseAddr);
#ifdef NEWCH701x
void     SiS_ChrontelDoSomething5(SiS_Private *SiS_Pr);
#endif
#endif /* 315 */
#if 0
BOOLEAN  SiS_IsSomethingCR5F(SiS_Private *SiS_Pr, PSIS_HW_DEVICE_INFO HwDeviceExtension, USHORT BaseAddr);
#endif
BOOLEAN  SiS_IsYPbPr(SiS_Private *SiS_Pr, PSIS_HW_DEVICE_INFO HwDeviceExtension, USHORT BaseAddr);
BOOLEAN  SiS_IsChScart(SiS_Private *SiS_Pr, PSIS_HW_DEVICE_INFO HwDeviceExtension, USHORT BaseAddr);
BOOLEAN  SiS_IsTVOrYPbPrOrScart(SiS_Private *SiS_Pr, PSIS_HW_DEVICE_INFO HwDeviceExtension, USHORT BaseAddr);
BOOLEAN  SiS_IsLCDOrLCDA(SiS_Private *SiS_Pr, PSIS_HW_DEVICE_INFO HwDeviceExtension, USHORT BaseAddr);
BOOLEAN  SiS_CR36BIOSWord23b(SiS_Private *SiS_Pr, PSIS_HW_DEVICE_INFO HwDeviceExtension);
BOOLEAN  SiS_CR36BIOSWord23d(SiS_Private *SiS_Pr, PSIS_HW_DEVICE_INFO HwDeviceExtension);
BOOLEAN  SiS_IsSR13_CR30(SiS_Private *SiS_Pr, PSIS_HW_DEVICE_INFO HwDeviceExtension);

extern   void     SiS_SetReg1(USHORT, USHORT, USHORT);
extern   void     SiS_SetReg3(USHORT, USHORT);
extern   UCHAR    SiS_GetReg1(USHORT, USHORT);
extern   UCHAR    SiS_GetReg2(USHORT);
extern   BOOLEAN  SiS_SearchModeID(SiS_Private *SiS_Pr, UCHAR *ROMAddr, USHORT *ModeNo,USHORT *ModeIdIndex);
extern   BOOLEAN  SiS_GetRatePtr(SiS_Private *SiS_Pr, ULONG, USHORT);
extern   void     SiS_SetReg4(USHORT, ULONG);
extern   ULONG    SiS_GetReg3(USHORT);
extern   void     SiS_SetReg5(USHORT, USHORT);
extern   USHORT   SiS_GetReg4(USHORT);
extern   void     SiS_DisplayOff(SiS_Private *SiS_Pr);
extern   void     SiS_DisplayOn(SiS_Private *SiS_Pr);
extern   UCHAR    SiS_GetModePtr(SiS_Private *SiS_Pr, UCHAR *ROMAddr, USHORT ModeNo,USHORT ModeIdIndex);
extern   BOOLEAN  SiS_GetLCDACRT1Ptr(SiS_Private *SiS_Pr, UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex,
		                     USHORT RefreshRateTableIndex,USHORT *ResInfo,USHORT *DisplayType);
extern   BOOLEAN  SiS_GetLVDSCRT1Ptr(SiS_Private *SiS_Pr, UCHAR *ROMAddr,USHORT ModeNo,USHORT ModeIdIndex,
                                     USHORT RefreshRateTableIndex,USHORT *ResInfo,USHORT *DisplayType);
extern   void     SiS_LoadDAC(SiS_Private *SiS_Pr, PSIS_HW_DEVICE_INFO, UCHAR *ROMAddr,USHORT ModeNo,
                              USHORT ModeIdIndex);
#ifdef SIS315H
extern   UCHAR    SiS_Get310DRAMType(SiS_Private *SiS_Pr, UCHAR *ROMAddr,PSIS_HW_DEVICE_INFO HwDeviceExtension);
#endif

#ifdef LINUX_XF86
/* DDC functions */
USHORT   SiS_InitDDCRegs(SiS_Private *SiS_Pr, SISPtr pSiS, USHORT adaptnum, USHORT DDCdatatype, BOOLEAN checkcr32);
USHORT   SiS_WriteDABDDC(SiS_Private *SiS_Pr);
USHORT   SiS_PrepareReadDDC(SiS_Private *SiS_Pr);
USHORT   SiS_PrepareDDC(SiS_Private *SiS_Pr);
void     SiS_SendACK(SiS_Private *SiS_Pr, USHORT yesno);
USHORT   SiS_DoProbeDDC(SiS_Private *SiS_Pr);
USHORT   SiS_ProbeDDC(SiS_Private *SiS_Pr);
USHORT   SiS_ReadDDC(SiS_Private *SiS_Pr, SISPtr pSiS, USHORT DDCdatatype, unsigned char *buffer);
USHORT   SiS_HandleDDC(SiS_Private *SiS_Pr, SISPtr pSiS, USHORT adaptnum,
                       USHORT DDCdatatype, unsigned char *buffer);
#endif

const UCHAR SiS_HiVisionTable[3][64] = {
  { 
    0x17, 0x1d, 0x03, 0x09, 0x05, 0x06, 0x0c, 0x0c,
    0x94, 0x49, 0x01, 0x0a, 0x06, 0x0d, 0x04, 0x0a,
    0x06, 0x14, 0x0d, 0x04, 0x0a, 0x00, 0x85, 0x1b,
    0x0c, 0x50, 0x00, 0x97, 0x00, 0xd4, 0x4a, 0x17,
    0x7d, 0x05, 0x4b, 0x00, 0x00, 0xe2, 0x00, 0x02,
    0x03, 0x0a, 0x65, 0x9d, 0x08, 0x92, 0x8f, 0x40,
    0x60, 0x80, 0x14, 0x90, 0x8c, 0x60, 0x14, 0x53,
    0x00, 0x40, 0x44, 0x00, 0xdb, 0x02, 0x3b, 0x00
  },
  { 
    0x1d, 0x1d, 0x06, 0x09, 0x0b, 0x0c, 0x0c, 0x0c,
    0x98, 0x0a, 0x01, 0x0d, 0x06, 0x0d, 0x04, 0x0a,
    0x06, 0x14, 0x0d, 0x04, 0x0a, 0x00, 0x85, 0x3f,
    0x0c, 0x50, 0xb2, 0x2e, 0x16, 0xb5, 0xf4, 0x03,
    0x7d, 0x11, 0x7d, 0xea, 0x30, 0x36, 0x18, 0x96,
    0x21, 0x0a, 0x58, 0xee, 0x42, 0x92, 0x0f, 0x40,
    0x60, 0x80, 0x14, 0x90, 0x8c, 0x60, 0x04, 0xf3,
    0x00, 0x40, 0x11, 0x00, 0xfc, 0xff, 0x32, 0x00
  },
  { 
    0x13, 0x1d, 0xe8, 0x09, 0x09, 0xed, 0x0c, 0x0c, 
    0x98, 0x0a, 0x01, 0x0c, 0x06, 0x0d, 0x04, 0x0a, 
    0x06, 0x14, 0x0d, 0x04, 0x0a, 0x00, 0x85, 0x3f, 
    0xed, 0x50, 0x70, 0x9f, 0x16, 0x59, 0x2b, 0x13, 
    0x27, 0x0b, 0x27, 0xfc, 0x30, 0x27, 0x1c, 0xb0, 
    0x4b, 0x4b, 0x6f, 0x2f, 0x63, 0x92, 0x0f, 0x40, 
    0x60, 0x80, 0x14, 0x90, 0x8c, 0x60, 0x14, 0x2a, 
    0x00, 0x40, 0x11, 0x00, 0xfc, 0xff, 0x32, 0x00 
  }
};

const UCHAR SiS_HiTVGroup3_1[] = {
    0x00, 0x14, 0x15, 0x25, 0x55, 0x15, 0x0b, 0x13,
    0xb1, 0x41, 0x62, 0x62, 0xff, 0xf4, 0x45, 0xa6,
    0x25, 0x2f, 0x67, 0xf6, 0xbf, 0xff, 0x8e, 0x20,
    0xac, 0xda, 0x60, 0xfe, 0x6a, 0x9a, 0x06, 0x10,
    0xd1, 0x04, 0x18, 0x0a, 0xff, 0x80, 0x00, 0x80,
    0x3b, 0x77, 0x00, 0xef, 0xe0, 0x10, 0xb0, 0xe0,
    0x10, 0x4f, 0x0f, 0x0f, 0x05, 0x0f, 0x08, 0x6e,
    0x1a, 0x1f, 0x25, 0x2a, 0x4c, 0xaa, 0x01
};

const UCHAR SiS_HiTVGroup3_2[] = {
    0x00, 0x14, 0x15, 0x25, 0x55, 0x15, 0x0b, 0x7a,
    0x54, 0x41, 0xe7, 0xe7, 0xff, 0xf4, 0x45, 0xa6,
    0x25, 0x2f, 0x67, 0xf6, 0xbf, 0xff, 0x8e, 0x20,
    0xac, 0x6a, 0x60, 0x2b, 0x52, 0xcd, 0x61, 0x10,
    0x51, 0x04, 0x18, 0x0a, 0x1f, 0x80, 0x00, 0x80,
    0xff, 0xa4, 0x04, 0x2b, 0x94, 0x21, 0x72, 0x94,
    0x26, 0x05, 0x01, 0x0f, 0xed, 0x0f, 0x0a, 0x64,
    0x18, 0x1d, 0x23, 0x28, 0x4c, 0xaa, 0x01
};

#endif
