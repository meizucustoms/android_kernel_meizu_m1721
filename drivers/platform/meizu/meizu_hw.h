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

// Cool logs
#define mz_info(fmt, args...) pr_info("mzhw: %s: " fmt, __func__, ##args)
#define mz_warn(fmt, args...) pr_warn("mzhw: %s: " fmt, __func__, ##args)
#define mz_err(fmt, args...) pr_err("mzhw: %s: " fmt, __func__, ##args)

// HW info struct
struct meizu_hw_info {
    char display[40];
    char touchscreen[40];
    char memory[40];
    char front_camera[40];
    char back_camera[40];
    char gravity_sensor[40];
    char light_sensor[40];
    char gyroscope[40];
    char magnetometer[40];
    char bluetooth[40];
    char wifi[40];
    char gps[40];
    char fm[40];
    char battery[40];
    char proximity_sensor[40];
    char masterb_camera[40];
    char slaveb_camera[40];
};

// IOCTL commands
#define MEIZUHW_IOC_DISPLAY             0x3fbfb7ff
#define MEIZUHW_IOC_TOUCHSCREEN         0x3fbfb7fe
#define MEIZUHW_IOC_MEMORY              0x3fbfb7fd
#define MEIZUHW_IOC_FRONT_CAMERA        0x3fbfb7fc
#define MEIZUHW_IOC_BACK_CAMERA         0x3fbfb7fb
#define MEIZUHW_IOC_GRAVITY_SENSOR      0x3fbfb7fa
#define MEIZUHW_IOC_LIGHT_SENSOR        0x3fbfb7f9
#define MEIZUHW_IOC_GYROSCOPE           0x3fbfb7f8
#define MEIZUHW_IOC_MAGNETOMETER        0x3fbfb7f7
#define MEIZUHW_IOC_BLUETOOTH           0x3fbfb7f0
#define MEIZUHW_IOC_WIFI                0x3fbfb7ef
#define MEIZUHW_IOC_GPS                 0x3fbfb7ee
#define MEIZUHW_IOC_FM                  0x3fbfb7ed
#define MEIZUHW_IOC_BATTERY             0x3fbfb7eb
#define MEIZUHW_IOC_MASTERB_CAMERA      0x3fbfb7e8
#define MEIZUHW_IOC_SLAVEB_CAMERA       0x3fbfb7e7
