#ifndef __LINUX_ETRAX_USB_H
#define __LINUX_ETRAX_USB_H

#include <linux/types.h>
#include <linux/list.h>

typedef struct USB_IN_Desc {
	__u16 sw_len;
	__u16 command;
	unsigned long next;
	unsigned long buf;
	__u16 hw_len;
	__u16 status;
} USB_IN_Desc_t;

typedef struct USB_SB_Desc {
	__u16 sw_len;
	__u16 command;
	unsigned long next;
	unsigned long buf;
	__u32 dummy;
} USB_SB_Desc_t;

typedef struct USB_EP_Desc {
	__u16 hw_len;
	__u16 command;
	unsigned long sub;
	unsigned long nep;
	__u32 dummy;
} USB_EP_Desc_t;

struct virt_root_hub {
	int devnum;
	void *urb;
	void *int_addr;
	int send;
	int interval;
	int numports;
	struct timer_list rh_int_timer;
	__u16 wPortChange_1;
	__u16 wPortChange_2;
	__u16 prev_wPortStatus_1;
	__u16 prev_wPortStatus_2;
};

struct etrax_usb_intr_traffic {
	int sleeping;
	int error;
	struct wait_queue *wq;
};

typedef struct etrax_usb_hc {
	struct usb_bus *bus;
	struct virt_root_hub rh;
	struct etrax_usb_intr_traffic intr;
} etrax_hc_t;

typedef enum {idle, eot, nodata}  etrax_usb_rx_state_t;

typedef struct etrax_usb_urb_priv {
	USB_SB_Desc_t *first_sb;
	__u32 rx_offset;
	etrax_usb_rx_state_t rx_state;
	__u8 eot;
	struct list_head ep_in_list;
} etrax_urb_priv_t;


struct usb_reg_context
{
	etrax_hc_t *hc;
	__u32 r_usb_epid_attn;
	__u8 r_usb_status;
	__u32 r_usb_rh_port_status_1;
	__u32 r_usb_rh_port_status_2;
	__u32 r_usb_irq_mask_read;
	struct tq_struct usb_bh;
#if 0
	__u32 r_usb_ept_data[32];
#endif
};

struct in_chunk
{
	void *data;
	int length;
	char epid;
	struct list_head list;
};

  
/* --------------------------------------------------------------------------- 
   Virtual Root HUB 
   ------------------------------------------------------------------------- */
/* destination of request */
#define RH_INTERFACE               0x01
#define RH_ENDPOINT                0x02
#define RH_OTHER                   0x03

#define RH_CLASS                   0x20
#define RH_VENDOR                  0x40

/* Requests: bRequest << 8 | bmRequestType */
#define RH_GET_STATUS           0x0080
#define RH_CLEAR_FEATURE        0x0100
#define RH_SET_FEATURE          0x0300
#define RH_SET_ADDRESS		0x0500
#define RH_GET_DESCRIPTOR	0x0680
#define RH_SET_DESCRIPTOR       0x0700
#define RH_GET_CONFIGURATION	0x0880
#define RH_SET_CONFIGURATION	0x0900
#define RH_GET_STATE            0x0280
#define RH_GET_INTERFACE        0x0A80
#define RH_SET_INTERFACE        0x0B00
#define RH_SYNC_FRAME           0x0C80
/* Our Vendor Specific Request */
#define RH_SET_EP               0x2000


/* Hub port features */
#define RH_PORT_CONNECTION         0x00
#define RH_PORT_ENABLE             0x01
#define RH_PORT_SUSPEND            0x02
#define RH_PORT_OVER_CURRENT       0x03
#define RH_PORT_RESET              0x04
#define RH_PORT_POWER              0x08
#define RH_PORT_LOW_SPEED          0x09
#define RH_C_PORT_CONNECTION       0x10
#define RH_C_PORT_ENABLE           0x11
#define RH_C_PORT_SUSPEND          0x12
#define RH_C_PORT_OVER_CURRENT     0x13
#define RH_C_PORT_RESET            0x14

/* Hub features */
#define RH_C_HUB_LOCAL_POWER       0x00
#define RH_C_HUB_OVER_CURRENT      0x01

#define RH_DEVICE_REMOTE_WAKEUP    0x00
#define RH_ENDPOINT_STALL          0x01

/* Our Vendor Specific feature */
#define RH_REMOVE_EP               0x00


#define RH_ACK                     0x01
#define RH_REQ_ERR                 -1
#define RH_NACK                    0x00

/* Field definitions for */

#define USB_IN_command__eol__BITNR      0 /* command macros */
#define USB_IN_command__eol__WIDTH      1
#define USB_IN_command__eol__no         0
#define USB_IN_command__eol__yes        1

#define USB_IN_command__intr__BITNR     3
#define USB_IN_command__intr__WIDTH     1
#define USB_IN_command__intr__no        0
#define USB_IN_command__intr__yes       1

#define USB_IN_status__eop__BITNR       1 /* status macros. */
#define USB_IN_status__eop__WIDTH       1
#define USB_IN_status__eop__no          0
#define USB_IN_status__eop__yes         1

#define USB_IN_status__eot__BITNR       5
#define USB_IN_status__eot__WIDTH       1
#define USB_IN_status__eot__no          0
#define USB_IN_status__eot__yes         1

#define USB_IN_status__error__BITNR     6
#define USB_IN_status__error__WIDTH     1
#define USB_IN_status__error__no        0
#define USB_IN_status__error__yes       1

#define USB_IN_status__nodata__BITNR    7
#define USB_IN_status__nodata__WIDTH    1
#define USB_IN_status__nodata__no       0
#define USB_IN_status__nodata__yes      1

#define USB_IN_status__epid__BITNR      8
#define USB_IN_status__epid__WIDTH      5

#define USB_EP_command__eol__BITNR      0
#define USB_EP_command__eol__WIDTH      1
#define USB_EP_command__eol__no         0
#define USB_EP_command__eol__yes        1

#define USB_EP_command__eof__BITNR      1
#define USB_EP_command__eof__WIDTH      1
#define USB_EP_command__eof__no         0
#define USB_EP_command__eof__yes        1

#define USB_EP_command__intr__BITNR     3
#define USB_EP_command__intr__WIDTH     1
#define USB_EP_command__intr__no        0
#define USB_EP_command__intr__yes       1

#define USB_EP_command__enable__BITNR   4
#define USB_EP_command__enable__WIDTH   1
#define USB_EP_command__enable__no      0
#define USB_EP_command__enable__yes     1

#define USB_EP_command__hw_valid__BITNR 5
#define USB_EP_command__hw_valid__WIDTH 1
#define USB_EP_command__hw_valid__no    0
#define USB_EP_command__hw_valid__yes   1

#define USB_EP_command__epid__BITNR     8
#define USB_EP_command__epid__WIDTH     5

#define USB_SB_command__eol__BITNR      0 /* command macros. */
#define USB_SB_command__eol__WIDTH      1
#define USB_SB_command__eol__no         0
#define USB_SB_command__eol__yes        1

#define USB_SB_command__eot__BITNR      1
#define USB_SB_command__eot__WIDTH      1
#define USB_SB_command__eot__no         0
#define USB_SB_command__eot__yes        1

#define USB_SB_command__intr__BITNR     3
#define USB_SB_command__intr__WIDTH     1
#define USB_SB_command__intr__no        0
#define USB_SB_command__intr__yes       1

#define USB_SB_command__tt__BITNR       4
#define USB_SB_command__tt__WIDTH       2
#define USB_SB_command__tt__zout        0
#define USB_SB_command__tt__in          1
#define USB_SB_command__tt__out         2
#define USB_SB_command__tt__setup       3


#define USB_SB_command__rem__BITNR      8
#define USB_SB_command__rem__WIDTH      6

#define USB_SB_command__full__BITNR     6
#define USB_SB_command__full__WIDTH     1
#define USB_SB_command__full__no        0
#define USB_SB_command__full__yes       1

#endif
