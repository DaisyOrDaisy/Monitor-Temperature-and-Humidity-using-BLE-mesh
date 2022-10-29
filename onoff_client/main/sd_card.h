#ifndef _SD_CARD_H_
#define _SD_CARD_H_
#include "esp_err.h"
esp_err_t sd_card_init();
esp_err_t write_file(char s_data[]);
void read_file();
#endif