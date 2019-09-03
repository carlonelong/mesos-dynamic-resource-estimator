MESOS_SRC_DIR = /root/mesos

all :
	g++ -O2 -fpic -std=c++11 -I$(MESOS_SRC_DIR)/include -I$(MESOS_SRC_DIR)/lib/mesos/3rdparty/include -I$(MESOS_SRC_DIR)/lib/mesos/3rdparty/usr/local/include -I$(MESOS_SRC_DIR)/3rdparty/libprocess/include -I$(MESOS_SRC_DIR)/3rdparty/stout/include -I$(MESOS_SRC_DIR)/build/3rdparty/picojson-1.3.0 -I$(MESOS_SRC_DIR)/build/3rdparty/rapidjson-1.1.0/include -I$(MESOS_SRC_DIR)/build/include -I$(MESOS_SRC_DIR)/build/3rdparty/protobuf-3.5.0/src -c dynamic_resource_estimator.cpp -o dynamic_resource_estimator.o 
	gcc -shared -L/root/mesos/build/src/.libs -o libdynamic_resource_estimator.so dynamic_resource_estimator.o
clean :
	rm -rf *.o *.so 
