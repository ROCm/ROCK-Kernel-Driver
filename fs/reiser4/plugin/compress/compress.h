#if !defined( __FS_REISER4_COMPRESS_H__ )
#define __FS_REISER4_COMPRESS_H__

#include <linux/types.h>
#include <linux/string.h>

typedef enum {
	TFM_READ,
	TFM_WRITE
} tfm_action;

/* builtin compression plugins */

typedef enum {
	LZO1_COMPRESSION_ID,
	LZO1_NO_COMPRESSION_ID,
	GZIP1_COMPRESSION_ID,
	GZIP1_NO_COMPRESSION_ID,
	NONE_COMPRESSION_ID,
	LAST_COMPRESSION_ID,
} reiser4_compression_id;

typedef unsigned long cloff_t;
typedef void * coa_t;
typedef coa_t coa_set[LAST_COMPRESSION_ID];

__u32 reiser4_adler32(char * data, __u32 len);

#endif /* __FS_REISER4_COMPRESS_H__ */

/* Make Linus happy.
   Local variables:
   c-indentation-style: "K&R"
   mode-name: "LC"
   c-basic-offset: 8
   tab-width: 8
   fill-column: 120
   scroll-step: 1
   End:
*/
