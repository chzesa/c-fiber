#include <iostream>
#include <chrono>
#include <thread>
#include <atomic>
#include <deque>

#include "src/fiber.hpp"
using namespace std;
using namespace czsfiber;

// Input data for performing computations
struct ComputeTask
{
	Semaphore* semaphore;
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
	Semaphore sem;

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
	sem.wait();
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
	task.semaphore->signal();
}

// Demonstrating use of barrier
void waitForAllComputations()
{
	TaskDecl decls[5];
	uint64_t inputData[] = {3, 6, 9, 12, 15};

	// Create fiber declarations
	for (int i = 0; i < 5; i++)
	{
		decls[i] = TaskDecl(waitForComputation, &inputData[i]);
	}

	Barrier* barrier;
	// runTasks allocates a barrier in heap if pointer to Barrier* is passed
	runTasks(decls, 5, &barrier);
	barrier->wait();
	// cleanup
	delete barrier;

	cout << "All computations have finished." << endl;
	EXITING = true;
}

int main()
{
	// Spawn some worker threads for concurrency
	thread t1 ([] { while(!EXITING) { yield(); } });
	thread t2 ([] { while(!EXITING) { yield(); } });
	thread t3 ([] { while(!EXITING) { expensiveComputation(); } });

	// Create fiber for demonstrating usage of barrier
	TaskDecl decl(waitForAllComputations);
	runTasks(&decl, 1, nullptr);

	// Try to execute fibers
	while(!EXITING) { yield(); }

	t1.join();
	t2.join();
	t3.join();
}