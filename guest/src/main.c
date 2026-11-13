// #include "descriptors.h"
// #include "interrupts.h"
// #include "io.h"
// #include "file.h"

// static struct gdt_entry gdt[3];

// void
// __attribute__((noreturn))
// __attribute__((section(".start")))
// _start(void)
// {
// 	struct dt_ptr p;

// 	gdt[0] = (struct gdt_entry){ 0 };
// 	gdt[1] = (struct gdt_entry){  /* 64-bit code, selector 0x08: P=1, DPL=0, S=1, type=0xA, L=1, G=1 */
// 		.limit_low   = 0xFFFF,
// 		.access      = 0x9A,
// 		.flags_limit = 0xAF,
// 	};
// 	gdt[2] = (struct gdt_entry){  /* 64-bit data, selector 0x10: P=1, DPL=0, S=1, type=0x2, D/B=1, G=1 */
// 		.limit_low   = 0xFFFF,
// 		.access      = 0x92,
// 		.flags_limit = 0xCF,
// 	};

// 	p.limit = sizeof(gdt) - 1;
// 	p.base  = (uint64_t)(uintptr_t)gdt;
// 	asm volatile("lgdt %0" : : "m"(p) : "memory");

// 	/* Reload CS to 0x08 and continue at label 1 */
// 	asm volatile(
// 		"pushq $0x08\n\t"
// 		"lea 1f(%%rip), %%rax\n\t"
// 		"pushq %%rax\n\t"
// 		"lretq\n\t"
// 		"1:\n\t"
// 		::: "rax", "memory"
// 	);

// 	/* Reload data segment selectors to 0x10 */
// 	asm volatile(
// 		"movl $0x10, %%eax\n\t"
// 		"movw %%ax, %%ds\n\t"
// 		"movw %%ax, %%es\n\t"
// 		"movw %%ax, %%ss\n\t"
// 		::: "eax", "memory"
// 	);

// 	init_idt();
// 	asm volatile("sti");

// 	// const char *s;
// 	// for (s = "Hello, world!\n"; *s; ++s)
// 	// 	outb(0xE9, *s);


// 	// char c = inb(0xE9);
// 	// outb(0xE9, c);
// 	// outb(0xE9, '\n');	

// 	// for (;;)
// 	// 	asm volatile("hlt");

    
// 	// int fd = open("test.txt", O_CREATE | O_RDWR);

//     // if (fd == -1) {
//     //     const char *s = "open failed\n";
//     //     for (; *s; s++)
//     //         outb(0xE9, *s);

//     //     for (;;)
//     //         asm volatile("hlt");
//     // }

//     // char msg[] = "hello";

//     // int w = write(fd, msg, 5);

//     // if (w != 5) {
//     //     const char *s = "write failed\n";
//     //     for (; *s; s++)
//     //         outb(0xE9, *s);

//     //     for (;;)
//     //         asm volatile("hlt");
//     // }

//     // int pos = lseek(fd, 0, SEEK_SET);

//     // if (pos == -1) {
//     //     const char *s = "lseek failed\n";
//     //     for (; *s; s++)
//     //         outb(0xE9, *s);

//     //     for (;;)
//     //         asm volatile("hlt");
//     // }

//     // char buf[6];

//     // int r = read(fd, buf, 5);

//     // if (r != 5) {
//     //     const char *s = "read failed\n";
//     //     for (; *s; s++)
//     //         outb(0xE9, *s);

//     //     for (;;)
//     //         asm volatile("hlt");
//     // }

//     // buf[5] = '\0';

//     // for (int i = 0; i < 5; i++)
//     //     outb(0xE9, buf[i]);

//     // outb(0xE9, '\n');

//     // int c = close(fd);

//     // if (c == -1) {
//     //     const char *s = "close failed\n";
//     //     for (; *s; s++)
//     //         outb(0xE9, *s);
//     // }
//     // else {
//     //     const char *s = "all success\n";
//     //     for (; *s; s++)
//     //         outb(0xE9, *s);
//     // }

//     // for (;;)
//     //     asm volatile("hlt");
//     //     int fd = open("private.txt", O_RD);

//     // if (fd == -1) {
//     //     const char *s = "VM2 correctly rejected\n";
//     //     for (; *s; s++)
//     //         outb(0xE9, *s);
//     // }
//     // else {
//     //     const char *s = "ERROR: VM2 opened VM1 file\n";
//     //     for (; *s; s++)
//     //         outb(0xE9, *s);

//     //     close(fd);
//     // }

//     for (;;)
//         asm volatile("hlt");
// }





// #include <stdint.h>

// /*
// 	A1: 
// 	Proverava: serijski izlaz na port 0xE9 i uredno gašenje VM-a instrukcijom hlt.
// */

// static void outb(uint16_t port, uint8_t value)
// {
// 	asm("outb %0,%1" : /* empty */ : "a" (value), "Nd" (port) : "memory");
// }

// static void serial_putc(char c)
// {
// 	outb(0xE9, (uint8_t)c);
// }

// static void serial_puts(const char *s)
// {
// 	for (; *s; ++s)
// 		serial_putc(*s);
// }

// static void serial_put_int(long v)
// {
// 	char buf[24];
// 	int i = 0;
// 	int neg = v < 0;
// 	unsigned long u = neg ? (unsigned long)(-v) : (unsigned long)v;

// 	do {
// 		buf[i++] = '0' + (u % 10);
// 		u /= 10;
// 	} while (u);

// 	if (neg)
// 		serial_putc('-');
// 	while (i > 0)
// 		serial_putc(buf[--i]);
// }

// static __attribute__((noreturn)) void halt_forever(void)
// {
// 	for (;;)
// 		asm volatile("hlt");
// }

// void
// __attribute__((noreturn))
// __attribute__((section(".start")))
// _start(void)
// {
// 	serial_puts("A1: HELLO_OK\n");

// 	halt_forever();
// }






// // dodata fja gdt_boot koja objedinjuje ono osnovno iz originalnog main-a
// #include <stdint.h>
// #include "descriptors.h"
// #define ECHO_COUNT 5

// /*
// 	A2: proverava da hipervizor podržava čitanje (IN) sa porta 0xE9.
//     Gost cita ECHO_COUNT bajtova i odmah ih vraća nazad na isti port.

// 	Pokrenuti npr.: printf 'ABCDE' | ./hypervisor -m 2 -p 4 -g a2_echo.img
// */


// static struct gdt_entry gdt[3];

// static void gdt_boot(void)
// {
//     struct dt_ptr p;

//     gdt[0] = (struct gdt_entry){ 0 };

//     gdt[1] = (struct gdt_entry){
//         .limit_low = 0xFFFF,
//         .access = 0x9A,
//         .flags_limit = 0xAF,
//     };

//     gdt[2] = (struct gdt_entry){
//         .limit_low = 0xFFFF,
//         .access = 0x92,
//         .flags_limit = 0xCF,
//     };

//     p.limit = sizeof(gdt) - 1;
//     p.base = (uint64_t)(uintptr_t)gdt;

//     asm volatile("lgdt %0" : : "m"(p) : "memory");

//     asm volatile(
//         "pushq $0x08\n\t"
//         "lea 1f(%%rip), %%rax\n\t"
//         "pushq %%rax\n\t"
//         "lretq\n\t"
//         "1:\n\t"
//         ::: "rax", "memory"
//     );

//     asm volatile(
//         "movl $0x10, %%eax\n\t"
//         "movw %%ax, %%ds\n\t"
//         "movw %%ax, %%es\n\t"
//         "movw %%ax, %%ss\n\t"
//         ::: "eax", "memory"
//     );
// }

// static void outb(uint16_t port, uint8_t value)
// {
// 	asm("outb %0,%1" : /* empty */ : "a" (value), "Nd" (port) : "memory");
// }

// static void serial_putc(char c)
// {
// 	outb(0xE9, (uint8_t)c);
// }

// static void serial_puts(const char *s)
// {
// 	for (; *s; ++s)
// 		serial_putc(*s);
// }

// static inline uint8_t inb(uint16_t port)
// {
// 	uint8_t value;
// 	asm volatile("inb %1,%0" : "=a"(value) : "Nd"(port) : "memory");
// 	return value;
// }

// static __attribute__((noreturn)) void halt_forever(void)
// {
//     for (;;)
//         asm volatile("hlt");
// }

// void
// __attribute__((noreturn))
// __attribute__((section(".start")))
// _start(void)
// {
// 	gdt_boot();

// 	serial_puts("A2: START\n");

// 	for (int i = 0; i < ECHO_COUNT; ++i) {
// 		uint8_t c = inb(0xE9);
// 		serial_putc(c);
// 	}

// 	serial_puts("\nA2: DONE\n");

// 	halt_forever();
// }






// #include <stdint.h>

// /*
// 	B1: kreira fajl, upisuje u njega, zatvara ga, ponovo ga otvara za citanje
// 	i ispisuje procitani sadrzaj na serijski izlaz (0xE9).
// */

// #define O_RD     1
// #define O_WR     2
// #define O_CREATE 8

// static void outb(uint16_t port, uint8_t value)
// {
// 	asm("outb %0,%1" : /* empty */ : "a" (value), "Nd" (port) : "memory");
// }

// static void serial_putc(char c)
// {
// 	outb(0xE9, (uint8_t)c);
// }

// static void serial_puts(const char *s)
// {
// 	for (; *s; ++s)
// 		serial_putc(*s);
// }

// static void serial_put_int(long v)
// {
// 	char buf[24];
// 	int i = 0;
// 	int neg = v < 0;
// 	unsigned long u = neg ? (unsigned long)(-v) : (unsigned long)v;

// 	do {
// 		buf[i++] = '0' + (u % 10);
// 		u /= 10;
// 	} while (u);

// 	if (neg)
// 		serial_putc('-');
// 	while (i > 0)
// 		serial_putc(buf[--i]);
// }

// static __attribute__((noreturn)) void halt_forever(void)
// {
// 	for (;;)
// 		asm volatile("hlt");
// }

// extern int open(const char *path, int flags);
// extern int close(int fd);
// extern int read(int fd, char *buf, int count);
// extern int write(int fd, const char *buf, int count);

// void
// __attribute__((noreturn))
// __attribute__((section(".start")))
// _start(void)
// {
// 	const char *path = "b1data.txt";
// 	const char *msg  = "Hello, file!\n";
// 	char buf[64];

// 	int wfd = open(path, O_WR | O_CREATE);
// 	serial_puts("B1: open(O_WR|O_CREATE) -> fd=");
// 	serial_put_int(wfd);
// 	serial_putc('\n');
// 	if (wfd < 0)
// 		halt_forever();

// 	int wn = write(wfd, msg, 13);
// 	serial_puts("B1: write() -> n=");
// 	serial_put_int(wn);
// 	serial_putc('\n');

// 	close(wfd);

// 	int rfd = open(path, O_RD);
// 	serial_puts("B1: open(O_RD) -> fd=");
// 	serial_put_int(rfd);
// 	serial_putc('\n');
// 	if (rfd < 0)
// 		halt_forever();

// 	int rn = read(rfd, buf, sizeof(buf) - 1);
// 	if (rn < 0)
// 		rn = 0;
// 	buf[rn] = '\0';

// 	serial_puts("B1: read() -> \"");
// 	serial_puts(buf);
// 	serial_puts("\"\n");

// 	close(rfd);

// 	serial_puts("B1: DONE\n");

// 	halt_forever();
// }








// #include <stdint.h>

// /*
// 	B2:
// 	1) otvaranje nepostojeceg fajla BEZ O_CREATE mora da vrati -1
// 	   (ne sme implicitno da ga kreira).
// 	2) O_RDWR: kreirati fajl, upisati, premotati na pocetak (lseek SEEK_SET)
// 	   i procitati u ISTOJ sesiji (bez close/open), pa uporediti sa upisanim.
// */

// #define O_RD     1
// #define O_RDWR   4
// #define O_CREATE 8
// #define SEEK_SET 1

// extern int open(const char *path, int flags);
// extern int close(int fd);
// extern int read(int fd, char *buf, int count);
// extern int write(int fd, const char *buf, int count);
// extern int lseek(int fd, const int offset, int off_flag);

// static void outb(uint16_t port, uint8_t value)
// {
// 	asm("outb %0,%1" : /* empty */ : "a" (value), "Nd" (port) : "memory");
// }

// static void serial_putc(char c)
// {
// 	outb(0xE9, (uint8_t)c);
// }

// static void serial_puts(const char *s)
// {
// 	for (; *s; ++s)
// 		serial_putc(*s);
// }

// static void serial_put_int(long v)
// {
// 	char buf[24];
// 	int i = 0;
// 	int neg = v < 0;
// 	unsigned long u = neg ? (unsigned long)(-v) : (unsigned long)v;

// 	do {
// 		buf[i++] = '0' + (u % 10);
// 		u /= 10;
// 	} while (u);

// 	if (neg)
// 		serial_putc('-');
// 	while (i > 0)
// 		serial_putc(buf[--i]);
// }

// static __attribute__((noreturn)) void halt_forever(void)
// {
// 	for (;;)
// 		asm volatile("hlt");
// }

// void
// __attribute__((noreturn))
// __attribute__((section(".start")))
// _start(void)
// {
// 	int missing_fd = open("b2missing.txt", O_RD);
// 	serial_puts("B2: open(missing, O_RD) -> fd=");
// 	serial_put_int(missing_fd);
// 	serial_puts(missing_fd < 0 ? " (OK, expected -1)\n" : " (FAIL, expected -1)\n");

// 	const char *msg = "RDWR-OK";
// 	char buf[16];

// 	int fd = open("b2rdwr.txt", O_RDWR | O_CREATE);
// 	serial_puts("B2: open(O_RDWR|O_CREATE) -> fd=");
// 	serial_put_int(fd);
// 	serial_putc('\n');
// 	if (fd < 0)
// 		halt_forever();

// 	write(fd, msg, 7);

// 	int pos = lseek(fd, 0, SEEK_SET);
// 	serial_puts("B2: lseek(0, SEEK_SET) -> pos=");
// 	serial_put_int(pos);
// 	serial_putc('\n');

// 	int n = read(fd, buf, 7);
// 	buf[n < 0 ? 0 : n] = '\0';
// 	serial_puts("B2: read-back -> \"");
// 	serial_puts(buf);
// 	serial_puts("\"\n");

// 	close(fd);

// 	serial_puts("B2: DONE\n");

// 	halt_forever();
// }







// #include <stdint.h>

// /*
// 	B3 (writer): otvara DELJENI fajl (prosledjen preko -f/--file hipervizoru)
// 	i upisuje u njega. 

//     Pokrenuti zajedno sa b3_shared_reader.img i deljenim fajlom, npr:
// 		printf 'ORIGINAL' > shared.txt
// 		./hypervisor -m 4 -p 4 -g b3_shared_writer.img b3_shared_reader.img --file shared.txt
// 	Nakon izvrsavanja, shared.txt na disku domacina mora i dalje da sadrzi
// 	"ORIGINAL" (proveriti sa cat shared.txt).
// */

// #define O_WR 2

// extern int open(const char *path, int flags);
// extern int close(int fd);
// extern int write(int fd, const char *buf, int count);


// static void outb(uint16_t port, uint8_t value)
// {
// 	asm("outb %0,%1" : /* empty */ : "a" (value), "Nd" (port) : "memory");
// }

// static void serial_putc(char c)
// {
// 	outb(0xE9, (uint8_t)c);
// }

// static void serial_puts(const char *s)
// {
// 	for (; *s; ++s)
// 		serial_putc(*s);
// }

// static void serial_put_int(long v)
// {
// 	char buf[24];
// 	int i = 0;
// 	int neg = v < 0;
// 	unsigned long u = neg ? (unsigned long)(-v) : (unsigned long)v;

// 	do {
// 		buf[i++] = '0' + (u % 10);
// 		u /= 10;
// 	} while (u);

// 	if (neg)
// 		serial_putc('-');
// 	while (i > 0)
// 		serial_putc(buf[--i]);
// }

// static __attribute__((noreturn)) void halt_forever(void)
// {
// 	for (;;)
// 		asm volatile("hlt");
// }

// void
// __attribute__((noreturn))
// __attribute__((section(".start")))
// _start(void)
// {
// 	int fd = open("shared.txt", O_WR);
// 	serial_puts("B3-WRITER: open(shared.txt, O_WR) -> fd=");
// 	serial_put_int(fd);
// 	serial_putc('\n');
// 	if (fd < 0)
// 		halt_forever();

// 	int n = write(fd, "MODIFIED", 8);
// 	serial_puts("B3-WRITER: write() -> n=");
// 	serial_put_int(n);
// 	serial_putc('\n');

// 	close(fd);

// 	serial_puts("B3-WRITER: DONE\n");

// 	halt_forever();
// }



// #include <stdint.h>

// /*
// 	B3 (reader): otvara isti deljeni fajl samo za citanje. Bez obzira na to
// 	da li je writer VM vec upisala svoju (privatnu, COW) izmenu, ova VM mora
// 	i dalje da vidi ORIGINALNI sadrzaj fajla.

// 	Ocekivan izlaz: "B3-READER: read() -> \"ORIGINAL\"" (NE "MODIFIED").
// */

// #define O_RD 1

// extern int open(const char *path, int flags);
// extern int close(int fd);
// extern int read(int fd, char *buf, int count);

// static void outb(uint16_t port, uint8_t value)
// {
// 	asm("outb %0,%1" : /* empty */ : "a" (value), "Nd" (port) : "memory");
// }

// static void serial_putc(char c)
// {
// 	outb(0xE9, (uint8_t)c);
// }

// static void serial_puts(const char *s)
// {
// 	for (; *s; ++s)
// 		serial_putc(*s);
// }

// static void serial_put_int(long v)
// {
// 	char buf[24];
// 	int i = 0;
// 	int neg = v < 0;
// 	unsigned long u = neg ? (unsigned long)(-v) : (unsigned long)v;

// 	do {
// 		buf[i++] = '0' + (u % 10);
// 		u /= 10;
// 	} while (u);

// 	if (neg)
// 		serial_putc('-');
// 	while (i > 0)
// 		serial_putc(buf[--i]);
// }

// static __attribute__((noreturn)) void halt_forever(void)
// {
// 	for (;;)
// 		asm volatile("hlt");
// }

// void
// __attribute__((noreturn))
// __attribute__((section(".start")))
// _start(void)
// {
// 	int fd = open("shared.txt", O_RD);
// 	serial_puts("B3-READER: open(shared.txt, O_RD) -> fd=");
// 	serial_put_int(fd);
// 	serial_putc('\n');
// 	if (fd < 0)
// 		halt_forever();

// 	char buf[32];
// 	int n = read(fd, buf, sizeof(buf) - 1);
// 	buf[n < 0 ? 0 : n] = '\0';

// 	serial_puts("B3-READER: read() -> \"");
// 	serial_puts(buf);
// 	serial_puts("\"\n");

// 	close(fd);

// 	serial_puts("B3-READER: DONE\n");

// 	halt_forever();
// }




#include <stdint.h>
#include "descriptors.h"
#include "interrupts.h"

static struct gdt_entry gdt[3];

void __attribute__((noreturn))
__attribute__((section(".start")))
_start(void)
{
    struct dt_ptr p;

    /* GDT */
    gdt[0] = (struct gdt_entry){ 0 };

    /* Code segment */
    gdt[1] = (struct gdt_entry){
        .limit_low = 0xFFFF,
        .access = 0x9A,
        .flags_limit = 0xAF,
    };

    /* Data segment */
    gdt[2] = (struct gdt_entry){
        .limit_low = 0xFFFF,
        .access = 0x92,
        .flags_limit = 0xCF,
    };

    p.limit = sizeof(gdt) - 1;
    p.base = (uint64_t)(uintptr_t)gdt;

    asm volatile("lgdt %0" : : "m"(p) : "memory");

    /* Ucitavanje code segmenta */
    asm volatile(
        "pushq $0x08\n\t"
        "lea 1f(%%rip), %%rax\n\t"
        "pushq %%rax\n\t"
        "lretq\n\t"
        "1:\n\t"
        ::: "rax", "memory"
    );

    /* Ucitavanje data segmenata */
    asm volatile(
        "movl $0x10, %%eax\n\t"
        "movw %%ax, %%ds\n\t"
        "movw %%ax, %%es\n\t"
        "movw %%ax, %%ss\n\t"
        ::: "eax", "memory"
    );

    /* IDT[32] -> irq0_handler */
    init_idt();

    /* Dozvoli spoljne prekide */
    asm volatile("sti");

    /* Guest ceka da mu host ubaci IRQ */
    for (;;)
        asm volatile("hlt");
}


