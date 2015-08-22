/*
 * Copyright © 2015 Red Hat, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#ifndef LIBRATBAG_PRIVATE_H
#define LIBRATBAG_PRIVATE_H

#include <linux/input.h>
#include <stdint.h>

#include "libratbag.h"
#include "libratbag-util.h"

#define BUS_ANY					0xffff
#define VENDOR_ANY				0xffff
#define PRODUCT_ANY				0xffff
#define VERSION_ANY				0xffff

struct ratbag_driver;

struct ratbag {
	const struct ratbag_interface *interface;
	void *userdata;

	struct udev *udev;
	struct list drivers;

	int refcount;
	ratbag_log_handler log_handler;
	enum ratbag_log_priority log_priority;
};

struct ratbag_device {
	char *name;
	struct udev_device *udev_device;
	struct udev_device *udev_hidraw;
	int hidraw_fd;
	int refcount;
	struct input_id ids;
	struct ratbag_driver *driver;
	struct ratbag *ratbag;

	unsigned num_profiles;
	struct list profiles;

	unsigned num_buttons;

	void *drv_data;
};

struct ratbag_id {
	struct input_id id;
	unsigned long data;
};

/**
 * struct ratbag_driver - user space driver for a ratbag device
 */
struct ratbag_driver {
	/** the name of the driver */
	char *name;

	/** a list of devices supported by this driver. The last element
	 * must be empty to mark the end. */
	const struct ratbag_id *table_ids;

	/** callback called while trying to open a device by libratbag.
	 * This function should decide whether or not this driver will
	 * handle the given device.
	 *
	 * Return -ENODEV to ignore the device and let other drivers
	 * probe the device. Any other error code will stop the probing.
	 */
	int (*probe)(struct ratbag_device *device, const struct ratbag_id id);

	/** callback called right before the struct ratbag_device is
	 * unref-ed.
	 *
	 * In this callback, the extra memory allocated in probe should
	 * be freed.
	 */
	void (*remove)(struct ratbag_device *device);

	/** callback called when a read profile is requested by the
	 * caller of the library.
	 *
	 * The driver should probe here the device for the requested
	 * profile and populate the related information.
	 * There is no need to populate the various struct ratbag_button
	 * as they are allocated when the user needs it.
	 */
	void (*read_profile)(struct ratbag_profile *profile, unsigned int index);

	/** here, the driver should actually write the profile to the
	 * device.
	 */
	int (*write_profile)(struct ratbag_profile *profile);

	/** called when the user of libratbag wants to know which
	 * current profile is in use.
	 *
	 * It is fundamentally racy.
	 */
	int (*get_active_profile)(struct ratbag_device *device);

	/** called to mark a previously writen profile as active.
	 *
	 * There should be no need to write the profile here, a
	 * .write_profile() call is issued before calling this.
	 */
	int (*set_active_profile)(struct ratbag_device *device, unsigned int index);

	/** This should return a boolean whether or not the device
	 * supports the given capability.
	 *
	 * In most cases, the .probe() should store a list of capabilities
	 * for each device, but most of the time, it can be statically
	 * stored.
	 */
	int (*has_capability)(const struct ratbag_device *device, enum ratbag_capability cap);

	/** For the given button, fill in the struct ratbag_button
	 * with the available information.
	 *
	 * For devices with profiles, profile will not be NULL. For
	 * device without, profile will be set to NULL.
	 *
	 * For devices with profile, there should not be the need to
	 * actually read the profile from the device. The caller
	 * should make sure that the profile is up to date.
	 */
	void (*read_button)(struct ratbag_button *button);

	/** For the given button, store in the profile or in the device
	 * the given struct ratbag_button.
	 *
	 * For devices with profiles, profile will not be NULL. For
	 * device without, profile will be set to NULL.
	 *
	 * For devices with profile, there should not be the need to
	 * actually write the button to the device. The caller
	 * should later on write the profile in one call to
	 * .write_profile().
	 */
	int (*write_button)(struct ratbag_button *button);

	/** For the given profile, overwrite the current resolution
	 * of the sensor expressed in DPI, and commit it to the hardware.
	 *
	 * Mandatory if the driver exports RATBAG_CAP_SWITCHABLE_RESOLUTION.
	 */
	int (*write_resolution_dpi)(struct ratbag_profile *profile, int dpi);

	/* private */
	struct list link;
};

struct ratbag_profile {
	int refcount;
	struct list link;
	unsigned index;
	struct ratbag_device *device;
	struct list buttons;
	void *drv_data;
	void *user_data;
	int current_dpi;
};

struct ratbag_button {
	int refcount;
	struct list link;
	struct ratbag_profile *profile;
	unsigned index;
	enum ratbag_button_type type;
	enum ratbag_button_action_type action_type;
};

static inline int
ratbag_open_path(struct ratbag_device *device, const char *path, int flags)
{
	struct ratbag *ratbag = device->ratbag;

	return ratbag->interface->open_restricted(path, flags, ratbag->userdata);
}

static inline void
ratbag_close_fd(struct ratbag_device *device, int fd)
{
	struct ratbag *ratbag = device->ratbag;

	return ratbag->interface->close_restricted(fd, ratbag->userdata);
}

static inline void
ratbag_set_drv_data(struct ratbag_device *device, void *drv_data)
{
	device->drv_data = drv_data;
}

static inline void *
ratbag_get_drv_data(struct ratbag_device *device)
{
	return device->drv_data;
}

static inline void
ratbag_profile_set_drv_data(struct ratbag_profile *profile, void *drv_data)
{
	profile->drv_data = drv_data;
}

static inline void *
ratbag_profile_get_drv_data(struct ratbag_profile *profile)
{
	return profile->drv_data;
}


void
log_msg_va(struct ratbag *ratbag,
	   enum ratbag_log_priority priority,
	   const char *format,
	   va_list args);
void log_msg(struct ratbag *ratbag,
	enum ratbag_log_priority priority,
	const char *format, ...);
void
log_buffer(struct ratbag *ratbag,
	enum ratbag_log_priority priority,
	const char *header,
	uint8_t *buf, size_t len);

#define log_debug(li_, ...) log_msg((li_), RATBAG_LOG_PRIORITY_DEBUG, __VA_ARGS__)
#define log_info(li_, ...) log_msg((li_), RATBAG_LOG_PRIORITY_INFO, __VA_ARGS__)
#define log_error(li_, ...) log_msg((li_), RATBAG_LOG_PRIORITY_ERROR, __VA_ARGS__)
#define log_bug_kernel(li_, ...) log_msg((li_), RATBAG_LOG_PRIORITY_ERROR, "kernel bug: " __VA_ARGS__)
#define log_bug_libratbag(li_, ...) log_msg((li_), RATBAG_LOG_PRIORITY_ERROR, "libratbag bug: " __VA_ARGS__)
#define log_bug_client(li_, ...) log_msg((li_), LIBRATBAG_LOG_PRIORITY_ERROR, "client bug: " __VA_ARGS__)
#define log_buf_debug(li_, h_, buf_, len_) log_buffer(li_, RATBAG_LOG_PRIORITY_DEBUG, h_, buf_, len_)
#define log_buf_info(li_, h_, buf_, len_) log_buffer(li_, RATBAG_LOG_PRIORITY_INFO, h_, buf_, len_)
#define log_buf_error(li_, h_, buf_, len_) log_buffer(li_, RATBAG_LOG_PRIORITY_ERROR, h_, buf_, len_)

/* list of all supported drivers */
struct ratbag_driver etekcity_driver;
struct ratbag_driver hidpp20_driver;
struct ratbag_driver hidpp10_driver;

#endif /* LIBRATBAG_PRIVATE_H */

