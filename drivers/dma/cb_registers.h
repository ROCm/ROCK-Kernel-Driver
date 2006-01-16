
#ifndef _CB_REGISTERS_H_
#define _CB_REGISTERS_H_

#define CB_PCI_DMACTRL_OFFSET				0x48
#define CB_PCI_DMACTRL_DMA_EN				0x00000001
#define CB_PCI_DMACTRL_MSI_EN				0x00000002

/* BNB DMA MMIO Device Registers */
#define CB_CHANCNT_OFFSET				0x00	/*  8-bit */

#define CB_XFERCAP_OFFSET				0x01	/*  8-bit */
#define CB_XFERCAP_4KB					12
#define CB_XFERCAP_8KB					13
#define CB_XFERCAP_16KB					14
#define CB_XFERCAP_32KB					15
#define CB_XFERCAP_32GB					0

#define CB_GENCTRL_OFFSET				0x02	/*  8-bit */
#define CB_GENCTRL_DEBUG_EN				0x01

#define CB_INTRCTRL_OFFSET				0x03	/*  8-bit */
#define CB_INTRCTRL_MASTER_INT_EN			0x01	/* Master Interrupt Enable */
#define CB_INTRCTRL_INT_STATUS				0x02	/* ATTNSTATUS -or- Channel Int */
#define CB_INTRCTRL_INT					0x04	/* INT_STATUS -and- MASTER_INT_EN */

#define CB_ATTNSTATUS_OFFSET				0x04	/* Each bit is a channel */

#define CB_VER_OFFSET					0x08	/*  8-bit */
#define CB_VER_MAJOR_MASK				0xF0
#define CB_VER_MINOR_MASK				0x0F
#define GET_CB_VER_MAJOR(x)				((x) & CB_VER_MAJOR_MASK)
#define GET_CB_VER_MINOR(x)				((x) & CB_VER_MINOR_MASK)

#define CB_PERPORTOFFSET_OFFSET				0x0A	/* 16-bit */

#define CB_INTRDELAY_OFFSET				0x0C	/* 16-bit */
#define CB_INTRDELAY_INT_DELAY_MASK			0x3FFF	/* Interrupt Delay Time */
#define CB_INTRDELAY_COALESE_SUPPORT			0x8000	/* Interrupt Coalesing Supported */

#define CB_DEVICE_STATUS_OFFSET				0x0E	/* 16-bit */
#define CB_DEVICE_STATUS_DEGRADED_MODE			0x0001

#define CB_DBG_CHAN_SYSERR_MSK0_OFFSET			0x40
#define CB_DBG_CHAN_SYSERR_MSK1_OFFSET			0x44
#define CB_DBG_CHAN_SYSERR_MSK2_OFFSET			0x48
#define CB_DBG_CHAN_SYSERR_MSK3_OFFSET			0x4C

#define CB_DBG_DCA_REQID2_OFFSET			0x54
#define CB_DBG_DCA_CTRL2_OFFSET				0x56
#define CB_DBG_DCA_REQID3_OFFSET			0x58
#define CB_DBG_DCA_CTRL3_OFFSET				0x5A
#define CB_DBG_DCA_REQID4_OFFSET			0x5C
#define CB_DBG_DCA_CTRL4_OFFSET				0x5E
#define CB_DBG_DCA_REQID5_OFFSET			0x60
#define CB_DBG_DCA_CTRL5_OFFSET				0x62
#define CB_DBG_DCA_REQID6_OFFSET			0x64
#define CB_DBG_DCA_CTRL6_OFFSET				0x66
#define CB_DBG_DCA_REQID7_OFFSET			0x68
#define CB_DBG_DCA_CTRL7_OFFSET				0x6A

#define CB_DBG_DCA_CTRL_IGN_DCA_FN_NUM			0x01
#define CB_DBG_DCA_REQID_INVALID			0x0000


#define CB_CHANNEL_MMIO_SIZE				0x80	/* Each Channel MMIO space is this size */

/* BNB DMA Channel Registers */
#define CB_CHANCTRL_OFFSET				0x00	/* 16-bit Channel Control Register */
#define CB_CHANCTRL_CHANNEL_PRIORITY_MASK		0xF000
#define CB_CHANCTRL_CHANNEL_IN_USE			0x0100
#define CB_CHANCTRL_DESCRIPTOR_ADDR_SNOOP_CONTROL	0x0020
#define CB_CHANCTRL_ERR_INT_EN				0x0010
#define CB_CHANCTRL_ANY_ERR_ABORT_EN			0x0008
#define CB_CHANCTRL_ERR_COMPLETION_EN			0x0004
#define CB_CHANCTRL_INT_DISABLE				0x0001

#define CB_DMA_COMP_OFFSET				0x02	/* 16-bit DMA channel compatability */
#define CB_DMA_COMP_V1					0x0001	/* Compatability with DMA version 1 */

#define CB_CHANSTS_OFFSET				0x04	/* 64-bit Channel Status Register */
#define CB_CHANSTS_OFFSET_LOW				0x04
#define CB_CHANSTS_OFFSET_HIGH				0x08
#define CB_CHANSTS_COMPLETED_DESCRIPTOR_ADDR		0xFFFFFFFFFFFFFFC0
#define CB_CHANSTS_SOFT_ERR				0x0000000000000010
#define CB_CHANSTS_UNAFFILIATED_ERR			0x0000000000000008
#define CB_CHANSTS_DMA_TRANSFER_STATUS			0x0000000000000007
#define CB_CHANSTS_DMA_TRANSFER_STATUS_ACTIVE		0x0
#define CB_CHANSTS_DMA_TRANSFER_STATUS_DONE		0x1
#define CB_CHANSTS_DMA_TRANSFER_STATUS_SUSPENDED	0x2
#define CB_CHANSTS_DMA_TRANSFER_STATUS_HALTED		0x3

#define CB_CHAINADDR_OFFSET				0x0C	/* 64-bit Descriptor Chain Address Register */
#define CB_CHAINADDR_OFFSET_LOW				0x0C
#define CB_CHAINADDR_OFFSET_HIGH			0x10

#define CB_CHANCMD_OFFSET				0x14	/*  8-bit DMA Channel Command Register */
#define CB_CHANCMD_RESET				0x20
#define CB_CHANCMD_RESUME				0x10
#define CB_CHANCMD_ABORT				0x08
#define CB_CHANCMD_SUSPEND				0x04
#define CB_CHANCMD_APPEND				0x02
#define CB_CHANCMD_START				0x01

#define CB_CHANCMP_OFFSET				0x18	/* 64-bit Channel Completion Address Register */
#define CB_CHANCMP_OFFSET_LOW				0x18
#define CB_CHANCMP_OFFSET_HIGH				0x1C

#define CB_CDAR_OFFSET					0x20	/* 64-bit Current Descriptor Address Register */
#define CB_CDAR_OFFSET_LOW				0x20
#define CB_CDAR_OFFSET_HIGH				0x24

#define CB_CHANERR_OFFSET				0x28	/* 32-bit Channel Error Register */
#define CB_CHANERR_DMA_TRANSFER_SRC_ADDR_ERR		0x0001
#define CB_CHANERR_DMA_TRANSFER_DEST_ADDR_ERR		0x0002
#define CB_CHANERR_NEXT_DESCRIPTOR_ADDR_ERR		0x0004
#define CB_CHANERR_NEXT_DESCRIPTOR_ALIGNMENT_ERR	0x0008
#define CB_CHANERR_CHAIN_ADDR_VALUE_ERR			0x0010
#define CB_CHANERR_CHANCMD_ERR				0x0020
#define CB_CHANERR_CHIPSET_UNCORRECTABLE_DATA_INTEGRITY_ERR	0x0040
#define CB_CHANERR_DMA_UNCORRECTABLE_DATA_INTEGRITY_ERR	0x0080
#define CB_CHANERR_READ_DATA_ERR			0x0100
#define CB_CHANERR_WRITE_DATA_ERR			0x0200
#define CB_CHANERR_DESCRIPTOR_CONTROL_ERR		0x0400
#define CB_CHANERR_DESCRIPTOR_LENGTH_ERR		0x0800
#define CB_CHANERR_COMPLETION_ADDR_ERR			0x1000
#define CB_CHANERR_INT_CONFIGURATION_ERR		0x2000
#define CB_CHANERR_SOFT_ERR				0x4000
#define CB_CHANERR_UNAFFILIATED_ERR			0x8000

#define CB_CHANERR_MASK_OFFSET				0x2C	/* 32-bit Channel Error Register */

/* Per-PCI Express Port Registers */
#define CB_NXTPPRSET_OFFSET				0x00	/* 16-bit Next Per-Port Register Set */

#define CB_PPRSETLEN_OFFSET				0x02	/* 8-bit Per-Port Register Set Length */

#define CB_STRMPRI_OFFSET				0x03	/* 8-bit Stream Priority Register */
#define CB_STRMPRI_HIGHEST_PRIORITY_SUPPORTED		0xF0
#define CB_STRMPRI_DEFAULT_STREAM_PRIORITY		0x0F

#define CB_REQID_OFFSET					0x04	/* 16-bit Requester ID Register */
#define CB_REQID_BUS					0xFF00
#define CB_REQID_DEVICE					0x00F8
#define CB_REQID_FUNCTION				0x0007

#define CB_STRMIDFMT_OFFSET				0x06	/* 8-bit Stream ID Format */
#define CB_STRMIFFMT_IN_USE				0x80
#define CB_STRMIFFMT_IGNORE_FUNCTION			0x10
#define CB_STRMIFFMT_FUNCTION_FIELD_SIZE		0x0C
#define CB_STRMIFFMT_TAG_FIELD_SIZE			0x03

#define CB_STRMCAP_OFFSET				0x07	/* 8-bit Stream Capacity */

#define CB_WCNUM_OFFSET					0x08	/* 8-bit Write Combining Number */

#define CB_WCSIZE_OFFSET				0x09	/* 8-bit Write Combining Size */
#define CB_WCSIZE_1KB					1
#define CB_WCSIZE_2KB					2
#define CB_WCSIZE_4KB					3
#define CB_WCSIZE_8KB					4

#define CB_WCCTRL_OFFSET				0x0A	/* 16-bit Write Combining Control */
#define CB_WCCTRL_IN_USE				0x0100
#define CB_WCCTRL_WINDOW_CLEAR				0x0002
#define CB_WCCTRL_WINDOW_FLUSH				0x0001

#define CB_WCCAP_OFFSET					0x0C	/* 8-bit Write Combining Capacity */

#define CB_WCWINDOW_OFFSET				0x0D	/* 8-bit Write Combining Window Size */
#define CB_WCWINDOW_64B					1
#define CB_WCWINDOW_128B				2
#define CB_WCWINDOW_256B				4

#define CB_BRIDGE_ID_OFFSET				0x0E	/* 16-bit Bridge ID */
#define CB_BRIDGE_ID_BUS				0xFF00
#define CB_BRIDGE_ID_DEVICE				0x00F8
#define CB_BRIDGE_ID_FUNCTION				0x0007

#define CB_WCBASE_OFFSET				0x10	/* 64-bit Write Combining Base Address */
#define CB_WCBASE_OFFSET_LOW				0x10
#define CB_WCBASE_OFFSET_HIGH				0x14

#define CB_STRMMAP_OFFSET_OFFSET			0x18	/* 16-bit Stream Priority Mapping Offset */
/* At CB_BAR + CB_STRMMAP_OFFSET is the STRMMAP array of 4-bit Stream Priority Values */

#define CB_PORTPRI_OFFSET				0x1A	/* 8-bit Port Priority */

#define CB_STRM_COMP_OFFSET				0x1C	/* 16-bit Stream Priority Compatability */
#define CB_STRM_COMP_V1					0x0001
#define CB_WC_COMP_OFFSET				0x1E	/* 16-bit Write Combining Compatability */
#define CB_WC_COMP_V1					0x0001

#define CB_BR_MEM_BASE_OFFSET				0x20	/* 16-bit Bridge Memory Base */
#define CB_BR_MEM_LIMIT_OFFSET				0x22	/* 16-bit Bridge Memory Limit */
#define CB_BR_PMEM_BASE_OFFSET				0x24	/* 16-bit Bridge Prefetchable Memory Base */
#define CB_BR_PMEM_LIMIT_OFFSET				0x26	/* 16-bit Bridge Prefetchable Memory Limit */
#define CB_BR_PBASE_UPPER32_OFFSET			0x28	/* 32-bit Bridge Prefetchable Base Upper 32 */
#define CB_BR_PLIMIT_UPPER32_OFFSET			0x2C	/* 32-bit Bridge Prefetchable Limit Upper 32 */

#define CB_DOORBELL_SNOOP_ADDR0_OFFSET			0x40	/* 64-bit Doorbell Snoop Address for WC Region 0 */
#define CB_DOORBELL_SNOOP_ADDR0_OFFSET_LOW		0x40
#define CB_DOORBELL_SNOOP_ADDR0_OFFSET_HIGH		0x44

#endif /* _CB_REGISTERS_H_ */
