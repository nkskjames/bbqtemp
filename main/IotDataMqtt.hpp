#ifndef IOTDATAMQTT_H_
#define IOTDATAMQTT_H_

#include "aws_iot_config.h"
#include "aws_iot_log.h"
#include "aws_iot_version.h"
#include "aws_iot_mqtt_client_interface.h"
#include "aws_iot_shadow_interface.h"

#include "IotData.hpp"

using namespace std;

typedef struct {
    bool valid;
    double t[4];
	double tu[4];
	double tl[4];
	char td[4][128];
	char token[164];
    char thingName[64];
	char prettyName[64];
	char username[64];
} data_t;


extern data_t thingData;

class IotDataMqtt : public IotData {
	
	AWS_IoT_Client mqttClient;
	char thingId[32];
	char thingName[32];
	char token[150];

	//data_t thingData;

	const char* TAG = "shadow";

        // set in sdkconfig
	const char* HOST = CONFIG_AWS_IOT_MQTT_HOST;
	uint32_t PORT = CONFIG_AWS_IOT_MQTT_PORT;
	
        public:
	virtual int signup(char*,char*);
	virtual int subscribe(char*);
	virtual int init(char*);
	virtual int sendraw(char*);
	virtual int close();
	
	char* getToken() {
		return thingData.token;
	}
};

#endif
