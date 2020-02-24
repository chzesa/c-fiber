#include "fiber.h"
#include <stddef.h>
#include <stdlib.h>

#define PUSHA	"pushq %%rbx \n\t"	\
		"pushq %%r12 \n\t"	\
		"pushq %%r13 \n\t"	\
		"pushq %%r14 \n\t"	\
		"pushq %%r15 \n\t"

#define POPA	"popq %%r15 \n\t"	\
		"popq %%r14 \n\t"	\
		"popq %%r13 \n\t"	\
		"popq %%r12 \n\t"	\
		"popq %%rbx"

#ifdef WIN32
	#define CZSF_THREAD_LOCAL __declspec(thread)
	#define CZSF_NOINLINE __declspec(noinline)
#else
	#include <threads.h>
	#define CZSF_THREAD_LOCAL thread_local
	#define CZSF_NOINLINE __attribute__((noinline))
#endif

// Initialization macros
#define CZSF_SPINLOCK_INIT { 0 }
#define CZSF_LIST_INIT { NULL, NULL }
#define CZSF_STACK_INIT { NULL }
// #define CZSF_FIBER_INIT { CZSF_FIBER_HEADER_INIT }

enum czsf_yield_kind
{
	CZSF_YIELD_ACQUIRE,
	CZSF_YIELD_BLOCK,
	CZSF_YIELD_RETURN
};

enum czsf_fiber_status
{
	CZSF_FIBER_STATUS_NEW,
	CZSF_FIBER_STATUS_ACTIVE,
	CZSF_FIBER_STATUS_BLOCKED,
	CZSF_FIBER_STATUS_DONE
};

// ########

void czsf_spinlock_acquire(struct czsf_spinlock_t* self)
{
	while(__sync_lock_test_and_set(&self->value, 1));
}

void czsf_spinlock_release(struct czsf_spinlock_t* self)
{
	__sync_lock_release(&self->value, 0);
}

// ########
struct czsf_fiber_t
{
	struct czsf_fiber_t* next;
	enum czsf_fiber_status status;
	uint64_t stack;
	uint64_t base;
	struct czsf_task_decl_t task;
	struct czsf_sync_t* sync;
	char* stack_space;
};

// ########
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

// ######## Storing previously allocated stack space
// Pointer to next item in ll stored as char* at head[0];
struct czsf_stack_t
{
	char* head;
};

void czsf_stack_push(struct czsf_stack_t* self, char* item)
{
	*((char**)(item)) = self->head;
	self->head = item;
}

char* czsf_stack_pop(struct czsf_stack_t* self)
{
	char* ret = self->head;

	if (ret != NULL)
	{
		self->head = *((char**)(ret));
	}

	return ret;
}

// ########
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

// ########

static struct czsf_list_t CZSF_GLOBAL_QUEUE = CZSF_LIST_INIT;
static struct czsf_spinlock_t CZSF_GLOBAL_LOCK = CZSF_SPINLOCK_INIT;
static CZSF_THREAD_LOCAL struct czsf_stack_t CZSF_ALLOCATED_STACK_SPACE = CZSF_STACK_INIT;

static CZSF_THREAD_LOCAL struct czsf_fiber_t* CZSF_EXEC_FIBER = NULL;
static CZSF_THREAD_LOCAL struct czsf_spinlock_t* CZSF_HELD_LOCK = NULL;

static CZSF_THREAD_LOCAL uint64_t CZSF_STACK = 0;
static CZSF_THREAD_LOCAL uint64_t CZSF_BASE = 0;
// ########

struct czsf_fiber_t* czsf_acquire_next_fiber()
{
	czsf_spinlock_acquire(&CZSF_GLOBAL_LOCK);
	struct czsf_fiber_t* fiber = czsf_list_pop_front(&CZSF_GLOBAL_QUEUE);
	czsf_spinlock_release(&CZSF_GLOBAL_LOCK);

	if (fiber == NULL)
	{
		return NULL;
	}

	if (fiber->status == CZSF_FIBER_STATUS_BLOCKED)
	{
		return fiber;
	}

	// assert not done, not active
	// fiber is new, grab stack space to use
	char* stack_space = czsf_stack_pop(&CZSF_ALLOCATED_STACK_SPACE);

	if (stack_space == NULL)
	{
		stack_space = (char*)(malloc(CZSF_STACK_SIZE));
	}

	fiber->stack = uint64_t(&stack_space[CZSF_STACK_SIZE]);
	fiber->base = fiber->stack;
	fiber->stack_space = stack_space;
	return fiber;
}

void __czsf_yield(enum czsf_yield_kind kind);

void czsf_exec_fiber()
{
	struct czsf_fiber_t* fiber = CZSF_EXEC_FIBER;
	fiber->task.fn(fiber->task.param);

	if (fiber->sync != NULL)
	{
		czsf_signal(fiber->sync);
	}

	fiber->status = CZSF_FIBER_STATUS_DONE;
	__czsf_yield(CZSF_YIELD_RETURN);
}

CZSF_NOINLINE void __czsf_yield(enum czsf_yield_kind kind)
{
	struct czsf_fiber_t* fiber = CZSF_EXEC_FIBER;
	uint64_t stack;
	uint64_t base;

	switch (kind)
	{
	case CZSF_YIELD_BLOCK:
		asm volatile
		(
			PUSHA
			"movq %%rsp, %0\n\t"
			"movq %%rbp, %1"
			:"=r" (stack)
			,"=r" (base)
		);

		fiber->stack = stack;
		fiber->base = base;
		czsf_spinlock_release(CZSF_HELD_LOCK);
	case CZSF_YIELD_RETURN:
		stack = CZSF_STACK;
		base = CZSF_BASE;
		asm volatile
		(
			"movq %0, %%rsp\n\t"
			"movq %1, %%rbp\n\t"
			POPA
			:
			:"r" (stack)
			,"r" (base)
		);

		switch(fiber->status)
		{
		case CZSF_FIBER_STATUS_DONE:
			czsf_stack_push(&CZSF_ALLOCATED_STACK_SPACE, fiber->stack_space);
			free(CZSF_EXEC_FIBER);
			break;
		case CZSF_FIBER_STATUS_BLOCKED:
			CZSF_HELD_LOCK = NULL;
			break;
		}

		CZSF_EXEC_FIBER = NULL;

	case CZSF_YIELD_ACQUIRE:
		fiber = czsf_acquire_next_fiber();
		if (fiber == NULL)
		{
			return;
		}

		asm volatile
		(
			PUSHA
			"movq %%rsp, %0\n\t"
			"movq %%rbp, %1"
			:"=r" (stack)
			,"=r" (base)
		);

		CZSF_STACK = stack;
		CZSF_BASE = base;

		stack = fiber->stack;
		base = fiber->base;

		CZSF_EXEC_FIBER = fiber;
		switch (fiber->status)
		{
		case CZSF_FIBER_STATUS_NEW:
			fiber->status = CZSF_FIBER_STATUS_ACTIVE;

			asm volatile
			(
				"movq %0, %%rsp\n\t"
				"movq %1, %%rbp"
				:
				:"r" (stack)
				,"r" (base)
			);

			czsf_exec_fiber();
			break;

		case CZSF_FIBER_STATUS_BLOCKED:
			fiber->status = CZSF_FIBER_STATUS_ACTIVE;

			asm volatile
			(
				"movq %0, %%rsp\n\t"
				"movq %1, %%rbp\n\t"
				POPA
				:
				:"r" (stack)
				,"r" (base)
			);
			break;
		}

		break;
	}
}

void czsf_yield()
{
	__czsf_yield(CZSF_YIELD_ACQUIRE);
}

// ########

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
				czsf_list_push_front(&CZSF_GLOBAL_QUEUE, head, tail);
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
			czsf_list_push_front(&CZSF_GLOBAL_QUEUE, head, head);
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
	__czsf_yield(CZSF_YIELD_BLOCK);
}

void czsf_run_signal(struct czsf_task_decl_t* decls, uint64_t count, struct czsf_sync_t* sync)
{
	if (count == 0)
	{
		return;
	}

	struct czsf_fiber_t* fibers[count];

	for (int i = 0; i < count; i++)
	{
		struct czsf_fiber_t* fiber = (struct czsf_fiber_t*)malloc(sizeof(czsf_fiber_t));
		fiber->status = CZSF_FIBER_STATUS_NEW;
		fiber->task = decls[i];
		fiber->sync = sync;
		fibers[i] = fiber;

		if (i > 0)
		{
			fibers[i - 1]->next = fiber;
		}
	}

	czsf_spinlock_acquire(&CZSF_GLOBAL_LOCK);
	czsf_list_push_back(&CZSF_GLOBAL_QUEUE, fibers[0], fibers[count - 1]);
	czsf_spinlock_release(&CZSF_GLOBAL_LOCK);
}

void czsf_run(struct czsf_task_decl_t* decls, uint64_t count)
{
	czsf_run_signal(decls, count, NULL);
}

void czsf_run_mono_signal(void (*fn)(void*), void* param, uint64_t param_size, uint64_t count, struct czsf_sync_t* sync)
{
	if (count == 0)
	{
		return;
	}

	struct czsf_fiber_t* fibers[count];

	for (int i = 0; i < count; i++)
	{
		struct czsf_fiber_t* fiber = (struct czsf_fiber_t*)malloc(sizeof(czsf_fiber_t));
		fiber->status = CZSF_FIBER_STATUS_NEW;
		fiber->task = czsf_task_decl(fn, (char*)(param) + i * param_size);
		fiber->sync = sync;
		fibers[i] = fiber;

		if (i > 0)
		{
			fibers[i - 1]->next = fiber;
		}
	}

	czsf_spinlock_acquire(&CZSF_GLOBAL_LOCK);
	czsf_list_push_back(&CZSF_GLOBAL_QUEUE, fibers[0], fibers[count - 1]);
	czsf_spinlock_release(&CZSF_GLOBAL_LOCK);
}

void czsf_run_mono(void (*fn)(void*), void* param, uint64_t param_size, uint64_t count)
{
	czsf_run_mono_signal(fn, param, param_size, count, NULL);
}

#ifdef __cplusplus
namespace czsf
{

czsf_task_decl_t taskDecl(void (*fn)())
{
	czsf_task_decl_t ret;
	ret.fn = reinterpret_cast<void(*)(void*)>(fn);
	ret.param = nullptr;
	return ret;
}

void run(struct czsf_task_decl_t* decls, uint64_t count, struct czsf_sync_t* sync) { czsf_run_signal(decls, count, sync); }
void run(struct czsf_task_decl_t* decls, uint64_t count) { czsf::run(decls, count, nullptr); }
void run(void (*fn)(), struct czsf_sync_t* sync) { czsf_run_mono_signal((void (*)(void*))(fn), nullptr, 0, 1, sync); }
void run(void (*fn)()) { czsf::run(fn, nullptr); }

}
#endif