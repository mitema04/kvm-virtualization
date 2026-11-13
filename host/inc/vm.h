#ifndef VM_H
#define VM_H

#include <stddef.h>
#include <stdint.h>
#include <linux/kvm.h>

#define MEM_SIZE         (2u * 1024u * 1024u)
#define GUEST_START_ADDR 0x0000
#define GUEST_CODE_PAGES 16

#define IRQ_NUM   32
#define IRQ_COUNT 3

#define PDE64_PRESENT (1u << 0)
#define PDE64_RW      (1u << 1)
#define PDE64_USER    (1u << 2)
#define PDE64_PS      (1u << 7)

#define CR0_PE   (1u << 0)
#define CR0_PG   (1u << 31)
#define CR4_PAE  (1u << 5)
#define EFER_LME (1u << 8)
#define EFER_LMA (1u << 10)

#define MAX_FILE_NAME 256

// ovo nam je kao state machine jer preko out ne moze da se prosledi ceo sistemski poziv
#define FILE_IDLE        0
#define FILE_WAIT_FLAGS  1
#define FILE_WAIT_LEN    2
#define FILE_WAIT_PATH   3
#define FILE_WAIT_RESULT 4
#define FILE_WAIT_FD 5
#define FILE_WAIT_COUNT   6
#define FILE_WAIT_DATA    7
#define FILE_SEND_DATA    8
#define FILE_WAIT_OFFSET     9
#define FILE_WAIT_SEEK_FLAG 10


#define MAX_FILE_DATA 4096
#define MAX_OPEN_FILES 32
#define MAX_HOST_FILES 128
#define MAX_HOST_PATH 512


#define MODE_READER 0
#define MODE_WRITER 1

struct file_request_state {
    int state;   // dokle smo dosli
	int operation;  // koju operaciju radimo 
    int flags;
	int count;
	int data_index;
    int path_len;    // duzina path-a
    int path_index;  // dokle smo stigli sa citanjem path-a
	int result;
	int fd;
	int offset;
	int off_flag;
	char path[MAX_FILE_NAME];
	char data[MAX_FILE_DATA];
};


struct open_file {
    int used;
    int host_fd;
    int flags;
    char path[MAX_FILE_NAME];
	int is_shared;
	int has_private_copy;
};

struct host_file {
    int used;
    char name[MAX_FILE_NAME];
    char host_path[MAX_HOST_PATH];
    int owner_vm_id;
    int shared;
};

struct vm {
	int id;
	int kvm_fd;
	int vm_fd;
	int vcpu_fd;
	char *mem;
	size_t mem_size;
	struct kvm_run *run;
	int run_mmap_size;

	struct file_request_state file_req;
	struct open_file open_files[MAX_OPEN_FILES];

	int mode;
	// reader polja:
	uint32_t buffer_index;
	int buffer_done;	
	// writer polja:
	uint32_t buffer_write_index;
	int waiting_for_buffer_count;
	int waiting_for_read_count;
	int mode_sent;
	// ovo nam je za gutanje viska
	uint32_t writer_requested_count;
	uint32_t last_buffer_version;
};

int  vm_init(struct vm *v, size_t mem_size);
void vm_destroy(struct vm *v);
void setup_long_mode(struct vm *v, struct kvm_sregs *sregs, size_t page_size);
int  load_guest_image(struct vm *v, const char *image_path, uint64_t load_addr);
int  inject_irq(struct vm *v, unsigned int vector);

#endif 
