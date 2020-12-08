/* SPDX-License-Identifier: MIT */
#ifndef KCL_KCL_DRM_FOURCC_H
#define KCL_KCL_DRM_FOURCC_H

#include <drm/drm_fourcc.h>

/*
 * Linear Layout
 *
 * Just plain linear layout. Note that this is different from no specifying any
 * modifier (e.g. not setting DRM_MODE_FB_MODIFIERS in the DRM_ADDFB2 ioctl),
 * which tells the driver to also take driver-internal information into account
 * and so might actually result in a tiled framebuffer.
 */
#if !defined(DRM_FORMAT_MOD_VENDOR_NONE)
#define DRM_FORMAT_MOD_VENDOR_NONE    0
#endif

#if !defined(DRM_FORMAT_MOD_LINEAR)
#define DRM_FORMAT_MOD_LINEAR	fourcc_mod_code(NONE, 0)
#endif

#if !defined(DRM_FORMAT_RESERVED)
#define DRM_FORMAT_RESERVED         ((1ULL << 56) - 1)
#endif
/*
 *  * Invalid Modifier
 *   *
 *    * This modifier can be used as a sentinel to terminate the format modifiers
 *     * list, or to initialize a variable with an invalid modifier. It might also be
 *      * used to report an error back to userspace for certain APIs.
 *       */
#if !defined(DRM_FORMAT_MOD_INVALID)
#define DRM_FORMAT_MOD_INVALID fourcc_mod_code(NONE, DRM_FORMAT_RESERVED)
#endif

/*
 * AMD modifiers
 *
 * Memory layout:
 *
 * without DCC:
 *   - main surface
 *
 * with DCC & without DCC_RETILE:
 *   - main surface in plane 0
 *   - DCC surface in plane 1 (RB-aligned, pipe-aligned if DCC_PIPE_ALIGN is set)
 *
 * with DCC & DCC_RETILE:
 *   - main surface in plane 0
 *   - displayable DCC surface in plane 1 (not RB-aligned & not pipe-aligned)
 *   - pipe-aligned DCC surface in plane 2 (RB-aligned & pipe-aligned)
 *
 * For multi-plane formats the above surfaces get merged into one plane for
 * each format plane, based on the required alignment only.
 */
#if !defined(AMD_FMT_MOD)
#define AMD_FMT_MOD fourcc_mod_code(AMD, 0)

#define IS_AMD_FMT_MOD(val) (((val) >> 56) == DRM_FORMAT_MOD_VENDOR_AMD)

/* Reserve 0 for GFX8 and older */
#define AMD_FMT_MOD_TILE_VER_GFX9 1
#define AMD_FMT_MOD_TILE_VER_GFX10 2
#define AMD_FMT_MOD_TILE_VER_GFX10_RBPLUS 3

/*
 * 64K_S is the same for GFX9/GFX10/GFX10_RBPLUS and hence has GFX9 as canonical
 * version.
 */
#define AMD_FMT_MOD_TILE_GFX9_64K_S 9

/*
 * 64K_D for non-32 bpp is the same for GFX9/GFX10/GFX10_RBPLUS and hence has
 * GFX9 as canonical version.
 */
#define AMD_FMT_MOD_TILE_GFX9_64K_D 10
#define AMD_FMT_MOD_TILE_GFX9_64K_S_X 25
#define AMD_FMT_MOD_TILE_GFX9_64K_D_X 26
#define AMD_FMT_MOD_TILE_GFX9_64K_R_X 27

#define AMD_FMT_MOD_DCC_BLOCK_64B 0
#define AMD_FMT_MOD_DCC_BLOCK_128B 1
#define AMD_FMT_MOD_DCC_BLOCK_256B 2

#define AMD_FMT_MOD_TILE_VERSION_SHIFT 0
#define AMD_FMT_MOD_TILE_VERSION_MASK 0xFF
#define AMD_FMT_MOD_TILE_SHIFT 8
#define AMD_FMT_MOD_TILE_MASK 0x1F

/* Whether DCC compression is enabled. */
#define AMD_FMT_MOD_DCC_SHIFT 13
#define AMD_FMT_MOD_DCC_MASK 0x1

/*
 * Whether to include two DCC surfaces, one which is rb & pipe aligned, and
 * one which is not-aligned.
 */
#define AMD_FMT_MOD_DCC_RETILE_SHIFT 14
#define AMD_FMT_MOD_DCC_RETILE_MASK 0x1

/* Only set if DCC_RETILE = false */
#define AMD_FMT_MOD_DCC_PIPE_ALIGN_SHIFT 15
#define AMD_FMT_MOD_DCC_PIPE_ALIGN_MASK 0x1

#define AMD_FMT_MOD_DCC_INDEPENDENT_64B_SHIFT 16
#define AMD_FMT_MOD_DCC_INDEPENDENT_64B_MASK 0x1
#define AMD_FMT_MOD_DCC_INDEPENDENT_128B_SHIFT 17
#define AMD_FMT_MOD_DCC_INDEPENDENT_128B_MASK 0x1
#define AMD_FMT_MOD_DCC_MAX_COMPRESSED_BLOCK_SHIFT 18
#define AMD_FMT_MOD_DCC_MAX_COMPRESSED_BLOCK_MASK 0x1

/*
 * DCC supports embedding some clear colors directly in the DCC surface.
 * However, on older GPUs the rendering HW ignores the embedded clear color
 * and prefers the driver provided color. This necessitates doing a fastclear
 * eliminate operation before a process transfers control.
 *
 * If this bit is set that means the fastclear eliminate is not needed for these
 * embeddable colors.
 */
#define AMD_FMT_MOD_DCC_CONSTANT_ENCODE_SHIFT 19
#define AMD_FMT_MOD_DCC_CONSTANT_ENCODE_MASK 0x1

/*
 * The below fields are for accounting for per GPU differences. These are only
 * relevant for GFX9 and later and if the tile field is *_X/_T.
 *
 * PIPE_XOR_BITS = always needed
 * BANK_XOR_BITS = only for TILE_VER_GFX9
 * PACKERS = only for TILE_VER_GFX10_RBPLUS
 * RB = only for TILE_VER_GFX9 & DCC
 * PIPE = only for TILE_VER_GFX9 & DCC & (DCC_RETILE | DCC_PIPE_ALIGN)
 */
#define AMD_FMT_MOD_PIPE_XOR_BITS_SHIFT 20
#define AMD_FMT_MOD_PIPE_XOR_BITS_MASK 0x7
#define AMD_FMT_MOD_BANK_XOR_BITS_SHIFT 23
#define AMD_FMT_MOD_BANK_XOR_BITS_MASK 0x7
#define AMD_FMT_MOD_PACKERS_SHIFT 26 /* aliases with BANK_XOR_BITS */
#define AMD_FMT_MOD_PACKERS_MASK 0x7
#define AMD_FMT_MOD_RB_SHIFT 29
#define AMD_FMT_MOD_RB_MASK 0x7
#define AMD_FMT_MOD_PIPE_SHIFT 32
#define AMD_FMT_MOD_PIPE_MASK 0x7

#define AMD_FMT_MOD_SET(field, value) \
	((uint64_t)(value) << AMD_FMT_MOD_##field##_SHIFT)
#define AMD_FMT_MOD_GET(field, value) \
	(((value) >> AMD_FMT_MOD_##field##_SHIFT) & AMD_FMT_MOD_##field##_MASK)
#define AMD_FMT_MOD_CLEAR(field) \
	(~((uint64_t)AMD_FMT_MOD_##field##_MASK << AMD_FMT_MOD_##field##_SHIFT))
#endif

#endif /* KCL_KCL_DRM_FOURCC_H */
