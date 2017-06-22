
#ifndef MAIN_BOOTWIFI_H_
#define MAIN_BOOTWIFI_H_

typedef void (*bootwifi_callback_t)(int rc);

#define SSID_SIZE (32) // Maximum SSID size
#define SSID_PASSWORD_SIZE (64) // Maximum password size
#define EMAIL_SIZE (64) // Maximum email size
#define PASSWORD_SIZE (64) // Maximum password size
#define TOKEN_SIZE (164) // Maximum token size
#define USERID_SIZE (30) // Maximum userid size
typedef struct {
	char ssid[SSID_SIZE];
	char ssid_password[SSID_PASSWORD_SIZE];
	char email[EMAIL_SIZE];
	char password[PASSWORD_SIZE];
    char token[TOKEN_SIZE];
    char userid[USERID_SIZE];
	uint8_t setup_done;
	tcpip_adapter_ip_info_t ipInfo; // Optional static IP information
} connection_info_t;
int getConnectionInfo(connection_info_t*);
void saveConnectionInfo(connection_info_t*);
void getThingName(char* thingName);

void bootWiFi(bootwifi_callback_t);


#endif /* MAIN_BOOTWIFI_H_ */
