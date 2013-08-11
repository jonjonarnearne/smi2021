/*******************************************************************************
 * smi2021_main.c                                                              *
 *                                                                             *
 * USB Driver for SMI2021 - EasyCAP                                            *
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

#include "smi2021.h"

#define VENDOR_ID 0x1c88
#define BOOTLOADER_ID 0x0007

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jon Arne Jørgensen <jonjon.arnearne--a.t--gmail.com>");
MODULE_DESCRIPTION("SMI2021 - EasyCap");
MODULE_VERSION(SMI2021_DRIVER_VERSION);

static const struct usb_device_id smi2021_usb_device_id_table[] = {
	{ USB_DEVICE(VENDOR_ID, BOOTLOADER_ID)	},
	{ }
};

MODULE_DEVICE_TABLE(usb, smi2021_usb_device_id_table);

/******************************************************************************/
/*                                                                            */
/*          DEVICE  -  PROBE   &   DISCONNECT                                 */
/*                                                                            */
/******************************************************************************/
static int smi2021_usb_probe(struct usb_interface *intf,
					const struct usb_device_id *devid)
{
	struct usb_device *udev = interface_to_usbdev(intf);

	if (udev->descriptor.idProduct == BOOTLOADER_ID)
		return smi2021_bootloader_probe(intf, devid);

	return -ENODEV;
}

static void smi2021_usb_disconnect(struct usb_interface *intf)
{
	struct usb_device *udev = interface_to_usbdev(intf);
	if (udev == NULL)
		return;

	if (udev->descriptor.idProduct == BOOTLOADER_ID)
		return smi2021_bootloader_disconnect(intf);
}

/******************************************************************************/
/*                                                                            */
/*            MODULE  -  INIT  &  EXIT                                        */
/*                                                                            */
/******************************************************************************/

struct usb_driver smi2021_usb_driver = {
	.name = "smi2021",
	.id_table = smi2021_usb_device_id_table,
	.probe = smi2021_usb_probe,
	.disconnect = smi2021_usb_disconnect
};

module_usb_driver(smi2021_usb_driver);
