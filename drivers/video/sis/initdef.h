/* $XFree86: xc/programs/Xserver/hw/xfree86/drivers/sis/initdef.h,v 1.4 2000/12/02 01:16:17 dawes Exp $ */
#ifndef _INITDEF_
#define _INITDEF_

#define SiS300                  0x0300
#define SiS540                  0x5300
#define SiS630                  0x6300
#define SiS730                  0x6300
#define VB_SIS301		0x0001	/*301b */
#define VB_SIS301B		0x0002
#define VB_SIS302B		0x0004
#define  VB_NoLCD		0x8000

/*end 301b*/
#define CRT1Len                 17
#define LVDSCRT1Len             15
#define CHTVRegDataLen          5

#define ModeInfoFlag            0x07
#define IsTextMode              0x07
#define ModeText                0x00
#define ModeCGA                 0x01
#define ModeEGA                 0x02
#define ModeVGA                 0x03
#define Mode15Bpp               0x04
#define Mode16Bpp               0x05
#define Mode24Bpp               0x06
#define Mode32Bpp               0x07

#define DACInfoFlag             0x18
#define MemoryInfoFlag          0x1E0
#define MemorySizeShift         0x05

#define Charx8Dot               0x0200
#define LineCompareOff          0x0400
#define CRT2Mode                0x0800
#define HalfDCLK                0x1000
#define NoSupportSimuTV         0x2000
#define DoubleScanMode          0x8000

#define SupportAllCRT2          0x0078
#define SupportTV               0x0008
#define SupportHiVisionTV       0x0010
#define SupportLCD              0x0020
#define SupportRAMDAC2          0x0040
#define NoSupportTV             0x0070
#define NoSupportHiVisionTV     0x0060
#define NoSupportLCD            0x0058
#define SupportCHTV 		0x0800
#define SupportTV1024           0x0800	/*301b */
#define InterlaceMode           0x0080
#define SyncPP                  0x0000
#define SyncPN                  0x4000
#define SyncNP                  0x8000
#define SyncNN                  0xc000
#define ECLKindex0              0x0000
#define ECLKindex1              0x0100
#define ECLKindex2              0x0200
#define ECLKindex3              0x0300
#define ECLKindex4              0x0400

#define SetSimuScanMode         0x0001
#define SwitchToCRT2            0x0002
#define SetCRT2ToTV             0x009C
#define SetCRT2ToAVIDEO         0x0004
#define SetCRT2ToSVIDEO         0x0008
#define SetCRT2ToSCART          0x0010
#define SetCRT2ToLCD            0x0020
#define SetCRT2ToRAMDAC         0x0040
#define SetCRT2ToHiVisionTV     0x0080
#define SetNTSCTV               0x0000
#define SetPALTV                0x0100
#define SetInSlaveMode          0x0200
#define SetNotSimuMode          0x0400
#define SetNotSimuTVMode        0x0400
#define SetDispDevSwitch        0x0800
#define LoadDACFlag             0x1000
#define DisableCRT2Display      0x2000
#define DriverMode              0x4000
#define HotKeySwitch            0x8000
#define SetCHTVOverScan  	0x8000
#define SetCRT2ToLCDA		0x8000	/*301b */
#define PanelRGB18Bit           0x0100
#define PanelRGB24Bit           0x0000

#define TVOverScan              0x10
#define TVOverScanShift         4
#define ClearBufferFlag         0x20
#define EnableDualEdge 		0x01	/*301b */
#define SetToLCDA		0x02

#define SetSCARTOutput          0x01
#define BoardTVType             0x02
#define  EnablePALMN		0x40
#define ProgrammingCRT2         0x01
#define TVSimuMode              0x02
#define RPLLDIV2XO              0x04
#define LCDVESATiming           0x08
#define EnableLVDSDDA           0x10
#define SetDispDevSwitchFlag    0x20
#define CheckWinDos             0x40
#define SetJDOSMode             0x80

#define Panel800x600            0x01
#define Panel1024x768           0x02
#define Panel1280x1024          0x03
#define Panel1280x960           0x04
#define Panel640x480            0x05
#define Panel1600x1200          0x06	/*301b */
#define LCDRGB18Bit             0x01
#define ExtChipType             0x0e
#define ExtChip301              0x02
#define ExtChipLVDS             0x04
#define ExtChipTrumpion         0x06
#define ExtChipCH7005           0x08
#define ExtChipMitacTV          0x0a
#define LCDNonExpanding         0x10
#define LCDNonExpandingShift    4
#define LCDSync                 0x20
#define LCDSyncBit              0xe0
#define LCDSyncShift            6

#define DDC2DelayTime           300

#define CRT2DisplayFlag         0x2000
#define LCDDataLen              8
#define HiTVDataLen             12
#define TVDataLen               16
#define SetPALTV                0x0100
#define HalfDCLK                0x1000
#define NTSCHT                  1716
#define NTSCVT                  525
#define PALHT                   1728
#define PALVT                   625
#define StHiTVHT                892
#define StHiTVVT                1126
#define StHiTextTVHT            1000
#define StHiTextTVVT            1126
#define ExtHiTVHT               2100
#define ExtHiTVVT               1125

#define VCLKStartFreq           25
#define SoftDramType            0x80
#define VCLK40                  0x04
#define VCLK65                  0x09
#define VCLK108_2               0x14
#define LCDRGB18Bit             0x01
#define LoadDACFlag             0x1000
#define AfterLockCRT2           0x4000
#define SetCRT2ToAVIDEO         0x0004
#define SetCRT2ToSCART          0x0010
#define Ext2StructSize          5

#define TVVCLKDIV2              0x021
#define TVVCLK                  0x022

#define HiTVVCLKDIV2            0x023
#define HiTVVCLK                0x024
#define HiTVSimuVCLK            0x025
#define HiTVTextVCLK            0x026
#define SwitchToCRT2            0x0002
#define LCDVESATiming           0x08
#define SetSCARTOutput          0x01
#define AVIDEOSense             0x01
#define SVIDEOSense             0x02
#define SCARTSense              0x04
#define LCDSense                0x08
#define Monitor1Sense           0x20
#define Monitor2Sense           0x10
#define HiTVSense               0x40
#define BoardTVType             0x02
#define HotPlugFunction         0x08
#define StStructSize            0x06

#define SIS_CRT2_PORT_04        0x04 - 0x030
#define SIS_CRT2_PORT_10        0x10 - 0x30
#define SIS_CRT2_PORT_12        0x12 - 0x30
#define SIS_CRT2_PORT_14        0x14 - 0x30

#define LCDNonExpanding         0x10
#define ADR_CRT2PtrData         0x20E
#define offset_Zurac            0x210
#define ADR_LVDSDesPtrData      0x212
#define ADR_LVDSCRT1DataPtr     0x214
#define ADR_CHTVVCLKPtr         0x216
#define ADR_CHTVRegDataPtr      0x218

#define LVDSDataLen             6
#define EnableLVDSDDA           0x10
#define LVDSDesDataLen          3
#define ActiveNonExpanding      0x40
#define ActiveNonExpandingShift 6
#define ActivePAL               0x20
#define ActivePALShift          5
#define ModeSwitchStatus        0x0F
#define SoftTVType              0x40
#define SoftSettingAddr         0x52
#define ModeSettingAddr         0x53

#define SelectCRT1Rate          0x4

#define _PanelType00            0x00
#define _PanelType01            0x08
#define _PanelType02            0x10
#define _PanelType03            0x18
#define _PanelType04            0x20
#define _PanelType05            0x28
#define _PanelType06            0x30
#define _PanelType07            0x38
#define _PanelType08            0x40
#define _PanelType09            0x48
#define _PanelType0A            0x50
#define _PanelType0B            0x58
#define _PanelType0C            0x60
#define _PanelType0D            0x68
#define _PanelType0E            0x70
#define _PanelType0F            0x78

#define PRIMARY_VGA		0	/* 1: SiS is primary vga 0:SiS is secondary vga */
#define BIOSIDCodeAddr          0x235
#define OEMUtilIDCodeAddr       0x237
#define VBModeIDTableAddr       0x239
#define OEMTVPtrAddr            0x241
#define PhaseTableAddr          0x243
#define NTSCFilterTableAddr     0x245
#define PALFilterTableAddr      0x247
#define OEMLCDPtr_1Addr         0x249
#define OEMLCDPtr_2Addr         0x24B
#define LCDHPosTable_1Addr      0x24D
#define LCDHPosTable_2Addr      0x24F
#define LCDVPosTable_1Addr      0x251
#define LCDVPosTable_2Addr      0x253
#define OEMLCDPIDTableAddr      0x255

#define VBModeStructSize        5
#define PhaseTableSize          4
#define FilterTableSize         4
#define LCDHPosTableSize        7
#define LCDVPosTableSize        5
#define OEMLVDSPIDTableSize     4
#define LVDSHPosTableSize       4
#define LVDSVPosTableSize       6

#define VB_ModeID               0
#define VB_TVTableIndex         1
#define VB_LCDTableIndex        2
#define VB_LCDHIndex            3
#define VB_LCDVIndex            4

#define OEMLCDEnable            0x0001
#define OEMLCDDelayEnable       0x0002
#define OEMLCDPOSEnable         0x0004
#define OEMTVEnable             0x0100
#define OEMTVDelayEnable        0x0200
#define OEMTVFlickerEnable      0x0400
#define OEMTVPhaseEnable        0x0800
#define OEMTVFilterEnable       0x1000

#define OEMLCDPanelIDSupport    0x0080

/* =============================================================
   for 310
============================================================== */
#define SoftDRAMType      	0x80
#define SoftSetting_OFFSET	0x52
#define SR07_OFFSET		0x7C
#define SR15_OFFSET		0x7D
#define SR16_OFFSET		0x81
#define SR17_OFFSET		0x85
#define SR19_OFFSET		0x8D
#define SR1F_OFFSET		0x99
#define SR21_OFFSET		0x9A
#define SR22_OFFSET		0x9B
#define SR23_OFFSET		0x9C
#define SR24_OFFSET		0x9D
#define SR25_OFFSET		0x9E
#define SR31_OFFSET		0x9F
#define SR32_OFFSET		0xA0
#define SR33_OFFSET		0xA1

#define CR40_OFFSET		0xA2
#define SR25_1_OFFSET		0xF6
#define CR49_OFFSET		0xF7

#define VB310Data_1_2_Offset	0xB6
#define VB310Data_4_D_Offset	0xB7
#define VB310Data_4_E_Offset	0xB8
#define VB310Data_4_10_Offset	0xBB

#define RGBSenseDataOffset	0xBD
#define YCSenseDataOffset	0xBF
#define VideoSenseDataOffset	0xC1
#define OutputSelectOffset	0xF3

#define ECLK_MCLK_DISTANCE	0x14
#define VBIOSTablePointerStart	0x100
#define StandTablePtrOffset	VBIOSTablePointerStart+0x02
#define EModeIDTablePtrOffset	VBIOSTablePointerStart+0x04
#define CRT1TablePtrOffset	VBIOSTablePointerStart+0x06
#define ScreenOffsetPtrOffset	VBIOSTablePointerStart+0x08
#define VCLKDataPtrOffset	VBIOSTablePointerStart+0x0A
#define MCLKDataPtrOffset	VBIOSTablePointerStart+0x0E
#define CRT2PtrDataPtrOffset	VBIOSTablePointerStart+0x10
#define TVAntiFlickPtrOffset	VBIOSTablePointerStart+0x12
#define TVDelayPtr1Offset	VBIOSTablePointerStart+0x14
#define TVPhaseIncrPtr1Offset	VBIOSTablePointerStart+0x16
#define TVYFilterPtr1Offset	VBIOSTablePointerStart+0x18
#define LCDDelayPtr1Offset	VBIOSTablePointerStart+0x20
#define TVEdgePtr1Offset	VBIOSTablePointerStart+0x24
#define CRT2Delay1Offset	VBIOSTablePointerStart+0x28

#endif
