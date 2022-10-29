#ifndef __APP_HTTP_SERVER_H
#define __APP_HTTP_SERVER_H
#include <esp_http_server.h>

typedef void (*post_handle_t) (char *data, int len,httpd_req_t *req);
void start_webserver(void);
void stop_webserver(void);

void timeInfo_set_cb (void *cb);
#endif