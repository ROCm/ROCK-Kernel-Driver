/* drivers/char/leedslite.h: header for LeedsLite IBM Crypto Adapter driver for Linux. */
/* Copyright (c) International Business Machines Corp., 2001 */

#ifndef _LEEDSLITE_H_
#define _LEEDSLITE_H_

#ifndef PCI_VENDOR_ID_IBM
#define PCI_VENDOR_ID_IBM 0x1014
#endif

#ifndef PCI_DEVICE_ID_IBM_LEEDSLITE
#define PCI_DEVICE_ID_IBM_LEEDSLITE 0x01e6
#endif


typedef char leedslite_rip_entry_t[1024];
typedef char leedslite_rop_entry_t[256];
typedef u32 leedslite_vfifo_entry_t;

/* Note: VFIFO is always 64 entries! */

#define LEEDSLITE_VFIFO_ENTRIES 64
#define RIP_ENTRY_SIZE sizeof(leedslite_rip_entry_t)
#define ROP_ENTRY_SIZE sizeof(leedslite_rop_entry_t) 
#define VFIFO_ENTRY_SIZE sizeof(leedslite_vfifo_entry_t)

enum leedslite_bar0_registers {
  REG_BMAR0 = 0x00,
  REG_BMCR0 = 0x04,
  REG_BMAR1 = 0x08,
  REG_BMCR1 = 0x0C,
  REG_BMAR2 = 0x10,
  REG_BMCR2 = 0x14,
  REG_BMAR3 = 0x18,
  REG_BMCR3 = 0x1C,
  REG_RFP   = 0x20,
  REG_DFP   = 0x24,
  REG_BMCSR = 0x28,
  REG_PIR = 0x2C,
  REG_PIER = 0x30,
  REG_SCR = 0x34,
  REG_RIBAR = 0x40,
  REG_ROBAR = 0x44,
  REG_VF_BAR = 0x48,
  REG_RCR = 0x4C,
  REG_RSCR = 0x50,
  REG_RSR = 0x54,
  REG_DCR = 0x58,
  REG_OSCR = 0x5C,
  REG_MIR = 0x60,
  REG_MIER = 0x64
};

enum leedslite_sha_key_control_bits {
  SHA_KEY_CONTROL_SET_H = 0x0002,
  SHA_KEY_CONTROL_SET_K = 0x0001
};

enum leedslite_window2_registers {
  REG_CAM_RCR   = 0x00000,
  REG_DES_OUT   = 0x01110,
  REG_DES_IV    = 0x01118,
  REG_DES_KEY_1 = 0x01120,
  REG_DES_KEY_2 = 0x01128,
  REG_DES_KEY_3 = 0x01130,
  REG_DES_CSR   = 0x01138,
  REG_GSR  = 0x1144,
  REG_GSRA = 0x1146,
  REG_GCR  = 0x1148,
  REG_SHA_STATUS = 0x1800,
  REG_SHA_KEY_CONTROL = 0x1802,
  REG_SHA_IN1 = 0x1804,
  REG_SHA_IN2 = 0x1806,
  REG_SHA_K0 = 0x1808,
  REG_SHA_K1 = 0x180c,
  REG_SHA_K2 = 0x1810,
  REG_SHA_K3 = 0x1814,
  REG_SHA_OUT0 = 0x1818,
  REG_SHA_OUT1 = 0x181C,
  REG_SHA_OUT2 = 0x1820,
  REG_SHA_OUT3 = 0x1824,
  REG_SHA_OUT4 = 0x1828,
  REG_RNG = 0x1c00
};

enum leedslite_camelot_gcra_bits {
  CAM_GCRA_RSA_Enable = 0x1000
};

enum leedslite_camelot_gsr_bits {
  CAM_GSR_DES_EN = 0x0400
};

enum leedslite_camelot_gsra_bits {
  CAM_GSRA_RNGSpd0 = 0x0100,
  CAM_GSRA_RNGSpd1 = 0x0200,
  CAM_GSRA_RNGSpd2 = 0x0400,
  CAM_GSRA_RNG_INT_EN = 0x0800,
  CAM_GSRA_SHA_EN = 0x1000 
};

enum leedslite_camelot_descsr {
  CAM_DESCSR_START = 0x2000
};

enum leedslite_camelot_rcr_bits {
  CAM_RCR_COMPLETE = 0x0001
};

enum leedslite_rcr_bits {
  RCR_Opcode_Mod_Mult     = 0x20000000,
  RCR_Opcode_Mod_Expo     = 0x40000000,
  RCR_Opcode_Mod_Expo_CRT = 0x80000000
};

enum leedslite_rcr_shift {
  RCR_MLen_SHIFT = 20,
  RCR_OpID_SHIFT = 11
};


enum leedslite_rscr_bits {
  RSCR_Merlin_ID = 0x000f,
  RSCR_flsh_outFF = 0x0010,
  RSCR_flsh_inFF = 0x0020,
  RSCR_SSP = 0x00C0,
  RSCR_REE = 0x1f00,
  RSCR_RDLE = 0x4000,
  RSCR_RFWB = 0x8000
};

enum leedslite_oscr_bits {
  OSCR_Cam_RST = 0x001f,
  OSCR_CSV = 0x00E0,    
  OSCR_CSV_1 = 0x0020,  // Camelot 1
  OSCR_CSV_2 = 0x0040,  // Camelot 2 
  OSCR_CSV_3 = 0x0060,  // Camelot 3
  OSCR_CSV_4 = 0x0080,  // Camelot 4
  OSCR_CSV_5 = 0x00A0,  // Camelot 5
  OSCR_ADSM = 0x0100,   // Always DSM_done Int.
  OSCR_SAM = 0xFE00     // Speed Adjustment Mechanism
};

enum leedslite_oscr_shift {
  OSCR_CSV_SHIFT = 5,
  OSCR_SAM_SHIFT = 9
};

enum leedslite_sam_limits {
  SAM_MIN = 0x00,  // Maximum performance/power
  SAM_MAX = 0x3f   // Minimum performance/power
};

enum leedslite_mier_bits {
  MIER_WRC = 0x0001,
  MIER_RCRO = 0x0002,
  MIER_WRL = 0x0004,
  MIER_WDC = 0x0040,
  MIER_DCRO = 0x080
};


enum leedslite_pir_bits {
  PIR_RSA_done = 0x0001, 
  PIR_BEO = 0x0002,
  PIR_LWP = 0x00fc,
  PIR_PAO = 0x0100,
  PIR_BCZ0 = 0x0200,
  PIR_BCZ1 = 0x0400,
  PIR_BCZ2 = 0x0800,
  PIR_BCZ3 = 0x1000,
  PIR_DSM_done = 0x2000,
  PIR_RNG_done = 0x4000,
  PIR_MI = 0x8000
};

enum leedslite_pier_bits {
  PIER_RSA_done = 0x0001, 
  PIER_BEO = 0x0002,
  PIER_PAO = 0x0100,
  PIER_BCZ0 = 0x0200,
  PIER_BCZ1 = 0x0400,
  PIER_BCZ2 = 0x0800,
  PIER_BCZ3 = 0x1000,
  PIER_DSM_done = 0x2000,
  PIER_RNG_done = 0x4000,
  PIER_MI = 0x8000
};

enum leedslite_scr_bits {
  SCR_Piuma_ID = 0x000f,
  SCR_VPDWP = 0x2000,
  SCR_AOM = 0x4000,
  SCR_SORST = 0x8000
};


enum leedslite_rsa_vf_bits {
  RSA_VF_ROC = 0x0001,
  RSA_VF_RZF = 0x0002,
  RSA_VF_RCF = 0x0004,
  RSA_VF_RAF = 0x0008,
  RSA_VF_CEF = 0x0010,
  RSA_VF_AEF = 0x0020,
  RSA_VF_CamID = 0x01C0,
  RSA_VF_RSAopID = 0x7e00,
  RSA_VF_VEM = 0x8000,
  RSA_VF_ERR_MASK = RSA_VF_RZF|RSA_VF_RCF|RSA_VF_RAF|RSA_VF_CEF|RSA_VF_AEF
};

enum leedslite_bmscr_bits {
  BMCSR_DES_BM_priority = 0x00000003,
  BMCSR_DES_BM_priority_equal = 0x00000000,
  BMCSR_DES_BM_priority_WR = 0x00000002,
  BMCSR_DES_BM_priority_RD = 0x00000003,
  BMCSR_RSA_BM_priority = 0x0000000C,
  BMCSR_RSA_BM_priority_equal = 0x00000000,
  BMCSR_RSA_BM_priority_WR = 0x00000008,
  BMCSR_RSA_BM_priority_RD = 0x0000000C,
  BMCSR_BM_channel_priority = 0x00000030,
  BMCSR_BM_channel_priority_equal = 0x00000000,
  BMCSR_BM_channel_priority_WR = 0x00000020,
  BMCSR_BM_channel_priority_RD = 0x00000030,
  BMCSR_D_BM_EN = 0x00000040,
  BMCSR_R_BM_EN = 0x00000080,
  BMCSR_flush_DES_to_PCI_FIFO = 0x00000400,
  BMCSR_flush_PCI_to_DES_FIFO = 0x00000800,
  BMCSR_flush_RSA_to_PCI_FIFO = 0x00001000,
  BMCSR_flush_PCI_to_RSA_FIFO = 0x00002000,
  BMCSR_pMWI_EN = 0x00008000,
  BMCSR_last_BM = 0x00C00000
};

enum leedslite_bmcsr_shift {
  BMCSR_last_BM_SHIFT = 22
};

enum leedslite_dcr_bits {
  DCR_IVin = 0x00000001,
  DCR_Kin  = 0x00000002,
  DCR_K_Index   = 0x000003FC,
  DCR_INT_back  = 0x00000400,
  DCR_init_FIPS = 0x00000800,
  DCR_DLen   = 0x03FFF000,
  DCR_TDES   = 0x04000000,
  DCR_ECB    = 0x08000000,
  DCR_Opcode = 0xF0000000,
  DCR_Opcode_DES_Enc       = 0x00000000,
  DCR_Opcode_DES_Dec       = 0x10000000,
  DCR_Opcode_SHA           = 0x40000000,
  DCR_Opcode_DESSHA_Enc    = 0x60000000,
  DCR_Opcode_DESSHA_Dec    = 0x70000000,
  DCR_Opcode_DES2SHA_1_Enc = 0x80000000,
  DCR_Opcode_DES2SHA_1_Dec = 0x90000000,
  DCR_Opcode_DES2SHA_2_Enc = 0xA0000000,
  DCR_Opcode_DES2SHA_2_Dec = 0xB0000000,
  DCR_Opcode_MAC           = 0xC0000000  
};

enum leedslite_dcr_shift {
  DCR_DLen_SHIFT = 12
}; 

enum leedslite_err_limits {
   LEEDSLITE_ERR_TIMEOUT_MIN = 60
};

static inline int _dlen(int length)
{
  return (length / ICA_DES_DATALENGTH_MIN);
}

static inline int _mlen(int length)
{
  return (length / ICA_RSA_DATALENGTH_MIN);
}


#endif /* _LEEDLITE_H_ */
