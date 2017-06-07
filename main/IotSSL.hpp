#ifndef IOT2SSL_H_
#define IOT2SSL_H_

using namespace std;
/*

*/
class IotSSL {
	mbedtls_ssl_context ssl;
	mbedtls_net_context server_fd;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_x509_crt cacert;
    mbedtls_ssl_config conf;

	char buf[512];
	const char* TAG = "IotSSL";
	int error = 0;
    char server[256];
    char port[4];
    
	public:
        IotSSL(const char* server, const char* port) {
                strcpy(this->server, server);
                strcpy(this->port, port);
        }
        void buildMessage(char*, int, const char*, char*);
		int init();
		int stop();
		int isAvailable();
		int data_to_read();
		int write(unsigned char*, uint16_t);
		int read(unsigned char*, int);
		int send(unsigned char*, unsigned char*, int);
};

#endif

