#include <stdio.h>
#include "threadPool.h"
#include <pthread.h>
#include <unistd.h>
#include <threads.h>

void testFunc(void* arg) {
	int num = *(int*)arg;
	printf("thread %ld is working, working num is %d", pthread_self(), num);
	usleep(1000);
	
}


int main() {
	int i;
	ThreadPool* pool = threadPoolCreate(3, 10, 100);
	for (i = 0; i < 10; i++) {
		int* num = (int*)malloc(sizeof(int));
		*num = i + 100;
		threadPoolAdd(pool, testFunc, num);
	}

	sleep(30);
	threadPoolDestroy(pool);
	return 0;
}