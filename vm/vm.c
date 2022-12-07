/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"
#include "lib/kernel/hash.h"
#include "threads/vaddr.h"
#include "userprog/process.h"

struct list_elem* start;

/* Initializes the virtual memory subsystem by invoking each subsystem's
 * intialize codes. */
void
vm_init (void) {
	vm_anon_init ();
	vm_file_init ();
#ifdef EFILESYS  /* For project 4 */
	pagecache_init ();
#endif
	register_inspect_intr ();
	/* DO NOT MODIFY UPPER LINES. */
	list_init(&frame_table);
}

/* Get the type of the page. This function is useful if you want to know the
 * type of the page after it will be initialized.
 * This function is fully implemented now. */
enum vm_type
page_get_type (struct page *page) {
	int ty = VM_TYPE (page->operations->type);
	switch (ty) {
		case VM_UNINIT:
			return VM_TYPE (page->uninit.type);
		default:
			return ty;
	}
}

/* Helpers */
static struct frame *vm_get_victim (void);
static bool vm_do_claim_page (struct page *page);
static struct frame *vm_evict_frame (void);

/* Create the pending page object with initializer. If you want to create a
 * page, do not create it directly and make it through this function or
 * `vm_alloc_page`. */
bool
vm_alloc_page_with_initializer (enum vm_type type, void *upage, bool writable,
		vm_initializer *init, void *aux) {

	ASSERT (VM_TYPE(type) != VM_UNINIT)
	struct supplemental_page_table *spt = &thread_current ()->spt;

	/* Check wheter the upage is already occupied or not. */
	if (spt_find_page (spt, upage) == NULL) {
		/* TODO: 페이지를 생성하고, VM type에 따라 initalier를 fetch한다,
		(initalier : page fault 시 uninit_initalizer가 호출되고 page type에 따라 호출)*/
		typedef bool (*initializeFunc)(struct page*, enum vm_type, void *);
		initializeFunc initializer = NULL;
		switch(VM_TYPE(type)){
			case VM_ANON:
				initializer = anon_initializer;
				break;
			case VM_FILE:
				initializer = file_backed_initializer;
				break;
		}
		// TODO: 그 후, uninit_new를 호출하여 uninit페이지 struct를 만든다. 
		struct page *new_page = malloc(sizeof(struct page));
		// TODO: 당신은 uninit_new를 call한 후 field를 수정해야 한다.
		uninit_new(new_page, upage, init, type, aux, initializer);
		new_page->writable = writable;
		/* TODO: page를 spt에 넣는다. */
		return spt_insert_page(spt,new_page);
	}
err:
	return false;
}

/* spt에서 Virtual Addr을 찾고 페이지를 리턴. 에러 발생시 NULL return*/
struct page *
spt_find_page (struct supplemental_page_table *spt UNUSED, void *va UNUSED) {
	struct page *page = (struct page*)malloc(sizeof(struct page)); 
	//malloc을 해야 thread name이 없어지지 않음
	struct hash_elem *e;

	page->va = pg_round_down(va);
	//해당 va가 속해있는 page 시작 주소를 가지는 Page를 return(page가 spt에 있어야 함!)
	e = hash_find(&spt->spt_hash, &page->hash_elem);
	free(page);

	return e != NULL ? hash_entry(e, struct page, hash_elem): NULL;
	/* e와 같은 hash값을 가지는 VA를 가지는 원소를 e에 해당하는 bucket list에서 찾아 return*/
}

bool insert_page(struct hash *pages, struct page *p){
    if(!hash_insert(pages, &p->hash_elem))
        return true;
    else
        return false;
}
bool delete_page(struct hash *pages, struct page *p){
    if(!hash_delete(pages, &p->hash_elem))
        return true;
    else
        return false;
}
/* Insert PAGE into spt with validation. */
bool
spt_insert_page (struct supplemental_page_table *spt UNUSED, struct page *page UNUSED) {
	return insert_page(&spt->spt_hash, page);
}

void
spt_remove_page (struct supplemental_page_table *spt, struct page *page) {
	vm_dealloc_page (page);
	return true;
}

/* Get the struct frame, that will be evicted. */
static struct frame *
vm_get_victim (void) {
	struct frame *victim = NULL;
    /* TODO: The policy for eviction is up to you. */
    // project 3
    struct thread *curr = thread_current();
    struct list_elem *e = start;

    for(start = e; start != list_end(&frame_table); start = list_next(start)){
        victim = list_entry(start, struct frame, frame_elem);
        if (pml4_is_accessed(curr->pml4, victim->page->va))
            pml4_set_accessed(curr->pml4, victim->page->va,0);
        else
            return victim;
    }
    for(start = list_begin(&frame_table); start != e; start = list_next(start)){
        victim = list_entry(start, struct frame, frame_elem);
        if(pml4_is_accessed(curr->pml4, victim->page->va))
            pml4_set_accessed(curr->pml4, victim->page->va, 0);
        else
            return victim;
    }

    return victim;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *
vm_evict_frame (void) {
	struct frame *victim = vm_get_victim ();

	/* TODO: victim을 swap out하고 evicted frame을 리턴하세요 */
	swap_out(victim->page);
    return victim;
}

/* palloc() and get frame. If there is no available page, evict the page
 * and return it. This always return valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.*/
static struct frame *
vm_get_frame (void) {
	// struct frame *frame = NULL;

	struct frame *frame = (struct frame*)malloc(sizeof(struct frame));

	frame->kva = palloc_get_page(PAL_USER); /* USER POOL에서 커널 가상 주소 공간으로 1page 할당 */
	
	/* if 프레임이 꽉 차서 할당받을 수 없다면 페이지 교체 실시
	   else 성공했다면 frame 구조체 커널 주소 멤버에 위에서 할당받은 메모리 커널 주소 넣기 */
    if(frame->kva == NULL)
    {
        frame = vm_evict_frame();
        frame->page = NULL;
        return frame;
    }
	list_push_back (&frame_table, &frame->frame_elem);
	frame->page = NULL;
	ASSERT (frame != NULL);
	ASSERT (frame->page == NULL);

	return frame;
}

/* Growing the stack. */
static void
vm_stack_growth (void *addr UNUSED) {
}

/* Handle the fault on write_protected page */
static bool
vm_handle_wp (struct page *page UNUSED) {
}

/* Return true on success */
bool
vm_try_handle_fault (struct intr_frame *f UNUSED, void *addr UNUSED,
		bool user UNUSED, bool write UNUSED, bool not_present UNUSED) {
	struct supplemental_page_table *spt UNUSED = &thread_current ()->spt;
	struct page *page = NULL;

	/* TODO: fault 여부 확인*/
	if(is_kernel_vaddr(addr))  return false;
	void *rsp_stack = is_kernel_vaddr(f->rsp) ? thread_current()->rsp_stack : f->rsp;

	if(not_present){
		if(!vm_claim_page(addr)) {
			if(rsp_stack - 8 <= addr && USER_STACK - 0x100000 <= addr && addr <= USER_STACK){
				vm_stack_growth(thread_current()->stack_bottom - PGSIZE);
				return true;
			}
			return false;
		}
		else 
			return true;
	}
	return false;
}

/* Free the page.
 * DO NOT MODIFY THIS FUNCTION. */
void
vm_dealloc_page (struct page *page) {
	destroy (page);
	free (page);
}

/* Claim the page that allocate on VA. */
bool
vm_claim_page (void *va UNUSED) {
	ASSERT(is_user_vaddr(va));

	struct page *page;
	//(+)
	page = spt_find_page(&thread_current()->spt, va);
	if(page == NULL) return false;

	return vm_do_claim_page (page);
}

/* Claim the PAGE and set up the mmu. */
static bool
vm_do_claim_page (struct page *page) {
	struct frame *frame = vm_get_frame();

	/* Set links */
	frame->page = page;
	page->frame = frame;
	
	/* TODO: page table 항목을 insert하여 page의 가상메모리를 프레임의 물리메모리에 mapping  */
	if (install_page(page->va, frame->kva, page->writable))
		return swap_in (page, frame->kva);

	return false;
}

/* Initialize new supplemental page table */
void
supplemental_page_table_init (struct supplemental_page_table *spt UNUSED) {
	hash_init(&spt->spt_hash, hash_func, less_func, NULL);
}

/* Copy supplemental page table from src to dst */
bool supplemental_page_table_copy (struct supplemental_page_table *dst UNUSED,
	struct supplemental_page_table *src UNUSED) {

	struct hash_iterator i;

	/* 1. SRC의 해시 테이블의 각 bucket 내 elem들을 모두 복사한다. */
	hash_first (&i, &src->spt_hash);
  while (hash_next (&i)) {	// src의 각각의 페이지를 반복문을 통해 복사
      struct page *parent_page = hash_entry (hash_cur (&i), struct page, hash_elem);   // 현재 해시 테이블의 element 리턴
      enum vm_type type = page_get_type(parent_page);		// 부모 페이지의 type
      void *upage = parent_page->va;				    		// 부모 페이지의 가상 주소
      bool writable = parent_page->writable;				// 부모 페이지의 쓰기 가능 여부
      vm_initializer *init = parent_page->uninit.init;	// 부모의 초기화되지 않은 페이지들 할당 위해 
      void* aux = parent_page->uninit.aux;

			// 부모 페이지가 STACK이라면 setup_stack()
      if (parent_page->uninit.type & VM_MARKER_0) { 
          setup_stack(&thread_current()->tf);
      }
			// 부모 타입이 uninit인 경우
      else if(parent_page->operations->type == VM_UNINIT) { 
          if(!vm_alloc_page_with_initializer(type, upage, writable, init, aux))
					// 자식 프로세스의 유저 메모리에 UNINIT 페이지를 하나 만들고 SPT 삽입.
              return false;
      }
			// STACK도 아니고 UNINIT도 아니면 vm_init 함수를 넣지 않은 상태에서 
      else {  
          if(!vm_alloc_page(type, upage, writable)) // uninit 페이지 만들고 SPT 삽입.
              return false;
          if(!vm_claim_page(upage))  // 바로 물리 메모리와 매핑하고 Initialize한다.
              return false;
      }

			// UNIT이 아닌 모든 페이지(stack 포함)에 대응하는 물리 메모리 데이터를 부모로부터 memcpy
      if (parent_page->operations->type != VM_UNINIT) { 
          struct page* child_page = spt_find_page(dst, upage);
          memcpy(child_page->frame->kva, parent_page->frame->kva, PGSIZE);
      }
  }
  return true;
}

void spt_destructor(struct hash_elem *e, void *aux)
{
	const struct page *p = hash_entry(e, struct page, hash_elem);
	free(p);
}

/* Free the resource hold by the supplemental page table */
void
supplemental_page_table_kill (struct supplemental_page_table *spt UNUSED) {
	/* TODO: thread에 의해 hold된 모든 spt를 제거하고
	 * TODO: 수정된 모든 내용을 storage에 다시 쓴다. */
	hash_destroy(&spt->spt_hash, spt_destructor);
}