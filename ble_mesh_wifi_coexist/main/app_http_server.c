#include "app_http_server.h"
#include <esp_http_server.h>
#include <esp_log.h>
#include<string.h>
extern const uint8_t html_wifi_start[] asm("_binary_wifi_html_start");
extern const uint8_t html_wifi_end[] asm("_binary_wifi_html_end");
extern const uint8_t html_channel_start[] asm("_binary_channel_html_start");
extern const uint8_t html_channel_end[] asm("_binary_channel_html_end");

static const char *TAG = "http server";


static post_handle_t wifi_data_handle = NULL;
static post_handle_t channel_data_handle = NULL;



static httpd_handle_t user_server = NULL;
/* An HTTP GET handler */

/* An HTTP GET handler */
static esp_err_t wifi_get_handler(httpd_req_t *req)
{
    char*  buf;
    size_t buf_len;

    /* Send response with custom headers and body set as the
     * string passed in user context*/
    // httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, (const char *)html_wifi_start, html_wifi_end-html_wifi_start);

    // /* After sending the HTTP response the old HTTP request
    //  * headers are lost. Check if HTTP request headers can be read now. */
    // if (httpd_req_get_hdr_value_len(req, "Host") == 0) {
    //     ESP_LOGI(TAG, "Request headers lost");
    // }
    return ESP_OK;
}

static const httpd_uri_t wifiSet= {
    .uri       = "/setup-wifi",      
    .method    = HTTP_GET,
    .handler   = wifi_get_handler,
    /* Let's pass response string in user
     * context to demonstrate it's usage */
    .user_ctx  = "Wifi configuration"
};

static esp_err_t channel_get_handler(httpd_req_t *req)
{
    char*  buf;
    size_t buf_len;

    /* Send response with custom headers and body set as the
     * string passed in user context*/
    // httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, (const char *)html_channel_start, html_channel_end-html_channel_start);

    // /* After sending the HTTP response the old HTTP request
    //  * headers are lost. Check if HTTP request headers can be read now. */
    // if (httpd_req_get_hdr_value_len(req, "Host") == 0) {
    //     ESP_LOGI(TAG, "Request headers lost");
    // }
    return ESP_OK;
}

static const httpd_uri_t channelSet = {
    .uri       = "/setup-channel",      
    .method    = HTTP_GET,
    .handler   = channel_get_handler,
    /* Let's pass response string in user
     * context to demonstrate it's usage */
    .user_ctx  = "Channel configuration"
};


/* An HTTP POST handler */
static esp_err_t wifiInfo_post_handler(httpd_req_t *req)
{
    char buf[100];
    int read_len = httpd_req_recv(req, buf, 100);
    wifi_data_handle(buf, read_len,req);
    // End response
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

static const httpd_uri_t wifiInfo = {
    .uri       = "/wifi-info",
    .method    = HTTP_POST,
    .handler   = wifiInfo_post_handler,
    .user_ctx  = NULL
};

static esp_err_t channelInfo_post_handler(httpd_req_t *req)
{
    char buf[200];
    int read_len = httpd_req_recv(req, buf, 200);
    channel_data_handle(buf, read_len,req);
    // End response
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

static const httpd_uri_t channelInfo = {
    .uri       = "/channel-info",
    .method    = HTTP_POST,
    .handler   = channelInfo_post_handler,
    .user_ctx  = NULL
};



/* This handler allows the custom error handling functionality to be
 * tested from client side. For that, when a PUT request 0 is sent to
 * URI /ctrl, the /hello and /echo URIs are unregistered and following
 * custom error handler http_404_error_handler() is registered.
 * Afterwards, when /hello or /echo is requested, this custom error
 * handler is invoked which, after sending an error message to client,
 * either closes the underlying socket (when requested URI is /echo)
 * or keeps it open (when requested URI is /hello). This allows the
 * client to infer if the custom error handler is functioning as expected
 * by observing the socket state.
 */
esp_err_t http_404_error_handler(httpd_req_t *req, httpd_err_code_t err)
{
    if (strcmp("/hello", req->uri) == 0) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "/hello URI is not available");
        /* Return ESP_OK to keep underlying socket open */
        return ESP_OK;
    } else if (strcmp("/echo", req->uri) == 0) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "/echo URI is not available");
        /* Return ESP_FAIL to close underlying socket */
        return ESP_FAIL;
    }
    /* For any other URI send 404 and close socket */
    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Some 404 error message");
    return ESP_FAIL;
}

void stop_webserver(void)
{
    // Stop the httpd server
    httpd_stop(user_server);
}

void start_webserver(void)
{
    
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;

    // Start the httpd server
    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
    if (httpd_start(&user_server, &config) == ESP_OK) {
        // Set URI handlers
        ESP_LOGI(TAG, "Registering URI handlers");
        httpd_register_uri_handler(user_server, &wifiSet);     // go to wifi config page
        httpd_register_uri_handler(user_server, &wifiInfo);
        httpd_register_uri_handler(user_server, &channelSet);     // go to channel config page
        httpd_register_uri_handler(user_server, &channelInfo);
    }
    else{
        ESP_LOGI(TAG, "Error starting server!");
    }
}


void wifiInfo_set_cb (void *cb)
{
    if(cb){
        wifi_data_handle = cb;
    }
}
void channelInfo_set_cb (void *cb)
{
    if(cb){
        channel_data_handle = cb;
    }
}