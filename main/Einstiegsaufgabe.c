#include <stdio.h>
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "freertos/event_groups.h"
#include "esp_http_client.h"

#define WIFI_READ_INFO BIT0
#define WIFI_GOT_IP_BIT BIT1

#define POST_URL "https://europe-west3-einstiegsaufgabe.cloudfunctions.net/receive_data"
#define POST_PORT 80

static EventGroupHandle_t wifi_event_group;

char wifi_ssid[32];
char wifi_password[64];

void http_event_handler(esp_http_client_event_handle_t event) {
    switch (event->event_id) {
        case HTTP_EVENT_ON_CONNECTED:
            printf("Successfully connected\n");
            break;
        case HTTP_EVENT_DISCONNECTED:
            printf("Disconnected\n");
            break;
        case HTTP_EVENT_ON_DATA:
            printf("HTTP_EVENT_ON_DATA: %.*s\n", event->data_len, (char *)event->data);
            break;
        default:
            break;
    }
}

void wifi_event_handler(void *event_handler_arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    switch (event_id){
        case WIFI_EVENT_STA_START:
            printf("Started wifi\n");
            break;
        case WIFI_EVENT_STA_CONNECTED:
            printf("Connected to wifi!\n");
            break;
        case WIFI_EVENT_STA_DISCONNECTED:
            wifi_event_sta_disconnected_t* event = (wifi_event_sta_disconnected_t*) event_data;
            printf("Lost connection/unable to find wifi, reason %d\n", event->reason);
            xEventGroupClearBits(wifi_event_group, WIFI_GOT_IP_BIT);
            for (int i=0; i<10; i++) {
                esp_err_t err = esp_wifi_connect();
                if (err == ESP_OK) {
                    printf("Connected to wifi! - after disconnect\n");
                    return;
                }
            }
            printf("failed to re-establish wifi connection\n");
            break;
        case IP_EVENT_STA_GOT_IP:
            printf("Got IP\n");
            xEventGroupSetBits(wifi_event_group, WIFI_GOT_IP_BIT);
            break;
        default:
            break;
    }
}

/* Establishes a WIFI-Connection
*/
void connect_to_wifi() {
    // Initialization Phase
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK) {
        printf("Some error occured whilst initializing: %s\n", esp_err_to_name(err));
        return;
    }
    err = esp_event_loop_create_default();
    if (err != ESP_OK) {
        printf("Some error occured whilst creating event loop: %s\n", esp_err_to_name(err));
        return;
    }
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t default_config = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&default_config);

    // event handler
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL);

    // Configuration Phase
    esp_wifi_set_mode(WIFI_MODE_STA);
    wifi_config_t config = {0};
    
    // workaround because of some weird error
    memcpy(config.sta.ssid, wifi_ssid, strlen(wifi_ssid));
    memcpy(config.sta.password, wifi_password, strlen(wifi_password));

    esp_wifi_set_config(WIFI_IF_STA, &config);

    // Start/Connect Phase
    err = esp_wifi_start();
    if (err != ESP_OK) {
        printf("Some error occured whilst starting: %s\n", esp_err_to_name(err));
        return;
    }
    err = esp_wifi_connect();
    if (err != ESP_OK) {
        printf("Some error occured whilst connecting: %s\n", esp_err_to_name(err));
        return;
    }
}

/*
void get_http() {
    printf("getting http\n");
    esp_http_client_config_t get_config = {
        .url = POST_URL
    };
}
*/

void post_http() {
    printf("posting http\n");
    esp_http_client_config_t http_config = {
        .url = POST_URL,
        .port = POST_PORT,
        .method = HTTP_METHOD_POST,
        .event_handler = http_event_handler
    };

    esp_http_client_handle_t client = esp_http_client_init(&http_config);
    if (client == NULL) {
        printf("Error during client-creation\n");
    }

    char* message = "{\"message\":\"hello, gcp\"}";
    esp_http_client_set_post_field(client, message, strlen(message));
    esp_http_client_set_header(client, "Content-Type", "application/json");

    esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK) {
        printf("Post failed: %s\n", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
}

// reading wifi ssid and password
void read_wifi() {
    nvs_handle_t wifi_handle;
    esp_err_t err = nvs_open("wifi_storage", NVS_READWRITE, &wifi_handle);
    if (err != ESP_OK) {
        printf("Error opening storage: %s \n", esp_err_to_name(err));
        nvs_close(wifi_handle);
        return;
    } 
    size_t ssid_size = sizeof(wifi_ssid);
    err = nvs_get_str(wifi_handle, "ssid", wifi_ssid, &ssid_size);
    if (err != ESP_OK) {
        printf("Error getting ssid: %s \n", esp_err_to_name(err));
        nvs_close(wifi_handle);
        return;
    } 
    printf("read wifi_ssid: %s\n", wifi_ssid);

    size_t password_size = sizeof(wifi_password);
    err = nvs_get_str(wifi_handle, "password", wifi_password, &password_size);
    if (err != ESP_OK) {
        printf("Error getting password: %s \n", esp_err_to_name(err));
        nvs_close(wifi_handle);
        return;
    }
    printf("read wifi_password: %s\n", wifi_password);

    // set event bit
    xEventGroupSetBits(wifi_event_group, WIFI_READ_INFO);

    nvs_close(wifi_handle);
}

void write_wifi(char* ssid, char* password) {
    nvs_handle_t wifi_handle;
    esp_err_t err = nvs_open("wifi_storage", NVS_READWRITE, &wifi_handle);
    if (err != ESP_OK) {
        printf("Error opening storage: %s \n", esp_err_to_name(err));
        return;
    }

    err = nvs_set_str(wifi_handle, "ssid", ssid);
    if (err != ESP_OK) {
        printf("Error writing ssid: %s \n", esp_err_to_name(err));
        nvs_close(wifi_handle);
        return;
    }
    err = nvs_set_str(wifi_handle, "password", password);
    if (err != ESP_OK) {
        printf("Error writing password: %s \n", esp_err_to_name(err));
        nvs_close(wifi_handle);
        return;
    }

    nvs_commit(wifi_handle);
    nvs_close(wifi_handle);
}

void app_main(void) {
    // copy-paste from station_example_main.c
    // Initialize NVS
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    wifi_event_group = xEventGroupCreate();
    read_wifi();

    EventBits_t wifi_bits = xEventGroupGetBits(wifi_event_group);
    if (!(wifi_bits & WIFI_READ_INFO)) {
        // reading didn't work, we try once more
        read_wifi();
        if (!(wifi_bits & WIFI_READ_INFO)) {
            printf("unable to read wifi information %s - %s\n", wifi_ssid, wifi_password);
            return;
        }
    }

    connect_to_wifi();
    
    xEventGroupWaitBits(wifi_event_group, WIFI_GOT_IP_BIT, pdFALSE, pdFALSE, portMAX_DELAY);
    post_http();

    printf("finished\n");
}
