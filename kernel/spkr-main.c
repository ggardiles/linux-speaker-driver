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


MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Gabriel Garcia <g.gardiles@alumnos.upm.es> and Sergio Vicente <...@alumnos.upm.es");
MODULE_DESCRIPTION("intspkr module for embedded system");
MODULE_VERSION("dev");

#define SUCCESS 0
#define DEVICE_NAME "intspkr"	/* Dev name as it appears in /proc/devices   */
#define FIFO_SIZE	32

/**	Global variables	**/
int i;
int ii;
int j;

static int minor = 0;
static int Device_Open_W = 0;	/* Is device open?  */
static int local_open = 0;
struct mutex open_mutex;
spinlock_t my_lock;
static struct kfifo fifo;
static struct timer_list timer;
static wait_queue_head_t cola;

static struct cdev c_dev;     	// Global variable for the char device structure
static struct class *cl;     	// Global variable for the device class
static dev_t midispo;		// Global variable for the device number

module_param(minor, int, S_IRUGO);

// callback variables
unsigned char fifo_buf[4];
int n;

void spkr_timer_callback( unsigned long data )
{
	printk( "spkr_timer_callback called (%ld).\n", jiffies );
	spin_lock(&my_lock);

	if (kfifo_len(&fifo)<4){
		spin_unlock(&my_lock);
		return;
	}

	n = kfifo_out(&fifo, fifo_buf, 4);
	printk("fifo_buf: %x ",fifo_buf[0]);
	printk("%x ",fifo_buf[1]);
	printk("%x ",fifo_buf[2]);
	printk("%x\n",fifo_buf[3]);
	


	//despertar escritores
	wake_up_interruptible(&cola);

	if (kfifo_len(&fifo)>0){
		//Program timer to handle data
		timer.data = (unsigned long) 0;
		timer.function = spkr_timer_callback;
		timer.expires = jiffies + msecs_to_jiffies(1000); /* parameter */
		add_timer(&timer);
	}
	
	spin_unlock(&my_lock);
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
	printk(KERN_ALERT "DEVICE WRITE\n");
	if(count < 0) 
		return -EINVAL;
		

	spin_lock(&my_lock);
	//Loop through all elements in buffer and put them in
	for(j=0; j<(int)count; j++){
		//printk("KFIFO_PUT %x\n", buff[j]);
		
		if (kfifo_avail(&fifo)==0){
			//Program timer to handle data
			timer.data = (unsigned long) 0;
			timer.function = spkr_timer_callback;
			timer.expires = jiffies + msecs_to_jiffies(1000); /* parameter */
			add_timer(&timer);

			spin_unlock(&my_lock);
			if(wait_event_interruptible(cola,kfifo_avail(&fifo)>0)){
				return -ERESTARTSYS;
			}
			spin_lock(&my_lock);
		}
		
		kfifo_put(&fifo, (char)buff[j]);
	}

	if (kfifo_len(&fifo)>3){
		//Program timer to handle data
		timer.data = (unsigned long) 0;
		timer.function = spkr_timer_callback;
		timer.expires = jiffies + msecs_to_jiffies(1000); /* parameter */
		add_timer(&timer);
	}

	spin_unlock(&my_lock);
	
	return j;
}

/*  
 * Contrato (Interface) que define la funcionalidad 
 * que implementa este device driver
 */
static const struct file_operations fops = {
	.owner		= THIS_MODULE,
	.write		= device_write,
	.open		= device_open,
	.release	= device_release,
};



static int init_intspkr(void)
{    

	/*******************************************
	 * Inicializar Mutex, spinlock, FIFO y TIMER
	 *******************************************/
	//mutex
	mutex_init(&open_mutex);

	//spinlock
	spin_lock_init(&my_lock);

	//timer
	init_timer(&timer);

	//kfifo
	if (kfifo_alloc(&fifo, FIFO_SIZE, GFP_KERNEL)) {
		printk(KERN_WARNING "error kfifo_alloc\n");
		return -ENOMEM;
	}
	printk(KERN_INFO "KFIFO queue size: %u\n", kfifo_size(&fifo));

	//Kfifo waitqueue
	init_waitqueue_head(&cola);

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
