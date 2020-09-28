#include "cross-platform.h"
#include "lfq.h"
#ifdef DEBUG
#include <assert.h>
#endif
#include <errno.h>
#define MAXFREE 150

static
int inHP(struct lfq_ctx *ctx, struct lfq_node * lfn) {
	for ( int i = 0 ; i < ctx->MAXHPSIZE ; i++ ) {
		//lmb(); // not needed, we don't care if loads reorder here, just that we check all the elements
		if (ctx->HP[i] == lfn)
			return 1;
	}
	return 0;
}

static
void enpool(struct lfq_ctx *ctx, struct lfq_node * lfn) {
	// add to tail of the free list
	lfn->free_next = NULL;
	volatile struct lfq_node *old_tail = XCHG(&ctx->fpt, lfn);  // seq_cst
	old_tail->free_next = lfn;

	// getting nodes out of this will have exactly the same deallocation problem
	// as the main queue.
	// TODO: a stack might be easier to manage, but would increase contention.

/*
	volatile struct lfq_node * p;
	do {
		p = ctx->fpt;
	} while(!CAS(&ctx->fpt, p, lfn));  // exchange using CAS
	p->free_next = lfn;
*/
}

static
void free_pool(struct lfq_ctx *ctx, bool freeall ) {
	if (!CAS(&ctx->is_freeing, 0, 1))
		return; // this pool free is not support multithreading.
	volatile struct lfq_node * p;

	for ( int i = 0 ; i < MAXFREE || freeall ; i++ ) {
		p = ctx->fph;
		if ( (!p->can_free) || (!p->free_next) || inHP(ctx, (struct lfq_node *)p) )
			goto exit;
		ctx->fph = p->free_next;
		free((void *)p);
	}
exit:
	ctx->is_freeing = false;
	smb();
}

static
void safe_free(struct lfq_ctx *ctx, struct lfq_node * lfn) {
	if (lfn->can_free && !inHP(ctx,lfn)) {
		// free is not thread-safe
		if (CAS(&ctx->is_freeing, 0, 1)) {
			lfn->next = (void*)-1;    // poison the pointer to detect use-after-free
			free(lfn);    // we got the lock; actually free
			ctx->is_freeing = false;
			smb();
		} else               // we didn't get the lock; only add to a freelist
			enpool(ctx, lfn);
	} else
		enpool(ctx, lfn);
	free_pool(ctx, false);
}

static
int alloc_tid(struct lfq_ctx *ctx) {
	for (int i = 0; i < ctx->MAXHPSIZE; i++) 
		if (ctx->tid_map[i] == 0) 
			if (CAS(&ctx->tid_map[i], 0, 1))
				return i;

	return -1;
}

static
void free_tid(struct lfq_ctx *ctx, int tid) {
	ctx->tid_map[tid]=0;
}

int lfq_init(struct lfq_ctx *ctx, int max_consume_thread) {
	struct lfq_node * tmpnode = calloc(1,sizeof(struct lfq_node));
	if (!tmpnode) 
		return -errno;
		
	struct lfq_node * free_pool_node = calloc(1,sizeof(struct lfq_node));
	if (!free_pool_node) 
		return -errno;
		
	tmpnode->can_free = free_pool_node->can_free = true;
	memset(ctx, 0, sizeof(struct lfq_ctx));
	ctx->MAXHPSIZE = max_consume_thread;
	ctx->HP = calloc(max_consume_thread,sizeof(struct lfq_node));
	ctx->tid_map = calloc(max_consume_thread,sizeof(struct lfq_node));
	ctx->head = ctx->tail=tmpnode;
	ctx->fph = ctx->fpt=free_pool_node;
	
	return 0;
}


long lfg_count_freelist(const struct lfq_ctx *ctx) {
	long count=0;
	struct lfq_node *p = (struct lfq_node *)ctx->fph; // non-volatile
	while(p) {
		count++;
		p = p->free_next;
	}
	
	return count;
}

int lfq_clean(struct lfq_ctx *ctx){
	if ( ctx->tail && ctx->head ) { // if have data in queue
		struct lfq_node *tmp;
		while ( (struct lfq_node *) ctx->head ) { // while still have node
			tmp = (struct lfq_node *) ctx->head->next;
			safe_free(ctx, (struct lfq_node *)ctx->head);
			ctx->head = tmp;
		}
		ctx->tail = 0;
	}
	if ( ctx->fph && ctx->fpt ) {
		free_pool(ctx, true);
		if ( ctx->fph != ctx->fpt )
			return -1;
		free((void *)ctx->fpt); // free the empty node
		ctx->fph=ctx->fpt=0;
	}
	if ( !ctx->fph && !ctx->fpt ) {
		free((void *)ctx->HP);
		free((void *)ctx->tid_map);
		memset(ctx,0,sizeof(struct lfq_ctx));
	} else
		return -1;
		
	return 0;
}

int lfq_enqueue(struct lfq_ctx *ctx, void * data) {
	struct lfq_node * insert_node = calloc(1,sizeof(struct lfq_node));
	if (!insert_node)
		return -errno;
	insert_node->data=data;
//	mb();  // we've only written to "private" memory that other threads can't see.
	volatile struct lfq_node *old_tail;
#if 0
	do {
		old_tail = (struct lfq_node *) ctx->tail;
	} while(!CAS(&ctx->tail,old_tail,insert_node));
#else
	old_tail = XCHG(&ctx->tail, insert_node);
#endif
	// We've claimed our spot in the insertion order by modifying tail
	// we are the only inserting thread with a pointer to the old tail.

	// now we can make it part of the list by overwriting the NULL pointer in the old tail
	// This is safe whether or not other threads have updated ->next in our insert_node
#ifdef DEBUG
	assert(!(old_tail->next) && "old tail wasn't NULL");
#endif
	old_tail->next = insert_node;
	// TODO: could a consumer thread could have freed the old tail?  no because that would leave head=NULL

//	ATOMIC_ADD( &ctx->count, 1);
	return 0;
}

void * lfq_dequeue_tid(struct lfq_ctx *ctx, int tid ) {
	//int cn_runtimes = 0;
	volatile struct lfq_node *old_head, *new_head;
#if 1  // HP[tid] stuff is necessary for deallocation.  (but it's still not safe).
	do {
	retry:  // continue jumps to the bottom of the loop, and would attempt a CAS with uninitialized new_head
		old_head = ctx->head;
		ctx->HP[tid] = old_head;  // seq-cst store.  (better: use xchg instead of mov + mfence on x86)
		mb();

		if (old_head != ctx->head)  // another thread freed it before seeing our HP[tid] store
			goto retry;
		new_head = old_head->next;   // FIXME: crash with old_head=NULL during deallocation (tid=5)?  (main thread=25486, this=25489)
		if (new_head==0 /* || new_head != old_head->next*/ ){  // redoing the same load isn't useful
			ctx->HP[tid] = 0;
			return 0;  // never remove the last node
		}
#ifdef DEBUG
		assert(new_head != (void*)-1 && "read an already-freed node");
#endif
	} while( ! CAS(&ctx->head, old_head, new_head) );
#else  // without HP[] stuff
	do {
		old_head = ctx->head;
		//ctx->HP[tid] = old_head;
		new_head = old_head->next;
		//if (old_head != ctx->head) continue;
		if (!new_head) {
			// ctx->HP[tid] = 0;
			return 0;  // never remove the last node
		}
#ifdef DEBUG
		assert(new_head != (void*)-1 && "read an already-freed node");
#endif
	} while( !CAS(&ctx->head, old_head, new_head) );
#endif
//	mb();  // CAS is already a memory barrier, at least on x86.

	// we've atomically advanced head, and we're the thread that won the race to claim a node
	// We return the data from the *new* head.
	// The list starts off with a dummy node, so the current head is always a node that's already been read.

	ctx->HP[tid] = 0;
	void *ret = new_head->data;
	new_head->can_free = true;
//	ATOMIC_SUB( &ctx->count, 1 );

	//old_head->next = (void*)-1;  // done in safe-free in the actual free() path.  poison the pointer to detect use-after-free

	// we need to avoid freeing until other readers are definitely not going to load its ->next in the CAS loop
	safe_free(ctx, (struct lfq_node *)old_head);

	//free(old_head);
	return ret;
}

void * lfq_dequeue(struct lfq_ctx *ctx ) {
	//return lfq_dequeue_tid(ctx, 0);  // TODO: let this inline even in the shared library
// old version
	int tid = alloc_tid(ctx);
	if (tid==-1)
		return (void *)-1; // To many thread race

	void * ret = lfq_dequeue_tid(ctx, tid);
	free_tid(ctx, tid);
	return ret;
}
