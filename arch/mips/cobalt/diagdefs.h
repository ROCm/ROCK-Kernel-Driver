/*
 * Filename: diagdefs.h
 * 
 * Description: Some general definitions used by the diagnostics 
 * 
 * Author(s): Timothy Stonis
 * 
 * Copyright 1997, Cobalt Microserver, Inc.
 */

#define KSEG0_Base		0x80000000
#define KSEG1_Base		0xA0000000

// Some useful Galileo registers/base addresses (boot time kseg1 mapping) 
#define kGal_InternalBase	( 0x14000000 | KSEG1_Base ) 
#define kGal_DevBank0Base	( 0x1C000000 | KSEG1_Base )
#define kGal_DevBank1Base 	( 0x1C800000 | KSEG1_Base )

#define kGal_RAS10Lo		0x008
#define kGal_RAS10Hi		0x010
#define kGal_RAS32Lo		0x018
#define kGal_RAS32Hi		0x020

#define kGal_PCIIOLo		0x048
#define kGal_PCIIOHi		0x050

#define kGal_RAS10LoCfg		0x000
#define kGal_RAS10HiCfg		0x03
#define kGal_RAS32LoCfg		0x004
#define kGal_RAS32HiCfg		0x07

#define kGal_PCIIOLoCfg		0x000
#define kGal_PCIIOHiCfg		0x0F


#define kGal_DevBank0PReg	0x45C
#define kGal_DevBank1PReg	0x460
#define kGal_DevBank2PReg	0x464
#define kGal_DevBank3PReg	0x468
#define kGal_DevBankBPReg	0x46C

#define kGal_DRAMCReg		0x448
#define kGal_DRAM0PReg		0x44C
#define kGal_DRAM1PReg		0x450
#define kGal_DRAM2PReg		0x454
#define kGal_DRAM3PReg		0x458

#define kGal_ConfigAddr		0xCF8
#define kGal_ConfigData		0xCFC
#define kGal_PCICfgEn		0x1F // Generate config cycle 
#define kGal_DevNum		0x00 // Technically 0x06, but 0 works too
#define kGal_MasMemEn		0x06
#define kGal_Latency		0x700

#define kGal_RAS01StartReg	0x10
#define kGal_RAS23StartReg	0x14
#define kGal_RAS01SizeReg	0x0C08
#define kGal_RAS23SizeReg	0x0C0C


#define kGal_RAS01Start		0x000
#define kGal_RAS23Start		0x00800000
#define kGal_RAS01Size		0x007FFFFF
#define kGal_RAS23Size		0x007FFFFF


// Paramter information for devices, DRAM, etc
#define	kGal_DevBank0Cfg	0x1446DB33
#define	kGal_DevBank1Cfg	0x144FE667
#define	kGal_DevBankBCfg	0x1446DC43
#define	kGal_DRAMConfig		0x00000300
#define	kGal_DRAM0Config	0x00000010
#define	kGal_DRAM1Config	0x00000010
#define	kGal_DRAM2Config	0x00000010
#define	kGal_DRAM3Config	0x00000010

#define	kGal_DRAM0Hi		0x00000003
#define	kGal_DRAM0Lo		0x00000000
#define	kGal_DRAM1Hi		0x00000007
#define	kGal_DRAM1Lo		0x00000004
#define	kGal_DRAM2Hi		0x0000000B
#define	kGal_DRAM2Lo		0x00000008
#define	kGal_DRAM3Hi		0x0000000F
#define	kGal_DRAM3Lo		0x0000000C

#define kGal_RAS0Lo		0x400
#define kGal_RAS0Hi		0x404
#define kGal_RAS1Lo		0x408
#define kGal_RAS1Hi		0x40C
#define kGal_RAS2Lo		0x410
#define kGal_RAS2Hi		0x414
#define kGal_RAS3Lo		0x418
#define kGal_RAS3Hi		0x41C

// Feedback LED indicators during setup code (reset.S, main.c) 
#define kLED_AllOn	0x0F
#define kLED_FlashTest	0x01
#define kLED_MemTest	0x02
#define kLED_SCCTest	0x03
#define kLED_GalPCI	0x04
#define kLED_EnetTest	0x05
#define kLED_SCSITest	0x06
#define kLED_IOCTest	0x07
#define kLED_Quickdone	0x0A
#define kLED_Exception	0x0B
#define kLED_ProcInit	0x0E
#define kLED_AllOff	0x00

#define kLEDBase	kGal_DevBank0Base

// Some memory related constants 
#define kRAM_Start	(0x00000000 | KSEG0_Base)

#define	kTestPat1	0xA5A5A5A5
#define kTestPat2	0x5A5A5A5A

#define k1Meg_kseg1 	(0x00100000 | KSEG0_Base)
#define k2Meg_kseg1  	(0x00200000 | KSEG0_Base)
#define k4Meg_kseg1  	(0x00400000 | KSEG0_Base)
#define k8Meg_kseg1  	(0x00800000 | KSEG0_Base)
#define k16Meg_kseg1  	(0x01000000 | KSEG0_Base)

#define kInit_SP	k4Meg_kseg1 - 0x100
#define kVectorBase	0x200	
#define kDebugVectors	0x400
#define kMallocCheese	0x80E00000
#define kDecompAddr	0x80700000
#define kCompAddr	0x80500000


// Ethernet definitions
#define	kEnet_VIOBase	( 0x12100000 | KSEG1_Base )
#define	kEnet_PIOBase	0x12100000
#define	kEnet_CSCfg	0x46
#define kEnet_DevNum	0x07
#define kEnet_CSR3	0x18
#define kEnet_CSR15	0x78


#define kEnet_GEPOut	0x080f0000
#define kEnet_GEPOn	0x000f0000


// SCSI definitions
#define	kSCSI_VIOBase	( 0x12200000 | KSEG1_Base )
#define	kSCSI_PIOBase	0x12200000 
#define	kSCSI_CSCfg	0x46 
#define kSCSI_DevNum	0x08
#define kSCSI_GPCNTL	0x47
#define kSCSI_GPREG	0x07
#define kSCSI_SCRTCHA	0x34

#define kSCSI_GPIOOut	0x0C
#define kSCSI_LEDsOn	0x00

// I/O Controller definitions
#define kIOC_VIOBase	( 0x10000000 | KSEG1_Base )
#define kIOC_RIOBase	0x10000000 
#define kIOC_DevNum	0x09
#define kIOC_ISAFunc	0x00
#define kIOC_IDEFunc	0x01
#define kIOC_USBFunc	0x02
#define kIOC_MiscC0	0x44
#define kIOC_IDEEnable	0x40

#define kIOC_PCIIDEOn 	0x02800085
#define kIOC_PriIDEOn	0x0A

// Some PCI Definitions
#define	kPCI_StatCmd	0x04
#define	kPCI_LatCache	0x0C
#define	kPCI_CBIO	0x10
#define	kPCI_CBMEM	0x14

// Random constants
#define kBogoSec	0x0003F940
#define kSCCDelay	0x00000001
