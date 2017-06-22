#ifndef IOT2SSL_H_
#define IOT2SSL_H_


#include "mbedtls/platform.h"
#include "mbedtls/net.h"
#include "mbedtls/esp_debug.h"
#include "mbedtls/ssl.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/error.h"
#include "mbedtls/certs.h"

using namespace std;
/*

*/
class IotSSL {
	mbedtls_ssl_context ssl;
	mbedtls_net_context server_fd;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
	mbedtls_pk_context pkey;
    mbedtls_x509_crt cacert;
	mbedtls_x509_crt clicert;
    mbedtls_ssl_config conf;

	char buf[512];
	const char* TAG = "IotSSL";
	int error = 0;
    char server[256];
    char port[5];
	int readptr;
    
	public:
		IotSSL() {}
        void buildMessage(char*, int, const char*, char*);
        void buildAuthMessage(char*, int, const char*, char*);
		void init(const char* server, const char* port) {
			strcpy(this->server,server);
			strcpy(this->port,port);
		}
		int connect();
		int stop();
		int isAvailable();
		int data_to_read();
		int write(unsigned char*, uint16_t);
		int read(unsigned char*, int);
		int send(unsigned char*, char*, int);
		int sendRetry(unsigned char*, char*, int, int);
};

#endif

