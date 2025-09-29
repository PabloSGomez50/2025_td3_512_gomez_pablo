#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/delay.h>
// #include "gpio_driver.h"

// Etiqueta para el autor del modulo
#define AUTHOR "PABLO_GOMEZ-TP5"

// Puntero para primer hilo
static struct task_struct *thread1;
// Puntero para segundo hilo
static struct task_struct *thread2;

struct print_data_t {
	uint16_t time;
	char message[64];
};

// Saludo cada 500mS
struct print_data_t thread1_data = {
	.time = 500,
	.message = "Hola desde el kernel!"
};
struct print_data_t thread2_data = {
	.time = 500,
	.message = "Chau desde el kernel!"
};

static int thread_print_data_f(void *params) {
	struct print_data_t *data = (struct print_data_t *)params;

	while(!kthread_should_stop()) {
		// Mensaje para el Kernel
		printk(KERN_INFO "%s: %s\n", AUTHOR, data->message);
		// Demora de 500 mS
		msleep(data->time);
	}
	return 0;
}


/**
 * @brief Se llama cuando el modulo se carga en el kernel
*/
static int __init kernel_module_init(void) {
	printk(KERN_INFO "%s: Inicio de programa!\n", AUTHOR);

	thread1 = kthread_run(thread_print_data_f, &thread1_data, "thread1");
	if (IS_ERR(thread1)) {
		printk(KERN_ERR "%s: Error al crear thread 1\n", AUTHOR);
		return -1;
	}

	thread2 = kthread_run(thread_print_data_f, &thread2_data, "thread2");
	if (IS_ERR(thread2)) {
		printk(KERN_ERR "%s: Error al crear thread 2\n", AUTHOR);
		// Frenar el hilo 1
		kthread_stop(thread1);
		return -1;
	}

	return 0;
}

/**
 * @brief Se llama cuando el modulo se quita del kernel
 */
static void __exit kernel_module_exit(void) {
	// Salida del modulo
	printk(KERN_INFO "%s: Limpiando los recursos!\n", AUTHOR);
	if (thread1) {
		kthread_stop(thread1);
	}

	if (thread2) {
		kthread_stop(thread2);
	}
}

// Registro la funcion de inicializacion y salida
module_init(kernel_module_init);
module_exit(kernel_module_exit);

// Informacion del modulo
MODULE_LICENSE("GPL");
MODULE_AUTHOR(AUTHOR);
MODULE_DESCRIPTION("UTN FRA Tecnicas Digitales III - TP5: GPOS");
