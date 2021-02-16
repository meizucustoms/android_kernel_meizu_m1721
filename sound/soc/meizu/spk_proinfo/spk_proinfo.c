/*
 * TXJ Speaker support - Low-level kernel driver
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
#define SPEAKER_PROINFO_DATA_START_ADDR  0xd8
#define SPEAKER_PROINFO_DATA_LENGTH      0x1e
#define SPEAKER_PROINFO_PARTITION        "/dev/block/by-name/proinfo"

#include <linux/firmware.h>
#include <linux/device.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/delay.h>
#include <linux/compat.h>
#include <linux/acpi.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/init.h>

static struct platform_device *speaker_pdev;
static struct device *speaker_root_dev;

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Roman Rihter");
MODULE_DESCRIPTION("TXJ Speaker low-level driver");
MODULE_VERSION("1.0");

static ssize_t speaker_proinfo_show(struct device *dev,
        struct device_attribute *attr, char *buf)
{
    ssize_t ret = 0;
    ssize_t len = 0;
    struct file *fp = NULL;
    char *data = NULL;
    
    pr_info("%s: start proinfo read\n", __func__);
    
    fp = filp_open(SPEAKER_PROINFO_PARTITION, 66, 0);
    if (IS_ERR(fp)) {
        pr_err("%s: open proinfo path error\n", __func__);
        return -EPERM;
    }
    
    msleep(50);

    // Go to speaker calibration data in proinfo
    fp->f_pos += SPEAKER_PROINFO_DATA_START_ADDR;

    ret = fp->f_op->read(fp, data, SPEAKER_PROINFO_DATA_LENGTH, &fp->f_pos);
    if (ret == SPEAKER_PROINFO_DATA_LENGTH) {
        filp_close(fp, 0);
        len = strlen(data);
        pr_info("%s: Read buffer length - %d", __func__, (int)len);
        
        if (len < SPEAKER_PROINFO_DATA_LENGTH) {
            pr_info("%s: Read buffer data: %s", __func__, data);
        } else {
            pr_err("%s: Invalid buffer length!", __func__);
            return -EINVAL;
        }
    } else {
        filp_close(fp, 0);
        pr_err("%s: Read bytes from proinfo failed! %d\n", __func__, (int)ret);
        return -EFAULT;
    }

    return 0;
}

static ssize_t speaker_proinfo_store(struct device *dev,
        struct device_attribute *attr, const char *buf, size_t count)
{
    ssize_t len = 0;
    struct file *fp = NULL;
    char *user = NULL;
    
    pr_info("%s: start proinfo write\n", __func__);
    
    if (count < (SPEAKER_PROINFO_DATA_LENGTH + 1)) {
        memcpy(user, buf, count);
        if (user == NULL)
            pr_warn("%s: WARNING: user is erasing speaker calibration data!\n", __func__);
    
        fp = filp_open(SPEAKER_PROINFO_PARTITION, 66, 0);
        if (IS_ERR(fp)) {
            pr_err("%s: open proinfo path error\n", __func__);
            return -EPERM;
        }
        
        // Go to speaker calibration data in proinfo
        fp->f_pos += SPEAKER_PROINFO_DATA_START_ADDR;
        len = fp->f_op->write(fp, user, count, &fp->f_pos);
        if (len == count) {
            pr_info("%s: write buf: SUCCESS, length: %d\n", __func__, (int)len);
        } else {
            filp_close(fp, 0);
            pr_err("%s: Write bytes to proinfo failed! %d\n", __func__, (int)len);
            return len;
        }
    } else {
        pr_err("%s: error: invalid count value - %d!\n", __func__, (int)count);
    }

    filp_close(fp, 0);
    
    return count;
}

static const struct device_attribute dev_attr_spk_cali[] = {
    __ATTR(calibration_data, 0644, speaker_proinfo_show, speaker_proinfo_store),
};

static struct platform_driver speaker_pdrv = {
    .driver = {
        .name = "speaker",
    },
};

static int __init speaker_init(void)
{
    struct platform_device_info pdevinfo;
    
    if (platform_driver_register(&speaker_pdrv)) {
        pdevinfo.parent = 0;
        pdevinfo.res = 0;
        pdevinfo.num_res = 0;
        pdevinfo.data = 0;
        pdevinfo.size_data = 0;
        pdevinfo.dma_mask = 0;
        pdevinfo.name = "speaker";
        pdevinfo.id = -1;
        
        speaker_pdev = platform_device_register_full(&pdevinfo);
        if (IS_ERR(speaker_pdev)) {
            platform_driver_unregister(&speaker_pdrv);
            return -EFAULT;
        }
        
        speaker_root_dev = __root_device_register("speaker", 0);
        if (IS_ERR(speaker_root_dev)) {
            platform_device_unregister(speaker_pdev);
            platform_driver_unregister(&speaker_pdrv);
            return -EFAULT;
        }
        
        if (device_create_file(speaker_root_dev, dev_attr_spk_cali)) {
            root_device_unregister(speaker_root_dev);
            platform_device_unregister(speaker_pdev);
            platform_driver_unregister(&speaker_pdrv);
            return -EFAULT;
        }
    }
    return 0;
}
module_init(speaker_init);

static void __exit speaker_exit(void)
{
    pr_info("%s: disable driver nodes\n", __func__);
    
    platform_device_unregister(speaker_pdev);
    platform_driver_unregister(&speaker_pdrv);
    root_device_unregister(speaker_root_dev);
    
    return;
}
module_exit(speaker_exit);
