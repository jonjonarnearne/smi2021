/*******************************************************************************
 * smi2021.h                                                                   *
 *                                                                             *
 * USB Driver for SMI2021 - EasyCap                                            *
 * *****************************************************************************
 *
 * Copyright 2011-2013 Jon Arne Jørgensen
 * <jonjon.arnearne--a.t--gmail.com>
 *
 * Copyright 2011, 2012 Tony Brown, Michal Demin, Jeffry Johnston
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * This driver is heavily influensed by the STK1160 driver.
 * Copyright (C) 2012 Ezequiel Garcia
 * <elezegarcia--a.t--gmail.com>
 *
 */

#ifndef SMI2021_H
#define SMI2021_H

#include <linux/module.h>
#include <linux/usb.h>

#define SMI2021_DRIVER_VERSION "0.1"

/* General USB control setup */
#define SMI2021_USB_REQUEST	0x01
#define SMI2021_USB_INDEX	0x00
#define SMI2021_USB_SNDPIPE	0x00
#define SMI2021_USB_RCVPIPE	0x80

/* Hardware constants */
#define SMI2021_HW_STATE_HEAD		0x01

#ifdef DEBUG
#define smi2021_dbg(fmt, args...)		\
	pr_debug("smi2021::%s: " fmt, __func__, \
			##args)
#else
#define smi2021_dbg(fmt, args...)
#endif

#define smi2021_info(fmt, args...)		\
	pr_info("smi2021::%s: " fmt,		\
		__func__, ##args)

#define smi2021_warn(fmt, args...)		\
	pr_warn("smi2021::%s: " fmt,		\
		__func__, ##args)

#define smi2021_err(fmt, args...)		\
	pr_err("smi2021::%s: " fmt,		\
		__func__, ##args)

/* Structs passed on USB for device setup */
struct smi2021_set_hw_state {
	u8 head;
	u8 state;
} __packed;

/* Provided by smi2021_bootloader.c */
int smi2021_bootloader_probe(struct usb_interface *intf,
					const struct usb_device_id *devid);
void smi2021_bootloader_disconnect(struct usb_interface *intf);
#endif /* SMI2021_H */
