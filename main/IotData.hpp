#ifndef IOTDATA_H_
#define IOTDATA_H_

using namespace std;

class IotData {
	public:
	virtual int signup(char*,char*);
	virtual int init(char*);
	virtual int subscribe(char*);
	virtual int close();
};

#endif
