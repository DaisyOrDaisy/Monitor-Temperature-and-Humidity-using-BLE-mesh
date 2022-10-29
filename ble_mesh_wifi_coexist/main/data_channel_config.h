#ifndef _DATA_CHANNEL_CONFIG_H
#define _DATA_CHANNEL_CONFIG_H
#include<string.h>
#include<stdint.h>
#define WEB_SERVER "api.thingspeak.com"
#define WEB_PORT "443"
#define WEB_URL "https://api.thingspeak.com/update?api_key=LETG61JK0KVE702C"

typedef struct app_channel_info{
    char channel[32];
}channel_t;
typedef struct app_tem_hum_data_t
{
    char temp[3];
    char hum[3];

}tem_hum_data_t;




char* prepare_resquest(char *input_data,uint16_t destination_add,channel_t listChannel[],uint8_t lastChannelIndex);
void print_list_channel(channel_t listChannel[],uint8_t index);
#endif