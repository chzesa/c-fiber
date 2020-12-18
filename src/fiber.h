#ifndef CZSF_IMPL
#define CZSF_IMPL

#ifndef CZSF_STACK_SIZE
#define CZSF_STACK_SIZE 1024 * 1024
#endif

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

// ########
/*
	czsf_task_decl_t represents a single fiber execution.
	Each consists of a function pointer and optionally
	a data pointer that is passed as a parameter to the
	function when the fiber begins executing.

	When this documentation mentions "calling from a fiber"
	it means invoking the function in question from any
	execution that was triggered via the czsf_run
	functions.
*/

struct czsf_task_decl_t
{
	void (*fn)(void*);
	void* param;
};

struct czsf_task_decl_t czsf_task_decl(void (*fn)(void*), void* param);
struct czsf_task_decl_t czsf_task_decl2(void (*fn)());

// ########

struct czsf_fiber_t;
struct czsf_list_t
{
	struct czsf_fiber_t* head;
	struct czsf_fiber_t* tail;
};

// ########
/*
	Base synchronization primitive type for threads.
	Unlike a spinlock, calling czsf_wait() on a
	primitive halts the execution of the calling
	fiber if the primitive's condition is not met.

	Signaling a primitive with czsf_signal() never
	causes the calling fiber to stop executing in
	favor of a waiting fiber.

	Two primitive types are supported:
		* Semaphore, create with czsf_semaphore(value)
		* Barrier, create with czsf_barrier(value)

	Semaphores allow up to one fiber through per signal,
	plus an additional number equal to the initial value
	of the semaphore.

	If a semaphore has waiting fibers when it is signaled,
	one fiber is released. Otherwise its value is increased by 1.

	Waiting on a semaphore pauses the execution of the fiber
	if the semaphore value <= 0. Otherwise its value is decreased
	by 1 and the fiber continues executing.

	Example use cases:
		* mutex

	Barrier is used to block fibers until it has been signaled
	the specified number of times.

	Signaling a barrier reduces it's value by 1, to a minimum of 0.
	Once the value becomes 0 all waiting fibers are released at once.

	Waiting on a barrier places the waiting fiber in the queue
	if the barrier value >0, otherwise the fiber continues
	executing.

	Example uses cases:
		* multiple fibers waiting on a single event

	czsf_signal can be called from anywhere.
	czsf_wait must only be called from a fiber.
*/

enum czsf_sync_kind
{
	CZSF_SYNC_SEMAPHORE,
	CZSF_SYNC_BARRIER
};

struct czsf_spinlock_t
{
	volatile uint32_t value;
};

struct czsf_sync_t
{
	enum czsf_sync_kind kind;
	volatile int64_t value;
	struct czsf_spinlock_t lock;
	struct czsf_list_t queue;
};

struct czsf_sync_t czsf_semaphore(int64_t value);
struct czsf_sync_t czsf_barrier(int64_t count);

void czsf_signal(struct czsf_sync_t* self);
void czsf_wait(struct czsf_sync_t* self);

// ########

// Call repeatedly from a thread to execute fibers. This must not be
// called from a fiber.
void czsf_yield();

// ########

/*
	czsf_run functions are used to queue up new fiber tasks for
	execution.

	Functions with _signal suffix take a synchronization primitive
	pointer (NULL or not null) and signal the primitive once the entire
	execution of the fiber finishes (provided the pointer isn't null).
	The synchronization primitive must remain valid until all tasks in
	the czsf_run call have signaled it.

	Below are examples of how the different functions are to be used.
*/

/*
	void task(Data* d);
	...
	Data param = {};
	czsf_task_decl_t decl = czsf_task_decl(task, &param);
	czsf_run(&decl, 1);
*/

void czsf_run(struct czsf_task_decl_t* decls, uint64_t count);
void czsf_run_signal(struct czsf_task_decl_t* decls, uint64_t count, struct czsf_sync_t* sync);

/*
	void task(Data* d);
	...
	Data params[3] = {};
	czsf_run_mono(task, params, 3);
*/

void czsf_run_mono(void (*fn)(void*), void* param, uint64_t param_size, uint64_t count);
void czsf_run_mono_signal(void (*fn)(void*), void* param, uint64_t param_size, uint64_t count, struct czsf_sync_t* sync);

/*
	void task(Data* d);
	...
	Data* params[3] = {};
	czsf_run_mono_pp(task, params, 3);
*/

void czsf_run_mono_pp_signal(void (*fn)(void*), void** param, uint64_t count, struct czsf_sync_t* sync);
void czsf_run_mono_pp(void (*fn)(void*), void** param, uint64_t count);

// ########

#ifdef __cplusplus
}

namespace czsf
{

struct Sync
{
	virtual void wait() = 0;
	virtual void signal() = 0;

	czsf_sync_t s;
};

struct Barrier : Sync
{
	Barrier();
	Barrier(int64_t value);
	void wait() override;
	void signal() override;
};

struct Semaphore : Sync
{
	Semaphore();
	Semaphore(int64_t value);
	void wait() override;
	void signal() override;
};

template<class T>
czsf_task_decl_t taskDecl(void (*fn)(T*), T* param)
{
	czsf_task_decl_t ret;
	ret.fn = reinterpret_cast<void(*)(void*)>(fn);
	ret.param = param;
	return ret;
}

czsf_task_decl_t taskDecl(void (*fn)());

template<class T>
void run(void (*fn)(T*), T* param, uint64_t count, struct czsf_sync_t* sync)
{
	czsf_run_mono_signal((void (*)(void*))(fn), param, sizeof(T), count, sync);
}

template<class T>
void run(void (*fn)(T*), T** param, uint64_t count, struct czsf_sync_t* sync)
{
	czsf_run_mono_pp_signal((void (*)(void*))(fn), (void**)param, count, sync);
}

template<class T>
void run(void (*fn)(T*), T* param, uint64_t count, czsf::Sync* sync)
{
	czsf_run_mono_signal((void (*)(void*))(fn), param, sizeof(T), count, &sync->s);
}

template<class T>
void run(void (*fn)(T*), T** param, uint64_t count, czsf::Sync* sync)
{
	czsf_run_mono_pp_signal((void (*)(void*))(fn), (void**)param, count, &sync->s);
}

template<class T>
void run(void (*fn)(T*), T* param, uint64_t count)
{
	czsf::run(fn, param, count, nullptr);
}

template<class T>
void run(void (*fn)(T*), T** param, uint64_t count)
{
	czsf::run(fn, param, count, nullptr);
}

void run(struct czsf_task_decl_t* decls, uint64_t count, struct czsf_sync_t* sync);
void run(struct czsf_task_decl_t* decls, uint64_t count);
void run(void (*fn)(), struct czsf_sync_t* sync);
void run(void (*fn)());

}
#endif


#endif