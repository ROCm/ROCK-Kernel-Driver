/*
 *  longhaul.h
 *  (C) 2003 Dave Jones.
 *
 *  Licensed under the terms of the GNU GPL License version 2.
 *
 *  VIA-specific information
 */

union msr_bcr2 {
	struct {
		unsigned Reseved:19,	// 18:0
		ESOFTBF:1,		// 19
		Reserved2:3,		// 22:20
		CLOCKMUL:4,		// 26:23
		Reserved3:5;		// 31:27
	} bits;
	unsigned long val;
};

union msr_longhaul {
	struct {
		unsigned RevisionID:4,	// 3:0
		RevisionKey:4,		// 7:4
		EnableSoftBusRatio:1,	// 8
		EnableSoftVID:1,	// 9
		EnableSoftBSEL:1,	// 10
		Reserved:3,		// 11:13
		SoftBusRatio4:1,	// 14
		VRMRev:1,		// 15
		SoftBusRatio:4,		// 19:16
		SoftVID:5,		// 24:20
		Reserved2:3,		// 27:25
		SoftBSEL:2,		// 29:28
		Reserved3:2,		// 31:30
		MaxMHzBR:4,		// 35:32
		MaximumVID:5,		// 40:36
		MaxMHzFSB:2,		// 42:41
		MaxMHzBR4:1,		// 43
		Reserved4:4,		// 47:44
		MinMHzBR:4,		// 51:48
		MinimumVID:5,		// 56:52
		MinMHzFSB:2,		// 58:57
		MinMHzBR4:1,		// 59
		Reserved5:4;		// 63:60
	} bits;
	unsigned long long val;
};

