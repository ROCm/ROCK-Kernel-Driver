#ifndef _ISO_FS_I
#define _ISO_FS_I

/*
 * iso fs inode data in memory
 */
struct iso_inode_info {
	unsigned int i_first_extent;
	unsigned char i_file_format;
	unsigned long i_next_section_ino;
	off_t i_section_size;
};

#endif
