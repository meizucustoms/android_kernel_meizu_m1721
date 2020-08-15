/*
 * Meizu hardware info - Low-level kernel driver
 *
 * Copyright (C) 2020 MeizuCustoms enthusiasts
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */ 

#include <asm/uaccess.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/ioctl.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/compat.h>
#include <linux/types.h>
#include "meizu_hw.h"

struct meizu_hw_info mzhw_info = {
    .display = "NO THAT DEVICE",
    .touchscreen = "NO THAT DEVICE",
    .memory = "NO THAT DEVICE",
    .front_camera = "NO THAT DEVICE",
    .back_camera = "NO THAT DEVICE",
    .gravity_sensor = "ICM-20608D",
    .light_sensor = "LTR579PS",
    .gyroscope = "ICM-20608D",
    .magnetometer = "ST480",
    .bluetooth = "WCN3615",
    .wifi = "WCN3615",
    .gps = "RF-U2200",
    .fm = "WCN3615",
    .battery = "FEIMAOTUI-CARINA-SDI",
    .proximity_sensor = "LTR579ALS",
    .masterb_camera = "NO THAT DEVICE",
    .slaveb_camera = "NO THAT DEVICE",
};

static long meizu_hw_ioctl(struct file *filp, unsigned int cmd, unsigned long arg) {
	int ret = 0;
	
	mz_info("enter\n");
	mz_info("ioctl cmd = 0x%02x, arg = %lu\n", cmd, arg);
	
	switch (cmd) {
	case -MEIZUHW_IOC_DISPLAY:
		ret = __copy_to_user(&arg, mzhw_info.display, 40);
		break;
	case -MEIZUHW_IOC_TOUCHSCREEN:
		ret = __copy_to_user(&arg, mzhw_info.touchscreen, 40);
		break;
	case -MEIZUHW_IOC_MEMORY:
		ret = __copy_to_user(&arg, mzhw_info.memory, 40);
		break;
	case -MEIZUHW_IOC_FRONT_CAMERA:
		ret = __copy_to_user(&arg, mzhw_info.front_camera, 40);
		break;
	case -MEIZUHW_IOC_BACK_CAMERA:
		ret = __copy_to_user(&arg, mzhw_info.back_camera, 40);
		break;
	case -MEIZUHW_IOC_GRAVITY_SENSOR:
		ret = __copy_to_user(&arg, mzhw_info.gravity_sensor, 40);
		break;
	case -MEIZUHW_IOC_LIGHT_SENSOR:
		ret = __copy_to_user(&arg, mzhw_info.light_sensor, 40);
		break;
	case -MEIZUHW_IOC_GYROSCOPE:
		ret = __copy_to_user(&arg, mzhw_info.gyroscope, 40);
		break;
	case -MEIZUHW_IOC_MAGNETOMETER:
		ret = __copy_to_user(&arg, mzhw_info.magnetometer, 40);
		break;
	case -MEIZUHW_IOC_BLUETOOTH:
		ret = __copy_to_user(&arg, mzhw_info.bluetooth, 40);
		break;
	case -MEIZUHW_IOC_WIFI:
		ret = __copy_to_user(&arg, mzhw_info.wifi, 40);
		break;
	case -MEIZUHW_IOC_GPS:
		ret = __copy_to_user(&arg, mzhw_info.gps, 40);
		break;
	case -MEIZUHW_IOC_FM:
		ret = __copy_to_user(&arg, mzhw_info.fm, 40);
		break;
	case -MEIZUHW_IOC_BATTERY:
		ret = __copy_to_user(&arg, mzhw_info.battery, 40);
		break;
	case -MEIZUHW_IOC_MASTERB_CAMERA:
		ret = __copy_to_user(&arg, mzhw_info.masterb_camera, 40);
		break;
	case -MEIZUHW_IOC_SLAVEB_CAMERA:
		ret = __copy_to_user(&arg, mzhw_info.slaveb_camera, 40);
		break;
	default:
		mz_warn("command 0x%02x not implemented\n", cmd);
		ret = -ENOIOCTLCMD;
		break;
	}
	
	return ret;
}

static long meizu_hw_compat_ioctl(struct file *filp, unsigned int cmd, unsigned long arg) {
	mz_info("enter\n");
	return meizu_hw_ioctl(filp, cmd, (unsigned long)compat_ptr(arg));
}

static int meizu_hw_open(struct inode *inode, struct file *filp) {
	mz_info("enter\n");
	nonseekable_open(inode, filp);
	return 0;
}

static const struct file_operations meizu_hw_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = meizu_hw_ioctl,
	.compat_ioctl = meizu_hw_compat_ioctl,
	.open = meizu_hw_open,
};

static struct miscdevice meizu_hw_device = {
	.fops = &meizu_hw_fops,
};

static int __init meizu_hw_init(void) {
	int ret = misc_register(&meizu_hw_device);
	if (ret < 0) {
		mz_err("error %d on creating misc device\n", ret);
		return ret;
	}
	
	mz_info("init done!\n");
	return 0;
}

module_init(meizu_hw_init);

static void __exit meizu_hw_exit(void) {
	misc_deregister(&meizu_hw_device);
}

module_exit(meizu_hw_exit);
