/* $XFree86: xc/programs/Xserver/hw/xfree86/drivers/sis/init301.h,v 1.4 2000/12/02 01:16:17 dawes Exp $ */
#ifndef  _INIT301_
#define  _INIT301_

#include "osdef.h"
#include "initdef.h"
#include "vgatypes.h"
#include "vstruct.h"

#include <asm/io.h>
#include <linux/types.h>
#include <linux/sisfb.h>

USHORT SiS_SetFlag;
USHORT SiS_RVBHCFACT, SiS_RVBHCMAX, SiS_RVBHRS;
USHORT SiS_VGAVT, SiS_VGAHT;
USHORT SiS_VT, SiS_HT;
USHORT SiS_VGAVDE, SiS_VGAHDE;
USHORT SiS_VDE, SiS_HDE;
USHORT SiS_NewFlickerMode, SiS_RY1COE, SiS_RY2COE, SiS_RY3COE, SiS_RY4COE;
USHORT SiS_LCDHDES, SiS_LCDVDES;
USHORT SiS_DDC_Port;
USHORT SiS_DDC_Index;
USHORT SiS_DDC_DataShift;
USHORT SiS_DDC_DeviceAddr;
USHORT SiS_DDC_Flag;
USHORT SiS_DDC_ReadAddr;
USHORT SiS_DDC_Buffer;

extern USHORT SiS_CRT1Mode;
extern USHORT SiS_P3c4, SiS_P3d4;
/*extern   USHORT      SiS_P3c0,SiS_P3ce,SiS_P3c2;*/
extern USHORT SiS_P3ca;
/*extern   USHORT      SiS_P3c6,SiS_P3c7,SiS_P3c8;*/
extern USHORT SiS_P3c9;
extern USHORT SiS_P3da;
extern USHORT SiS_Part1Port, SiS_Part2Port;
extern USHORT SiS_Part3Port, SiS_Part4Port, SiS_Part5Port;
extern USHORT SiS_MDA_DAC[];
extern USHORT SiS_CGA_DAC[];
extern USHORT SiS_EGA_DAC[];
extern USHORT SiS_VGA_DAC[];
extern USHORT SiS_ModeType;
extern USHORT SiS_SelectCRT2Rate;
extern USHORT SiS_IF_DEF_LVDS;
extern USHORT SiS_IF_DEF_TRUMPION;
extern USHORT SiS_IF_DEF_CH7005;
extern USHORT SiS_IF_DEF_HiVision;
extern USHORT SiS_IF_DEF_DSTN;	/*add for dstn */
extern USHORT SiS_VBInfo;
extern USHORT SiS_VBType;	/*301b */
extern USHORT SiS_LCDResInfo;
extern USHORT SiS_LCDTypeInfo;
extern USHORT SiS_LCDInfo;
extern BOOLEAN SiS_SearchVBModeID (ULONG, USHORT);
extern BOOLEAN SiS_Is301B (USHORT BaseAddr);	/*301b */
extern BOOLEAN SiS_IsDisableCRT2 (USHORT BaseAddr);
extern BOOLEAN SiS_IsVAMode (USHORT BaseAddr);
extern BOOLEAN SiS_IsDualEdge (USHORT BaseAddr);
/*end 301b*/

void SiS_SetDefCRT2ExtRegs (USHORT BaseAddr);
USHORT SiS_GetRatePtrCRT2 (ULONG ROMAddr, USHORT ModeNo, USHORT ModeIdIndex);
BOOLEAN SiS_AjustCRT2Rate (ULONG ROMAddr, USHORT ModeNo, USHORT MODEIdIndex,
			   USHORT RefreshRateTableIndex, USHORT * i);
void SiS_SaveCRT2Info (USHORT ModeNo);
void SiS_GetCRT2Data (ULONG ROMAddr, USHORT ModeNo, USHORT ModeIdIndex,
		      USHORT RefreshRateTableIndex);
void SiS_GetCRT2DataLVDS (ULONG ROMAddr, USHORT ModeNo, USHORT ModeIdIndex,
			  USHORT RefreshRateTableIndex);
void SiS_GetCRT2PtrA (ULONG ROMAddr, USHORT ModeNo, USHORT ModeIdIndex,
		      USHORT RefreshRateTableIndex, USHORT * CRT2Index, USHORT * ResIndex);	/*301b */
void SiS_GetCRT2Data301 (ULONG ROMAddr, USHORT ModeNo, USHORT ModeIdIndex,
			 USHORT RefreshRateTableIndex);
USHORT SiS_GetResInfo (ULONG ROMAddr, USHORT ModeNo, USHORT ModeIdIndex);
void SiS_GetCRT2ResInfo (ULONG ROMAddr, USHORT ModeNo, USHORT ModeIdIndex);
void SiS_GetRAMDAC2DATA (ULONG ROMAddr, USHORT ModeNo, USHORT ModeIdIndex,
			 USHORT RefreshRateTableIndex);
void SiS_GetCRT2Ptr (ULONG ROMAddr, USHORT ModeNo, USHORT ModeIdIndex,
		     USHORT RefreshRateTableIndex, USHORT * CRT2Index,
		     USHORT * ResIndex);
void SiS_SetCRT2ModeRegs (USHORT BaseAddr, USHORT ModeNo, PSIS_HW_DEVICE_INFO);

void SiS_GetLVDSDesData (ULONG ROMAddr, USHORT ModeNo, USHORT ModeIdIndex,
			 USHORT RefreshRateTableIndex);
void SiS_SetCRT2Offset (USHORT Part1Port, ULONG ROMAddr, USHORT ModeNo,
			USHORT ModeIdIndex, USHORT RefreshRateTableIndex,
			PSIS_HW_DEVICE_INFO HwDeviceExtension);
USHORT SiS_GetOffset (ULONG ROMAddr, USHORT ModeNo, USHORT ModeIdIndex,
		      USHORT RefreshRateTableIndex,
		      PSIS_HW_DEVICE_INFO HwDeviceExtension);
USHORT SiS_GetColorDepth (ULONG ROMAddr, USHORT ModeNo, USHORT ModeIdIndex);
USHORT SiS_GetVCLK (ULONG ROMAddr, USHORT ModeNo);
USHORT SiS_GetVCLKPtr (ULONG ROMAddr, USHORT ModeNo);
USHORT SiS_GetColorTh (ULONG ROMAddr);
USHORT SiS_GetMCLK (ULONG ROMAddr);
USHORT SiS_GetMCLKPtr (ULONG ROMAddr);
USHORT SiS_GetDRAMType (ULONG ROMAddr);
USHORT SiS_CalcDelayVB (void);
extern USHORT SiS_GetVCLK2Ptr (ULONG ROMAddr, USHORT ModeNo, USHORT ModeIdIndex,
			       USHORT RefreshRateTableIndex,
			       PSIS_HW_DEVICE_INFO HwDeviceExtension);
void SiS_SetCRT2Sync (USHORT BaseAddr, ULONG ROMAddr, USHORT ModeNo,
		      USHORT RefreshRateTableIndex);
void SiS_SetRegANDOR (USHORT Port, USHORT Index, USHORT DataAND, USHORT DataOR);
void SiS_SetRegOR (USHORT Port, USHORT Index, USHORT DataOR);
void SiS_SetRegAND (USHORT Port, USHORT Index, USHORT DataAND);
USHORT SiS_GetVGAHT2 (void);
void SiS_SetGroup2 (USHORT BaseAddr, ULONG ROMAddr, USHORT ModeNo,
		    USHORT ModeIdIndex, USHORT RefreshRateTableIndex,
		    PSIS_HW_DEVICE_INFO HwDeviceExtension);
void SiS_SetGroup3 (USHORT BaseAddr, ULONG ROMAddr, USHORT ModeNo,
		    USHORT ModeIdIndex, PSIS_HW_DEVICE_INFO HwDeviceExtension);
void SiS_SetGroup4 (USHORT BaseAddr, ULONG ROMAddr, USHORT ModeNo,
		    USHORT ModeIdIndex, USHORT RefreshRateTableIndex,
		    PSIS_HW_DEVICE_INFO HwDeviceExtension);
void SiS_SetGroup5 (USHORT BaseAddr, ULONG ROMAddr, USHORT ModeNo,
		    USHORT ModeIdIndex);
void SiS_SetCRT2VCLK (USHORT BaseAddr, ULONG ROMAddr, USHORT ModeNo,
		      USHORT ModeIdIndex, USHORT RefreshRateTableIndex,
		      PSIS_HW_DEVICE_INFO HwDeviceExtension);
void SiS_EnableCRT2 (void);
void SiS_LoadDAC2 (ULONG ROMAddr, USHORT Part5Port, USHORT ModeNo,
		   USHORT ModeIdIndex);
void SiS_WriteDAC2 (USHORT Pdata, USHORT dl, USHORT ah, USHORT al, USHORT dh);
void SiS_GetVBInfo301 (USHORT BaseAddr, ULONG ROMAddr, USHORT ModeNo,
		       USHORT ModeIdIndex,
		       PSIS_HW_DEVICE_INFO HwDeviceExtension);
BOOLEAN SiS_GetLCDResInfo (ULONG ROMAddr, USHORT P3d4, USHORT ModeNo,
			   USHORT ModeIdIndex);
BOOLEAN SiS_BridgeIsOn (USHORT BaseAddr);
BOOLEAN SiS_BridgeIsEnable (USHORT BaseAddr, PSIS_HW_DEVICE_INFO);
BOOLEAN SiS_BridgeInSlave (void);
/*void     SiS_PresetScratchregister(USHORT P3d4);*/
void SiS_PresetScratchregister (USHORT SiS_P3d4,
				PSIS_HW_DEVICE_INFO HwDeviceExtension);
void SiS_SetTVSystem (VOID);
void SiS_LongWait (VOID);
USHORT SiS_GetQueueConfig (VOID);
void SiS_VBLongWait (VOID);
USHORT SiS_GetVCLKLen (ULONG ROMAddr);
BOOLEAN SiS_WaitVBRetrace (USHORT BaseAddr);
void SiS_SetCRT2ECLK (ULONG ROMAddr, USHORT ModeNo, USHORT ModeIdIndex,
		      USHORT RefreshRateTableIndex,
		      PSIS_HW_DEVICE_INFO HwDeviceExtension);
void SiS_GetLVDSDesPtr (ULONG ROMAddr, USHORT ModeNo, USHORT ModeIdIndex,
			USHORT RefreshRateTableIndex, USHORT * PanelIndex,
			USHORT * ResIndex);
void SiS_GetLVDSDesPtrA (ULONG ROMAddr, USHORT ModeNo, USHORT ModeIdIndex,
			 USHORT RefreshRateTableIndex, USHORT * PanelIndex,
			 USHORT * ResIndex);	/*301b */
void SiS_SetTPData (VOID);
void SiS_ModCRT1CRTC (ULONG ROMAddr, USHORT ModeNo, USHORT ModeIdIndex,
		      USHORT RefreshRateTableIndex);
extern BOOLEAN SiS_GetLVDSCRT1Ptr (ULONG ROMAddr, USHORT ModeNo,
				   USHORT ModeIdIndex,
				   USHORT RefreshRateTableIndex,
				   USHORT * ResInfo, USHORT * DisplayType);
void SiS_SetCHTVReg (ULONG ROMAddr, USHORT ModeNo, USHORT ModeIdIndex,
		     USHORT RefreshRateTableIndex);
void SiS_SetCHTVRegANDOR (USHORT tempax, USHORT tempbh);
void SiS_GetCHTVRegPtr (ULONG ROMAddr, USHORT ModeNo, USHORT ModeIdIndex,
			USHORT RefreshRateTableIndex);
void SiS_SetCH7005 (USHORT tempax);
USHORT SiS_GetCH7005 (USHORT tempax);
void SiS_SetSwitchDDC2 (void);
void SiS_SetStart (void);
void SiS_SetStop (void);
void SiS_DDC2Delay (void);
void SiS_SetSCLKLow (void);
void SiS_SetSCLKHigh (void);
USHORT SiS_ReadDDC2Data (USHORT tempax);
USHORT SiS_WriteDDC2Data (USHORT tempax);
USHORT SiS_CheckACK (void);
void SiS_OEM310Setting (PSIS_HW_DEVICE_INFO HwDeviceExtension, USHORT BaseAddr,
			ULONG ROMAddr, USHORT ModeNo, USHORT ModeIdIndex);
void SiS_OEM300Setting (PSIS_HW_DEVICE_INFO HwDeviceExtension, USHORT BaseAddr,
			ULONG ROMAddr, USHORT ModeNo);
USHORT GetRevisionID (PSIS_HW_DEVICE_INFO HwDeviceExtension);
extern void SiS_SetReg1 (USHORT, USHORT, USHORT);
extern void SiS_SetReg3 (USHORT, USHORT);
extern UCHAR SiS_GetReg1 (USHORT, USHORT);
extern UCHAR SiS_GetReg2 (USHORT);
extern BOOLEAN SiS_SearchModeID (ULONG ROMAddr, USHORT ModeNo,
				 USHORT * ModeIdIndex);
extern BOOLEAN SiS_GetRatePtr (ULONG, USHORT);
extern void SiS_SetReg4 (USHORT, ULONG);
extern ULONG SiS_GetReg3 (USHORT);
extern void SiS_DisplayOff (void);
extern void SiS_CRT2AutoThreshold (USHORT BaseAddr);
extern void SiS_DisplayOn (void);
extern UCHAR SiS_GetModePtr (ULONG ROMAddr, USHORT ModeNo, USHORT ModeIdIndex);
extern UCHAR SiS_Get310DRAMType (ULONG ROMAddr);

BOOLEAN SiS_SetCRT2Group301 (USHORT BaseAddr, ULONG ROMAddr, USHORT ModeNo,
			     PSIS_HW_DEVICE_INFO HwDeviceExtension);
void SiS_SetGroup1 (USHORT BaseAddr, ULONG ROMAddr, USHORT ModeNo,
		    USHORT ModeIdIndex, PSIS_HW_DEVICE_INFO HwDeviceExtension,
		    USHORT RefreshRateTableIndex);
void SiS_SetGroup1_LVDS (USHORT BaseAddr, ULONG ROMAddr, USHORT ModeNo,
			 USHORT ModeIdIndex,
			 PSIS_HW_DEVICE_INFO HwDeviceExtension,
			 USHORT RefreshRateTableIndex);
void SiS_SetGroup1_LCDA (USHORT BaseAddr, ULONG ROMAddr, USHORT ModeNo,
			 USHORT ModeIdIndex,
			 PSIS_HW_DEVICE_INFO HwDeviceExtension, USHORT RefreshRateTableIndex);	/*301b */
void SiS_SetGroup1_301 (USHORT BaseAddr, ULONG ROMAddr, USHORT ModeNo,
			USHORT ModeIdIndex,
			PSIS_HW_DEVICE_INFO HwDeviceExtension,
			USHORT RefreshRateTableIndex);
void SiS_SetCRT2FIFO (USHORT Part1Port, ULONG ROMAddr, USHORT ModeNo,
		      PSIS_HW_DEVICE_INFO HwDeviceExtension);
void SiS_SetCRT2FIFO2 (USHORT Part1Port, ULONG ROMAddr, USHORT ModeNo,
		       PSIS_HW_DEVICE_INFO HwDeviceExtension);
BOOLEAN SiS_GetLCDDDCInfo (PSIS_HW_DEVICE_INFO HwDeviceExtension);
void SiS_UnLockCRT2 (PSIS_HW_DEVICE_INFO, USHORT BaseAddr);
void SiS_LockCRT2 (PSIS_HW_DEVICE_INFO, USHORT BaseAddr);
void SiS_DisableBridge (PSIS_HW_DEVICE_INFO, USHORT BaseAddr);
void SiS_EnableBridge (PSIS_HW_DEVICE_INFO, USHORT BaseAddr);
void SiS_SetPanelDelay (USHORT DelayTime);
void SiS_LCD_Wait_Time (UCHAR DelayTime);

#endif
