#include "sd_card.h"

int8_t open_file_sd_card(FATFS *fs, FIL *file, const char *filename, BYTE mode) {
    FRESULT res;

    // Montar el sistema de archivos
    res = f_mount(fs, "", 1);
    if (res != FR_OK) {
        return res; // Error al montar
    }

    // Abrir el archivo
    res = f_open(file, filename, mode);
    if (res != FR_OK) {
        // f_mount(NULL, "", 1); // Desmontar en caso de error
        return res; // Error al abrir el archivo
    }

    return FR_OK; // Éxito
    
}

bool sd_card_alive(void) {
    FIL fp;
    char buf = 'A';
    UINT br;

    if (f_open(&fp, "test.txt", FA_WRITE | FA_OPEN_ALWAYS) != FR_OK) return false;
    FRESULT res = f_write(&fp, &buf, 1, &br);
    f_close(&fp);

    return (res == FR_OK);
}