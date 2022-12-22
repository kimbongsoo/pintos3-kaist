/* anon.c: Implementation of page for non-disk image (a.k.a. anonymous page). */

#include "vm/vm.h"
#include "devices/disk.h"
#include "intrinsic.h"
#include "lib/kernel/bitmap.h"

/* DO NOT MODIFY BELOW LINE */
static struct disk *swap_disk;
static bool anon_swap_in (struct page *page, void *kva);
static bool anon_swap_out (struct page *page);
static void anon_destroy (struct page *page);
struct bitmap *swap_table;
const size_t SECTORS_PER_PAGE = PGSIZE / DISK_SECTOR_SIZE;  // sectors / page
/* DO NOT MODIFY this struct */
static const struct page_operations anon_ops = {
	.swap_in = anon_swap_in,
	.swap_out = anon_swap_out,
	.destroy = anon_destroy,
	.type = VM_ANON,
};

/* Initialize the data for anonymous pages */
void
vm_anon_init (void) {
	/* TODO: swap_disk를 set_up */
	/*디스크를 swap용도로 사용 시 disk_get에 1,1을 인자로 넘겨줌 */
	swap_disk = disk_get(1, 1);
	/*디스크 사이즈에 몇개의 페이지가 할당되는지 계산*/
	size_t swap_size = disk_size(swap_disk) / SECTORS_PER_PAGE;
	/*페이지 수 만큼의 비트를 가진 비트맵 생성*/
	swap_table = bitmap_create(swap_size);
	//swap_disk = NULL;
}

/* Initialize the file mapping */
bool
anon_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	struct uninit_page *uninit = &page->uninit;
	memset(uninit, 0, sizeof(struct uninit_page));

	/* Set up the handler */
	/* 이제 해당 페이지는 ANON이므로 operations도 anon으로 지정한다. */
	page->operations = &anon_ops;

	struct anon_page *anon_page = &page->anon;
	anon_page->swap_index = -1;
	
	return true;
}

/* Swap in the page by read contents from the swap disk. */
static bool
anon_swap_in (struct page *page, void *kva) {
	struct anon_page *anon_page = &page->anon;
	int page_no = anon_page->swap_index;

	/*swap out 상태이므로 해당 페이지의 비트는 true가 되어야 한다.*/
	if (bitmap_test(swap_table, page_no) == false){
		return false;
	}
	/*페이지 내 섹터들에 대해 커널 가상 주소에 데이터 입력*/
	for (int i = 0; i <SECTORS_PER_PAGE; ++i){
		disk_read(swap_disk, page_no * SECTORS_PER_PAGE + i, kva + DISK_SECTOR_SIZE * i);
	}
	/*swap in 완료했으므로 해당 슬롯의 비트를 False로 갱신*/
	bitmap_set(swap_table, page_no, false);

	return true;
}

/* Swap out the page by writing contents to the swap disk. */
static bool
anon_swap_out (struct page *page) {
	struct anon_page *anon_page = &page->anon;

	/*비트맵을 처음부터 순회해 false 값을 가진 비트를 하나 찾는다.(페이지를 할당받을 수 있는 swap slot을 하나 찾는다)*/
	int page_no = bitmap_scan(swap_table, 0, 1, false);

	if(page_no == BITMAP_ERROR){
		return false;
	}

	/*한 페이지를 디스크에 써 주기 위해 SECTOR_PER_PAGE개의 섹터에 저장해야 한다
	이 때 디스크에 각 섹터의 크기 DISK_SECTOR_SIZE만큼 써 준다.*/
	for (int i = 0; i < SECTORS_PER_PAGE; ++i) {
		disk_write(swap_disk, page_no * SECTORS_PER_PAGE + i, page->va + DISK_SECTOR_SIZE * i);
	}

	/*swap table의 해당 페이지에 대한 swap slot의 비트를 TRUE로 바꿔주고, 해당 페이지의 PTE에서 Present Bit를 0으로 바꿔준다.
	이제 프로세스가 이 페이지에 접근하면 Page Fault가 뜬다.*/
	bitmap_set(swap_table, page_no, true);
	pml4_clear_page(thread_current()->pml4, page->va);

	/*페이지의 swap_index값을 이 페이지가 저장된 swap slot의 번호로 써준다*/
	anon_page->swap_index = page_no;

	return true;
}

/* Destroy the anonymous page. PAGE will be freed by the caller. */
static void
anon_destroy (struct page *page) {
	struct anon_page *anon_page = &page->anon;
}
