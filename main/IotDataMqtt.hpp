#ifndef IOTDATAMQTT_H_
#define IOTDATAMQTT_H_

#include "aws_iot_config.h"
#include "aws_iot_log.h"
#include "aws_iot_version.h"
#include "aws_iot_mqtt_client_interface.h"
#include "aws_iot_shadow_interface.h"

#include "IotData.hpp"
#include "thingData.h"

using namespace std;

class IotDataMqtt {
	
	AWS_IoT_Client mqttClient;
	const char* TAG = "shadow";

        // set in sdkconfig
	const char* HOST = CONFIG_AWS_IOT_MQTT_HOST;
	uint32_t PORT = CONFIG_AWS_IOT_MQTT_PORT;
	
        public:
	int subscribe(char*,char*);
	int close();
	
	char* getToken() {
		return thingData.token;
	}
};

#endif
