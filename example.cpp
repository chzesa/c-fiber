#include <iostream>
#include <chrono>
#include <thread>
#include <atomic>
#include <deque>

#include "src/fiber.hpp"
using namespace std;

struct ComputeTask;
static deque<ComputeTask> computeQueue;
static std::atomic_flag computeQueueLock = ATOMIC_FLAG_INIT;
using namespace czsfiber;

struct ComputeTask
{
	Semaphore* m_semaphore;
	uint64_t* m_input;
	uint64_t* m_output;
};

void expensiveComputation()
{
	while (computeQueueLock.test_and_set(std::memory_order_relaxed));

	ComputeTask task;

	if (computeQueue.size() == 0) {
		computeQueueLock.clear();
		return;
	} else {
		task = computeQueue.front();
		computeQueue.pop_front();
	}

	computeQueueLock.clear();

	this_thread::sleep_for(1s);
	*task.m_output = *task.m_input + 1;

	task.m_semaphore->signal();
}

void waitForExpensiveComputation(uint64_t* input)
{
	uint64_t output;
	Semaphore sem;

	ComputeTask task = {};
	task.m_input = input;
	task.m_output = &output;
	task.m_semaphore = &sem;

	while (computeQueueLock.test_and_set(std::memory_order_relaxed));
	computeQueue.push_back(task);
	computeQueueLock.clear();

	sem.wait();

	cout << "Expensive computation finished: " << *input << " -> " << output << "\n\0";
}

int main()
{
	thread t1 ([] {
		while(true) {
			yield();
			this_thread::sleep_for(chrono::milliseconds(1));
		}
	});

	thread t2 ([] {
		while(true) {
			yield();
			this_thread::sleep_for(chrono::milliseconds(1));
		}
	});

	thread t3([] {
		while(true)
		{
			expensiveComputation();
			this_thread::sleep_for(chrono::milliseconds(1));
		}
	});

	TaskDecl decls[5];
	uint64_t inputData[] = {3, 6, 9, 12, 15};

	for (int i = 0; i < 5; i++)
	{
		decls[i] = TaskDecl(waitForExpensiveComputation, &inputData[i]);
	}

	Barrier* barrier;
	runTasks(decls, 5, &barrier);

	while (barrier->m_value > 0)
	{
		this_thread::sleep_for(1s);
	}
	
	delete barrier;
}