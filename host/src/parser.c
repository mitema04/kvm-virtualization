#include "parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


int parse_args(int argc, char *argv[], struct config *config) {
    config->memory_size = 0;
    config->page_size = 0;
    config->guest_paths = NULL;
    config->guest_count = 0;
    config->shared_files = NULL;
    config->shared_file_count = 0;

    int memory_set = 0;  // da li smo postavili memoriju 
    int page_set = 0;    // da li smo postavili page
    int guest_set = 0;   // da li smo postavili guest
    int file_set = 0;    // da li smo postavili file

    
    // U najgorem slucaju skoro svi argumenti komandne linije mogu biti putanje do guest fajlova

    config->guest_paths = malloc((size_t)argc * sizeof(char *));
    if (config->guest_paths == NULL) {
        perror("malloc");
        return -1;
    }

    config->shared_files = malloc((size_t)argc * sizeof(char *));
    if (config->shared_files == NULL) {
        perror("malloc");
        free(config->guest_paths);
        config->guest_paths = NULL;
        return -1;
    }

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-m") == 0 || strcmp(argv[i], "--memory") == 0) {
            if (memory_set) {
                fprintf(stderr, "Cannot specify mem options more than once.\n");
                goto error;
            }

            if (i + 1 >= argc) {
                fprintf(stderr, "Missing value for mem option.\n");
                goto error;
            }

            int memory = atoi(argv[++i]);

            if (memory != 2 && memory != 4 && memory != 8) {
                fprintf(stderr, "Memory must be 2, 4 or 8 MB.\n");
                goto error;
            }

            config->memory_size = (size_t)memory * 1024 * 1024;
            memory_set = 1;
        }


        else if (strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--page") == 0) {
            if (page_set) {
                fprintf(stderr, "Cannot specify page options more than once.\n");
                goto error;
            }

            if (i + 1 >= argc) {
                fprintf(stderr, "Missing value for page option.\n");
                goto error;
            }

            int page = atoi(argv[++i]);

            if (page == 4) {
                config->page_size = 4 * 1024;
            }

            else if (page == 2) {
                config->page_size = 2 * 1024 * 1024;
            }

            else {
                fprintf(stderr,"Page size must be 4 KB or 2 MB.\n");
                goto error;
            }
            page_set = 1;
        }

        // Skupljamo argumente sve dok ne dodjemo do
        // sledece opcije koja pocinje karakterom '-'

        else if (strcmp(argv[i], "-g") == 0 || strcmp(argv[i], "--guest") == 0) {
            if (guest_set) {
                fprintf(stderr, "Cannot specify guest options more than once.\n");
                goto error;
            }

            i++;

            if (i >= argc || argv[i][0] == '-') {
                fprintf(stderr,"At least one guest must be specified.\n");
                goto error;
            }

            while (i < argc && argv[i][0] != '-') {
                config->guest_paths[config->guest_count] = argv[i];
                config->guest_count++;
                i++;
            }
            // jer previse smo povecali i, svakako ce for petlja ovo da uradi za nas, mora vratimo
            i--;
            guest_set = 1;
        }

        else if (strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "--file") == 0) {
            if (file_set) {
                fprintf(stderr, "Cannot specify file options more than once.\n");
                goto error;
            }

            i++;

            if (i >= argc || argv[i][0] == '-') {
                fprintf(stderr, "At least one shared file must be specified.\n");
                goto error;
            }

            while (i < argc && argv[i][0] != '-') {
                config->shared_files[config->shared_file_count] = argv[i];
                config->shared_file_count++;
                i++;
            }

            i--;
            file_set = 1;
        }

        else {
            fprintf(stderr,"Unknown option or argument: %s\n", argv[i]);
            goto error;
        }
    }


    if (!memory_set) {
        fprintf(stderr, "Memory option is required.\n");
        goto error;
    }

    if (!page_set) {
        fprintf(stderr, "Page option is required.\n");
        goto error;
    }

    if (!guest_set || config->guest_count == 0) {
        fprintf(stderr, "At least one guest is required.\n");
        goto error;
    }

    return 0;


error:
    free(config->guest_paths);
    free(config->shared_files);
    config->guest_paths = NULL;
    config->shared_files = NULL;
    config->guest_count = 0;
    config->shared_file_count = 0;
    return -1;
}


void free_config(struct config *config){
    free(config->guest_paths);
    free(config->shared_files);
    config->guest_paths = NULL;
    config->shared_files = NULL;
    config->guest_count = 0;
    config->shared_file_count = 0;
}