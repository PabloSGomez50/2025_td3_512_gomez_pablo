import serial
import csv
from datetime import datetime

# Configuración del puerto serie
PUERTO = 'COM9'   # En Windows sería COM3, COM4, etc.
BAUDIOS = 115200          # Velocidad del microcontrolador

# Nombre del archivo CSV con timestamp para no sobreescribir
archivo_salida = "datos.csv"

with serial.Serial(PUERTO, BAUDIOS, timeout=1) as ser, open(archivo_salida, 'w', newline='') as f:
    writer = csv.writer(f)

    print(f"Grabando datos en {archivo_salida}. Presioná Ctrl+C para detener.")

    try:
        while True:
            linea = ser.readline().decode('utf-8').strip()
            if linea:  # Evitar líneas vacías
                # Asumimos que el micro ya manda "valor1;valor2;valor3"
                datos = linea.split(';')
                writer.writerow(datos)
                print(datos)
    except KeyboardInterrupt:
        print("\nGrabación finalizada.")