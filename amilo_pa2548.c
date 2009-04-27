/*
  Copyright (C) 2008-2009 Piotr V. Abramov <piotr.abram@gmail.com>

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

/**
 * @mainpage
 *
 * \section intro Introduction
 *
 * This is a documentation for the driver which supports the backlight interface
 * of the Linux.
 *
 * By the default the Linux kernel has not the support of the backlight
 * interface for the notebook: <b>FSC Amilo Pa 2548</b>. This driver fixes this issue.
 * But it doesn't fix the hotkey issue, it only registers in the backlight
 * interface and in the platform interface.
 *
 * \section howto How to use
 *
 * \subsection howtoplatform Using through the platform interface
 *
 * The driver exports the following files:
 * - /sys/devices/platform/amilo_pa2548/lcd_level [mode: <b>666</b>]
 *
 * In order to change the brightness level of the LCD-screen you have to type
 * the command: "echo n > /sys/devices/platform/amilo_pa2548/lcd_level", where
 * the 'n' is a single number in the range 0..7.
 *
 * \subsection howtobacklight Using through the backlight interface
 *
 * Also you can use the backlight interface.
 *
 * This is a standart way to change the brightness level. It is available to
 * userspace under /sys/class/backlight/amilo_pa2548/.
 *
 * \section setup Installation
 *
 * Get the source tar-ball and extract it. Type "make" to build from sources a
 * kernel module, then type "make install" to install to the module dir.
 *
 * To autoload the module in the system startup add "amilo_pa2548" to your
 * rc-config.
 *
 * \section authors Authors & Copyrights
 * - Piotr V. Abramov
 *
 * Copyrights (c) 2008-2009
 */

/**
 * @file amilo_pa2548.c
 *
 * @brief Fujitsu Siemens Computers Amilo Pa 2548 ACPI support
 *
 * The driver exports a file in /sys/devices/platform/amilo_pa2548/;
 *
 * -rw-rw-rw- lcd_level - LCD brightness, a single integer in the range 0..7
 *
 * Also it registers in the Linux backlight control subsystem and is
 * available to userspace under /sys/class/backlight/amilo_pa2548/.
 */

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
 * Defines
 *****************************************************************************/

#define AMILO_PA2548_VENDOR         "FUJITSU SIEMENS"
#define AMILO_PA2548_SYSTEM_NAME    "amilo_pa2548"

#define AMILO_PA2548_AUTHOR         "Piotr V. Abramov"
#define AMILO_PA2548_DESC           "Fujitsu Siemens Computers Amilo Pa 2548 ACPI support"
#define AMILO_PA2548_PREFIX         AMILO_PA2548_SYSTEM_NAME ": "
#define AMILO_PA2548_VERSION        "0.2"

#define AMILO_PA2548_EC_HID         "PNP0C09"
#define AMILO_PA2548_DRIVER_NAME    "Amilo Pa 2548 ACPI brightness driver"
#define AMILO_PA2548_DRIVER_CLASS   "amilo_pa2548"

#define kfree_s(x)                  if (x) { kfree(x); x = NULL; }

/*****************************************************************************
 * Structs
 *****************************************************************************/

/** 
 * @brief The structure of the available model options
 */
struct options_t
{
    char *name;     /**< The model name */
    char *BCL;      /**< The path to `query list of brightness control level supported` */
    char *BCM;      /**< The path to `set the brightness level` */
    int max_blevel; /**< The max brightness level */
    int min_blevel; /**< The min brightness level */
};

/** 
 * @brief The structure of the data for the global object
 */
struct data_storage_t
{
    int current_blevel; /**< The current brightness level */
};

/** 
 * @brief The structure of the global object
 */
struct amilo_pa2548_t
{
    /** The backlight device */
    struct backlight_device *bl_device;
    /** The platform device */
    struct platform_device *pf_device;

    /** The available model options */
    struct options_t options;
    /** The data for model */
    struct data_storage_t data_storage;
};

/*****************************************************************************
 * Prototypes
 *****************************************************************************/

static int dmi_setup_opts_to_amilo_pa_2548(const struct dmi_system_id *dsid);

static int low_set_blevel(int level);

static int bl_get_blevel(struct backlight_device *bd);
static int bl_set_blevel(struct backlight_device *bd);

static ssize_t pf_show_lcd_level(struct device *dev,
struct device_attribute *attr, char *buf);
static ssize_t pf_store_lcd_level(struct device *dev,
struct device_attribute *attr, const char *buf, size_t count);

/*****************************************************************************
 * Initialized variables
 *****************************************************************************/

/** 
 * @brief The global object of this module
 */
static struct amilo_pa2548_t *this_laptop = NULL;

/** 
 * @brief The option indexes of the supported models
 */
enum OPTIONS_IDX {
    MODEL_AMILO_PA_2548 = 0,
    MODEL_END
};

/** 
 * @brief The model options
 */
static const struct options_t __initdata model_options [MODEL_END] =
{
    /* The options for model Amilo Pa 2548 */
    {
        .name = "Amilo Pa 2548",
        .BCL = "\\_SB.PCI0.XVR0.VGA.LCD._BCL",
        .BCM = "\\_SB.PCI0.XVR0.VGA.LCD._BCM",
        .max_blevel = 7,
        .min_blevel = 0,
    },
};

/** 
 * @brief The DMI whitelist of the supported models
 */
static const struct dmi_system_id __initdata dmi_vip_table[] =
{
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

/** 
 * @brief The backlight options
 */
static struct backlight_ops bl_opts = {
    .get_brightness = bl_get_blevel,
    .update_status = bl_set_blevel
};

static DEVICE_ATTR(lcd_level, 0666, pf_show_lcd_level, pf_store_lcd_level);

/** 
 * @brief The platform specific attributes
 * 
 * @ingroup platformgroup
 */
static struct attribute *pf_attributes[] = {
    &dev_attr_lcd_level.attr,
    NULL
};

/** 
 * @brief The platform specific group attributes
 *
 * @ingroup platformgroup
 */
static struct attribute_group pf_attribute_group = {
    .attrs = pf_attributes
};

/** 
 * @brief The platform driver data
 *
 * @ingroup platformgroup
 */
static struct platform_driver pf_driver = {
    .driver = {
        .name = AMILO_PA2548_SYSTEM_NAME,
        .owner = THIS_MODULE
    }
};

/*****************************************************************************
 * Implementation
 *****************************************************************************/

/** 
 * @brief Initializes options for model FSC Amilo Pa 2548
 *
 * @param dsid The DMI system identificator
 *
 * @return Always the normal status
 */
static int dmi_setup_opts_to_amilo_pa_2548(const struct dmi_system_id *dsid)
{
   this_laptop->options = model_options[MODEL_AMILO_PA_2548];

   return 0;
}

/** 
 * @brief Sets a brightness level
 * 
 * @param level The brightness level in the range 0..7
 * 
 * @return The ACPI error level
 */
static int low_set_blevel(int level)
{
    int status = 0;
    union acpi_object arg0 = { ACPI_TYPE_INTEGER };
    struct acpi_object_list args = { 1, &arg0 };

    int out_of_left_border = (level < this_laptop->options.min_blevel);
    int out_of_right_border = (level > this_laptop->options.max_blevel);

    if (out_of_left_border || out_of_right_border)
        return -EINVAL;

    this_laptop->data_storage.current_blevel = level;
    arg0.integer.value = level;
    status = acpi_evaluate_object(NULL, (char*)this_laptop->options.BCM, &args, NULL);

    return ACPI_FAILURE(status);
}

/**
 * @defgroup backlightgroup The backlight related stuff
 * @{
 */

/** 
 * @brief Gets the brightness level
 *
 * @param bd The system backlight device
 *
 * @return The current brightness level
 */
static int bl_get_blevel(struct backlight_device *bd)
{
    return this_laptop->data_storage.current_blevel;
}

/** 
 * @brief Sets the brightness level
 *
 * @param bd The system backlight device
 *
 * @return The ACPI error level
 */
static int bl_set_blevel(struct backlight_device *bd)
{
    return low_set_blevel(bd->props.brightness);
}

/** @} */

/**
 * @defgroup platformgroup The platform related stuff
 * @{
 */

/** 
 * @brief Gets the platform brightness level
 *
 * @param dev The device
 * @param attr The device attribute
 * @param buf The system buffer
 *
 * @return The number of passed characters
 */
static ssize_t pf_show_lcd_level(struct device *dev,
    struct device_attribute *attr, char *buf)
{
    return sprintf(buf, "%i\n", (this_laptop->data_storage.current_blevel));
}

/** 
 * @brief Sets the platform brightness level
 *
 * @param dev The device
 * @param attr The device attribute
 * @param buf The system buffer
 * @param count The count of character in the system buffer
 *
 * @return The buffer size
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

/** @} */

/** 
 * @brief Initializes this module
 * 
 * @return The exit code
 */
static int __init amilo_pa2548_init(void)
{
    int result = 0;

    if (acpi_disabled) /* Without ACPI nothing to do */
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

/** 
 * @brief The module destructor
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

