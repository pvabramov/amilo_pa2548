/*
  Copyright (C) 2008-2009 Piotr W. Abramov <piotr.abram@gmail.com>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or   
  (at your option) any later version.                                 

  This program is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of         
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU  
  General Public License for more details.                           

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software      
  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA    
  02110-1301, USA.                                                 
 */

/*****************************************************************************
 * amilo_pa2548.c - Fujitsu Siemens Computers Amilo Pa 2548 ACPI support
 *****************************************************************************
 * DESCRIPTION:
 *****************************************************************************
 *
 * The driver exports a file in /sys/devices/platform/amilo_pa2548/;
 *
 * -rw-rw-rw- lcd_level - LCD brightness, a single integer in the range 0..7
 *
 * Also it registers in the Linux backlight control subsystem and is
 * available to userspace under /sys/class/backlight/amilo_pa2548/.
 *
 ****************************************************************************/

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/acpi.h>
#include <linux/dmi.h>
#include <linux/backlight.h>
#include <linux/input.h>
#include <linux/kfifo.h>
#include <linux/video_output.h>
#include <linux/platform_device.h>
#include <linux/string.h>

/*****************************************************************************
 *		Defines
 *****************************************************************************/

#define AMILO_PA2548_VENDOR         "FUJITSU SIEMENS"
#define AMILO_PA2548_SYSTEM_NAME    "amilo_pa2548"

#define AMILO_PA2548_AUTHOR			"Piotr W. Abramov"
#define AMILO_PA2548_DESC			"Fujitsu Siemens Computers Amilo Pa 2548 ACPI support"
#define AMILO_PA2548_PREFIX			AMILO_PA2548_SYSTEM_NAME ": "
#define AMILO_PA2548_VERSION        "0.2"

#define AMILO_PA2548_EC_HID         "PNP0C09"
#define AMILO_PA2548_DRIVER_NAME    "Amilo Pa 2548 ACPI brightness driver"
#define AMILO_PA2548_DRIVER_CLASS   "amilo_pa2548"

#define kfree_s(x)                  if (x) { kfree(x); x = NULL; }

/*****************************************************************************
 *		Structs
 *****************************************************************************/

struct options_t
{
	char *name;		/* Model name */
	char *BCL;		/* Path to `query list of brightness control level supported` */
	char *BCM;		/* Path to `set the brightness level` */
	int max_blevel;	/* Max brightness level */
	int min_blevel;	/* Min brightness level */
};

struct data_storage_t
{
    int current_blevel;
};

struct amilo_pa2548_t
{
    struct backlight_device *bl_device;
    struct platform_device *pf_device;

	struct options_t options;
    struct data_storage_t data_storage;
};

/*****************************************************************************
 * Prototypes
 *****************************************************************************/

/* DMI */
static int dmi_setup_opts_to_amilo_pa_2548(const struct dmi_system_id *dsid);

/* Low-level interface to set a brightness level */
static int low_set_blevel(int level);

/* backlight stuff */
static int bl_get_blevel(struct backlight_device *bd);
static int bl_set_blevel(struct backlight_device *bd);

/* platform stuff */
static ssize_t pf_show_lcd_level(struct device *dev,
		struct device_attribute *attr, char *buf);
static ssize_t pf_store_lcd_level(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count);

/*****************************************************************************
 * Initialized variables
 *****************************************************************************/

static struct amilo_pa2548_t *this_laptop = NULL;

enum OPTIONS_IDX {
    MODEL_AMILO_PA_2548 = 0,
    MODEL_END
};

static const struct options_t __initdata model_options [MODEL_END] =
{
    /* Options for model Amilo Pa 2548 */
	{
		.name = "Amilo Pa 2548",
		.BCL = "\\_SB.PCI0.XVR0.VGA.LCD._BCL",
		.BCM = "\\_SB.PCI0.XVR0.VGA.LCD._BCM",
		.max_blevel = 7,
		.min_blevel = 0,
	},
};

static const struct dmi_system_id __initdata dmi_vip_table[] = {
	{
		.ident = "Amilo Pa 2548",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, AMILO_PA2548_VENDOR),
			DMI_MATCH(DMI_PRODUCT_NAME, "AMILO Pa 2548")
		},
		.callback = dmi_setup_opts_to_amilo_pa_2548
	},
	{}
};

static struct backlight_ops bl_opts = {
    .get_brightness = bl_get_blevel,
    .update_status = bl_set_blevel
};

static DEVICE_ATTR(lcd_level, 0666, pf_show_lcd_level, pf_store_lcd_level);

static struct attribute *pf_attributes[] = {
	&dev_attr_lcd_level.attr,
	NULL
};

static struct attribute_group pf_attribute_group = {
	.attrs = pf_attributes
};

static struct platform_driver pf_driver = {
	.driver = {
		.name = AMILO_PA2548_SYSTEM_NAME,
		.owner = THIS_MODULE
	}
};

/*****************************************************************************
 *		Implementation
 *****************************************************************************/

/* Inits options for model Amilo Pa 2548
 *
 */
static int dmi_setup_opts_to_amilo_pa_2548(const struct dmi_system_id *dsid)
{
   this_laptop->options = model_options[MODEL_AMILO_PA_2548];

   return 0;
}

/* Sets a brightness level
 *
 */
static int low_set_blevel(int level)
{
	int status = 0;
	union acpi_object arg0 = { ACPI_TYPE_INTEGER };
	struct acpi_object_list args = { 1, &arg0 };

	if (level < this_laptop->options.min_blevel
    ||  level > this_laptop->options.max_blevel)
		return -EINVAL;

    this_laptop->data_storage.current_blevel = level;
    arg0.integer.value = level;
	status = acpi_evaluate_object(NULL, (char*)this_laptop->options.BCM, &args, NULL);

	return ACPI_FAILURE(status);
}

/* Backlight stuff */

/* Returns the brightness level
 *
 */
static int bl_get_blevel(struct backlight_device *bd)
{
    return this_laptop->data_storage.current_blevel;
}

/* Sets the brightness level
 *
 */
static int bl_set_blevel(struct backlight_device *bd)
{
    return low_set_blevel(bd->props.brightness);
}

/* Platform stuff */

/* Returns the brightness level
 *
 */
static ssize_t pf_show_lcd_level(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%i\n", (this_laptop->data_storage.current_blevel));
}

/* Sets the brightness level
 *
 */
static ssize_t pf_store_lcd_level(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int lvl, ret;

	if ((sscanf(buf, "%i", &lvl) != 1)
            || (lvl < this_laptop->options.min_blevel)
            || (lvl > this_laptop->options.max_blevel))
		return EINVAL;

	ret = low_set_blevel(lvl);
	if (ret < 0)
		return ret;

	return count;
}

/* Inits module
 *
 */
static int __init amilo_pa2548_init(void)
{
	int result = 0;

	if (acpi_disabled)	/* Without ACPI nothing to do */
		return -ENODEV;

	/* Allocate main obj */
	this_laptop = kzalloc(sizeof(struct amilo_pa2548_t), GFP_KERNEL);	
	if (!this_laptop)
		return -ENOMEM;

	/* Verify supported models */
	if (!dmi_check_system(dmi_vip_table))
    {
		printk(KERN_ERR AMILO_PA2548_PREFIX "this notebook is not supported.\n");

		result = -ENODEV;
		goto __unsupported_device;
	}

    /* Set brightness level to max */
	low_set_blevel(this_laptop->options.max_blevel);

    /* Register backlight stuff */

    this_laptop->bl_device = backlight_device_register(AMILO_PA2548_SYSTEM_NAME,
            NULL, NULL, &bl_opts);
    if (IS_ERR(this_laptop->bl_device)) {
        result = -ENODEV;
        goto __cannot_register_backlight_device;
    }

    /* set backlight options */

    this_laptop->bl_device->props.max_brightness = this_laptop->options.max_blevel;
    this_laptop->bl_device->props.brightness = this_laptop->data_storage.current_blevel;

    /* Register platform stuff */

	result = platform_driver_register(&pf_driver);
	if (result < 0)
		goto __cannot_register_platform_driver;

	this_laptop->pf_device = platform_device_alloc(AMILO_PA2548_SYSTEM_NAME, -1);
	if (IS_ERR(this_laptop->pf_device))
		goto __cannot_allocate_device;

	result = platform_device_add(this_laptop->pf_device);
	if (result < 0)
		goto __cannot_add_device;

	result = sysfs_create_group(&this_laptop->pf_device->dev.kobj,
			&pf_attribute_group);
	if (result < 0)
		goto __cannot_create_group_in_sysfs;

    /* Print ok message */
    printk(KERN_INFO AMILO_PA2548_SYSTEM_NAME " version %s loaded\n",
            AMILO_PA2548_VERSION);

	return 0;

__cannot_create_group_in_sysfs:
	platform_device_del(this_laptop->pf_device);

__cannot_add_device:
	platform_device_put(this_laptop->pf_device);

__cannot_allocate_device:
	platform_driver_unregister(&pf_driver);

__cannot_register_platform_driver:
    backlight_device_unregister(this_laptop->bl_device);
__cannot_register_backlight_device:
__unsupported_device:
	kfree_s(this_laptop);

	return result;
}

/* Kills module
 *
 */
static void __exit amilo_pa2548_exit(void)
{
	sysfs_remove_group(&this_laptop->pf_device->dev.kobj,
			&pf_attribute_group);

	platform_device_unregister(this_laptop->pf_device);

	platform_driver_unregister(&pf_driver);

    backlight_device_unregister(this_laptop->bl_device);

	kfree_s(this_laptop);
	/* Goodbye message */
	printk(KERN_INFO AMILO_PA2548_PREFIX "unloaded\n");	
}

module_init(amilo_pa2548_init);
module_exit(amilo_pa2548_exit);

MODULE_AUTHOR(AMILO_PA2548_AUTHOR);
MODULE_DESCRIPTION(AMILO_PA2548_DESC);
MODULE_VERSION(AMILO_PA2548_VERSION);
MODULE_LICENSE("GPL");

