/* Number of bytes to readahead on disc access */
#define FAT_READAHEAD (18*1024)

struct buffer_head *fat_bread (struct super_block *sb, int block);
struct buffer_head *fat_getblk (struct super_block *sb, int block);
void fat_brelse (struct super_block *sb, struct buffer_head *bh);
void fat_mark_buffer_dirty (struct super_block *sb,
	 struct buffer_head *bh);
void fat_set_uptodate (struct super_block *sb,
	 struct buffer_head *bh,
	 int val);
int fat_is_uptodate (struct super_block *sb, struct buffer_head *bh);
void fat_ll_rw_block (struct super_block *sb, int opr,
	int nbreq, struct buffer_head *bh[32]);
