#include <linux/module.h>
#include <linux/init.h>

static int __init msucks_init(void)
{
    pr_info("msucks: Welcome to MeizuSucks Project's Kernel!\n");
    pr_info("msucks: Device: Meizu M6 Note\n");
	pr_info("msucks: Device codename: m1721\n");
	pr_info("msucks: MeizuSucks version: MSPKv2\n");
    pr_info("msucks: Android version: 7.1.2\n");
	pr_info("msucks: Device maintainer: Teledurak\n");
    return 0;
}
module_init(msucks_init);

static void __exit msucks_exit(void)
{
    return;
}
module_exit(msucks_exit); 
