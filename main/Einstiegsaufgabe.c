#include <stdio.h>
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "freertos/event_groups.h"
#include "esp_http_client.h"

#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_gatt_common_api.h"
#include "esp_gatt_defs.h"

#define WIFI_READ_INFO BIT0
#define WIFI_GOT_IP_BIT BIT1

#define POST_URL "https://europe-west3-einstiegsaufgabe.cloudfunctions.net/receive_data"
#define POST_PORT 80

#define BLUETOOTH_NAME "esp32"
#define GATTS_SERVICE_UUID 0xFF
#define PREPARE_BUF_MAX_SIZE 1024

static EventGroupHandle_t wifi_event_group;

char wifi_ssid[32];
char wifi_password[64];

// COPY-PASTE examples/bluedroid/ble/gatt_server/main.c
struct gatts_profile_inst {
    uint16_t app_id;
    uint16_t conn_id;
    uint16_t service_handle;
    esp_gatt_srvc_id_t service_id;
    uint16_t char_handle;
    esp_bt_uuid_t char_uuid;
    esp_gatt_perm_t perm;
    esp_gatt_char_prop_t property;
    uint16_t descr_handle;
    esp_bt_uuid_t descr_uuid;
};
static uint8_t adv_service_uuid128[32] = {
    /* LSB <--------------------------------------------------------------------------------> MSB */
    //first uuid, 16bit, [12],[13] is the value
    0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80, 0x00, 0x10, 0x00, 0x00, 0xEE, 0x00, 0x00, 0x00,
    //second uuid, 32bit, [12], [13], [14], [15] is the value
    0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80, 0x00, 0x10, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00,
};
//adv data
static esp_ble_adv_data_t adv_data = {
    .set_scan_rsp = false,
    .include_name = true,
    .include_txpower = false,
    .min_interval = 0x0006, //slave connection min interval, Time = min_interval * 1.25 msec
    .max_interval = 0x0010, //slave connection max interval, Time = max_interval * 1.25 msec
    .appearance = 0x00,
    .manufacturer_len = 0, //TEST_MANUFACTURER_DATA_LEN,
    .p_manufacturer_data =  NULL, //&test_manufacturer[0],
    .service_data_len = 0,
    .p_service_data = NULL,
    .service_uuid_len = sizeof(adv_service_uuid128),
    .p_service_uuid = adv_service_uuid128,
    .flag = (ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT),
};
// scan response data
static esp_ble_adv_data_t scan_rsp_data = {
    .set_scan_rsp = true,
    .include_name = true,
    .include_txpower = true,
    //.min_interval = 0x0006,
    //.max_interval = 0x0010,
    .appearance = 0x00,
    .manufacturer_len = 0, //TEST_MANUFACTURER_DATA_LEN,
    .p_manufacturer_data =  NULL, //&test_manufacturer[0],
    .service_data_len = 0,
    .p_service_data = NULL,
    .service_uuid_len = sizeof(adv_service_uuid128),
    .p_service_uuid = adv_service_uuid128,
    .flag = (ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT),
};
static esp_ble_adv_params_t adv_params = {
    .adv_int_min        = 0x20,
    .adv_int_max        = 0x40,
    .adv_type           = ADV_TYPE_IND,
    .own_addr_type      = BLE_ADDR_TYPE_PUBLIC,
    //.peer_addr            =
    //.peer_addr_type       =
    .channel_map        = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};
typedef struct {
    uint8_t                 *prepare_buf;
    int                     prepare_len;
} prepare_type_env_t;
// End Copy-Paste

static uint8_t attr_val[] = {0x11, 0x22, 0x33};
static esp_attr_value_t service_char_value = {
    .attr_max_len = 64,
    .attr_value = attr_val,
    .attr_len = sizeof(attr_val)
};

static struct gatts_profile_inst bt_profile = {};
static prepare_type_env_t write_prep_env;

void print_uint8_as_char(uint8_t* msg, uint16_t len) {
    char str[len + 1];
    str[len] = '\0';
    memcpy(str, msg, len);
    printf("got %s\n", str);
}

static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param) {
    switch (event) {
        case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
            esp_ble_gap_start_advertising(&adv_params);
            break;
        case ESP_GAP_BLE_SCAN_RSP_DATA_SET_COMPLETE_EVT:
            // advertise the whole time (?)
            esp_ble_gap_start_advertising(&adv_params);
            break;
        case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
            if (param -> adv_start_cmpl.status != ESP_BT_STATUS_SUCCESS) {
                printf("Some error occured during advertising start\n");
            }
            break;
        default:
            break;
    }
}

static void gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param) {
    switch (event) {
        case ESP_GATTS_REG_EVT:
            bt_profile.service_id.is_primary = true;
            bt_profile.service_id.id.inst_id = 0x00;
            bt_profile.service_id.id.uuid.len = ESP_UUID_LEN_16;
            bt_profile.service_id.id.uuid.uuid.uuid16 = GATTS_SERVICE_UUID;

            esp_ble_gap_set_device_name(BLUETOOTH_NAME);

            esp_ble_gap_config_adv_data(&adv_data);
            // ?
            esp_ble_gap_config_adv_data(&scan_rsp_data);
            esp_ble_gatts_create_service(gatts_if, &bt_profile.service_id, 4);
            break;
        case ESP_GATTS_CREATE_EVT:
            bt_profile.service_handle = param -> create.service_handle;
            bt_profile.char_uuid.len = ESP_UUID_LEN_16;
            bt_profile.char_uuid.uuid.uuid16 = GATTS_SERVICE_UUID;

            // start service
            esp_ble_gatts_start_service(bt_profile.service_handle);
            esp_gatt_char_prop_t service_properties = ESP_GATT_CHAR_PROP_BIT_WRITE;
            esp_ble_gatts_add_char(bt_profile.service_handle, &bt_profile.char_uuid, ESP_GATT_PERM_WRITE, service_properties,&service_char_value, ESP_GATT_RSP_BY_APP);
            esp_ble_gatts_add_char_descr(bt_profile.service_handle, &bt_profile.descr_uuid, ESP_GATT_PERM_WRITE, NULL, NULL);
            break;
        case ESP_GATTS_ADD_CHAR_EVT:
            bt_profile.char_handle = param -> add_char.attr_handle;
            bt_profile.descr_uuid.len = ESP_UUID_LEN_16;
            bt_profile.descr_uuid.uuid.uuid16 = ESP_GATT_UUID_CHAR_CLIENT_CONFIG;
            
            int char_len = 0;
            uint8_t* char_val;
            esp_ble_gatts_get_attr_value(bt_profile.char_handle, &char_len, &char_val);

            break;
        case ESP_GATTS_CONNECT_EVT:
            printf("Device connected \n");
            esp_ble_conn_update_params_t conn_params = {0};
            memcpy(conn_params.bda, param->connect.remote_bda, sizeof(esp_bd_addr_t));
            // values copied from example
            conn_params.latency = 0;
            conn_params.min_int = 0x10;
            conn_params.max_int = 0x20;
            conn_params.timeout = 400;
            bt_profile.conn_id = param -> connect.conn_id;
            esp_ble_gap_update_conn_params(&conn_params);
            break;
        case ESP_GATTS_DISCONNECT_EVT:
            printf("Device disconnected \n");
            // need to advertise once again
            esp_ble_gap_start_advertising(&adv_params);
            break;
        case ESP_GATTS_WRITE_EVT:
            print_uint8_as_char(param->write.value, param->write.len);
            if (!param->write.need_rsp) {
                // if no response i needed, we do not respond
                printf("No response needed\n");
                // converting uint8_t to str/char[]
                char msg[param->write.len + 1];
                msg[param->write.len] = '\0';
                memcpy(msg, param->write.value, param->write.len);
                printf("got %s\n", msg);
                break;
            }
            // Client needs a response
            printf("Response needed\n");
            if (!param->write.is_prep) {
                /* only need to handle write prep seperately, this doubles one line of code, 
                * but keeps it unnested*/
                esp_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id, ESP_GATT_OK, NULL);
                break;
            }

            esp_gatt_status_t status = ESP_GATT_OK;
            if (write_prep_env.prepare_buf == NULL) {
                // no need to free prepare_buf (?), as it needs to be used the whole time
                write_prep_env.prepare_buf = (uint8_t*) malloc(PREPARE_BUF_MAX_SIZE*sizeof(uint8_t));
                // yes, you should check prepafe_buf == NULL
                write_prep_env.prepare_len = 0;
            } else {
                if((param->write.offset + param->write.len) > PREPARE_BUF_MAX_SIZE) {
                    // this is not 100% correct, but good enough
                    status = ESP_GATT_INVALID_ATTR_LEN;
                }
            }

            esp_gatt_rsp_t* gatt_rsp = (esp_gatt_rsp_t *)malloc(sizeof(esp_gatt_rsp_t));
            // again - you should check whether gatt_rsp == NULL

            // create response
            memcpy(gatt_rsp->attr_value.value, param->write.value, param->write.len);
            gatt_rsp -> attr_value.handle = param -> write.handle;
            gatt_rsp -> attr_value.offset = param -> write.offset;
            gatt_rsp -> attr_value.len = param -> write.len;
            gatt_rsp -> attr_value.auth_req = ESP_GATT_AUTH_REQ_NONE;
            
            esp_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id, status, gatt_rsp);
            free(gatt_rsp);

            // copy data into buffer
            memcpy(write_prep_env.prepare_buf+param->write.offset, param->write.value, param->write.len);
            write_prep_env.prepare_len += param->write.len;
            break;
        case ESP_GATTS_EXEC_WRITE_EVT:
            printf("exec write event\n");
            break;
        default:
            break;
    }
}

static void http_event_handler(esp_http_client_event_handle_t event) {
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

static void wifi_event_handler(void *event_handler_arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
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

// Establishe a WIFI-Connection
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

// Post HTTP request to POST_URL and initialize http_event_handler
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

// Read wifi ssid and password from nvs
void read_wifi() {
    printf("reading wifi\n");
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

// Write wifi ssid and password to nvs
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

// Start the bluetooth module and advertise
void start_bluetooth() {
    printf("starting bluetooth\n");
    esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
    esp_bt_controller_config_t config = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    esp_err_t err = esp_bt_controller_init(&config);
    if (err != ESP_OK) {
        printf("failure\n");
        return;
    }
    err = esp_bt_controller_enable(ESP_BT_MODE_BLE);

    // enable bluedroid
    esp_bluedroid_config_t bluedroid_cfg = BT_BLUEDROID_INIT_CONFIG_DEFAULT();
    err = esp_bluedroid_init_with_cfg(&bluedroid_cfg);
    err = esp_bluedroid_enable();

    // event handler
    err = esp_ble_gatts_register_callback(gatts_event_handler);
    err = esp_ble_gap_register_callback(gap_event_handler);

    // Application Profile(s)
    esp_ble_gatts_app_register(0);

    // ?
    esp_ble_gatt_set_local_mtu(500);
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

    start_bluetooth();

    wifi_event_group = xEventGroupCreate();
    /*
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
    */

    printf("finished\n");
}
