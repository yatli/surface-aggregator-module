// SPDX-License-Identifier: GPL-2.0+
/*
 * Surface System Aggregator Module (SSAM) client device registry.
 *
 * Registry for non-platform/non-ACPI SSAM client devices, i.e. devices that
 * cannot be auto-detected. Provides device-hubs and performs instantiation
 * for these devices.
 *
 * Copyright (C) 2020-2021 Maximilian Luz <luzmaximilian@gmail.com>
 */

#include <linux/acpi.h>
#include <linux/kernel.h>
#include <linux/limits.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/types.h>
#include <linux/workqueue.h>

#include "../../include/linux/surface_aggregator/controller.h"
#include "../../include/linux/surface_aggregator/device.h"


/* -- Device registry. ------------------------------------------------------ */

/*
 * SSAM device names follow the SSAM module alias, meaning they are prefixed
 * with 'ssam:', followed by domain, category, target ID, instance ID, and
 * function, each encoded as two-digit hexadecimal, separated by ':'. In other
 * words, it follows the scheme
 *
 *      ssam:dd:cc:tt:ii:ff
 *
 * Where, 'dd', 'cc', 'tt', 'ii', and 'ff' are the two-digit hexadecimal
 * values mentioned above, respectively.
 */

/* Root node. */
static const struct software_node ssam_node_root = {
	.name = "ssam_platform_hub",
};

/* KIP device hub (connects detachable keyboard/touchpad on Surface Pro 8 and Book 3). */
static const struct software_node ssam_node_hub_kip = {
	.name = "ssam:01:0e:01:00:00",
	.parent = &ssam_node_root,
};

/* AC adapter. */
static const struct software_node ssam_node_bat_ac = {
	.name = "ssam:01:02:01:01:01",
	.parent = &ssam_node_root,
};

/* Primary battery. */
static const struct software_node ssam_node_bat_main = {
	.name = "ssam:01:02:01:01:00",
	.parent = &ssam_node_root,
};

/* Secondary battery (Surface Book 3, managed via KIP hub). */
static const struct software_node ssam_node_bat_kip = {
	.name = "ssam:01:02:02:01:00",
	.parent = &ssam_node_hub_kip,
};

/* Platform profile / performance-mode device. */
static const struct software_node ssam_node_tmp_pprof = {
	.name = "ssam:01:03:01:00:01",
	.parent = &ssam_node_root,
};

/* Tablet-mode switch via KIP subsystem. */
static const struct software_node ssam_node_kip_tablet_switch = {
	.name = "ssam:01:0e:01:00:01",
	.parent = &ssam_node_root,
};

/* DTX / detachment-system device (Surface Book 3). */
static const struct software_node ssam_node_bas_dtx = {
	.name = "ssam:01:11:01:00:00",
	.parent = &ssam_node_root,
};

/* HID keyboard (TID1). */
static const struct software_node ssam_node_hid_tid1_keyboard = {
	.name = "ssam:01:15:01:01:00",
	.parent = &ssam_node_root,
};

/* HID pen stash (TID1; pen taken / stashed away evens). */
static const struct software_node ssam_node_hid_tid1_penstash = {
	.name = "ssam:01:15:01:02:00",
	.parent = &ssam_node_root,
};

/* HID touchpad (TID1). */
static const struct software_node ssam_node_hid_tid1_touchpad = {
	.name = "ssam:01:15:01:03:00",
	.parent = &ssam_node_root,
};

/* HID device instance 6 (TID1, unknown HID device). */
static const struct software_node ssam_node_hid_tid1_iid6 = {
	.name = "ssam:01:15:01:06:00",
	.parent = &ssam_node_root,
};

/* HID device instance 7 (TID1, unknown HID device). */
static const struct software_node ssam_node_hid_tid1_iid7 = {
	.name = "ssam:01:15:01:07:00",
	.parent = &ssam_node_root,
};

/* HID system controls (TID1). */
static const struct software_node ssam_node_hid_tid1_sysctrl = {
	.name = "ssam:01:15:01:08:00",
	.parent = &ssam_node_root,
};

/* HID keyboard. */
static const struct software_node ssam_node_hid_main_keyboard = {
	.name = "ssam:01:15:02:01:00",
	.parent = &ssam_node_root,
};

/* HID touchpad. */
static const struct software_node ssam_node_hid_main_touchpad = {
	.name = "ssam:01:15:02:03:00",
	.parent = &ssam_node_root,
};

/* HID device instance 5 (unknown HID device). */
static const struct software_node ssam_node_hid_main_iid5 = {
	.name = "ssam:01:15:02:05:00",
	.parent = &ssam_node_root,
};

/* HID keyboard (KIP hub). */
static const struct software_node ssam_node_hid_kip_keyboard = {
	.name = "ssam:01:15:02:01:00",
	.parent = &ssam_node_hub_kip,
};

/* HID pen stash (KIP hub; pen taken / stashed away evens). */
static const struct software_node ssam_node_hid_kip_penstash = {
	.name = "ssam:01:15:02:02:00",
	.parent = &ssam_node_hub_kip,
};

/* HID touchpad (KIP hub). */
static const struct software_node ssam_node_hid_kip_touchpad = {
	.name = "ssam:01:15:02:03:00",
	.parent = &ssam_node_hub_kip,
};

/* HID device instance 5 (KIP hub, unknown HID device). */
static const struct software_node ssam_node_hid_kip_iid5 = {
	.name = "ssam:01:15:02:05:00",
	.parent = &ssam_node_hub_kip,
};

/* HID device instance 6 (KIP hub, unknown HID device). */
static const struct software_node ssam_node_hid_kip_iid6 = {
	.name = "ssam:01:15:02:06:00",
	.parent = &ssam_node_hub_kip,
};

/*
 * Devices for 5th- and 6th-generations models:
 * - Surface Book 2,
 * - Surface Laptop 1 and 2,
 * - Surface Pro 5 and 6.
 */
static const struct software_node *ssam_node_group_gen5[] = {
	&ssam_node_root,
	&ssam_node_tmp_pprof,
	NULL,
};

/* Devices for Surface Book 3. */
static const struct software_node *ssam_node_group_sb3[] = {
	&ssam_node_root,
	&ssam_node_hub_kip,
	&ssam_node_bat_ac,
	&ssam_node_bat_main,
	&ssam_node_bat_kip,
	&ssam_node_tmp_pprof,
	&ssam_node_bas_dtx,
	&ssam_node_hid_kip_keyboard,
	&ssam_node_hid_kip_touchpad,
	&ssam_node_hid_kip_iid5,
	&ssam_node_hid_kip_iid6,
	NULL,
};

/* Devices for Surface Laptop 3 and 4. */
static const struct software_node *ssam_node_group_sl3[] = {
	&ssam_node_root,
	&ssam_node_bat_ac,
	&ssam_node_bat_main,
	&ssam_node_tmp_pprof,
	&ssam_node_hid_main_keyboard,
	&ssam_node_hid_main_touchpad,
	&ssam_node_hid_main_iid5,
	NULL,
};

/* Devices for Surface Laptop Studio. */
static const struct software_node *ssam_node_group_sls[] = {
	&ssam_node_root,
	&ssam_node_bat_ac,
	&ssam_node_bat_main,
	&ssam_node_tmp_pprof,
	&ssam_node_hid_tid1_keyboard,
	&ssam_node_hid_tid1_penstash,
	&ssam_node_hid_tid1_touchpad,
	&ssam_node_hid_tid1_iid6,
	&ssam_node_hid_tid1_iid7,
	&ssam_node_hid_tid1_sysctrl,
	NULL,
};

/* Devices for Surface Laptop Go. */
static const struct software_node *ssam_node_group_slg1[] = {
	&ssam_node_root,
	&ssam_node_bat_ac,
	&ssam_node_bat_main,
	&ssam_node_tmp_pprof,
	NULL,
};

/* Devices for Surface Pro 7 and Surface Pro 7+. */
static const struct software_node *ssam_node_group_sp7[] = {
	&ssam_node_root,
	&ssam_node_bat_ac,
	&ssam_node_bat_main,
	&ssam_node_tmp_pprof,
	NULL,
};

static const struct software_node *ssam_node_group_sp8[] = {
	&ssam_node_root,
	&ssam_node_hub_kip,
	&ssam_node_bat_ac,
	&ssam_node_bat_main,
	&ssam_node_tmp_pprof,
	&ssam_node_kip_tablet_switch,
	&ssam_node_hid_kip_keyboard,
	&ssam_node_hid_kip_penstash,
	&ssam_node_hid_kip_touchpad,
	&ssam_node_hid_kip_iid5,
	NULL,
};


/* -- Device registry helper functions. ------------------------------------- */

static int ssam_uid_from_string(const char *str, struct ssam_device_uid *uid)
{
	u8 d, tc, tid, iid, fn;
	int n;

	n = sscanf(str, "ssam:%hhx:%hhx:%hhx:%hhx:%hhx", &d, &tc, &tid, &iid, &fn);
	if (n != 5)
		return -EINVAL;

	uid->domain = d;
	uid->category = tc;
	uid->target = tid;
	uid->instance = iid;
	uid->function = fn;

	return 0;
}

static int ssam_hub_add_device(struct device *parent, struct ssam_controller *ctrl,
			       struct fwnode_handle *node)
{
	struct ssam_device_uid uid;
	struct ssam_device *sdev;
	int status;

	status = ssam_uid_from_string(fwnode_get_name(node), &uid);
	if (status)
		return status;

	sdev = ssam_device_alloc(ctrl, uid);
	if (!sdev)
		return -ENOMEM;

	sdev->dev.parent = parent;
	sdev->dev.fwnode = node;

	status = ssam_device_add(sdev);
	if (status)
		ssam_device_put(sdev);

	return status;
}

static int ssam_hub_register_clients(struct device *parent, struct ssam_controller *ctrl,
				     struct fwnode_handle *node)
{
	struct fwnode_handle *child;
	int status;

	fwnode_for_each_child_node(node, child) {
		/*
		 * Try to add the device specified in the firmware node. If
		 * this fails with -EINVAL, the node does not specify any SSAM
		 * device, so ignore it and continue with the next one.
		 */

		status = ssam_hub_add_device(parent, ctrl, child);
		if (status && status != -EINVAL)
			goto err;
	}

	return 0;
err:
	ssam_remove_clients(parent);
	return status;
}


/* -- SSAM KIP-subsystem hub driver. ---------------------------------------- */

/*
 * Some devices may need a bit of time to be fully usable after being
 * (re-)connected. This delay has been determined via experimentation.
 */
#define SSAM_KIP_UPDATE_CONNECT_DELAY		msecs_to_jiffies(250)

#define SSAM_EVENT_KIP_CID_CONNECTION		0x2c

enum ssam_kip_hub_state {
	SSAM_KIP_HUB_UNINITIALIZED,
	SSAM_KIP_HUB_CONNECTED,
	SSAM_KIP_HUB_DISCONNECTED,
};

struct ssam_kip_hub {
	struct ssam_device *sdev;

	enum ssam_kip_hub_state state;
	struct delayed_work update_work;

	struct ssam_event_notifier notif;
};

SSAM_DEFINE_SYNC_REQUEST_R(__ssam_kip_get_connection_state, u8, {
	.target_category = SSAM_SSH_TC_KIP,
	.target_id       = 0x01,
	.command_id      = 0x2c,
	.instance_id     = 0x00,
});

static int ssam_kip_get_connection_state(struct ssam_kip_hub *hub, enum ssam_kip_hub_state *state)
{
	int status;
	u8 connected;

	status = ssam_retry(__ssam_kip_get_connection_state, hub->sdev->ctrl, &connected);
	if (status < 0) {
		dev_err(&hub->sdev->dev, "failed to query KIP connection state: %d\n", status);
		return status;
	}

	*state = connected ? SSAM_KIP_HUB_CONNECTED : SSAM_KIP_HUB_DISCONNECTED;
	return 0;
}

static ssize_t ssam_kip_hub_state_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct ssam_kip_hub *hub = dev_get_drvdata(dev);
	const char *state;

	switch (hub->state) {
	case SSAM_KIP_HUB_UNINITIALIZED:
		state = "uninitialized";
		break;

	case SSAM_KIP_HUB_CONNECTED:
		state = "connected";
		break;

	case SSAM_KIP_HUB_DISCONNECTED:
		state = "disconnected";
		break;

	default:
		/*
		 * Any value not handled in the above cases is invalid and
		 * should never have been set. Thus this case should be
		 * impossible to reach.
		 */
		WARN(1, "invalid KIP hub state: %d\n", hub->state);
		state = "<invalid>";
		break;
	}

	return sysfs_emit(buf, "%s\n", state);
}

static struct device_attribute ssam_kip_hub_attr_state =
	__ATTR(state, 0444, ssam_kip_hub_state_show, NULL);

static struct attribute *ssam_kip_hub_attrs[] = {
	&ssam_kip_hub_attr_state.attr,
	NULL,
};

static const struct attribute_group ssam_kip_hub_group = {
	.attrs = ssam_kip_hub_attrs,
};

static void ssam_kip_hub_update_workfn(struct work_struct *work)
{
	struct ssam_kip_hub *hub = container_of(work, struct ssam_kip_hub, update_work.work);
	struct fwnode_handle *node = dev_fwnode(&hub->sdev->dev);
	enum ssam_kip_hub_state state;
	int status = 0;

	status = ssam_kip_get_connection_state(hub, &state);
	if (status)
		return;

	if (hub->state == state)
		return;
	hub->state = state;

	if (hub->state == SSAM_KIP_HUB_CONNECTED)
		status = ssam_hub_register_clients(&hub->sdev->dev, hub->sdev->ctrl, node);
	else
		ssam_hot_remove_clients(&hub->sdev->dev);

	if (status)
		dev_err(&hub->sdev->dev, "failed to update KIP-hub devices: %d\n", status);
}

static u32 ssam_kip_hub_notif(struct ssam_event_notifier *nf, const struct ssam_event *event)
{
	struct ssam_kip_hub *hub = container_of(nf, struct ssam_kip_hub, notif);
	unsigned long delay;

	if (event->command_id != SSAM_EVENT_KIP_CID_CONNECTION)
		return 0;	/* Return "unhandled". */

	if (event->length < 1) {
		dev_err(&hub->sdev->dev, "unexpected payload size: %u\n", event->length);
		return 0;
	}

	/*
	 * Delay update when KIP devices are being connected to give devices/EC
	 * some time to set up.
	 */
	delay = event->data[0] ? SSAM_KIP_UPDATE_CONNECT_DELAY : 0;
	schedule_delayed_work(&hub->update_work, delay);

	return SSAM_NOTIF_HANDLED;
}

static int __maybe_unused ssam_kip_hub_resume(struct device *dev)
{
	struct ssam_kip_hub *hub = dev_get_drvdata(dev);

	schedule_delayed_work(&hub->update_work, 0);
	return 0;
}
static SIMPLE_DEV_PM_OPS(ssam_kip_hub_pm_ops, NULL, ssam_kip_hub_resume);

static int ssam_kip_hub_probe(struct ssam_device *sdev)
{
	struct ssam_kip_hub *hub;
	int status;

	hub = devm_kzalloc(&sdev->dev, sizeof(*hub), GFP_KERNEL);
	if (!hub)
		return -ENOMEM;

	hub->sdev = sdev;
	hub->state = SSAM_KIP_HUB_UNINITIALIZED;

	hub->notif.base.priority = INT_MAX;  /* This notifier should run first. */
	hub->notif.base.fn = ssam_kip_hub_notif;
	hub->notif.event.reg = SSAM_EVENT_REGISTRY_SAM;
	hub->notif.event.id.target_category = SSAM_SSH_TC_KIP,
	hub->notif.event.id.instance = 0,
	hub->notif.event.mask = SSAM_EVENT_MASK_TARGET;
	hub->notif.event.flags = SSAM_EVENT_SEQUENCED;

	INIT_DELAYED_WORK(&hub->update_work, ssam_kip_hub_update_workfn);

	ssam_device_set_drvdata(sdev, hub);

	status = ssam_device_notifier_register(sdev, &hub->notif);
	if (status)
		return status;

	status = sysfs_create_group(&sdev->dev.kobj, &ssam_kip_hub_group);
	if (status)
		goto err;

	schedule_delayed_work(&hub->update_work, 0);
	return 0;

err:
	ssam_device_notifier_unregister(sdev, &hub->notif);
	cancel_delayed_work_sync(&hub->update_work);
	ssam_remove_clients(&sdev->dev);
	return status;
}

static void ssam_kip_hub_remove(struct ssam_device *sdev)
{
	struct ssam_kip_hub *hub = ssam_device_get_drvdata(sdev);

	sysfs_remove_group(&sdev->dev.kobj, &ssam_kip_hub_group);

	ssam_device_notifier_unregister(sdev, &hub->notif);
	cancel_delayed_work_sync(&hub->update_work);
	ssam_remove_clients(&sdev->dev);
}

static const struct ssam_device_id ssam_kip_hub_match[] = {
	{ SSAM_SDEV(KIP, 0x01, 0x00, 0x00) },
	{ },
};

static struct ssam_device_driver ssam_kip_hub_driver = {
	.probe = ssam_kip_hub_probe,
	.remove = ssam_kip_hub_remove,
	.match_table = ssam_kip_hub_match,
	.driver = {
		.name = "surface_kip_hub",
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
		.pm = &ssam_kip_hub_pm_ops,
	},
};


/* -- SSAM platform/meta-hub driver. ---------------------------------------- */

static const struct acpi_device_id ssam_platform_hub_match[] = {
	/* Surface Pro 4, 5, and 6 (OMBR < 0x10) */
	{ "MSHW0081", (unsigned long)ssam_node_group_gen5 },

	/* Surface Pro 6 (OMBR >= 0x10) */
	{ "MSHW0111", (unsigned long)ssam_node_group_gen5 },

	/* Surface Pro 7 */
	{ "MSHW0116", (unsigned long)ssam_node_group_sp7 },

	/* Surface Pro 7+ */
	{ "MSHW0119", (unsigned long)ssam_node_group_sp7 },

	/* Surface Pro 8 */
	{ "MSHW0263", (unsigned long)ssam_node_group_sp8 },

	/* Surface Book 2 */
	{ "MSHW0107", (unsigned long)ssam_node_group_gen5 },

	/* Surface Book 3 */
	{ "MSHW0117", (unsigned long)ssam_node_group_sb3 },

	/* Surface Laptop 1 */
	{ "MSHW0086", (unsigned long)ssam_node_group_gen5 },

	/* Surface Laptop 2 */
	{ "MSHW0112", (unsigned long)ssam_node_group_gen5 },

	/* Surface Laptop 3 (13", Intel) */
	{ "MSHW0114", (unsigned long)ssam_node_group_sl3 },

	/* Surface Laptop 3 (15", AMD) and 4 (15", AMD) */
	{ "MSHW0110", (unsigned long)ssam_node_group_sl3 },

	/* Surface Laptop 4 (13", Intel) */
	{ "MSHW0250", (unsigned long)ssam_node_group_sl3 },

	/* Surface Laptop Go 1 */
	{ "MSHW0118", (unsigned long)ssam_node_group_slg1 },

	/* Surface Laptop Studio */
	{ "MSHW0123", (unsigned long)ssam_node_group_sls },

	{ },
};
MODULE_DEVICE_TABLE(acpi, ssam_platform_hub_match);

static int ssam_platform_hub_probe(struct platform_device *pdev)
{
	const struct software_node **nodes;
	struct ssam_controller *ctrl;
	struct fwnode_handle *root;
	int status;

	nodes = (const struct software_node **)acpi_device_get_match_data(&pdev->dev);
	if (!nodes)
		return -ENODEV;

	/*
	 * As we're adding the SSAM client devices as children under this device
	 * and not the SSAM controller, we need to add a device link to the
	 * controller to ensure that we remove all of our devices before the
	 * controller is removed. This also guarantees proper ordering for
	 * suspend/resume of the devices on this hub.
	 */
	ctrl = ssam_client_bind(&pdev->dev);
	if (IS_ERR(ctrl))
		return PTR_ERR(ctrl) == -ENODEV ? -EPROBE_DEFER : PTR_ERR(ctrl);

	status = software_node_register_node_group(nodes);
	if (status)
		return status;

	root = software_node_fwnode(&ssam_node_root);
	if (!root) {
		software_node_unregister_node_group(nodes);
		return -ENOENT;
	}

	set_secondary_fwnode(&pdev->dev, root);

	status = ssam_hub_register_clients(&pdev->dev, ctrl, root);
	if (status) {
		set_secondary_fwnode(&pdev->dev, NULL);
		software_node_unregister_node_group(nodes);
	}

	platform_set_drvdata(pdev, nodes);
	return status;
}

static int ssam_platform_hub_remove(struct platform_device *pdev)
{
	const struct software_node **nodes = platform_get_drvdata(pdev);

	ssam_remove_clients(&pdev->dev);
	set_secondary_fwnode(&pdev->dev, NULL);
	software_node_unregister_node_group(nodes);
	return 0;
}

static struct platform_driver ssam_platform_hub_driver = {
	.probe = ssam_platform_hub_probe,
	.remove = ssam_platform_hub_remove,
	.driver = {
		.name = "surface_aggregator_platform_hub",
		.acpi_match_table = ssam_platform_hub_match,
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
	},
};


/* -- Module initialization. ------------------------------------------------ */

static int __init ssam_device_hub_init(void)
{
	int status;

	status = platform_driver_register(&ssam_platform_hub_driver);
	if (status)
		goto err_platform;

	status = ssam_device_driver_register(&ssam_kip_hub_driver);
	if (status)
		goto err_kip;

	return 0;

err_kip:
	platform_driver_unregister(&ssam_platform_hub_driver);
err_platform:
	return status;
}
module_init(ssam_device_hub_init);

static void __exit ssam_device_hub_exit(void)
{
	ssam_device_driver_unregister(&ssam_kip_hub_driver);
	platform_driver_unregister(&ssam_platform_hub_driver);
}
module_exit(ssam_device_hub_exit);

MODULE_AUTHOR("Maximilian Luz <luzmaximilian@gmail.com>");
MODULE_DESCRIPTION("Device-registry for Surface System Aggregator Module");
MODULE_LICENSE("GPL");
