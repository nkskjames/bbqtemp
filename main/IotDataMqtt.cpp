#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_event_loop.h"
#include <nvs.h>
#include <nvs_flash.h>
#include "aws_iot_config.h"
#include "aws_iot_log.h"
#include "aws_iot_version.h"
#include "aws_iot_mqtt_client_interface.h"
#include "aws_iot_shadow_interface.h"
#include "cJSON.h"

#include "IotData.hpp"
#include "IotDataMqtt.hpp"

using namespace std;
extern const uint8_t aws_root_ca_pem_start[] asm("_binary_aws_root_ca_pem_start");
extern const uint8_t aws_root_ca_pem_end[] asm("_binary_aws_root_ca_pem_end");
extern const uint8_t certificate_pem_crt_start[] asm("_binary_certificate_pem_crt_start");
extern const uint8_t certificate_pem_crt_end[] asm("_binary_certificate_pem_crt_end");
extern const uint8_t private_pem_key_start[] asm("_binary_private_pem_key_start");
extern const uint8_t private_pem_key_end[] asm("_binary_private_pem_key_end");
extern const uint8_t certificate_and_ca_pem_crt_start[] asm("_binary_certificate_and_ca_pem_crt_start");
extern const uint8_t certificate_and_ca_pem_crt_end[] asm("_binary_certificate_and_ca_pem_crt_end");

#define MQTT_NAMESPACE "mqtt" // Namespace in NVS
#define KEY_REGISTER     "registered"
#define KEY_S_VERSION  "version"
#define tag "mqtt"

uint32_t s_version = 0x0100;


#define SIZEOF(x)  (sizeof(x) / sizeof((x)[0]))

/**
 * Save signup status
 */
static void saveRegisterStatus(bool registered) {
	nvs_handle handle;
	ESP_ERROR_CHECK(nvs_open(MQTT_NAMESPACE, NVS_READWRITE, &handle));
	ESP_ERROR_CHECK(nvs_set_u8(handle, KEY_REGISTER, registered));
	ESP_ERROR_CHECK(nvs_set_u32(handle, KEY_S_VERSION, s_version));
	ESP_ERROR_CHECK(nvs_commit(handle));
    ESP_LOGI(tag, "Registration saved");
	nvs_close(handle);
}

bool isRegistered() {
	nvs_handle handle;
	uint8_t registered = false;
	esp_err_t err;
	uint32_t version;
	err = nvs_open(MQTT_NAMESPACE, NVS_READWRITE, &handle);
	if (err != 0) {
		ESP_LOGE(tag, "nvs_open: %x", err);
		return false;
	}

	// Get the version that the data was saved against.
	err = nvs_get_u32(handle, KEY_S_VERSION, &version);
	if (err != ESP_OK) {
		ESP_LOGD(tag, "No version record found (%d).", err);
		nvs_close(handle);
		return false;
	}

	// Check the versions match
	if ((version & 0xff00) != (s_version & 0xff00)) {
		ESP_LOGD(tag, "Incompatible versions ... current is %x, found is %x", version, s_version);
		nvs_close(handle);
		return false;
	}

	err = nvs_get_u8(handle, KEY_REGISTER, &registered);
	if (err != ESP_OK) {
		ESP_LOGD(tag, "No signup record found (%d).", err);
		nvs_close(handle);
		return false;
	}
	if (err != ESP_OK) {
		ESP_LOGE(tag, "nvs_open: %x", err);
		nvs_close(handle);
		return -false;
	}

	// Cleanup
	nvs_close(handle);
	ESP_LOGI(tag, "Found registration");
	return registered;
}



static void disconnectCallbackHandler(AWS_IoT_Client *pClient, void *data) {

    static const char* TAG = "shadow_disconnect";
    ESP_LOGW(TAG, "MQTT Disconnect");
    IoT_Error_t rc = FAILURE;

    if(NULL == pClient) {
        return;
    }

    if(aws_iot_is_autoreconnect_enabled(pClient)) {
        ESP_LOGI(TAG, "Auto Reconnect is enabled, Reconnecting attempt will start now");
    } else {
        ESP_LOGW(TAG, "Auto Reconnect not enabled. Starting manual reconnect...");
        rc = aws_iot_mqtt_attempt_reconnect(pClient);
        if(NETWORK_RECONNECTED == rc) {
            ESP_LOGW(TAG, "Manual Reconnect Successful");
        } else {
            ESP_LOGW(TAG, "Manual Reconnect Failed - %d", rc);
        }
    }
}

void iot_subscribe_callback_handler(AWS_IoT_Client *pClient, char *topicName, uint16_t topicNameLen,
                                    IoT_Publish_Message_Params *params, void *pData) {
    static const char* TAG = "subscribe_callback";
    ESP_LOGI(TAG, "Subscribe callback: %s",(char*)params->payload);
    
	cJSON *root = cJSON_Parse((char*)params->payload);
	cJSON *current = cJSON_GetObjectItem(root,"current");
	cJSON *state = cJSON_GetObjectItem(current,"state");
	cJSON *reported = cJSON_GetObjectItem(state,"reported");
	cJSON *td = cJSON_GetObjectItem(reported,"td");
	cJSON *tl = cJSON_GetObjectItem(reported,"tl");
	cJSON *tu = cJSON_GetObjectItem(reported,"tu");
	cJSON *token = cJSON_GetObjectItem(reported,"token");

	if (token) {
		strcpy(thingData.token,token->valuestring);
	}	
	if (td) {
		int sz = cJSON_GetArraySize(td);
		if (sz > SIZEOF(thingData.td)) { sz = SIZEOF(thingData.td); }
		ESP_LOGI(TAG, "SiE: %d,%d",sz,SIZEOF(thingData.td));
		for (int i = 0 ; i < sz ; i++) {
			strcpy(thingData.td[i],cJSON_GetArrayItem(td,i)->valuestring);
		}
	}
	if (tu) {
		int sz = cJSON_GetArraySize(tu);
		if (sz > SIZEOF(thingData.tu)) { sz = SIZEOF(thingData.tu); }
		ESP_LOGI(TAG, "SiE: %d,%d",sz,SIZEOF(thingData.tu));
		for (int i = 0 ; i < sz ; i++) {
			thingData.tu[i] = cJSON_GetArrayItem(tu,i)->valuedouble;
		}
	}
	if (tl) {
		int sz = cJSON_GetArraySize(tl);
		if (sz > SIZEOF(thingData.tl)) { sz = SIZEOF(thingData.tl); }
		ESP_LOGI(TAG, "SiE: %d,%d",sz,SIZEOF(thingData.tl));
		for (int i = 0 ; i < sz ; i++) {
			thingData.tl[i] = cJSON_GetArrayItem(tl,i)->valuedouble;
		}
	}
    ESP_LOGI(TAG, "%.*s\t%.*s", topicNameLen, topicName, (int) params->payloadLen, (char *)params->payload);
}

int IotDataMqtt::subscribe(char* username) {
    IoT_Error_t rc = FAILURE;

    AWS_IoT_Client client;
    IoT_Client_Init_Params mqttInitParams = iotClientInitParamsDefault;
    IoT_Client_Connect_Params connectParams = iotClientConnectParamsDefault;

    ESP_LOGI(TAG, "AWS IoT SDK Version %d.%d.%d-%s", VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH, VERSION_TAG);

    mqttInitParams.enableAutoReconnect = false; // We enable this later below
    mqttInitParams.pHostURL = (char*)IotDataMqtt::HOST;
    mqttInitParams.port = IotDataMqtt::PORT;


    mqttInitParams.pDeviceCertLocation = (const char *)certificate_and_ca_pem_crt_start;
    //mqttInitParams.pDeviceCertLocation = (const char *)certificate_pem_crt_start;
    mqttInitParams.pDevicePrivateKeyLocation = (const char *)private_pem_key_start;
    mqttInitParams.pRootCALocation = (const char *)aws_root_ca_pem_start;
    mqttInitParams.mqttCommandTimeout_ms = 20000;
    mqttInitParams.tlsHandshakeTimeout_ms = 5000;
    mqttInitParams.isSSLHostnameVerify = true;
    mqttInitParams.disconnectHandler = disconnectCallbackHandler;
    mqttInitParams.disconnectHandlerData = NULL;


    rc = aws_iot_mqtt_init(&client, &mqttInitParams);
    if(SUCCESS != rc) {
        ESP_LOGE(TAG, "aws_iot_mqtt_init returned error : %d ", rc);
        abort();
    }

    connectParams.keepAliveIntervalInSec = 10;
    connectParams.isCleanSession = true;
    connectParams.MQTTVersion = MQTT_3_1_1;
    /* Client ID is set in the menuconfig of the example */
    connectParams.pClientID = thingData.thingName;
    connectParams.clientIDLen = (uint16_t) strlen(thingData.thingName);
    connectParams.isWillMsgPresent = false;

    ESP_LOGI(TAG, "Connecting to AWS...");
    do {
        rc = aws_iot_mqtt_connect(&client, &connectParams);
        if(SUCCESS != rc) {
            ESP_LOGE(TAG, "Error(%d) connecting to %s:%d", rc, mqttInitParams.pHostURL, mqttInitParams.port);
            vTaskDelay(1000 / portTICK_RATE_MS);
        }
    } while(SUCCESS != rc);
    ESP_LOGI(TAG, ">>>>>>>>>>>>>>>>>>>>>>> Done Connecting to AWS...");
    /*
     * Enable Auto Reconnect functionality. Minimum and Maximum time of Exponential backoff are set in aws_iot_config.h
     *  #AWS_IOT_MQTT_MIN_RECONNECT_WAIT_INTERVAL
     *  #AWS_IOT_MQTT_MAX_RECONNECT_WAIT_INTERVAL
     */
    rc = aws_iot_mqtt_autoreconnect_set_status(&client, true);
    if(SUCCESS != rc) {
        ESP_LOGE(TAG, "Unable to set Auto Reconnect to true - %d", rc);
        abort();
    }
	char topic[100];
    sprintf(topic,"$aws/things/%s/shadow/update/documents",thingData.thingName);

    ESP_LOGI(TAG, "Subscribing: %s",topic);
    rc = aws_iot_mqtt_subscribe(&client, topic, strlen(topic), QOS0, iot_subscribe_callback_handler, &thingData);
    if(SUCCESS != rc) {
        ESP_LOGE(TAG, "Error subscribing : %d ", rc);
        abort();
    }
    
    
    const char *TOPIC = "topic/iot_signup";
    const int TOPIC_LEN = strlen(TOPIC);

    char cPayload[512];

    IoT_Publish_Message_Params paramsQOS0;
    paramsQOS0.qos = QOS0;
    paramsQOS0.payload = (void *) cPayload;
    sprintf(cPayload, "{\"username\" : \"%s\"}",username);
    paramsQOS0.isRetained = 0;
    paramsQOS0.payloadLen = strlen(cPayload);
    
    bool first = true;
    while((NETWORK_ATTEMPTING_RECONNECT == rc || NETWORK_RECONNECTED == rc || SUCCESS == rc)) {

        //Max time the yield function will wait for read messages
        rc = aws_iot_mqtt_yield(&client, 100);
        //ESP_LOGI(TAG, "-->sleep: %d",rc);		
        if(NETWORK_ATTEMPTING_RECONNECT == rc) {
            // If the client is attempting to reconnect we will skip the rest of the loop.
            continue;
        }
        if (first) {
			ESP_LOGI(TAG, "First Publish");
            rc = aws_iot_mqtt_publish(&client, TOPIC, TOPIC_LEN, &paramsQOS0);
            if (rc == MQTT_REQUEST_TIMEOUT_ERROR) {
                ESP_LOGW(TAG, "QOS1 publish ack not received.");
                rc = SUCCESS;
            }
            first = false;
        }
        vTaskDelay(1000 / portTICK_RATE_MS);
    }

    ESP_LOGE(TAG, "An error occurred in the main loop.");
    abort();
	return 1;
}

int IotDataMqtt::close() {

    IoT_Error_t rc = SUCCESS;
    ESP_LOGI(TAG, "Disconnecting");
    rc = aws_iot_shadow_disconnect(&mqttClient);

    if(SUCCESS != rc) {
        ESP_LOGE(IotDataMqtt::TAG, "Disconnect error %d", rc);
    }
    return rc;
}

/*

static esp_err_t event_handler(void *ctx, system_event_t *event)
{
   return ESP_OK;
}
*/

