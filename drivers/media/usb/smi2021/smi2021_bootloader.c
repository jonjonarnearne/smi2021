/************************************************************************
 * smi2021_bootloader.c							*
 *									*
 * USB Driver for SMI2021 - EasyCAP					*
 * **********************************************************************
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

#include <linux/module.h>
#include <linux/usb.h>
#include <linux/firmware.h>
#include <linux/slab.h>

#define FIRMWARE_CHUNK_SIZE	62
#define FIRMWARE_HEADER_SIZE	2

#define FIRMWARE_CHUNK_HEAD_0	0x05
#define FIRMWARE_CHUNK_HEAD_1	0xff
#define FIRMWARE_HW_STATE_HEAD	0x01
#define FIRMWARE_HW_READY_STATE	0x07

#define SMI2021_3C_FIRMWARE	"smi2021_3c.bin"
#define SMI2021_3E_FIRMWARE	"smi2021_3e.bin"
#define SMI2021_3F_FIRMWARE	"smi2021_3f.bin"

static unsigned int firmware_version;
module_param(firmware_version, int, 0644);
MODULE_PARM_DESC(firmware_version,
			"Select what firmware to upload\n"
			"accepted values: 0x3c, 0x3e, 0x3f");

static int smi2021_load_firmware(struct usb_device *udev,
					const struct firmware *firmware)
{
	int i, size, rc;
	struct smi2021_set_hw_state *hw_state;
	u8 *chunk;

	size = FIRMWARE_CHUNK_SIZE + FIRMWARE_HEADER_SIZE;

	chunk = kzalloc(size, GFP_KERNEL);
	if (!chunk) {
		dev_err(&udev->dev,
			"could not allocate space for firmware chunk\n");
		rc = -ENOMEM;
		goto end_out;
	}

	hw_state = kzalloc(sizeof(*hw_state), GFP_KERNEL);
	if (!hw_state) {
		dev_err(&udev->dev, "could not allocate space for usb data\n");
		rc = -ENOMEM;
		goto free_out;
	}

	if (!firmware) {
		dev_err(&udev->dev, "firmware is NULL\n");
		rc = -ENODEV;
		goto free_out;
	}

	if (firmware->size % FIRMWARE_CHUNK_SIZE) {
		dev_err(&udev->dev, "firmware has wrong size\n");
		rc = -ENODEV;
		goto free_out;
	}

	rc = usb_control_msg(udev, usb_rcvctrlpipe(udev, SMI2021_USB_RCVPIPE),
			SMI2021_USB_REQUEST,
			USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			FIRMWARE_HW_STATE_HEAD, SMI2021_USB_INDEX,
			hw_state, sizeof(*hw_state), 1000);

	if (rc < 0 || hw_state->state != FIRMWARE_HW_READY_STATE) {
		dev_err(&udev->dev,
			"device is not ready for firmware upload: %d\n", rc);
		goto free_out;
	}

	chunk[0] = FIRMWARE_CHUNK_HEAD_0;
	chunk[1] = FIRMWARE_CHUNK_HEAD_1;

	for (i = 0; i < firmware->size / FIRMWARE_CHUNK_SIZE; i++) {
		memcpy(chunk + FIRMWARE_HEADER_SIZE,
			firmware->data + (i * FIRMWARE_CHUNK_SIZE),
			FIRMWARE_CHUNK_SIZE);

		rc = usb_control_msg(udev,
			usb_sndctrlpipe(udev, SMI2021_USB_SNDPIPE),
			SMI2021_USB_REQUEST,
			USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			FIRMWARE_CHUNK_HEAD_0, SMI2021_USB_INDEX,
			chunk, size, 1000);
		if (rc < 0) {
			dev_err(&udev->dev, "firmware upload failed: %d\n",
				rc);
			goto free_out;
		}
	}

	hw_state->head = FIRMWARE_HW_READY_STATE;
	hw_state->state = 0x00;
	rc = usb_control_msg(udev, usb_sndctrlpipe(udev, SMI2021_USB_SNDPIPE),
			SMI2021_USB_REQUEST,
			USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			FIRMWARE_HW_READY_STATE, SMI2021_USB_INDEX,
			hw_state, sizeof(*hw_state), 1000);

	if (rc < 0) {
		dev_err(&udev->dev, "device failed to ack firmware: %d\n", rc);
		goto free_out;
	}

	rc = 0;

free_out:
	kfree(chunk);
	kfree(hw_state);
end_out:
	return rc;
}


/*
 * There are atleast three different hardware versions of the smi2021 devices
 * that require different firmwares.
 * Before the firmware is loaded, they all report the same usb product id,
 * so I don't know of any way to tell what device the user just plugged.
 * If we only find one smi2021 firmware,
 * we can probably asume it's correct for the device.
 *
 * Users with multiple different firmwares/devices
 * will have to specify the version in /sysfs before plugging in each device.
 */

int smi2021_bootloader_probe(struct usb_interface *intf,
					const struct usb_device_id *devid)
{
	struct usb_device *udev = interface_to_usbdev(intf);

	const struct firmware *firmware;
	int i, rc = 0;

	struct smi2021_versions {
		unsigned int	id;
		const char	*name;
	} static const hw_versions[3] = {
		{
			.id = 0x3f,
			.name = SMI2021_3F_FIRMWARE,
		},
		{
			.id = 0x3e,
			.name = SMI2021_3E_FIRMWARE,
		},
		{
			.id = 0x3c,
			.name = SMI2021_3C_FIRMWARE,
		}
	};

	for (i = 0; i < ARRAY_SIZE(hw_versions); i++) {
		if (firmware_version && firmware_version != hw_versions[i].id)
			continue;

		dev_info(&udev->dev, "Looking for: %s\n", hw_versions[i].name);

		rc = request_firmware_direct(&firmware,	hw_versions[i].name,
								&udev->dev);

		if (rc == 0) {
			dev_info(&udev->dev, "Found firmware for 0x00%x\n",
							hw_versions[i].id);
			goto load_fw;
		}
	}
	if (firmware_version)
		dev_err(&udev->dev,
		"the specified firmware for this device could not be loaded\n");
	else
		dev_err(&udev->dev,
			"could not load any firmware for this device\n");
	return rc;

load_fw:
	rc = smi2021_load_firmware(udev, firmware);

	if (rc < 0)
		dev_err(&udev->dev, "firmware upload failed\n");

	release_firmware(firmware);
	return rc;
}

MODULE_FIRMWARE(SMI2021_3C_FIRMWARE);
MODULE_FIRMWARE(SMI2021_3E_FIRMWARE);
MODULE_FIRMWARE(SMI2021_3F_FIRMWARE);
