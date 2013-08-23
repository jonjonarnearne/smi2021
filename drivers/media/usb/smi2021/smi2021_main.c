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

static void smi2021_release(struct v4l2_device *v4l2_dev)
{
	struct smi2021 *smi2021 = container_of(v4l2_dev, struct smi2021,
								v4l2_dev);
	v4l2_device_unregister(&smi2021->v4l2_dev);
	vb2_queue_release(&smi2021->vb2q);
	kfree(smi2021);
}

/******************************************************************************/
/*                                                                            */
/*          DEVICE  -  PROBE   &   DISCONNECT                                 */
/*                                                                            */
/******************************************************************************/

static const struct usb_device_id smi2021_usb_device_id_table[] = {
	{ USB_DEVICE(VENDOR_ID, BOOTLOADER_ID)	},
	{ USB_DEVICE(VENDOR_ID, 0x003c)		},
	{ USB_DEVICE(VENDOR_ID, 0x003d)		},
	{ USB_DEVICE(VENDOR_ID, 0x003e)		},
	{ USB_DEVICE(VENDOR_ID, 0x003f)		},
	{ }
};
MODULE_DEVICE_TABLE(usb, smi2021_usb_device_id_table);

static int smi2021_usb_probe(struct usb_interface *intf,
					const struct usb_device_id *devid)
{
	int rc, size;
	struct device *dev = &intf->dev;
	struct usb_device *udev = interface_to_usbdev(intf);
	struct smi2021 *smi2021;

	if (udev->descriptor.idProduct == BOOTLOADER_ID)
		return smi2021_bootloader_probe(intf, devid);

	if (intf->num_altsetting != 3)
		return -ENODEV;
	if (intf->altsetting[2].desc.bNumEndpoints != 1)
		return -ENODEV;

	size = usb_endpoint_maxp(&intf->altsetting[2].endpoint[0].desc);
	size = (size & 0x07ff) * (((size & 0x1800) >> 11) + 1);
	dev_info(dev, "Size = %d\n", size);

	smi2021 = kzalloc(sizeof(struct smi2021), GFP_KERNEL);
	if (dev == NULL)
		return -ENOMEM;

	smi2021->dev = dev;
	smi2021->udev = usb_get_dev(udev);

	/* videobuf2 struct and locks */
	smi2021->cur_norm = V4L2_STD_NTSC;
	smi2021->cur_height = SMI2021_NTSC_LINES;

	spin_lock_init(&smi2021->buf_lock);
	mutex_init(&smi2021->v4l2_lock);
	mutex_init(&smi2021->vb2q_lock);
	INIT_LIST_HEAD(&smi2021->bufs);

	rc = smi2021_vb2_setup(smi2021);
	if (rc < 0) {
		dev_warn(dev, "Could not initialize videobuf2 queue\n");
		goto smi2021_fail;
	}

	/* v4l2 struct */
	smi2021->v4l2_dev.release = smi2021_release;
	rc = v4l2_device_register(dev, &smi2021->v4l2_dev);
	if (rc < 0) {
		dev_warn(dev, "Could not register v4l2 device\n");
		goto v4l2_fail;
	}

	usb_set_intfdata(intf, smi2021);

	/* video structure */
	rc = smi2021_video_register(smi2021);
	if (rc < 0) {
		dev_warn(dev, "Could not register video device\n");
		goto vdev_fail;
	}

	dev_info(dev, "Somagic Easy-Cap Video Grabber\n");
	return 0;

vdev_fail:
	v4l2_device_unregister(&smi2021->v4l2_dev);
v4l2_fail:
	vb2_queue_release(&smi2021->vb2q);
smi2021_fail:
	kfree(smi2021);

	return rc;

}

static void smi2021_usb_disconnect(struct usb_interface *intf)
{
	struct smi2021 *smi2021;
	struct usb_device *udev = interface_to_usbdev(intf);

	if (udev->descriptor.idProduct == BOOTLOADER_ID)
		return smi2021_bootloader_disconnect(intf);

	smi2021 = usb_get_intfdata(intf);

	mutex_lock(&smi2021->vb2q_lock);
	mutex_lock(&smi2021->v4l2_lock);

	usb_set_intfdata(intf, NULL);
	video_unregister_device(&smi2021->vdev);
	v4l2_device_disconnect(&smi2021->v4l2_dev);
	usb_put_dev(smi2021->udev);
	smi2021->udev = NULL;

	mutex_unlock(&smi2021->v4l2_lock);
	mutex_unlock(&smi2021->vb2q_lock);

	v4l2_device_put(&smi2021->v4l2_dev);	
}

/******************************************************************************/
/*                                                                            */
/*            MODULE  -  INIT  &  EXIT                                        */
/*                                                                            */
/******************************************************************************/

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jon Arne Jørgensen <jonjon.arnearne--a.t--gmail.com>");
MODULE_DESCRIPTION("SMI2021 - EasyCap");
MODULE_VERSION(SMI2021_DRIVER_VERSION);

struct usb_driver smi2021_usb_driver = {
	.name = "smi2021",
	.id_table = smi2021_usb_device_id_table,
	.probe = smi2021_usb_probe,
	.disconnect = smi2021_usb_disconnect
};

module_usb_driver(smi2021_usb_driver);
