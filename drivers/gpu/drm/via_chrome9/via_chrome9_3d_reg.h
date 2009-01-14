/*
 * Copyright 1998-2003 VIA Technologies, Inc. All Rights Reserved.
 * Copyright 2001-2003 S3 Graphics, Inc. All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sub license,
 * and/or sell copies of the Software, and to permit persons to
 * whom the Software is furnished to do so, subject to the
 * following conditions:
 *
 * The above copyright notice and this permission notice
 * (including the next paragraph) shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NON-INFRINGEMENT. IN NO EVENT SHALL VIA, S3 GRAPHICS, AND/OR
 * ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR
 * THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef VIA_CHROME9_3D_REG_H
#define VIA_CHROME9_3D_REG_H
#define GetMMIORegister(base, offset)      \
	(*(__volatile__ unsigned int *)(void *)(((unsigned char *)(base)) + \
	(offset)))
#define SetMMIORegister(base, offset, val) \
	(*(__volatile__ unsigned int *)(void *)(((unsigned char *)(base)) + \
	(offset)) = (val))

#define GetMMIORegisterU8(base, offset)      \
	(*(__volatile__ unsigned char *)(void *)(((unsigned char *)(base)) + \
	(offset)))
#define SetMMIORegisterU8(base, offset, val) \
	(*(__volatile__ unsigned char *)(void *)(((unsigned char *)(base)) + \
	(offset)) = (val))

#define BCI_SEND(bci, value)   (*(bci)++ = (unsigned long)(value))
#define BCI_SET_STREAM_REGISTER(bci_base, bci_index, reg_value)         \
do {                                                                    \
	unsigned long cmd;                                              \
									\
	cmd = (0x90000000                                               \
		| (1<<16) /* stream processor register */               \
		| (bci_index & 0x3FFC)); /* MMIO register address */    \
	BCI_SEND(bci_base, cmd);                                        \
	BCI_SEND(bci_base, reg_value);                                  \
	} while (0)

/* Command Header Type */

#define INV_AGPHeader0              0xFE000000
#define INV_AGPHeader1              0xFE010000
#define INV_AGPHeader2              0xFE020000
#define INV_AGPHeader3              0xFE030000
#define INV_AGPHeader4              0xFE040000
#define INV_AGPHeader5              0xFE050000
#define INV_AGPHeader6              0xFE060000
#define INV_AGPHeader7              0xFE070000
#define INV_AGPHeader82             0xFE820000
#define INV_AGPHeader_MASK          0xFFFF0000
#define INV_DUMMY_MASK              0xFF000000

/*send pause address of AGP ring command buffer via_chrome9 this IO port*/
#define INV_REG_PCIPAUSE            0x294
#define INV_REG_PCIPAUSE_ENABLE     0x4

#define INV_CMDBUF_THRESHOLD     (8)
#define INV_QW_PAUSE_ALIGN       0x40

/* Transmission IO Space*/
#define INV_REG_CR_TRANS            0x041C
#define INV_REG_CR_BEGIN            0x0420
#define INV_REG_CR_END              0x0438

#define INV_REG_3D_TRANS            0x043C
#define INV_REG_3D_BEGIN            0x0440
#define INV_REG_3D_END              0x06FC
#define INV_REG_23D_WAIT            0x326C
/*3D / 2D ID Control (Only For Group A)*/
#define INV_REG_2D3D_ID_CTRL     0x060


/* Engine Status */

#define INV_RB_ENG_STATUS           0x0400
#define INV_ENG_BUSY_HQV0           0x00040000
#define INV_ENG_BUSY_HQV1           0x00020000
#define INV_ENG_BUSY_CR             0x00000010
#define INV_ENG_BUSY_MPEG           0x00000008
#define INV_ENG_BUSY_VQ             0x00000004
#define INV_ENG_BUSY_2D             0x00000002
#define INV_ENG_BUSY_3D             0x00001FE1
#define INV_ENG_BUSY_ALL            		\
	(INV_ENG_BUSY_2D | INV_ENG_BUSY_3D | INV_ENG_BUSY_CR)

/* Command Queue Status*/
#define INV_RB_VQ_STATUS            0x0448
#define INV_VQ_FULL                 0x40000000

/* AGP command buffer pointer current position*/
#define INV_RB_AGPCMD_CURRADDR      0x043C

/* AGP command buffer status*/
#define INV_RB_AGPCMD_STATUS        0x0444
#define INV_AGPCMD_InPause          0x80000000

/*AGP command buffer pause address*/
#define INV_RB_AGPCMD_PAUSEADDR     0x045C

/*AGP command buffer jump address*/
#define INV_RB_AGPCMD_JUMPADDR      0x0460

/*AGP command buffer start address*/
#define INV_RB_AGPCMD_STARTADDR      0x0464


/* Constants */
#define NUMBER_OF_EVENT_TAGS        1024
#define NUMBER_OF_APERTURES_CLB     16

/* Register definition */
#define HW_SHADOW_ADDR              0x8520
#define HW_GARTTABLE_ADDR           0x8540

#define INV_HSWFlag_DBGMASK          0x00000FFF
#define INV_HSWFlag_ENCODEMASK       0x007FFFF0
#define INV_HSWFlag_ADDRSHFT         8
#define INV_HSWFlag_DECODEMASK       			\
	(INV_HSWFlag_ENCODEMASK << INV_HSWFlag_ADDRSHFT)
#define INV_HSWFlag_ADDR_ENCODE(x)   0xCC000000
#define INV_HSWFlag_ADDR_DECODE(x)    			\
	(((unsigned int)x & INV_HSWFlag_DECODEMASK) >> INV_HSWFlag_ADDRSHFT)


#define INV_SubA_HAGPBstL        0x60000000
#define INV_SubA_HAGPBstH        0x61000000
#define INV_SubA_HAGPBendL       0x62000000
#define INV_SubA_HAGPBendH       0x63000000
#define INV_SubA_HAGPBpL         0x64000000
#define INV_SubA_HAGPBpID        0x65000000
#define INV_HAGPBpID_PAUSE               0x00000000
#define INV_HAGPBpID_JUMP                0x00000100
#define INV_HAGPBpID_STOP                0x00000200

#define INV_HAGPBpH_MASK                 0x000000FF
#define INV_HAGPBpH_SHFT                 0

#define INV_SubA_HAGPBjumpL      0x66000000
#define INV_SubA_HAGPBjumpH      0x67000000
#define INV_HAGPBjumpH_MASK              0x000000FF
#define INV_HAGPBjumpH_SHFT              0

#define INV_SubA_HFthRCM         0x68000000
#define INV_HFthRCM_MASK                 0x003F0000
#define INV_HFthRCM_SHFT                 16
#define INV_HFthRCM_8                    0x00080000
#define INV_HFthRCM_10                   0x000A0000
#define INV_HFthRCM_18                   0x00120000
#define INV_HFthRCM_24                   0x00180000
#define INV_HFthRCM_32                   0x00200000

#define INV_HAGPBClear                   0x00000008

#define INV_HRSTTrig_RestoreAGP          0x00000004
#define INV_HRSTTrig_RestoreAll          0x00000002
#define INV_HAGPBTrig                    0x00000001

#define INV_ParaSubType_MASK     0xff000000
#define INV_ParaType_MASK        0x00ff0000
#define INV_ParaOS_MASK          0x0000ff00
#define INV_ParaAdr_MASK         0x000000ff
#define INV_ParaSubType_SHIFT    24
#define INV_ParaType_SHIFT       16
#define INV_ParaOS_SHIFT         8
#define INV_ParaAdr_SHIFT        0

#define INV_ParaType_Vdata       0x00000000
#define INV_ParaType_Attr        0x00010000
#define INV_ParaType_Tex         0x00020000
#define INV_ParaType_Pal         0x00030000
#define INV_ParaType_FVF         0x00040000
#define INV_ParaType_PreCR       0x00100000
#define INV_ParaType_CR          0x00110000
#define INV_ParaType_Cfg         0x00fe0000
#define INV_ParaType_Dummy       0x00300000

#define INV_SubType_Tex0         0x00000000
#define INV_SubType_Tex1         0x00000001
#define INV_SubType_Tex2         0x00000002
#define INV_SubType_Tex3         0x00000003
#define INV_SubType_Tex4         0x00000004
#define INV_SubType_Tex5         0x00000005
#define INV_SubType_Tex6         0x00000006
#define INV_SubType_Tex7         0x00000007
#define INV_SubType_General      0x000000fe
#define INV_SubType_TexSample    0x00000020

#define INV_HWBasL_MASK          0x00FFFFFF
#define INV_HWBasH_MASK          0xFF000000
#define INV_HWBasH_SHFT          24
#define INV_HWBasL(x)            ((unsigned int)(x) & INV_HWBasL_MASK)
#define INV_HWBasH(x)            ((unsigned int)(x) >> INV_HWBasH_SHFT)
#define INV_HWBas256(x)          ((unsigned int)(x) >> 8)
#define INV_HWPit32(x)           ((unsigned int)(x) >> 5)

/* Read Back Register Setting */
#define INV_SubA_HSetRBGID       	 0x02000000
#define INV_HSetRBGID_CR                 0x00000000
#define INV_HSetRBGID_FE                 0x00000001
#define INV_HSetRBGID_PE                 0x00000002
#define INV_HSetRBGID_RC                 0x00000003
#define INV_HSetRBGID_PS                 0x00000004
#define INV_HSetRBGID_XE                 0x00000005
#define INV_HSetRBGID_BE                 0x00000006


struct drm_clb_event_tag_info {
	unsigned int *linear_address;
	unsigned int *event_tag_linear_address;
	int   usage[NUMBER_OF_EVENT_TAGS];
	unsigned int   pid[NUMBER_OF_EVENT_TAGS];
};

static inline int is_agp_header(unsigned int data)
{
	switch (data & INV_AGPHeader_MASK) {
	case INV_AGPHeader0:
	case INV_AGPHeader1:
	case INV_AGPHeader2:
	case INV_AGPHeader3:
	case INV_AGPHeader4:
	case INV_AGPHeader5:
	case INV_AGPHeader6:
	case INV_AGPHeader7:
		return 1;
	default:
		return 0;
	}
}

/*  Header0: 2D */
#define ADDCmdHeader0_INVI(pCmd, dwCount)                       \
{                                                               \
	/* 4 unsigned int align, insert NULL Command for padding */    \
	while (((unsigned long *)(pCmd)) & 0xF) {                    \
		*(pCmd)++ = 0xCC000000;                         \
	}                                                       \
	*(pCmd)++ = INV_AGPHeader0;                             \
	*(pCmd)++ = (dwCount);                                  \
	*(pCmd)++ = 0;                                          \
	*(pCmd)++ = (unsigned int)INV_HSWFlag_ADDR_ENCODE(pCmd);       \
}

/* Header1: 2D */
#define ADDCmdHeader1_INVI(pCmd, dwAddr, dwCount)               \
{                                                               \
	/* 4 unsigned int align, insert NULL Command for padding */    \
	while (((unsigned long *)(pCmd)) & 0xF) {                    \
		*(pCmd)++ = 0xCC000000;                         \
	}                                                       \
	*(pCmd)++ = INV_AGPHeader1 | (dwAddr);                  \
	*(pCmd)++ = (dwCount);                                  \
	*(pCmd)++ = 0;                                          \
	*(pCmd)++ = (unsigned int)INV_HSWFlag_ADDR_ENCODE(pCmd);       \
}

/* Header2: CR/3D */
#define ADDCmdHeader2_INVI(pCmd, dwAddr, dwType)                \
{                                                               \
	/* 4 unsigned int align, insert NULL Command for padding */    \
	while (((unsigned int)(pCmd)) & 0xF) {                        \
		*(pCmd)++ = 0xCC000000;                         \
	}                                                       \
	*(pCmd)++ = INV_AGPHeader2 | ((dwAddr)+4);              \
	*(pCmd)++ = (dwAddr);                                   \
	*(pCmd)++ = (dwType);                                   \
	*(pCmd)++ = (unsigned int)INV_HSWFlag_ADDR_ENCODE(pCmd);       \
}

/* Header2: CR/3D with SW Flag */
#define ADDCmdHeader2_SWFlag_INVI(pCmd, dwAddr, dwType, dwSWFlag)  \
{                                                                  \
	/* 4 unsigned int align, insert NULL Command for padding */       \
	while (((unsigned long *)(pCmd)) & 0xF) {			   \
		*(pCmd)++ = 0xCC000000;                            \
	}                                                          \
	*(pCmd)++ = INV_AGPHeader2 | ((dwAddr)+4);                 \
	*(pCmd)++ = (dwAddr);                                      \
	*(pCmd)++ = (dwType);                                      \
	*(pCmd)++ = (dwSWFlag);                                    \
}


/* Header3: 3D */
#define ADDCmdHeader3_INVI(pCmd, dwType, dwStart, dwCount)      \
{                                                               \
	/* 4 unsigned int align, insert NULL Command for padding */    \
	while (((unsigned long *)(pCmd)) & 0xF) {			\
		*(pCmd)++ = 0xCC000000;                         \
	}                                                       \
	*(pCmd)++ = INV_AGPHeader3 | INV_REG_3D_TRANS;          \
	*(pCmd)++ = (dwCount);                                  \
	*(pCmd)++ = (dwType) | ((dwStart) & 0xFFFF);            \
	*(pCmd)++ = (unsigned int)INV_HSWFlag_ADDR_ENCODE(pCmd);       \
}

/* Header3: 3D with SW Flag */
#define ADDCmdHeader3_SWFlag_INVI(pCmd, dwType, dwStart, dwSWFlag, dwCount)  \
{                                                                            \
	/* 4 unsigned int align, insert NULL Command for padding */          \
	while (((unsigned long *)(pCmd)) & 0xF) {                           \
		*(pCmd)++ = 0xCC000000;                                      \
	}                                                                    \
	*(pCmd)++ = INV_AGPHeader3 | INV_REG_3D_TRANS;                       \
	*(pCmd)++ = (dwCount);                                               \
	*(pCmd)++ = (dwType) | ((dwStart) & 0xFFFF);                         \
	*(pCmd)++ = (dwSWFlag);                                              \
}

/* Header4: DVD */
#define ADDCmdHeader4_INVI(pCmd, dwAddr, dwCount, id)           \
{                                                               \
    /* 4 unsigned int align, insert NULL Command for padding */ \
	while (((unsigned long *)(pCmd)) & 0xF) {              \
		*(pCmd)++ = 0xCC000000;                         \
	}                                                       \
	*(pCmd)++ = INV_AGPHeader4 | (dwAddr);                  \
	*(pCmd)++ = (dwCount);                                  \
	*(pCmd)++ = (id);                                       \
	*(pCmd)++ = 0;                                          \
}

/* Header5: DVD */
#define ADDCmdHeader5_INVI(pCmd, dwQWcount, id)                 \
{                                                               \
	/* 4 unsigned int align, insert NULL Command for padding */    \
	while (((unsigned long *)(pCmd)) & 0xF) {              \
		*(pCmd)++ = 0xCC000000;                                 \
	}                                                       \
	*(pCmd)++ = INV_AGPHeader5;                             \
	*(pCmd)++ = (dwQWcount);                                \
	*(pCmd)++ = (id);                                       \
	*(pCmd)++ = 0;                                          \
}

/* Header6: DEBUG */
#define ADDCmdHeader6_INVI(pCmd)                                \
{                                                               \
	/* 4 unsigned int align, insert NULL Command for padding */    \
	while (((unsigned long *)(pCmd)) & 0xF) {                    \
		*(pCmd)++ = 0xCC000000;                         \
	}                                                       \
	*(pCmd)++ = INV_AGPHeader6;                             \
	*(pCmd)++ = 0;                                          \
	*(pCmd)++ = 0;                                          \
	*(pCmd)++ = 0;                                          \
}

/* Header7: DMA */
#define ADDCmdHeader7_INVI(pCmd, dwQWcount, id)                 \
{                                                               \
	/* 4 unsigned int align, insert NULL Command for padding */    \
	while (((unsigned long *)(pCmd)) & 0xF) {                    \
		*(pCmd)++ = 0xCC000000;                         \
	}                                                       \
	*(pCmd)++ = INV_AGPHeader7;                             \
	*(pCmd)++ = (dwQWcount);                                \
	*(pCmd)++ = (id);                                       \
	*(pCmd)++ = 0;                                          \
}

/* Header82: Branch buffer */
#define ADDCmdHeader82_INVI(pCmd, dwAddr, dwType);              \
{                                                               \
	/* 4 unsigned int align, insert NULL Command for padding */    \
	while (((unsigned long *)(pCmd)) & 0xF) {                    \
		*(pCmd)++ = 0xCC000000;                         \
	}                                                       \
	*(pCmd)++ = INV_AGPHeader82 | ((dwAddr)+4);             \
	*(pCmd)++ = (dwAddr);                                   \
	*(pCmd)++ = (dwType);                                   \
	*(pCmd)++ = 0xCC000000;                                 \
}


#define ADD2DCmd_INVI(pCmd, dwAddr, dwCmd)                  \
{                                                           \
	*(pCmd)++ = (dwAddr);                               \
	*(pCmd)++ = (dwCmd);                                \
}

#define ADDCmdData_INVI(pCmd, dwCmd)             (*(pCmd)++ = (dwCmd))

#define ADDCmdDataStream_INVI(pCmdBuf, pCmd, dwCount)       \
{                                                           \
	memcpy((pCmdBuf), (pCmd), ((dwCount)<<2));        \
	(pCmdBuf) += (dwCount);                             \
}

#endif
