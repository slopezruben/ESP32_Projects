/******************++++
 *
 *  Based on Low Level Learning ESP32 Wireless video
 * 
 * ********************/

#include "freertos/FreeRTOS.h"

#include "cc.h"
#include "esp_netif_ip_addr.h"
#include "esp_bit_defs.h"
#include "esp_event_base.h"
#include "esp_netif_types.h"
#include "esp_wifi_types_generic.h"

#include "esp_wifi.h"
#include "freertos/idf_additions.h"
#include "freertos/projdefs.h"
#include "lwip/inet.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "portmacro.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"


#define MAX_FAILURES 10

#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_FAIL_BIT       BIT1

#define WIFI_SSID ""
#define WIFI_PASS ""

static const char *TAG = "WIFI";

static EventGroupHandle_t wifi_event_group;
static int s_retry_num = 0;

void event_handler(void* arg, esp_event_base_t event_base,
                        int32_t event_id, void* event_data) {
        if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START){
            ESP_LOGI(TAG, "esp_wifi_connected");
            esp_wifi_connect();
        } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_STOP){

            if (s_retry_num < MAX_FAILURES) {
                esp_wifi_connect();
                s_retry_num++;
                ESP_LOGI(TAG, "Retrying to connect to the AP");
            } else {
                xEventGroupSetBits(wifi_event_group, WIFI_FAIL_BIT);
            }                                       

            ESP_LOGI(TAG, "Failed connection to AP");
        } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP){
            ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;       

            ESP_LOGI(TAG, "got ip: " IPSTR, IP2STR(&event->ip_info.ip) );
            s_retry_num = 0;
            xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
        }

}

esp_err_t connect_wifi() {

    wifi_event_group = xEventGroupCreate();

    esp_err_t status = ESP_OK;    

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,     // Esto es un callback
                                                        NULL,
                                                        &instance_any_id));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,     // Esto es un callback
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .capable = true,
                .required = false
            }
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "STA initialized");

    EventBits_t bits = xEventGroupWaitBits(wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE, pdFALSE,
            portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT){
        ESP_LOGI(TAG, "connected to ap SSID:%s password:%s",
                WIFI_SSID, WIFI_PASS);
                
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "failed connect to ap SSID:%s password:%s",
                WIFI_SSID, WIFI_PASS);
        status = ESP_FAIL;
    } else {
        ESP_LOGI(TAG, "UNEXPECTED EVENT");
        status = ESP_FAIL;
    }

    return status;
}

esp_err_t connect_tcp_server() {

    ESP_LOGI(TAG, "Init connection to TCP server.");
    esp_err_t status = ESP_OK;    

    struct sockaddr_in serverInfo = {0};
    char readBuffer[1024] = {0};

    serverInfo.sin_family = AF_INET;
    serverInfo.sin_addr.s_addr = 0x2101a8c0;
    serverInfo.sin_port = htons(12345);

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
    {
        ESP_LOGI(TAG, "Failed to create a socket");
        return ESP_FAIL;
    }


    ESP_LOGI(TAG, "Open Socket");
    if (connect(sock, 
                (struct sockaddr *) &serverInfo, 
                sizeof(serverInfo)) != 0)
    {
        ESP_LOGI(TAG, "Failed to connect to %s!", 
                inet_ntoa(serverInfo.sin_addr.s_addr));
        close(sock);
        
        return ESP_FAIL;
    }


    ESP_LOGI(TAG, "Connection Established");
    
    ESP_LOGI(TAG, "Connected to TCP server.");
	bzero(readBuffer, sizeof(readBuffer));
    int r = read(sock, readBuffer, sizeof(readBuffer)-1);
    for(int i = 0; i < r; i++) {
        putchar(readBuffer[i]);
    }

    if (strcmp(readBuffer, "HELLO") == 0)
    {
    	ESP_LOGI(TAG, "WE DID IT!");
    }

    return status;
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }

    ESP_ERROR_CHECK(ret);

    if (connect_wifi())
    {
        ESP_LOGI(TAG, "Failed to associate to AP, dying...");
        return;
    }

    if (connect_tcp_server())
    {
        ESP_LOGI(TAG, "Failed to remote server, dying...");
        return;
    }
}
