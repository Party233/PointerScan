#include <iostream>
#include <memory>
#include <vector>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <chrono>
#include <ctime>
#include <sys/types.h>
#include <unistd.h>



int g_value = 0;
void AddValueThread() {
	while(1) {
		g_value++;
		printf("pid:%d, g_value addr:%p, g_value: %d\n", getpid(), (void*)&g_value, g_value);
		fflush(stdout);
		sleep(1);
	}
}

std::string TimestampToDatetime(uint64_t timestamp) {
    time_t t = (time_t)timestamp;
    struct tm *tm_info = localtime(&t);
    char buffer[256] = {0};
    strftime(buffer, 26, "%Y-%m-%d %H:%M:%S", tm_info);
    return buffer;
}


int main(int argc, char *argv[]){

	int pid = getpid();

	//启动数值增加线程
	std::thread tdValue(AddValueThread);
	tdValue.detach();
	sleep(1000*5000);//250秒 用于测试

    return 0;
}