/*
 * TXJ Speaker support - Low-level kernel driver
 *
 * Copyright (C) 2020 MeizuCustoms team
 * Copyright (C) 2020 Roman Rihter, msucks-m1721 maintaner
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
#define MEIZU_FOPEN_READ    0x42
#define MEIZU_FOPEN_LENGTH  0x1e

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

static struct platform_device *spk_proinfo_pdev;
static struct device *spk_proinfo_root_dev;

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Roman Rihter");
MODULE_DESCRIPTION("TXJ Speaker low-level driver");
MODULE_VERSION("1.0");

static ssize_t proinfo_spk_cali_show(struct device *dev,
        struct device_attribute *attr, char *buf)
{
    ssize_t sret = 0;
    ssize_t bufLen = 0;
    struct file *fp = NULL;
    char *r_buf = NULL;
    char fBuf [32];
    
    pr_info("%s: start proinfo read\n", __func__);
    
    fp = filp_open("/dev/block/platform/soc/7824900.sdhci/by-name/proinfo", MEIZU_FOPEN_READ, 0);
    if (IS_ERR(fp)) {
        pr_err("[TYPE] %s: open proinfo path error\n", __func__);
        return -1;
    }
    
    msleep(0x32);
    fp->f_pos = fp->f_pos + 0xd8;
    sret = fp->f_op->read(fp, r_buf, MEIZU_FOPEN_LENGTH, &fp->f_pos);
    if (sret == MEIZU_FOPEN_LENGTH) {
        filp_close(fp, 0);
        bufLen = strlen(r_buf);
        pr_info("%s: Read buffer length - %d", __func__, (int)bufLen);
        
        if (bufLen < MEIZU_FOPEN_LENGTH) {
            strcpy(fBuf, r_buf);
            pr_info("%s: Read buffer data: %s", __func__, fBuf);
        } else {
            pr_err("%s: Invalid buffer length!", __func__);
            return -EINVAL;
        }
    } else {
        pr_err("%s: Read bytes from proinfo failed! %d\n", __func__, (int)sret);
        return sret;
    }
    
    return sret;
}

static ssize_t proinfo_spk_cali_store(struct device *dev,
        struct device_attribute *attr, const char *buf, size_t count)
{
    
    int intCount = count;
    ssize_t bufLen = 0;
    struct file *fp = NULL;
    char *localBuf = NULL;
    
    pr_info("%s: start proinfo write\n", __func__);
    
    if (intCount < 0x1f) {
        memcpy(localBuf, buf, intCount);
        if (localBuf == NULL) {
            pr_err("%s: ERROR: localBuf IS NULL!\n", __func__);
        }
    
        fp = filp_open("/dev/block/platform/soc/7824900.sdhci/by-name/proinfo", MEIZU_FOPEN_READ, 0);
        if (IS_ERR(fp)) {
            pr_err("[RTX] %s: open proinfo path error\n", __func__);
            return -1;
        }
        
        fp->f_pos = fp->f_pos + 0xd8;
        bufLen = fp->f_op->write(fp, localBuf, intCount, &fp->f_pos);
        if (bufLen == intCount) {
            filp_close(fp, 0);
            pr_info("%s: write buf: SUCCESS, length: %d\n", __func__, (int)bufLen);
        } else {
            pr_err("%s: Write bytes to proinfo failed! %d\n", __func__, (int)bufLen);
            return bufLen;
        }
    } else {
        pr_err("%s: error: invalid count value - %d!\n", __func__, (int)count);
    }
    
    return count;
}

static const struct device_attribute dev_attr_spk_cali[] = {
    __ATTR(spk_cali, 0644, proinfo_spk_cali_show, proinfo_spk_cali_store),
};

static struct platform_driver spk_proinfo_pdrv = {
    .driver = {
        .name = "spk_proinfo",
    },
};

static int __init spk_proinfo_init(void)
{
    int result = 0;
    int ret = 0;
    const struct platform_device_info pdevinfo = {
        .name = "spk_proinfo",
        .id = 0xffffffff,
    };
    
    pr_info("SPK_PROINFO_INIT: enable driver nodes\n");
    result = platform_driver_register(&spk_proinfo_pdrv);
    if (result != 0)
        pr_err("SPK_PROINFO_INIT: FAILED TO REGISTER DRIVER!\n");
        return result;

    spk_proinfo_pdev = platform_device_register_full(&pdevinfo);
    if (IS_ERR(spk_proinfo_pdev)) {
        pr_err("SPK_PROINFO_INIT: ERROR REGISTERING PLATFORM DEVICE!\n");
        platform_device_unregister(spk_proinfo_pdev);
        return -EINVAL;
    }
    
    spk_proinfo_root_dev = root_device_register("spk_proinfo");
    if (IS_ERR(spk_proinfo_root_dev)) {
        pr_err("SPK_PROINFO_INIT: ERROR REGISTERING ROOT DEVICE!\n");
        platform_device_unregister(spk_proinfo_pdev);
        platform_driver_unregister(&spk_proinfo_pdrv);
        return -EINVAL;
    }
    
    ret = device_create_file(spk_proinfo_root_dev, dev_attr_spk_cali);
    if (ret != 0) {
        pr_err("SPK_PROINFO_INIT: ERROR CREATING DEVICE NODE!\n");
        platform_device_unregister(spk_proinfo_pdev);
        platform_driver_unregister(&spk_proinfo_pdrv);
        root_device_unregister(spk_proinfo_root_dev);
        return ret;
    }
    
    return 0;
}
module_init(spk_proinfo_init);

static void __exit spk_proinfo_exit(void)
{
    pr_info("SPK_PROINFO_EXIT: disable driver nodes\n");
    
    platform_device_unregister(spk_proinfo_pdev);
    platform_driver_unregister(&spk_proinfo_pdrv);
    root_device_unregister(spk_proinfo_root_dev);
    
    return;
}
module_exit(spk_proinfo_exit);
