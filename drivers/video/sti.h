#define STI_REGION_MAX 8
#define STI_DEV_NAME_LENGTH 32

struct sti_rom_font {
	u16 first_char;
	u16 last_char;
	 u8 width;
	 u8 height;
	 u8 font_type;
	 u8 bytes_per_char;
	u32 next_font;
	 u8 underline_height;
	 u8 underline_pos;
	 u8 res008[2];
};

struct sti_rom {
	 u8 type[4];
	 u8 res004;
	 u8 num_mons;
	 u8 revno[2];
	 u8 graphics_id[8];

	u32 font_start;
	u32 statesize;
	u32 last_addr;
	u32 region_list;

	u16 reentsize;
	u16 maxtime;
	u32 mon_tbl_addr;
	u32 user_data_addr;
	u32 sti_mem_req;

	u32 user_data_size;
	u16 power;
	 u8 bus_support;
	 u8 ext_bus_support;
	 u8 alt_code_type;
	 u8 ext_dd_struct[3];
	u32 cfb_addr;
	
	u32 init_graph;
	u32 state_mgmt;
	u32 font_unpmv;
	u32 block_move;
	u32 self_test;
	u32 excep_hdlr;
	u32 inq_conf;
	u32 set_cm_entry;
	u32 dma_ctrl;
	 u8 res040[7 * 4];
	
	u32 init_graph_m68k;
	u32 flow_ctrl;
	u32 user_timing;
	u32 process_mgr;
	u32 sti_util;
	u32 end_addr;
	u32 res0b8;
	u32 res0bc;
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
	u8  curr_mon;
	u8  friendly_boot;
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
	({							\
		real32_call( func, (unsigned long)STI_PTR(flags), \
				    STI_PTR(inptr), STI_PTR(outptr), \
				    glob_cfg); \
	})

/* The latency of the STI functions cannot really be reduced by setting
 * this to 0;  STI doesn't seem to be designed to allow calling a different
 * function (or the same function with different arguments) after a
 * function exited with 1 as return value.
 *
 * As all of the functions below could be called from interrupt context,
 * we have to spin_lock_irqsave around the do { ret = bla(); } while(ret==1)
 * block.  Really bad latency there.
 *
 * Probably the best solution to all this is have the generic code manage
 * the screen buffer and a kernel thread to call STI occasionally.
 * 
 * Luckily, the frame buffer guys have the same problem so we can just wait
 * for them to fix it and steal their solution.   prumpf
 *
 * Actually, another long-term viable solution is to completely do STI
 * support in userspace - that way we avoid the potential license issues
 * of using proprietary fonts, too. */
 
#define STI_WAIT 1
#define STI_PTR(p) ( (typeof(p)) virt_to_phys(p))
#define PTR_STI(p) ( (typeof(p)) phys_to_virt((unsigned long)p) )

#define sti_onscreen_x(sti) (PTR_STI(sti->glob_cfg)->onscreen_x)
#define sti_onscreen_y(sti) (PTR_STI(sti->glob_cfg)->onscreen_y)
#define sti_font_x(sti) (PTR_STI(sti->font)->width)
#define sti_font_y(sti) (PTR_STI(sti->font)->height)

extern struct sti_struct * sti_init_roms(void);

void sti_init_graph(struct sti_struct *sti);
void sti_inq_conf(struct sti_struct *sti);
void sti_putc(struct sti_struct *sti, int c, int y, int x);
void sti_set(struct sti_struct *sti, int src_y, int src_x,
	     int height, int width, u8 color);
void sti_clear(struct sti_struct *sti, int src_y, int src_x,
	       int height, int width);
void sti_bmove(struct sti_struct *sti, int src_y, int src_x,
	       int dst_y, int dst_x, int height, int width);

/* XXX: this probably should not be here, but we rely on STI being
   initialized early and independently of stifb at the moment, so
   there's no other way for stifb to find it. */
extern struct sti_struct default_sti;
