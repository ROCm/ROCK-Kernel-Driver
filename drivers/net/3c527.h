/*
 *	3COM "EtherLink MC/32" Descriptions
 */

/*
 *	Registers
 */
  
#define HOST_CMD		0

#define HOST_STATUS		2
#define		HOST_STATUS_CRR	(1<<6)
#define		HOST_STATUS_CWR	(1<<5)

#define HOST_CTRL		6
#define		HOST_CTRL_ATTN	(1<<7)
#define 	HOST_CTRL_RESET	(1<<6)
#define 	HOST_CTRL_INTE	(1<<2)

#define HOST_RAMPAGE		8

struct skb_header
{
	u8	status __attribute((packed));
	u8	control __attribute((packed));
	u16	next __attribute((packed));	/* Do not change! */
	u16	length __attribute((packed));
	u32	data __attribute((packed));
};

#define STATUS_MASK	0x0F
#define COMPLETED	0x80
#define COMPLETED_OK	0x40
#define BUFFER_BUSY	0x20

#define CONTROL_EOP	0x80	/* End Of Packet */
#define CONTROL_EL	0x40	/* End of List */


#define MCA_MC32_ID	0x0041	/* Our MCA ident */