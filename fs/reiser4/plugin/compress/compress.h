#if !defined( __FS_REISER4_COMPRESS_H__ )
#define REISER4_COMPRESS_H

#include <linux/types.h>
#include <linux/string.h>

#define NONE_NRCOPY 4

typedef enum {
	TFM_READ,
	TFM_WRITE
} tfm_action;

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
