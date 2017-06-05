/*
 * bootwifi.h
 *
 *  Created on: Nov 25, 2016
 *      Author: kolban
 */

#ifndef MAIN_BOOTWIFI_H_
#define MAIN_BOOTWIFI_H_

typedef void (*bootwifi_callback_t)(int rc);

#define SSID_SIZE (32) // Maximum SSID size
#define PASSWORD_SIZE (64) // Maximum password size
#define USERNAME_SIZE (64) // Maximum username size
typedef struct {
	char ssid[SSID_SIZE];
	char password[PASSWORD_SIZE];
	char username[USERNAME_SIZE];
	tcpip_adapter_ip_info_t ipInfo; // Optional static IP information
} connection_info_t;
int getConnectionInfo(connection_info_t*);

void bootWiFi(bootwifi_callback_t);


#endif /* MAIN_BOOTWIFI_H_ */
