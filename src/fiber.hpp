#pragma once

#include <atomic>
#include <deque>
#include <vector>

namespace fiber
{
struct Fiber;

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

static std::atomic_flag queueLock = ATOMIC_FLAG_INIT;
static std::deque<Fiber*> fiberQueue;

static thread_local std::atomic_flag* HELD_LOCK = nullptr;
static thread_local Fiber* EXEC_FIBER = nullptr;
static thread_local uint64_t P_BASE = 0;
static thread_local uint64_t P_STACK = 0;

void acquireLock()
{
	while(queueLock.test_and_set(std::memory_order_relaxed));
}

void releaseLock()
{
	queueLock.clear();
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

void yield(YieldType);

struct TaskDecl
{
	TaskDecl()
	{
		m_task = nullptr;
		m_param = nullptr;
	}
	template<class T> TaskDecl(void (*fn)(T*), T* param);
	void (*m_task)(void*);
	void* m_param;
};

template<class T>
TaskDecl::TaskDecl(void (*fn)(T*), T* param)
{
	m_task = reinterpret_cast<void(*)(void*)>(fn);
	m_param = param;
}

struct Sync
{
	virtual void signal() = 0;
	virtual void wait() = 0;
};

struct Barrier : public Sync
{
	void signal();
	void wait();

	std::atomic_flag m_lock;
	uint64_t m_value;
	std::vector<Fiber*> m_waiters;
};

struct Fiber
{
	char m_stack[STACK_SIZE];

	uint64_t m_fiberStack;
	uint64_t m_fiberBase;

	void (*m_task)(void*);
	void* m_param;

	Sync* m_signal;
	FiberStatus m_status;
};

void Barrier::signal()
{
	while(m_lock.test_and_set(std::memory_order_relaxed));

	if (m_value > 0 && --m_value == 0)
	{
		acquireLock();

		for (uint i = 0; i < m_waiters.size(); i++)
		{
			fiberQueue.push_front(m_waiters[i]);
		}

		releaseLock();
		m_waiters.clear();
	}

	m_lock.clear();
}

void Barrier::wait()
{
	Fiber* fiber = EXEC_FIBER;
	while(m_lock.test_and_set(std::memory_order_relaxed));

	if (m_value == 0)
	{
		m_lock.clear();
		return;
	}

	fiber->m_status = FiberStatus::Blocked;

	m_waiters.push_back(fiber);
	HELD_LOCK = &m_lock;
	yield(YieldType::Block);
}

struct Semaphore : public Sync
{
	void signal();
	void wait();

	std::atomic_flag m_lock;
	int64_t m_value;
	std::deque<Fiber*> m_waiters;
};

void Semaphore::signal()
{
	while(m_lock.test_and_set(std::memory_order_relaxed));

	if (m_waiters.size() > 0)
	{
		auto fiber = m_waiters.front();
		m_waiters.pop_front();

		acquireLock();
		fiberQueue.push_front(fiber);
		releaseLock();
	} else {
		m_value++;
	}

	m_lock.clear();
}

void Semaphore::wait()
{
	while(m_lock.test_and_set(std::memory_order_relaxed));

	if (m_value > 0)
	{
		m_value--;
		m_lock.clear();
		return;
	}

	Fiber* fiber = EXEC_FIBER;
	fiber->m_status = FiberStatus::Blocked;

	m_waiters.push_back(fiber);
	HELD_LOCK = &m_lock;
	yield(YieldType::Block);
}

Fiber* acquireNext()
{
	acquireLock();
	Fiber* fiber = nullptr;
	if (fiberQueue.size() > 0)
	{
		fiber = fiberQueue.front();
		fiberQueue.pop_front(); 
	}

	releaseLock();
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
		HELD_LOCK->clear();

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
			delete EXEC_FIBER;
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

	Fiber* fibers[numTasks];

	for (uint i = 0; i < numTasks; i++)
	{
		Fiber* fiber = new Fiber;

		fiber->m_fiberStack = (uint64_t) &(fiber->m_fiberStack);
		fiber->m_fiberBase = fiber->m_fiberStack;

		fiber->m_task = decl[i].m_task;
		fiber->m_param = decl[i].m_param;
		fiber->m_signal = barrier;
		fiber->m_status = FiberStatus::New;

		fibers[i] = fiber;
	}

	acquireLock();

	for (uint i = 0; i < numTasks; i++)
	{
		fiberQueue.push_back(fibers[i]);
	}

	releaseLock();
}

}