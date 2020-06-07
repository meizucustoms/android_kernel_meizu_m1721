/* 
 * We have no any license for this driver :)
 */

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

static int __init msucks_init(void)
{
    pr_info("msucks: Welcome to MeizuSucks kernel!\n");
    pr_info("msucks: msucks device: Meizu M6 Note (m1721)\n");
	pr_info("msucks: msucks device codename: msucks-m1721\n");
	pr_info("msucks: msucks kernel version: MSPKr29\n");
	pr_info("msucks: msucks maintainer: Teledurak\n");
    return 0;
}
module_init(msucks_init);

static void __exit msucks_exit(void)
{
    pr_info("msucks: Shutting down MeizuSucks welcome driver...\n");
    return;
}
module_exit(msucks_exit); 
