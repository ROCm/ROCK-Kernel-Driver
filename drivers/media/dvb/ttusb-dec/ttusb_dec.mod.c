#include <linux/module.h>
#include <linux/vermagic.h>
#include <linux/compiler.h>

MODULE_INFO(vermagic, VERMAGIC_STRING);

static const char __module_depends[]
__attribute_used__
__attribute__((section(".modinfo"))) =
"depends=dvb-core";

MODULE_ALIAS("usb:v0B48p1006dl*dh*dc*dsc*dp*ic*isc*ip*");
MODULE_ALIAS("usb:v0B48p1008dl*dh*dc*dsc*dp*ic*isc*ip*");
