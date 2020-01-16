#ifndef CZSF_IMPL
#define CZSF_IMPL

#include <stdint.h>

enum czsf_item_kind
{
	CZSF_FIBER,
	CZSF_TASK_DESC
};

enum czsf_yield_kind
{
	CZSF_YIELD_ACQUIRE,
	CZSF_YIELD_BLOCK,
	CZSF_YIELD_RETURN
};

enum czsf_sync_kind
{
	CZSF_SYNC_SEMAPHORE,
	CZSF_SYNC_BARRIER
};

struct czsf_task_decl_t
{
	void (*fn)(void*);
	void* param;
};

struct czsf_item_header_t
{
	struct czsf_item_header_t* next;
	enum czsf_item_kind kind;
};

struct czsf_list_t
{
	struct czsf_item_header_t* head;
	struct czsf_item_header_t* tail;
};

struct czsf_spinlock_t
{
	volatile uint32_t value;
};

struct czsf_sync_t
{
	enum czsf_sync_kind kind;
	int64_t value;
	struct czsf_spinlock_t lock;
	struct czsf_list_t queue;
};

struct czsf_task_decl_t czsf_task_decl(void (*fn)(void*), void* param);
struct czsf_task_decl_t czsf_task_decl2(void (*fn)());

void czsf_yield(enum czsf_yield_kind kind);
void czsf_signal(struct czsf_sync_t* self);

struct czsf_sync_t czsf_init_semaphore(int64_t value);
struct czsf_sync_t czsf_init_barrier(int64_t count);

void czsf_signal(struct czsf_sync_t* self);
void czsf_wait(struct czsf_sync_t* self);
void czsf_run(struct czsf_task_decl_t* decls, uint64_t count);

#endif