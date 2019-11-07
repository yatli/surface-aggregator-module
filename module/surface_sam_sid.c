#include <linux/acpi.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/mfd/core.h>


static const struct mfd_cell sid_devs_sp4[] = {
	{ .name = "surface_sam_sid_gpelid", .id = -1 },
	{ },
};

static const struct mfd_cell sid_devs_sb1[] = {
	{ .name = "surface_sam_sid_gpelid", .id = -1 },
	{ },
};

static const struct mfd_cell sid_devs_sb2[] = {
	{ .name = "surface_sam_sid_gpelid",   .id = -1 },
	{ .name = "surface_sam_sid_perfmode", .id = -1 },
	{ },
};

static const struct mfd_cell sid_devs_sl1[] = {
	{ .name = "surface_sam_sid_gpelid", .id = -1 },
	{ },
};

static const struct mfd_cell sid_devs_sl2[] = {
	{ .name = "surface_sam_sid_gpelid", .id = -1 },
	{ },
};

static const struct acpi_device_id surface_sam_sid_match[] = {
	{ "MSHW0081", (unsigned long)sid_devs_sp4 },	/* Surface Pro 4, 5, and 6 */
	{ "MSHW0080", (unsigned long)sid_devs_sb1 },	/* Surface Book 1 */
	{ "MSHW0107", (unsigned long)sid_devs_sb2 },	/* Surface Book 2 */
	{ "MSHW0086", (unsigned long)sid_devs_sl1 },	/* Surface Laptop 1 */
	{ "MSHW0112", (unsigned long)sid_devs_sl2 },	/* Surface Laptop 2 */
	{ },
};
MODULE_DEVICE_TABLE(acpi, surface_sam_sid_match);


static int surface_sam_sid_probe(struct platform_device *pdev)
{
	const struct acpi_device_id *match;
	const struct mfd_cell *cells, *p;

	match = acpi_match_device(surface_sam_sid_match, &pdev->dev);
	if (!match)
		return -ENODEV;

	cells = (struct mfd_cell *)match->driver_data;
	if (!cells)
		return -ENODEV;

	for (p = cells; p->name; ++p) {
		/* just count */
	}

	return mfd_add_devices(&pdev->dev, 0, cells, p - cells, NULL, 0, NULL);
}

static int surface_sam_sid_remove(struct platform_device *pdev)
{
	mfd_remove_devices(&pdev->dev);
	return 0;
}

struct platform_driver surface_sam_sid = {
	.probe = surface_sam_sid_probe,
	.remove = surface_sam_sid_remove,
	.driver = {
		.name = "surface_sam_sid",
		.acpi_match_table = ACPI_PTR(surface_sam_sid_match),
	},
};
