/* 
 * We have no any license for this driver :)
 */
#include <linux/module.h>
#include <linux/init.h>

static int __init msucks_init(void)
{
    pr_info("msucks: Welcome to MeizuSucks kernel!\n");
    pr_info("msucks: Device: Meizu M6 Note\n");
	pr_info("msucks: Device codename: m1721\n");
	pr_info("msucks: MeizuSucks version: MSPKr30.1\n");
	pr_info("msucks: Device maintainer: Teledurak\n");
    return 0;
}
module_init(msucks_init);

static void __exit msucks_exit(void)
{
    return;
}
module_exit(msucks_exit); 
