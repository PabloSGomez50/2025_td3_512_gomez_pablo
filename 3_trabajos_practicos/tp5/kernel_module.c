#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include "gpio_driver.h"

// Etiqueta para el autor del modulo
#define AUTHOR "PABLO_GOMEZ-TP5"

// Puntero para primer hilo
static struct task_struct *thread1;
// Puntero para segundo hilo
static struct task_struct *thread2;

uint8_t gpio_pin = 16;

static int thread_led_on_f(void *params) {
	uint8_t gpio = *(uint8_t *)params;
	
	while(!kthread_should_stop()) {
		// Mensaje para el Kernel
		printk(KERN_INFO "%s: Encendiendo led en %d\n", AUTHOR, gpio);
		gpio_set(gpio);
		// Demora
		msleep(1000);
	}
	return 0;
}

static int thread_led_off_f(void *params) {
	uint8_t gpio = *(uint8_t *)params;

	msleep(500);
	while(!kthread_should_stop()) {
		// Mensaje para el Kernel
		printk(KERN_INFO "%s: Apagando led en %d\n", AUTHOR, gpio);
		gpio_clr(gpio);
		// Demora
		msleep(1000);
	}
	return 0;
}


/**
 * @brief Se llama cuando el modulo se carga en el kernel
*/
static int __init kernel_module_init(void) {
	printk(KERN_INFO "%s: Inicio de programa!\n", AUTHOR);
	
	void __iomem* map_addr = gpio_map();
	if (!map_addr) {
		printk(KERN_ERR "%s: No se pudo mapear la memoria\n", AUTHOR);
		return -1;
	}
	printk(KERN_INFO "%s: Memoria mapeada en %p\n", AUTHOR, map_addr);
	gpio_set_dir_output(gpio_pin);

	thread1 = kthread_run(thread_led_on_f, &gpio_pin, "thread1");
	if (IS_ERR(thread1)) {
		printk(KERN_ERR "%s: Error al crear thread 1\n", AUTHOR);
		return -1;
	}

	thread2 = kthread_run(thread_led_off_f, &gpio_pin, "thread2");
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
	gpio_clr(gpio_pin);
	gpio_unmap();
	if (thread1) {
		kthread_stop(thread1);
	}

	if (thread2) {
		kthread_stop(thread2);
	}
	printk(KERN_INFO "%s: Hilos detenidos!\n", AUTHOR);
}

// Registro la funcion de inicializacion y salida
module_init(kernel_module_init);
module_exit(kernel_module_exit);

// Informacion del modulo
MODULE_LICENSE("GPL");
MODULE_AUTHOR(AUTHOR);
MODULE_DESCRIPTION("UTN FRA Tecnicas Digitales III - TP5: GPOS");
