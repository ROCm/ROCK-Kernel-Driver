/*
**  aic5800.h - Adaptec AIC-5800 PCI-IEEE1394 chip driver header file
**  Copyright (C)1999 Emanuel Pirker <epirker@edu.uni-klu.ac.at>
**
**  This program is free software; you can redistribute it and/or modify
**  it under the terms of the GNU General Public License as published by
**  the Free Software Foundation; either version 2 of the License, or
**  (at your option) any later version.
**
**  This program is distributed in the hope that it will be useful,
**  but WITHOUT ANY WARRANTY; without even the implied warranty of
**  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**  GNU General Public License for more details.
**
**  You should have received a copy of the GNU General Public License
**  along with this program; if not, write to the Free Software
**  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
**
*/

#ifndef AIC5800_H
#define AIC5800_H

#define AIC5800_DRIVER_NAME      "aic5800"

#define MAX_AIC5800_CARDS        4
#define AIC5800_REGSPACE_SIZE    512
#define AIC5800_PBUF_SIZE        512

#define MAX_AT_PROGRAM_SIZE      10
#define AIC5800_ARFIFO_SIZE      128

struct dma_cmd {
    u32 control;
    u32 address;
    u32 branchAddress;
    u32 status;
};

struct aic5800 {
    int id; /* sequential card number */

    struct pci_dev *dev;
    
    /* remapped memory spaces */
    void *registers;
    
    struct hpsb_host *host;
    
    int phyid, isroot;

    void *rcv_page;
    void *pbuf;
    
    struct dma_cmd *AT_program;
    u32 *AT_status;
    struct dma_cmd *AR_program;
    u32 *AR_status;
    int AR_active;
    
    struct hpsb_packet *async_queue;
    spinlock_t async_queue_lock;

    unsigned long NumInterrupts, NumBusResets;
    unsigned long TxPackets, RxPackets;
    unsigned long TxErrors, RxErrors;
    unsigned long TxRdy, ATError, HdrErr, TCodeErr, SendRej;

};


/*
 * Register read and write helper functions.
 */
inline static void reg_write(const struct aic5800 *aic, int offset, u32 data)
{
        writel(data, aic->registers + offset);
}

inline static u32 reg_read(const struct aic5800 *aic, int offset)
{
        return readl(aic->registers + offset);
}

inline static void reg_set_bits(const struct aic5800 *aic, int offset,
                                u32 mask)
{
        reg_write(aic, offset, (reg_read(aic, offset) | mask));
}

inline static void reg_clear_bits(const struct aic5800 *aic, int offset,
                                  u32 mask)
{
        reg_write(aic, offset, (reg_read(aic, offset) & ~mask));
}


/*  AIC-5800 Registers */

#define AT_ChannelControl	0x0
#define AT_ChannelStatus	0x4
#define AT_CommandPtr	0xC
#define AT_InterruptSelect	0x10
#define AT_BranchSelect	0x14
#define AT_WaitSelect	0x18

/* Asynchronous receive */
#define AR_ChannelControl	0x20
#define AR_ChannelStatus	0x24
#define AR_CommandPtr	0x2C

/* ITA */
#define ITA_ChannelControl	0x40
#define ITA_ChannelStatus	0x44
#define ITA_CommandPtr	0x4C

/* ITB */
#define ITB_ChannelControl	0x60
#define ITB_ChannelStatus	0x64
#define ITB_CommandPtr	0x6C

/* IRA */
#define IRA_ChannelControl	0x80
#define IRA_ChannelStatus	0x84
#define IRA_CommandPtr	0x8C

/* IRB */
#define IRB_ChannelControl	0xA0
#define IRB_ChannelStatus	0xA4
#define IRB_CommandPtr	0xAC

/* miscellaneous */
#define misc_Version	0x100
#define misc_Control	0x104
#define misc_NodeID	0x108
#define misc_Reset	0x10C
#define misc_PacketControl	0x110
#define misc_Diagnostic	0x114
#define misc_PhyControl	0x118
#define misc_ATRetries	0x11C
#define misc_SSNinterface	0x120
#define misc_CycleTimer	0x124

/* ITA */
#define ITA_EventCycle	0x130
#define ITA_Configuration	0x134
#define ITA_Bandwidth	0x138

/* ITB */
#define ITB_EventCycle	0x140
#define ITB_Configuration	0x144
#define ITB_Bandwidth	0x148

/* IRA */
#define IRA_EventCycle	0x150
#define IRA_Configuration	0x154

/* IRB */
#define IRB_EventCycle	0x160
#define IRB_Configuration	0x164

/* RSU */
#define RSU_Enable	0x170
#define RSU_Interrupt	0x174
#define RSU_TablePtr	0x178
#define RSU_InterruptSet	0x17C

/* misc */
#define misc_InterruptEvents	0x180
#define misc_InterruptMask	0x184
#define misc_InterruptClear	0x188
#define misc_CardBusEvent	0x1E0
#define misc_CardBusMask	0x1E4
#define misc_CardBusState	0x1E8
#define misc_CardBusForce	0x1EC
#define misc_SEEPCTL	        0x1F0

/* Interrupts */
#define INT_DmaAT             1
#define INT_DmaAR            (1<<1)
#define INT_DmaITA           (1<<2)
#define INT_DmaITB           (1<<3)
#define INT_DmaIRA           (1<<4)
#define INT_DmaIRB           (1<<5)
#define INT_PERResponse      (1<<7)
#define INT_CycleEventITA    (1<<8)
#define INT_CycleEventITB    (1<<9)
#define INT_CycleEventIRA    (1<<10)
#define INT_CycleEventIRB    (1<<11)
#define INT_BusReset         (1<<12)
#define INT_CmdReset         (1<<13)
#define INT_PhyInt           (1<<14)
#define INT_RcvData          (1<<15)
#define INT_TxRdy            (1<<16)
#define INT_CycleStart       (1<<17)
#define INT_CycleSeconds     (1<<18)
#define INT_CycleLost        (1<<19)
#define INT_ATError          (1<<20)
#define INT_SendRej          (1<<21)
#define INT_HdrErr           (1<<22)
#define INT_TCodeErr         (1<<23)
#define INT_PRQUxferErr      (1<<24)
#define INT_PWQUxferErr      (1<<25)
#define INT_RSUxferErr       (1<<26)
#define INT_RSDone           (1<<27)
#define INT_PSOutOfRetries   (1<<28)
#define INT_cycleTooLong     (1<<29)

/* DB DMA constants */
#define DMA_CMD_OUTPUTMORE  0
#define DMA_CMD_OUTPUTLAST  0x10000000
#define DMA_CMD_INPUTMORE   0x20000000
#define DMA_CMD_INPUTLAST   0x30000000
#define DMA_CMD_STOREQUAD   0x40000000
#define DMA_CMD_LOADQUAD    0x50000000
#define DMA_CMD_NOP         0x60000000
#define DMA_CMD_STOP        0x70000000

#define DMA_KEY_STREAM0  0
#define DMA_KEY_STREAM1  (1<<24)
#define DMA_KEY_STREAM2  (2<<24)
#define DMA_KEY_STREAM3  (3<<24)
#define DMA_KEY_REGS     (5<<24)
#define DMA_KEY_SYSTEM   (6<<24)
#define DMA_KEY_DEVICE   (7<<24)

#define DMA_INTR_NEVER    0
#define DMA_INTR_TRUE     (1<<20)
#define DMA_INTR_FALSE    (2<<20)
#define DMA_INTR_ALWAYS   (3<<20)
#define DMA_WAIT_NEVER    0
#define DMA_WAIT_TRUE     (1<<16)
#define DMA_WAIT_FALSE    (2<<16)
#define DMA_WAIT_ALWAYS   (3<<16)
#define DMA_BRANCH_NEVER    0
#define DMA_BRANCH_TRUE     (1<<18)
#define DMA_BRANCH_FALSE    (2<<18)
#define DMA_BRANCH_ALWAYS   (3<<18)

#define DMA_SPEED_100 0
#define DMA_SPEED_200 (1<<16)
#define DMA_SPEED_400 (2<<16)

/* PHY access */
#define LINK_PHY_READ                     (1<<15)
#define LINK_PHY_WRITE                    (1<<14)
#define LINK_PHY_ADDR(addr)               (addr<<8)
#define LINK_PHY_WDATA(data)              (data)
#define LINK_PHY_RADDR(addr)              (addr<<24)

quadlet_t aic5800_csr_rom[] = {
    /* bus info block */
    0x041ffb82,     // length of bus info block, CRC
    0x31333934,     // 1394 designator 
    0xf005a000,     // various capabilites
    0x0000d189,     // node_vendor_id, chip_id_hi
    0x401010fc,     // chip_id_lo
    /* root directory */
    0x00040e54,     // length of root directory, CRC
    0x030000d1,     // module_vendor_id
    0x0c008000,     // various capabilities
    0x8d000006,     // offset of node unique id leaf
    0xd1000001,     // offset of unit directory
    /* unit directory */
    0x0003e60d,     // length of unit directory, CRC
    0x12000000,     // unit_spec_id
    0x13000000,     // unit_sw_version
    0xd4000004,     // offset of unit dependent directory
    /* node unique id leaf */
    0x00026ba7,     // length of leaf, CRC
    0x0000d189,     // node_vendor_id, chip_id_hi
    0x401010fc,     // chip_id_lo
    /* unit dependent directory */
    0x0002ae47,     // length of directory, CRC
    0x81000002,     // offset of vendor name leaf
    0x82000006,     // offset of model name leaf
    /* vendor name leaf */
    0x000486a3,     // length of leaf, CRC
    0x00000000,
    0x00000000,
    0x41444150,     // ADAP
    0x54454300,     // TEC
    /* model name leaf */
    0x0004f420,     // length of leaf, CRC
    0x00000000,
    0x00000000,
    0x4148412d,     // AHA-
    0x38393430      // 8940
};

#endif

