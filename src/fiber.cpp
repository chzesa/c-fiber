#include "fiber.hpp"

namespace czsfiber
{

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

static const uint64_t STACK_SIZE = 1024 * 128;

static std::atomic_flag QUEUE_LOCK = ATOMIC_FLAG_INIT;
static Dummy* HEAD = nullptr;
static Dummy* TAIL = nullptr;

static thread_local Fiber* OLD_FIBERS = nullptr;

static thread_local std::atomic_flag* HELD_LOCK = nullptr;
static thread_local Fiber* EXEC_FIBER = nullptr;
static thread_local uint64_t P_BASE = 0;
static thread_local uint64_t P_STACK = 0;

Dummy* ll_pop_front(Dummy** ll_head, Dummy** ll_tail)
{
	Dummy* ret = *ll_head;
	if (ret != nullptr)
	{
		*ll_head = ret->next;
	}

	return ret;
}

void ll_push_back(Dummy** ll_head, Dummy** ll_tail, Dummy* head, Dummy* tail)
{
	tail->next = nullptr;
	if (*ll_head == nullptr)
	{
		*ll_head = head;
		*ll_tail = tail;
	}
	else
	{
		(*ll_tail)->next = head;
		*ll_tail = tail;
	}
}

void ll_push_front(Dummy** ll_head, Dummy** ll_tail, Dummy* head, Dummy* tail)
{
	if (*ll_head == nullptr)
	{
		*ll_tail = tail;
	}
	tail->next = *ll_head;
	*ll_head = head;
}

void acquire(std::atomic_flag* lock)
{
	while(lock->test_and_set(std::memory_order_acquire)) { }
}

void release(std::atomic_flag* lock)
{
	lock->clear(std::memory_order_release);
}

enum FiberStatus
{
	New,
	Active,
	Blocked,
	Done
};

enum YieldType
{
	Acquire,
	Block,
	Return
};

struct QueueTask : Dummy
{
	QueueTask(TaskDecl task, Sync* sync)
	{
		kind = QUEUE_TASK;
		this->task = task;
		this->sync = sync;
	}

	TaskDecl task;
	Sync* sync;
};

void yield(YieldType ty);
void yield() { yield(YieldType::Acquire); }

struct Fiber : Dummy
{
	Fiber()
	{
		kind = QUEUE_FIBER;
	}

	void set(void (*task)(void*), void* param, Sync* signal)
	{
		m_fiberStack = (uint64_t) &(m_fiberStack);
		m_fiberBase = m_fiberStack;

		m_task = task;
		m_param = param;
		m_signal = signal;
		m_status = FiberStatus::New;
	}

	char m_stack[STACK_SIZE];

	uint64_t m_fiberStack;
	uint64_t m_fiberBase;

	void (*m_task)(void*);
	void* m_param;

	Sync* m_signal;
	FiberStatus m_status;
};

TaskDecl::TaskDecl()
{
	m_task = nullptr;
	m_param = nullptr;
}

Barrier::Barrier()
{
	m_value = 0;
}

Barrier::Barrier(uint64_t value)
{
	m_value = value;
}

void Barrier::signal()
{
	acquire(&m_lock);
	if (m_value > 0 && --m_value == 0)
	{
		Dummy* head = m_head;
		Dummy* tail = m_tail;
		m_head = nullptr;
		release(&m_lock);

		if (head != nullptr)
		{
			acquire(&QUEUE_LOCK);
			ll_push_front(&HEAD, &TAIL, head, tail);
			release(&QUEUE_LOCK);
		}

	} else {
		release(&m_lock);
	}
}

void Barrier::wait()
{
	Fiber* fiber = EXEC_FIBER;
	fiber->next = nullptr;
	acquire(&m_lock);

	if (m_value == 0)
	{
		release(&m_lock);
		return;
	}

	fiber->m_status = FiberStatus::Blocked;
	ll_push_back(&m_head, &m_tail, fiber, fiber);

	HELD_LOCK = &m_lock;
	yield(YieldType::Block);
}

Semaphore::Semaphore()
{
	m_value = 0;
}

Semaphore::Semaphore(int64_t value)
{
	m_value = value;
}

void Semaphore::signal()
{
	acquire(&m_lock);
	Dummy* f = ll_pop_front(&m_head, &m_tail);

	if (f != nullptr)
	{
		release(&m_lock);

		acquire(&QUEUE_LOCK);
		ll_push_front(&HEAD, &TAIL, f, f);
		release(&QUEUE_LOCK);
	} else {
		m_value++;
		release(&m_lock);
	}
}

void Semaphore::wait()
{
	acquire(&m_lock);
	if (m_value > 0)
	{
		m_value--;
		release(&m_lock);
		return;
	}

	Fiber* fiber = EXEC_FIBER;
	fiber->m_status = FiberStatus::Blocked;

	ll_push_back(&m_head, &m_tail, fiber, fiber);
	HELD_LOCK = &m_lock;
	yield(YieldType::Block);
}

Fiber* acquireNext()
{
	acquire(&QUEUE_LOCK);
	Dummy* dummy = ll_pop_front(&HEAD, &TAIL);
	release(&QUEUE_LOCK);

	if (dummy == nullptr)
	{
		return nullptr;
	}

	if (dummy->kind == QUEUE_FIBER)
	{
		return reinterpret_cast<Fiber*>(dummy);
	}

	QueueTask* qt = reinterpret_cast<QueueTask*>(dummy);

	Fiber* fiber;
	if (OLD_FIBERS != nullptr)
	{
		fiber = OLD_FIBERS;
		OLD_FIBERS = reinterpret_cast<Fiber*>(fiber->next);
	} else {
		fiber = new Fiber();
	}

	fiber->set(qt->task.m_task, qt->task.m_param, qt->sync);
	delete qt;

	return fiber;
}

void execFiber()
{
	Fiber* fiber = EXEC_FIBER;
	fiber->m_task(fiber->m_param);
	if (fiber->m_signal != nullptr)
	{
		fiber->m_signal->signal();
	}
	fiber->m_status = FiberStatus::Done;
	yield(YieldType::Return);
}

void __attribute__((noinline)) yield(YieldType ty)
{
	Fiber* fiber;
	uint64_t p_stack;
	uint64_t p_base;

	switch (ty)
	{
	case YieldType::Block:
		fiber = EXEC_FIBER;
		asm volatile
		(
			PUSHA
			"movq %%rsp, %0\n\t"
			"movq %%rbp, %1"
			:"=r" (p_stack)
			,"=r" (p_base)
		);

		fiber->m_fiberStack = p_stack;
		fiber->m_fiberBase = p_base;
		release(HELD_LOCK);

	case YieldType::Return:
		p_stack = P_STACK;
		p_base = P_BASE;
		asm volatile
		(
			"movq %0, %%rsp\n\t"
			"movq %1, %%rbp\n\t"
			POPA
			:
			:"r" (p_stack)
			,"r" (p_base)
		);

		switch(EXEC_FIBER->m_status)
		{
		case FiberStatus::Done:
			fiber = EXEC_FIBER;
			fiber->next = OLD_FIBERS;
			OLD_FIBERS = fiber;
			break;
		case FiberStatus::Blocked:
			HELD_LOCK = nullptr;
			break;
		}

		EXEC_FIBER = nullptr;

	case YieldType::Acquire:
		fiber = acquireNext();
		if (fiber == nullptr)
		{
			return;
		}

		asm volatile
		(
			PUSHA
			"movq %%rsp, %0\n\t"
			"movq %%rbp, %1"
			:"=r" (p_stack)
			,"=r" (p_base)
		);

		P_STACK = p_stack;
		P_BASE = p_base;

		p_stack = fiber->m_fiberStack;
		p_base = fiber->m_fiberBase;

		EXEC_FIBER = fiber;

		switch (fiber->m_status)
		{
		case FiberStatus::New:
			fiber->m_status = FiberStatus::Active;

			asm volatile
			(
				"movq %0, %%rsp\n\t"
				"movq %1, %%rbp"
				:
				:"r" (p_stack)
				,"r" (p_base)
			);

			execFiber();
			break;

		case FiberStatus::Blocked:
			fiber->m_status = FiberStatus::Active;

			asm volatile
			(
				"movq %0, %%rsp\n\t"
				"movq %1, %%rbp\n\t"
				POPA
				:
				:"r" (p_stack)
				,"r" (p_base)
			);
			break;
		}

		break;
	}
}

void runTasks(TaskDecl* decl, uint64_t numTasks, Barrier** p_barrier)
{
	Barrier* barrier = nullptr;

	if (p_barrier != nullptr)
	{
		barrier = new Barrier;
		barrier->m_value = numTasks;
		(*p_barrier) = barrier;
	}

	if (numTasks == 0)
	{
		return;
	}

	Dummy* dummys[numTasks];
	for (uint64_t i = 0; i < numTasks; i++)
	{
		Dummy* d = new QueueTask(decl[i], barrier);
		dummys[i] = d;
		if (i > 0)
		{
			dummys[i - 1]->next = d;
		}
	}

	acquire(&QUEUE_LOCK);
	ll_push_back(&HEAD, &TAIL, dummys[0], dummys[numTasks - 1]);
	release(&QUEUE_LOCK);
}

}