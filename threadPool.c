#include "threadPool.h"
#include <pthread.h>
#include <string.h>
#include <unistd.h>

const int NUMADD = 2;

typedef struct Task
{
	void (*func)(void* arg);
	void* arg;
} Task;


// �̳߳ؽṹ��
struct ThreadPool
{
	// �������
	Task* taskQ;
	int queueCapacity;  // ����
	int queueSize;      // ��ǰ�������
	int queueFront;     // ��ͷ -> ȡ����
	int queueRear;      // ��β -> ������

	pthread_t managerID;    // �������߳�ID
	pthread_t* threadIDs;   // �������߳�ID
	int minNum;             // ��С�߳�����
	int maxNum;             // ����߳�����
	int busyNum;            // æ���̵߳ĸ���
	int liveNum;            // �����̵߳ĸ���
	int exitNum;            // Ҫ���ٵ��̸߳���
	pthread_mutex_t mutexPool;  // ���������̳߳�
	pthread_mutex_t mutexBusy;  // ��busyNum����
	pthread_cond_t notFull;     // ��������ǲ�������
	pthread_cond_t notEmpty;    // ��������ǲ��ǿ���

	int shutdown;           // �ǲ���Ҫ�����̳߳�, ����Ϊ1, ������Ϊ0
};

ThreadPool* threadPoolCreate(int minNum, int maxNum, int queueSize) {
	ThreadPool* pool = (ThreadPool*)malloc(sizeof(ThreadPool));
	do
	{
		if (!pool)
		{
			printf("error\n");
			break;
		}
		pool->threadIDs = (pthread_t*)malloc(sizeof(pthread_t) * maxNum);
		if (!pool->threadIDs)
		{
			break;
		}
		memset(pool->threadIDs, 0, sizeof(pthread_t) * maxNum);

		pool->minNum = minNum;
		pool->maxNum = maxNum;
		pool->busyNum = 0;
		pool->liveNum = minNum;
		pool->exitNum = 0;

		if (pthread_mutex_init(&pool->mutexPool, NULL) != 0 ||
			pthread_mutex_init(&pool->mutexBusy, NULL) != 0 ||
			pthread_cond_init(&pool->notEmpty, NULL) != 0 ||
			pthread_cond_init(&pool->notFull, NULL) != 0)
		{
			printf("mutex or condition init fail...\n");
			break;
		}

		pool->taskQ = (Task*)malloc(sizeof(Task) * queueSize);
		pool->queueCapacity = queueSize;
		pool->queueSize = 0;
		pool->queueFront = 0;
		pool->queueRear = 0;
		pool->shutdown = 0;

		// �����߳�
		pthread_create(&pool->managerID, NULL, manager, pool);
		for (int i = 0; i < minNum; i++)
		{
			pthread_create(&pool->threadIDs[i], NULL, worker, pool);
		}
		return pool;
	} while (0);

	//�ͷ���Դ
	if (pool && pool->threadIDs) {
		free(pool->threadIDs);
	}
	if (pool && pool->taskQ) {
		free(pool->taskQ);
	}
	if (pool) free(pool);
	return NULL;
}

int threadPoolDestroy(ThreadPool* pool) {
	if (!pool) {
		return -1;
	}

	pool->shutdown = 1;
	pthread_join(pool->managerID, NULL);

	for (int i = 0; i < pool->liveNum; ++i) {
		pthread_cond_signal(&pool->notEmpty);
	}

	//�ͷŶ��ڴ�
	if (pool->taskQ) {
		free(pool->taskQ);
	}
	if (pool->threadIDs) {
		free(pool->threadIDs);
	}

	pthread_mutex_destroy(&pool->mutexBusy);
	pthread_mutex_destroy(&pool->mutexPool);
	pthread_cond_destroy(&pool->notEmpty);
	pthread_cond_destroy(&pool->notFull);

	free(pool);
	pool = NULL;
	return 0;
}

void* worker(void* arg) {
	ThreadPool* pool = (ThreadPool*)arg;
	while (1) {
		pthread_mutex_lock(&pool->mutexPool);
		while (pool->queueSize == 0 && !pool->shutdown)
		{
			//�����߳�
			pthread_cond_wait(&pool->notEmpty, &pool->mutexPool);

			//�ж��Ƿ������߳�
			if (pool->exitNum > 0) {
				pool->exitNum--;
				if (pool->liveNum > pool->minNum) {
					pool->liveNum--;
					pthread_mutex_unlock(&pool->mutexPool);
					threadExit(pool);
				}
			}
		}
		if (pool->shutdown) {
			pthread_mutex_unlock(&pool->mutexPool);
			threadExit(pool);
		}
		//���renw
		Task task;
		task.func = pool->taskQ[pool->queueFront].func;
		task.arg = pool->taskQ[pool->queueFront].arg;
		pool->queueFront = (pool->queueFront + 1) % pool->queueCapacity;
		pool->queueSize--;
		pthread_cond_signal(&pool->notFull);
		pthread_mutex_unlock(&pool->mutexPool);

		printf("thread %ld start working...\n", pthread_self());
		pthread_mutex_lock(&pool->mutexBusy);
		pool->busyNum++;
		pthread_mutex_unlock(&pool->mutexBusy);

		task.func(task.arg);
		free(task.arg);
		task.arg = NULL;
		printf("thread %ld end working...\n", pthread_self());
		pthread_mutex_lock(&pool->mutexBusy);
		pool->busyNum--;
		pthread_mutex_unlock(&pool->mutexBusy);
	}
}

void* manager(void* arg) {
	ThreadPool* pool = (ThreadPool*)arg;
	while (!pool->shutdown) {
		sleep(3);

		//ȡ���������͵�ǰ�߳�����
		pthread_mutex_lock(&pool->mutexPool);
		int queueSize = pool->queueSize;
		int liveNum = pool->liveNum;
		pthread_mutex_unlock(&pool->mutexPool);

		//ȡ��æ�߳���
		pthread_mutex_lock(&pool->mutexBusy);
		int busyNum = pool->busyNum;
		pthread_mutex_unlock(&pool->mutexBusy);

		if (queueSize > liveNum && liveNum < pool->maxNum) {
			//����߳�
			pthread_mutex_lock(&pool->mutexPool);
			int count = 0;
			for (int i = 0; i < pool->maxNum
				&& count < NUMADD && pool->liveNum < pool->maxNum; ++i) {
				if (!pool->threadIDs[i]) { //�ҵ����Բ����̵߳�λ��
					pthread_create(pool->threadIDs[i], NULL, worker, pool);
					count++;
					pool->liveNum++;
				}
			}
			pthread_mutex_unlock(&pool->mutexPool);
		}

		//�����߳�
		if (2 * busyNum < liveNum && liveNum > pool->minNum) {
			pthread_mutex_lock(&pool->mutexPool);
			pool->exitNum = NUMADD;
			pthread_mutex_unlock(&pool->mutexPool);
			for (int i = 0; i < NUMADD; ++i) {
				pthread_cond_signal(&pool->notEmpty);
			}
		}
	}
}

void threadExit(ThreadPool* pool) {
	pthread_t pid = pthread_self();
	for (int i = 0; i < pool->maxNum; ++i) {
		if (pool->threadIDs[i] == pid) {
			pool->threadIDs[i] = 0;
			printf("ThreadExit called, %ld id thread exit...", pid);
			break;
		}
	}
	pthread_exit(NULL);
}

void threadPoolAdd(ThreadPool* pool, void(*func)(void*), void* arg) {
	pthread_mutex_lock(&pool->mutexPool);
	while (pool->queueSize == pool->queueCapacity && !pool->shutdown)
	{
		// �����߳�
		pthread_cond_wait(&pool->notFull, &pool->mutexPool);
	}

	if (pool->shutdown) {
		pthread_mutex_unlock(&pool->mutexPool);
		return;
	}

	//�������
	pool->taskQ[pool->queueRear].func = func;
	pool->taskQ[pool->queueRear].arg = arg;
	pool->queueRear = (pool->queueRear + 1) % pool->queueCapacity;
	pool->queueSize++;
	pthread_cond_signal(&pool->notEmpty);
	pthread_mutex_unlock(&pool->mutexPool);
}

int threadPoolBusyNum(ThreadPool* pool) {
	pthread_mutex_lock(&pool->mutexBusy);
	int busyNum = pool->busyNum;
	pthread_mutex_unlock(&pool->mutexBusy);
	return busyNum;
}

int threadPoolAliveNum(ThreadPool* pool) {
	pthread_mutex_lock(&pool->mutexPool);
	int liveNum = pool->liveNum;
	pthread_mutex_unlock(&pool->mutexPool);
	return liveNum;
}
