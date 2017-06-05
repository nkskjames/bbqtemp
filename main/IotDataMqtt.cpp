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

static bool shadowUpdateInProgress;


void iot_subscribe_callback_handler(AWS_IoT_Client *pClient, char *topicName, uint16_t topicNameLen,
                                    IoT_Publish_Message_Params *params, void *pData) {
    static const char* TAG = "subscribe_callback";
    ESP_LOGI(TAG, "Subscribe callback");
    ESP_LOGI(TAG, "%.*s\t%.*s", topicNameLen, topicName, (int) params->payloadLen, (char *)params->payload);
}

int IotDataMqtt::subscribe(char* thingId) {
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
    connectParams.pClientID = thingId;
    connectParams.clientIDLen = (uint16_t) strlen(thingId);
    connectParams.isWillMsgPresent = false;

    ESP_LOGI(TAG, "Connecting to AWS...");
    do {
        rc = aws_iot_mqtt_connect(&client, &connectParams);
        if(SUCCESS != rc) {
            ESP_LOGE(TAG, "Error(%d) connecting to %s:%d", rc, mqttInitParams.pHostURL, mqttInitParams.port);
            vTaskDelay(1000 / portTICK_RATE_MS);
        }
    } while(SUCCESS != rc);

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
    sprintf(topic,"$aws/things/%s/shadow/update/accepted",thingId);

    ESP_LOGI(TAG, "Subscribing: %s",topic);
    rc = aws_iot_mqtt_subscribe(&client, topic, strlen(topic), QOS0, iot_subscribe_callback_handler, NULL);
    if(SUCCESS != rc) {
        ESP_LOGE(TAG, "Error subscribing : %d ", rc);
        abort();
    }

    while((NETWORK_ATTEMPTING_RECONNECT == rc || NETWORK_RECONNECTED == rc || SUCCESS == rc)) {

        //Max time the yield function will wait for read messages
        rc = aws_iot_mqtt_yield(&client, 100);
        ESP_LOGI(TAG, "-->sleep: %d",rc);		
        if(NETWORK_ATTEMPTING_RECONNECT == rc) {
            // If the client is attempting to reconnect we will skip the rest of the loop.
            continue;
        }


        vTaskDelay(1000 / portTICK_RATE_MS);
    }

    ESP_LOGE(TAG, "An error occurred in the main loop.");
    abort();
	return 1;
}


















static void ShadowUpdateStatusCallback(const char *pThingName, ShadowActions_t action, Shadow_Ack_Status_t status,
                                const char *pReceivedJsonDocument, void *pContextData) {
    IOT_UNUSED(pThingName);
    IOT_UNUSED(action);
    IOT_UNUSED(pReceivedJsonDocument);
    IOT_UNUSED(pContextData);

    shadowUpdateInProgress = false;
    static const char* TAG = "shadow_callback";

    if(SHADOW_ACK_TIMEOUT == status) {
        ESP_LOGE(TAG, "Update timed out");
    } else if(SHADOW_ACK_REJECTED == status) {
        ESP_LOGE(TAG, "Update rejected");
    } else if(SHADOW_ACK_ACCEPTED == status) {
        ESP_LOGI(TAG, "Update accepted");
    }
}

int IotDataMqtt::signup(char* thingId,char* username) {
    if (isRegistered()) { return 0; }
    char cPayload[100];

    //int32_t i = 0;

    IoT_Error_t rc = FAILURE;
    AWS_IoT_Client client;
    IoT_Client_Init_Params mqttInitParams = iotClientInitParamsDefault;
    IoT_Client_Connect_Params connectParams = iotClientConnectParamsDefault;

    IoT_Publish_Message_Params paramsQOS0;
 
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
    
    connectParams.pClientID = thingId;
    connectParams.clientIDLen = (uint16_t) strlen(thingId);
    connectParams.isWillMsgPresent = false;

    ESP_LOGI(TAG, "Connecting to AWS...");
    do {
        rc = aws_iot_mqtt_connect(&client, &connectParams);
        if(SUCCESS != rc) {
            ESP_LOGE(TAG, "Error(%d) connecting to %s:%d", rc, mqttInitParams.pHostURL, mqttInitParams.port);
            vTaskDelay(1000 / portTICK_RATE_MS);
        }
    } while(SUCCESS != rc);

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

    const char *TOPIC = "topic/iot_signup";
    const int TOPIC_LEN = strlen(TOPIC);

    paramsQOS0.qos = QOS0;
    paramsQOS0.payload = (void *) cPayload;
    paramsQOS0.isRetained = 0;

    rc = aws_iot_mqtt_yield(&client, 100);
    sprintf(cPayload, "{\"username\" : \"%s\"}",username);
    paramsQOS0.payloadLen = strlen(cPayload);
    rc = aws_iot_mqtt_publish(&client, TOPIC, TOPIC_LEN, &paramsQOS0);
    if (rc == MQTT_REQUEST_TIMEOUT_ERROR) {
        ESP_LOGW(TAG, "QOS1 publish ack not received.");
    } else {
        ESP_LOGI(TAG, "Signup publish successful.");
		//TODO: fix race issue
		this->init(thingId);
    	char JsonDocumentBuffer[400];
		sprintf(JsonDocumentBuffer,
			"{\"state\": {\"reported\": {\"thingname\":\"%s\",\"username\":\"%s\", \"td\": [\"Temp 1\",\"Temp 2\",\"Temp 3\"], \"t\": [0,0,0], \"tl\": [0,0,0], \"tu\": [100,100,100]}}, \"clientToken\":\"%s-100\"}",
			thingId,username,thingId);
	
		this->sendraw(JsonDocumentBuffer);
		this->close();
        saveRegisterStatus(true);
    }

	return 0;

}



int IotDataMqtt::init(char* thingName) {
    IoT_Error_t rc = FAILURE;

    strcpy(this->thingName,thingName);
    strcpy(this->thingId,thingName);

    ESP_LOGI(IotDataMqtt::TAG, "AWS IoT SDK Version %d.%d.%d-%s", VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH, VERSION_TAG);
    ShadowInitParameters_t sp = ShadowInitParametersDefault;
    sp.pHost = (char*)IotDataMqtt::HOST;
    sp.port = IotDataMqtt::PORT;
    sp.pClientCRT = (const char *)certificate_and_ca_pem_crt_start;
    //sp.pClientCRT = (const char *)certificate_pem_crt_start;
    sp.pClientKey = (const char *)private_pem_key_start;
    sp.pRootCA = (const char *)aws_root_ca_pem_start;
    sp.enableAutoReconnect = false;
    sp.disconnectHandler = disconnectCallbackHandler;

    ESP_LOGI(TAG, "Shadow Init");
    rc = aws_iot_shadow_init(&mqttClient, &sp);
    if(SUCCESS != rc) {
        ESP_LOGE(TAG, "aws_iot_shadow_init returned error %d, aborting...", rc);
        abort();
    }

    ShadowConnectParameters_t scp = ShadowConnectParametersDefault;

	//AWS recommends setting thing name equal to client id
    scp.pMyThingName = this->thingName;
    scp.pMqttClientId = this->thingName;
    scp.mqttClientIdLen = (uint16_t) strlen(this->thingName);

    ESP_LOGI(IotDataMqtt::TAG, "Shadow Connect");
    rc = aws_iot_shadow_connect(&mqttClient, &scp);
    if(SUCCESS != rc) {
        ESP_LOGE(IotDataMqtt::TAG, "aws_iot_shadow_connect returned error %d, aborting...", rc);
        abort();
    }

    /*
     * Enable Auto Reconnect functionality. Minimum and Maximum time of Exponential backoff are set in aws_iot_config.h
     *  #AWS_IOT_MQTT_MIN_RECONNECT_WAIT_INTERVAL
     *  #AWS_IOT_MQTT_MAX_RECONNECT_WAIT_INTERVAL
     */
    rc = aws_iot_shadow_set_autoreconnect_status(&mqttClient, true);
    if(SUCCESS != rc) {
        ESP_LOGE(IotDataMqtt::TAG, "Unable to set Auto Reconnect to true - %d, aborting...", rc);
    }

    if(SUCCESS != rc) {
        ESP_LOGE(IotDataMqtt::TAG, "Shadow Register Delta Error");
    }
	


    return rc;
}

int IotDataMqtt::sendraw(char* JsonDocumentBuffer) {

    IoT_Error_t rc = SUCCESS;
    bool sent = false;

    while(NETWORK_ATTEMPTING_RECONNECT == rc || NETWORK_RECONNECTED == rc || SUCCESS == rc) {
		ESP_LOGI(TAG, "yield");
        rc = aws_iot_shadow_yield(&mqttClient, 100);
        if(NETWORK_ATTEMPTING_RECONNECT == rc || shadowUpdateInProgress) {
			ESP_LOGI(TAG, "yield2");
            rc = aws_iot_shadow_yield(&mqttClient, 100);
            // If the client is attempting to reconnect, or already waiting on a shadow update,
            // we will skip the rest of the loop.
            continue;
        }
        if (sent) {
              break;
        }
        ESP_LOGI(IotDataMqtt::TAG, "Update Shadow: %s", JsonDocumentBuffer);
        rc = aws_iot_shadow_update(&mqttClient, thingName, JsonDocumentBuffer,
                            ShadowUpdateStatusCallback, NULL, 4, true);
		ESP_LOGI(TAG, "update: %d",rc);
        shadowUpdateInProgress = true;
        sent = true;
		vTaskDelay(2000 / portTICK_RATE_MS);
        ESP_LOGI(TAG, "*****************************************************************************************");
    }

    if(SUCCESS != rc) {
        ESP_LOGE(TAG, "An error occurred in the loop %d", rc);
    }

    return rc;
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

