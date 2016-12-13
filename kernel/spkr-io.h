#include <linux/init.h>
#include <linux/module.h>
#include <linux/version.h>
#include <linux/i8253.h>

//static int is_speaker_on = 0;
void set_spkr_frequency(unsigned int frequency);
void spkr_on(void);
void spkr_off(void);
