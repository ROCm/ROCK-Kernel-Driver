# SPDX-License-Identifier: GPL-2.0

# chacha is used by the /dev/random driver which is always builtin
obj-y						+= chacha.o
obj-$(CONFIG_CRYPTO_LIB_CHACHA_GENERIC)		+= libchacha.o

obj-$(CONFIG_CRYPTO_LIB_ARC4)			+= libarc4.o
libarc4-y					:= arc4.o

obj-$(CONFIG_CRYPTO_LIB_POLY1305_GENERIC)	+= libpoly1305.o
libpoly1305-y					:= poly1305.o
