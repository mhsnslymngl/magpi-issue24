#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <asm/uaccess.h>
#include <linux/time.h>
#include <linux/io.h>
#include <linux/vmalloc.h>

int init_module(void);
void cleanup_module(void);
static int device_open(struct inode *, struct file *);
static int device_release(struct inode *, struct file *);
static ssize_t device_read(struct file *, char *, size_t, 
  loff_t *);
static ssize_t device_write(struct file *, const char *,
  size_t, loff_t *);

#define SUCCESS 0
#define DEVICE_NAME "chardev" /* Device name */ 
#define BUF_LEN 80 /* Maximum length of device message */ 
/*------------------------------------------------------------*/
/* Settings and macros for the GPIO connections */
#define BCM2708_PERI_BASE 0x20000000
#define GPIO_BASE (BCM2708_PERI_BASE + 0x200000) /* GPIO controller */  

#define INP_GPIO(g)  *(gpio.addr + ((g)/10)) &= ~(7<<(((g)%10)*3)) 
#define SET_GPIO_ALT(g,a) *(gpio.addr + (((g)/10))) |= (((a)<=3?(a) + 4:(a)==4?3:2)<<(((g)%10)*3))


/* GPIO clock */
#define CLOCK_BASE              (BCM2708_PERI_BASE + 0x00101000)
#define GZ_CLK_BUSY (1 << 7)
/*------------------------------------------------------------*/
/* Number of samples to capture */
#define SAMPLE_SIZE 	10000

/* Define GPIO Pins */
/* ADC 1 */
#define BIT0_PIN 7
#define BIT1_PIN 8
#define BIT2_PIN 9
#define BIT3_PIN 10
#define BIT4_PIN 11
#define BIT5_PIN 25

/* ADC 2 */
#define BIT0_PIN2 17
#define BIT1_PIN2 18
#define BIT2_PIN2 22
#define BIT3_PIN2 23
#define BIT4_PIN2 24
#define BIT5_PIN2 27
/*------------------------------------------------------------*/
struct bcm2835_peripheral {
  unsigned long addr_p;
  int mem_fd;
  void *map;
  volatile unsigned int *addr;
};

static int map_peripheral(struct bcm2835_peripheral *p);
static void unmap_peripheral(struct bcm2835_peripheral *p);
static void readScope(void); /* Read a sample */

static int Major; /* Major number set for device driver */
static int Device_Open = 0; /* Store device status */
static char msg[BUF_LEN];
static char *msg_Ptr;

static unsigned char *buf_p;

static struct file_operations fops = {
  .read = device_read,
  .write = device_write,
  .open = device_open,
  .release = device_release
};

static struct bcm2835_peripheral myclock = {CLOCK_BASE};
static struct bcm2835_peripheral gpio = {GPIO_BASE};
struct DataStruct{
  uint32_t Buffer[SAMPLE_SIZE];
  uint32_t time;
};

struct DataStruct dataStruct;

static unsigned char *ScopeBufferStart;
static unsigned char *ScopeBufferStop;
/*------------------------------------------------------------*/
static int map_peripheral(struct bcm2835_peripheral *p){
  p->addr=(uint32_t *)ioremap(GPIO_BASE, 41*4); 
  return 0;
}
static void unmap_peripheral(struct bcm2835_peripheral *p){
  iounmap(p->addr);//unmap the address
}
/*------------------------------------------------------------*/
static void readScope(){
  int counter=0;
  struct timespec ts_start,ts_stop;

  /* disable IRQ */
  local_irq_disable();
  local_fiq_disable();

  getnstimeofday(&ts_start); /* Get start time in ns */

  /* take samples */
  while(counter<SAMPLE_SIZE){
    dataStruct.Buffer[counter++]= *(gpio.addr + 13); 
  }

  getnstimeofday(&ts_stop); /* Get stop time in ns */

  /* enable IRQ */
  local_fiq_enable();
  local_irq_enable();

  /* Store the time difference in ns */
  dataStruct.time =
    timespec_to_ns(&ts_stop) - timespec_to_ns(&ts_start); 

  buf_p = (unsigned char*)&dataStruct;
  ScopeBufferStart = (unsigned char*)&dataStruct;

  ScopeBufferStop = ScopeBufferStart+
    sizeof(struct DataStruct);
}
/*------------------------------------------------------------*/
int init_module(void){
  struct bcm2835_peripheral *p=&myclock;
  int speed_id = 6; /* 1 for 19MHz or 6 for 500 MHz */

  Major = register_chrdev(0, DEVICE_NAME, &fops);
  if(Major < 0){
    printk(KERN_ALERT "Reg. char dev fail %d\n",Major);
    return Major;
  }
  printk(KERN_INFO "Major number %d.\n", Major);
  printk(KERN_INFO "created a dev file with\n");
  printk(KERN_INFO "'mknod /dev/%s c %d 0'.\n", 
    DEVICE_NAME,Major);

  /* Map GPIO */
  if(map_peripheral(&gpio) == -1){
    printk(KERN_ALERT "Failed to map the GPIO\n");
    return -1;
  }

  /* Define input ADC connections */
  INP_GPIO(BIT0_PIN);
  INP_GPIO(BIT1_PIN);
  INP_GPIO(BIT2_PIN);
  INP_GPIO(BIT3_PIN);
  INP_GPIO(BIT4_PIN);
  INP_GPIO(BIT5_PIN);

  INP_GPIO(BIT0_PIN2);
  INP_GPIO(BIT1_PIN2);
  INP_GPIO(BIT2_PIN2);
  INP_GPIO(BIT3_PIN2);
  INP_GPIO(BIT4_PIN2);
  INP_GPIO(BIT5_PIN2);

  /* Set a clock signal on Pin 4 */
  p->addr=(uint32_t *)ioremap(CLOCK_BASE, 41*4);

  INP_GPIO(4);
  SET_GPIO_ALT(4,0);
  *(myclock.addr+28)=0x5A000000 | speed_id;

  /* Wait until clock is no longer busy (BUSY flag) */
  while(*(myclock.addr+28) & GZ_CLK_BUSY) {}; 

  /* Set divider to divide by 50, to reach 10MHz. */
  *(myclock.addr+29)= 0x5A000000 | (0x32 << 12) | 0;

  /* Turn the clock on */
  *(myclock.addr+28)=0x5A000010 | speed_id;

  return SUCCESS;
}
/*------------------------------------------------------------*/
void cleanup_module(void){
  unregister_chrdev(Major, DEVICE_NAME);
  unmap_peripheral(&gpio);
  unmap_peripheral(&myclock);
}
/*------------------------------------------------------------*/
static int device_open(struct inode *inode, 
  struct file *file){
  static int counter = 0;
  if(Device_Open) return -EBUSY;
  Device_Open++;
  sprintf(msg,"Called device_open %d times\n",counter++);
  msg_Ptr = msg;
  readScope(); /* Read ADC samples into memory. */
  try_module_get(THIS_MODULE);
  return SUCCESS;
}

static int device_release(struct inode *inode, 
  struct file *file){
  Device_Open--; /* We're now ready for our next caller */
  module_put(THIS_MODULE);
  return 0;
}

static ssize_t device_read(struct file *filp,char *buffer,
  size_t length,loff_t * offset){
  int bytes_read = 0; /* To count bytes read. */
  if(*msg_Ptr == 0) return 0;

  /* Protect against going outside the buffer. */
  while(length && buf_p<ScopeBufferStop){
    if(0!=put_user(*(buf_p++), buffer++))
      printk(KERN_INFO "Problem with copy\n");
    length--;
    bytes_read++;
  }
  return bytes_read;
}
/*--------------------------------------------------------------*/
static ssize_t device_write(struct file *filp, 
  const char *buff, size_t len, loff_t * off) {
  printk(KERN_ALERT "This operation isn't supported.\n");
  return -EINVAL;
}
