#include <iostream>
#include <chrono>
#include <thread>
#include <atomic>
#include <deque>

#define CZSF_IMPLEMENTATION
#include "czsf.h"
using namespace std;

// Input data type for performing computations
struct ComputeTask
{
	czsf::Semaphore* semaphore;
	uint64_t* input;
	uint64_t* output;
};

static deque<ComputeTask> COMPUTE_QUEUE;
static std::atomic_flag COMPUTE_QUEUE_LOCK = ATOMIC_FLAG_INIT;
volatile static bool EXITING = false;

void addTaskToComputeQueue(ComputeTask task);
bool getTaskFromComputeQueue(ComputeTask* task);

void waitForComputation(uint64_t* input)
{
	czsf::Semaphore sem(0);                                 // Semaphore to be signaled once computation finishes

	uint64_t result;

	ComputeTask task = {
		semaphore: &sem,
		input: input,
		output: &result
	};

	addTaskToComputeQueue(task);
	sem.wait();                                             // Wait on semaphore, execution continues once it's
	                                                        // signaled

	cout << "Expensive computation finished: "              // Announce result
		<< *input << " -> " << result << endl;
}

void expensiveComputation()
{
	ComputeTask task;
	if (!getTaskFromComputeQueue(&task)) return;

	this_thread::sleep_for(1s);                             // Pretend the computation takes a while
	*task.output = *task.input + 1;                         // Write result
	task.semaphore->signal();
}

void waitForAllComputations()
{
	czsf::Barrier barrier(5);                               // Create a barrier with value equal to the number of
	                                                        // tasks

	uint64_t inputData[] = {3, 6, 9, 12, 15};
	czsf::run(waitForComputation, inputData, 5, &barrier);
	barrier.wait();                                         // Execution continues once this barrier is signaled 5
	                                                        // times

	cout << "All computations have finished." << endl;
	EXITING = true;
}

int main()
{
	thread t1 ([] { while(!EXITING) { czsf_yield(); } });   // Spawn worker threads for concurrency
	thread t2 ([] { while(!EXITING) { czsf_yield(); } });
	thread t3 ([] { while(!EXITING) { expensiveComputation(); } });

	czsf::run(waitForAllComputations);                      // Using Sync primitives requires they're called from a
	                                                        // fiber, so at least one function must be run this way.
	                                                        // Whether worker threads are initialized in main or
	                                                        // from a fiber comes down to user preference.

	t1.join();                                              // Wait for work to finish. Some libraries require
	t2.join();                                              // functions be called from the main thread only. In
	t3.join();                                              // such a case condvars should be used to wake the main
	                                                        // thread whenever necessary with a messaging system
	                                                        // that allows the fibers to communicate with the main
	                                                        // thread.
}

void addTaskToComputeQueue(ComputeTask task)
{
	while (COMPUTE_QUEUE_LOCK.test_and_set(std::memory_order_relaxed));
	COMPUTE_QUEUE.push_back(task);
	COMPUTE_QUEUE_LOCK.clear();
}

bool getTaskFromComputeQueue(ComputeTask* task)
{
	while (COMPUTE_QUEUE_LOCK.test_and_set(std::memory_order_relaxed));

	if (COMPUTE_QUEUE.size() == 0) {
		COMPUTE_QUEUE_LOCK.clear();
		return false;
	} else {
		*task = COMPUTE_QUEUE.front();
		COMPUTE_QUEUE.pop_front();
	}

	COMPUTE_QUEUE_LOCK.clear();
	return true;
}