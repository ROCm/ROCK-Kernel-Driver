#define STI_REGION_MAX 8
#define STI_DEV_NAME_LENGTH 32

typedef struct {
	 u8 res[3];
	 u8 data;
} __attribute__((packed)) sti_u8;

typedef struct {
	sti_u8 data[2];
} __attribute__((packed)) sti_u16;

typedef struct {
	sti_u8 data[4];
} __attribute__((packed)) sti_u32;

#define  STI_U8( u8) ((u8).data)
#define STI_U16(u16) ((STI_U8((u16).data[0])<<8) | STI_U8((u16).data[1]))
#define STI_U32(u32) ((STI_U8((u32).data[0])<<24) | \
		      (STI_U8((u32).data[1])<<16) | \
		      (STI_U8((u32).data[2])<< 8) | \
		      (STI_U8((u32).data[3])<< 0))

struct sti_rom_region {
	sti_u32 region;
};

struct sti_rom_font {
	sti_u16 first_char;
	sti_u16 last_char;
	 sti_u8 width;
	 sti_u8 height;
	 sti_u8 font_type;
	 sti_u8 bytes_per_char;
	sti_u32 next_font;
	 sti_u8 underline_height;
	 sti_u8 underline_pos;
	 sti_u8 res008[2];
};

struct sti_rom {
	 sti_u8 type;
	 sti_u8 num_mons;
	 sti_u8 revno[2];

	 sti_u8 graphics_id[8];			/* 0x010 */

	sti_u32 font_start;			/* 0x030 */
	sti_u32 statesize;
	sti_u32 last_addr;
	sti_u32 region_list;

	sti_u16 reentsize;			/* 0x070 */
	sti_u16 maxtime;
	sti_u32 mon_tbl_addr;
	sti_u32 user_data_addr;
	sti_u32 sti_mem_req;

	sti_u32 user_data_size;			/* 0x0b0 */
	sti_u16 power;				/* 0x0c0 */
	 sti_u8 bus_support;
	 sti_u8 ext_bus_support;
	 sti_u8 alt_code_type;			/* 0x0d0 */
	 sti_u8 ext_dd_struct[3];
	sti_u32 cfb_addr;			/* 0x0e0 */
	
	 sti_u8 res0f0[4];			

	sti_u32 init_graph;		/* 0x0e0 */
	sti_u32 state_mgmt;
	sti_u32 font_unpmv;
	sti_u32 block_move;
	sti_u32 self_test;
	sti_u32 excep_hdlr;
	sti_u32 inq_conf;
	sti_u32 set_cm_entry;
	sti_u32 dma_ctrl;
	sti_u32 flow_ctrl;
	sti_u32 user_timing;
	sti_u32 process_mgr;
	sti_u32 sti_util;
	sti_u32 end_addr;
	sti_u32 res0b8;
	sti_u32 res0bc;

	sti_u32 init_graph_m68k;		/* 0x0e0 */
	sti_u32 state_mgmt_m68k;
	sti_u32 font_unpmv_m68k;
	sti_u32 block_move_m68k;
	sti_u32 self_test_m68k;
	sti_u32 excep_hdlr_m68k;
	sti_u32 inq_conf_m68k;
	sti_u32 set_cm_entry_m68k;
	sti_u32 dma_ctrl_m68k;
	sti_u32 flow_ctrl_m68k;
	sti_u32 user_timing_m68k;
	sti_u32 process_mgr_m68k;
	sti_u32 sti_util_m68k;
	sti_u32 end_addr_m68k;
	sti_u32 res0b8_m68k;
	sti_u32 res0bc_m68k;

	 sti_u8 res040[7 * 4];
};
	
struct sti_cooked_font {
	struct sti_rom_font *raw;
	struct sti_cooked_font *next_font;
};

struct sti_cooked_rom {
	struct sti_rom *raw;
	struct sti_cooked_font *font_start;
	u32 *region_list;
};

struct sti_glob_cfg_ext {
	 u8 curr_mon;
	 u8 friendly_boot;
	s16 power;
	s32 freq_ref;
	s32 *sti_mem_addr;
	s32 *future_ptr;
};

struct sti_glob_cfg {
	s32 text_planes;
	s16 onscreen_x;
	s16 onscreen_y;
	s16 offscreen_x;
	s16 offscreen_y;
	s16 total_x;
	s16 total_y;
	u32 region_ptrs[STI_REGION_MAX];
	s32 reent_lvl;
	s32 *save_addr;
	struct sti_glob_cfg_ext *ext_ptr;
};

struct sti_init_flags {
	u32 wait : 1;
	u32 reset : 1;
	u32 text : 1;
	u32 nontext : 1;
	u32 clear : 1;
	u32 cmap_blk : 1;
	u32 enable_be_timer : 1;
	u32 enable_be_int : 1;
	u32 no_chg_tx : 1;
	u32 no_chg_ntx : 1;
	u32 no_chg_bet : 1;
	u32 no_chg_bei : 1;
	u32 init_cmap_tx : 1;
	u32 cmt_chg : 1;
	u32 retain_ie : 1;
	u32 pad : 17;

	s32 *future_ptr;
};

struct sti_init_inptr_ext {
	u8  config_mon_type;
	u8  pad[1];
	u16 inflight_data;
	s32 *future_ptr;
};

struct sti_init_inptr {
	s32 text_planes;
	struct sti_init_inptr_ext *ext_ptr;
};

struct sti_init_outptr {
	s32 errno;
	s32 text_planes;
	s32 *future_ptr;
};

struct sti_conf_flags {
	u32 wait : 1;
	u32 pad : 31;
	s32 *future_ptr;
};

struct sti_conf_inptr {
	s32 *future_ptr;
};

struct sti_conf_outptr_ext {
	u32 crt_config[3];
	u32 crt_hdw[3];
	s32 *future_ptr;
};

struct sti_conf_outptr {
	s32 errno;
	s16 onscreen_x;
	s16 onscreen_y;
	s16 offscreen_x;
	s16 offscreen_y;
	s16 total_x;
	s16 total_y;
	s32 bits_per_pixel;
	s32 bits_used;
	s32 planes;
	 u8 dev_name[STI_DEV_NAME_LENGTH];
	u32 attributes;
	struct sti_conf_outptr_ext *ext_ptr;
};


struct sti_font_inptr {
	u32 font_start_addr;
	s16 index;
	u8 fg_color;
	u8 bg_color;
	s16 dest_x;
	s16 dest_y;
	s32 *future_ptr;
};

struct sti_font_flags {
	u32 wait : 1;
	u32 non_text : 1;
	u32 pad : 30;

	s32 *future_ptr;
};
	
struct sti_font_outptr {
	s32 errno;
	s32 *future_ptr;
};

struct sti_blkmv_flags {
	u32 wait : 1;
	u32 color : 1;
	u32 clear : 1;
	u32 non_text : 1;
	u32 pad : 28;
	s32 *future_ptr;
};

struct sti_blkmv_inptr {
	u8 fg_color;
	u8 bg_color;
	s16 src_x;
	s16 src_y;
	s16 dest_x;
	s16 dest_y;
	s16 width;
	s16 height;
	s32 *future_ptr;
};

struct sti_blkmv_outptr {
	s32 errno;
	s32 *future_ptr;
};

struct sti_struct {
	spinlock_t lock;

	struct sti_cooked_rom *rom;

	unsigned long font_unpmv;
	unsigned long block_move;
	unsigned long init_graph;
	unsigned long inq_conf;

	struct sti_glob_cfg *glob_cfg;
	struct sti_rom_font *font;

	s32 text_planes;

	char **mon_strings;
	u32 *regions;
	 u8 *pci_regions;
};

#define STI_CALL(func, flags, inptr, outptr, glob_cfg) \
	({	 						\
		real32_call( func, (unsigned long)STI_PTR(flags), \
				    STI_PTR(inptr), STI_PTR(outptr), \
				    glob_cfg); \
	})

