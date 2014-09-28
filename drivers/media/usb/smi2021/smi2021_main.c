/************************************************************************
 * smi2021_main.c							*
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

#define VENDOR_ID 0x1c88
#define BOOTLOADER_ID 0x0007

#define SMI2021_MODE_CTRL_HEAD		0x01
#define SMI2021_MODE_CAPTURE		0x05
#define SMI2021_MODE_STANDBY		0x03
#define SMI2021_REG_CTRL_HEAD		0x0b

static int smi2021_set_mode(struct smi2021 *smi2021, u8 mode)
{
	int pipe, rc;
	struct mode_ctrl_transfer {
		u8 head;
		u8 mode;
	} *transfer_buf;

	transfer_buf = kzalloc(sizeof(*transfer_buf), GFP_KERNEL);
	if (!transfer_buf)
		return -ENOMEM;

	transfer_buf->head = SMI2021_MODE_CTRL_HEAD;
	transfer_buf->mode = mode;

	pipe = usb_sndctrlpipe(smi2021->udev, SMI2021_USB_SNDPIPE);
	rc = usb_control_msg(smi2021->udev, pipe, SMI2021_USB_REQUEST,
			USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			transfer_buf->head, SMI2021_USB_INDEX,
			transfer_buf, sizeof(*transfer_buf), 1000);

	kfree(transfer_buf);

	return rc;
}

/*
 * The smi2021 chip will handle two different types of register settings.
 * Settings for the gm7113c chip via i2c or settings for the smi2021 chip.
 * All settings are passed with the following struct.
 * Some bits in data_offset and data_cntl parameters tells the device what
 * kind of setting it's receiving and if it's a read or write request.
 */
struct smi2021_reg_ctrl_transfer {
	u8 head;
	u8 i2c_addr;
	u8 data_cntl;
	u8 data_offset;
	u8 data_size;
	union data {
		u8 val;
		struct i2c_data {
			u8 reg;
			u8 val;
		} __packed i2c_data;
		struct smi_data {
			__be16 reg;
			u8 val;
		} __packed smi_data;
		u8 reserved[8];
	} __packed data;
} __packed;

static int smi2021_set_reg(struct smi2021 *smi2021, u8 i2c_addr,
			   u16 reg, u8 val)
{
	struct smi2021_reg_ctrl_transfer *transfer_buf;
	int rc, pipe;

	static const struct smi2021_reg_ctrl_transfer smi_data = {
		.head = SMI2021_REG_CTRL_HEAD,
		.i2c_addr = 0x00,
		.data_cntl = 0x00,
		.data_offset = 0x82,
		.data_size = sizeof(u8),
	};

	static const struct smi2021_reg_ctrl_transfer i2c_data = {
		.head = SMI2021_REG_CTRL_HEAD,
		.i2c_addr = 0x00,
		.data_cntl = 0xc0,
		.data_offset = 0x01,
		.data_size = sizeof(u8)
	};

	if (!smi2021->udev) {
		rc = -ENODEV;
		goto out;
	}

	transfer_buf = kzalloc(sizeof(*transfer_buf), GFP_KERNEL);
	if (!transfer_buf) {
		rc = -ENOMEM;
		goto out;
	}

	if (i2c_addr) {
		memcpy(transfer_buf, &i2c_data, sizeof(*transfer_buf));
		transfer_buf->i2c_addr = i2c_addr;
		transfer_buf->data.i2c_data.reg = reg;
		transfer_buf->data.i2c_data.val = val;
	} else {
		memcpy(transfer_buf, &smi_data, sizeof(*transfer_buf));
		transfer_buf->data.smi_data.reg = cpu_to_be16(reg);
		transfer_buf->data.smi_data.val = val;
	}

	pipe = usb_sndctrlpipe(smi2021->udev, SMI2021_USB_SNDPIPE);
	rc = usb_control_msg(smi2021->udev, pipe, SMI2021_USB_REQUEST,
			USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			transfer_buf->head, SMI2021_USB_INDEX,
			transfer_buf, sizeof(*transfer_buf), 1000);

	kfree(transfer_buf);
out:
	return rc;
}

static int smi2021_get_reg(struct smi2021 *smi2021, u8 i2c_addr,
			   u16 reg, u8 *val)
{
	int rc, pipe;
	struct smi2021_reg_ctrl_transfer *transfer_buf;

	static const struct smi2021_reg_ctrl_transfer i2c_prepare_read = {
		.head = SMI2021_REG_CTRL_HEAD,
		.i2c_addr = 0x00,
		.data_cntl = 0x84,
		.data_offset = 0x00,
		.data_size = sizeof(u8)
	};

	static const struct smi2021_reg_ctrl_transfer smi_read = {
		.head = SMI2021_REG_CTRL_HEAD,
		.i2c_addr = 0x00,
		.data_cntl = 0x20,
		.data_offset = 0x82,
		.data_size = sizeof(u8)
	};

	*val = 0;

	if (!smi2021->udev) {
		rc = -ENODEV;
		goto out;
	}

	transfer_buf = kzalloc(sizeof(*transfer_buf), GFP_KERNEL);
	if (!transfer_buf) {
		rc = -ENOMEM;
		goto out;
	}

	pipe = usb_sndctrlpipe(smi2021->udev, SMI2021_USB_SNDPIPE);

	if (i2c_addr) {
		memcpy(transfer_buf, &i2c_prepare_read, sizeof(*transfer_buf));
		transfer_buf->i2c_addr = i2c_addr;
		transfer_buf->data.i2c_data.reg = reg;

		rc = usb_control_msg(smi2021->udev, pipe, SMI2021_USB_REQUEST,
			USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			transfer_buf->head, SMI2021_USB_INDEX,
			transfer_buf, sizeof(*transfer_buf), 1000);
		if (rc < 0)
			goto free_out;

		transfer_buf->data_cntl = 0xa0;
	} else {
		memcpy(transfer_buf, &smi_read, sizeof(*transfer_buf));
		transfer_buf->data.smi_data.reg = cpu_to_be16(reg);
	}

	rc = usb_control_msg(smi2021->udev, pipe, SMI2021_USB_REQUEST,
			USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			transfer_buf->head, SMI2021_USB_INDEX,
			transfer_buf, sizeof(*transfer_buf), 1000);
	if (rc < 0)
		goto free_out;

	pipe = usb_rcvctrlpipe(smi2021->udev, SMI2021_USB_RCVPIPE);
	rc = usb_control_msg(smi2021->udev, pipe, SMI2021_USB_REQUEST,
			USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			transfer_buf->head, SMI2021_USB_INDEX,
			transfer_buf, sizeof(*transfer_buf), 1000);
	if (rc < 0)
		goto free_out;

	*val = transfer_buf->data.val;

free_out:
	kfree(transfer_buf);
out:
	return rc;
}

static int smi2021_i2c_xfer(struct i2c_adapter *i2c_adap,
				struct i2c_msg msgs[], int num)
{
	struct smi2021 *smi2021 = i2c_adap->algo_data;

	switch (num) {
	case 2:  /* Read reg */
		if (msgs[0].len != 1 || msgs[1].len != 1)
			goto err_out;

		if ((msgs[1].flags & I2C_M_RD) != I2C_M_RD)
			goto err_out;
		smi2021_get_reg(smi2021, msgs[0].addr, msgs[0].buf[0],
							msgs[1].buf);
		break;
	case 1: /* Write reg */
		if (msgs[0].len == 0)
			break;
		else if (msgs[0].len != 2)
			goto err_out;
		if (msgs[0].buf[0] == 0)
			break;
		smi2021_set_reg(smi2021, msgs[0].addr, msgs[0].buf[0],
							msgs[0].buf[1]);
		break;
	default:
		goto err_out;
	}
	return num;

err_out:
	return -EOPNOTSUPP;
}

static u32 smi2021_i2c_functionality(struct i2c_adapter *adap)
{
	return I2C_FUNC_SMBUS_EMUL;
}

static int smi2021_initialize(struct smi2021 *smi2021)
{
	int i, rc;

	/*
	 * These registers initializes the smi2021 chip,
	 * but I have not been able to figure out exactly what they do.
	 * My guess is that they toggle the reset pins of the
	 * cs5350 and gm7113c chips.
	 */
	static u8 init[][2] = {
		{ 0x3a, 0x80 },
		{ 0x3b, 0x00 },
		{ 0x34, 0x01 },
		{ 0x35, 0x00 },
		{ 0x34, 0x11 },
		{ 0x35, 0x11 },
		{ 0x3b, 0x80 },
		{ 0x3b, 0x00 },
	};

	for (i = 0; i < ARRAY_SIZE(init); i++) {
		rc = smi2021_set_reg(smi2021, 0x00, init[i][0], init[i][1]);
		if (rc < 0)
			return rc;
	}

	return 0;
}

static struct smi2021_buf *smi2021_get_buf(struct smi2021 *smi2021)
{
	unsigned long flags;
	struct smi2021_buf *buf = NULL;

	spin_lock_irqsave(&smi2021->buf_lock, flags);
	if (list_empty(&smi2021->bufs)) {
		/* No free buffers, userspace likely to slow! */
		spin_unlock_irqrestore(&smi2021->buf_lock, flags);
		return NULL;
	}
	buf = list_first_entry(&smi2021->bufs, struct smi2021_buf, list);
	list_del(&buf->list);
	spin_unlock_irqrestore(&smi2021->buf_lock, flags);

	return buf;
}

static void smi2021_buf_done(struct smi2021 *smi2021)
{
	struct smi2021_buf *buf = smi2021->cur_buf;

	v4l2_get_timestamp(&buf->vb.v4l2_buf.timestamp);
	buf->vb.v4l2_buf.sequence = smi2021->sequence++;
	buf->vb.v4l2_buf.field = V4L2_FIELD_INTERLACED;

	if (buf->pos < (SMI2021_BYTES_PER_LINE * smi2021->cur_height)) {
		vb2_set_plane_payload(&buf->vb, 0, 0);
		vb2_buffer_done(&buf->vb, VB2_BUF_STATE_ERROR);
	} else {
		vb2_set_plane_payload(&buf->vb, 0, buf->pos);
		vb2_buffer_done(&buf->vb, VB2_BUF_STATE_DONE);
	}

	smi2021->cur_buf = NULL;
}

#define is_sav(trc)						\
	((trc & SMI2021_TRC_EAV) == 0x00)
#define is_field2(trc)						\
	((trc & SMI2021_TRC_FIELD_2) == SMI2021_TRC_FIELD_2)
#define is_active_video(trc)					\
	((trc & SMI2021_TRC_VBI) == 0x00)
/*
 * Parse the TRC.
 * Grab a new buffer from the queue if don't have one
 * and we are recieving the start of a video frame.
 *
 * Mark video buffers as done if we have one full frame.
 */
static void parse_trc(struct smi2021 *smi2021, u8 trc)
{
	struct smi2021_buf *buf = smi2021->cur_buf;
	int lines_per_field = smi2021->cur_height / 2;
	int line = 0;

	if (!buf) {
		if (!is_sav(trc))
			return;

		if (!is_active_video(trc))
			return;

		if (is_field2(trc))
			return;

		buf = smi2021_get_buf(smi2021);
		if (!buf)
			return;

		smi2021->cur_buf = buf;
	}

	if (is_sav(trc)) {
		/* Start of VBI or ACTIVE VIDEO */
		if (is_active_video(trc)) {
			buf->in_blank = false;
			buf->trc_av++;
		} else {
			/* VBI */
			buf->in_blank = true;
		}

		if (!buf->second_field && is_field2(trc)) {
			line = buf->pos / SMI2021_BYTES_PER_LINE;
			if (line < lines_per_field)
				goto buf_done;

			buf->second_field = true;
			buf->trc_av = 0;
		}

		if (buf->second_field && !is_field2(trc))
			goto buf_done;
	} else {
		/* End of VBI or ACTIVE VIDEO */
		buf->in_blank = true;
	}

	return;

buf_done:
	smi2021_buf_done(smi2021);
}

static void copy_video(struct smi2021 *smi2021, u8 p)
{
	struct smi2021_buf *buf = smi2021->cur_buf;

	int lines_per_field = smi2021->cur_height / 2;
	int line = 0;
	int pos_in_line = 0;
	unsigned int offset = 0;
	u8 *dst;

	if (!buf)
		return;

	if (buf->in_blank)
		return;

	if (buf->pos >= buf->length) {
		smi2021_buf_done(smi2021);
		return;
	}

	pos_in_line = buf->pos % SMI2021_BYTES_PER_LINE;
	line = buf->pos / SMI2021_BYTES_PER_LINE;
	if (line >= lines_per_field)
			line -= lines_per_field;

	if (line != buf->trc_av - 1) {
		/* Keep video synchronized.
		 * The device will sometimes give us too many bytes
		 * for a line, before we get a new TRC.
		 * We just drop these bytes */
		return;
	}

	if (buf->second_field)
		offset += SMI2021_BYTES_PER_LINE;

	offset += (SMI2021_BYTES_PER_LINE * line * 2) + pos_in_line;

	/* Will this ever happen? */
	if (offset >= buf->length)
		return;

	dst = buf->mem + offset;
	*dst = p;
	buf->pos++;
}

/*
 * Scan the saa7113 Active video data.
 * This data is:
 *	4 bytes header (0xff 0x00 0x00 [TRC/SAV])
 *	1440 bytes of UYUV Video data
 *	4 bytes footer (0xff 0x00 0x00 [TRC/EAV])
 *
 * TRC = Time Reference Code.
 * SAV = Start Active Video.
 * EAV = End Active Video.
 * This is described in the saa7113 datasheet.
 */
static void parse_video(struct smi2021 *smi2021, u8 *p, int size)
{
	int i;

	for (i = 0; i < size; i++) {
		switch (smi2021->sync_state) {
		case HSYNC:
			if (p[i] == 0xff)
				smi2021->sync_state = SYNCZ1;
			else
				copy_video(smi2021, p[i]);
			break;
		case SYNCZ1:
			if (p[i] == 0x00) {
				smi2021->sync_state = SYNCZ2;
			} else {
				smi2021->sync_state = HSYNC;
				copy_video(smi2021, 0xff);
				copy_video(smi2021, p[i]);
			}
			break;
		case SYNCZ2:
			if (p[i] == 0x00) {
				smi2021->sync_state = TRC;
			} else {
				smi2021->sync_state = HSYNC;
				copy_video(smi2021, 0xff);
				copy_video(smi2021, 0x00);
				copy_video(smi2021, p[i]);
			}
			break;
		case TRC:
			smi2021->sync_state = HSYNC;
			parse_trc(smi2021, p[i]);
			break;
		}
	}
}

/*
 * The device delivers data in chunks of 0x400 bytes.
 * The four first bytes is a magic header to identify the chunks.
 *	0xaa 0xaa 0x00 0x00 = saa7113 Active Video Data
 *	0xaa 0xaa 0x00 0x01 = PCM - 24Bit 2 Channel audio data
 */
static void process_packet(struct smi2021 *smi2021, u8 *p, int size)
{
	int i;
	u32 *header;

	if (size % 0x400 != 0) {
		printk_ratelimited(KERN_INFO "smi2021::%s: size: %d\n",
				__func__, size);
		return;
	}

	for (i = 0; i < size; i += 0x400) {
		header = (u32 *)(p + i);
		switch (*header) {
		case cpu_to_be32(0xaaaa0000):
			spin_lock(&smi2021->slock);
			parse_video(smi2021, p+i+4, 0x400-4);
			spin_unlock(&smi2021->slock);
			break;
		case cpu_to_be32(0xaaaa0001):
			smi2021_audio(smi2021, p+i+4, 0x400-4);
			break;
		}
	}
}

static void smi2021_iso_cb(struct urb *ip)
{
	struct smi2021 *smi2021 = ip->context;
	int i;

	switch (ip->status) {
	case 0:
	/* All fine */
		break;
	/* Device disconnected or capture stopped? */
	case -ENODEV:
	case -ENOENT:
	case -ECONNRESET:
	case -ESHUTDOWN:
		return;
	/* Unknown error, retry */
	default:
		dev_warn(smi2021->dev, "urb error! status %d\n", ip->status);
		goto resubmit;
	}

	for (i = 0; i < ip->number_of_packets; i++) {
		int size = ip->iso_frame_desc[i].actual_length;
		unsigned char *data = ip->transfer_buffer +
				ip->iso_frame_desc[i].offset;

		process_packet(smi2021, data, size);

		ip->iso_frame_desc[i].status = 0;
		ip->iso_frame_desc[i].actual_length = 0;
	}

resubmit:
	ip->status = 0;

	if (!atomic_read(&smi2021->running))
		return;

	ip->status = usb_submit_urb(ip, GFP_ATOMIC);
	if (ip->status)
		dev_warn(smi2021->dev, "urb re-submit failed (%d)\n", ip->status);

}

static struct urb *smi2021_setup_iso_transfer(struct smi2021 *smi2021)
{
	struct urb *ip;
	int i, size = smi2021->iso_size;

	ip = usb_alloc_urb(SMI2021_ISOC_PACKETS, GFP_KERNEL);
	if (!ip)
		return NULL;

	ip->dev = smi2021->udev;
	ip->context = smi2021;
	ip->pipe = usb_rcvisocpipe(smi2021->udev, SMI2021_ISOC_EP);
	ip->interval = 1;
	ip->transfer_flags = URB_ISO_ASAP;
	ip->transfer_buffer = kzalloc(SMI2021_ISOC_PACKETS * size, GFP_KERNEL);
	ip->complete = smi2021_iso_cb;
	ip->number_of_packets = SMI2021_ISOC_PACKETS;
	ip->transfer_buffer_length = SMI2021_ISOC_PACKETS * size;
	for (i = 0; i < SMI2021_ISOC_PACKETS; i++) {
		ip->iso_frame_desc[i].offset = size * i;
		ip->iso_frame_desc[i].length = size;
	}

	return ip;
}

void smi2021_toggle_audio(struct smi2021 *smi2021, bool enable)
{
	/*
	 * I know that setting this register enables and disables
	 * the transfer of audio data over usb.
	 * I have no idea about what the number 0x1d really represents.
	 */

	if (enable)
		smi2021_set_reg(smi2021, 0, 0x1740, 0x1d);
	else
		smi2021_set_reg(smi2021, 0, 0x1740, 0x00);
}

int smi2021_start(struct smi2021 *smi2021)
{
	int i, rc;
	u8 reg;
	smi2021->sync_state = HSYNC;

	v4l2_subdev_call(smi2021->gm7113c_subdev, video, s_stream, 1);

	/*
	 * Enble automatic field detection on gm7113c (Bit 7)
	 * It seems the device needs this to not fail when receiving bad video
	 * i.e. from an old VHS tape.
	 */
	smi2021_get_reg(smi2021, 0x4a, 0x08, &reg);
	smi2021_set_reg(smi2021, 0x4a, 0x08, reg | 0x80);

	/*
	 * Reset RTSO0 6 Times (Bit 7)
	 * The Windows driver does this, not sure if it's really needed.
	 */
	smi2021_get_reg(smi2021, 0x4a, 0x0e, &reg);
	reg |= 0x80;
	for (i = 0; i < 6; i++)
		smi2021_set_reg(smi2021, 0x4a, 0x0e, reg);

	rc = smi2021_set_mode(smi2021, SMI2021_MODE_CAPTURE);
	if (rc < 0)
		goto start_fail;

	rc = usb_set_interface(smi2021->udev, 0, 2);
	if (rc < 0)
		goto start_fail;

	smi2021_toggle_audio(smi2021, false);

	for (i = 0; i < SMI2021_ISOC_TRANSFERS; i++) {
		struct urb *ip;

		ip = smi2021_setup_iso_transfer(smi2021);
		if (!ip) {
			rc = -ENOMEM;
			goto start_fail;
		}
		smi2021->isoc_urbs[i] = ip;
		rc = usb_submit_urb(ip, GFP_KERNEL);
		if (rc < 0)
			goto start_fail;
	}

	/* I have no idea about what this register does with this value. */
	smi2021_set_reg(smi2021, 0, 0x1800, 0x0d);

	atomic_set(&smi2021->running, 1);

	return 0;

start_fail:
	smi2021_stop(smi2021);

	return rc;

}

void smi2021_stop(struct smi2021 *smi2021)
{
	int i;
	atomic_set(&smi2021->running, 0);

	/* Cancel running transfers */
	for (i = 0; i < SMI2021_ISOC_TRANSFERS; i++) {
		struct urb *ip = smi2021->isoc_urbs[i];
		if (!ip)
			continue;
		usb_kill_urb(ip);
		kfree(ip->transfer_buffer);
		usb_free_urb(ip);
		smi2021->isoc_urbs[i] = NULL;
	}

	usb_set_interface(smi2021->udev, 0, 0);
	smi2021_set_mode(smi2021, SMI2021_MODE_STANDBY);

	smi2021_stop_audio(smi2021);

	return;
}

static void smi2021_release(struct v4l2_device *v4l2_dev)
{
	struct smi2021 *smi2021 = container_of(v4l2_dev, struct smi2021,
								v4l2_dev);
	i2c_del_adapter(&smi2021->i2c_adap);

	v4l2_ctrl_handler_free(&smi2021->ctrl_handler);
	v4l2_device_unregister(&smi2021->v4l2_dev);
	vb2_queue_release(&smi2021->vb2q);
	kfree(smi2021);
}


/*
 *	DEVICE  -  PROBE   &   DISCONNECT
 */

static const struct usb_device_id smi2021_usb_device_id_table[] = {
	{ USB_DEVICE(VENDOR_ID, BOOTLOADER_ID)	},
	{ USB_DEVICE(VENDOR_ID, 0x003c)		},
	{ USB_DEVICE(VENDOR_ID, 0x003d)		},
	{ USB_DEVICE(VENDOR_ID, 0x003e)		},
	{ USB_DEVICE(VENDOR_ID, 0x003f)		},
	{ }
};
MODULE_DEVICE_TABLE(usb, smi2021_usb_device_id_table);

static const struct smi2021_vid_input dual_input[] = {
	{
		.name = "Composite",
		.type = SAA7115_COMPOSITE0,
	},
	{
		.name = "S-Video",
		.type = SAA7115_SVIDEO1,
	}
};

static const struct smi2021_vid_input quad_input[] = {
	{
		.name = "Composite 0",
		.type = SAA7115_COMPOSITE0,
	},
	{
		.name = "Composite 1",
		.type = SAA7115_COMPOSITE1,
	},
	{
		.name = "Composite 2",
		.type = SAA7115_COMPOSITE2,
	},
	{
		.name = "Composite 3",
		.type = SAA7115_COMPOSITE3,
	},
};

const static struct i2c_algorithm smi2021_algo = {
	.master_xfer = smi2021_i2c_xfer,
	.functionality = smi2021_i2c_functionality,
};

static int smi2021_usb_probe(struct usb_interface *intf,
					const struct usb_device_id *devid)
{
	int rc, size, input_count;
	const struct smi2021_vid_input *vid_inputs;
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

	switch (udev->descriptor.idProduct) {
	case 0x3e:
	case 0x3f:
		input_count = ARRAY_SIZE(quad_input);
		vid_inputs = quad_input;
		break;
	case 0x3c:
	case 0x3d:
	default:
		input_count = ARRAY_SIZE(dual_input);
		vid_inputs = dual_input;
	}

	smi2021 = kzalloc(sizeof(struct smi2021), GFP_KERNEL);
	if (!smi2021)
		return -ENOMEM;

	smi2021->dev = dev;
	smi2021->udev = usb_get_dev(udev);

	smi2021->vid_input_count = input_count;
	smi2021->vid_inputs = vid_inputs;
	smi2021->iso_size = size;

	/* videobuf2 struct and locks */
	smi2021->cur_norm = V4L2_STD_NTSC;
	smi2021->cur_height = SMI2021_NTSC_LINES;

	spin_lock_init(&smi2021->slock);
	spin_lock_init(&smi2021->buf_lock);
	mutex_init(&smi2021->v4l2_lock);
	mutex_init(&smi2021->vb2q_lock);
	INIT_LIST_HEAD(&smi2021->bufs);

	atomic_set(&smi2021->running, 0);

	rc = smi2021_vb2_setup(smi2021);
	if (rc < 0) {
		dev_warn(dev, "Could not initialize videobuf2 queue\n");
		goto smi2021_fail;
	}

	rc = v4l2_ctrl_handler_init(&smi2021->ctrl_handler, 0);
	if (rc < 0) {
		dev_warn(dev, "Could not initialize v4l2 ctrl handler\n");
		goto ctrl_fail;
	}

	/* v4l2 struct */
	smi2021->v4l2_dev.release = smi2021_release;
	smi2021->v4l2_dev.ctrl_handler = &smi2021->ctrl_handler;
	rc = v4l2_device_register(dev, &smi2021->v4l2_dev);
	if (rc < 0) {
		dev_warn(dev, "Could not register v4l2 device\n");
		goto v4l2_fail;
	}

	smi2021_initialize(smi2021);

	/* i2c adapter */
	strlcpy(smi2021->i2c_adap.name, "smi2021",
				sizeof(smi2021->i2c_adap.name));
	smi2021->i2c_adap.dev.parent = smi2021->dev;
	smi2021->i2c_adap.owner = THIS_MODULE;
	smi2021->i2c_adap.algo = &smi2021_algo;
	smi2021->i2c_adap.algo_data = smi2021;
	i2c_set_adapdata(&smi2021->i2c_adap, &smi2021->v4l2_dev);
	rc = i2c_add_adapter(&smi2021->i2c_adap);
	if (rc < 0) {
		dev_warn(dev, "Could not add i2c adapter\n");
		goto i2c_fail;
	}

	/* i2c client */
	strlcpy(smi2021->i2c_client.name, "smi2021 internal",
				sizeof(smi2021->i2c_client.name));
	smi2021->i2c_client.adapter = &smi2021->i2c_adap;

	/* gm7113c_init table overrides */
	smi2021->gm7113c_overrides.r10_ofts = SAA7113_OFTS_VFLAG_BY_VREF;
	smi2021->gm7113c_overrides.r10_vrln = true;
	smi2021->gm7113c_overrides.r13_adlsb = true;

	smi2021->gm7113c_platform_data.saa7113_r10_ofts = &smi2021->gm7113c_overrides.r10_ofts;
	smi2021->gm7113c_platform_data.saa7113_r10_vrln = &smi2021->gm7113c_overrides.r10_vrln;
	smi2021->gm7113c_platform_data.saa7113_r13_adlsb = &smi2021->gm7113c_overrides.r13_adlsb;

	smi2021->gm7113c_info.addr = 0x4a;
	smi2021->gm7113c_info.platform_data = &smi2021->gm7113c_platform_data;
	strlcpy(smi2021->gm7113c_info.type, "gm7113c",
					sizeof(smi2021->gm7113c_info.type));

	smi2021->gm7113c_subdev = v4l2_i2c_new_subdev_board(&smi2021->v4l2_dev,
							&smi2021->i2c_adap,
							&smi2021->gm7113c_info, NULL);

	v4l2_subdev_call(smi2021->gm7113c_subdev, video, s_routing,
			smi2021->vid_inputs[smi2021->cur_input].type, 0, 0);
	v4l2_subdev_call(smi2021->gm7113c_subdev, video, s_std,
			smi2021->cur_norm);

	usb_set_intfdata(intf, smi2021);
	smi2021_snd_register(smi2021);


	/* video structure */
	rc = smi2021_video_register(smi2021);
	if (rc < 0) {
		dev_warn(dev, "Could not register video device\n");
		goto vdev_fail;
	}

	dev_info(dev, "Somagic Easy-Cap Video Grabber\n");

	return 0;

vdev_fail:
	i2c_del_adapter(&smi2021->i2c_adap);
i2c_fail:
	v4l2_device_unregister(&smi2021->v4l2_dev);
v4l2_fail:
	v4l2_ctrl_handler_free(&smi2021->ctrl_handler);
ctrl_fail:
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
		return;

	smi2021 = usb_get_intfdata(intf);
	smi2021_snd_unregister(smi2021);

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
