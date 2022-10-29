/* main.c - Application main entry point */

/*
 * Copyright (c) 2017 Intel Corporation
 * Additional Copyright (c) 2018 Espressif Systems (Shanghai) PTE LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */


// THIS NODE PUBLISHES DATA TO THE ADDRESS 0XC001

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "esp_log.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_ble_mesh_defs.h"
#include "esp_ble_mesh_common_api.h"
#include "esp_ble_mesh_networking_api.h"
#include "esp_ble_mesh_provisioning_api.h"
#include "esp_ble_mesh_config_model_api.h"
#include "esp_ble_mesh_health_model_api.h"
#include "esp_ble_mesh_generic_model_api.h"
#include "esp_ble_mesh_local_data_operation_api.h"

#include "ble_mesh_example_init.h"
#include "device_property.h"
#include "dht11.h"
#include "sd_card.h"
#include<time.h>
#include <errno.h>
#include "esp_log.h"

#include "driver/gpio.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_err.h"

#include "ble_mesh_example_nvs.h"

#include "device_property.h"
#include "esp_system.h"
#include "esp_event.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"

#include "esp_tls.h"
#include "esp_crt_bundle.h"
#include "app_http_server.h"
#include <esp_http_server.h>


#define TAG "MY NODE"

#define CID_ESP 0x02E5
#define AREA_1 0xC001
#define AREA_2 0xC002
#define AREA_3 0xC003
#define AREA_4 0xC004

#define AREA_HAVE_BEEN_CHOSEN AREA_1
static int test=0;
static char testData[6];


/* define variable and gpio for reading dht11*/
#define DHT11_GPIO GPIO_NUM_25
static struct dht11_reading dht11_data= {0};
static char dht_string_data[6]="";
/* define variable and gpio for reading dht11*/

static uint8_t dev_uuid[16] = { 0xdd, 0xdd };
// flag to check the node added to ble mesh and user property client model bound or not
static int addToMeshFlag=0;
static int bindClientModel=0;

static struct example_info_store {
    uint16_t net_idx;   /* NetKey Index */
    uint16_t app_idx;   /* AppKey Index */  
    uint16_t property_id;  
    uint16_t dst_address;  
    uint8_t pub_ttl;
    uint64_t delayTime;
} __attribute__((packed)) store = {
    .net_idx = ESP_BLE_MESH_KEY_UNUSED,
    .app_idx = ESP_BLE_MESH_KEY_UNUSED,
    .property_id=BLE_MESH_DEVICE_UNDER_TEMPERATURE_EVENT_STATISTICS,
    .dst_address=0xFFFF,
    .pub_ttl=3,
    .delayTime=20
};


#define CREATE_NET_BUF_SIMPLE_DEFINE_STATIC(_name, _size)  \
    static uint8_t net_buf_data_##_name[_size];     \
    static struct net_buf_simple _name = {          \
        .data  = net_buf_data_##_name,              \
        .len   = 6,                                 \
        .size  = _size,                             \
        .__buf = net_buf_data_##_name,              \
    }
#define CREATE_ESP_BLE_MESH_MODEL_PUB_DEFINE(_name,_size, _role) \
    CREATE_NET_BUF_SIMPLE_DEFINE_STATIC(bt_mesh_pub_msg_##_name,_size); \
    static esp_ble_mesh_model_pub_t _name = { \
        .update = (uint32_t)NULL, \
        .msg = &bt_mesh_pub_msg_##_name, \
        .dev_role = _role, \
    }
CREATE_NET_BUF_SIMPLE_DEFINE_STATIC(my_simple_buf,7);

CREATE_ESP_BLE_MESH_MODEL_PUB_DEFINE(send_my_data_pub, 10, ROLE_NODE);
static esp_ble_mesh_client_t send_my_data_client;
static esp_ble_mesh_model_op_t my_op[] = {
    { ESP_BLE_MESH_MODEL_OP_GEN_USER_PROPERTY_GET, 5, 0},
    { ESP_BLE_MESH_MODEL_OP_GEN_USER_PROPERTY_SET, 5, 0},
    { ESP_BLE_MESH_MODEL_OP_GEN_USER_PROPERTY_SET_UNACK, 5, 0},
    ESP_BLE_MESH_MODEL_OP_END,
};

static esp_ble_mesh_cfg_srv_t config_server = {
    .relay = ESP_BLE_MESH_RELAY_ENABLED,
    .beacon = ESP_BLE_MESH_BEACON_ENABLED,
    .friend_state = ESP_BLE_MESH_FRIEND_ENABLED,
    .gatt_proxy = ESP_BLE_MESH_GATT_PROXY_ENABLED,
    .default_ttl = 5,
    /* 3 transmissions with 20ms interval */
    .net_transmit = ESP_BLE_MESH_TRANSMIT(2, 20),
    .relay_retransmit = ESP_BLE_MESH_TRANSMIT(2, 20),
};
static esp_ble_mesh_health_srv_t health_server={

};


static esp_ble_mesh_model_t root_models[] = {
    ESP_BLE_MESH_MODEL_CFG_SRV(&config_server),
    ESP_BLE_MESH_SIG_MODEL(ESP_BLE_MESH_MODEL_ID_GEN_PROP_CLI, my_op,
    &send_my_data_pub, &send_my_data_client),
};

static esp_ble_mesh_elem_t elements[] = {
    ESP_BLE_MESH_ELEMENT(0, root_models, ESP_BLE_MESH_MODEL_NONE),
};

static esp_ble_mesh_comp_t composition = {
    .cid = CID_ESP,
    .elements = elements,
    .element_count = ARRAY_SIZE(elements),
};

/* Disable OOB security for SILabs Android app */
static esp_ble_mesh_prov_t provision = {
    .uuid = dev_uuid,
#if 0
    .output_size = 4,
    .output_actions = ESP_BLE_MESH_DISPLAY_NUMBER,
    .input_actions = ESP_BLE_MESH_PUSH,
    .input_size = 4,
#else
    .output_size = 0,
    .output_actions = 0,
#endif
};

static void prov_complete(uint16_t net_idx, uint16_t addr, uint8_t flags, uint32_t iv_index)
{
    ESP_LOGI(TAG, "net_idx: 0x%04x, addr: 0x%04x", net_idx, addr);
    ESP_LOGI(TAG, "flags: 0x%02x, iv_index: 0x%08x", flags, iv_index);
    store.net_idx = net_idx;

}

static void example_ble_mesh_provisioning_cb(esp_ble_mesh_prov_cb_event_t event,
                                             esp_ble_mesh_prov_cb_param_t *param)
{
    switch (event) {
    case ESP_BLE_MESH_PROV_REGISTER_COMP_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_PROV_REGISTER_COMP_EVT, err_code %d", param->prov_register_comp.err_code);
        break;
    case ESP_BLE_MESH_NODE_PROV_ENABLE_COMP_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_NODE_PROV_ENABLE_COMP_EVT, err_code %d", param->node_prov_enable_comp.err_code);
        break;
    case ESP_BLE_MESH_NODE_PROV_LINK_OPEN_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_NODE_PROV_LINK_OPEN_EVT, bearer %s",
            param->node_prov_link_open.bearer == ESP_BLE_MESH_PROV_ADV ? "PB-ADV" : "PB-GATT");
        break;
    case ESP_BLE_MESH_NODE_PROV_LINK_CLOSE_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_NODE_PROV_LINK_CLOSE_EVT, bearer %s",
            param->node_prov_link_close.bearer == ESP_BLE_MESH_PROV_ADV ? "PB-ADV" : "PB-GATT");
        break;
    case ESP_BLE_MESH_NODE_PROV_COMPLETE_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_NODE_PROV_COMPLETE_EVT");
        prov_complete(param->node_prov_complete.net_idx, param->node_prov_complete.addr,
            param->node_prov_complete.flags, param->node_prov_complete.iv_index);
        addToMeshFlag=1;
        break;
    case ESP_BLE_MESH_NODE_PROV_RESET_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_NODE_PROV_RESET_EVT");
        break;
    case ESP_BLE_MESH_NODE_SET_UNPROV_DEV_NAME_COMP_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_NODE_SET_UNPROV_DEV_NAME_COMP_EVT, err_code %d", param->node_set_unprov_dev_name_comp.err_code);
        break;
    default:
        break;
    }
}

void example_ble_mesh_send_gen_my_data_set(char *data){
    esp_ble_mesh_generic_client_set_state_t set = {0};
    esp_ble_mesh_client_common_param_t common = {0};
    esp_err_t err = ESP_OK;

    common.opcode = ESP_BLE_MESH_MODEL_OP_GEN_USER_PROPERTY_SET_UNACK;
    common.model = send_my_data_client.model;
    common.ctx.net_idx = store.net_idx;
    common.ctx.app_idx = store.app_idx;
    common.ctx.addr = store.dst_address;   /* to all nodes in Area 1*/
    common.ctx.send_ttl = store.pub_ttl;
    common.ctx.send_rel = true;//or false
    common.msg_timeout = 2000;     /* 0 indicates that timeout value from menuconfig will be used */
    common.msg_role = ROLE_NODE;

    set.user_property_set.property_id=store.property_id;
    my_simple_buf.data=(uint8_t *)data;
    my_simple_buf.__buf=my_simple_buf.data;
    set.user_property_set.property_value=&my_simple_buf;
    ESP_LOGI(TAG,"dst_add %x",common.ctx.addr);
    ESP_LOGI(TAG,"TTL %d",common.ctx.send_ttl);
    ESP_LOGI(TAG,"property id %4x",set.user_property_set.property_id);
    ESP_LOGI(TAG,"data %s",set.user_property_set.property_value->data);
    ESP_LOGI(TAG,"len %d",set.user_property_set.property_value->len);
    ESP_LOGI(TAG,"size %d",set.user_property_set.property_value->size);
    ESP_LOGI(TAG,"_buf %s",set.user_property_set.property_value->__buf);
    

     err = esp_ble_mesh_generic_client_set_state(&common, &set);
    if (err==ESP_OK) {
        ESP_LOGI(TAG, "Send Generic My Data Set Unack OK.....");
        return;
    }else if (err==ESP_ERR_INVALID_ARG)
    {
        ESP_LOGE(TAG, "============>RESULT:invalid argument! ");
    }else{
        ESP_LOGE(TAG, "============> err code : %04x ",err);

    }

}

static void example_ble_mesh_generic_client_cb(esp_ble_mesh_generic_client_cb_event_t event,
                                               esp_ble_mesh_generic_client_cb_param_t *param)
{
    ESP_LOGI(TAG, "Generic client, event %u, error code %d, opcode is 0x%04x",
        event, param->error_code, param->params->opcode);

    switch (event) {
    case ESP_BLE_MESH_GENERIC_CLIENT_GET_STATE_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_GENERIC_CLIENT_GET_STATE_EVT");
        break;
    case ESP_BLE_MESH_GENERIC_CLIENT_SET_STATE_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_GENERIC_CLIENT_SET_STATE_EVT");
        break;
    case ESP_BLE_MESH_GENERIC_CLIENT_PUBLISH_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_GENERIC_CLIENT_PUBLISH_EVT");
        break;
    case ESP_BLE_MESH_GENERIC_CLIENT_TIMEOUT_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_GENERIC_CLIENT_TIMEOUT_EVT");
        break;
    default:
        break;
    }
}
// freeRTOS to measure temparature and humidity, then send data to the proxy node.
// send DATA 
static int stt=0;
static char s_data[100]="";
void taskSendData(void *param){
   while (1)
   {
        while (addToMeshFlag==1 && bindClientModel==1)
        {
           
            dht11_data=DHT11_read();
            while (dht11_data.status!=DHT11_OK)
            {
                dht11_data=DHT11_read();
            }
            
            
                sprintf(dht_string_data,"%d:%d",dht11_data.temperature,dht11_data.humidity);
                if(sd_card_init()==ESP_OK){
                     sprintf(s_data,"[(%d)__tem:%d__hum:%d]",stt,dht11_data.temperature,dht11_data.humidity);
                    if(stt<287) 
                        stt++;
                    else
                        stt=0;
                    if(write_file(s_data)==ESP_OK){
                        ESP_LOGI("LOG DATA TO SD CARD","OK");
                    }
                }
                example_ble_mesh_send_gen_my_data_set(dht_string_data);

             vTaskDelay((store.delayTime*1000)/portTICK_PERIOD_MS);
            
            
        }
       // ESP_LOGI("FLAGS STATUS:","addMESH: %d  bindmodel: %d",addToMeshFlag,bindClientModel);
   }

    vTaskDelete(NULL);

}


static void example_ble_mesh_config_server_cb(esp_ble_mesh_cfg_server_cb_event_t event,
                                              esp_ble_mesh_cfg_server_cb_param_t *param)
{
    if (event == ESP_BLE_MESH_CFG_SERVER_STATE_CHANGE_EVT) {
        switch (param->ctx.recv_op) {
        case ESP_BLE_MESH_MODEL_OP_APP_KEY_ADD:
            ESP_LOGI(TAG, "ESP_BLE_MESH_MODEL_OP_APP_KEY_ADD");
            ESP_LOGI(TAG, "net_idx 0x%04x, app_idx 0x%04x",
                param->value.state_change.appkey_add.net_idx,
                param->value.state_change.appkey_add.app_idx);
            ESP_LOG_BUFFER_HEX("AppKey", param->value.state_change.appkey_add.app_key, 16);
            break;
        case ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND:
            ESP_LOGI(TAG, "ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND");
            ESP_LOGI(TAG, "elem_addr 0x%04x, app_idx 0x%04x, cid 0x%04x, mod_id 0x%04x",
                param->value.state_change.mod_app_bind.element_addr,
                param->value.state_change.mod_app_bind.app_idx,
                param->value.state_change.mod_app_bind.company_id,
                param->value.state_change.mod_app_bind.model_id);
            store.app_idx = param->value.state_change.mod_app_bind.app_idx;
            if(param->value.state_change.mod_app_bind.model_id==ESP_BLE_MESH_MODEL_ID_GEN_PROP_CLI){
                bindClientModel=1;
                xTaskCreate(taskSendData,"send_data",1024*4,NULL,1,NULL);
            }
            break;
        case ESP_BLE_MESH_MODEL_OP_MODEL_SUB_ADD:
            ESP_LOGI(TAG, "ESP_BLE_MESH_MODEL_OP_MODEL_SUB_ADD");
            ESP_LOGI(TAG, "elem_addr 0x%04x, sub_addr 0x%04x, cid 0x%04x, mod_id 0x%04x",
                param->value.state_change.mod_sub_add.element_addr,
                param->value.state_change.mod_sub_add.sub_addr,
                param->value.state_change.mod_sub_add.company_id,
                param->value.state_change.mod_sub_add.model_id);
            break;
        case ESP_BLE_MESH_MODEL_OP_MODEL_PUB_SET :
            ESP_LOGI(TAG, "ESP_BLE_MESH_MODEL_OP_MODEL_PUB_SET ");
            store.dst_address=param->value.state_change.mod_pub_set.pub_addr;
            store.pub_ttl=param->value.state_change.mod_pub_set.pub_ttl;
            ESP_LOGI("NEW PUBLISH STAGE", "ADD %x ",param->value.state_change.mod_pub_set.pub_addr);
            break;
        case ESP_BLE_MESH_MODEL_OP_RELAY_SET :
            ESP_LOGI(TAG, "ESP_BLE_MESH_MODEL_OP_RELAY_SET ");
            break;

       

        default:
            break;
        }
    }
}

static esp_err_t ble_mesh_init(void)
{
    esp_err_t err = ESP_OK;

    esp_ble_mesh_register_prov_callback(example_ble_mesh_provisioning_cb);
    esp_ble_mesh_register_config_server_callback(example_ble_mesh_config_server_cb);
    esp_ble_mesh_register_generic_client_callback(example_ble_mesh_generic_client_cb);

    err = esp_ble_mesh_init(&provision, &composition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize mesh stack (err %d)", err);
        return err;
    }

    err = esp_ble_mesh_node_prov_enable(ESP_BLE_MESH_PROV_ADV | ESP_BLE_MESH_PROV_GATT);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable mesh node (err %d)", err);
        return err;
    }
  

    ESP_LOGI(TAG, "BLE Mesh Node initialized");


    return err;
}
//----------------------------------------------soft AP-----------------------------------------------------


EventGroupHandle_t s_wifi_event_group;


#define EXAMPLE_ESP_WIFI_SSID      CONFIG_ESP_WIFI_SSID
#define EXAMPLE_ESP_WIFI_PASS      CONFIG_ESP_WIFI_PASSWORD
#define EXAMPLE_ESP_WIFI_CHANNEL   CONFIG_ESP_WIFI_CHANNEL
#define EXAMPLE_MAX_STA_CONN       CONFIG_ESP_MAX_STA_CONN

static const char *TAG1 = "wifi softAP";

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                    int32_t event_id, void* event_data)
{
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(TAG1, "station "MACSTR" join, AID=%d",
                 MAC2STR(event->mac), event->aid);
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        ESP_LOGI(TAG1, "station "MACSTR" leave, AID=%d",
                 MAC2STR(event->mac), event->aid);
    }
}

void wifi_init_softap(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = EXAMPLE_ESP_WIFI_SSID,
            .ssid_len = strlen(EXAMPLE_ESP_WIFI_SSID),
            .channel = EXAMPLE_ESP_WIFI_CHANNEL,
            .password = EXAMPLE_ESP_WIFI_PASS,
            .max_connection = EXAMPLE_MAX_STA_CONN,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_softap finished. SSID:%s password:%s channel:%d",
             EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS, EXAMPLE_ESP_WIFI_CHANNEL);
}


// -----------------------------------------------------htpp server---------------------------------------------
static char m[5],s[5];
static uint64_t timeDelay=0;
void wifi_cb(char *data, int len,httpd_req_t *req){
     data[len] = '\0';
    ESP_LOGI("WIFI INFO","%s",data);
    //m:s
    char *pt = strtok(data, ":");
    printf("->minutes: %s\n", pt);
    strcpy(m, pt);
    timeDelay=atoi(m)*60;
    pt = strtok(NULL, ":");
    printf("->seconds: %s\n", pt);
    strcpy(s, pt);
    timeDelay+=atoi(s);
    store.delayTime=timeDelay;
    ESP_LOGI("delay time","%d",(int)store.delayTime);
    httpd_resp_send_chunk(req, "Saved time configuration", 24);
    xEventGroupSetBits(s_wifi_event_group, BIT0);
}





void app_main(void)
{
    

    ESP_LOGI(TAG, "Initializing...");
    DHT11_init(DHT11_GPIO);
    s_wifi_event_group=xEventGroupCreate();
    //gpio_set_direction(GPIO_NUM_18, GPIO_MODE_INPUT);
    //gpio_set_pull_mode(GPIO_NUM_18, GPIO_PULLUP_ONLY);

      esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "ESP_WIFI_MODE_AP");
    wifi_init_softap();

     timeInfo_set_cb(wifi_cb);
     start_webserver();
    
     
     xEventGroupWaitBits(s_wifi_event_group,BIT0,true,false,portMAX_DELAY);


    esp_err_t err = bluetooth_init();
    if (err) {
        ESP_LOGE(TAG, "esp32_bluetooth_init failed (err %d)", err);
        return;
    }

    ble_mesh_get_dev_uuid(dev_uuid);

    /* Initialize the Bluetooth Mesh Subsystem */
    err = ble_mesh_init();
    if (err) {
        ESP_LOGE(TAG, "Bluetooth mesh init failed (err %d)", err);
    }

    //xTaskCreate(taskSendData,"send_data",1024*2,NULL,3,NULL);
   
    
}
