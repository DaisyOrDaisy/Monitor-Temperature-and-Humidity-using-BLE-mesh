#include "data_channel_config.h"
#include<stdio.h>
#include "esp_log.h"
static char data_copy[10];
static  tem_hum_data_t tem_hum_data;
static char key[20]="hello";
static char *Request;
static char req[256];


void prepare_data(char *input_data){
   
    strcpy(data_copy,input_data);
    char *ptr=strtok(data_copy,":");
    sprintf(tem_hum_data.temp,"%s",ptr);
    ptr=strtok(NULL,":");
    sprintf(tem_hum_data.hum,"%s",ptr);
   

    
}
int get_channel_info(uint16_t destination_add,channel_t listChannel[],uint8_t lastChannelIndex){
    char dst[5];
    sprintf(dst,"%x",destination_add);
    for(int i=0;i<=lastChannelIndex;i++){
        if(strstr(listChannel[i].channel,dst)&&lastChannelIndex!=0){
                ESP_LOGI("CHANNEL","%s",listChannel[i].channel);
                char dp[32];
                sprintf(dp,"%s",listChannel[i].channel);
                 char *pt = strtok(dp, ":");
                 pt = strtok(NULL, ":");
                 if(pt){
                    sprintf(key,"%s",pt);
                    ESP_LOGI("key","%s",key);
                    return 1;   
                 }else{
                    return 0;
                 }
                 
            }else{
                continue;
            }
        
    }
    return 0;
}
char* prepare_resquest(char *input_data,uint16_t destination_add,channel_t listChannel[],uint8_t lastChannelIndex){
    prepare_data(input_data);
    int flag=get_channel_info(destination_add,listChannel,lastChannelIndex);
    if(flag){
         sprintf(req,"POST https://api.thingspeak.com/update?api_key=%s&field1=%s&field2=%s HTTP/1.0\r\n"
        "Host: "WEB_SERVER":"WEB_PORT"\r\n"
        "User-Agent: esp-idf/1.0 esp32\r\n"
        "\r\n",key,tem_hum_data.temp,tem_hum_data.hum);
        Request=req;
        return Request;
    }else{
        return NULL;
    }
}

void print_list_channel(channel_t listChannel[],uint8_t index){
    ESP_LOGI("Channel list","===============>{ \nvalid index:%d\n",index);
    for(int i=0;i<index;i++){
        ESP_LOGI("channel list","channel %d = %s ",(i+1),listChannel[i].channel);
    }
     ESP_LOGI("Channel list","}<===============");
}