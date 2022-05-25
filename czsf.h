/*
	Add the following to only one source file:
	#define CZSF_IMPLEMENTATION
	#include "czsf.h"
*/

#ifndef CZSF_HEADERS_H
#define CZSF_HEADERS_H

#ifdef __cplusplus
	extern "C" {
#endif

#include <stdint.h>

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

struct czsf_fiber_t;
struct czsf_list_t
{
	struct czsf_fiber_t* head;
	struct czsf_fiber_t* tail;
};

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

/*
	Call repeatedly from a thread to execute fibers. This must not be
	called from a fiber.
*/

void czsf_yield();

/*
	czsf_run functions are used to queue up new fiber tasks for
	execution.

	Functions with _signal suffix take a synchronization primitive
	pointer (NULL or not null) and signal the primitive once the entire
	execution of the fiber finishes (provided the pointer isn't null).
	The synchronization primitive must remain valid until all tasks in
	the czsf_run call have signaled it.

	Functions with _fls suffix enable adding data to a fiber-local
	storage. Since fibers can execute in any thread, using thread-local
	storage might cause problems when used in fibers so fls offers a
	similar mechanism. Each task will have their own copy of this data.
	The data pointed to is copied as-is when the task is first selected
	for execution. so the pointer must remain valid until all tasks
	have been started. Since there is no mechanism to detect this, it
	is recommended that the pointer remain valid until all tasks have
	finished. The data pointer _must not_ be null.

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
void czsf_run_fls(struct czsf_task_decl_t* decls, uint64_t count, void* data, uint64_t size_of_data, uint64_t align_of_data);
void czsf_run_signal_fls(struct czsf_task_decl_t* decls, uint64_t count, struct czsf_sync_t* sync, void* data, uint64_t size_of_data, uint64_t align_of_data);

/*
	void task(Data* d);
	...
	Data params[3] = {};
	czsf_run_mono(task, params, 3);
*/

void czsf_run_mono(void (*fn)(void*), void* param, uint64_t param_size, uint64_t count);
void czsf_run_mono_signal(void (*fn)(void*), void* param, uint64_t param_size, uint64_t count, struct czsf_sync_t* sync);
void czsf_run_mono_fls(void (*fn)(void*), void* param, uint64_t param_size, uint64_t count, void* data, uint64_t size_of_data, uint64_t align_of_data);
void czsf_run_mono_signal_fls(void (*fn)(void*), void* param, uint64_t param_size, uint64_t count, struct czsf_sync_t* sync, void* data, uint64_t size_of_data, uint64_t align_of_data);

/*
	void task(Data* d);
	...
	Data* params[3] = {};
	czsf_run_mono_pp(task, params, 3);
*/

void czsf_run_mono_pp(void (*fn)(void*), void** param, uint64_t count);
void czsf_run_mono_pp_signal(void (*fn)(void*), void** param, uint64_t count, struct czsf_sync_t* sync);
void czsf_run_mono_pp_fls(void (*fn)(void*), void** param, uint64_t count, void* data, uint64_t size_of_data, uint64_t align_of_data);
void czsf_run_mono_pp_signal_fls(void (*fn)(void*), void** param, uint64_t count, struct czsf_sync_t* sync, void* data, uint64_t size_of_data, uint64_t align_of_data);

void* czsf_get_fls();

#ifdef __cplusplus
}

#include <cstddef>

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


template<class F, class T>
void run(F* fls, void (*fn)(T*), T* param, uint64_t count, struct czsf_sync_t* sync)
{
	czsf_run_mono_signal_fls((void (*)(void*))(fn), param, sizeof(T), count, sync, fls, sizeof (F), alignof (F));
}

template<class F, class T>
void run(F* fls, void (*fn)(T*), T** param, uint64_t count, struct czsf_sync_t* sync)
{
	czsf_run_mono_pp_signal_fls(fls, (void (*)(void*))(fn), (void**)param, count, sync, fls, sizeof (F), alignof (F));
}

template<class F, class T>
void run(F* fls, void (*fn)(T*), T* param, uint64_t count, czsf::Sync* sync)
{
	czsf_run_mono_signal_fls((void (*)(void*))(fn), param, sizeof(T), count, &sync->s, fls, sizeof (F), alignof (F));
}

template<class F, class T>
void run(F* fls, void (*fn)(T*), T** param, uint64_t count, czsf::Sync* sync)
{
	czsf_run_mono_pp_signal_fls((void (*)(void*))(fn), (void**)param, count, &sync->s, fls, sizeof (F), alignof (F));
}

template<class F, class T>
void run(F* fls, void (*fn)(T*), T* param, uint64_t count)
{
	czsf_run_mono_signal_fls((void (*)(void*))(fn), param, sizeof(T), count, nullptr, fls, sizeof (F), alignof (F));
}

template<class F, class T>
void run(F* fls, void (*fn)(T*), T** param, uint64_t count)
{
	czsf_run_mono_pp_signal_fls(fls, (void (*)(void*))(fn), (void**)param, count, nullptr, fls, sizeof (F), alignof (F));
}

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
	czsf_run_mono_signal((void (*)(void*))(fn), param, sizeof(T), count, nullptr);
}

template<class T>
void run(void (*fn)(T*), T** param, uint64_t count)
{
	czsf_run_mono_pp_signal((void (*)(void*))(fn), (void**)param, count, nullptr);
}

template <typename T>
void run(T* data, struct czsf_task_decl_t* decls, uint64_t count, struct czsf_sync_t* sync) { czsf_run_signal_fls(decls, count, sync, data, sizeof(T), alignof(T)); }

template <typename T>
void run(T* data, struct czsf_task_decl_t* decls, uint64_t count) { czsf_run_signal_fls(decls, count, NULL, data, sizeof(T), alignof(T)); }

template <typename T>
void run(T* data, void (*fn)(), struct czsf_sync_t* sync) { czsf_run_mono_signal_fls((void (*)(void*))(fn), NULL, 0, 1, sync, data, sizeof(T), alignof(T)); }

template <typename T>
void run(T* data, void (*fn)(), czsf::Sync* sync) { czsf_run_mono_signal_fls((void (*)(void*))(fn), NULL, 0, 1, &sync->s, data, sizeof(T), alignof(T)); }

template <typename T>
void run(T* data, void (*fn)()) { czsf_run_mono_signal_fls((void (*)(void*))(fn), NULL, 0, 1, NULL, data, sizeof(T), alignof(T)); }

void run(struct czsf_task_decl_t* decls, uint64_t count, struct czsf_sync_t* sync);
void run(struct czsf_task_decl_t* decls, uint64_t count);
void run(void (*fn)(), struct czsf_sync_t* sync);
void run(void (*fn)(), czsf::Sync* sync);
void run(void (*fn)());

}
#endif


#endif // CZSF_HEADERS_H


#ifdef CZSF_IMPLEMENTATION

#ifndef CZSF_IMPLEMENTATION_GUARD_
#define CZSF_IMPLEMENTATION_GUARD_


#ifndef CZSF_STACK_SIZE
#define CZSF_STACK_SIZE 1024 * 1024
#endif

#include <stdlib.h>
#include <string.h>

#ifdef WIN32
	#define CZSF_THREAD_LOCAL __declspec(thread)
#else
	#include <threads.h>
	#define CZSF_THREAD_LOCAL thread_local
#endif

#define CZSF_SPINLOCK_INIT { 0 }
#define CZSF_LIST_INIT { NULL, NULL }
#define CZSF_STACK_INIT { NULL }

enum czsf_fiber_status
{
	CZSF_FIBER_STATUS_NORMAL,
	CZSF_FIBER_STATUS_BLOCKED
};

void czsf_spinlock_acquire(struct czsf_spinlock_t* self)
{
	while(__sync_lock_test_and_set(&self->value, 1));
}

void czsf_spinlock_release(struct czsf_spinlock_t* self)
{
	__sync_lock_release(&self->value, 0);
}

struct czsf_fiber_t
{
	struct czsf_fiber_t* next;
	enum czsf_fiber_status status;
	uint64_t stack;
	uint64_t base;
	struct czsf_task_decl_t task;
	struct czsf_sync_t* sync;
	char* stack_space;
	uint64_t* execution_counter;
	uint64_t count;
	void* fls_ptr;
	uint64_t fls_size;
	uint64_t fls_align;
};

struct czsf_fiber_t* czsf_list_pop_front(struct czsf_list_t* self)
{
	struct czsf_fiber_t* ret = self->head;
	if (ret != NULL)
	{
		self->head = ret->next;
	}

	return ret;
}

void czsf_list_push_back(struct czsf_list_t* self, struct czsf_fiber_t* head, struct czsf_fiber_t* tail)
{
	tail->next = NULL;
	if (self->head == NULL)
	{
		self->head = head;
		self->tail = tail;
	}
	else
	{
		self->tail->next = head;
		self->tail = tail;
	}
}

void czsf_list_push_front(struct czsf_list_t* self, struct czsf_fiber_t* head, struct czsf_fiber_t* tail)
{
	if (self->head == NULL)
	{
		self->tail = tail;
	}
	tail->next = self->head;
	self->head = head;
}

/*
	Storing previously allocated stack space
	Pointer to next item in ll stored as char* at head[STACK_SIZE - 1];
	Pointer to beginning of allocated area stored as char* at head[STACK_SIZE - 2];
	Stack fills from right to left so this should minimize cache misses
*/

struct czsf_stack_t
{
	char* head;
};

void czsf_stack_push(struct czsf_stack_t* self, char* item)
{
	*((char**)(&item[CZSF_STACK_SIZE - sizeof(char*)])) = self->head;
	*((char**)(&item[CZSF_STACK_SIZE - 2 * sizeof(char*)])) = item;
	self->head = item;
}

char* czsf_stack_pop(struct czsf_stack_t* self)
{
	char* ret = self->head;

	if (ret != NULL)
	{
		self->head = *((char**)(&ret[CZSF_STACK_SIZE - sizeof(char*)]));
	}

	return ret;
}


struct czsf_task_decl_t czsf_task_decl(void (*fn)(void*), void* param)
{
	struct czsf_task_decl_t ret = {fn, param};
	return ret;
}

struct czsf_task_decl_t czsf_task_decl2(void (*fn)())
{
	struct czsf_task_decl_t ret = {(void (*)(void*))fn, NULL};
	return ret;
}

static struct czsf_list_t CZSF_GLOBAL_RELEASE_QUEUE = CZSF_LIST_INIT;
static struct czsf_list_t CZSF_GLOBAL_QUEUE = CZSF_LIST_INIT;
static struct czsf_spinlock_t CZSF_GLOBAL_LOCK = CZSF_SPINLOCK_INIT;
static CZSF_THREAD_LOCAL struct czsf_stack_t CZSF_ALLOCATED_STACK_SPACE = CZSF_STACK_INIT;

static CZSF_THREAD_LOCAL struct czsf_fiber_t* CZSF_EXEC_FIBER = NULL;
static CZSF_THREAD_LOCAL struct czsf_spinlock_t* CZSF_HELD_LOCK = NULL;

static CZSF_THREAD_LOCAL uint64_t CZSF_STACK = 0;

uint64_t get_fls_loc(struct czsf_fiber_t* fiber)
{
	uint64_t fls_loc = (uint64_t)(&fiber->stack_space[CZSF_STACK_SIZE]) - fiber->fls_size;
	return fls_loc - fls_loc % fiber->fls_align;
}

void* czsf_get_fls()
{
	if (CZSF_EXEC_FIBER->fls_ptr == NULL)
		return NULL;

	return (void*)get_fls_loc(CZSF_EXEC_FIBER);
}

struct czsf_fiber_t* czsf_acquire_next_fiber()
{
	czsf_spinlock_acquire(&CZSF_GLOBAL_LOCK);
	struct czsf_fiber_t* fiber = czsf_list_pop_front(&CZSF_GLOBAL_RELEASE_QUEUE);
	if (fiber == NULL)
	{
		fiber = czsf_list_pop_front(&CZSF_GLOBAL_QUEUE);
	}
	czsf_spinlock_release(&CZSF_GLOBAL_LOCK);

	if (fiber == NULL)
	{
		return NULL;
	}

	if (fiber->status == CZSF_FIBER_STATUS_BLOCKED)
	{
		return fiber;
	}

	char* stack_space = czsf_stack_pop(&CZSF_ALLOCATED_STACK_SPACE);

	if (stack_space == NULL)
	{
		stack_space = (char*)(malloc(CZSF_STACK_SIZE));
	}

	fiber->base = (uint64_t)(&stack_space[CZSF_STACK_SIZE]);
	fiber->stack_space = stack_space;

	if (fiber->fls_ptr != NULL)
	{
		uint64_t fls_loc = get_fls_loc(fiber);
		memcpy((char*)(fls_loc), (char*)(fiber->fls_ptr), fiber->fls_size);
		fiber->base = fls_loc;
	}

	// Make room for pushq
	fiber->base = fiber->base - 8;
	// Align stack
	fiber->base -= fiber->base % 16;
	fiber->stack = fiber->base;

	return fiber;
}

void czsf_yield_return();

void czsf_exec_fiber()
{
	CZSF_EXEC_FIBER->task.fn(CZSF_EXEC_FIBER->task.param);

	if (CZSF_EXEC_FIBER->sync != NULL)
	{
		czsf_signal(CZSF_EXEC_FIBER->sync);
	}
	czsf_yield_return();
}

#define PUSHA			\
	"pushq %%rax \n\t"	\
	"pushq %%rdi \n\t"	\
	"pushq %%rsi \n\t"	\
	"pushq %%rdx \n\t"	\
	"pushq %%rcx \n\t"	\
	"pushq %%r8 \n\t"	\
	"pushq %%r9 \n\t"	\
	"pushq %%r10 \n\t"	\
	"pushq %%r11 \n\t"	\
	"pushq %%rbx \n\t"	\
	"pushq %%r12 \n\t"	\
	"pushq %%r13 \n\t"	\
	"pushq %%r14 \n\t"	\
	"pushq %%r15 \n\t"	\
	"pushq %%rbp \n\t"

#define POPA			\
	"popq %%rbp \n\t"	\
	"popq %%r15 \n\t"	\
	"popq %%r14 \n\t"	\
	"popq %%r13 \n\t"	\
	"popq %%r12 \n\t"	\
	"popq %%rbx \n\t"	\
	"popq %%r11 \n\t"	\
	"popq %%r10 \n\t"	\
	"popq %%r9 \n\t"	\
	"popq %%r8 \n\t"	\
	"popq %%rcx \n\t"	\
	"popq %%rdx \n\t"	\
	"popq %%rsi \n\t"	\
	"popq %%rdi \n\t"	\
	"popq %%rax \n\t"

#define CZSF_STORE_AND_CALL(store, fun_label)	\
	uint64_t stack;				\
	asm volatile				\
	(					\
		"movq %%rax, %0\n\t"		\
		"movq %%rsp, %%rax\n\t"		\
		"andq $15, %%rax\n\t"		\
		"subq %%rax, %%rsp\n\t"		\
		"subq $8, %%rsp\n\t"		\
		"movq %0, %%rax\n\t"		\
		PUSHA				\
		"movq %%rsp, %0"		\
		:"+m" (stack)			\
	);					\
	*store = stack - 8;			\
	asm volatile				\
	(					\
		"call " fun_label "\n\t"	\
		POPA				\
		:				\
	);

#define CZSF_RETURN_TO_STACK			\
	asm volatile				\
	(					\
		"movq %0, %%rsp\n\t"		\
		"ret"				\
		:				\
		:"r" (stack)			\
	);

void czsf_yield_2() asm("czsf_yield_2");
void czsf_yield_2()
{
	struct czsf_fiber_t* fiber = czsf_acquire_next_fiber();
	uint64_t stack;

	if (fiber == NULL)
	{
		stack = CZSF_STACK;
		CZSF_RETURN_TO_STACK
		return;
	}

	CZSF_EXEC_FIBER = fiber;
	stack = fiber->stack;

	switch (fiber->status)
	{
	case CZSF_FIBER_STATUS_NORMAL:
		asm volatile
		(
			"movq %0, %%rsp\n\t"
			"movq %%rsp, %%rbp"
			:
			:"r" (stack)
		);
		czsf_exec_fiber();
		/* Line is never reached */
	case CZSF_FIBER_STATUS_BLOCKED:
		fiber->status = CZSF_FIBER_STATUS_NORMAL;
		CZSF_RETURN_TO_STACK
	}
}

void czsf_yield()
{
	CZSF_STORE_AND_CALL(&CZSF_STACK, "czsf_yield_2")
}

void czsf_yield_block() asm ("czsf_yield_block");
void czsf_yield_block()
{
	czsf_spinlock_release(CZSF_HELD_LOCK);
	uint64_t stack = CZSF_STACK;
	CZSF_RETURN_TO_STACK
}

void czsf_yield_return()
{
	char* stack_space = CZSF_EXEC_FIBER->stack_space;
	if(__atomic_sub_fetch(CZSF_EXEC_FIBER->execution_counter, 1, __ATOMIC_SEQ_CST) == 0) {
		free((void*)(uint64_t(CZSF_EXEC_FIBER->execution_counter) - CZSF_EXEC_FIBER->count * sizeof(czsf_fiber_t)));
	}

	czsf_stack_push(&CZSF_ALLOCATED_STACK_SPACE, stack_space);

	CZSF_EXEC_FIBER = NULL;
	uint64_t stack = CZSF_STACK;
	CZSF_RETURN_TO_STACK
}

struct czsf_sync_t czsf_semaphore(int64_t value)
{
	struct czsf_sync_t s = {CZSF_SYNC_SEMAPHORE, value, CZSF_SPINLOCK_INIT, CZSF_LIST_INIT }; 
	return s;
}

struct czsf_sync_t czsf_barrier(int64_t count)
{
	struct czsf_sync_t b = {CZSF_SYNC_BARRIER, count, CZSF_SPINLOCK_INIT, CZSF_LIST_INIT }; 
	return b;
}

void czsf_signal(struct czsf_sync_t* self)
{
	switch(self->kind)
	{
	case CZSF_SYNC_BARRIER:
	{
		czsf_spinlock_acquire(&self->lock);
		if (self->value > 0 && --self->value == 0)
		{
			struct czsf_fiber_t* head = self->queue.head;
			struct czsf_fiber_t* tail = self->queue.tail;

			self->queue.head = NULL;
			czsf_spinlock_release(&self->lock);

			if (head != NULL)
			{
				czsf_spinlock_acquire(&CZSF_GLOBAL_LOCK);
				czsf_list_push_back(&CZSF_GLOBAL_RELEASE_QUEUE, head, tail);
				czsf_spinlock_release(&CZSF_GLOBAL_LOCK);
			}
		}
		else
		{
			czsf_spinlock_release(&self->lock);
		}
	} break;
	case CZSF_SYNC_SEMAPHORE:
	{
		czsf_spinlock_acquire(&self->lock);

		struct czsf_fiber_t* head = czsf_list_pop_front(&self->queue);
		if (head != NULL)
		{
			czsf_spinlock_release(&self->lock);

			czsf_spinlock_acquire(&CZSF_GLOBAL_LOCK);
			czsf_list_push_back(&CZSF_GLOBAL_RELEASE_QUEUE, head, head);
			czsf_spinlock_release(&CZSF_GLOBAL_LOCK);
		}
		else
		{
			self->value++;
			czsf_spinlock_release(&self->lock);
		}

	} break;
	}
}

void czsf_wait(struct czsf_sync_t* self)
{
	struct czsf_fiber_t* fiber = CZSF_EXEC_FIBER;
	czsf_spinlock_acquire(&self->lock);
	switch(self->kind)
	{
	case CZSF_SYNC_BARRIER:
	{
		if (self->value == 0)
		{
			czsf_spinlock_release(&self->lock);
			return;
		}
	} break;
	case CZSF_SYNC_SEMAPHORE:
	{
		if (self->value > 0)
		{
			self->value--;
			czsf_spinlock_release(&self->lock);
			return;
		}
	} break;
	}

	fiber->status = CZSF_FIBER_STATUS_BLOCKED;
	czsf_list_push_back(&self->queue, fiber, fiber);
	CZSF_HELD_LOCK = &self->lock;
	CZSF_STORE_AND_CALL(&CZSF_EXEC_FIBER->stack, "czsf_yield_block")
}

struct czsf_fiber_t* czsf_allocate_tasks(uint64_t count, struct czsf_sync_t* sync)
{
	struct czsf_fiber_t* fibers = (struct czsf_fiber_t*)malloc(count * sizeof(struct czsf_fiber_t) + sizeof(uint64_t));
	uint64_t* execution_counter = (uint64_t*)((uint64_t)fibers + count * sizeof(struct czsf_fiber_t));
	*execution_counter = count;

	for (uint64_t i = 0; i < count; i++)
	{
		fibers[i].status = CZSF_FIBER_STATUS_NORMAL;
		fibers[i].sync = sync;
		fibers[i].execution_counter = execution_counter;
		fibers[i].count = count;
		fibers[i].fls_ptr = NULL;

		if (i > 0)
		{
			fibers[i - 1].next = &fibers[i];
		}
	}

	return fibers;
}

struct czsf_fiber_t* czsf_allocate_tasks_fls(uint64_t count, struct czsf_sync_t* sync, void* data, uint64_t size_of_data, uint64_t align_of_data)
{
	struct czsf_fiber_t* fibers = (struct czsf_fiber_t*)malloc(count * sizeof(struct czsf_fiber_t) + sizeof(uint64_t));
	uint64_t* execution_counter = (uint64_t*)((uint64_t)fibers + count * sizeof(struct czsf_fiber_t));
	*execution_counter = count;

	for (uint64_t i = 0; i < count; i++)
	{
		fibers[i].status = CZSF_FIBER_STATUS_NORMAL;
		fibers[i].sync = sync;
		fibers[i].execution_counter = execution_counter;
		fibers[i].count = count;

		fibers[i].fls_ptr = data;
		fibers[i].fls_size = size_of_data;
		fibers[i].fls_align = align_of_data;

		if (i > 0)
		{
			fibers[i - 1].next = &fibers[i];
		}
	}

	return fibers;
}

void czsf_fibers_post(struct czsf_fiber_t* fibers, uint64_t count)
{
	czsf_spinlock_acquire(&CZSF_GLOBAL_LOCK);
	czsf_list_push_back(&CZSF_GLOBAL_QUEUE, fibers, fibers + count - 1);
	czsf_spinlock_release(&CZSF_GLOBAL_LOCK);
}

void czsf_run_signal(struct czsf_task_decl_t* decls, uint64_t count, struct czsf_sync_t* sync)
{
	if (count == 0)
		return;

	struct czsf_fiber_t* fibers = czsf_allocate_tasks(count, sync);
	for (uint64_t i = 0; i < count; i++)
		fibers[i].task = decls[i];

	czsf_fibers_post(fibers, count);
}

void czsf_run_mono_signal(void (*fn)(void*), void* param, uint64_t param_size, uint64_t count, struct czsf_sync_t* sync)
{
	if (count == 0)
		return;

	struct czsf_fiber_t* fibers = czsf_allocate_tasks(count, sync);
	for (uint64_t i = 0; i < count; i++)
		fibers[i].task = czsf_task_decl(fn, (char*)(param) + i * param_size);

	czsf_fibers_post(fibers, count);
}

void czsf_run_mono_pp_signal(void (*fn)(void*), void** param, uint64_t count, struct czsf_sync_t* sync)
{
	if (count == 0)
		return;

	struct czsf_fiber_t* fibers = czsf_allocate_tasks(count, sync);

	for (uint64_t i = 0; i < count; i++)
		fibers[i].task = czsf_task_decl(fn, param[i]);

	czsf_fibers_post(fibers, count);
}

void czsf_run(struct czsf_task_decl_t* decls, uint64_t count)
{
	czsf_run_signal(decls, count, NULL);
}

void czsf_run_mono(void (*fn)(void*), void* param, uint64_t param_size, uint64_t count)
{
	czsf_run_mono_signal(fn, param, param_size, count, NULL);
}

void czsf_run_mono_pp(void (*fn)(void*), void** param, uint64_t count)
{
	czsf_run_mono_pp_signal(fn, param, count, NULL);
}

void czsf_run_signal_fls(struct czsf_task_decl_t* decls, uint64_t count, struct czsf_sync_t* sync, void* data, uint64_t size_of_data, uint64_t align_of_data)
{
	if (count == 0)
		return;

	struct czsf_fiber_t* fibers = czsf_allocate_tasks_fls(count, sync, data, size_of_data, align_of_data);
	for (uint64_t i = 0; i < count; i++)
		fibers[i].task = decls[i];

	czsf_fibers_post(fibers, count);
}

void czsf_run_mono_signal_fls(void (*fn)(void*), void* param, uint64_t param_size, uint64_t count, struct czsf_sync_t* sync, void* data, uint64_t size_of_data, uint64_t align_of_data)
{
	if (count == 0)
		return;

	struct czsf_fiber_t* fibers = czsf_allocate_tasks_fls(count, sync, data, size_of_data, align_of_data);
	for (uint64_t i = 0; i < count; i++)
		fibers[i].task = czsf_task_decl(fn, (char*)(param) + i * param_size);

	czsf_fibers_post(fibers, count);
}

void czsf_run_mono_pp_signal_fls(void (*fn)(void*), void** param, uint64_t count, struct czsf_sync_t* sync, void* data, uint64_t size_of_data, uint64_t align_of_data)
{
	if (count == 0)
		return;

	struct czsf_fiber_t* fibers = czsf_allocate_tasks_fls(count, sync, data, size_of_data, align_of_data);

	for (uint64_t i = 0; i < count; i++)
		fibers[i].task = czsf_task_decl(fn, param[i]);

	czsf_fibers_post(fibers, count);
}

void czsf_run_fls(struct czsf_task_decl_t* decls, uint64_t count, void* data, uint64_t size_of_data, uint64_t align_of_data)
{
	czsf_run_signal_fls(decls, count, NULL, data, size_of_data, align_of_data);
}

void czsf_run_mono_fls(void (*fn)(void*), void* param, uint64_t param_size, uint64_t count, void* data, uint64_t size_of_data, uint64_t align_of_data)
{
	czsf_run_mono_signal_fls(fn, param, param_size, count, NULL, data, size_of_data, align_of_data);
}

void czsf_run_mono_pp_fls(void (*fn)(void*), void** param, uint64_t count, void* data, uint64_t size_of_data, uint64_t align_of_data)
{
	czsf_run_mono_pp_signal_fls(fn, param, count, NULL, data, size_of_data, align_of_data);
}

#ifdef __cplusplus
namespace czsf
{

Barrier::Barrier() { this->s = czsf_barrier(0); }
Barrier::Barrier(int64_t value) { this->s = czsf_barrier(value); }
void Barrier::wait() { czsf_wait(&this->s); }
void Barrier::signal() { czsf_signal(&this->s); }

Semaphore::Semaphore() { this->s = czsf_semaphore(0); }
Semaphore::Semaphore(int64_t value) { this->s = czsf_semaphore(value); }
void Semaphore::wait() { czsf_wait(&this->s); }
void Semaphore::signal() { czsf_signal(&this->s); }

czsf_task_decl_t taskDecl(void (*fn)())
{
	czsf_task_decl_t ret;
	ret.fn = reinterpret_cast<void(*)(void*)>(fn);
	ret.param = NULL;
	return ret;
}

void run(struct czsf_task_decl_t* decls, uint64_t count, struct czsf_sync_t* sync) { czsf_run_signal(decls, count, sync); }
void run(struct czsf_task_decl_t* decls, uint64_t count) { czsf_run_signal(decls, count, NULL); }
void run(void (*fn)(), struct czsf_sync_t* sync) { czsf_run_mono_signal((void (*)(void*))(fn), NULL, 0, 1, sync); }
void run(void (*fn)(), czsf::Sync* sync) { czsf_run_mono_signal((void (*)(void*))(fn), NULL, 0, 1, &sync->s); }
void run(void (*fn)()) { czsf_run_mono_signal((void (*)(void*))(fn), NULL, 0, 1, NULL); }

template <typename T>
T* get_fls()
{
	return reinterpret_cast<T*>(czsf_get_fls());
}

}
#endif

#endif // CZSF_IMPLEMENTATION_GUARD_
#endif // CZSF_IMPLEMENTATION