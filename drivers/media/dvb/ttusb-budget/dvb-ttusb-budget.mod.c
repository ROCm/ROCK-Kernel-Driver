#include <linux/module.h>
#include <linux/vermagic.h>
#include <linux/compiler.h>

MODULE_INFO(vermagic, VERMAGIC_STRING);

static const struct modversion_info ____versions[]
__attribute__((section("__versions"))) = {
	{ 0x98a034c6, "struct_module" },
	{ 0x1b636da2, "usb_deregister" },
	{ 0x9c655b4f, "usb_register" },
	{ 0x37a0cba, "kfree" },
	{ 0xc490b263, "dvb_unregister_adapter" },
	{ 0xf0ca7c9b, "dvb_unregister_i2c_bus" },
	{ 0x213a4973, "dvb_dmxdev_release" },
	{ 0x12014018, "dvb_net_release" },
	{ 0x616af636, "dvb_net_init" },
	{ 0x6a8b7cf7, "dvb_dmx_release" },
	{ 0x107a341d, "dvb_dmxdev_init" },
	{ 0x8fa22872, "dvb_dmx_init" },
	{ 0x5117ed38, "dvb_add_frontend_ioctls" },
	{ 0xfc17c7e7, "dvb_register_i2c_bus" },
	{ 0x3ad6f025, "dvb_register_adapter" },
	{ 0x7ac96080, "kmem_cache_alloc" },
	{ 0xa73704a, "malloc_sizes" },
	{ 0xdfcbe89f, "usb_set_interface" },
	{ 0x4f0eac15, "usb_reset_configuration" },
	{ 0x1b49153f, "usb_unlink_urb" },
	{ 0xf136026d, "usb_alloc_urb" },
	{ 0xe6f8a15d, "dma_alloc_coherent" },
	{ 0xe8d874ea, "dma_free_coherent" },
	{ 0x8ee31378, "usb_free_urb" },
	{ 0x53e02b2e, "usb_submit_urb" },
	{ 0xda02d67, "jiffies" },
	{ 0x4f7c0ba8, "dvb_dmx_swfilter_packets" },
	{ 0x9d669763, "memcpy" },
	{ 0xd22b546, "__up_wakeup" },
	{ 0x1b7d4074, "printk" },
	{ 0x85eee601, "usb_bulk_msg" },
	{ 0x28c3bbf5, "__down_failed_interruptible" },
	{ 0xd533bec7, "__might_sleep" },
};

static const char __module_depends[]
__attribute_used__
__attribute__((section(".modinfo"))) =
"depends=usbcore,dvb-core";

MODULE_ALIAS("usb:v0B48p1003dl*dh*dc*dsc*dp*ic*isc*ip*");
MODULE_ALIAS("usb:v0B48p1004dl*dh*dc*dsc*dp*ic*isc*ip*");
MODULE_ALIAS("usb:v0B48p1005dl*dh*dc*dsc*dp*ic*isc*ip*");
