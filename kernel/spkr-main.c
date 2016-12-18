#include <linux/init.h>
#include <linux/module.h>
#include <linux/version.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/kfifo.h>
#include <linux/ioctl.h>
#include <asm/uaccess.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/timer.h>
#include <linux/jiffies.h>
#include "./spkr-io.h"


MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Gabriel Garcia <g.gardiles@alumnos.upm.es> and Sergio Vicente <...@alumnos.upm.es");
MODULE_DESCRIPTION("intspkr module for embedded system");
MODULE_VERSION("dev");

#define SUCCESS 0
#define DEVICE_NAME "intspkr"	/* Dev name as it appears in /proc/devices   */

/**	Global variables	**/
int i;
int ii;
int j;
int x = 2;

static int minor = 0;
static unsigned int buffer_size = PAGE_SIZE;
static unsigned int buffer_threshold = PAGE_SIZE;
static int elements_to_write = 0;
static int Device_Open_W = 0;	/* Is device open?  */
static int local_open = 0;
struct mutex open_mutex;
spinlock_t fifo_lock, fsync_lock;
static struct kfifo fifo;
static struct timer_list timer;
static wait_queue_head_t cola, fsync_cola;

static struct cdev c_dev;     	// Global variable for the char device structure
static struct class *cl;     	// Global variable for the device class
static dev_t midispo;		// Global variable for the device number

module_param(minor, int, S_IRUGO);
module_param(buffer_size, uint, S_IRUGO);
module_param(buffer_threshold, uint, S_IRUGO);

// callback variables
unsigned char fifo_buf[4];
unsigned long freq, ms;
int n;

void add_timer_if_not_set(unsigned long d);
void spkr_timer_callback( unsigned long data );
int is_memory_accessible(const char *buff, size_t count);

void spkr_timer_callback( unsigned long data )
{
	spin_lock_bh(&fifo_lock);

	// Skip function if there are less than 4 elements in FIFO
	if (kfifo_len(&fifo)<4){
		printk("menos de 4 elementos en fifo speaker off\n");
		spkr_off();
		spin_unlock_bh(&fifo_lock);
		return;
	}

	n = kfifo_out(&fifo, fifo_buf, 4);
	freq = (fifo_buf[1]<<8)+fifo_buf[0];
	ms = (fifo_buf[3]<<8)+fifo_buf[2];
	
	printk("frequency: %lu \tms: %lu\n", freq, ms);
	//printk("fifo_buf (frequency): %02X%02X\n",fifo_buf[1],fifo_buf[0]);
	//printk("fifo_buf (ms): %02X%02X\n",fifo_buf[3],fifo_buf[2]);
	
	
	if ((freq>0) && (ms > 0)){
		spkr_play(freq);
	}else{ //pausa o silencio
		printk("pausa\n");
		spkr_off();
	}

	printk("elements_to_write: %d\tkfifo_avail()= %d\n", elements_to_write,kfifo_avail(&fifo));
	//despertar escritor si hay huevo

	if (elements_to_write > 0){
		printk("elements_to_write > 0");
		if 	(kfifo_avail(&fifo) >= buffer_threshold
		  || kfifo_avail(&fifo) >= elements_to_write
		  || kfifo_is_empty(&fifo)){

			wake_up_interruptible(&cola);
		}
	}
	
	//reprogramar sonido si hay mas de 3 en la cola, sino apagar speaker
	if (kfifo_len(&fifo)>3){
		add_timer_if_not_set(ms);
		spin_unlock_bh(&fifo_lock);
	}else{ // no quedan elementos pendientes en kfifo y apagar el speaker
		printk("no quedan sonidos\n");
		spin_unlock_bh(&fifo_lock);
		wake_up_interruptible(&fsync_cola);
		spkr_off();
	}
}

//Call inside of spinlock
void add_timer_if_not_set(unsigned long msecs){
	if (timer_pending(&timer)){
		printk("timer pending\n");
		return;
	}

	//Program timer to handle data
	timer.data = (unsigned long) 0;
	timer.function = spkr_timer_callback;
	timer.expires = jiffies + msecs_to_jiffies(msecs); /* parameter */
	add_timer(&timer);

}

int is_memory_accessible(const char *buff, size_t count){
	const char *buff_ch;
	/*if(access_ok(VERIFY_READ, buff, count) != 0){
		printk("access to memory NOT OK: %x\n", access_ok(VERIFY_READ, buff, count));
		return 0;
	}*/
	if (get_user(buff_ch, buff) != 0){
		printk("access to memory NOT OK");
		return 0;	
	}
	return 1;
}

/*
 * Called when a process tries to open the device file, like
 * "open /dev/intspkr"
 */
static int device_open(struct inode *inode, struct file *filp)
{
	/******************************************************************
	 * CHECK IF WRITE OR READ -> IF WRITE, ALLOW ONLY ONE OPEN (MUTEX)
	 *****************************************************************/
	printk("DEVICE OPEN - ");
	if (filp->f_mode & FMODE_READ){
		printk("READ MODE\n");
	}else if (filp->f_mode & FMODE_WRITE){
		printk("WRITE MODE - ");

		mutex_lock(&open_mutex);
		local_open = Device_Open_W++;
		mutex_unlock(&open_mutex);

		if (local_open){
			printk("BUSY\n");
			return -EBUSY;
		}
		printk("COUNT: %d\n", local_open+1);
	}

	try_module_get(THIS_MODULE);
	return SUCCESS;
}

/* 
 * Called when a process closes the device file.
 * "close /dev/intspkr"
 */
static int device_release(struct inode *inode, struct file *file)
{
	/* Free /dev/intspkr for opening in write mode */
	if (file->f_mode & FMODE_WRITE){
		mutex_lock(&open_mutex);
		Device_Open_W--;
		mutex_unlock(&open_mutex);
	}
			

	/* 
	 * Decrement the usage count, or else once you opened the file, you'll
	 * never get get rid of the module. 
	 */
	module_put(THIS_MODULE);
	printk(KERN_ALERT "DEVICE RELEASED\n");
	return 0;
}

/*  
 * Called when a process writes to dev file:
 * "echo "hi" > /dev/intspkr"
 */
static ssize_t
device_write(struct file *filp, const char *buff, size_t count, loff_t *f_pos)
{
	printk(KERN_ALERT "DEVICE WRITE START count=%d\n",(int) count);
	
	spin_lock_bh(&fsync_lock);
	if(count < 0){
		return -EINVAL;
	} 
	spin_unlock_bh(&fsync_lock);

	if(is_memory_accessible(buff, count) == 0){
		return -EFAULT;
	}
		
	spin_lock_bh(&fifo_lock);
	elements_to_write = count;

	//Loop through all elements in buffer and put them in kfifo
	for(j=0; j<(int)count; j++){
		//printk("KFIFO_PUT %x\n", buff[j]);
		
		if (kfifo_avail(&fifo)==0){
			printk("write - kfifo lleno\n");
			spin_unlock_bh(&fifo_lock);
			
			add_timer_if_not_set(200);

			if(wait_event_interruptible(cola, kfifo_avail(&fifo)>0)){
				return -ERESTARTSYS;
			}
			printk(KERN_ALERT "WAITQUEUE - kfifo liberado\n");
			
			spin_lock_bh(&fifo_lock);
		}
		
		kfifo_put(&fifo, (char)buff[j]);
		elements_to_write--;
	}

	add_timer_if_not_set(200);

	spin_unlock_bh(&fifo_lock);

	printk(KERN_ALERT "DEVICE END\n");
	return j;
}

#if (LINUX_VERSION_CODE & 0xFFFF00) == KERNEL_VERSION(3,0,0)
#pragma message("LINUX VERSION 3.0.X")
static int device_fsync(struct file *filp, int datasync)
#else
#pragma message("LINUX VERSION > 3.0.X")
static int device_fsync(struct file *filp, loff_t start, loff_t end, int datasync)
#endif  
{	
	printk("LLamada a FSYNC\n");
	spin_lock_bh(&fsync_lock);
	if(wait_event_interruptible(fsync_cola, kfifo_is_empty(&fifo) == 1)){
		return -ERESTARTSYS;
	}
	spin_unlock_bh(&fsync_lock);
	return SUCCESS;
}


static long device_ioctl(struct file *filp, unsigned int cmd, unsigned long arg) 
{

}
/*  
 * Contrato (Interface) que define la funcionalidad 
 * que implementa este device driver
 */
static const struct file_operations fops = {
	.owner			= THIS_MODULE,
	.write			= device_write,
	.open			= device_open,
	.release		= device_release,
	.fsync			= device_fsync,
	.unlocked_ioctl = device_ioctl
};



static int init_intspkr(void)
{    
    // Check if buffer_size is a power of 2
	if (buffer_size>16384){
		buffer_size = PAGE_SIZE;
	}else if(buffer_size & (buffer_size - 1)){ // not a power of 2
		for (x=2;x<16384;x=x*2){
			if (x>=buffer_size){
				buffer_size = x;
				break;
			}
		}
	}

	if (buffer_threshold > buffer_size){
		buffer_threshold = buffer_size;
	}

	printk("buffer_size: %d, buffer_threshold: %d\n",buffer_size,buffer_threshold);

	/*******************************************
	 * Inicializar Mutex, spinlock, FIFO y TIMER
	 *******************************************/
	//mutex
	mutex_init(&open_mutex);

	//spinlock
	spin_lock_init(&fifo_lock);
	spin_lock_init(&fsync_lock);

	//timer
	init_timer(&timer);

	//kfifo
	if (kfifo_alloc(&fifo, buffer_size, GFP_KERNEL)) {
		printk(KERN_WARNING "error kfifo_alloc\n");
		return -ENOMEM;
	}
	printk(KERN_INFO "KFIFO queue size: %u\n", kfifo_size(&fifo));

	//Init waitqueues
	init_waitqueue_head(&cola);
	init_waitqueue_head(&fsync_cola);

	/****************************************************
	 * Inicializar estructuras de datos para demonio udev
	 ****************************************************/
	// Reservar major y minor
	if(alloc_chrdev_region( &midispo, minor, 1, DEVICE_NAME ) < 0 ){
		printk( KERN_ALERT "Device Registration failed\n" );
		return -1;
	}
	// Iniciar la estructura de datos "cdev" que representa un dispositivo de caracteres 
	cdev_init( &c_dev, &fops );
	
	// Asociar cdev con los identificadores de dispositivo reservados (major y minor)
	if( cdev_add( &c_dev, midispo, 1 ) == -1)
	{
		printk( KERN_ALERT "Device addition failed\n" );
		device_destroy( cl, midispo );
		class_destroy( cl );
		unregister_chrdev_region( midispo, 1 );
		return -1;
	}

	printk(KERN_ALERT "INTSPKR - INIT - cdev_add()\n");

	/***********************************************************
	 * Alta de un dispositivo para su uso desde las aplicaciones 
	 ***********************************************************/

	//creación de una clase en sysfs -> /sys/class/speaker/
	if ( (cl = class_create( THIS_MODULE, "speaker" ) ) == NULL )
	{
		printk( KERN_ALERT "Class creation failed\n" );
		unregister_chrdev_region( midispo, 1 );
		return -1;
	}

	printk(KERN_ALERT "INTSPKR - INIT - class_create()\n");
	
	// Dar de alta el dispositivo de la clase creada -> /sys/class/speaker/intspkr
	// A partir de aquí "udev" creará la entrada -> /dev/intspkr
	if( device_create( cl, NULL, midispo, NULL, DEVICE_NAME) == NULL )
	{
		printk( KERN_ALERT "Device creation failed\n" );
		class_destroy(cl);
		unregister_chrdev_region( midispo, 1 );
		return -1;
	}
	
	printk(KERN_ALERT "INTSPKR - INIT - device_create()\n");
	return SUCCESS;
}

static void exit_intspkr(void)
{	
	/* *******************************************
	* Silence speaker 
	*********************************************/
	printk("salir speaker off\n");
	spkr_off();

	/* *******************************************
	* Destroy mutex 
	*********************************************/
	mutex_destroy(&open_mutex);

	/* *******************************************
	* Destroy FIFO 
	*********************************************/
	kfifo_free(&fifo);

	/* *******************************************
	* Destroy Timer 
	*********************************************/
	del_timer_sync(&timer);

	/* *******************************************
	 * Unregister the device 
	 *********************************************/
	// Remove character device data structure
	cdev_del( &c_dev );
	// Remove device from /sys/class/speaker/intspkr
	device_destroy( cl, midispo );
	// Remove class from /sys/class/speaker
	class_destroy( cl );
	// Free major and minor values
	unregister_chrdev_region( midispo, 1 );

	printk(KERN_ALERT "Device unregistered\n");

}

module_init(init_intspkr);
module_exit(exit_intspkr);
