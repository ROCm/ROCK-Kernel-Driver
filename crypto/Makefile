#
# Cryptographic API
#

export-objs := api.o

obj-$(CONFIG_CRYPTO) += api.o cipher.o digest.o compress.o
obj-$(CONFIG_KMOD) += autoload.o

obj-$(CONFIG_CRYPTO_MD5) += md5.o
obj-$(CONFIG_CRYPTO_SHA1) += sha1.o
obj-$(CONFIG_CRYPTO_DES) += des.o

obj-$(CONFIG_CRYPTO_TEST) += tcrypt.o

include $(TOPDIR)/Rules.make
