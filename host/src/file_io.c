#include "file_io.h"

#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>
#include <sys/stat.h>
#include <errno.h>


// kodovi za operations
#define FILE_OPEN 1
#define FILE_CLOSE 2
#define FILE_READ 3
#define FILE_WRITE 4
#define FILE_LSEEK 5

// kodovi za prava pristupa
#define GUEST_O_RD      1
#define GUEST_O_WR      2
#define GUEST_O_RDWR    4
#define GUEST_O_CREATE  8

// kako guest zada seek konstante
#define GUEST_SEEK_SET 1
#define GUEST_SEEK_END 2


// registar fajlova koje host ima
static struct host_file host_files[MAX_HOST_FILES];
// moramo da ga zakljucamo ako radimo sa njim kao jedna virtuelna masina
// jer ga svi dele
static pthread_mutex_t host_files_mutex = PTHREAD_MUTEX_INITIALIZER;


// flagove koje je korisnik poslao konvertujemo u prave linux flagove
static int convert_open_flags(int flags){
    int host_flags = 0;

    if (flags & GUEST_O_RD)
        host_flags |= O_RDONLY;

    if (flags & GUEST_O_WR)
        host_flags |= O_WRONLY;

    if (flags & GUEST_O_RDWR)
        host_flags |= O_RDWR;

    if (flags & GUEST_O_CREATE)
        host_flags |= O_CREAT;

    return host_flags;
}

// provera formata imena iz postavke zadatka
static int valid_file_name(const char *name){
    if (name[0] == '\0')
        return 0;

    if (!((name[0] >= 'a' && name[0] <= 'z') ||
          (name[0] >= 'A' && name[0] <= 'Z')))
        return 0;

    for (int i = 1; name[i] != '\0'; i++) {
        char c = name[i];

        if (!((c >= 'a' && c <= 'z') ||
              (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') ||
              c == '.')) {
            return 0;
        }
    }

    return 1;
}


// moramo za datu vm da dodamo njoj novootvoreni fajl 
// ona vraca guest descriptor
static int add_open_file(struct vm *v, int host_fd, int flags, const char *path, int is_shared, int has_private_copy){
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        if (!v->open_files[i].used) {
            v->open_files[i].used = 1;
            v->open_files[i].host_fd = host_fd;
            v->open_files[i].flags = flags;

            v->open_files[i].is_shared = is_shared;
            v->open_files[i].has_private_copy = has_private_copy;

            int j = 0;
            while (path[j] != '\0' && j < MAX_FILE_NAME - 1) {
                v->open_files[i].path[j] = path[j];
                j++;
            }

            v->open_files[i].path[j] = '\0';
            // i je guest file descriptor
            return i;
        }
    }

    return -1;
}


// guest descriptor definitivno nije isto sto i host descriptor, ovime preslikavamo
static int get_host_fd(struct vm *v, int guest_fd){
    if (guest_fd < 0 || guest_fd >= MAX_OPEN_FILES)
        return -1;

    if (!v->open_files[guest_fd].used)
        return -1;

    return v->open_files[guest_fd].host_fd;
}


// idemo kroz registar fajlova od hosta i trazimo
static int find_private_file(int vm_id, const char *name){
    for (int i = 0; i < MAX_HOST_FILES; i++) {
        if (host_files[i].used &&
            !host_files[i].shared &&
            host_files[i].owner_vm_id == vm_id &&
            strcmp(host_files[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}


// idemo kroz registar fajlova od hosta i trazimo
static int find_shared_file(const char *name){
    for (int i = 0; i < MAX_HOST_FILES; i++) {
        if (host_files[i].used &&
            host_files[i].shared &&
            strcmp(host_files[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}


// kad neka vm otvori novi fajl, ona ga je napravila i moramo da ubacimo u registar fajlova na hostu to 
static int add_host_file(const char *name, const char *host_path, int owner_vm_id, int shared){
    for (int i = 0; i < MAX_HOST_FILES; i++) {
        // za prvi neiskorisceni 
        if (!host_files[i].used) {
            host_files[i].used = 1;
            host_files[i].owner_vm_id = owner_vm_id;
            host_files[i].shared = shared;

            strncpy(host_files[i].name, name, MAX_FILE_NAME - 1);
            host_files[i].name[MAX_FILE_NAME - 1] = '\0';   

            strncpy(host_files[i].host_path, host_path, MAX_HOST_PATH - 1);
            host_files[i].host_path[MAX_HOST_PATH - 1] = '\0';

            return i;
        }
    }

    return -1;
}


// od poslednjeg / pa do kraja gledamo ime 
static const char *get_file_name(const char *path){
    const char *last_slash = strrchr(path, '/');
    if (last_slash != NULL)
        return last_slash + 1;
    return path;
}



int file_io_init(char **shared_files, int shared_file_count){
    // sve se inicijalizuje nulama u host_files
    memset(host_files, 0, sizeof(host_files));

    if (mkdir(".vm_files", 0755) < 0) {
        if (errno != EEXIST) {
            perror("mkdir");
            return -1;
        }
    }

    for (int i = 0; i < shared_file_count; i++) {

        const char *name = get_file_name(shared_files[i]);
            
        if (!valid_file_name(name)) {
            fprintf(stderr, "Invalid shared file name: %s\n", name);
            return -1;
        }

        // ovim open - close proveravamo da li fajl postoji
        int fd = open(shared_files[i], O_RDONLY);
        if (fd < 0) {
            fprintf(stderr, "Shared file does not exist: %s\n", shared_files[i]);
            return -1;
        }
        close(fd);

        // i ovde dodajemo shared fajl 
        if (add_host_file(name, shared_files[i], -1, 1) < 0) {
            return -1;
        }
    }

    return 0;
}


// ovo pravi u folderu .vm_files tacan naziv fajla na osnovu virtuelne masine i imena
static int make_private_path(int vm_id, const char *name, char *path, size_t path_size){

    // upisuje u path path_size velicine maksimalne 
    int result = snprintf(path, path_size, ".vm_files/vm%d_%s", vm_id, name);

    if (result < 0 || (size_t)result >= path_size)
        return -1;

    return 0;
}


// helepr za kopiranje shared fajla u privatnu kopiju, samo prebacuje sadrzaj
// posredstvom buffera
static int copy_file(const char *src_path, const char *dst_path){
    int src_fd = open(src_path, O_RDONLY);

    if (src_fd < 0)
        return -1;

    int dst_fd = open(dst_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);

    if (dst_fd < 0) {
        close(src_fd);
        return -1;
    }

    char buffer[4096];
    ssize_t bytes_read;

    while ((bytes_read = read(src_fd, buffer, sizeof(buffer))) > 0) {
        ssize_t total_written = 0;
        while (total_written < bytes_read) {
            ssize_t written = write(dst_fd, buffer + total_written, bytes_read - total_written);
            if (written <= 0) {
                close(src_fd);
                close(dst_fd);
                return -1;
            }
            total_written += written;
        }
    }

    close(src_fd);
    close(dst_fd);

    return bytes_read < 0 ? -1 : 0;
}



// najbitnija fja za COW mehanizam
static int ensure_private_copy(struct vm *v, int guest_fd){
    if (guest_fd < 0 || guest_fd >= MAX_OPEN_FILES)
        return -1;

    struct open_file *file = &v->open_files[guest_fd];

    if (!file->used)
        return -1;

    // ako file nije shared, ili vec ima kopiju, sve ok 
    if (!file->is_shared || file->has_private_copy)
        return 0;


    // ovo se pamti trenutan offset u fajlu
    off_t old_offset = lseek(file->host_fd, 0, SEEK_CUR);

    if (old_offset < 0)
        return -1;


    pthread_mutex_lock(&host_files_mutex);

    // Mozda privatna kopija za ovu VM vec postoji
    // zbog nekog ranijeg otvaranja/pisanja
    // deskriptori dva otvaranja istog fajla ne moraju da budu isti 
    int private_index = find_private_file(v->id, file->path);
    int new_fd = -1;

    if (private_index >= 0) {
        struct host_file *private_file = &host_files[private_index];
        int host_flags = convert_open_flags(file->flags);
        new_fd = open(private_file->host_path, host_flags);
    }
    else {

        int shared_index = find_shared_file(file->path);
        if (shared_index < 0) {
            pthread_mutex_unlock(&host_files_mutex);
            return -1;
        }

        struct host_file *shared_file = &host_files[shared_index];
        char private_path[MAX_HOST_PATH];

        // napravimo privatnu putanju 
        if (make_private_path(v->id, file->path, private_path, sizeof(private_path)) < 0) {
            pthread_mutex_unlock(&host_files_mutex);
            return -1;
        }

        // iskopiramo sadrzaj u privatni fajl
        if (copy_file(shared_file->host_path, private_path) < 0) {
            pthread_mutex_unlock(&host_files_mutex);
            return -1;
        }

        int host_flags = convert_open_flags(file->flags);
        // otvaramo privatni fajl sa istim flagovima kao sto je vec otvoreni fajl imao
        new_fd = open(private_path, host_flags);

        if (new_fd < 0) {
            unlink(private_path);
            pthread_mutex_unlock(&host_files_mutex);
            return -1;
        }

        // svakako host mora da ima sve fajlove, pa tako i ovaj najnoviji
        if (add_host_file(file->path, private_path, v->id, 0) < 0) {
            close(new_fd);
            unlink(private_path);
            pthread_mutex_unlock(&host_files_mutex);
            return -1;
        }
    }

    // novi fd mora da ima isti offset kao stari 
    if (lseek(new_fd, old_offset, SEEK_SET) < 0) {
        close(new_fd);
        pthread_mutex_unlock(&host_files_mutex);
        return -1;
    }

    pthread_mutex_unlock(&host_files_mutex);

    // sada prestajemo da koristimo shared fajl
    close(file->host_fd);
    // i prelazimo na new_fd
    file->host_fd = new_fd;
    file->has_private_copy = 1;

    return 0;
}



// da li je guest poslao dozvoljenu kombinaciju flagova
static int valid_open_flags(int flags){
    int access_flags = flags & (GUEST_O_RD | GUEST_O_WR | GUEST_O_RDWR);

    if (access_flags != GUEST_O_RD && access_flags != GUEST_O_WR && access_flags != GUEST_O_RDWR) {
        return 0;
    }

    int allowed_flags = GUEST_O_RD | GUEST_O_WR | GUEST_O_RDWR | GUEST_O_CREATE;

    if (flags & ~allowed_flags)
        return 0;

    return 1;
}


// ovo je glavna funkcija u kojoj tumacimo prekid koji je dosao zbog rada sa fajlom 
int handle_file_io(struct vm *v, char *data){
    // ako je state FILE_IDLE, to znaci da cekamo kod poziva
    // pa prebacujemo state na sledeci u zavisnosti od koda
    if (v->file_req.state == FILE_IDLE && v->run->io.direction == KVM_EXIT_IO_OUT && v->run->io.size == 1) {
        uint8_t operation = *(uint8_t *)data;
        v->file_req.operation = operation;

        if (operation == FILE_OPEN) {
            v->file_req.state = FILE_WAIT_FLAGS;
            return 0;
        }

        if (operation == FILE_CLOSE || operation == FILE_READ || operation == FILE_WRITE || operation == FILE_LSEEK) {
            v->file_req.state = FILE_WAIT_FD;
            return 0;
        }

        return -1;
    }

    // ako cekamo flagove, stigli su, sad cekamo duzinu path-a
    if (v->file_req.state == FILE_WAIT_FLAGS && v->run->io.direction == KVM_EXIT_IO_OUT && v->run->io.size == 4) {
        v->file_req.flags = *(uint32_t *)data;
        v->file_req.state = FILE_WAIT_LEN;
        return 0;
    }

    // ako cekamo duzinu, stigla je, sad cekamo karakter po karakter
    if (v->file_req.state == FILE_WAIT_LEN && v->run->io.direction == KVM_EXIT_IO_OUT && v->run->io.size == 4) {
        v->file_req.path_len = *(uint32_t *)data;

        if (v->file_req.path_len <= 0 ||
            v->file_req.path_len >= MAX_FILE_NAME) {
            return -1;
        }

        v->file_req.path_index = 0; // koliko smo dobili karaktera do sada 
        v->file_req.state = FILE_WAIT_PATH;
        return 0;
    }


    // ako cekamo path, ili nastavljamo, ili smo dosli do kraja pa krecemo da radimo open
    if (v->file_req.state == FILE_WAIT_PATH && v->run->io.direction == KVM_EXIT_IO_OUT && v->run->io.size == 1) {
        v->file_req.path[v->file_req.path_index++] = *data;

        if (v->file_req.path_index == v->file_req.path_len) {
            // dobili smo i poslednji karakter
            v->file_req.path[v->file_req.path_len] = '\0';

            // da li je guest poslao dozvoljenu kombinaciju flagova
            if (!valid_open_flags(v->file_req.flags)) {
                v->file_req.result = -1;
                v->file_req.state = FILE_WAIT_RESULT;
                return 0;
            }

            // jer linux ne koristi nase api flagove nego svoje 
            int host_flags = convert_open_flags(v->file_req.flags);
            int host_fd = -1;
            int is_shared = 0;  
            int has_private_copy = 0;

            
            // provera imena fajla da li je kao iz postavke

            if (!valid_file_name(v->file_req.path)) {
                v->file_req.result = -1;
                v->file_req.state = FILE_WAIT_RESULT;
                return 0;
            }

            // radimo nad globalnim registrom fajlova u hostu
            pthread_mutex_lock(&host_files_mutex);

            // odavde imamo 3 mogucnosti za open
            // ili se otvara privatni fajl
            // ili se otvara shared 
            // ili se pravi privatni fajl ako postoji O_CREATE

            int private_index = find_private_file(v->id, v->file_req.path);
            int shared_index = find_shared_file(v->file_req.path);

            if (private_index >= 0) {
                struct host_file *file = &host_files[private_index];

                // Ako postoji i shared original istog imena, ovo je privatna COW kopija shared fajla
                
                if (shared_index >= 0) {
                    is_shared = 1;
                    has_private_copy = 1;
                }

                if (v->file_req.flags & GUEST_O_CREATE) {
                    // ovo 0644 je zbog O_CREATE, to dodeljuje one permisije za user, grupu, others
                    host_fd = open(file->host_path, host_flags, 0644); 
                }
                else {
                    host_fd = open(file->host_path,host_flags);
                }
            }

            // ako smo ovde, znaci samo postoji shared, nema privatne kopije
            // ne radimo COW jer ne treba prilikom opena da se radi COW 
            else if (shared_index >= 0) {
                is_shared = 1;
                has_private_copy = 0;
                struct host_file *shared_file = &host_files[shared_index];
                host_fd = open(shared_file->host_path, host_flags);
            }


            // fajla nema nigde, nastaje ako je O_CREATE ukljucen
            else {
                if (!(v->file_req.flags & GUEST_O_CREATE)) {
                    host_fd = -1;
                }
                else {
                    char private_path[MAX_HOST_PATH];
                    if (make_private_path(v->id, v->file_req.path, private_path, sizeof(private_path)) < 0) {
                        host_fd = -1;
                    }
                    else {
                        host_fd = open(private_path, host_flags, 0644);
                        if (host_fd >= 0) {
                            if (add_host_file(v->file_req.path, private_path, v->id, 0) < 0) {
                                close(host_fd);
                                unlink(private_path);
                                host_fd = -1;
                            }
                        }
                    }
                }
            }

            pthread_mutex_unlock(&host_files_mutex);

            // ako nismo uspeli da otvorimo fajl, open gosta vraća -1.
            if (host_fd < 0) {
                v->file_req.result = -1;
            }
            else {
                int guest_fd = add_open_file(v, host_fd, v->file_req.flags, v->file_req.path, is_shared, has_private_copy);
                if (guest_fd < 0) {
                    close(host_fd);
                    v->file_req.result = -1;
                }
                else {
                    v->file_req.result = guest_fd;
                }
            }
            v->file_req.state = FILE_WAIT_RESULT;
        }

        return 0;
    }


    // u kvm strukutru se upisuje fd i ide se na stanje idle 
    // u slucaju read operacije se nakon read-a jos i upisuje u interni buffer koji se zadaje u guestu
    // zato nakon uspesne read operacije treba jos i poslati sve procitano u buffer
    if (v->file_req.state == FILE_WAIT_RESULT && v->run->io.direction == KVM_EXIT_IO_IN && v->run->io.size == 4) {

        // u data koji smo prosledili celoj ovoj fji upisujemo sadrzaj 
        *(uint32_t*)data = (uint32_t)v->file_req.result;

        if (v->file_req.operation == FILE_READ && v->file_req.result > 0) {
            v->file_req.data_index = 0;
            v->file_req.state = FILE_SEND_DATA;
        }
        else {
            v->file_req.state = FILE_IDLE;
        }

        return 0;
    }

    
    // ceka se file descriptor u slucaju i close, i read i write i lseek
    if (v->file_req.state == FILE_WAIT_FD && v->run->io.direction == KVM_EXIT_IO_OUT && v->run->io.size == 4) {
        v->file_req.fd = *(uint32_t*)data;

        if (v->file_req.operation == FILE_CLOSE) {
            int guest_fd = v->file_req.fd;
            int host_fd = get_host_fd(v, guest_fd);

            if (host_fd < 0) {
                v->file_req.result = -1;
            }
            else {
                v->file_req.result = close(host_fd);
                if (v->file_req.result == 0) {
                    v->open_files[guest_fd].used = 0;
                    v->open_files[guest_fd].host_fd = -1;
                    v->open_files[guest_fd].flags = 0;
                    v->open_files[guest_fd].path[0] = '\0';
                }
            }
            v->file_req.state = FILE_WAIT_RESULT;
        }

        else if (v->file_req.operation == FILE_READ || v->file_req.operation == FILE_WRITE) {
            v->file_req.state = FILE_WAIT_COUNT;
        }

        else if (v->file_req.operation == FILE_LSEEK) {
            v->file_req.state = FILE_WAIT_OFFSET;
        }

        return 0;
    }


    // za read i write, ceka se broj bajtova
    if (v->file_req.state == FILE_WAIT_COUNT && v->run->io.direction == KVM_EXIT_IO_OUT && v->run->io.size == 4) {
        v->file_req.count = *(uint32_t *)data;

        if (v->file_req.count < 0 || v->file_req.count > MAX_FILE_DATA) {
            return -1;
        }

        v->file_req.data_index = 0;

        if (v->file_req.operation == FILE_READ) {
            int host_fd = get_host_fd(v, v->file_req.fd);
            if (host_fd < 0) {
                v->file_req.result = -1;
            }
            else {
                // u result se upisuje broj procitanih bajtova
                v->file_req.result = read(host_fd, v->file_req.data, v->file_req.count);
            }
            v->file_req.state = FILE_WAIT_RESULT;
        }



        else if (v->file_req.operation == FILE_WRITE) {
            // ako je zatrazen upis 0 bajtova, svakako se aktivira cow mehanizam 
            // ako je zatrazeno 0 bajtova, samo se ide na rezultat, ne ceka se na podatke
            if (v->file_req.count == 0) {
                if (ensure_private_copy(v, v->file_req.fd) < 0) {
                    v->file_req.result = -1;
                }
                else {
                    int host_fd = get_host_fd(v, v->file_req.fd);
                    if (host_fd < 0) {
                        v->file_req.result = -1;
                    }
                    else {
                        v->file_req.result = write(host_fd, v->file_req.data, 0);
                    }
                }
                v->file_req.state = FILE_WAIT_RESULT;
            }
            // inace krecemo na podatke
            else {
                v->file_req.state = FILE_WAIT_DATA;
            }
        }

        return 0;
    }


    // ovde dolazimo kada smo u FILE_WRITE i kada cekamo da nam guest salje podatke za upis 
    if (v->file_req.state == FILE_WAIT_DATA && v->run->io.direction == KVM_EXIT_IO_OUT && v->run->io.size == 1) {

        v->file_req.data[v->file_req.data_index] = *data;
        v->file_req.data_index++;

        // ako smo zavrsili sa upisom u strukturu file_req
        // sa ensure private copy mi za dati otvoren faj
        if (v->file_req.data_index == v->file_req.count) {
            if (ensure_private_copy(v, v->file_req.fd) < 0) {
                v->file_req.result = -1;
            }
            else {
                int host_fd = get_host_fd(v, v->file_req.fd);
                if (host_fd < 0) {
                    v->file_req.result = -1;
                }
                else {
                    v->file_req.result = write(host_fd, v->file_req.data, v->file_req.count);
                }
            }

            v->file_req.state = FILE_WAIT_RESULT;
        }
        return 0;
    }


    // ovo je samo za read, treba da se salje bajt po bajt iz strukture
    // file_req data koja je vec popunjena sa fjom read iz linuxa
    if (v->file_req.state == FILE_SEND_DATA && v->run->io.direction == KVM_EXIT_IO_IN && v->run->io.size == 1) {
        *data = v->file_req.data[v->file_req.data_index];
        v->file_req.data_index++;

        if (v->file_req.data_index == v->file_req.result) {
            v->file_req.state = FILE_IDLE;
        }

        return 0;
    }


    // posle pomeraja ceka se seek_flag
    if (v->file_req.state == FILE_WAIT_SEEK_FLAG && v->run->io.direction == KVM_EXIT_IO_OUT &&v->run->io.size == 4) {
        v->file_req.off_flag = (int)*(uint32_t *)data;
        int host_fd = get_host_fd(v, v->file_req.fd);

        if (host_fd < 0) {
            v->file_req.result = -1;
        }

        else if (v->file_req.off_flag == GUEST_SEEK_SET) {
            v->file_req.result = (int)lseek(host_fd, v->file_req.offset, SEEK_SET);
        }
        // za SEEK_END se offset ignorise
        else if (v->file_req.off_flag == GUEST_SEEK_END) {
            v->file_req.result = (int)lseek(host_fd, 0, SEEK_END);
        }
        else {
            v->file_req.result = -1;
        }

        v->file_req.state = FILE_WAIT_RESULT;
        return 0;
    }


    // posle descriptora ceka se pomeraj 
    if (v->file_req.state == FILE_WAIT_OFFSET && v->run->io.direction == KVM_EXIT_IO_OUT && v->run->io.size == 4) {
        v->file_req.offset = (int32_t)*(uint32_t *)data;
        v->file_req.state = FILE_WAIT_SEEK_FLAG;
        return 0;
    }

    return -1;
}