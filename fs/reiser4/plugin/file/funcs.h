/* Copyright 2001, 2002, 2003, 2004 by Hans Reiser, licensing governed by reiser4/README */

/* this prototyles functions used by both file.c and tail_conversion.c */
void get_exclusive_access(unix_file_info_t *);
void drop_exclusive_access(unix_file_info_t *);
void get_nonexclusive_access(unix_file_info_t *);
void drop_nonexclusive_access(unix_file_info_t *);
void drop_access(unix_file_info_t *uf_info);

int tail2extent(unix_file_info_t *);
int extent2tail(unix_file_info_t *);
int finish_conversion(struct inode *inode);

void hint_init_zero(hint_t *, lock_handle *);
int find_file_item(hint_t *, const reiser4_key *, znode_lock_mode,
		   ra_info_t *, struct inode *);
int find_file_item_nohint(coord_t *, lock_handle *, const reiser4_key *,
			  znode_lock_mode, struct inode *);

int goto_right_neighbor(coord_t *, lock_handle *);
int find_or_create_extent(struct page *);
write_mode_t how_to_write(uf_coord_t *, const reiser4_key *);

extern inline int
cbk_errored(int cbk_result)
{
	return (cbk_result != CBK_COORD_NOTFOUND && cbk_result != CBK_COORD_FOUND);
}
