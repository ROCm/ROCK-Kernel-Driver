/*
 *  util.h
 *  Header file for util.c
 *
 *  Copyright (C) 1997 Régis Duchesne
 */

/* Which character set is used for file names */
/*  Translate everything to UTF-8 */
#define nct_utf8             1
/*  Translate to 8859-1 */
#define nct_iso8859_1        2
/*  Quote unprintables with : */
#define nct_uni_xlate        4
/*  Do that in the vfat way instead of the documented way */
#define nct_uni_xlate_vfat   8
/*  Use a mapping table to determine printables */
#define nct_map              16

/* The first 11 inodes correspond to special files */
#define FILE_MFT      0
#define FILE_MFTMIRR  1
#define FILE_LOGFILE  2
#define FILE_VOLUME   3
#define FILE_ATTRDEF  4
#define FILE_ROOT     5
#define FILE_BITMAP   6
#define FILE_BOOT     7
#define FILE_BADCLUS  8
#define FILE_QUOTA    9
#define FILE_UPCASE  10

/* Memory management */
void *ntfs_calloc(int size);

/* String operations */
/*  Copy Unicode <-> ASCII */
#if 0
void ntfs_uni2ascii(char *to,char *from,int len);
#endif
void ntfs_ascii2uni(short int *to,char *from,int len);
/*  Comparison */
int ntfs_uni_strncmp(short int* a,short int *b,int n);
int ntfs_ua_strncmp(short int* a,char* b,int n);

/* Same address space copies */
void ntfs_put(ntfs_io *dest, void *src, ntfs_size_t n);
void ntfs_get(void* dest, ntfs_io *src, ntfs_size_t n);

/* Charset conversion */
int ntfs_encodeuni(ntfs_volume *vol,ntfs_u16 *in, int in_len,char **out, int *out_len);
int ntfs_decodeuni(ntfs_volume *vol,char *in, int in_len, ntfs_u16 **out, int *out_len);

/* Time conversion */
/*  NT <-> Unix */
ntfs_time_t ntfs_ntutc2unixutc(ntfs_time64_t ntutc);
ntfs_time64_t ntfs_unixutc2ntutc(ntfs_time_t t);

/* Attribute names */
void ntfs_indexname(char *buf, int type);

/*
 * Local variables:
 * c-file-style: "linux"
 * End:
 */
