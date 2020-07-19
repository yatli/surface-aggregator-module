// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Microsofs Surface HID (VHF) driver for HID input events via SAM.
 * Used for keyboard input events on the 7th generation Surface Laptops.
 */

#include <linux/acpi.h>
#include <linux/hid.h>
#include <linux/input.h>
#include <linux/platform_device.h>
#include <linux/types.h>

#include "surface_sam_ssh.h"
#include "surface_sam_sid_vhf.h"

#define SID_VHF_INPUT_NAME	"Microsoft Surface HID"

#define SAM_EVENT_SID_VHF_TC	0x15

#define VHF_HID_STARTED		0

struct sid_vhf {
	struct platform_device *dev;
	struct ssam_controller *ctrl;
	const struct ssam_hid_properties *p;

	struct ssam_event_notifier notif;

	struct hid_device *hid;
	unsigned long state;
};


static int sid_vhf_hid_start(struct hid_device *hid)
{
	hid_dbg(hid, "%s\n", __func__);
	return 0;
}

static void sid_vhf_hid_stop(struct hid_device *hid)
{
	hid_dbg(hid, "%s\n", __func__);
}

static int sid_vhf_hid_open(struct hid_device *hid)
{
	struct sid_vhf *vhf = dev_get_drvdata(hid->dev.parent);

	hid_dbg(hid, "%s\n", __func__);

	set_bit(VHF_HID_STARTED, &vhf->state);
	return 0;
}

static void sid_vhf_hid_close(struct hid_device *hid)
{

	struct sid_vhf *vhf = dev_get_drvdata(hid->dev.parent);

	hid_dbg(hid, "%s\n", __func__);

	clear_bit(VHF_HID_STARTED, &vhf->state);
}

struct surface_sam_sid_vhf_meta_rqst {
	u8 id;
	u32 offset;
	u32 length; // buffer limit on send, length of data received on receive
	u8 end; // 0x01 if end was reached
} __packed;

struct vhf_device_metadata_info {
	u8 len;
	u8 _2;
	u8 _3;
	u8 _4;
	u8 _5;
	u8 _6;
	u8 _7;
	u16 hid_len; // hid descriptor length
} __packed;

struct vhf_device_metadata {
	u32 len;
	u16 vendor_id;
	u16 product_id;
	u8  _1[24];
} __packed;

union vhf_buffer_data {
	struct vhf_device_metadata_info info;
	u8 pld[0x76];
	struct vhf_device_metadata meta;
};

struct surface_sam_sid_vhf_meta_resp {
	struct surface_sam_sid_vhf_meta_rqst rqst;
	union vhf_buffer_data data;
} __packed;


static int vhf_get_metadata(u8 iid, struct vhf_device_metadata *meta)
{
	struct surface_sam_sid_vhf_meta_resp resp = {};
	struct surface_sam_ssh_rqst rqst;
	struct surface_sam_ssh_buf result;
	int status;

	resp.rqst.id = 2;
	resp.rqst.offset = 0;
	resp.rqst.length = 0x76;
	resp.rqst.end = 0;

	rqst.tc  = 0x15;
	rqst.cid = 0x04;
	rqst.iid = iid;
	rqst.chn = 0x02;
	rqst.snc = 0x01;
	rqst.cdl = sizeof(struct surface_sam_sid_vhf_meta_rqst);
	rqst.pld = (u8 *)&resp.rqst;

	result.cap  = sizeof(struct surface_sam_sid_vhf_meta_resp);
	result.len  = 0;
	result.data = (u8 *)&resp;

	status = surface_sam_ssh_rqst(&rqst, &result);
	if (status)
		return status;

	*meta = resp.data.meta;

	return 0;
}

static int vhf_get_hid_descriptor(struct hid_device *hid, u8 iid, u8 **desc, int *size)
{
	struct surface_sam_sid_vhf_meta_resp resp = {};
	struct surface_sam_ssh_rqst rqst;
	struct surface_sam_ssh_buf result;
	int status, len;
	u8 *buf;

	resp.rqst.id = 0;
	resp.rqst.offset = 0;
	resp.rqst.length = 0x76;
	resp.rqst.end = 0;

	rqst.tc  = 0x15;
	rqst.cid = 0x04;
	rqst.iid = iid;
	rqst.chn = 0x02;
	rqst.snc = 0x01;
	rqst.cdl = sizeof(struct surface_sam_sid_vhf_meta_rqst);
	rqst.pld = (u8 *)&resp.rqst;

	result.cap  = sizeof(struct surface_sam_sid_vhf_meta_resp);
	result.len  = 0;
	result.data = (u8 *)&resp;

	// first fetch 00 to get the total length
	status = surface_sam_ssh_rqst(&rqst, &result);
	if (status)
		return status;

	len = resp.data.info.hid_len;

	// allocate a buffer for the descriptor
	buf = kzalloc(len, GFP_KERNEL);

	// then, iterate and write into buffer, copying out bytes
	resp.rqst.id = 1;
	resp.rqst.offset = 0;
	resp.rqst.length = 0x76;
	resp.rqst.end = 0;

	while (!resp.rqst.end && resp.rqst.offset < len) {
		status = surface_sam_ssh_rqst(&rqst, &result);
		if (status) {
			kfree(buf);
			return status;
		}
		memcpy(buf + resp.rqst.offset, resp.data.pld, resp.rqst.length);

		resp.rqst.offset += resp.rqst.length;
	}

	*desc = buf;
	*size = len;

	return 0;
}

static int sid_vhf_hid_parse(struct hid_device *hid)
{
	struct sid_vhf *vhf = dev_get_drvdata(hid->dev.parent);
	int ret = 0, size;
	u8 *buf;

	ret = vhf_get_hid_descriptor(hid, vhf->p->instance, &buf, &size);
	if (ret != 0) {
		hid_err(hid, "Failed to read HID descriptor from device: %d\n", ret);
		return -EIO;
	}
	hid_dbg(hid, "HID descriptor of device:");
	print_hex_dump_debug("descriptor:", DUMP_PREFIX_OFFSET, 16, 1, buf, size, false);

	ret = hid_parse_report(hid, buf, size);
	kfree(buf);
	return ret;

}

static int sid_vhf_hid_raw_request(struct hid_device *hid, unsigned char
		reportnum, u8 *buf, size_t len, unsigned char rtype, int
		reqtype)
{
	struct sid_vhf *vhf = dev_get_drvdata(hid->dev.parent);
	int status;
	u8 cid;
	struct surface_sam_ssh_rqst rqst = {};
	struct surface_sam_ssh_buf result = {};

	hid_dbg(hid, "%s: reportnum=%#04x rtype=%i reqtype=%i\n", __func__, reportnum, rtype, reqtype);
	print_hex_dump_debug("report:", DUMP_PREFIX_OFFSET, 16, 1, buf, len, false);

	// Byte 0 is the report number. Report data starts at byte 1.
	buf[0] = reportnum;

	switch (rtype) {
	case HID_OUTPUT_REPORT:
		cid = 0x01;
		break;
	case HID_FEATURE_REPORT:
		switch (reqtype) {
		case HID_REQ_GET_REPORT:
			// The EC doesn't respond to GET FEATURE for these touchpad reports
			// we immediately discard to avoid waiting for a timeout.
			if (reportnum == 6 || reportnum == 7 || reportnum == 8 || reportnum == 9 || reportnum == 0x0b) {
				hid_dbg(hid, "%s: skipping get feature report for 0x%02x\n", __func__, reportnum);
				return 0;
			}

			cid = 0x02;
			break;
		case HID_REQ_SET_REPORT:
			cid = 0x03;
			break;
		default:
			hid_err(hid, "%s: unknown req type 0x%02x\n", __func__, rtype);
			return -EIO;
		}
		break;
	default:
		hid_err(hid, "%s: unknown report type 0x%02x\n", __func__, reportnum);
		return -EIO;
	}

	rqst.tc  = SAM_EVENT_SID_VHF_TC;
	rqst.chn = 0x02;
	rqst.iid = vhf->p->instance;
	rqst.cid = cid;
	rqst.snc = reqtype == HID_REQ_GET_REPORT ? 0x01 : 0x00;
	rqst.cdl = reqtype == HID_REQ_GET_REPORT ? 0x01 : len;
	rqst.pld = buf;

	result.cap = len;
	result.len = 0;
	result.data = buf;

	hid_dbg(hid, "%s: sending to cid=%#04x snc=%#04x\n", __func__, cid, HID_REQ_GET_REPORT == reqtype);

	status = surface_sam_ssh_rqst(&rqst, &result);
	hid_dbg(hid, "%s: status %i\n", __func__, status);

	if (status)
		return status;

	if (result.len > 0)
		print_hex_dump_debug("response:", DUMP_PREFIX_OFFSET, 16, 1, result.data, result.len, false);

	return result.len;
}

static struct hid_ll_driver sid_vhf_hid_ll_driver = {
	.start         = sid_vhf_hid_start,
	.stop          = sid_vhf_hid_stop,
	.open          = sid_vhf_hid_open,
	.close         = sid_vhf_hid_close,
	.parse         = sid_vhf_hid_parse,
	.raw_request   = sid_vhf_hid_raw_request,
};


static struct hid_device *sid_vhf_create_hid_device(struct platform_device *pdev, struct vhf_device_metadata *meta)
{
	struct hid_device *hid;

	hid = hid_allocate_device();
	if (IS_ERR(hid))
		return hid;

	hid->dev.parent = &pdev->dev;

	hid->bus     = BUS_VIRTUAL;
	hid->vendor  = meta->vendor_id;
	hid->product = meta->product_id;

	hid->ll_driver = &sid_vhf_hid_ll_driver;

	sprintf(hid->name, "%s", SID_VHF_INPUT_NAME);

	return hid;
}

static u32 sid_vhf_event_handler(struct ssam_notifier_block *nb, const struct ssam_event *event)
{
	struct sid_vhf *vhf = container_of(nb, struct sid_vhf, notif.base);
	int status;

	if (event->target_category != SSAM_SSH_TC_HID)
		return 0;

	if (event->channel != 0x02)
		return 0;

	if (event->instance_id != vhf->p->instance)
		return 0;

	if (event->command_id != 0x00 && event->command_id != 0x03 && event->command_id != 0x04)
		return 0;

	// skip if HID hasn't started yet
	if (!test_bit(VHF_HID_STARTED, &vhf->state))
		return SSAM_NOTIF_HANDLED;

	status = hid_input_report(vhf->hid, HID_INPUT_REPORT, (u8 *)&event->data[0], event->length, 0);
	return ssam_notifier_from_errno(status) | SSAM_NOTIF_HANDLED;
}

static int surface_sam_sid_vhf_probe(struct platform_device *pdev)
{
	const struct ssam_hid_properties *p = pdev->dev.platform_data;
	struct ssam_controller *ctrl;
	struct sid_vhf *vhf;
	struct vhf_device_metadata meta = {};
	struct hid_device *hid;
	int status;

	// add device link to EC
	status = ssam_client_bind(&pdev->dev, &ctrl);
	if (status)
		return status == -ENXIO ? -EPROBE_DEFER : status;

	vhf = kzalloc(sizeof(struct sid_vhf), GFP_KERNEL);
	if (!vhf)
		return -ENOMEM;

	status = vhf_get_metadata(p->instance, &meta);
	if (status)
		goto err_create_hid;

	hid = sid_vhf_create_hid_device(pdev, &meta);
	if (IS_ERR(hid)) {
		status = PTR_ERR(hid);
		goto err_create_hid;
	}

	vhf->dev = pdev;
	vhf->ctrl = ctrl;
	vhf->p = pdev->dev.platform_data;
	vhf->hid = hid;

	vhf->notif.base.priority = 1;
	vhf->notif.base.fn = sid_vhf_event_handler;
	vhf->notif.event.reg = p->registry;
	vhf->notif.event.id.target_category = SSAM_SSH_TC_HID;
	vhf->notif.event.id.instance = p->instance;
	vhf->notif.event.flags = 0;

	platform_set_drvdata(pdev, vhf);

	status = surface_sam_ssh_notifier_register(&vhf->notif);
	if (status)
		goto err_notif;

	status = hid_add_device(hid);
	if (status)
		goto err_add_hid;

	return 0;

err_add_hid:
	surface_sam_ssh_notifier_unregister(&vhf->notif);
err_notif:
	hid_destroy_device(hid);
	platform_set_drvdata(pdev, NULL);
err_create_hid:
	kfree(vhf);
	return status;
}

static int surface_sam_sid_vhf_remove(struct platform_device *pdev)
{
	struct sid_vhf *vhf = platform_get_drvdata(pdev);

	surface_sam_ssh_notifier_unregister(&vhf->notif);
	hid_destroy_device(vhf->hid);
	kfree(vhf);

	platform_set_drvdata(pdev, NULL);
	return 0;
}

static struct platform_driver surface_sam_sid_vhf = {
	.probe = surface_sam_sid_vhf_probe,
	.remove = surface_sam_sid_vhf_remove,
	.driver = {
		.name = "surface_sam_sid_vhf",
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
	},
};
module_platform_driver(surface_sam_sid_vhf);

MODULE_AUTHOR("Blaž Hrastnik <blaz@mxxn.io>");
MODULE_DESCRIPTION("Driver for HID devices connected via Surface SAM");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:surface_sam_sid_vhf");
