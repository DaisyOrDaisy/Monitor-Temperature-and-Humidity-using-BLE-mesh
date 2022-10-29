// Copyright 2017-2019 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at

//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <errno.h>
#include <stdio.h>
#include "driver/gpio.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_err.h"

#include "esp_log.h"
#include "nvs_flash.h"

#include "esp_ble_mesh_defs.h"
#include "esp_ble_mesh_common_api.h"
#include "esp_ble_mesh_networking_api.h"
#include "esp_ble_mesh_provisioning_api.h"
#include "esp_ble_mesh_config_model_api.h"
#include "esp_ble_mesh_generic_model_api.h"
#include "esp_ble_mesh_local_data_operation_api.h"

#include "ble_mesh_example_init.h"
#include "ble_mesh_example_nvs.h"

#include "device_property.h"
#include "app_mqtt.h"
#include "esp_http_client.h"

#include "esp_system.h"
#include "esp_event.h"
//#include "protocol_examples_common.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"



#include "esp_tls.h"
#include "esp_crt_bundle.h"
#include "app_http_server.h"
#include "data_channel_config.h"
#include <esp_http_server.h>

#define TAG "GATEWAY"

#define CID_ESP 0x02E5
// received message destination address
static uint16_t dst_add=0xFFFF;
// received data 
static char *rcv_data;
// list channel 
static uint8_t lastChannelIndex=0;
static channel_t listChannel[50];
// station config
static wifi_config_t my_wifi_config={
    .sta={
        .ssid="newssid",
        .password="",
        .threshold.authmode = WIFI_AUTH_WPA2_PSK,

        .pmf_cfg = {
            .capable = true,
            .required = false
        },
    },
};


static uint8_t dev_uuid[16] = { 0xdd, 0xdd };

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
CREATE_NET_BUF_SIMPLE_DEFINE_STATIC(my_simple_buf, 7);
static esp_ble_mesh_generic_property_t my_properties = {
        .id = BLE_MESH_DEVICE_UNDER_TEMPERATURE_EVENT_STATISTICS,
        .user_access = 0x03,
        .manu_access = 0x03,
        .admin_access = 0x03,
        .val=&my_simple_buf,
    };


static struct example_info_store {
    uint16_t net_idx;   // NetKey Index 
    uint16_t app_idx;   // AppKey Index 
} __attribute__((packed)) store = {
    .net_idx = ESP_BLE_MESH_KEY_UNUSED,
    .app_idx = ESP_BLE_MESH_KEY_UNUSED,
    
};

CREATE_ESP_BLE_MESH_MODEL_PUB_DEFINE(send_my_data_pub,10,ROLE_NODE);
static esp_ble_mesh_gen_user_prop_srv_t send_my_data_server = {
    .rsp_ctrl.get_auto_rsp = ESP_BLE_MESH_SERVER_AUTO_RSP,
    .rsp_ctrl.set_auto_rsp = ESP_BLE_MESH_SERVER_RSP_BY_APP,
    .property_count = 1,
    .properties =&my_properties,
};

static esp_ble_mesh_model_op_t my_op[] = {
    { ESP_BLE_MESH_MODEL_OP_GEN_USER_PROPERTY_GET, 5, 0},
    { ESP_BLE_MESH_MODEL_OP_GEN_USER_PROPERTY_SET, 5, 0},
    { ESP_BLE_MESH_MODEL_OP_GEN_USER_PROPERTY_SET_UNACK, 5, 0},
    ESP_BLE_MESH_MODEL_OP_END,
};





static esp_ble_mesh_cfg_srv_t config_server = {
    .relay = ESP_BLE_MESH_RELAY_DISABLED,
    .beacon = ESP_BLE_MESH_BEACON_ENABLED,
#if defined(CONFIG_BLE_MESH_FRIEND)
    .friend_state = ESP_BLE_MESH_FRIEND_ENABLED,
#else
    .friend_state = ESP_BLE_MESH_FRIEND_NOT_SUPPORTED,
#endif
#if defined(CONFIG_BLE_MESH_GATT_PROXY_SERVER)
    .gatt_proxy = ESP_BLE_MESH_GATT_PROXY_ENABLED,
#else
    .gatt_proxy = ESP_BLE_MESH_GATT_PROXY_NOT_SUPPORTED,
#endif
    .default_ttl = 7,
    /* 3 transmissions with 20ms interval */
    .net_transmit = ESP_BLE_MESH_TRANSMIT(2, 20),
    .relay_retransmit = ESP_BLE_MESH_TRANSMIT(2, 20),
};



static esp_ble_mesh_model_t root_models[] = {
    ESP_BLE_MESH_MODEL_CFG_SRV(&config_server),
    ESP_BLE_MESH_SIG_MODEL(ESP_BLE_MESH_MODEL_ID_GEN_USER_PROP_SRV, my_op,
    &send_my_data_pub, &send_my_data_server),
    
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

// HTTP Connection define and config
/* http client config*/
/* Constants that aren't configurable in menuconfig */
static char *REQUEST="hello";
#define WEB_SERVER "api.thingspeak.com"
#define WEB_PORT "443"
#define WEB_URL "https://api.thingspeak.com/update?api_key=LETG61JK0KVE702C"


static void https_post_data(void *pvParameters)
{
    char buf[512];
    int ret, len;

    esp_tls_cfg_t cfg = {
            .crt_bundle_attach = esp_crt_bundle_attach,
        };

    struct esp_tls *tls = esp_tls_conn_http_new(WEB_URL, &cfg);

    if(tls != NULL) {
        ESP_LOGI(TAG, "Connection established...");
    } else {
        ESP_LOGE(TAG, "Connection failed...");
        goto exit;
    }

    size_t written_bytes = 0;
    do {
         ret = esp_tls_conn_write(tls,
                                    REQUEST + written_bytes,
                                    strlen(REQUEST) - written_bytes);
        if (ret >= 0) {
             ESP_LOGI(TAG, "%d bytes written", ret);
            written_bytes += ret;
        } else if (ret != ESP_TLS_ERR_SSL_WANT_READ  && ret != ESP_TLS_ERR_SSL_WANT_WRITE) {
            ESP_LOGE(TAG, "esp_tls_conn_write  returned 0x%x", ret);
            goto exit;
        }
    } while(written_bytes < strlen(REQUEST));

    ESP_LOGI(TAG, "Reading HTTP response...");

    do
    {
        len = sizeof(buf) - 1;
        bzero(buf, sizeof(buf));
        ret = esp_tls_conn_read(tls, (char *)buf, len);

        if(ret == ESP_TLS_ERR_SSL_WANT_WRITE  || ret == ESP_TLS_ERR_SSL_WANT_READ)
             continue;

        if(ret < 0)
        {
            ESP_LOGE(TAG, "esp_tls_conn_read  returned -0x%x", -ret);
            break;
        }

        if(ret == 0)
        {
            ESP_LOGI(TAG, "connection closed");
            break;
        }

        len = ret;
        ESP_LOGD(TAG, "%d bytes read", len);
            /* Print response directly to stdout as it is read */
        for(int i = 0; i < len; i++) {
            putchar(buf[i]);
        }
    } while(1);

    exit:
        esp_tls_conn_delete(tls);
        putchar('\n'); // JSON output doesn't have a newline at end

        static int request_count;
        ESP_LOGI(TAG, "Completed %d requests", ++request_count);

  
}

/*define publish data to thingspeak..........*/

static void prov_complete(uint16_t net_idx, uint16_t addr, uint8_t flags, uint32_t iv_index)
{
    ESP_LOGI(TAG, "net_idx: 0x%04x, addr: 0x%04x", net_idx, addr);
    ESP_LOGI(TAG, "flags: 0x%02x, iv_index: 0x%08x", flags, iv_index);
    store.net_idx = net_idx;
    /* mesh_example_info_store() shall not be invoked here, because if the device
     * is restarted and goes into a provisioned state, then the following events
     * will come:
     * 1st: ESP_BLE_MESH_NODE_PROV_COMPLETE_EVT
     * 2nd: ESP_BLE_MESH_PROV_REGISTER_COMP_EVT
     * So the store.net_idx will be updated here, and if we store the mesh example
     * info here, the wrong app_idx (initialized with 0xFFFF) will be stored in nvs
     * just before restoring it.
     */
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
        break;
    case ESP_BLE_MESH_NODE_PROV_RESET_EVT:
        break;
    case ESP_BLE_MESH_NODE_SET_UNPROV_DEV_NAME_COMP_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_NODE_SET_UNPROV_DEV_NAME_COMP_EVT, err_code %d", param->node_set_unprov_dev_name_comp.err_code);
        break;
    default:
        break;
    }
}

static void example_ble_mesh_generic_server_cb(esp_ble_mesh_generic_server_cb_event_t event,
                                               esp_ble_mesh_generic_server_cb_param_t *param)
{
   

    ESP_LOGI(TAG, "event 0x%02x, opcode 0x%04x, src 0x%04x, dst 0x%04x",
             event, param->ctx.recv_op, param->ctx.addr, param->ctx.recv_dst);

    switch (event)
    {
    case ESP_BLE_MESH_GENERIC_SERVER_STATE_CHANGE_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_GENERIC_SERVER_STATE_CHANGE_EVT");
        ESP_LOGI(TAG,"DATA: %s",(char *)param->value.set.user_property.property_value->data);
        ESP_LOGI(TAG,"ADDRESS: %x",param->ctx.addr);
        break;
    case ESP_BLE_MESH_GENERIC_SERVER_RECV_GET_MSG_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_GENERIC_SERVER_RECV_GET_MSG_EVT");
        break;
    case ESP_BLE_MESH_GENERIC_SERVER_RECV_SET_MSG_EVT:
        ESP_LOGI(TAG, "ESP_BLE_MESH_GENERIC_SERVER_RECV_SET_MSG_EVT");
        if (param->ctx.recv_op == ESP_BLE_MESH_MODEL_OP_GEN_USER_PROPERTY_GET ||
            param->ctx.recv_op == ESP_BLE_MESH_MODEL_OP_GEN_USER_PROPERTY_SET_UNACK)
        {
            rcv_data=(char *)param->value.set.user_property.property_value->data;
            ESP_LOGI(TAG,"DATA: %s",rcv_data);
            dst_add=param->ctx.recv_dst;
            ESP_LOGI("DATA INFO","ADDRESS dst %x  TTL %d",dst_add,param->ctx.recv_ttl);
            REQUEST=prepare_resquest(rcv_data,dst_add,listChannel,lastChannelIndex);
            if(REQUEST){
                ESP_LOGI("Request before sent","%s",REQUEST);
                https_post_data(NULL);
            }else{
                ESP_LOGI("PREPARE REQUEST","Fail....");
            }
        }
        
        
        break;
    default:
        ESP_LOGE(TAG, "Unknown Generic Server event 0x%02x", event);
        break;
    }
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
            if (param->value.state_change.mod_app_bind.company_id == 0xFFFF &&
                param->value.state_change.mod_app_bind.model_id == ESP_BLE_MESH_MODEL_ID_GEN_USER_PROP_SRV) {
                store.app_idx = param->value.state_change.mod_app_bind.app_idx;
                //mesh_example_info_store(); /* Store proper mesh example info */
            }
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
    esp_ble_mesh_register_generic_server_callback(example_ble_mesh_generic_server_cb);
    esp_ble_mesh_register_config_server_callback(example_ble_mesh_config_server_cb);

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

// ------------------------------------------------soft Ap-------------------------------------------------------------------------------------------------------
#define EXAMPLE_ESP_WIFI_SSID      CONFIG_ESP_WIFI_SSID
#define EXAMPLE_ESP_WIFI_PASS      CONFIG_ESP_WIFI_PASSWORD
#define EXAMPLE_ESP_WIFI_CHANNEL   CONFIG_ESP_WIFI_CHANNEL
#define EXAMPLE_MAX_STA_CONN       CONFIG_ESP_MAX_STA_CONN

EventGroupHandle_t s_wifi_event_group;
EventGroupHandle_t s_channel_event_group;

#define BIT_COMPLETE_CHANNEL_SETUP BIT3

#define BIT_WIFI_CONNECTED_TO_AP	BIT0
#define BIT_WIFI_CONNECT_TO_AP_FAILED   BIT1
#define BIT_WIFI_RECV_INFO	BIT2

static const char *TAG1 = "wifi softAP";
static int s_retry_num;

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
       if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        gpio_set_level(GPIO_NUM_2, 0);
        if (s_retry_num < 100000) {
            esp_wifi_connect();
            s_retry_num++;
            vTaskDelay(1000);
            ESP_LOGI(TAG, "retry %d to connect to the AP", s_retry_num);
        } else {
            xEventGroupSetBits(s_wifi_event_group, BIT_WIFI_CONNECT_TO_AP_FAILED);
        }
        ESP_LOGI(TAG,"connect to the AP fail");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        gpio_set_level(GPIO_NUM_2,1);
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, BIT_WIFI_CONNECTED_TO_AP);
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
            .ssid = "ESP_GATEWAY",
            .ssid_len = strlen("ESP_GATEWAY"),
            .channel = EXAMPLE_ESP_WIFI_CHANNEL,
            .password = "gateway123",
            .max_connection = EXAMPLE_MAX_STA_CONN,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK
        },
    };
   

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG1, "wifi_init_softap finished. SSID:%s password:%s channel:%d",
             wifi_config.ap.ssid, wifi_config.ap.password, EXAMPLE_ESP_WIFI_CHANNEL);
}

void wifi_init_sta(void)
{
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &my_wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start() );

    ESP_LOGI(TAG, "wifi_init_sta finished.");
}

// -----------------------------------------------------htpp server---------------------------------------------
void wifi_cb(char *data, int len,httpd_req_t *req){
     data[len] = '\0';
    ESP_LOGI("WIFI INFO","%s",data);
    //ssid:password
    char *pt = strtok(data, ":");
    printf("->ssid: %s\n", pt);
    strcpy((char*)my_wifi_config.sta.ssid, pt);
    pt = strtok(NULL, ":");
    printf("->pass: %s\n", pt);
    strcpy((char*)my_wifi_config.sta.password, pt);
    httpd_resp_send_chunk(req, "Saved Wifi information", 22);
    xEventGroupSetBits(s_wifi_event_group, BIT_WIFI_RECV_INFO);
}
void channel_cb(char *data, int len,httpd_req_t *req){
    data[len] = '\0';
    ESP_LOGI("CHANNEL INFO","%s",data);
    if(strstr(data,"done")) {
        httpd_resp_send_chunk(req, "set up complete", 15);
        xEventGroupSetBits(s_channel_event_group,BIT_COMPLETE_CHANNEL_SETUP);
        return;
    }
    if(lastChannelIndex==49) {
        ESP_LOGW("ADD CHANNEL","CHANNEL FULL");
         //todo: send response to the client to notice list is full
        httpd_resp_send_chunk(req, "list is full", 12);
        return;
    }else if(strstr(data,"clear")){
        // clear list channel
        lastChannelIndex=0;
        httpd_resp_send_chunk(req, "clear complete", 14);
        return;    
    }else{
        strcpy(listChannel[lastChannelIndex].channel,data);
        lastChannelIndex++;   
        httpd_resp_send_chunk(req, "successfully add new channel", 28); 
    }

    
   

    
}

void app_main(void)
{
    
    ESP_LOGI(TAG, "Initializing...");
    gpio_set_direction(GPIO_NUM_2, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_NUM_2, 0);
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    s_wifi_event_group = xEventGroupCreate();
    s_channel_event_group = xEventGroupCreate();
    

     wifi_init_softap();
     wifiInfo_set_cb(wifi_cb);
     channelInfo_set_cb(channel_cb);
     start_webserver();

     xEventGroupWaitBits(s_channel_event_group,BIT_COMPLETE_CHANNEL_SETUP,true,false,portMAX_DELAY);
     print_list_channel(listChannel,lastChannelIndex);
     xEventGroupWaitBits(s_wifi_event_group,BIT_WIFI_RECV_INFO,true,false,portMAX_DELAY);
     wifi_init_sta();
     EventBits_t uxBits = xEventGroupWaitBits(s_wifi_event_group,BIT_WIFI_CONNECTED_TO_AP | BIT_WIFI_CONNECT_TO_AP_FAILED, true, false, portMAX_DELAY);
    if(uxBits & BIT_WIFI_CONNECTED_TO_AP){
        printf("WIFI CONNECTED\n");
    }
    if(uxBits & BIT_WIFI_CONNECT_TO_AP_FAILED){
        printf("WIFI CONNECT FAIL\n");
    }

    esp_err_t err = bluetooth_init();
    if (err) {
        ESP_LOGE(TAG, "esp32_bluetooth_init failed (err %d)", err);
        return;
    }

    ble_mesh_get_dev_uuid(dev_uuid);

    //Initialize the Bluetooth Mesh Subsystem 
   err = ble_mesh_init();
    if (err) {
        ESP_LOGE(TAG, "Bluetooth mesh init failed (err %d)", err);
        return;
    }


}
