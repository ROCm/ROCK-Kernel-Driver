/*      $Id: lirc_sasem.h,v 1.3 2005/03/29 17:51:45 lirc Exp $      */

#ifndef LIRC_SASEM_H
#define LIRC_SASEM_H

#include "kcompat.h"

/*
 * Version Information
 */
#define DRIVER_VERSION  	"v0.4"
#define DATE            	"Mar 2005"
#define DRIVER_AUTHOR 		"Oliver Stabel <oliver.stabel@gmx.de>"
#define DRIVER_DESC 		"USB Driver for Sasem Remote Controller V1.1"
#define DRIVER_SHORTDESC 	"Sasem"
#define DRIVER_NAME		"lirc_sasem"

#define BANNER \
  KERN_INFO DRIVER_SHORTDESC " " DRIVER_VERSION " (" DATE ")\n" \
  KERN_INFO "   by " DRIVER_AUTHOR "\n"

static const char longbanner[] = {
	DRIVER_DESC ", " DRIVER_VERSION " (" DATE "), by " DRIVER_AUTHOR
};

#define MAX_INTERRUPT_DATA 8
#define SASEM_MINOR 144

#include <linux/version.h>
#include <linux/time.h>

static const char SasemCode[MAX_INTERRUPT_DATA] =
	{ 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

struct SasemDevice {
	struct usb_device *Device;
	struct usb_endpoint_descriptor *DescriptorIn;
	struct usb_endpoint_descriptor *DescriptorOut;
	struct urb *UrbIn;
	struct urb *UrbOut;
	struct timeval PressTime;
	unsigned int InterfaceNum;
	int	Devnum;
	unsigned char BufferIn[MAX_INTERRUPT_DATA];
	unsigned char BufferOut[MAX_INTERRUPT_DATA];
	struct semaphore SemLock;

	char LastCode[MAX_INTERRUPT_DATA];
	int CodeSaved;
	
	/* lirc */
	struct lirc_plugin *LircPlugin;
	int Connected;

	/* LCDProc */
	int Open;
	wait_queue_head_t QueueWrite;
	wait_queue_head_t QueueOpen;
};

#ifndef KERNEL_2_5
static void * SasemProbe(struct usb_device *Device, unsigned InterfaceNum,
		const struct usb_device_id *ID);
static void SasemDisconnect(struct usb_device *Device, void *Ptr);
static void SasemCallbackIn(struct urb *Urb);
static void SasemCallbackOut(struct urb *Urb);
#else
static int SasemProbe(struct usb_interface *Int,
		      const struct usb_device_id *ID);
static void SasemDisconnect(struct usb_interface *Int); 
static void SasemCallbackIn(struct urb *Urb, struct pt_regs *regs);
static void SasemCallbackOut(struct urb *Urb, struct pt_regs *regs);
#endif

/* lirc */
static int UnregisterFromLirc(struct SasemDevice *SasemDevice);
static int LircSetUseInc(void *Data);
static void LircSetUseDec(void *Data);

#define IOCTL_GET_HARD_VERSION  1
#define IOCTL_GET_DRV_VERSION   2

static int SasemFSOpen(struct inode *Inode, struct file *File);
static int SasemFSRelease(struct inode *Inode, struct file *File);
static ssize_t SasemFSWrite(struct file *File, const char *Buffer,
			size_t Count, loff_t *Pos);
static ssize_t SasemFSRead(struct file *File, char *Buffer,
			size_t Count, loff_t *Unused_pos);
static int SasemFSIoctl(struct inode *Inode, struct file *File,
			unsigned Cmd, unsigned long Arg);
static unsigned SasemFSPoll(struct file *File, poll_table *Wait);

#endif
