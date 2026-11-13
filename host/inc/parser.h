#ifndef PARSER_H
#define PARSER_H

#include <stddef.h>  // ovo nam treba zbog size_t i ostalih tipova

// ovde smestamo sve podatke koje korisnik unese
// na osnovu kojih dalje radimo
struct config {
    size_t memory_size;   // 2, 4 ili 8 MB
    size_t page_size;     // 4 KB ili 2 MB
    char **guest_paths;   
    int guest_count;      
    char **shared_files;  
    int shared_file_count;
};

int parse_args(int argc, char *argv[], struct config *config);
void free_config(struct config *config);

#endif