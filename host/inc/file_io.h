#ifndef FILE_IO_H
#define FILE_IO_H

#include "vm.h"
#define FILE_PORT 0x0278

int handle_file_io(struct vm *v, char *data);
int file_io_init(char **shared_files, int shared_file_count);

#endif