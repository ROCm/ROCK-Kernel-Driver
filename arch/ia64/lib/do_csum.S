/*
 *
 * Optmized version of the standard do_csum() function
 *
 * Return: a 64bit quantity containing the 16bit Internet checksum
 *
 * Inputs:
 *	in0: address of buffer to checksum (char *)
 *	in1: length of the buffer (int)
 * 
 * Copyright (C) 1999 Hewlett-Packard Co
 * Copyright (C) 1999 Stephane Eranian <eranian@hpl.hp.com>
 *
 */

#include <asm/asmmacro.h>

//
// Theory of operations:
//	The goal is to go as quickly as possible to the point where
//	we can checksum 8 bytes/loop. Before reaching that point we must
//	take care of incorrect alignment of first byte.
//
//	The code hereafter also takes care of the "tail" part of the buffer
//	before entering the core loop, if any. The checksum is a sum so it
//	allows us to commute operations. So we do do the "head" and "tail"
//	first to finish at full speed in the body. Once we get the head and
//	tail values, we feed them into the pipeline, very handy initialization.
//
//	Of course we deal with the special case where the whole buffer fits
//	into one 8 byte word. In this case we have only one entry in the pipeline.
//
//	We use a (3+1)-stage pipeline in the loop to account for possible
//	load latency and also to accomodate for head and tail.
//
//	The end of the function deals with folding the checksum from 64bits
//	down to 16bits taking care of the carry.
//
//	This version avoids synchronization in the core loop by also using a
//	pipeline for the accumulation of the checksum in result[].
//
//	 p[]     
//	|---|
//     0|   | r32 : new value loaded in pipeline
//	|---|
//     1|   | r33 : in transit data
//	|---|
//     2|   | r34 : current value to add to checksum
//	|---|
//     3|   | r35 : previous value added to checksum (previous iteration)
//      |---|
//
//	result[] 
//	|---|
//     0|   | r36 : new checksum
//	|---|
//     1|   | r37 : previous value of checksum
//	|---|
//     2|   | r38 : final checksum when out of the loop (after 2 epilogue rots)
//	|---|
//
//
// NOT YET DONE:
//	- Take advantage of the MMI bandwidth to load more than 8byte per loop
//	  iteration
//	- use the lfetch instruction to augment the chances of the data being in
//	  the cache when we need it.
//	- Maybe another algorithm which would take care of the folding at the
//	  end in a different manner
//	- Work with people more knowledgeable than me on the network stack
//	  to figure out if we could not split the function depending on the 
//	  type of packet or alignment we get. Like the ip_fast_csum() routine
//	  where we know we have at least 20bytes worth of data to checksum.
//	- Look at RFCs about checksums to see whether or not we can do better
//
//	- Do a better job of handling small packets.
//
#define saved_pfs	r11
#define hmask		r16
#define tmask		r17
#define first		r18
#define firstval	r19
#define firstoff	r20
#define last		r21
#define lastval		r22
#define lastoff		r23
#define saved_lc	r24
#define saved_pr	r25
#define tmp1		r26
#define tmp2		r27
#define tmp3		r28
#define carry		r29

#define buf		in0
#define len		in1


	.text
	.psr abi64
	.psr lsb
	.lsb

// unsigned long do_csum(unsigned char *buf,int len)

GLOBAL_ENTRY(do_csum)
	UNW(.prologue)
	UNW(.save ar.pfs, saved_pfs)
	alloc saved_pfs=ar.pfs,2,8,0,8

	.rotr p[4], result[3]
	mov ret0=r0		// in case we have zero length
	cmp4.lt p0,p6=r0,len	// check for zero length or negative (32bit len)
	;;			// avoid WAW on CFM
	mov tmp3=0x7		// a temporary mask/value
	add tmp1=buf,len	// last byte's address
(p6)	br.ret.spnt.few rp	// return if true (hope we can avoid that)

	and firstoff=7,buf	// how many bytes off for first element
	tbit.nz p10,p0=buf,0	// is buf an odd address ?
	mov hmask=-1		// intialize head mask
	;;

	andcm first=buf,tmp3	// 8byte aligned down address of first element
	mov tmask=-1		// initialize tail mask
	adds tmp2=-1,tmp1	// last-1
	;;
	and lastoff=7,tmp1	// how many bytes off for last element
	andcm last=tmp2,tmp3	// address of word containing last byte
	UNW(.save pr, saved_pr)
	mov saved_pr=pr		// preserve predicates (rotation)
	;;
	sub tmp3=last,first	// tmp3=distance from first to last
	cmp.eq p8,p9=last,first	// everything fits in one word ?
	sub tmp1=8,lastoff	// complement to lastoff

	ld8 firstval=[first],8	// load,ahead of time, "first" word
	shl tmp2=firstoff,3	// number of bits
	;;
	and tmp1=7, tmp1	// make sure that if tmp1==8 -> tmp1=0

(p9)	ld8 lastval=[last]	// load,ahead of time, "last" word, if needed
(p8)	mov lastval=r0		// we don't need lastval if first==last
	mov result[1]=r0	// initialize result
	;;

	shl tmp1=tmp1,3		// number of bits
	shl hmask=hmask,tmp2 	// build head mask, mask off [0,firstoff[
	;;
	shr.u tmask=tmask,tmp1	// build tail mask, mask off ]8,lastoff]
	UNW(.save ar.lc, saved_lc)
	mov saved_lc=ar.lc	// save lc
	;;

	UNW(.body)

(p8)	and hmask=hmask,tmask	// apply tail mask to head mask if 1 word only
(p9)	and p[1]=lastval,tmask	// mask last it as appropriate
	shr.u tmp3=tmp3,3	// we do 8 bytes per loop
	;;
	cmp.lt p6,p7=2,tmp3	// tmp3 > 2 ?
	and p[2]=firstval,hmask	// and mask it as appropriate
	add tmp1=-2,tmp3	// -2 = -1 (br.ctop) -1 (last-first)
	;;
	// XXX Fixme: not very nice initialization here
	//
	// Setup loop control registers: 
	//
	// tmp3=0 (1 word)   : lc=0, ec=2, p16=F
	// tmp3=1 (2 words)  : lc=0, ec=3, p16=F
	// tmp3=2 (3 words)  : lc=0, ec=4, p16=T
	// tmp3>2 (4 or more): lc=tmp3-2, ec=4, p16=T
	//
	cmp.eq p8,p9=r0,tmp3	// tmp3 == 0 ?
(p6)	mov ar.lc=tmp1
(p7)	mov ar.lc=0
	;;
	cmp.lt p6,p7=1,tmp3	// tmp3 > 1 ?
(p8)	mov ar.ec=2		// we need the extra rotation on result[]
(p9)	mov ar.ec=3		// hard not to set it twice sometimes
	;;
	mov carry=r0			// initialize carry
(p6)	mov ar.ec=4
(p6)	mov pr.rot=0xffffffffffff0000	// p16=T, p18=T

	cmp.ne p8,p0=r0,r0		// p8 is false
	mov p[3]=r0			// make sure first compare fails
(p7)	mov pr.rot=0xfffffffffffe0000	// p16=F, p18=T
	;;
1:
(p16)	ld8 p[0]=[first],8		// load next
(p8)	adds carry=1,carry		// add carry on prev_prev_value
(p18)	add result[0]=result[1],p[2]	// new_res = prev_res + cur_val
	cmp.ltu p8,p0=result[1],p[3]	// p8= prev_result < prev_val
	br.ctop.dptk.few 1b		// loop until lc--==0
	;;				// RAW on carry when loop exits
 (p8)	adds carry=1,carry;;		// correct for carry on prev_value
	add result[2]=carry,result[2];;	// add carry to final result
	cmp.ltu p6,p7=result[2], carry	// check for new carry
	;;
(p6)	adds result[2]=1,result[1]	// correct if required
	movl tmp3=0xffffffff
	;;
	// XXX Fixme
	//
	// now fold 64 into 16 bits taking care of carry
	// that's not very good because it has lots of sequentiality
	//
	and tmp1=result[2],tmp3
	shr.u tmp2=result[2],32
	;;
	add result[2]=tmp1,tmp2
	shr.u tmp3=tmp3,16
	;;
	and tmp1=result[2],tmp3
	shr.u tmp2=result[2],16
	;;
	add result[2]=tmp1,tmp2
	;;
	and tmp1=result[2],tmp3
	shr.u tmp2=result[2],16
	;;
	add result[2]=tmp1,tmp2
	;;
	and tmp1=result[2],tmp3
	shr.u tmp2=result[2],16
	;;
	add ret0=tmp1,tmp2
	mov pr=saved_pr,0xffffffffffff0000
	;;
	// if buf was odd then swap bytes 
	mov ar.pfs=saved_pfs		// restore ar.ec
(p10)	mux1 ret0=ret0,@rev		// reverse word
	;;
	mov ar.lc=saved_lc
(p10)	shr.u ret0=ret0,64-16	// + shift back to position = swap bytes
	br.ret.sptk.few rp
END(do_csum)
