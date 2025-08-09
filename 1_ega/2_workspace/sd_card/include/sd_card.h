#ifndef SD_CARD_H
#define SD_CARD_H

#include "ff.h"
#include "diskio.h"

int8_t open_file_sd_card(FATFS *fs, FIL *file, const char *filename, BYTE mode);

#endif // SD_CARD_H