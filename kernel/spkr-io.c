#include <linux/init.h>
#include <linux/module.h>
#include <linux/version.h>
#include <linux/i8253.h>

void set_spkr_frequency(unsigned int frequency) {
	printk(KERN_INFO "spkr set frequency: %d\n", frequency);
}

void spkr_on(void) {
	printk(KERN_INFO "spkr ON\n");
}
void spkr_off(void) {
	printk(KERN_INFO "spkr OFF\n");
}
