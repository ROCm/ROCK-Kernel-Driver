/******************************************************************************
 *                  QLOGIC LINUX SOFTWARE
 *
 * QLogic ISP4xxx device driver for Linux 2.6.x
 * Copyright (C) 2004 QLogic Corporation
 * (www.qlogic.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 ******************************************************************************/
/*
 * Module Name: ql4nvrm.h
 */


#ifndef _QL2XNVRM_H_
#define _QL2XNVRM_H_


//
// AM29LV Flash definitions
//
#define  FM93C56A_SIZE_8      0x100
#define  FM93C56A_SIZE_16     0x80
#define  FM93C66A_SIZE_8      0x200
#define  FM93C66A_SIZE_16     0x100
#define  FM93C86A_SIZE_16     0x400
	
#ifdef QLA4022
#define  EEPROM_SIZE          FM93C86A_SIZE_16
#else
#define  EEPROM_SIZE          FM93C66A_SIZE_16
#endif

#define  FM93C56A_START       0x1

// Commands
#define  FM93C56A_READ        0x2
#define  FM93C56A_WEN         0x0
#define  FM93C56A_WRITE       0x1
#define  FM93C56A_WRITE_ALL   0x0
#define  FM93C56A_WDS         0x0
#define  FM93C56A_ERASE       0x3
#define  FM93C56A_ERASE_ALL   0x0

// Command Extentions
#define  FM93C56A_WEN_EXT        0x3
#define  FM93C56A_WRITE_ALL_EXT  0x1
#define  FM93C56A_WDS_EXT        0x0
#define  FM93C56A_ERASE_ALL_EXT  0x2

// Address Bits
#define  FM93C56A_NO_ADDR_BITS_16   8
#define  FM93C56A_NO_ADDR_BITS_8    9
#define  FM93C86A_NO_ADDR_BITS_16   10

#ifdef QLA4022
#define  EEPROM_NO_ADDR_BITS  FM93C86A_NO_ADDR_BITS_16
#else
#define  EEPROM_NO_ADDR_BITS  FM93C56A_NO_ADDR_BITS_16
#endif


// Data Bits
#define  FM93C56A_DATA_BITS_16   16
#define  FM93C56A_DATA_BITS_8    8

#define  EEPROM_NO_DATA_BITS  FM93C56A_DATA_BITS_16
	
// Special Bits
#define  FM93C56A_READ_DUMMY_BITS   1
#define  FM93C56A_READY             0
#define  FM93C56A_BUSY              1
#define  FM93C56A_CMD_BITS          2

// Auburn Bits
#define  AUBURN_EEPROM_DI           0x8
#define  AUBURN_EEPROM_DI_0         0x0
#define  AUBURN_EEPROM_DI_1         0x8
#define  AUBURN_EEPROM_DO           0x4
#define  AUBURN_EEPROM_DO_0         0x0
#define  AUBURN_EEPROM_DO_1         0x4
#define  AUBURN_EEPROM_CS           0x2
#define  AUBURN_EEPROM_CS_0         0x0
#define  AUBURN_EEPROM_CS_1         0x2
#define  AUBURN_EEPROM_CLK_RISE     0x1
#define  AUBURN_EEPROM_CLK_FALL     0x0


//
// EEPROM format
//
typedef struct _BIOS_PARAMS
{
    UINT16  SpinUpDelay                  :1;
    UINT16  BIOSDisable                  :1;
    UINT16  MMAPEnable                   :1;
    UINT16  BootEnable                   :1;
    UINT16  Reserved0                    :12;

    UINT8   bootID0                      :7;
    UINT8   bootID0Valid                 :1;

    UINT8   bootLUN0[8];

    UINT8   bootID1                      :7;
    UINT8   bootID1Valid                 :1;

    UINT8   bootLUN1[8];

    UINT16  MaxLunsPerTarget;
    UINT8   Reserved1[10];
} BIOS_PARAMS, *PBIOS_PARAMS;

typedef struct _EEPROM_PORT_CFG
{
   // MTU MAC 0
   u16               etherMtu_mac;

   // Flow Control MAC 0
   u16               pauseThreshold_mac;
   u16               resumeThreshold_mac;
   u16               reserved[13];
} EEPROM_PORT_CFG, *PEEPROM_PORT_CFG;

typedef struct _EEPROM_FUNCTION_CFG
{
   u8                reserved[30];

   // MAC ADDR
   u8                macAddress[6];
   u8                macAddressSecondary[6];

   u16               subsysVendorId;
   u16               subsysDeviceId;
} EEPROM_FUNCTION_CFG;

typedef struct {
	union {
		struct { /* isp4010 */
			u8    asic_id[4];				// x00
			u8    version;					// x04
			u8    reserved;					// x05

			u16   board_id;					// x06
		     #  define   EEPROM_BOARDID_ELDORADO    1
		     #  define   EEPROM_BOARDID_PLACER      2

		     #  define EEPROM_SERIAL_NUM_SIZE       16
			u8    serial_number[EEPROM_SERIAL_NUM_SIZE];	// x08

		     // ExtHwConfig:
		     // Offset = 24bytes
		     //
		     // | SSRAM Size|     |ST|PD|SDRAM SZ| W| B| SP  |  |
		     // |15|14|13|12|11|10| 9| 8 | 7| 6| 5| 4| 3| 2| 1| 0|
		     // +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
			u16   ext_hw_conf;				// x18

			u8    mac0[6];					// x1A
			u8    mac1[6];					// x20
			u8    mac2[6];					// x26
			u8    mac3[6];					// x2C
	
			u16   etherMtu;					// x32
			u16   macConfig;				// x34
		     #define  MAC_CONFIG_ENABLE_ANEG     0x0001
		     #define  MAC_CONFIG_ENABLE_PAUSE    0x0002

			u16   phyConfig;				// x36
		     #define  PHY_CONFIG_PHY_ADDR_MASK             0x1f
		     #define  PHY_CONFIG_ENABLE_FW_MANAGEMENT_MASK 0x20

		        u16   topcat;					// x38
		     #define TOPCAT_PRESENT		0x0100
		     #define TOPCAT_MASK		0xFF00

		     #  define EEPROM_UNUSED_1_SIZE   2
			u8    unused_1[EEPROM_UNUSED_1_SIZE];		// x3A

			u16   bufletSize;				// x3C
			u16   bufletCount;				// x3E
			u16   bufletPauseThreshold;			// x40
			u16   tcpWindowThreshold50;			// x42
			u16   tcpWindowThreshold25;			// x44
			u16   tcpWindowThreshold0;			// x46
			u16   ipHashTableBaseHi;			// x48
			u16   ipHashTableBaseLo;			// x4A
			u16   ipHashTableSize;				// x4C
			u16   tcpHashTableBaseHi;			// x4E
			u16   tcpHashTableBaseLo;			// x50
			u16   tcpHashTableSize;				// x52
			u16   ncbTableBaseHi;                        	// x54
			u16   ncbTableBaseLo;                        	// x56
			u16   ncbTableSize;                          	// x58
			u16   drbTableBaseHi;                        	// x5A
			u16   drbTableBaseLo;                        	// x5C
			u16   drbTableSize;                          	// x5E

		     #  define EEPROM_UNUSED_2_SIZE   4
			u8    unused_2[EEPROM_UNUSED_2_SIZE];        	// x60

			u16   ipReassemblyTimeout;                   	// x64
			u16   tcpMaxWindowSizeHi;                    	// x66
			u16   tcpMaxWindowSizeLo;                    	// x68

			u32   net_ip_addr0 ;	               		// x6A /* Added for TOE functionality. */
			u32   net_ip_addr1 ;	                        // x6E
			u32   scsi_ip_addr0 ;	                	// x72
			u32   scsi_ip_addr1 ;	                	// x76
		     #  define EEPROM_UNUSED_3_SIZE   128	/* changed from 144 to account for ip addresses */
			u8    unused_3[EEPROM_UNUSED_3_SIZE];        	// x7A

			u16   subsysVendorId_f0;                     	// xFA
			u16   subsysDeviceId_f0;                     	// xFC

			// Address = 0x7F
		     #  define FM93C56A_SIGNATURE  0x9356
		     #  define FM93C66A_SIGNATURE  0x9366
			u16   signature;                             	// xFE

		     #  define EEPROM_UNUSED_4_SIZE   250
			u8    unused_4[EEPROM_UNUSED_4_SIZE];        	// x100

			u16   subsysVendorId_f1;                     	// x1FA
			u16   subsysDeviceId_f1;                     	// x1FC

			u16   checksum;                              	// x1FE
		} __attribute__((packed)) isp4010;

		struct { /* isp4022 */
			u8                asicId[4];                    // x00
			u8                version;                      // x04
			u8                reserved_5;                   // x05

			u16               boardId;                      // x06
			u8                boardIdStr[16];               // x08
			u8                serialNumber[16];             // x18

			// External Hardware Configuration
			u16               ext_hw_conf;                  // x28

			// MAC 0 CONFIGURATION
			EEPROM_PORT_CFG macCfg_port0;                   // x2A

			// MAC 1 CONFIGURATION
			EEPROM_PORT_CFG macCfg_port1;                   // x4A

			// DDR SDRAM Configuration
			u16               bufletSize;                   // x6A
			u16               bufletCount;                  // x6C
			u16               tcpWindowThreshold50;		// x6E
			u16               tcpWindowThreshold25;         // x70
			u16               tcpWindowThreshold0;          // x72
			u16               ipHashTableBaseHi;            // x74
			u16               ipHashTableBaseLo;            // x76
			u16               ipHashTableSize;              // x78
			u16               tcpHashTableBaseHi;           // x7A
			u16               tcpHashTableBaseLo;           // x7C
			u16               tcpHashTableSize;             // x7E
			u16               ncbTableBaseHi;               // x80
			u16               ncbTableBaseLo;               // x82
			u16               ncbTableSize;                 // x84
			u16               drbTableBaseHi;               // x86
			u16               drbTableBaseLo;               // x88
			u16               drbTableSize;                 // x8A
			u16               reserved_142[4];              // x8C

			// TCP/IP Parameters
			u16               ipReassemblyTimeout;          // x94
			u16               tcpMaxWindowSize;             // x96
			u16               ipSecurity;                   // x98

			u8                reserved_156[294];            // x9A
			u16               qDebug[8];  // QLOGIC USE ONLY   x1C0

			EEPROM_FUNCTION_CFG  funcCfg_fn0;               // x1D0
			u16               reserved_510;                 // x1FE

			// Address = 512
			u8                oemSpace[432];                // x200

			BIOS_PARAMS          sBIOSParams_fn1;           // x3B0
			EEPROM_FUNCTION_CFG  funcCfg_fn1;               // x3D0
			u16               reserved_1022;                // x3FE

			// Address = 1024
			u8                reserved_1024[464];           // x400
			EEPROM_FUNCTION_CFG  funcCfg_fn2;               // x5D0

			u16               reserved_1534;                // x5FE

			// Address = 1536
			u8                reserved_1536[432];           // x600
			BIOS_PARAMS          sBIOSParams_fn3;           // x7B0
			EEPROM_FUNCTION_CFG  funcCfg_fn3;               // x7D0

			u16               checksum;                     // x7FE
		} __attribute__((packed)) isp4022;
	};

} eeprom_data_t;

#define EEPROM_EXT_HW_CONF_OFFSET() \
	(IS_QLA4022(ha) ? \
	 offsetof(eeprom_data_t, isp4022.ext_hw_conf) / 2 : \
	 offsetof(eeprom_data_t, isp4010.ext_hw_conf) / 2)


/*************************************************************************
 *
 *			Hardware Semaphore
 *
 *************************************************************************/
//
// Semaphore register definitions
//
#define SEM_AVAILABLE        	0x00
#define SEM_OWNER_FIRMWARE   	0x01
#define SEM_OWNER_STORAGE    	0x02
#define SEM_OWNER_NETWORK    	0x03


//
// Private Semaphore definitions
//
typedef enum
{
	SEM_DRIVER
	, SEM_GPO
	, SEM_SDRAM_INIT
	, SEM_DRAM
	, SEM_PHY_GBIC
	, SEM_NVRAM
	, SEM_FLASH
	, SEM_HW_LOCK

	, SEM_COUNT // Not a real semaphore, just indicates how many there are
} ISP4XXX_SEMAPHORE;

typedef struct {
	UINT32   semId;
	UINT32   semShift;
} isp4xxxSemInfo_t;


#define SEM_MASK  0x3

/* Wait flag defines -- specifies type of wait to acquire semaphore */
#define SEM_FLG_NO_WAIT		0
#define SEM_FLG_WAIT_FOREVER	1
#define SEM_FLG_TIMED_WAIT	2



#endif // _QL2XNVRM_H_

/*
 * Overrides for Emacs so that we get a uniform tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-indent-level: 4
 * c-brace-imaginary-offset: 0
 * c-brace-offset: -4
 * c-argdecl-indent: 4
 * c-label-offset: -4
 * c-continued-statement-offset: 4
 * c-continued-brace-offset: 0
 * indent-tabs-mode: nil
 * tab-width: 8
 * End:
 */
