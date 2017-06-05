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

	const char* WEB_SERVER = "bbqtest-c47a8.firebaseio.com";
	const char* WEB_PORT = "443";
	char buf[512];
	const char* TAG = "IotSSL";
	public:
		
		int send();
		int init();
		void close();
		
};

#endif

