#include "sis.h"

#define PRIMARY_VGA	  1	//1: SiS is primary vga 0:SiS is secondary vga 
#define ModeInfoFlag      0x07
#define MemoryInfoFlag    0x1E0
#define MemorySizeShift   0x05
#define ModeText          0x00
#define ModeCGA           0x01
#define ModeEGA           0x02
#define ModeVGA           0x03
#define Mode15Bpp         0x04
#define Mode16Bpp         0x05
#define Mode24Bpp         0x06
#define Mode32Bpp         0x07
#define CRT1Len           17
#define DoubleScanMode    0x8000
#define ADR_CRT2PtrData   0x20E //address of CRT2PtrData in ROM image 
#define offset_Zurac      0x210
#define ADR_LVDSDesPtrData      0x212
#define ADR_LVDSCRT1DataPtr     0x214

#define SoftDRAMType      0x80  //5/19/2000,Mars,for soft setting dram type
#define SoftSettingAddr   0x52 
#define ModeSettingAddr   0x53

#define InterlaceMode     0x80
#define HalfDCLK          0x1000
#define DACInfoFlag       0x18
#define LineCompareOff    0x400
#define ActivePAL	  0x20
#define ActivePALShift	  5

                
#define SelectCRT2Rate          0x4
#define ProgrammingCRT2         0x1
#define CRT2DisplayFlag         0x2000
#define SetCRT2ToRAMDAC         0x0040
#define Charx8Dot               0x0200
#define LCDDataLen              8
#define SetCRT2ToLCD            0x0020
#define SetCRT2ToHiVisionTV     0x0080
#define HiTVDataLen             12
#define TVDataLen               16
#define SetPALTV                0x0100
#define SetInSlaveMode          0x0200
#define SetCRT2ToTV             0x009C
#define SetNotSimuTVMode        0x0400
#define SetSimuScanMode         0x0001
#define DriverMode              0x4000
#define CRT2Mode                0x0800
//#define ReIndexEnhLCD           4
#define HalfDCLK                0x1000
//#define HiVisionTVHT            2100
//#define HiVisionTVVT            2100
#define NTSCHT                  1716
#define NTSCVT                  525
#define PALHT                   1728
#define PALVT                   625

#define VCLKStartFreq           25      
//Freq of first item in VCLKTable 

#define SoftDramType            0x80
#define VCLK65                  0x09
#define VCLK108_2               0x14
//#define LCDIs1280x1024Panel     0x04
//#define HiVisionVCLK            0x22
#define TVSimuMode              0x02
#define SetCRT2ToSVIDEO         0x08
//#define LCDRGB18Bit             0x20
#define LCDRGB18Bit             0x01
#define Panel1280x1024          0x03
#define Panel1024x768           0x02
#define Panel800x600            0x01
#define RPLLDIV2XO              0x04 
#define LoadDACFlag             0x1000
#define AfterLockCRT2           0x4000
#define SupportRAMDAC2          0x0040
#define SupportLCD              0x0020
//#define Support1024x768LCD      0x0020
//#define Support1280x1024LCD     0x0040
#define SetCRT2ToAVIDEO         0x0004
#define SetCRT2ToSCART          0x0010
//#define NoSupportSimuTV         0x0100
#define NoSupportSimuTV         0x2000
#define Ext2StructSize          5
#define SupportTV               0x0008
//#define TVVCLKDIV2              0x020
//#define TVVCLK                  0x021
#define TVVCLKDIV2              0x021
#define TVVCLK                  0x022
#define SwitchToCRT2            0x0002
#define LCDVESATiming           0x08
#define SetSCARTOutput          0x01
#define SCARTSense              0x04
#define Monitor1Sense           0x20
#define Monitor2Sense           0x10
#define SVIDEOSense             0x02
#define AVIDEOSense             0x01
#define LCDSense                0x08
#define BoardTVType             0x02
#define HotPlugFunction         0x08
#define StStructSize            0x06

#define ExtChip301              0x02
#define ExtChipLVDS             0x04
#define ExtChipTrumpion         0x06
#define LCDNonExpanding         0x10
#define LCDNonExpandingShift    4
#define LVDSDataLen             6
#define EnableLVDSDDA           0x10
#define LCDSync                 0x20
#define SyncPP                  0x0000
#define LCDSyncBit              0xE0
#define LVDSDesDataLen          3
#define LVDSCRT1Len             15
#define ActiveNonExpanding	0x40
#define ActiveNonExpandingShift	6
#define ModeSwitchStatus	0x0F
#define SoftTVType		0x40
	
#define PanelType00             0x00    
#define PanelType01             0x08
#define PanelType02             0x10
#define PanelType03             0x18
#define PanelType04             0x20
#define PanelType05             0x28
#define PanelType06             0x30
#define PanelType07             0x38
#define PanelType08             0x40
#define PanelType09             0x48
#define PanelType0A             0x50
#define PanelType0B             0x58
#define PanelType0C             0x60
#define PanelType0D             0x68
#define PanelType0E             0x70
#define PanelType0F             0x78

