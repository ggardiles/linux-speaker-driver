#include <linux/init.h>
#include <linux/module.h>
#include <linux/version.h>
#include <linux/i8253.h>

//static int is_speaker_on = 0;
void set_spkr_frequency(unsigned int frequency) {
	printk(KERN_INFO "SPKR set frequency: %d\n", frequency);	
}

void spkr_on(void) {
	/*if (is_speaker_on){
		printk(KERN_INFO "spkr ALREADY ON\n");
		return;
	}
	is_speaker_on = 1;*/
	printk(KERN_INFO "SPKR ON\n");
}
void spkr_off(void) {
	/*if (!is_speaker_on){
		printk(KERN_INFO "spkr ALREADY OFF\n");
		return;		
	}
	is_speaker_on = 0;*/
	printk(KERN_INFO "SPKR OFF\n");
}
