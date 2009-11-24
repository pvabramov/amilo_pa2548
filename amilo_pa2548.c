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
 * By the default the Linux kernel has no the support of the backlight
 * interface for the notebook: <b>FSC Amilo Pa 2548</b>. This driver fixes this issue.
 *
 * From 2009-11-24 this driver supports Fn-keys: brightness up/down.
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
#define AMILO_PA2548_VERSION        "0.3"

#define AMILO_PA2548_DRIVER_NAME    "Amilo Pa 2548 ACPI brightness driver"
#define AMILO_PA2548_DRIVER_CLASS   "amilo_pa2548"

#define AMILO_PA2548_ACPI_DRIVER_HID     "LNXSYSTM"

#define ACPI_VIDEO_NOTIFY_INC_BRIGHTNESS     0x86
#define ACPI_VIDEO_NOTIFY_DEC_BRIGHTNESS     0x87

#define IO_PORT_ADDRESS_SET                  0x72
#define IO_PORT_DATA_RW                      0x73

#define BRTS_REGISTER_ADDRESS                0xF3

#define kfree_s(x)                  if (x) { kfree(x); x = NULL; }
#define safe_do(p,a)                if (p) { a; }

/*****************************************************************************
 * Structs
 *****************************************************************************/

/** 
 * @brief The structure of the available model options
 */
struct options_t
{
    char *name;       /**< The model name */
    char *BCL;        /**< The path to `query list of brightness control level supported` */
    char *BCM;        /**< The path to `set the brightness level` */
    int max_blevel;   /**< The max brightness level */
    int min_blevel;   /**< The min brightness level */
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
    
    /** ACPI device */
    struct acpi_device *driver_device;
    /** Input device */
    struct input_dev *input;

    /** The available model options */
    struct options_t options;
    
    char input_phys[32];  /**< The path of the input device */
    int current_blevel;   /**< The current brightness level */

};

/*****************************************************************************
 * Prototypes
 *****************************************************************************/

static int dmi_setup_opts_to_amilo_pa_2548(const struct dmi_system_id *dsid);

static int lcd_set_blevel(int level);
static int lcd_get_blevel(int *level);

static int bl_get_blevel(struct backlight_device *bd);
static int bl_set_blevel(struct backlight_device *bd);

static ssize_t pf_show_lcd_level(struct device *dev,
                                 struct device_attribute *attr, char *buf);
static ssize_t pf_store_lcd_level(struct device *dev,
                                  struct device_attribute *attr,
                                  const char *buf, size_t count);
static int acpi_driver_add(struct acpi_device *device);
static int acpi_driver_remove(struct acpi_device *device, int type);
static void acpi_driver_notify(struct acpi_device *device, u32 event);

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
enum OPTIONS_IDX
{
    MODEL_AMILO_PA_2548 = 0,
    MODEL_END
};

/** 
 * @brief The model options
 */
static const struct options_t __initdata model_options[MODEL_END] = {
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

/**
 * @brief IDs of ACPI device
 */
static const struct acpi_device_id acpi_driver_device_ids[] = {
    {AMILO_PA2548_ACPI_DRIVER_HID, 0},
    {"", 0},
};

/**
 * @brief The ACPI driver specific options
 */
static struct acpi_driver acpi_amilo_pa2548_driver = {
    .name = AMILO_PA2548_DRIVER_NAME,
    .class = AMILO_PA2548_DRIVER_CLASS,
    .ids = acpi_driver_device_ids,
    .ops = {
        .add = acpi_driver_add,
        .remove = acpi_driver_remove,
        .notify = acpi_driver_notify,
    },
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
static int lcd_set_blevel(int level)
{
    int status = 0;
    union acpi_object arg0 = { ACPI_TYPE_INTEGER };
    struct acpi_object_list args = { 1, &arg0 };

    int out_of_left_border = (level < this_laptop->options.min_blevel);
    int out_of_right_border = (level > this_laptop->options.max_blevel);

    if (out_of_left_border || out_of_right_border)
        return -EINVAL;

    this_laptop->current_blevel = level;
    arg0.integer.value = level;
    status =
        acpi_evaluate_object(NULL, (char *)this_laptop->options.BCM, &args,
                             NULL);

    return ACPI_FAILURE(status);
}

/** 
 * @brief Gets a brightness level
 * 
 * @param level The brightness level
 * 
 * @return The ACPI error level
 */
static int lcd_get_blevel(int *level)
{
    int data = -1;
    int left_border = this_laptop->options.min_blevel;
    int right_border = this_laptop->options.max_blevel;
    int status;

    if (level == NULL)
        return AE_ERROR;

    (*level) = this_laptop->current_blevel;

    status = acpi_os_write_port(IO_PORT_ADDRESS_SET, BRTS_REGISTER_ADDRESS, 1);
    if (status < 0)
    {
        printk(KERN_ERR AMILO_PA2548_PREFIX
               "Cannot to write data: %d in port 0x%X\n", BRTS_REGISTER_ADDRESS,
               IO_PORT_ADDRESS_SET);
        return AE_ERROR;
    }

    status = acpi_os_read_port(IO_PORT_DATA_RW, &data, 1);
    if (status < 0)
    {
        printk(KERN_ERR AMILO_PA2548_PREFIX
               "Cannot to read data from port: 0x%X\n", IO_PORT_DATA_RW);
        return AE_ERROR;
    }

    if (left_border > data || data > right_border)
    {
        printk(KERN_ERR AMILO_PA2548_PREFIX
               "Something is strange the read data is %d but expected data in range from %d to %d\n",
               data, this_laptop->options.min_blevel, this_laptop->options.max_blevel);
        return AE_ERROR;
    }

    (*level) = this_laptop->current_blevel = data;

    return AE_OK;
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
    int level;
    lcd_get_blevel(&level);
        
    return level;
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
    return lcd_set_blevel(bd->props.brightness);
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
    int level;
    lcd_get_blevel(&level);
    
    return sprintf(buf, "%i\n", level);
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
                                  struct device_attribute *attr,
                                  const char *buf, size_t count)
{
    int status;
    int level;

    status = sscanf(buf, "%i", &level);
    if (status < 0)
        level = this_laptop->current_blevel;

    status = lcd_set_blevel(level);
    if (status < 0)
        return status;

    return count;
}

/** @} */

/**
 * @defgroup The ACPI driver group
 * @{
 */

/** 
 * Adds ACPI driver to kernelspace - registers input device to trap ACPI events
 * 
 * @param device The ACPI device
 * 
 * @return The exit code
 */
static int acpi_driver_add(struct acpi_device *device)
{
    struct input_dev *input;
    int result = 0;

    if (device == NULL)
    {
        result = -EINVAL;
        goto __failed_to_get_device;
    }

    sprintf(acpi_device_name(device), "%s", AMILO_PA2548_DRIVER_NAME);
    sprintf(acpi_device_class(device), "%s", AMILO_PA2548_DRIVER_CLASS);

    device->driver_data = this_laptop;

    input = input_allocate_device();
    if (input == NULL)
    {
        result = -ENOMEM;
        goto __failed_to_allocate_input_device;
    }
    this_laptop->input = input;

    snprintf(this_laptop->input_phys, sizeof(this_laptop->input_phys),
             "%s/video/input0", acpi_device_hid(device));

    input->name = acpi_device_name(device);

    input->phys = this_laptop->input_phys;
    input->id.bustype = BUS_HOST;
    input->id.product = 0x06;
    input->dev.parent = &device->dev;
    input->evbit[0] = BIT(EV_KEY);
    set_bit(KEY_BRIGHTNESSUP, input->keybit);
    set_bit(KEY_BRIGHTNESSDOWN, input->keybit);
    set_bit(KEY_UNKNOWN, input->keybit);

    result = input_register_device(input);
    if (result)
    {
        printk(KERN_ERR AMILO_PA2548_PREFIX "Cannot register input device\n");
        goto __failed_to_register_input_device;
    }

    this_laptop->driver_device = device;

    return 0;

__failed_to_register_input_device:
    input_free_device(input);
    this_laptop->input = NULL;

__failed_to_allocate_input_device:
__failed_to_get_device:

   return result;
}

/** 
 * Removes the ACPI driver from kernelspace
 * 
 * @param device The ACPI device
 * @param type The type of the ACPI driver
 * 
 * @return The exit code
 */
static int acpi_driver_remove(struct acpi_device *device, int type)
{
    int result = 0;

    if (device == NULL)
        result = -EINVAL;

    /* free the input device */
    safe_do(this_laptop->input, input_free_device(this_laptop->input));
    this_laptop->input = NULL;

    return result;
}

/** 
 * Handles notifications
 * 
 * @param device The ACPI device
 * @param event The ACPI event
 */
static void acpi_driver_notify(struct acpi_device *device, u32 event)
{
    struct input_dev *input = NULL;
    int keycode = 0;
    int level;

    input = this_laptop->input;

    lcd_get_blevel(&level);

    switch (event)
    {
        case ACPI_VIDEO_NOTIFY_DEC_BRIGHTNESS:
            lcd_set_blevel(--level);
            keycode = KEY_BRIGHTNESSDOWN;
            acpi_bus_generate_proc_event(this_laptop->driver_device,
                                         ACPI_VIDEO_NOTIFY_DEC_BRIGHTNESS, 0);
            break;

        case ACPI_VIDEO_NOTIFY_INC_BRIGHTNESS:
            lcd_set_blevel(++level);
            keycode = KEY_BRIGHTNESSUP;
            acpi_bus_generate_proc_event(this_laptop->driver_device,
                                         ACPI_VIDEO_NOTIFY_INC_BRIGHTNESS, 0);
            break;

        default:
            keycode = 0;
            printk(KERN_WARNING AMILO_PA2548_PREFIX "Unknown event: 0x%X\n",
                   event);
    }

    if (keycode != 0)
    {
        input_report_key(input, keycode, 1);
        input_sync(input);
        input_report_key(input, keycode, 0);
        input_sync(input);
   }
}

/**
 * }@
 */

static void this_laptop_init(struct amilo_pa2548_t *this)
{
    this->pf_device = NULL;
    this->bl_device = NULL;
    this->input = NULL;
    this->driver_device = NULL;
    
    memset(this->input_phys, 0, sizeof(this->input_phys));

    {
        int level;
        this->current_blevel = this->options.max_blevel;
        if (lcd_get_blevel(&level))
            this->current_blevel = level;
    }
}

/** 
 * @brief Initializes this module
 * 
 * @return The exit code
 */
static int __init amilo_pa2548_init(void)
{
    int result = 0;

    if (acpi_disabled)           /* Without ACPI nothing to do */
        return -ENODEV;

    /* Allocate main obj */
    this_laptop = kzalloc(sizeof(struct amilo_pa2548_t), GFP_KERNEL);
    if (!this_laptop)
        return -ENOMEM;

    /* Verify supported models */
    if (!dmi_check_system(dmi_vip_table))
    {
        printk(KERN_ERR AMILO_PA2548_PREFIX
               "this notebook is not supported.\n");

        result = -ENODEV;
        goto __unsupported_device;
    }

    this_laptop_init(this_laptop);

    /* ACPI driver stuff */
    
    result = acpi_bus_register_driver(&acpi_amilo_pa2548_driver);
    if (result < 0)
    {
        result = -ENODEV;
        goto __cannot_register_acpi_driver;
    }

    /* Backlight stuff */

    if (acpi_video_backlight_support() == 0)
    /*
     * If kernel ACPI doesn't support backlight device
     * then we have to register own
     */
    {
        int level;
        
        this_laptop->bl_device =
            backlight_device_register(AMILO_PA2548_SYSTEM_NAME, NULL, NULL,
                                      &bl_opts);
        if (IS_ERR(this_laptop->bl_device))
        {
            result = -ENODEV;
            goto __cannot_register_backlight_device;
        }

        /* Set backlight options */
        this_laptop->bl_device->props.max_brightness =
            this_laptop->options.max_blevel;

        lcd_get_blevel(&level);
        this_laptop->bl_device->props.brightness = level;
    }

    /* Platform stuff */

    result = platform_driver_register(&pf_driver);
    if (result < 0)
        goto __cannot_register_platform_driver;

    this_laptop->pf_device =
        platform_device_alloc(AMILO_PA2548_SYSTEM_NAME, -1);
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
    printk(KERN_INFO AMILO_PA2548_PREFIX AMILO_PA2548_SYSTEM_NAME
           " version %s loaded\n", AMILO_PA2548_VERSION);

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
    acpi_bus_unregister_driver(&acpi_amilo_pa2548_driver);
__cannot_register_acpi_driver:
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
    if (!this_laptop)
        return;

    safe_do(this_laptop->pf_device,
            sysfs_remove_group(&this_laptop->pf_device->dev.kobj,
                               &pf_attribute_group));
    
    safe_do(this_laptop->pf_device,
            platform_device_unregister(this_laptop->pf_device));
    
    platform_driver_unregister(&pf_driver);
    
    safe_do(this_laptop->bl_device,
            backlight_device_unregister(this_laptop->bl_device));
    
    acpi_bus_unregister_driver(&acpi_amilo_pa2548_driver);

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

