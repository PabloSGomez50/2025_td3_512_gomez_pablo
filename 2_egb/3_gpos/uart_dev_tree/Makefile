FILE := dev_tree_uart
DEV_TREE := td3_uart_overlay
AUTHOR := PabloG_CE

KDIR := /lib/modules/$(shell uname -r)/build
MDIR := $(PWD)/build

obj-m += ${FILE}.o

all:
	@mkdir -p build;					\
	cp -f ${FILE}.c Makefile ${MDIR};	\
	cd build;							\
	make -C ${KDIR} M=${MDIR} modules

clean:
	@cd build;							\
	make -C ${KDIR} M=${MDIR} clean;	\
	cd ..;	\
	rm -rf build

build-devtree:
	@dtc -@ -I dts -O dtb -o build/$(DEV_TREE).dtbo $(DEV_TREE).dts

load-devtree:
	@sudo cp build/${DEV_TREE}.dtbo /boot/firmware/overlays/
	@sudo dtoverlay ${DEV_TREE}

unload-devtree:
	@sudo dtoverlay -r ${DEV_TREE}

insmod:
	@sudo insmod build/${FILE}.ko
	@sudo chmod 666 /dev/egb_commands
	@sudo chmod 666 /dev/egb_data

rmmod:
	@sudo rmmod ${FILE}

# Muestra los mensajes del kernel que imprimio este modulo
dmesg:
	@dmesg -T | grep ${AUTHOR}

help:
	@echo "all: Compila el modulo del kernel desarrollado (por defecto)"
	@echo "clean: Limpia los resultados de la compilacion"
	@echo "build-devtree: Compila el device tree overlay"
	@echo "load-devtree: Carga el device tree overlay en el sistema"
	@echo "unload-devtree: Retira el device tree overlay del sistema"
	@echo "insmod: Carga el modulo de kernel"
	@echo "rmmod: Remueve el modulo de kernel"
	@echo "help: Muestra esta ayuda"
