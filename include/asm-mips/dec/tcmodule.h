/* 
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Offsets for the ROM header locations for
 * TURBOchannel cards
 *
 * created from:
 *
 * TURBOchannel Firmware Specification
 *
 * EK-TCAAD-FS-004
 * from Digital Equipment Corporation
 *
 * Jan.1998 Harald Koerfgen
 */
 
#define OLDCARD 0x3c0000

#define ROM_WIDTH 0x3e0
#define ROM_STRIDE 0x3e4
#define ROM_SIZE 0x3e8
#define SLOT_SIZE 0x3ec
#define PATTERN0 0x3f0
#define PATTERN1 0x3f4
#define PATTERN2 0x3f8
#define PATTERN3 0x3fc
#define FIRM_VER 0x400
#define VENDOR 0x420
#define MODULE 0x440
#define FIRM_TYPE 0x460
#define FLAGS 0x470

#define ROM_OBJECTS 0x480
