/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"
#include "intrinsic.h"
#include "userprog/process.h"





static bool file_backed_swap_in (struct page *page, void *kva);
static bool file_backed_swap_out (struct page *page);
static void file_backed_destroy (struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations file_ops = {
	.swap_in = file_backed_swap_in,
	.swap_out = file_backed_swap_out,
	.destroy = file_backed_destroy,
	.type = VM_FILE,
};

/* The initializer of file vm */
void
vm_file_init (void) {
	//TODO : file_init 구현
}

/* Initialize the file backed page */
bool
file_backed_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	page->operations = &file_ops;

	struct file_page *file_page = &page->file;
}

/* Swap in the page by read contents from the file. */
static bool
file_backed_swap_in (struct page *page, void *kva) {
	struct file_page *file_page UNUSED = &page->file;
	if(page ==NULL)
		return false;
	
	struct container *aux = (struct container *)page->uninit.aux;

	struct file *file = aux->file;
	off_t offset = aux->offset;
	size_t page_read_bytes = aux->page_read_bytes;
	size_t page_zero_bytes = PGSIZE - page_read_bytes;

	file_seek (file, offset);

	if (file_read (file, kva, page_read_bytes) != (int) page_read_bytes) {
		return false;
	}

	if(page_zero_bytes>0)
		memset (kva + page_read_bytes, 0, page_zero_bytes);

	return true;
}

/* Swap out the page by writeback contents to the file. */
static bool
file_backed_swap_out (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;

	if(page ==NULL)
		return false;
	struct container * aux = (struct container *) page->uninit.aux;

	/*페이지의 수정여부를 확인하고 수정 되었다면 파일에 수정내용 기입*/
	if(pml4_is_dirty(thread_current()->pml4, page->va)){
		file_write_at(aux->file, page->va, aux->page_read_bytes, aux->offset);
		pml4_set_dirty(thread_current()->pml4, page->va, 0);
	}
	/*pml4 갱신*/
	pml4_clear_page(thread_current()->pml4, page->va);
}

/* Destory the file backed page. PAGE will be freed by the caller. */
static void
file_backed_destroy (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
}
static bool
lazy_load_segment (struct page *page, void *aux) {
  /* TODO: file의 Segment를 Load한다 */
  struct file *file = ((struct box *)aux)->file;
  off_t offsetof = (((struct box *)aux))->ofs;
  size_t page_read_bytes = ((struct box *)aux)->page_read_bytes;
  size_t page_zero_bytes = PGSIZE - page_read_bytes;

  file_seek(file, offsetof);

  if(file_read(file, page->frame->kva, page_read_bytes) != (int)page_read_bytes){
    palloc_free_page(page->frame->kva);
    return false;
  }
  /* TODO: 이 함수는 가상메모리에서 첫 page fault가 발생했을 때 call한다. */
  memset(page->frame->kva + page_read_bytes, 0, page_zero_bytes);
  /* TODO: 가상 메모리는 이 함수를 call 했을 때 사용 가능 */

  return true;
}
/* Do the mmap */
void *
do_mmap (void *addr, size_t length, int writable,
		struct file *file, off_t offset) {
	struct file *mfile = file_reopen(file);

	void * start_addr = addr;
	size_t read_bytes = length > file_length(file) ? file_length(file) : length;
	size_t zero_bytes = PGSIZE - read_bytes % PGSIZE;

	/*파일을 페이지 단위로 잘라 해당 파일의 정보들을 container 구조체에 저장한다. 
	FILE-BACKED 타입의 UNINIT 페이지를 만들어 lazy_load_segment()를 vm_init으로 넣는다.*/

	while (read_bytes > 0 || zero_bytes > 0) {
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		struct container *container = (struct container*)malloc(sizeof(struct container));
		container->file = mfile;
		container->offset = offset;
		container->page_read_bytes = page_read_bytes;

		if (!vm_alloc_page_with_initializer (VM_FILE, addr, writable, lazy_load_segment, container)) {
			return NULL;
		}
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		addr += PGSIZE;
		offset += page_read_bytes;
	}
	return start_addr;
}

/* Do the munmap */
void
do_munmap (void *addr) {
	while (true) {
        struct page* page = spt_find_page(&thread_current()->spt, addr);
        
        if (page == NULL)
            break;

        struct container * aux = (struct container *) page->uninit.aux;
        
        // dirty(사용되었던) bit 체크
        if(pml4_is_dirty(thread_current()->pml4, page->va)) {
            file_write_at(aux->file, addr, aux->page_read_bytes, aux->offset);
            pml4_set_dirty (thread_current()->pml4, page->va, 0);
        }

        pml4_clear_page(thread_current()->pml4, page->va);
        addr += PGSIZE;
    }
}
