#ifndef _INIT_
#define _INIT_

#include "osdef.h"
#include "initdef.h"
#include "vgatypes.h"
#include "vstruct.h"

#include <linux/types.h>
#include <asm/io.h>
#include <linux/sisfb.h>


USHORT SiS_DRAMType[17][5] = { 
	{0x0C, 0x0A, 0x02, 0x40, 0x39},
	{0x0D, 0x0A, 0x01, 0x40, 0x48},
	{0x0C, 0x09, 0x02, 0x20, 0x35},
	{0x0D, 0x09, 0x01, 0x20, 0x44},
	{0x0C, 0x08, 0x02, 0x10, 0x31},
	{0x0D, 0x08, 0x01, 0x10, 0x40},
	{0x0C, 0x0A, 0x01, 0x20, 0x34},
	{0x0C, 0x09, 0x01, 0x08, 0x32},
	{0x0B, 0x08, 0x02, 0x08, 0x21},
	{0x0C, 0x08, 0x01, 0x08, 0x30},
	{0x0A, 0x08, 0x02, 0x04, 0x11},
	{0x0B, 0x0A, 0x01, 0x10, 0x28},
	{0x09, 0x08, 0x02, 0x02, 0x01},
	{0x0B, 0x09, 0x01, 0x08, 0x24},
	{0x0B, 0x08, 0x01, 0x04, 0x20},
	{0x0A, 0x08, 0x01, 0x02, 0x10},
	{0x09, 0x08, 0x01, 0x01, 0x00}
};

USHORT SiS_SDRDRAM_TYPE[13][5] = {
	{2, 12, 9, 64, 0x35},
	{1, 13, 9, 64, 0x44},
	{2, 12, 8, 32, 0x31},
	{2, 11, 9, 32, 0x25},
	{1, 12, 9, 32, 0x34},
	{1, 13, 8, 32, 0x40},
	{2, 11, 8, 16, 0x21},
	{1, 12, 8, 16, 0x30},
	{1, 11, 9, 16, 0x24},
	{1, 11, 8, 8, 0x20},
	{2, 9, 8, 4, 0x01},
	{1, 10, 8, 4, 0x10},
	{1, 9, 8, 2, 0x00}
};

USHORT SiS_DDRDRAM_TYPE[4][5] = {
	{2, 12, 9, 64, 0x35},
	{2, 12, 8, 32, 0x31},
	{2, 11, 8, 16, 0x21},
	{2, 9, 8, 4, 0x01}
};

UCHAR SiS_ChannelAB, SiS_DataBusWidth;

USHORT SiS_MDA_DAC[] = { 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x15, 0x15, 0x15, 0x15, 0x15, 0x15, 0x15, 0x15,
	0x15, 0x15, 0x15, 0x15, 0x15, 0x15, 0x15, 0x15,
	0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x15, 0x15, 0x15, 0x15, 0x15, 0x15, 0x15, 0x15,
	0x15, 0x15, 0x15, 0x15, 0x15, 0x15, 0x15, 0x15,
	0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F
};

USHORT SiS_CGA_DAC[] = { 
	0x00, 0x10, 0x04, 0x14, 0x01, 0x11, 0x09, 0x15,
	0x00, 0x10, 0x04, 0x14, 0x01, 0x11, 0x09, 0x15,
	0x2A, 0x3A, 0x2E, 0x3E, 0x2B, 0x3B, 0x2F, 0x3F,
	0x2A, 0x3A, 0x2E, 0x3E, 0x2B, 0x3B, 0x2F, 0x3F,
	0x00, 0x10, 0x04, 0x14, 0x01, 0x11, 0x09, 0x15,
	0x00, 0x10, 0x04, 0x14, 0x01, 0x11, 0x09, 0x15,
	0x2A, 0x3A, 0x2E, 0x3E, 0x2B, 0x3B, 0x2F, 0x3F,
	0x2A, 0x3A, 0x2E, 0x3E, 0x2B, 0x3B, 0x2F, 0x3F
};

USHORT SiS_EGA_DAC[] = { 
	0x00, 0x10, 0x04, 0x14, 0x01, 0x11, 0x05, 0x15,
	0x20, 0x30, 0x24, 0x34, 0x21, 0x31, 0x25, 0x35,
	0x08, 0x18, 0x0C, 0x1C, 0x09, 0x19, 0x0D, 0x1D,
	0x28, 0x38, 0x2C, 0x3C, 0x29, 0x39, 0x2D, 0x3D,
	0x02, 0x12, 0x06, 0x16, 0x03, 0x13, 0x07, 0x17,
	0x22, 0x32, 0x26, 0x36, 0x23, 0x33, 0x27, 0x37,
	0x0A, 0x1A, 0x0E, 0x1E, 0x0B, 0x1B, 0x0F, 0x1F,
	0x2A, 0x3A, 0x2E, 0x3E, 0x2B, 0x3B, 0x2F, 0x3F
};

USHORT SiS_VGA_DAC[] = { 
	0x00, 0x10, 0x04, 0x14, 0x01, 0x11, 0x09, 0x15,
	0x2A, 0x3A, 0x2E, 0x3E, 0x2B, 0x3B, 0x2F, 0x3F,
	0x00, 0x05, 0x08, 0x0B, 0x0E, 0x11, 0x14, 0x18,
	0x1C, 0x20, 0x24, 0x28, 0x2D, 0x32, 0x38, 0x3F,

	0x00, 0x10, 0x1F, 0x2F, 0x3F, 0x1F, 0x27, 0x2F,
	0x37, 0x3F, 0x2D, 0x31, 0x36, 0x3A, 0x3F, 0x00,
	0x07, 0x0E, 0x15, 0x1C, 0x0E, 0x11, 0x15, 0x18,
	0x1C, 0x14, 0x16, 0x18, 0x1A, 0x1C, 0x00, 0x04,
	0x08, 0x0C, 0x10, 0x08, 0x0A, 0x0C, 0x0E, 0x10,
	0x0B, 0x0C, 0x0D, 0x0F, 0x10
};

USHORT SiS_P3c4, SiS_P3d4, SiS_P3c0, SiS_P3ce, SiS_P3c2;
USHORT SiS_P3ca, SiS_P3c6, SiS_P3c7, SiS_P3c8, SiS_P3c9, SiS_P3da;
USHORT SiS_Part1Port, SiS_Part2Port;
USHORT SiS_Part3Port, SiS_Part4Port, SiS_Part5Port;
USHORT SiS_CRT1Mode;

USHORT flag_clearbuffer;	/*0: no clear frame buffer 1:clear frame buffer  */
int SiS_RAMType;		/*int      ModeIDOffset,StandTable,CRT1Table,ScreenOffset,REFIndex; */
USHORT SiS_ModeType;
USHORT SiS_IF_DEF_LVDS, SiS_IF_DEF_TRUMPION, SiS_IF_DEF_DSTN;	/*add for dstn */
USHORT SiS_IF_DEF_CH7005, SiS_IF_DEF_HiVision;
USHORT SiS_VBInfo, SiS_LCDResInfo, SiS_LCDTypeInfo, SiS_LCDInfo, SiS_VBType;	/*301b */
USHORT SiS_SelectCRT2Rate;

extern USHORT SiS_SetFlag;

void SiS_SetMemoryClock (ULONG ROMAddr);
void SiS_SetDRAMModeRegister (ULONG ROMAddr);
void SiS_SetDRAMSize_310 (PSIS_HW_DEVICE_INFO);
void SiS_SetDRAMSize_300 (PSIS_HW_DEVICE_INFO HwDeviceExtension);
USHORT SiS_ChkBUSWidth_300 (ULONG FBAddress);
UCHAR SiS_Get310DRAMType (ULONG ROMAddr);

void SiS_Delay15us (ULONG);
BOOLEAN SiS_SearchModeID (ULONG ROMAddr, USHORT ModeNo, USHORT * ModeIdIndex);
BOOLEAN SiS_CheckMemorySize (ULONG ROMAddr,
			     PSIS_HW_DEVICE_INFO HwDeviceExtension,
			     USHORT ModeNo, USHORT ModeIdIndex);
UCHAR SiS_GetModePtr (ULONG ROMAddr, USHORT ModeNo, USHORT ModeIdIndex);
void SiS_SetSeqRegs (ULONG, USHORT StandTableIndex);
void SiS_SetMiscRegs (ULONG, USHORT StandTableIndex);
void SiS_SetCRTCRegs (ULONG, PSIS_HW_DEVICE_INFO HwDeviceExtension,
		      USHORT StandTableIndex);
void SiS_SetATTRegs (ULONG, USHORT StandTableIndex);
void SiS_SetGRCRegs (ULONG, USHORT StandTableIndex);
void SiS_ClearExt1Regs (void);
void SiS_SetSync (ULONG ROMAddr, USHORT RefreshRateTableIndex);
void SiS_SetCRT1CRTC (ULONG ROMAddr, USHORT ModeNo, USHORT ModeIdIndex,
		      USHORT RefreshRateTableIndex);
void SiS_SetCRT1VCLK (ULONG ROMAddr, USHORT ModeNo, USHORT ModeIdIndex,
		      PSIS_HW_DEVICE_INFO, USHORT RefreshRateTableIndex);
void SiS_SetVCLKState (ULONG ROMAddr, PSIS_HW_DEVICE_INFO, USHORT ModeNo,
		       USHORT RefreshRateTableIndex);
void SiS_LoadDAC (ULONG ROMAddr, USHORT ModeNo, USHORT ModeIdIndex);
void SiS_DisplayOn (void);
void SiS_SetCRT1ModeRegs (ULONG ROMAddr, PSIS_HW_DEVICE_INFO, USHORT ModeNo,
			  USHORT ModeIdIndex, USHORT RefreshRateTableIndex);
void SiS_WriteDAC (USHORT, USHORT, USHORT, USHORT);
void SiS_GetVBType (USHORT BaseAddr);	/*301b */
USHORT SiS_ChkBUSWidth (ULONG);
USHORT SiS_GetModeIDLength (ULONG, USHORT);
USHORT SiS_GetRefindexLength (ULONG, USHORT);
void SiS_SetInterlace (ULONG ROMAddr, USHORT ModeNo,
		       USHORT RefreshRateTableIndex);
USHORT SiS_CalcDelay2 (ULONG, UCHAR);
USHORT SiS_CalcDelay (ULONG, USHORT);
void SiS_Set_LVDS_TRUMPION (PSIS_HW_DEVICE_INFO HwDeviceExtension);
void SiS_SetCRT1Offset (ULONG, USHORT, USHORT, USHORT, PSIS_HW_DEVICE_INFO);
void SiS_SetCRT1FIFO (ULONG, USHORT, PSIS_HW_DEVICE_INFO);
void SiS_SetCRT1FIFO2 (ULONG, USHORT ModeNo, PSIS_HW_DEVICE_INFO,
		       USHORT RefreshRateTableIndex);
void SiS_CRT2AutoThreshold (USHORT BaseAddr);
void SiS_ClearBuffer (PSIS_HW_DEVICE_INFO, USHORT ModeNo);
void SiS_SetCRT1Group (ULONG ROMAddr, PSIS_HW_DEVICE_INFO HwDeviceExtension,
		       USHORT ModeNo, USHORT ModeIdIndex);
void SiS_DetectMonitor (PSIS_HW_DEVICE_INFO HwDeviceExtension, USHORT BaseAddr);
void SiS_GetSenseStatus (PSIS_HW_DEVICE_INFO HwDeviceExtension, ULONG ROMAddr);
USHORT SiS_TestMonitorType (UCHAR R_DAC, UCHAR G_DAC, UCHAR B_DAC);
USHORT SiS_SenseCHTV (VOID);
BOOLEAN SiS_Sense (USHORT Part4Port, USHORT tempbx, USHORT tempcx);
BOOLEAN SiS_GetPanelID (VOID);
BOOLEAN SiS_GetLCDDDCInfo (PSIS_HW_DEVICE_INFO);
USHORT SiS_SenseLCD (PSIS_HW_DEVICE_INFO);

extern BOOLEAN SiS_SetCRT2Group301 (USHORT BaseAddr, ULONG ROMAddr,
				    USHORT ModeNo,
				    PSIS_HW_DEVICE_INFO HwDeviceExtension);
extern void SiS_PresetScratchregister (USHORT SiS_P3d4,
				       PSIS_HW_DEVICE_INFO HwDeviceExtension);
extern void SiS_UnLockCRT2 (PSIS_HW_DEVICE_INFO HwDeviceExtension,
			    USHORT BaseAddr);
extern void SiS_LockCRT2 (PSIS_HW_DEVICE_INFO HwDeviceExtension,
			  USHORT BaseAddr);
extern BOOLEAN SiS_BridgeIsOn (USHORT BaseAddr);
extern BOOLEAN SiS_BridgeIsEnable (USHORT BaseAddr, PSIS_HW_DEVICE_INFO);
extern void SiS_SetTVSystem301 (VOID);
extern BOOLEAN SiS_GetLCDDDCInfo301 (PSIS_HW_DEVICE_INFO HwDeviceExtension);
extern BOOLEAN SiS_GetSenseStatus301 (PSIS_HW_DEVICE_INFO HwDeviceExtension,
				      USHORT BaseAddr, ULONG ROMAddr);
extern USHORT SiS_GetVCLKLen (ULONG ROMAddr,
			      PSIS_HW_DEVICE_INFO HwDeviceExtension);
extern BOOLEAN SiS_SetCRT2Group302 (USHORT BaseAddr, ULONG ROMAddr,
				    USHORT ModeNo,
				    PSIS_HW_DEVICE_INFO HwDeviceExtension);
extern void SiS_GetVBInfo301 (USHORT BaseAddr, ULONG ROMAddr, USHORT ModeNo,
			      USHORT ModeIdIndex,
			      PSIS_HW_DEVICE_INFO HwDeviceExtension);
extern BOOLEAN SiS_GetLCDResInfo301 (ULONG ROMAddr, USHORT P3d4, USHORT ModeNo,
				     USHORT ModeIdIndex);
extern USHORT SiS_VBInfo, LCDResInfo, LCDTypeInfo, LCDInfo;
extern USHORT SiS_GetRatePtrCRT2 (ULONG ROMAddr, USHORT ModeNo,
				  USHORT ModeIdIndex);
extern void SiS_LongWait (VOID);
extern void SiS_SetRegANDOR (USHORT Port, USHORT Index, USHORT DataAND,
			     USHORT DataOR);
extern USHORT SiS_GetResInfo (ULONG ROMAddr, USHORT ModeNo, USHORT ModeIdIndex);
extern void SiS_SetCH7005 (USHORT tempax);
extern USHORT SiS_GetCH7005 (USHORT tempax);
extern BOOLEAN SiS_GetLVDSCRT1Ptr (ULONG ROMAddr, USHORT ModeNo,
				   USHORT ModeIdIndex,
				   USHORT RefreshRateTableIndex,
				   USHORT * ResInfo, USHORT * DisplayType);
extern BOOLEAN SiS_GetLCDACRT1Ptr (ULONG ROMAddr, USHORT ModeNo,
				   USHORT ModeIdIndex,
				   USHORT RefreshRateTableIndex,
				   USHORT * ResInfo, USHORT * DisplayType);
extern USHORT SiS_GetVCLK2Ptr (ULONG ROMAddr, USHORT ModeNo, USHORT ModeIdIndex,
			       USHORT RefreshRateTableIndex,
			       PSIS_HW_DEVICE_INFO HwDeviceExtension);
extern BOOLEAN SiS_Is301B (USHORT BaseAddr);	/*301b */

#endif
