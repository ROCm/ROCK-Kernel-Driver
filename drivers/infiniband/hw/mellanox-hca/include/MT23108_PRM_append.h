/* - Mellanox Confidential and Proprietary -*/

/***
 *** This file was generated at "Tue Apr 13 13:45:13 2004"
 *** by:
 ***    % csp_bf -copyright=/mswg/misc/license-header.txt -bits MT23108_PRM_append.csp
 ***/

#ifndef H_bits_MT23108_PRM_append_csp_H
#define H_bits_MT23108_PRM_append_csp_H


/* Gather entry with inline data */

struct wqe_segment_data_inline_st {	/* Little Endian */
    pseudo_bit_t	byte_count[0x0000a];   /* Not including padding for 16Byte chunks */
    pseudo_bit_t	reserved0[0x00015];
    pseudo_bit_t	always1[0x00001];
/* -------------- */
    pseudo_bit_t	data[0x00018];         /* Data may be more this segment size - in 16Byte chunks */
/* -------------- */
}; 

/* Scatter/Gather entry with a pointer */

struct wqe_segment_data_ptr_st {	/* Little Endian */
    pseudo_bit_t	byte_count[0x0001f];
    pseudo_bit_t	always0[0x00001];
/* -------------- */
    pseudo_bit_t	l_key[0x00020];
/* -------------- */
    pseudo_bit_t	local_address_h[0x00020];
/* -------------- */
    pseudo_bit_t	local_address_l[0x00020];
/* -------------- */
}; 

/*  */

struct wqe_segment_atomic_st {	/* Little Endian */
    pseudo_bit_t	swap_add_h[0x00020];
/* -------------- */
    pseudo_bit_t	swap_add_l[0x00020];
/* -------------- */
    pseudo_bit_t	compare_h[0x00020];
/* -------------- */
    pseudo_bit_t	compare_l[0x00020];
/* -------------- */
}; 

/*  */

struct wqe_segment_remote_address_st {	/* Little Endian */
    pseudo_bit_t	remote_virt_addr_h[0x00020];
/* -------------- */
    pseudo_bit_t	remote_virt_addr_l[0x00020];
/* -------------- */
    pseudo_bit_t	rkey[0x00020];
/* -------------- */
    pseudo_bit_t	reserved0[0x00020];
/* -------------- */
}; 

/* Bind memory window segment */

struct wqe_segment_bind_st {	/* Little Endian */
    pseudo_bit_t	reserved0[0x0001d];
    pseudo_bit_t	rr[0x00001];           /* Remote read */
    pseudo_bit_t	rw[0x00001];           /* Remote write */
    pseudo_bit_t	a[0x00001];            /* atomic */
/* -------------- */
    pseudo_bit_t	reserved1[0x00020];
/* -------------- */
    pseudo_bit_t	new_rkey[0x00020];
/* -------------- */
    pseudo_bit_t	region_lkey[0x00020];
/* -------------- */
    pseudo_bit_t	start_address_h[0x00020];
/* -------------- */
    pseudo_bit_t	start_address_l[0x00020];
/* -------------- */
    pseudo_bit_t	length_h[0x00020];
/* -------------- */
    pseudo_bit_t	length_l[0x00020];
/* -------------- */
}; 

/*  */

struct wqe_segment_ud_st {	/* Little Endian */
    pseudo_bit_t	reserved0[0x00020];
/* -------------- */
    pseudo_bit_t	l_key[0x00020];        /* memory key for UD AV */
/* -------------- */
    pseudo_bit_t	av_address_63_32[0x00020];
/* -------------- */
    pseudo_bit_t	reserved1[0x00005];
    pseudo_bit_t	av_address_31_5[0x0001b];
/* -------------- */
    pseudo_bit_t	reserved2[0x00080];
/* -------------- */
    pseudo_bit_t	destination_qp[0x00018];
    pseudo_bit_t	reserved3[0x00008];
/* -------------- */
    pseudo_bit_t	q_key[0x00020];
/* -------------- */
    pseudo_bit_t	reserved4[0x00040];
/* -------------- */
}; 

/*  */

struct wqe_segment_rd_st {	/* Little Endian */
    pseudo_bit_t	destination_qp[0x00018];
    pseudo_bit_t	reserved0[0x00008];
/* -------------- */
    pseudo_bit_t	q_key[0x00020];
/* -------------- */
    pseudo_bit_t	reserved1[0x00040];
/* -------------- */
}; 

/*  */

struct wqe_segment_ctrl_recv_st {	/* Little Endian */
    pseudo_bit_t	reserved0[0x00002];
    pseudo_bit_t	e[0x00001];            /* WQE event */
    pseudo_bit_t	c[0x00001];            /* Create CQE (for "requested signalling" QP) */
    pseudo_bit_t	reserved1[0x0001c];
/* -------------- */
    pseudo_bit_t	reserved2[0x00020];
/* -------------- */
}; 

/*  */

struct wqe_segment_ctrl_mlx_st {	/* Little Endian */
    pseudo_bit_t	reserved0[0x00002];
    pseudo_bit_t	e[0x00001];            /* WQE event */
    pseudo_bit_t	c[0x00001];            /* Create CQE (for "requested signalling" QP) */
    pseudo_bit_t	reserved1[0x00004];
    pseudo_bit_t	sl[0x00004];
    pseudo_bit_t	max_statrate[0x00003];
    pseudo_bit_t	reserved2[0x00001];
    pseudo_bit_t	slr[0x00001];          /* 0= take slid from port. 1= take slid from given headers */
    pseudo_bit_t	v15[0x00001];          /* Send packet over VL15 */
    pseudo_bit_t	reserved3[0x0000e];
/* -------------- */
    pseudo_bit_t	vcrc[0x00010];         /* Packet's VCRC (if not 0 - otherwise computed by HW) */
    pseudo_bit_t	rlid[0x00010];         /* Destination LID (must match given headers) */
/* -------------- */
}; 

/*  */

struct wqe_segment_ctrl_send_st {	/* Little Endian */
    pseudo_bit_t	always1[0x00001];
    pseudo_bit_t	s[0x00001];            /* Solicited event */
    pseudo_bit_t	e[0x00001];            /* WQE event */
    pseudo_bit_t	c[0x00001];            /* Create CQE (for "requested signalling" QP) */
    pseudo_bit_t	reserved0[0x0001c];
/* -------------- */
    pseudo_bit_t	immediate[0x00020];
/* -------------- */
}; 

/*  */

struct wqe_segment_next_st {	/* Little Endian */
    pseudo_bit_t	nopcode[0x00005];      /* next opcode */
    pseudo_bit_t	reserved0[0x00001];
    pseudo_bit_t	nda_31_6[0x0001a];     /* NDA[31:6] */
/* -------------- */
    pseudo_bit_t	nds[0x00006];
    pseudo_bit_t	f[0x00001];            /* fence bit */
    pseudo_bit_t	dbd[0x00001];          /* doorbell rung */
    pseudo_bit_t	nee[0x00018];          /* next EE */
/* -------------- */
}; 
#endif /* H_bits_MT23108_PRM_append_csp_H */
