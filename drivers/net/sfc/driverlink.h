/****************************************************************************
 * Driver for Solarflare Solarstorm network controllers and boards
 * Copyright 2005      Fen Systems Ltd.
 * Copyright 2006-2008 Solarflare Communications Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#ifndef EFX_DRIVERLINK_H
#define EFX_DRIVERLINK_H

/* Forward declarations */
struct efx_dl_device;
struct efx_nic;

#ifdef CONFIG_SFC_DRIVERLINK

#include "driverlink_api.h"

/* Efx callback devices
 *
 * A list of the devices that own each callback. The partner to
 * struct efx_dl_callbacks.
 */
struct efx_dl_cb_devices {
	struct efx_dl_device *tx_packet;
	struct efx_dl_device *rx_packet;
	struct efx_dl_device *request_mtu;
	struct efx_dl_device *mtu_changed;
	struct efx_dl_device *event;
};

extern struct efx_dl_callbacks efx_default_callbacks;

#define EFX_DL_CALLBACK(_port, _name, ...)				\
	(_port)->dl_cb._name((_port)->dl_cb_dev._name, __VA_ARGS__)

extern int efx_dl_register_nic(struct efx_nic *efx);
extern void efx_dl_unregister_nic(struct efx_nic *efx);

/* Suspend and resume client drivers over a hardware reset */
extern void efx_dl_reset_suspend(struct efx_nic *efx);
extern void efx_dl_reset_resume(struct efx_nic *efx, int ok);

#define EFX_DL_LOG EFX_LOG

#else /* CONFIG_SFC_DRIVERLINK */

enum efx_veto { EFX_ALLOW_PACKET = 0 };

static inline int efx_nop_callback(struct efx_nic *efx) { return 0; }
#define EFX_DL_CALLBACK(port, name, ...) efx_nop_callback(port)

static inline int efx_dl_register_nic(struct efx_nic *efx) { return 0; }
static inline void efx_dl_unregister_nic(struct efx_nic *efx) {}

static inline void efx_dl_reset_suspend(struct efx_nic *efx) {}
static inline void efx_dl_reset_resume(struct efx_nic *efx, int ok) {}

#define EFX_DL_LOG(efx, fmt, args...) ((void)(efx))

#endif /* CONFIG_SFC_DRIVERLINK */

#endif /* EFX_DRIVERLINK_H */
