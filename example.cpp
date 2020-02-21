#include <iostream>
#include <chrono>
#include <thread>
#include <atomic>
#include <deque>

#include "src/fiber.h"
using namespace std;

// Input data for performing computations
struct ComputeTask
{
	czsf_sync_t* semaphore;
	uint64_t* input;
	uint64_t* output;
};

static deque<ComputeTask> COMPUTE_QUEUE;
static std::atomic_flag COMPUTE_QUEUE_LOCK = ATOMIC_FLAG_INIT;
static bool EXITING = false;

// Demonstrating use of semaphore
void waitForComputation(uint64_t* input)
{
	// Create semaphore to signal once computation is done
	czsf_sync_t sem = czsf_semaphore(0);

	uint64_t result;

	ComputeTask task = {};
	task.input = input;
	task.output = &result;
	task.semaphore = &sem;

	// Add computation info to global computation queue
	while (COMPUTE_QUEUE_LOCK.test_and_set(std::memory_order_relaxed));
	COMPUTE_QUEUE.push_back(task);
	COMPUTE_QUEUE_LOCK.clear();

	// Halt fiber execution until semaphore is signaled
	czsf_wait(&sem);
	cout << "Expensive computation finished: " << *input << " -> " << result << endl;
}

void expensiveComputation()
{
	// Acquire next task from computation queue, if any
	while (COMPUTE_QUEUE_LOCK.test_and_set(std::memory_order_relaxed));

	ComputeTask task;

	if (COMPUTE_QUEUE.size() == 0) {
		COMPUTE_QUEUE_LOCK.clear();
		return;
	} else {
		task = COMPUTE_QUEUE.front();
		COMPUTE_QUEUE.pop_front();
	}

	COMPUTE_QUEUE_LOCK.clear();

	// Pretend the computation takes a while
	this_thread::sleep_for(1s);
	*task.output = *task.input + 1;

	// Signal the semaphore
	czsf_signal(task.semaphore);
}

// Demonstrating use of barrier
void waitForAllComputations()
{
	czsf_sync_t barrier = czsf_barrier(5);
	uint64_t inputData[] = {3, 6, 9, 12, 15};
	czsf::run(waitForComputation, inputData, 5, &barrier);
	czsf_wait(&barrier);

	cout << "All computations have finished." << endl;
	EXITING = true;
}

int main()
{
	// Spawn some worker threads for concurrency
	thread t1 ([] { while(!EXITING) { czsf_yield(); } });
	thread t2 ([] { while(!EXITING) { czsf_yield(); } });
	thread t3 ([] { while(!EXITING) { expensiveComputation(); } });

	czsf::run(waitForAllComputations);

	// Try to execute fibers
	while(!EXITING) { czsf_yield(); }

	t1.join();
	t2.join();
	t3.join();
}