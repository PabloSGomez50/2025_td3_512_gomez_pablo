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

// Autor del modulo
#define AUTHOR              "Hernandez-Jorja"
// Nombre del char device
#define CHRDEV_NAME         "egb"
// Minor number del char device
#define CHRDEV_MINOR        1
// Cantidad de char devices
#define CHRDEV_COUNT        1
// Cantidad de caracteres maximos en el buffer
#define SHARED_BUFFER_SIZE  64
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
    {.compatible = "Her_Jorj,egb", },
    {}
};
MODULE_DEVICE_TABLE(of, serdev_ids);
// Serdev Device
static struct serdev_device *g_serdev = NULL;

// Buffer de datos para compartir entre user y kernel
static char shared_buffer[SHARED_BUFFER_SIZE];

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
        .name = "egb_uart_driver",
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
    // Variables auxiliares
    int to_copy, not_copied;
    // Se fija cuanto hay que copiar, fijandose si la cantidad
    // a leer o lo que queda del buffer es menor. Evita leer 
    // fuera de los limites del buffer
    to_copy = min(size, sizeof(shared_buffer) - *off);
    // Si el offset es mas grande al buffer compartido no hay
    // mas para leer -> return 0 para indicar EOF
    if(*off >= to_copy) return 0;
    // Copia del kernel space al user space, devuelve cuanto no se copio
    not_copied = copy_to_user(buff, shared_buffer + *off, to_copy);
    // actualiza el offset
    *off = to_copy - not_copied;
    // Retorna la cantidad copiada
    return to_copy - not_copied;
}

/**
 * @brief Operacion si se escribe el char device
 */
static ssize_t chr_dev_write(struct file *f, const char __user *buff, size_t size, loff_t *off) {
    // Variables auxiliares
    int to_copy, not_copied;
    // Se fija cuanto puede copiar sin exceder el shared buffer
    to_copy = min(size, sizeof(shared_buffer) - 1);
    // Copia del user space al kernel space, devuelve cuanto no se copio
    not_copied = copy_from_user(shared_buffer, buff, to_copy);
    // Agrego el /0 cuando uso el comando echo
    for(int = 0; i < to_copy - not_copied; i++) {
        if(shared_buffer[i] == '/n') {
            shared_buffer[i + 1] = '\0';
            break;
        }
    }
    // Hago un print de lo que se escribio efectivamente
    printk("%s: Escrito sobre /dev/%s - %s\n", AUTHOR, CHRDEV_NAME, shared_buffer);
    // Se verifica la UART
    if(g_serdev != NULL) {
        // Se envia al UART
        serdev_device_write_buf(g_serdev, shared_buffer, to_copy - not_copied);
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
    printk(KERN_INFO "%s: Se conecto UART utilizando egb_uart_probe\n", AUTHOR);
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
    static char str[SHARED_BUFFER_SIZE] = {0};
    static int i = 0;
    // Se detecta caracter valido
    if(*buffer) {
        // Se copia el caracter
        str[i++] = *buffer;
    }
    // Se verifica el fin de cadena
    if(i == SHARED_BUFFER_SIZE || str[i-1] == '\0') {
        // Imprimo
        printk(KERN_INFO "%s: Se recibieron %d bytes por UART. El mensaje fue '%s'\n", AUTHOR, i-1, str);
        // Reinicio de variables
        memset(str, 0, i);
        i = 0;
    }
    return size;
}

/**
 * @brief Crea el char device
 * @return Devuelve cero si la inicializacion fue correcta
 */
static int __init module_kernel_init(void) {
    // Reservar char device
    if(alloc_chrdev_region(&chrdev_number, CHRDEV_MINOR, CHRDEV_COUNT, AUTHOR) < 0) {
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
    if(IS_ERR(device_create(chrdev_class, NULL, chrdev_number, NULL, AUTHOR))) {
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
