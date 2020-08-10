// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/device.h>
#include <linux/uuid.h>

#include "bus.h"
#include "controller.h"


static struct bus_type ssam_bus_type;


static void ssam_device_release(struct device *dev)
{
	struct ssam_device *sdev = to_ssam_device(dev);

	ssam_controller_put(sdev->ctrl);
	kfree(sdev);
}

static const struct device_type ssam_device_type = {
	.name    = "ssam_client",
	.release = ssam_device_release,
	.groups  = NULL,	// TODO
	.uevent  = NULL,	// TODO
};


static bool is_ssam_device(struct device *device)
{
	return device->type == &ssam_device_type;
}


struct ssam_device *ssam_device_alloc(struct ssam_controller *ctrl, guid_t type)
{
	struct ssam_device *sdev;

	sdev = kzalloc(sizeof(*sdev), GFP_KERNEL);
	if (!sdev)
		return NULL;

	device_initialize(&sdev->dev);
	sdev->dev.bus = &ssam_bus_type;
	sdev->dev.type = &ssam_device_type;
	sdev->dev.parent = ssam_controller_device(ctrl);
	sdev->ctrl = ssam_controller_get(ctrl);
	sdev->type = type;

	return sdev;
}
EXPORT_SYMBOL_GPL(ssam_device_alloc);

int ssam_device_add(struct ssam_device *sdev)
{
	enum ssam_controller_state state;
	int status;

	/*
	 * Ensure that we can only add new devices to a controller if it has
	 * been started and is not going away soon. This works in combination
	 * with ssam_controller_remove_clients to ensure driver presence for the
	 * controller device, i.e. it ensures that the controller (sdev->ctrl)
	 * is always valid and can be used for requests as long as the client
	 * device we add here is registered as child under it. This essentially
	 * guarantees that the client driver can always expect the preconditions
	 * for functions like ssam_request_sync (controller has to be started
	 * and is not suspended) to hold and thus does not have to check for
	 * them.
	 *
	 * Note that for this to work, the controller has to be a parent device.
	 * If it is not a direct parent, care has to be taken that the device is
	 * removed via ssam_device_remove, as device_unregister does not remove
	 * child devices recursively.
	 */
	ssam_controller_statelock(sdev->ctrl);

	state = smp_load_acquire(&sdev->ctrl->state);
	if (state != SSAM_CONTROLLER_STARTED) {
		ssam_controller_stateunlock(sdev->ctrl);
		return -ENXIO;
	}

	// TODO: allow for multiple devices of same type

	dev_set_name(&sdev->dev, "%s-%pUl:00",
		     dev_name(sdev->dev.parent),
		     &sdev->type);

	status = device_add(&sdev->dev);

	ssam_controller_stateunlock(sdev->ctrl);
	return status;
}
EXPORT_SYMBOL_GPL(ssam_device_add);

void ssam_device_remove(struct ssam_device *sdev)
{
	device_unregister(&sdev->dev);
}
EXPORT_SYMBOL_GPL(ssam_device_remove);


const struct ssam_device_id *ssam_device_id_match(
		const struct ssam_device_id *table, const guid_t *guid)
{
	const struct ssam_device_id *id;

	for (id = table; !guid_is_null(&id->type); ++id)
		if (guid_equal(&id->type, guid))
			return id;

	return NULL;
}
EXPORT_SYMBOL_GPL(ssam_device_id_match);


static int ssam_bus_match(struct device *dev, struct device_driver *drv)
{
	struct ssam_device_driver *sdrv = to_ssam_device_driver(drv);
	struct ssam_device *sdev = to_ssam_device(dev);

	if (!is_ssam_device(dev))
		return 0;

	return !!ssam_device_id_match(sdrv->match_table, &sdev->type);
}

static int ssam_bus_probe(struct device *dev)
{
	struct ssam_device_driver *sdrv = to_ssam_device_driver(dev->driver);

	return sdrv->probe(to_ssam_device(dev));
}

static int ssam_bus_remove(struct device *dev)
{
	struct ssam_device_driver *sdrv = to_ssam_device_driver(dev->driver);

	if (sdrv->remove)
		sdrv->remove(to_ssam_device(dev));

	return 0;
}

static struct bus_type ssam_bus_type = {
	.name   = "ssam",
	.match  = ssam_bus_match,
	.probe  = ssam_bus_probe,
	.remove = ssam_bus_remove,
};


int __ssam_device_driver_register(struct ssam_device_driver *sdrv,
				  struct module *owner)
{
	sdrv->driver.owner = owner;
	sdrv->driver.bus = &ssam_bus_type;

	/* force drivers to async probe so I/O is possible in probe */
        sdrv->driver.probe_type = PROBE_PREFER_ASYNCHRONOUS;

	return driver_register(&sdrv->driver);
}
EXPORT_SYMBOL_GPL(__ssam_device_driver_register);

void ssam_device_driver_unregister(struct ssam_device_driver *sdrv)
{
	driver_unregister(&sdrv->driver);
}
EXPORT_SYMBOL_GPL(ssam_device_driver_unregister);


static int ssam_remove_device(struct device *dev, void *_data)
{
	struct ssam_device *sdev = to_ssam_device(dev);

	if (is_ssam_device(dev))
		ssam_device_remove(sdev);

	return 0;
}

/*
 * Controller lock should be held during this call and subsequent
 * de-initialization.
 */
void ssam_controller_remove_clients(struct ssam_controller *ctrl)
{
	struct device *dev = ssam_controller_device(ctrl);

	device_for_each_child(dev, NULL, ssam_remove_device);
}


int ssam_bus_register(void)
{
	return bus_register(&ssam_bus_type);
}

void ssam_bus_unregister(void) {
	return bus_unregister(&ssam_bus_type);
}
