
/* Common Flash Interface structures 
 * See http://support.intel.com/design/flash/technote/index.htm
 * $Id: cfi.h,v 1.6 2000/07/03 13:29:16 dwmw2 Exp $
 */

#ifndef __MTD_CFI_H__
#define __MTD_CFI_H__

#include <linux/types.h>
#include <linux/mtd/flashchip.h>

/* NB: We keep these structures in memory in HOST byteorder, except
 * where individually noted.
 */

/* Basic Query Structure */
struct cfi_ident {
  __u8  qry[3];
  __u16 P_ID;
  __u16 P_ADR;
  __u16 A_ID;
  __u16 A_ADR;
  __u8  VccMin;
  __u8  VccMax;
  __u8  VppMin;
  __u8  VppMax;
  __u8  WordWriteTimeoutTyp;
  __u8  BufWriteTimeoutTyp;
  __u8  BlockEraseTimeoutTyp;
  __u8  ChipEraseTimeoutTyp;
  __u8  WordWriteTimeoutMax;
  __u8  BufWriteTimeoutMax;
  __u8  BlockEraseTimeoutMax;
  __u8  ChipEraseTimeoutMax;
  __u8  DevSize;
  __u16 InterfaceDesc;
  __u16 MaxBufWriteSize;
  __u8  NumEraseRegions;
  __u32 EraseRegionInfo[1]; /* Not host ordered */
} __attribute__((packed));

/* Extended Query Structure for both PRI and ALT */

struct cfi_extquery {
  __u8  pri[3];
  __u8  MajorVersion;
  __u8  MinorVersion;
} __attribute__((packed));

/* Vendor-Specific PRI for Intel/Sharp Extended Command Set (0x0001) */

struct cfi_pri_intelext {
  __u8  pri[3];
  __u8  MajorVersion;
  __u8  MinorVersion;
  __u32 FeatureSupport;
  __u8  SuspendCmdSupport;
  __u16 BlkStatusRegMask;
  __u8  VccOptimal;
  __u8  VppOptimal;
} __attribute__((packed));

struct cfi_pri_query {
  __u8  NumFields;
  __u32 ProtField[1]; /* Not host ordered */
} __attribute__((packed));

struct cfi_bri_query {
  __u8  PageModeReadCap;
  __u8  NumFields;
  __u32 ConfField[1]; /* Not host ordered */
} __attribute__((packed));

#define P_ID_NONE 0
#define P_ID_INTEL_EXT 1
#define P_ID_AMD_STD 2
#define P_ID_INTEL_STD 3
#define P_ID_AMD_EXT 4
#define P_ID_MITSUBISHI_STD 256
#define P_ID_MITSUBISHI_EXT 257
#define P_ID_RESERVED 65535


struct cfi_private {
	__u16 cmdset;
	void *cmdset_priv;
	int interleave;
	struct mtd_info *(*cmdset_setup)(struct map_info *);
	struct cfi_ident cfiq; /* For now only one. We insist that all devs
				  must be of the same type. */
	int numchips;
	unsigned long chipshift; /* Because they're of the same type */
	const char *im_name;	 /* inter_module name for cmdset_setup */
	struct flchip chips[0];  /* per-chip data structure for each chip */
	/* do not add extra fields after "chips" */
};

#define MAX_CFI_CHIPS 8 /* Entirely arbitrary to avoid realloc() */

#endif /* __MTD_CFI_H__ */
