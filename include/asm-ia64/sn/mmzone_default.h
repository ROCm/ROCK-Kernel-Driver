/*
 * Copyright, 2000, Silicon Graphics, sprasad@engr.sgi.com
 */

#define MAXNODES                16
#define MAXNASIDS               16

#define CHUNKSZ                (8*1024*1024)
#define CHUNKSHIFT              23      /* 2 ^^ CHUNKSHIFT == CHUNKSZ */

#define CNODEID_TO_NASID(n)	n
#define NASID_TO_CNODEID(n)     n

#define MAX_CHUNKS_PER_NODE     8

