/*
 * include/asm-ia64/xor.h
 *
 * Optimized RAID-5 checksumming functions for IA-64.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * You should have received a copy of the GNU General Public License
 * (for example /usr/src/linux/COPYING); if not, write to the Free
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */


extern void xor_ia64_2(unsigned long, unsigned long *, unsigned long *);
extern void xor_ia64_3(unsigned long, unsigned long *, unsigned long *,
		       unsigned long *);
extern void xor_ia64_4(unsigned long, unsigned long *, unsigned long *,
		       unsigned long *, unsigned long *);
extern void xor_ia64_5(unsigned long, unsigned long *, unsigned long *,
		       unsigned long *, unsigned long *, unsigned long *);

asm ("\n\
	.text\n\
\n\
	// Assume L2 memory latency of 6 cycles.\n\
\n\
	.proc xor_ia64_2\n\
xor_ia64_2:\n\
	.prologue\n\
	.fframe 0\n\
	{ .mii\n\
	  .save ar.pfs, r31\n\
	  alloc r31 = ar.pfs, 3, 0, 13, 16\n\
	  .save ar.lc, r30\n\
	  mov r30 = ar.lc\n\
	  .save pr, r29\n\
	  mov r29 = pr\n\
	  ;;\n\
	}\n\
	.body\n\
	{ .mii\n\
	  mov r8 = in1\n\
	  mov ar.ec = 6 + 2\n\
	  shr in0 = in0, 3\n\
	  ;;\n\
	}\n\
	{ .mmi\n\
	  adds in0 = -1, in0\n\
	  mov r16 = in1\n\
	  mov r17 = in2\n\
	  ;;\n\
	}\n\
	{ .mii\n\
	  mov ar.lc = in0\n\
	  mov pr.rot = 1 << 16\n\
	  ;;\n\
	}\n\
	.rotr s1[6+1], s2[6+1], d[2]\n\
	.rotp p[6+2]\n\
0:	 { .mmi\n\
(p[0])	  ld8.nta s1[0] = [r16], 8\n\
(p[0])	  ld8.nta s2[0] = [r17], 8\n\
(p[6])	  xor d[0] = s1[6], s2[6]\n\
	}\n\
	{ .mfb\n\
(p[6+1])  st8.nta [r8] = d[1], 8\n\
	  nop.f 0\n\
	  br.ctop.dptk.few 0b\n\
	  ;;\n\
	}\n\
	{ .mii\n\
	  mov ar.lc = r30\n\
	  mov pr = r29, -1\n\
	}\n\
	{ .bbb\n\
	  br.ret.sptk.few rp\n\
	}\n\
	.endp xor_ia64_2\n\
\n\
	.proc xor_ia64_3\n\
xor_ia64_3:\n\
	.prologue\n\
	.fframe 0\n\
	{ .mii\n\
	  .save ar.pfs, r31\n\
	  alloc r31 = ar.pfs, 4, 0, 20, 24\n\
	  .save ar.lc, r30\n\
	  mov r30 = ar.lc\n\
	  .save pr, r29\n\
	  mov r29 = pr\n\
	  ;;\n\
	}\n\
	.body\n\
	{ .mii\n\
	  mov r8 = in1\n\
	  mov ar.ec = 6 + 2\n\
	  shr in0 = in0, 3\n\
	  ;;\n\
	}\n\
	{ .mmi\n\
	  adds in0 = -1, in0\n\
	  mov r16 = in1\n\
	  mov r17 = in2\n\
	  ;;\n\
	}\n\
	{ .mii\n\
	  mov r18 = in3\n\
	  mov ar.lc = in0\n\
	  mov pr.rot = 1 << 16\n\
	  ;;\n\
	}\n\
	.rotr s1[6+1], s2[6+1], s3[6+1], d[2]\n\
	.rotp p[6+2]\n\
0:	{ .mmi\n\
(p[0])	  ld8.nta s1[0] = [r16], 8\n\
(p[0])	  ld8.nta s2[0] = [r17], 8\n\
(p[6])	  xor d[0] = s1[6], s2[6]\n\
	  ;;\n\
	}\n\
	{ .mmi\n\
(p[0])	  ld8.nta s3[0] = [r18], 8\n\
(p[6+1])  st8.nta [r8] = d[1], 8\n\
(p[6])	  xor d[0] = d[0], s3[6]\n\
	}\n\
	{ .bbb\n\
	  br.ctop.dptk.few 0b\n\
	  ;;\n\
	}\n\
	{ .mii\n\
	  mov ar.lc = r30\n\
	  mov pr = r29, -1\n\
	}\n\
	{ .bbb\n\
	  br.ret.sptk.few rp\n\
	}\n\
	.endp xor_ia64_3\n\
\n\
	.proc xor_ia64_4\n\
xor_ia64_4:\n\
	.prologue\n\
	.fframe 0\n\
	{ .mii\n\
	  .save ar.pfs, r31\n\
	  alloc r31 = ar.pfs, 5, 0, 27, 32\n\
	  .save ar.lc, r30\n\
	  mov r30 = ar.lc\n\
	  .save pr, r29\n\
	  mov r29 = pr\n\
	  ;;\n\
	}\n\
	.body\n\
	{ .mii\n\
	  mov r8 = in1\n\
	  mov ar.ec = 6 + 2\n\
	  shr in0 = in0, 3\n\
	  ;;\n\
	}\n\
	{ .mmi\n\
	  adds in0 = -1, in0\n\
	  mov r16 = in1\n\
	  mov r17 = in2\n\
	  ;;\n\
	}\n\
	{ .mii\n\
	  mov r18 = in3\n\
	  mov ar.lc = in0\n\
	  mov pr.rot = 1 << 16\n\
	}\n\
	{ .mfb\n\
	  mov r19 = in4\n\
	  ;;\n\
	}\n\
	.rotr s1[6+1], s2[6+1], s3[6+1], s4[6+1], d[2]\n\
	.rotp p[6+2]\n\
0:	{ .mmi\n\
(p[0])	  ld8.nta s1[0] = [r16], 8\n\
(p[0])	  ld8.nta s2[0] = [r17], 8\n\
(p[6])	  xor d[0] = s1[6], s2[6]\n\
	}\n\
	{ .mmi\n\
(p[0])	  ld8.nta s3[0] = [r18], 8\n\
(p[0])	  ld8.nta s4[0] = [r19], 8\n\
(p[6])	  xor r20 = s3[6], s4[6]\n\
	  ;;\n\
	}\n\
	{ .mib\n\
(p[6+1])  st8.nta [r8] = d[1], 8\n\
(p[6])	  xor d[0] = d[0], r20\n\
	  br.ctop.dptk.few 0b\n\
	  ;;\n\
	}\n\
	{ .mii\n\
	  mov ar.lc = r30\n\
	  mov pr = r29, -1\n\
	}\n\
	{ .bbb\n\
	  br.ret.sptk.few rp\n\
	}\n\
	.endp xor_ia64_4\n\
\n\
	.proc xor_ia64_5\n\
xor_ia64_5:\n\
	.prologue\n\
	.fframe 0\n\
	{ .mii\n\
	  .save ar.pfs, r31\n\
	  alloc r31 = ar.pfs, 6, 0, 34, 40\n\
	  .save ar.lc, r30\n\
	  mov r30 = ar.lc\n\
	  .save pr, r29\n\
	  mov r29 = pr\n\
	  ;;\n\
	}\n\
	.body\n\
	{ .mii\n\
	  mov r8 = in1\n\
	  mov ar.ec = 6 + 2\n\
	  shr in0 = in0, 3\n\
	  ;;\n\
	}\n\
	{ .mmi\n\
	  adds in0 = -1, in0\n\
	  mov r16 = in1\n\
	  mov r17 = in2\n\
	  ;;\n\
	}\n\
	{ .mii\n\
	  mov r18 = in3\n\
	  mov ar.lc = in0\n\
	  mov pr.rot = 1 << 16\n\
	}\n\
	{ .mib\n\
	  mov r19 = in4\n\
	  mov r20 = in5\n\
	  ;;\n\
	}\n\
	.rotr s1[6+1], s2[6+1], s3[6+1], s4[6+1], s5[6+1], d[2]\n\
	.rotp p[6+2]\n\
0:	{ .mmi\n\
(p[0])	  ld8.nta s1[0] = [r16], 8\n\
(p[0])	  ld8.nta s2[0] = [r17], 8\n\
(p[6])	  xor d[0] = s1[6], s2[6]\n\
	}\n\
	{ .mmi\n\
(p[0])	  ld8.nta s3[0] = [r18], 8\n\
(p[0])	  ld8.nta s4[0] = [r19], 8\n\
(p[6])	  xor r21 = s3[6], s4[6]\n\
	  ;;\n\
	}\n\
	{ .mmi\n\
(p[0])	  ld8.nta s5[0] = [r20], 8\n\
(p[6+1])  st8.nta [r8] = d[1], 8\n\
(p[6])	  xor d[0] = d[0], r21\n\
	  ;;\n\
	}\n\
	{ .mfb\n\
(p[6])	  xor d[0] = d[0], s5[6]\n\
	  nop.f 0\n\
	  br.ctop.dptk.few 0b\n\
	  ;;\n\
	}\n\
	{ .mii\n\
	  mov ar.lc = r30\n\
	  mov pr = r29, -1\n\
	}\n\
	{ .bbb\n\
	  br.ret.sptk.few rp\n\
	}\n\
	.endp xor_ia64_5\n\
");

static struct xor_block_template xor_block_ia64 = {
	name: "ia64",
	do_2: xor_ia64_2,
	do_3: xor_ia64_3,
	do_4: xor_ia64_4,
	do_5: xor_ia64_5,
};

#define XOR_TRY_TEMPLATES	xor_speed(&xor_block_ia64)
