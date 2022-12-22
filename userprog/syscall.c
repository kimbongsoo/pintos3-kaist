#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "intrinsic.h"
#include "userprog/process.h"
#include "kernel/stdio.h"
#include "threads/palloc.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "user/syscall.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR         0xc0000081 /* Segment selector msr */
#define MSR_LSTAR        0xc0000082 /* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */
#define STDIN_FILENO     0
#define STDOUT_FILENO    1

void
syscall_init (void) {
  write_msr (MSR_STAR, ((uint64_t) SEL_UCSEG - 0x10) << 48 |
                           ((uint64_t) SEL_KCSEG) << 32);
  write_msr (MSR_LSTAR, (uint64_t) syscall_entry);

  /* The interrupt service rountine should not serve any interrupts
   * until the syscall_entry swaps the userland stack to the kernel
   * mode stack. Therefore, we masked the FLAG_FL. */
  write_msr (MSR_SYSCALL_MASK,
             FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);

  lock_init (&filesys_lock);
}

struct page* check_address(void *addr){
	// 주소 addr이 유저 가상 주소가 아니거나 pml4에 없으면 프로세스 종료
	if (addr == NULL || !is_user_vaddr(addr)) exit_handler(-1);
	
	// 유저 가상 주소면 SPT에서 페이지 찾아서 리턴
	return spt_find_page(&thread_current()->spt, addr);
}

void check_valid_buffer(void* buffer, unsigned size, void* rsp, bool to_write) {
    for (int i = 0; i < size; i++) {
        struct page* page = check_address(buffer + i);
        // 인자로 받은 buffer부터 buffer + size까지의 크기가 한 페이지의 크기를 넘을수도 있음
        if(page == NULL)
            exit_handler(-1);
        if(to_write == true && page->writable == false)
            exit_handler(-1);
    }
}

void
syscall_handler (struct intr_frame *f UNUSED) {

  int syscall_no = f->R.rax;

  uint64_t a1 = f->R.rdi;
  uint64_t a2 = f->R.rsi;
  uint64_t a3 = f->R.rdx;
  uint64_t a4 = f->R.r10;
  uint64_t a5 = f->R.r8;
  uint64_t a6 = f->R.r9;

  // SCW_dump_frame (f);
  switch (syscall_no) {
  case SYS_HALT:
    halt_handler ();
    break;
  case SYS_EXIT:
    exit_handler (a1);
    break;
  case SYS_FORK:
    f->R.rax = fork_handler (a1, f);
    break;
  case SYS_EXEC:
    f->R.rax = exec_handler (a1);
    break;
  case SYS_WAIT:
    f->R.rax = wait_handler (a1);
    break;
  case SYS_CREATE:
    f->R.rax = create_handler (a1, a2);
    break;
  case SYS_REMOVE:
    f->R.rax = remove_handler (a1);
    break;
  case SYS_OPEN:
    f->R.rax = open_handler (a1);
    break;
  case SYS_FILESIZE:
    f->R.rax = file_size_handler (a1);
    break;
  case SYS_READ:
    check_valid_buffer(f->R.rsi, f->R.rdx, f->rsp, 1);
    f->R.rax = read_handler (a1, a2, a3);
    break;
  case SYS_WRITE:
  check_valid_buffer(f->R.rsi, f->R.rdx, f->rsp, 0);
    f->R.rax = write_handler (a1, a2, a3);
    break;
  case SYS_SEEK:
    seek_handler (a1, a2);
    break;
  case SYS_TELL:
    f->R.rax = tell_handler (a1);
    break;
  case SYS_CLOSE:
    close_handler (a1);
    break;
  case SYS_MMAP:
    f->R.rax = mmap(f->R.rdi, f->R.rsi, f->R.rdx, f->R.r10, f->R.r8);
    break;
  case SYS_MUNMAP:
    munmap(f->R.rdi);
    break;

  default:
    exit_handler (-1);
    break;
  }
}

/* 포인터가 가리키는 주소가 user영역에 유요한 주소인지 확인*/
/*
is_user_vaddr : 유저 가상주소 check
add == NULL : 들어온 주소가 NULL 인지 확인
pml4_get_page : 들어온 주소가 유자 가상주소 안에 할당된 페이지의 pointer인지 확인 
-->유저 영역 내이면서도 그 안에 할당된 페이지 안에 있어야 한다
*/

static struct file *
find_file_using_fd (int fd) {
  struct thread *cur = thread_current ();

  if (fd < 0 || fd >= FD_COUNT_LIMT)
    return NULL;

  return cur->fd_table[fd];
}

void
halt_handler (void) {
  power_off ();
}

void
exit_handler (int status) {
  struct thread *cur = thread_current ();
  cur->exit_status = status;
  printf ("%s: exit(%d)\n", cur->name, status);
  thread_exit ();
}

tid_t
fork_handler (const char *thread_name, struct intr_frame *f) {
  return process_fork (thread_name, f);
}

int
exec_handler (const char *file) {
  check_address (file);
  char *file_name_copy = palloc_get_page (PAL_ZERO);

  if (file_name_copy == NULL)
    exit_handler (-1);

  strlcpy (file_name_copy, file, strlen (file) + 1);

  if (process_exec (file_name_copy) == -1)
    return -1;

  NOT_REACHED ();
  return 0;
}

int
wait_handler (tid_t pid) {
  return process_wait (pid);
}

bool
create_handler (const char *file, unsigned initial_size) {

  check_address (file);
  return filesys_create (file, initial_size);
}

bool
remove_handler (const char *file) {
  check_address (file);
  return (filesys_remove (file));
}

int
open_handler (const char *file) {
  check_address (file);
  struct file *file_st = filesys_open (file);
  if (file_st == NULL) {
    return -1;
  }

  int fd_idx = add_file_to_FDT (file_st);

  if (fd_idx == -1) {
    file_close (file_st);
  }

  return fd_idx;
}

int
add_file_to_FDT (struct file *file) {
  struct thread *cur = thread_current ();
  struct file **fdt = cur->fd_table;
  int fd_index = cur->fd_idx;

  while (fdt[fd_index] != NULL && fd_index < FD_COUNT_LIMT) {
    fd_index++;
  }

  if (fd_index >= FD_COUNT_LIMT) {
    return -1;
  }

  cur->fd_idx = fd_index;
  fdt[fd_index] = file;
  return fd_index;
}

int
file_size_handler (int fd) {
  struct file *file_ = find_file_using_fd (fd);

  if (file_ == NULL)
    return -1;

  return file_length (file_);
}

int
read_handler (int fd, const void *buffer, unsigned size) {
  check_address (buffer);
  int read_result;
  struct file *file_obj = find_file_using_fd (fd);

  if (file_obj == NULL)
    return -1;

  if (fd == STDIN_FILENO) {
    char word;
    for (read_result = 0; read_result < size; read_result++) {
      word = input_getc ();
      if (word == "\0")
        break;
    }
  } else if (fd == STDOUT_FILENO) {
    return -1;
  } else {
    lock_acquire (&filesys_lock);
    read_result = file_read (file_obj, buffer, size);
    lock_release (&filesys_lock);
  }
  return read_result;
}

int
write_handler (int fd, const void *buffer, unsigned size) {
  check_address (buffer);
  struct file *file_obj = find_file_using_fd (fd);
  if (fd == STDIN_FILENO)
    return 0;

  if (fd == STDOUT_FILENO) {
    putbuf (buffer, size);
    return size;
  } else {
    if (file_obj == NULL)
      return 0;
    lock_acquire (&filesys_lock);
    off_t write_result = file_write (file_obj, buffer, size);
    lock_release (&filesys_lock);
    return write_result;
  }
}

void
seek_handler (int fd, unsigned position) {
  struct file *file_obj = find_file_using_fd (fd);

  file_seek (file_obj, position);
}

unsigned
tell_handler (int fd) {
  if (fd <= 2)
    return;
  struct file *file_obj = find_file_using_fd (fd);
  check_address (file_obj);
  if (file_obj == NULL)
    return;

  return file_tell (file_obj);
}

void
close_handler (int fd) {
  struct file *file_obj = find_file_using_fd (fd);
  if (file_obj == NULL)
    return;

  if (fd < 0 || fd >= FD_COUNT_LIMT)
    return;
  thread_current ()->fd_table[fd] = NULL;
  palloc_free_page(thread_current ()->fd_table[fd]);

  lock_acquire (&filesys_lock);
  file_close (file_obj);
  lock_release (&filesys_lock);
}
struct file *process_get_file(int fd) {
	struct thread *curr = thread_current();
	struct file* fd_file = curr->fd_table[fd];

	if(fd_file)
		return fd_file;
	else
		return	NULL;
}

// void *mmap (void *addr, size_t length, int writable, int fd, off_t offset){
//   struct file *file = process_get_file(fd);

// 	if (file == NULL)
// 		return NULL;
	
// 	/* 파일의 시작점도 페이지 정렬 */
// 	if (offset % PGSIZE != 0) {
//         return NULL;
//     }

// 	/*  It must fail if addr is not page-aligned */
// 	if (pg_round_down(addr) != addr || is_kernel_vaddr(addr))
// 		return NULL;

// 	/*  if the range of pages mapped overlaps any existing set of mapped pages */
// 	if (spt_find_page(&thread_current()->spt, addr))
// 		return NULL;

// 	/* addr가 NULL(0), 파일의 길이가 0*/
// 	if (addr == NULL || (long long)length == 0)
// 		return NULL;
	
// 	/* file descriptors representing console input and output are not mappable */
// 	if (fd == 0 || fd == 1)
// 		exit_handler(-1);
	
// 	return do_mmap(addr, length, writable, file, offset);
// }

// void *mmap(void *addr, size_t length, int writable, int fd, off_t offset)
// {
// 	if (!addr || is_kernel_vaddr(addr) || pg_round_down(addr) != addr || (long long)length <= 0)
// 	{
// 		return NULL;
// 	}

// 	if (offset % PGSIZE)
// 	{
// 		return NULL;
// 	}

// 	if (fd == 0 || fd == 1)
// 	{
// 		return NULL;
// 	}

// 	if (addr == NULL)
// 	{
// 		return NULL;
// 	}

// 	if (spt_find_page(&thread_current()->spt, addr))
// 	{
// 		return NULL;
// 	}

// 	struct file *file = find_file_using_fd(fd);

// 	if (file == NULL)
// 	{
// 		return NULL;
// 	}
//   lock_acquire(&filesys_lock);
// 	file = file_reopen(file);
//   lock_release(&filesys_lock);
  

// 	if (file == NULL)
// 	{
// 		return NULL;
// 	}
//   lock_acquire(&filesys_lock);
// 	size_t length_result = file_length(file);
//   lock_release(&filesys_lock);

// 	return do_mmap(addr, length_result, writable, file, offset);
// }

void *mmap (void *addr, size_t length, int writable, int fd, off_t offset) {

    if (offset % PGSIZE != 0) {
        return NULL;
    }
		/* addr은 페이지 시작 주소이며 유저 영역 안에 있어야 한다.*/
    if (pg_round_down(addr) != addr || is_kernel_vaddr(addr) || addr == NULL || (long long)length <= 0)
        return NULL;
    /* stdin, stdout은 제외한다.*/
    if (fd == 0 || fd == 1)
        exit_handler(-1);
    /* 기존에 매핑되어 있는 페이지면 안된다. */
    if (spt_find_page(&thread_current()->spt, addr))
        return NULL;

    struct file *target = process_get_file(fd);
		/* 존재하는 파일을 매핑해야 한다.*/
    if (target == NULL)
        return NULL;
		/* do_mmap 호출한다. */
    void * ret = do_mmap(addr, length, writable, target, offset);

    return ret;
}


void munmap (void *addr) {
    if (is_kernel_vaddr(addr) || (uint64_t)addr % PGSIZE || !addr){
        return;
    }

    do_munmap(addr);
}