#include <linux/init.h>
#include <linux/module.h>
#include <linux/version.h>
#include <asm/io.h>

#if (LINUX_VERSION_CODE & 0xFFFF00) == KERNEL_VERSION(3,0,0)
#pragma message("LINUX VERSION 3.0.X")
extern raw_spinlock_t i8253_lock;
#else
#include <linux/i8253.h>
#endif  	

//static int is_speaker_on = 0;
void set_spkr_frequency(unsigned int frequency) {
	uint32_t count = 0;
	unsigned long flags;

    //Set the PIT to the desired frequency
 	count = PIT_TICK_RATE / frequency;
	printk(KERN_INFO "SPKR set frequency: %d\n", frequency);
	raw_spin_lock_irqsave(&i8253_lock, flags);

	// Configure spkr
	outb_p(0xB6, 0x43);
	// Set frequency
	outb_p(count & 0xff, 0x42);
	outb((count >> 8) & 0xff, 0x42);

	raw_spin_unlock_irqrestore(&i8253_lock, flags);
}
void spkr_play(unsigned int frequency){
	uint32_t count = 0;
	unsigned long flags;
	

    //Set the PIT to the desired frequency
 	count = PIT_TICK_RATE / frequency;
	//printk(KERN_INFO "SPKR set frequency: %d\nSPKR ON\n", frequency);
	raw_spin_lock_irqsave(&i8253_lock, flags);

	// Configure spkr
	outb_p(0xB6, 0x43);
	// Set frequency
	outb_p(count & 0xff, 0x42);
	outb_p((count >> 8) & 0xff, 0x42);
	outb(inb_p(0x61) | 3, 0x61);

	//printk(KERN_INFO "SPKR ON\n");
	raw_spin_unlock_irqrestore(&i8253_lock, flags);
}
void spkr_on(void) {
	unsigned long flags;
	printk(KERN_INFO "SPKR ON\n");
	
	raw_spin_lock_irqsave(&i8253_lock, flags);
	outb(inb_p(0x61) | 3, 0x61);
	raw_spin_unlock_irqrestore(&i8253_lock, flags);
}
void spkr_off(void) {
	unsigned long flags;
	printk(KERN_INFO "SPKR OFF\n");
	raw_spin_lock_irqsave(&i8253_lock, flags);
	outb(inb_p(0x61) & 0xFC, 0x61);
	raw_spin_unlock_irqrestore(&i8253_lock, flags);
}
