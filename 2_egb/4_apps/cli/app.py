import os
import re
import select
import time
import sys
import termios

DEV_PATH = "/dev/egb_commands" # Definimos ubicacion del archivo

set_keys = {
    "PID Kp": "kp",
    "PID Kd": "kd",
    "PID Ki": "ki",
    "Resistencia objetivo": "r_target",
    "Tiempo de variación": "t_target",
    "Límite termino integral": "int_lim",
    "Límite termino derivativa": "der_lim",
    "Temperatura máxima": "max_temp",
    "Corriente máxima": "max_current",
    "Voltaje máximo": "max_voltage"
}

get_keys = {
    **set_keys,
    "Estado": "status",
    "Protecciones": "protec",
    "Voltaje": "voltage",
    "Corriente": "current",
    "Temperatura": "temp",
    # "Memoria SD": "sd_card"
}

def main():
    while True:
        match menu_principal():
            case "1":
                menu_set()
            case "2":
                menu_get()
            case "3":
                rta = enviar_uart("$start")
            case "4":
                rta = enviar_uart("$stop")
            case "5":
                read_uart()
                input("Presione tecla para continuar...")
            case "6":
                print("-------------------------------------------------")
                print("Está seguro:")
                print("-------------------------------------------------")
                print("1> Si")
                print("2> No")
                print("-------------------------------------------------")
                if input("Opción [1-2]: ").strip() == "1":
                    os.system("clear")
                    break
            case _:
                print("\nOpción invalida")
                flush_stdin()
                input("Presione tecla para continuar...")

def menu_principal():
    os.system("clear") # Limpiamos la consola
    print("-------------------------------------------------")
    print("-------------{ Aplicación UART EGB }-------------")
    print("-------------------------------------------------")
    print("Ingrese accion a realizar:")
    print("-------------------------------------------------")
    print("1> Enviar configuración")
    print("2> Obtener información")
    print("3> Iniciar PID")
    print("4> Frenar PID")
    print("5> Leer uart")
    print("6> Salir")
    print("-------------------------------------------------")
    flush_stdin()
    return input("Opción [1-6]: ").strip()

def menu_set():
    print("-------------------------------------------------")
    print("Seleccione variable a configurar:")
    print("-------------------------------------------------")
    for i, key in enumerate(set_keys.keys(), start=1):
        print(f"{i}> {key}")
    print(f"0> Volver al menu anterior")
    print("-------------------------------------------------")
    flush_stdin()

    opc = input(f"Opción [0-{len(set_keys)}]: ").strip()
    if opc == "0":
        flush_stdin()
        return
    try:
        msg = "$set " + set_keys[list(set_keys.keys())[int(opc)-1]]
    except KeyError:
        print("\nOpción invalida")
        flush_stdin()
        return
    
    print("-------------------------------------------------")
    flush_stdin()
    enviar_uart(msg + " " + input("Ingrese el valor objetivo: ").strip())

def menu_get():
    print("-------------------------------------------------")
    print("Seleccione variable a consultar:")
    print("-------------------------------------------------")

    for i, key in enumerate(get_keys.keys(), start=1):
        print(f"{i}> {key}")
    print("0> Volver al menu anterior")
    print("-------------------------------------------------")
    flush_stdin()
    opc = input(f"Opción [0-{len(get_keys)}]: ").strip()
    if opc == "0":
        flush_stdin()
        return
    try:
        msg = "$get " + get_keys[list(get_keys.keys())[int(opc)-1]]
    except KeyError:
        print("\nOpción invalida")
        flush_stdin()
        return
    
    enviar_uart(msg)

def read_uart(timeout=2.0):
    fd = os.open(DEV_PATH, os.O_RDONLY | os.O_NONBLOCK)
    resp = ""
    rlist, _, _ = select.select([fd], [], [], timeout)
    if rlist:
        data = os.read(fd, 10000)
        resp = data.decode(errors='ignore').strip()
        print(f"Respuesta: {resp}")
    else:
        print("Timeout esperando respuesta del dispositivo.")
    os.close(fd)
    return resp

def enviar_uart(msg, timeout=2.0):
    msg = msg.strip()
    print("-------------------------------------------------")
    print(f"Enviado por UART: {msg}")
    print("-------------------------------------------------")
    with open(DEV_PATH, "w") as dev:
        dev.write(msg + "\n")
    # with open(DEV_PATH, "r") as dev:
    #     resp = dev.read().strip()
    resp = read_uart(timeout)

    print("-------------------------------------------------")
    print("Presione tecla para continuar...")
    input()
    return resp

def flush_stdin():
    termios.tcflush(sys.stdin, termios.TCIFLUSH)

if __name__ == "__main__":
    if not os.path.exists(DEV_PATH):
        print(f"Dispositivo {DEV_PATH} no encontrado.")
    else:
        main()