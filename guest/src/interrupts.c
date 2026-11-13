#include "descriptors.h"
#include "interrupts.h"
#include "io.h"
#include "file.h"

#define BUFFER_PORT 0x510
#define STATUS_PORT 0x520
#define LOCAL_BUFFER_SIZE 256

static int writer_fd = -1;
static int reader_fd = -1;
static char local_buffer[LOCAL_BUFFER_SIZE];
static int finished = 0;

static struct idt_entry idt[IDT_ENTRIES];
static int mode_initialized = 0;
static int mode = -1;

/*
	"general-regs-only" sprečava GCC da emituje SSE instrukcije, koje su zabranjene unutar
	__attribute__((interrupt)) handlera
*/
static void __attribute__((interrupt, target("general-regs-only")))
irq0_handler(struct interrupt_frame *frame){
    if (!mode_initialized) {
        // ovde VM_EXIT
        mode = inb(0x510);
        mode_initialized = 1;

        // ovo je samo slanje hostu da vidi da smo dobro primili ulogu
        if (mode == 1) {
            const char *s = "WRITER\n";
            for (; *s; s++)
                outb(0xE9, *s);
        }
        else {
            const char *s = "READER\n";
            for (; *s; s++)
                outb(0xE9, *s);
        }
        return;
    }


    // writer 
    if (mode == 1) {

        if (finished)
            return;

        // ako je ovo prvi put da buffer salje nesto 
        if (writer_fd == -1) {
            writer_fd = open("shared.txt", O_RD);
            if (writer_fd < 0) {
                const char *s = "writer open failed\n";
                for (; *s; s++)
                    outb(0xE9, *s);
                finished = 1;
                return;
            }
        }

        // writer procita iz deljenog fajla 
        int count = read(writer_fd, local_buffer, LOCAL_BUFFER_SIZE);

        if (count < 0) {
            const char *s = "writer read failed\n";
            for (; *s; s++)
                outb(0xE9, *s);
            finished = 1;
            return;
        }

        if (count == 0) {
            // nula je kraj prenosa
            outl(BUFFER_PORT, 0);
            // host potvrdjuje da je dobio kraj prenosa
            inl(STATUS_PORT);
            close(writer_fd);
            writer_fd = -1;
            finished = 1;
            const char *s = "writer finished\n";
            for (; *s; s++)
                outb(0xE9, *s);

            return;
        }
        
        // Writer prvo salje broj bajtova.
        outl(BUFFER_PORT, (uint32_t)count);
        // a onda salje i same bajtove
        for (int i = 0; i < count; i++) {
            outb(BUFFER_PORT, (uint8_t)local_buffer[i]);
        }

        // hipervizor vraca koliko je procitao 
        uint32_t written = inl(STATUS_PORT);

        if (written != (uint32_t)count) {
            const char *s = "writter partial buffer\n";
            for (; *s; s++)
                outb(0xE9, *s);
        }
    }


    // reader 
    else {

        if (finished)
            return;

        // prvi put da reader cita, znaci pravi svoj izlazni fajl 
        if (reader_fd == -1) {
            reader_fd = open("received.txt", O_CREATE | O_RDWR);
            if (reader_fd < 0) {
                const char *s = "reader open failed\n";
                for (; *s; s++)
                    outb(0xE9, *s);
                return;
            }
        }

        // dobijamo broj bajtova za citanje iz buffera
        uint32_t count = inl(BUFFER_PORT);

        // ako je kraj prenosa, count = 0
        if (count == 0) {
            outl(STATUS_PORT, 0);
            if (reader_fd != -1) {
                close(reader_fd);
                reader_fd = -1;
            }
            finished = 1;
            const char *s = "reader finished\n";
            for (; *s; s++)
                outb(0xE9, *s);
            return;
        }

        // citamo podatke bajt po bajt
        for (uint32_t i = 0; i < count; i++) {
            local_buffer[i] = (char)inb(BUFFER_PORT);
        }
        // u received koji smo napravili upisujemo
        int written = write(reader_fd, local_buffer, (int)count);
        // javljamo hostu koliko smo uspeli da procitamo
        outl(STATUS_PORT, count);
        if (written != (int)count) {
            const char *s = "reader write failed\n";
            for (; *s; s++)
                outb(0xE9, *s);
        }

        uint8_t command = inb(0xE9);
        (void)command;
    }
}


static void __attribute__((interrupt, target("general-regs-only")))
irq33_handler(struct interrupt_frame *frame)
{
    finished = 1;

    const char *s = "reader interrupted\n";
    for (; *s; s++)
        outb(0xE9, *s);
}

static void set_idt_gate(unsigned n, void (*handler)(struct interrupt_frame *)){
	uint64_t addr = (uint64_t)(uintptr_t)handler;
	idt[n].offset_low  = addr & 0xFFFF;
	idt[n].selector    = 0x08;  /* 64-bit code segment */
	idt[n].ist         = 0;
	idt[n].type_attr   = 0x8E;  /* P=1, DPL=0, 64-bit interrupt gate */
	idt[n].offset_mid  = (addr >> 16) & 0xFFFF;
	idt[n].offset_high = (addr >> 32) & 0xFFFFFFFF;
	idt[n].reserved    = 0;
}

void init_idt(void){
	struct dt_ptr p;
	set_idt_gate(32, irq0_handler);
    set_idt_gate(33, irq33_handler);
	p.limit = sizeof(idt) - 1;
	p.base  = (uint64_t)(uintptr_t)idt;
	asm volatile("lidt %0" : : "m"(p) : "memory");
}
