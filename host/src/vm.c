#include "vm.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

int vm_init(struct vm *v, size_t mem_size)
{
	// ovaj struct je predefinisan
	struct kvm_userspace_memory_region region;

	// sve na nule 
	memset(v, 0, sizeof(*v));
	v->kvm_fd = v->vm_fd = v->vcpu_fd = -1;
	v->mem = MAP_FAILED;
	v->run = MAP_FAILED;
	v->run_mmap_size = 0;
	v->mem_size = mem_size;

	v->file_req.state = FILE_IDLE;
	v->file_req.path[0] = '\0';
	v->file_req.result = -1;
	v->file_req.fd = -1;

	v->buffer_index = 0;
	v->buffer_done = 0;
	v->buffer_write_index = 0;
	v->waiting_for_buffer_count = 1;
	v->writer_requested_count = 0;
	v->waiting_for_read_count = 1;

	v->waiting_for_command = 0;

	v->mode_sent = 0;
	v->stop_irq_sent = 0;
	
	v->last_buffer_version = 0;
	

	for (int i = 0; i < MAX_OPEN_FILES; i++) {
		v->open_files[i].used = 0;
		v->open_files[i].host_fd = -1;
		v->open_files[i].flags = 0;
		v->open_files[i].path[0] = '\0';
		v->open_files[i].is_shared = 0;
		v->open_files[i].has_private_copy = 0;
	}


	// pocetak koriscenja kvm-a
	v->kvm_fd = open("/dev/kvm", O_RDWR);
	if (v->kvm_fd < 0) {
		perror("open /dev/kvm");
		return -1;
	}

	// proveravanje verzije za svaki slucaj?
	int api = ioctl(v->kvm_fd, KVM_GET_API_VERSION, 0);
	if (api != KVM_API_VERSION) {
		printf("KVM API mismatch: kernel=%d headers=%d\n", api, KVM_API_VERSION);
		return -1;
	}

	// pravimo nasu vm
	v->vm_fd = ioctl(v->kvm_fd, KVM_CREATE_VM, 0);
	if (v->vm_fd < 0) {
		perror("KVM_CREATE_VM");
		return -1;
	}

	// pravimo nas RAM
	v->mem = mmap(NULL, mem_size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (v->mem == MAP_FAILED) {
		perror("mmap mem");
		return -1;
	}

	// ovo uvek ovako 
	region.slot = 0;
	region.flags = 0;

	region.guest_phys_addr = 0; // da se koriste fizicke adrese 
	region.memory_size = v->mem_size;
	region.userspace_addr = (uintptr_t)v->mem; 


	// da se upise u onu region kvm predefinisanu strukturu sta radimo 
	if (ioctl(v->vm_fd, KVM_SET_USER_MEMORY_REGION, &region) < 0) {
		perror("KVM_SET_USER_MEMORY_REGION");
		return -1;
	}

	// pravljenje virtuelnog procesora
	v->vcpu_fd = ioctl(v->vm_fd, KVM_CREATE_VCPU, 0);
	if (v->vcpu_fd < 0) {
		perror("KVM_CREATE_VCPU");
		return -1;
	}

	// izdvajanje za kvm_run strukturu 
	v->run_mmap_size = ioctl(v->kvm_fd, KVM_GET_VCPU_MMAP_SIZE, 0);
	if (v->run_mmap_size <= 0) {
		perror("KVM_GET_VCPU_MMAP_SIZE");
		return -1;
	}

	// ovo je kvm_run struktura koja nam je vezana za nas vcpu koji smo napravili 
	// ovo ce nam biti bitno za gledanje razloga izlazaka itd.
	v->run = mmap(NULL, v->run_mmap_size, PROT_READ | PROT_WRITE, MAP_SHARED, v->vcpu_fd, 0);
	if (v->run == MAP_FAILED) {
		perror("mmap kvm_run");
		return -1;
	}

	return 0;
}


// radimo unmap svega sto smo mapirali, close svih descriptora
void vm_destroy(struct vm *v)
{
	if (v->run && v->run != MAP_FAILED) {
		munmap(v->run, (size_t)v->run_mmap_size);
		v->run = MAP_FAILED;
	}

	if (v->mem && v->mem != MAP_FAILED) {
		munmap(v->mem, v->mem_size);
		v->mem = MAP_FAILED;
	}

	if (v->vcpu_fd >= 0) {
		close(v->vcpu_fd);
		v->vcpu_fd = -1;
	}

	if (v->vm_fd >= 0) {
		close(v->vm_fd);
		v->vm_fd = -1;
	}

	if (v->kvm_fd >= 0) {
		close(v->kvm_fd);
		v->kvm_fd = -1;
	}
}


// namestanje segmenata za rad, kao sa vezbi
static void setup_segments_64(struct kvm_sregs *sregs){
	struct kvm_segment code = {
		.base    = 0,
		.limit   = 0xffffffff,
		.present = 1,
		.type    = 11,
		.dpl     = 0,
		.db      = 0,
		.s       = 1,
		.l       = 1,  
		.g       = 1,
	};
	struct kvm_segment data = code;
	data.type = 3;
	data.l    = 0;

	sregs->cs = code;
	sregs->ds = sregs->es = sregs->fs = sregs->gs = sregs->ss = data;
}


// namestanje long mode-a
void setup_long_mode(struct vm *v, struct kvm_sregs *sregs, size_t page_size)
{
	// pml4 tabela u poslednjih 4KB ram-a
	uint64_t pml4_addr = v->mem_size - 0x1000;
	uint64_t *pml4 = (void*)(v->mem + pml4_addr);

	uint64_t pdpt_addr = v->mem_size - 0x2000;
	uint64_t *pdpt = (void*)(v->mem + pdpt_addr);

	uint64_t pd_addr = v->mem_size - 0x3000;
	uint64_t *pd = (void*)(v->mem + pd_addr);

	pml4[0] = PDE64_PRESENT | PDE64_RW | PDE64_USER | pdpt_addr;
	pdpt[0] = PDE64_PRESENT | PDE64_RW | PDE64_USER | pd_addr;

	//PML4, PDPT, PD, PT, fizička stranica 4 KB
	if (page_size == 4 * 1024) {
		// jer jedna page table pokriva 512 * 4 KB
        size_t num_pts = v->mem_size / (2 * 1024 * 1024);
		for (size_t i = 0; i < num_pts; i++) {

			uint64_t pt_addr = v->mem_size - (0x4000 + i * 0x1000);
			uint64_t *pt = (void*)(v->mem + pt_addr);

			pd[i] = PDE64_PRESENT | PDE64_RW | PDE64_USER | pt_addr;
			    for (size_t j = 0; j < 512; j++) {
					// ovo je da bismo videli koja je ovo stranica po redu u celoj memoriji
					size_t page_index = i * 512 + j;
					// fizicka adresa stranice naravno mora da se pomnozi sa 4KB zbog njene velicine
					pt[j] = (page_index * 0x1000) | PDE64_PRESENT | PDE64_RW | PDE64_USER;
				}
		}
	}

	// PML4, PDPT, PD, fizička stranica 2 MB
	else {
		size_t num_pages = v->mem_size / page_size;
		for (size_t i = 0; i < num_pages; i++) {
			pd[i] = (i * page_size) | PDE64_PRESENT | PDE64_RW | PDE64_USER | PDE64_PS;
		}
	}

    sregs->cr3 = pml4_addr;
    sregs->cr4 = CR4_PAE;
    sregs->cr0 = CR0_PE | CR0_PG;
    sregs->efer = EFER_LME | EFER_LMA;

    setup_segments_64(sregs);
	
}



int load_guest_image(struct vm *v, const char *image_path, uint64_t load_addr){
	FILE *f = fopen(image_path, "rb");
	if (!f) {
		perror("Failed to open guest image");
		return -1;
	}

	// ovim na dalje proveravamo da li program staje u memoriju 
	if (fseek(f, 0, SEEK_END) < 0) {
		perror("Failed to seek to end of guest image");
		fclose(f);
		return -1;
	}

	long fsz = ftell(f);
	if (fsz < 0) {
		perror("Failed to get size of guest image");
		fclose(f);
		return -1;
	}
	rewind(f);

	if ((uint64_t)fsz > v->mem_size - load_addr) {
		printf("Guest image is too large for the VM memory\n");
		fclose(f);
		return -1;
	}

	// na kraju ucitamo ovde
	if (fread((uint8_t *)v->mem + load_addr, 1, (size_t)fsz, f) != (size_t)fsz) {
		perror("Failed to read guest image");
		fclose(f);
		return -1;
	}
	fclose(f);
	return 0;
}


// ubacuje se prekid virtuelnoj masini da joj se naznaci mod rada
int inject_irq(struct vm *v, unsigned int vector){
	struct kvm_interrupt irq = { .irq = vector };
	if (ioctl(v->vcpu_fd, KVM_INTERRUPT, &irq) < 0) {
		perror("KVM_INTERRUPT");
		return -1;
	}
	return 0;
}
