/*
 * Version Information
 */
#define DRIVER_VERSION 		"v0.1"
#define DATE 			"June 2004"
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

static const char sc_cSasemCode[MAX_INTERRUPT_DATA] =
	{ 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

typedef struct usb_driver t_usb_driver, *tp_usb_driver;
typedef struct usb_device t_usb_device, *tp_usb_device;
typedef struct usb_interface t_usb_interface, *tp_usb_interface;
typedef struct usb_device_id t_usb_device_id, *tp_usb_device_id;
typedef struct usb_host_interface t_usb_host_interface,
	*tp_usb_host_interface;
typedef struct usb_interface_descriptor t_usb_interface_descriptor,
	*tp_usb_interface_descriptor;
typedef struct usb_endpoint_descriptor t_usb_endpoint_descriptor,
	*tp_usb_endpoint_descriptor;
typedef struct urb t_urb, *tp_urb;

typedef struct semaphore t_semaphore, *tp_semaphore;

typedef struct lirc_plugin t_lirc_plugin, *tp_lirc_plugin;
typedef struct lirc_buffer t_lirc_buffer;

struct sasemDevice {
	t_usb_device *m_device;
	t_usb_endpoint_descriptor *m_descriptorIn;
	t_usb_endpoint_descriptor *m_descriptorOut;
	t_urb *m_urbIn;
	t_urb *m_urbOut;
	unsigned int m_iInterfaceNum;
	int	m_iDevnum;
	unsigned char m_cBufferIn[MAX_INTERRUPT_DATA];
	t_semaphore m_semLock;

	char m_cLastCode[MAX_INTERRUPT_DATA];
	int m_iCodeSaved;
	
	/* lirc */
	t_lirc_plugin *m_lircPlugin;
	int m_iConnected;
};

typedef struct sasemDevice t_sasemDevice, *tp_sasemDevice;

static void* s_sasemProbe(t_usb_device *p_dev, unsigned p_iInterfaceNum,
			  const t_usb_device_id *p_id);
static void s_sasemDisconnect(t_usb_device *p_dev, void *p_ptr);
static void s_sasemCallbackIn(t_urb *p_urb);

static int s_unregister_from_lirc(t_sasemDevice *p_sasemDevice);
static int s_lirc_set_use_inc(void *p_data);
static void s_lirc_set_use_dec(void *p_data);
