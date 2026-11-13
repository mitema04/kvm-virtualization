#include "file.h"
#include "io.h"
#include <stdint.h>

#define FILE_PORT 0x0278

#define FILE_OPEN 1
#define FILE_CLOSE 2
#define FILE_READ 3
#define FILE_WRITE 4
#define FILE_LSEEK 5

int open(const char *path, int flags){
    outb(FILE_PORT, FILE_OPEN);    // saljemo kod za otvaranje 
    outl(FILE_PORT, (uint32_t)flags);   // saljemo flagove, to nam je u parametru poziva
    int len = 0;
    while (path[len] != '\0') 
        len++;
    
    outl(FILE_PORT, (uint32_t)len);  // onda saljemo duzinu path-a
    for (int i = 0; i < len; i++) 
        outb(FILE_PORT, (uint8_t)path[i]);  // i onda saljemo karakter po karakter path-a
    
    int fd = (int)inl(FILE_PORT);   // i primamo deskriptor
    return fd;
}

int close(int fd)
{
    outb(FILE_PORT, FILE_CLOSE);  // saljemo kod 
    outl(FILE_PORT, (uint32_t)fd);  // saljemo deskriptor
    return (int)inl(FILE_PORT);  // primamo rezultat zatvaranja
}

int read(int fd, char *buf, int count)
{
    outb(FILE_PORT, FILE_READ);
    outl(FILE_PORT, (uint32_t)fd);
    outl(FILE_PORT, (uint32_t)count);

    int result = (int)inl(FILE_PORT);

    if (result < 0)
        return -1;

    for (int i = 0; i < result; i++) {
        buf[i] = (char)inb(FILE_PORT);
    }

    return result;
}


int write(int fd, const char *buf, int count)
{
    outb(FILE_PORT, FILE_WRITE);
    outl(FILE_PORT, (uint32_t)fd);
    outl(FILE_PORT, (uint32_t)count);

    for (int i = 0; i < count; i++) {
        outb(FILE_PORT, (uint8_t)buf[i]);
    }

    return (int)inl(FILE_PORT);
}


int lseek(int fd, const int offset, int off_flag)
{
    outb(FILE_PORT, FILE_LSEEK);
    outl(FILE_PORT, (uint32_t)fd);
    outl(FILE_PORT, (uint32_t)offset);
    outl(FILE_PORT, (uint32_t)off_flag);
    return (int)inl(FILE_PORT);
}