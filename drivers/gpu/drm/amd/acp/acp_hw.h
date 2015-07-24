#ifndef __ACP_HW_H
#define __ACP_HW_H

#define ACP_MODE_I2S				0
#define ACP_MODE_AZ				1

#define DISABLE					0
#define ENABLE					1

#define PAGE_SIZE_4K				4096
#define PAGE_SIZE_4K_ENABLE			0x02

#define PLAYBACK_PTE_OFFSET			10
#define CAPTURE_PTE_OFFSET			0

#define GARLIC_CNTL_DEFAULT			0x00000FB4
#define ONION_CNTL_DEFAULT			0x00000FB4

#define ACP_PHYSICAL_BASE			0x14000

/* Playback SRAM address (as a destination in dma descriptor) */
#define ACP_SHARED_RAM_BANK_1_ADDRESS		0x4002000

/* Capture SRAM address (as a source in dma descriptor) */
#define ACP_SHARED_RAM_BANK_5_ADDRESS		0x400A000

#define ACP_DMA_RESET_TIME			10000
#define ACP_CLOCK_EN_TIME_OUT_VALUE		0x000000FF
#define ACP_SOFT_RESET_DONE_TIME_OUT_VALUE	0x000000FF
#define ACP_DMA_COMPLETE_TIME_OUT_VALUE		0x000000FF

#define ACP_SRAM_BASE_ADDRESS			0x4000000
#define ACP_DAGB_GRP_SRAM_BASE_ADDRESS		0x4001000
#define ACP_DAGB_GRP_SRBM_SRAM_BASE_OFFSET	0x1000
#define ACP_INTERNAL_APERTURE_WINDOW_0_ADDRESS	0x00000000
#define ACP_INTERNAL_APERTURE_WINDOW_4_ADDRESS	0x01800000

#define TO_ACP_I2S_1   0x2
#define TO_ACP_I2S_2   0x4
#define FROM_ACP_I2S_1 0xa
#define FROM_ACP_I2S_2 0xb

#define ACP_TILE_ON_MASK                0x03
#define ACP_TILE_OFF_MASK               0x02
#define ACP_TILE_ON_RETAIN_REG_MASK     0x1f
#define ACP_TILE_OFF_RETAIN_REG_MASK    0x20

#define ACP_TILE_P1_MASK                0x3e
#define ACP_TILE_P2_MASK                0x3d
#define ACP_TILE_DSP0_MASK              0x3b
#define ACP_TILE_DSP1_MASK              0x37
#define ACP_TILE_DSP2_MASK              0x2f

enum {
	ACP_TILE_P1 = 0,
	ACP_TILE_P2,
	ACP_TILE_DSP0,
	ACP_TILE_DSP1,
	ACP_TILE_DSP2,
};

enum {
	STREAM_PLAYBACK = 0,
	STREAM_CAPTURE,
	STREAM_LAST = STREAM_CAPTURE,
};

enum {
	ACP_DMA_ATTRIBUTES_SHAREDMEM_TO_DAGB_ONION = 0x0,
	ACP_DMA_ATTRIBUTES_SHARED_MEM_TO_DAGB_GARLIC = 0x1,
	ACP_DMA_ATTRIBUTES_DAGB_ONION_TO_SHAREDMEM = 0x8,
	ACP_DMA_ATTRIBUTES_DAGB_GARLIC_TO_SHAREDMEM = 0x9,
	ACP_DMA_ATTRIBUTES_FORCE_SIZE = 0xF
};

typedef struct acp_dma_dscr_transfer {
	/* Specifies the source memory location for the DMA data transfer. */
	u32 src;
	/* Specifies the destination memory location to where the data will
	   be transferred.
	 */
	u32 dest;
	/* Specifies the number of bytes need to be transferred
	 * from source to destination memory.Transfer direction & IOC enable
	 */
	u32 xfer_val;
	/** Reserved for future use */
	u32 reserved;
} acp_dma_dscr_transfer_t;

#endif /*__ACP_HW_H */
