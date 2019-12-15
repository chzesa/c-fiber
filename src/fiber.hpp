#pragma once

#include <atomic>

namespace czsfiber
{
struct Fiber;

struct TaskDecl
{
	TaskDecl();
	template<class T> TaskDecl(void (*fn)(T*), T* param)
	{
		m_task = reinterpret_cast<void(*)(void*)>(fn);
		m_param = param;
	}
	TaskDecl(void (*fn)())
	{
		m_task = reinterpret_cast<void(*)(void*)>(fn);
		m_param = nullptr;
	}
	void (*m_task)(void*);
	void* m_param;
};

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
	Fiber* m_head = nullptr;
	Fiber* m_tail = nullptr;
};

struct Semaphore : public Sync
{
	void signal();
	void wait();

	std::atomic_flag m_lock;
	int64_t m_value = 0;
	Fiber* m_head = nullptr;
	Fiber* m_tail = nullptr;
};

void yield();
void runTasks(TaskDecl* decl, uint64_t numTasks, Barrier** p_barrier);
}