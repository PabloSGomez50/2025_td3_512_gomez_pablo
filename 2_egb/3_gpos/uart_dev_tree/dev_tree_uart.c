#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/serdev.h>
#include <linux/fs.h>
#include <linux/of_device.h>
#include <linux/of.h>
#include <linux/uaccess.h>
#include <linux/wait.h>

#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/list.h>

// Autor del modulo
#define AUTHOR              "PabloG_CE"
// Nombre base del char device
#define CHRDEV_NAME         "td3_egb"
#define CHRDEV_CMD_NAME     "egb_commands"
#define CHRDEV_DATA_NAME    "egb_data"
// Minor start del char device
#define CHRDEV_MINOR        0
// Cantidad de char devices (dos: commands y data)
#define CHRDEV_COUNT        2
// Minors: 0 => egb_commands, 1 => egb_data
#define MINOR_CMD           (CHRDEV_MINOR + 0)
#define MINOR_DATA          (CHRDEV_MINOR + 1)

#define SHARED_BUFFER_SIZE  64
#define UART_BUFFER_SIZE    512
#define MAX_QUEUE_MSG       68
#define BAUD_RATE           115200
#define PARITY              SERDEV_PARITY_NONE

static dev_t chrdev_number;
static struct cdev chrdev;
static struct class *chrdev_class;

static struct of_device_id serdev_ids[] = {
    { .compatible = "PabloG,egb", },
    {}
};
MODULE_DEVICE_TABLE(of, serdev_ids);

/* Serdev device */
static struct serdev_device *g_serdev = NULL;
/* Buffer de datos para escribir al UART */
static char shared_buffer[SHARED_BUFFER_SIZE];

static wait_queue_head_t waitqueue_cmd;
static wait_queue_head_t waitqueue_data;

/* Cola de mensajes para cada dispositivo */
struct uart_msg {
    struct list_head list;
    size_t len;
    size_t offset; /* bytes already consumed by user read */
    char data[];   /* flexible array member */
};
static LIST_HEAD(msg_list_cmd);
static LIST_HEAD(msg_list_data);
static spinlock_t msg_lock_cmd;
static spinlock_t msg_lock_data;

static char uart_buff[UART_BUFFER_SIZE];
int uart_buff_index = 0;
spinlock_t uart_lock;

/* Prototipos fops */
static unsigned int chr_dev_poll(struct file *file, poll_table *wait);
static ssize_t chr_dev_read(struct file *f, char __user *buff, size_t size, loff_t *off);
static ssize_t chr_dev_write(struct file *f, const char __user *buff, size_t size, loff_t *off);

/* Prototipos serdev */
static int egb_uart_probe(struct serdev_device *serdev);
static void egb_uart_remove(struct serdev_device *serdev);
static size_t egb_uart_recv(struct serdev_device *serdev, const unsigned char *buffer, size_t size);

/* fops */
static struct file_operations chrdev_ops = {
    .owner = THIS_MODULE,
    .read = chr_dev_read,
    .write = chr_dev_write,
    .poll = chr_dev_poll,
};

static struct serdev_device_driver egb_uart_driver = {
    .probe = egb_uart_probe,
    .remove = egb_uart_remove,
    .driver = {
        .name = "egb_uart",
        .of_match_table = serdev_ids,
    }
};

static const struct serdev_device_ops egb_uart_ops = {
    .receive_buf = egb_uart_recv,
};

static int msg_list_count_locked(struct list_head *list)
{
    struct uart_msg *m;
    int c = 0;
    list_for_each_entry(m, list, list)
        c++;
    return c;
}

/* Helper to select per-minor resources */
static void select_resources(struct file *file,
                             struct list_head **list,
                             spinlock_t **lock,
                             wait_queue_head_t **wq,
                             const char **name)
{
    unsigned int minor = iminor(file_inode(file));
    if (minor == MINOR_DATA) {
        *list = &msg_list_data;
        *lock = &msg_lock_data;
        *wq = &waitqueue_data;
        *name = "egb_data";
    } else {
        *list = &msg_list_cmd;
        *lock = &msg_lock_cmd;
        *wq = &waitqueue_cmd;
        *name = "egb_commands";
    }
}

/**
 * @brief Operacion notifica si el char device esta disponible para lectura
 */
static unsigned int chr_dev_poll(struct file *file, poll_table *wait)
{
    ssize_t mask = 0;
    unsigned long flags;
    struct list_head *list;
    spinlock_t *lock;
    wait_queue_head_t *wq;
    const char *devname;

    select_resources(file, &list, &lock, &wq, &devname);

    poll_wait(file, wq, wait);

    spin_lock_irqsave(lock, flags);
    if (!list_empty(list))
        mask |= POLLIN | POLLRDNORM;
    spin_unlock_irqrestore(lock, flags);

    return mask;
}

/**
 * @brief Operacion si se lee el char device
 */
static ssize_t chr_dev_read(struct file *f, char __user *buff, size_t size, loff_t *off) {
    struct uart_msg *msg, *tmp;
    size_t total_copied = 0;
    unsigned long flags;
    struct list_head *list;
    spinlock_t *lock;
    wait_queue_head_t *wq;
    const char *devname;

    select_resources(f, &list, &lock, &wq, &devname);

    printk(KERN_INFO "%s: Lectura de /dev/%s, size=%zu\n", AUTHOR, devname, size);
    if (size == 0) {
        printk(KERN_WARNING "%s: Intento de lectura con size 0\n", AUTHOR);
        return 0;
    }

    if (wait_event_interruptible(*wq, !list_empty(list))) {
        printk(KERN_INFO "%s: Lectura interrumpida por señal\n", AUTHOR);
        return -ERESTARTSYS;
    }

    spin_lock_irqsave(lock, flags);
    list_for_each_entry_safe(msg, tmp, list, list) {
        size_t remain = msg->len - msg->offset;
        size_t avail = size - total_copied;

        if (remain > avail) {
            if (total_copied == 0) {
                spin_unlock_irqrestore(lock, flags);
                return 0;
            } else {
                break;
            }
        }

        spin_unlock_irqrestore(lock, flags);
        if (copy_to_user(buff + total_copied, msg->data + msg->offset, remain)) {
            spin_lock_irqsave(lock, flags);
            if (total_copied == 0)
                return -EFAULT;
            break;
        }
        spin_lock_irqsave(lock, flags);

        msg->offset += remain;
        total_copied += remain;

        if (msg->offset >= msg->len) {
            list_del(&msg->list);
            kfree(msg);
            printk(KERN_INFO "%s: Mensaje consumido en %s, quedan=%d\n", AUTHOR, devname, msg_list_count_locked(list));
        }

        if (total_copied >= size)
            break;
    }
    spin_unlock_irqrestore(lock, flags);

    return total_copied;
}

/**
 * @brief Operacion si se escribe el char device
 */
static ssize_t chr_dev_write(struct file *f, const char __user *buff, size_t size, loff_t *off) {
    int to_copy, not_copied, len;
    to_copy = min(size, sizeof(shared_buffer) - 1);
    not_copied = copy_from_user(shared_buffer, buff, to_copy);
    len = to_copy - not_copied;
    char printk_buff[SHARED_BUFFER_SIZE];
    memcpy(printk_buff, shared_buffer, len);
    printk_buff[len] = '\0';
    if(len > 0 && printk_buff[len - 1] == '\n')
        printk_buff[len - 1] = '\0';
    printk("%s: Escrito sobre /dev/%s - %s\n", AUTHOR, CHRDEV_NAME, printk_buff);
    if(g_serdev != NULL) {
        serdev_device_write_buf(g_serdev, shared_buffer, len);
        return to_copy - not_copied;
    }
    return 0;
}

/**
 * @brief Operacion si se detecta UART. Crea el serdev device y le asigna las operaciones
 */
static int egb_uart_probe(struct serdev_device *serdev) {
    printk(KERN_INFO "%s: Se conecto UART\n", AUTHOR);
    serdev_device_set_client_ops(serdev, &egb_uart_ops);
    if(serdev_device_open(serdev)) {
        printk(KERN_ERR "%s: Error abriendo el UART\n", AUTHOR);
        return -1;
    }
    serdev_device_set_baudrate(serdev, BAUD_RATE);
    serdev_device_set_flow_control(serdev, false);
    serdev_device_set_parity(serdev, PARITY);
    g_serdev = serdev;
    if(g_serdev == NULL) {
        printk(KERN_ERR "%s: Error configurando el UART\n", AUTHOR);
        return -1;
    }
    return 0;
}

/**
 * @brief Operacion si se remueve UART. Cierra el serdev device.
 */
static void egb_uart_remove(struct serdev_device *serdev) {
    printk(KERN_INFO "%s: UART cerrada\n", AUTHOR);
    serdev_device_close(serdev);
}

/**
 * @brief Operacion si se reciben caracteres de UART
 */
static size_t egb_uart_recv(struct serdev_device *serdev, const unsigned char *buffer, size_t size) {
    for (size_t i = 0; i < size; i++) {
        unsigned char c = buffer[i];
        
        uart_buff[uart_buff_index++] = c;
        if (uart_buff_index >= UART_BUFFER_SIZE) {
            printk(KERN_WARNING "%s: UART buffer overflow, reiniciando indice\n", AUTHOR);
            uart_buff_index = 0;
        }
        
        if (c == '\n') {
            struct uart_msg *m;
            size_t len = uart_buff_index;
            if (len == 0) {
                continue;
            }

            m = kmalloc(sizeof(*m) + len + 1, GFP_ATOMIC);
            if (!m) {
                printk(KERN_WARNING "%s: kmalloc fallo al encolar mensaje UART\n", AUTHOR);
                uart_buff_index = 0;
                continue;
            }
            INIT_LIST_HEAD(&m->list);
            m->len = len;
            m->offset = 0;
            memcpy(m->data, uart_buff, len);
            m->data[len] = '\0';

            /* Decide destino basado en primer par de caracteres */
            bool is_data = (len >= 2 && uart_buff[0] == 'D' && uart_buff[1] == ':');
            if (is_data) {
                spin_lock(&msg_lock_data);
                list_add_tail(&m->list, &msg_list_data);
                spin_unlock(&msg_lock_data);
                wake_up_interruptible(&waitqueue_data);
                printk(KERN_INFO "%s: Mensaje encolado en egb_data, total=%d\n", AUTHOR, msg_list_count_locked(&msg_list_data));
            } else {
                spin_lock(&msg_lock_cmd);
                list_add_tail(&m->list, &msg_list_cmd);
                spin_unlock(&msg_lock_cmd);
                wake_up_interruptible(&waitqueue_cmd);
                printk(KERN_INFO "%s: Mensaje encolado en egb_commands, total=%d\n", AUTHOR, msg_list_count_locked(&msg_list_cmd));
            }

            uart_buff_index = 0;
            printk(KERN_INFO "%s: Recibido completo por UART: '%s'\n", AUTHOR, m->data);
            continue;
        }
    }

    return size;
}

/**
 * @brief Crea el char devices (commands y data)
 */
static int __init module_kernel_init(void) {
    dev_t dev;
    init_waitqueue_head(&waitqueue_cmd);
    init_waitqueue_head(&waitqueue_data);
    spin_lock_init(&uart_lock);
    spin_lock_init(&msg_lock_cmd);
    spin_lock_init(&msg_lock_data);

    if (alloc_chrdev_region(&chrdev_number, CHRDEV_MINOR, CHRDEV_COUNT, CHRDEV_NAME) < 0) {
        printk(KERN_ERR "%s: No se pudo crear el char device\n", AUTHOR);
        return -1;
    }
    printk(KERN_INFO "%s: Se reservo char device con major %d y minor %d..%d\n",
           AUTHOR, MAJOR(chrdev_number), MINOR(chrdev_number), MINOR(chrdev_number)+CHRDEV_COUNT-1);

    cdev_init(&chrdev, &chrdev_ops);
    if (cdev_add(&chrdev, chrdev_number, CHRDEV_COUNT) < 0) {
        unregister_chrdev_region(chrdev_number, CHRDEV_COUNT);
        printk(KERN_ERR "%s: No se pudo crear el char device\n", AUTHOR);
        return -1;
    }

    chrdev_class = class_create(AUTHOR);
    if (IS_ERR(chrdev_class)) {
        unregister_chrdev_region(chrdev_number, CHRDEV_COUNT);
        printk(KERN_ERR "%s: No se pudo crear la clase del char device\n", AUTHOR);
        return -1;
    }

    /* Crear el nodo para egb_commands (minor 0) */
    dev = MKDEV(MAJOR(chrdev_number), MINOR(chrdev_number) + 0);
    if (IS_ERR(device_create(chrdev_class, NULL, dev, NULL, CHRDEV_CMD_NAME))) {
        class_destroy(chrdev_class);
        unregister_chrdev_region(chrdev_number, CHRDEV_COUNT);
        printk(KERN_ERR "%s: No se pudo crear el char device egb_commands\n", AUTHOR);
        return -1;
    }

    /* Crear el nodo para egb_data (minor 1) */
    dev = MKDEV(MAJOR(chrdev_number), MINOR(chrdev_number) + 1);
    if (IS_ERR(device_create(chrdev_class, NULL, dev, NULL, CHRDEV_DATA_NAME))) {
        device_destroy(chrdev_class, MKDEV(MAJOR(chrdev_number), MINOR(chrdev_number) + 0));
        class_destroy(chrdev_class);
        unregister_chrdev_region(chrdev_number, CHRDEV_COUNT);
        printk(KERN_ERR "%s: No se pudo crear el char device egb_data\n", AUTHOR);
        return -1;
    }

    if (serdev_device_driver_register(&egb_uart_driver)) {
        printk(KERN_ERR "%s: No se pudo crear el driver de UART\n", AUTHOR);
        return -1;
    }

    printk(KERN_INFO "%s: Fue creado el char devices (egb_commands, egb_data) y driver UART\n", AUTHOR);
    return 0;
}

/**
 * @brief Libera el espacio reservado del char device
 */
static void __exit module_kernel_exit(void) {
    device_destroy(chrdev_class, MKDEV(MAJOR(chrdev_number), MINOR(chrdev_number) + 1));
    device_destroy(chrdev_class, MKDEV(MAJOR(chrdev_number), MINOR(chrdev_number) + 0));
    class_destroy(chrdev_class);
    unregister_chrdev_region(chrdev_number, CHRDEV_COUNT);
    cdev_del(&chrdev);
    serdev_device_driver_unregister(&egb_uart_driver);
    printk(KERN_INFO "%s: Modulo removido\n", AUTHOR);
}

module_init(module_kernel_init);
module_exit(module_kernel_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR(AUTHOR);
MODULE_DESCRIPTION("Modulo de kernel EGB");