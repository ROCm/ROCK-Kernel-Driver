/*
 *  support.h
 *  Header file for specific support.c
 *
 *  Copyright (C) 1997 Régis Duchesne
 */

/* Debug levels */
#define DEBUG_OTHER	1
#define DEBUG_MALLOC	2
#define DEBUG_BSD       4
#define DEBUG_LINUX     8
#define DEBUG_DIR1     16
#define DEBUG_DIR2     32
#define DEBUG_DIR3     64
#define DEBUG_FILE1   128
#define DEBUG_FILE2   256
#define DEBUG_FILE3   512
#define DEBUG_NAME1  1024
#define DEBUG_NAME2  2048

void ntfs_debug(int mask, const char *fmt, ...);
#ifdef NTFS_IN_LINUX_KERNEL
#include <linux/slab.h>
#define ntfs_malloc(size)  kmalloc(size,GFP_KERNEL)
#define ntfs_free(ptr)     kfree(ptr)
#else
void *ntfs_malloc(int size);
void ntfs_free(void *block);
#endif
void ntfs_bzero(void *s, int n);
void ntfs_memcpy(void *dest, const void *src, ntfs_size_t n);
void ntfs_memmove(void *dest, const void *src, ntfs_size_t n);
void ntfs_error(const char *fmt,...);
int ntfs_read_mft_record(ntfs_volume *vol, int mftno, char *buf);
int ntfs_getput_clusters(ntfs_volume *pvol, int cluster, ntfs_size_t offs,
        ntfs_io *buf);
ntfs_time64_t ntfs_now(void);
int ntfs_dupuni2map(ntfs_volume *vol, ntfs_u16 *in, int in_len, char **out,
        int *out_len);
int ntfs_dupmap2uni(ntfs_volume *vol, char* in, int in_len, ntfs_u16 **out,
        int *out_len);

/*
 * Local variables:
 * c-file-style: "linux"
 * End:
 */
