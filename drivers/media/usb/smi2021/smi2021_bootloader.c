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
			"Firmware version to be uploaded to device\n"
			"if there are more than one firmware present");

struct smi2021_firmware {
	int		id;
	const char	*name;
	int		found;
};

struct smi2021_firmware available_fw[] = {
	{
		.id = 0x3c,
		.name = SMI2021_3C_FIRMWARE,
	},
	{
		.id = 0x3e,
		.name = SMI2021_3E_FIRMWARE,
	},
	{
		.id = 0x3f,
		.name = SMI2021_3F_FIRMWARE,
	}
};

static const struct firmware *firmware[ARRAY_SIZE(available_fw)];
static int firmwares = -1;

static int smi2021_load_firmware(struct usb_device *udev,
					const struct firmware *firmware)
{
	int i, size, rc;
	struct smi2021_set_hw_state *hw_state;
	u8 *chunk;

	size = FIRMWARE_CHUNK_SIZE + FIRMWARE_HEADER_SIZE;
	chunk = kzalloc(size, GFP_KERNEL);

	if (chunk == NULL) {
		dev_err(&udev->dev,
			"could not allocate space for firmware chunk\n");
		rc = -ENOMEM;
		goto end_out;
	}

	hw_state = kzalloc(sizeof(*hw_state), GFP_KERNEL);
	if (hw_state == NULL) {
		dev_err(&udev->dev, "could not allocate space for usb data\n");
		rc = -ENOMEM;
		goto free_out;
	}

	if (firmware == NULL) {
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

static int smi2021_choose_firmware(struct usb_device *udev)
{
	int i, found, id;
	for (i = 0; i < ARRAY_SIZE(available_fw); i++) {
		found = available_fw[i].found;
		id = available_fw[i].id;
		if (firmware_version == id && found >= 0) {
			dev_info(&udev->dev, "uploading firmware for 0x%x\n",
					id);
			return smi2021_load_firmware(udev, firmware[found]);
		}
	}

	dev_info(&udev->dev,
	"could not decide what firmware to upload, user action required\n");
	return 0;
}

int smi2021_bootloader_probe(struct usb_interface *intf,
					const struct usb_device_id *devid)
{
	struct usb_device *udev = interface_to_usbdev(intf);
	int rc, i;

	/* Check what firmwares are available in the system */
	for (i = 0; i < ARRAY_SIZE(available_fw); i++) {
		dev_info(&udev->dev, "Looking for: %s\n",
			 available_fw[i].name);
		rc = request_firmware(&firmware[firmwares + 1],
			available_fw[i].name, &udev->dev);

		if (rc == 0) {
			firmwares++;
			available_fw[i].found = firmwares;
			dev_info(&udev->dev, "Found firmware for 0x00%x\n",
				available_fw[i].id);
		} else if (rc == -ENOENT) {
			available_fw[i].found = -1;
		} else {
			dev_err(&udev->dev,
				"request_firmware failed with: %d\n", rc);
			goto err_out;
		}
	}

	if (firmwares < 0) {
		dev_err(&udev->dev,
			"could not find any firmware for this device\n");
		goto no_dev;
	} else if (firmwares == 0) {
		rc = smi2021_load_firmware(udev, firmware[0]);
		if (rc < 0)
			goto err_out;
	} else {
		smi2021_choose_firmware(udev);
	}

	return 0;

no_dev:
	rc = -ENODEV;
err_out:
	return rc;
}

void smi2021_bootloader_disconnect(struct usb_interface *intf)
{
	struct usb_device *udev = interface_to_usbdev(intf);
	int i;

	for (i = 0; i < ARRAY_SIZE(available_fw); i++) {
		if (available_fw[i].found >= 0) {
			dev_info(&udev->dev, "Releasing firmware for 0x00%x\n",
							available_fw[i].id);
			release_firmware(firmware[available_fw[i].found]);
			firmware[available_fw[i].found] = NULL;
			available_fw[i].found = -1;
		}
	}
	firmwares = -1;

}

MODULE_FIRMWARE(SMI2021_3C_FIRMWARE);
MODULE_FIRMWARE(SMI2021_3E_FIRMWARE);
MODULE_FIRMWARE(SMI2021_3F_FIRMWARE);
