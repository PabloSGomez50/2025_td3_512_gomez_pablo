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

// Autor del modulo
#define AUTHOR              "PabloG_CE"
// Nombre del char device
#define CHRDEV_NAME         "td3_egb"
// Minor number del char device
#define CHRDEV_MINOR        1
// Cantidad de char devices
#define CHRDEV_COUNT        1
// Cantidad de caracteres maximos en el buffer
#define SHARED_BUFFER_SIZE  64
#define UART_BUFFER_SIZE    512
// Baud rate del UART
#define BAUD_RATE           115200
// Paridad
#define PARITY              SERDEV_PARITY_NONE

// Variable que guarda los major y minor numbers del char device
static dev_t chrdev_number;
// Variable que representa el char device
static struct cdev chrdev;
// Clase del char device
static struct class *chrdev_class;
// ID
static struct of_device_id serdev_ids[] = {
    { .compatible = "PabloG,egb", },
    {}
};
MODULE_DEVICE_TABLE(of, serdev_ids);


// Serdev Device
static struct serdev_device *g_serdev = NULL;
// Buffer de datos para compartir entre user y kernel
static char shared_buffer[SHARED_BUFFER_SIZE];
static int recibido = 0;
static size_t recibido_size = 0;
static wait_queue_head_t waitqueue;

static char uart_buff[UART_BUFFER_SIZE];
int uart_buff_index;
spinlock_t uart_lock;


// Prototipos de los callbacks de fops
static ssize_t chr_dev_read(struct file *f, char __user *buff, size_t size, loff_t *off);
static ssize_t chr_dev_write(struct file *f, const char __user *buff, size_t size, loff_t *off);
// Prototipos de los callbacks del driver uart 
static int egb_uart_probe(struct serdev_device *serdev);
static void egb_uart_remove(struct serdev_device *serdev);
// Prototipos de las operaciones del UART
static size_t egb_uart_recv(struct serdev_device *serdev, const unsigned char *buffer, size_t size);

// Operaciones de archivos del char device
static struct file_operations chrdev_ops = {
    .owner = THIS_MODULE,
    .read = chr_dev_read,
    .write = chr_dev_write
};

// Operaciones del driver uart
static struct serdev_device_driver egb_uart_driver = {
    .probe = egb_uart_probe,
    .remove = egb_uart_remove,
    .driver = {
        .name = "egb_uart",
        .of_match_table = serdev_ids,
    }
};

// Operaciones del UART
static const struct serdev_device_ops egb_uart_ops = {
    .receive_buf = egb_uart_recv,
};

/**
 * @brief Operacion si se lee el char device
 */
static ssize_t chr_dev_read(struct file *f, char __user *buff, size_t size, loff_t *off) {
    int not_copied;
    size_t bytes_to_copy;
    unsigned long flags;

    // Si el usuario ya leyo un dato terminamos la lectura
    if(*off > 0) {
        printk(KERN_INFO "%s: Termino de leer\n", AUTHOR);
	    return 0;
    }
    // Bloqueamos hasta recibir dato por UART
    if (wait_event_interruptible(waitqueue, recibido == 1)) {
        // Si el usuario presiona Ctrl+C o recibe una señal
        return -ERESTARTSYS;
    }
    // Copiamos al usuario
    spin_lock_irqsave(&uart_lock, flags);
    if (recibido != 1) {
        spin_unlock_irqrestore(&uart_lock, flags);
        return 0;
    }
    bytes_to_copy = min(size, recibido_size);
    spin_unlock_irqrestore(&uart_lock, flags);

    not_copied = copy_to_user(buff, uart_buff, bytes_to_copy);
    if (not_copied) {
        printk(KERN_WARNING "%s: Error copiando al user space\n", AUTHOR);
    }
    // Actualizamos el offset
    *off += bytes_to_copy - not_copied;
    // Limpiamos el flag para volver a bloquear
    printk(KERN_INFO "%s: Leido del char device (%zu bytes)\n", AUTHOR, bytes_to_copy - not_copied);

    memset(uart_buff, 0, UART_BUFFER_SIZE);
    uart_buff_index = 0;
    recibido = 0;

    return bytes_to_copy - not_copied;
}

/**
 * @brief Operacion si se escribe el char device
 */
static ssize_t chr_dev_write(struct file *f, const char __user *buff, size_t size, loff_t *off) {
    // Variables auxiliares
    int to_copy, not_copied, len;
    // Se fija cuanto puede copiar sin exceder el shared buffer
    to_copy = min(size, sizeof(shared_buffer) - 1);
    // Copia del user space al kernel space, devuelve cuanto no se copio
    not_copied = copy_from_user(shared_buffer, buff, to_copy);
    // Guardamos la cantidad de datos recibidos:
    len = to_copy - not_copied;
    // Usamos otra variable para hacer el printk pero enviar el dato con el \n
    char printk_buff[SHARED_BUFFER_SIZE];
    memcpy(printk_buff, shared_buffer, len);
    printk_buff[len] = '\0';
    if(len > 0 && printk_buff[len - 1] == '\n')
        printk_buff[len - 1] = '\0';
    // Hago un print de lo que se escribio efectivamente
    printk("%s: Escrito sobre /dev/%s - %s\n", AUTHOR, CHRDEV_NAME, printk_buff);
    // Se verifica la UART
    if(g_serdev != NULL) {
        // Se envia al UART
        serdev_device_write_buf(g_serdev, shared_buffer, len);
        // Se devuelve cuanto se copio
        return to_copy - not_copied;
    }
    // Retorna 0 si no hay UART
    return 0;
}

/**
 * @brief Operacion si se detecta UART. Crea el serdev device y le asigna las operaciones
 * @return Devuelve cero si la inicializacion fue correcta
 */
static int egb_uart_probe(struct serdev_device *serdev) {
    printk(KERN_INFO "%s: Se conecto UART\n", AUTHOR);
    // Se asignan las operaciones del UART
    serdev_device_set_client_ops(serdev, &egb_uart_ops);
    // Se intenta abrir el UART
    if(serdev_device_open(serdev)) {
        printk(KERN_ERR "%s: Error abriendo el UART\n", AUTHOR);
        return -1;
    }
    // Configuracion de UART
    serdev_device_set_baudrate(serdev, BAUD_RATE);
    serdev_device_set_flow_control(serdev, false);
    serdev_device_set_parity(serdev, PARITY);
    // Guardamos el punto al serdev device
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
    // Se cierra el UART
    serdev_device_close(serdev);
}

/**
 * @brief Operacion si se reciben caracteres de UART
 */
static size_t egb_uart_recv(struct serdev_device *serdev, const unsigned char *buffer, size_t size) {

	int to_copy = min(size, SHARED_BUFFER_SIZE - 1);
    for (int i = 0; i < to_copy; i++) {
        if (buffer[i] == '\n') {
            uart_buff[uart_buff_index] = '\0';
            printk(KERN_INFO "%s: Recibido completo por UART: '%s'\n", AUTHOR, uart_buff);
            recibido_size = uart_buff_index;
            uart_buff_index = 0;
            recibido = 1;
            wake_up_interruptible(&waitqueue);
            return to_copy;
        }
        uart_buff[uart_buff_index++] = buffer[i];
        // Overflow
        if (uart_buff_index >= UART_BUFFER_SIZE) {
            uart_buff_index = 0;
            printk(KERN_WARNING "%s: UART buffer overflow, reiniciando indice\n", AUTHOR);
        }
    }
	printk(KERN_INFO "%s: Recibido por UART: '%s'\n", AUTHOR, shared_buffer);
    return size;
}

/**
 * @brief Crea el char device
 * @return Devuelve cero si la inicializacion fue correcta
 */
static int __init module_kernel_init(void) {
    init_waitqueue_head(&waitqueue);
    spin_lock_init(&uart_lock);
    // Reservar char device
    if(alloc_chrdev_region(&chrdev_number, CHRDEV_MINOR, CHRDEV_COUNT, CHRDEV_NAME) < 0) {
        printk(KERN_ERR "%s: No se pudo crear el char device\n", AUTHOR);
        return -1;
    }
    // Mensaje para buscar el char device
    printk(KERN_INFO "%s: Se reservo char device con major %d y minor %d\n", AUTHOR, MAJOR(chrdev_number), MINOR(chrdev_number));
    // Inicializa el char device y sus operaciones de archivos
    cdev_init(&chrdev, &chrdev_ops);
    // Asocia el char device a la zona reservada
    if(cdev_add(&chrdev, chrdev_number, CHRDEV_COUNT) < 0) {
        unregister_chrdev_region(chrdev_number, CHRDEV_COUNT);
        printk(KERN_ERR "%s: No se pudo crear el char device\n", AUTHOR);
        return -1;
    }
    // Crea la estructura de clase
    chrdev_class = class_create(AUTHOR);
    // Verifica error
    if(IS_ERR(chrdev_class)) {
        unregister_chrdev_region(chrdev_number, CHRDEV_COUNT);
        printk(KERN_ERR "%s: No se pudo crear el char device\n", AUTHOR);
        return -1;
    }
    // Se crea el archivo del char device
    if(IS_ERR(device_create(chrdev_class, NULL, chrdev_number, NULL, CHRDEV_NAME))) {
        class_destroy(chrdev_class);
        unregister_chrdev_region(chrdev_number, CHRDEV_COUNT);
        printk(KERN_ERR "%s: No se pudo crear el char device\n", AUTHOR);
        return -1;
    }
    // Registro driver para UART
    if(serdev_device_driver_register(&egb_uart_driver)) {
        printk(KERN_ERR "%s: No se pudo crear el driver de UART\n", AUTHOR);
        return -1;
    }
    // Mensaje de correcta finalizacion
    printk(KERN_INFO "%s: Fue creado el char device y driver UART\n", AUTHOR);
    return 0;
}

/**
 * @brief Libera el espacio reservado del char device
 */
static void __exit module_kernel_exit(void) {
    device_destroy(chrdev_class, chrdev_number);
    class_destroy(chrdev_class);
    unregister_chrdev_region(chrdev_number, CHRDEV_COUNT);
    cdev_del(&chrdev);
    serdev_device_driver_unregister(&egb_uart_driver);
    printk(KERN_INFO "%s: Modulo removido\n", AUTHOR);
}

// Funciones de inicializacion y salida
module_init(module_kernel_init);
module_exit(module_kernel_exit);

// Informacion del modulo
MODULE_LICENSE("GPL");
MODULE_AUTHOR(AUTHOR);
MODULE_DESCRIPTION("Modulo de kernel EGB");