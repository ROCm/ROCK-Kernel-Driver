/* zlib_fs.h -- A compatability file mapping the zlib functions to zlib_fs
   functions.   This will go away. */
#ifndef _ZLIB_FS_H
#define _ZLIB_FS_H

#include <linux/zlib.h>

#define zlib_fs_inflate_workspacesize zlib_inflate_workspacesize
#define zlib_fs_deflate_workspacesize zlib_deflate_workspacesize
#define zlib_fs_zlibVersion zlib_zlibVersion
#define zlib_fs_deflate zlib_deflate
#define zlib_fs_deflateEnd zlib_deflateEnd
#define zlib_fs_inflate zlib_inflate
#define zlib_fs_inflateEnd zlib_inflateEnd
#define zlib_fs_deflateSetDictionary zlib_deflateSetDictionary
#define zlib_fs_deflateCopy zlib_deflateCopy
#define zlib_fs_deflateReset zlib_deflateReset
#define zlib_fs_deflateParams zlib_deflateParams
#define zlib_fs_inflateIncomp zlib_inflateIncomp
#define zlib_fs_inflateSetDictionary zlib_inflateSetDictionary
#define zlib_fs_inflateSync zlib_inflateSync
#define zlib_fs_inflateReset zlib_inflateReset
#define zlib_fs_adler32 zlib_adler32
#define zlib_fs_crc32 zlib_crc32
#define zlib_fs_deflateInit(strm, level) \
        zlib_deflateInit_((strm), (level), ZLIB_VERSION, sizeof(z_stream))
#define zlib_fs_inflateInit(strm) \
        zlib_inflateInit_((strm), ZLIB_VERSION, sizeof(z_stream))
#define zlib_fs_deflateInit2(strm, level, method, windowBits, memLevel, strategy)\
        zlib_deflateInit2_((strm),(level),(method),(windowBits),(memLevel),\
		              (strategy), ZLIB_VERSION, sizeof(z_stream))
#define zlib_fs_inflateInit2(strm, windowBits) \
        zlib_inflateInit2_((strm), (windowBits), ZLIB_VERSION, \
                              sizeof(z_stream))

#endif /* _ZLIB_FS_H */
