#include "vm.h"

#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <linux/kvm.h>
#include "parser.h"
#include <pthread.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include "file_io.h"
#include "shared_buffer.h"

#define FILE_PORT 0x0278
#define BUFFER_PORT 0x510
#define STATUS_PORT 0x520

#define FILE_OPEN 1
#define FILE_CLOSE 2

#define GUEST_O_RD      1
#define GUEST_O_WR      2
#define GUEST_O_RDWR    4
#define GUEST_O_CREATE  8

#define GUEST_SEEK_SET 1
#define GUEST_SEEK_END 2


void *vm_thread(void *arg){
    struct vm *v = (struct vm *)arg;
    int stop = 0;
    int ret;
	// za svaku masinu ima po 1 prekid na pocetku, to je onaj da se ustanovi mode
	// reader ili writer
    int irq_pending = 1;
	// // imam irq, javi kad guest bude spreman da ga primi, kvm mehanizam 
    v->run->request_interrupt_window = (irq_pending > 0);


    // // za faze bez prekida:
	// int irq_pending = 0;
	// v->run->request_interrupt_window = 0;

    while (stop == 0) {
		// krece izvrsavanje
        ret = ioctl(v->vcpu_fd, KVM_RUN, 0);

        if (ret == -1) {
            printf("KVM_RUN failed\n");
            break;
        }

        switch (v->run->exit_reason) {

		case KVM_EXIT_IO: {
			// ovo je jer nam je data_offset u bajtovima
			char *p = (char*)v->run;
			char *data = p + v->run->io.data_offset;

			if (v->run->io.port == 0xE9) {
				// primamo samo po 1 bajt 
				if (v->run->io.size != 1) {
					printf("Unexpected IO operation\n");
					stop = 1;
					break;
				}

				if (v->run->io.direction == KVM_EXIT_IO_OUT) {
					printf("%c", *data);
					// guest salje karakter po karakter i mi hocemo odmah da vidimo to 
					// zato zove ovo, za brzi prikaz, ne samo da bleji u baferu
					fflush(stdout);
				}
				
				else if (v->run->io.direction == KVM_EXIT_IO_IN) {
					int c = getchar();
					if (c == EOF)
						*data = 0;
					else
						*data = (char)c;


					if (v->mode == MODE_WRITER && v->waiting_for_command) {

						pthread_mutex_lock(&buffer_mutex);
						stop_all = 1;
						pthread_mutex_unlock(&buffer_mutex);
						v->waiting_for_command = 0;
					}
				}
			}

			else if (v->run->io.port == FILE_PORT) {
				if (handle_file_io(v, data) < 0)
					stop = 1;
			}

			else if (v->run->io.port == BUFFER_PORT) {
				// prvi in gde guest cita svoj mode 
				if (!v->mode_sent) {
					if (v->run->io.direction == KVM_EXIT_IO_IN && v->run->io.size == 1) {
						*data = (char)v->mode;
						v->mode_sent = 1;
					}
					else {
						printf("Unexpected IO while reading mode\n");
						stop = 1;
					}
				}

				// writer, ovo je ponovo masina stanja
				else if (v->mode == MODE_WRITER && v->run->io.direction == KVM_EXIT_IO_OUT) {
					if (v->waiting_for_buffer_count && v->run->io.size == 4) {

						// guest prvo salje broj bajtova za upis
						uint32_t count = *(uint32_t *)data;
						v->writer_requested_count = count;

						pthread_mutex_lock(&buffer_mutex);
						readers_done = 0;
						buffer_ready = 0;
						pthread_mutex_unlock(&buffer_mutex);

						// koliko cemo stvarno sacuvati 
						if (count > BUFFER_SIZE)
							shared_count = BUFFER_SIZE;
						else
							shared_count = count;

						// odakle krecemo upis u bufferu
						v->buffer_write_index = 0;

						// kad se posalje 0, to oznacava gotov prenos
						if (count == 0) {
							v->waiting_for_buffer_count = 1;
							pthread_mutex_lock(&buffer_mutex);
							transfer_finished = 1;
							buffer_version++;
							buffer_ready = 1;
							pthread_mutex_unlock(&buffer_mutex);
						}
						else {
							v->waiting_for_buffer_count = 0;
						}
					}

					else if (!v->waiting_for_buffer_count && v->run->io.size == 1) {

						if (v->buffer_write_index < shared_count) {
							shared_buffer[v->buffer_write_index] = *data;
						}

						v->buffer_write_index++;

						// kad se buffer napuni, tada treba sugerisati da se procita
						if (v->buffer_write_index ==
							v->writer_requested_count) {
							v->waiting_for_buffer_count = 1;
							pthread_mutex_lock(&buffer_mutex);
							buffer_version++;
							buffer_ready = 1;
							pthread_mutex_unlock(&buffer_mutex);
						}
					}

					else {
						printf("Unexpected writer IO on buffer port\n");
						stop = 1;
					}
				}

				
				// reader 
				else if (v->mode == MODE_READER &&v->run->io.direction == KVM_EXIT_IO_IN) {
					// prvo saljemo koliko treba da procita
					if (v->waiting_for_read_count && v->run->io.size == 4) {
						*(uint32_t *)data = shared_count;
						v->buffer_index = 0;
						v->buffer_done = 0;

						// ako je poslata nula, znaci da smo zavrsili sa citanjem buffera
						if (shared_count == 0) {
							v->buffer_done = 1;
							v->waiting_for_read_count = 1;
						}
						else {
							v->waiting_for_read_count = 0;
						}
					}

					else if (!v->waiting_for_read_count && v->run->io.size == 1) {
						*data = shared_buffer[v->buffer_index];
						v->buffer_index++;

						if (v->buffer_index == shared_count) {
							v->buffer_done = 1;
							v->waiting_for_read_count = 1;
						}
					}

					else {
						printf(
							"Unexpected reader IO on buffer port: "
							"size=%d direction=%d waiting_for_read_count=%d "
							"buffer_index=%d shared_count=%u\n",
							v->run->io.size,
							v->run->io.direction,
							v->waiting_for_read_count,
							v->buffer_index,
							shared_count);
						stop = 1;
					}
				}

				else {
					printf("Unexpected IO on buffer port\n");
					stop = 1;
				}
			}


			else if (v->run->io.port == STATUS_PORT) {
				
				// reader javlja preko ovog porta koliko je bajtova na kraju procitao, salje 
				if (v->mode == MODE_READER && v->run->io.direction == KVM_EXIT_IO_OUT && v->run->io.size == 4) {

					uint32_t bytes_read = *(uint32_t *)data;

					// ako reader nije uspeo da procita sve, sto smo mu poslali, ne valja
					if (bytes_read != shared_count) {
						printf("VM %d did not read whole buffer (read %u, expected %u)\n", v->id, bytes_read, shared_count);
						stop = 1;
					}

					else {
						pthread_mutex_lock(&buffer_mutex);
    					v->last_buffer_version = buffer_version;
						readers_done++;
						if (readers_done == reader_count) {
							buffer_ready = 0;
						}
						pthread_mutex_unlock(&buffer_mutex);
					}
				}

				// writer treba da dobije podatak koliko je stvarno upisano u buffer njegovih podataka
				else if (v->mode == MODE_WRITER && v->run->io.direction == KVM_EXIT_IO_IN && v->run->io.size == 4) {
					*(uint32_t *)data = shared_count;
					v->waiting_for_command = 1;
				}

				else {
					printf("Unexpected IO on status port\n");
					stop = 1;
				}
			}

			else {
				printf("Unexpected IO port: 0x%x\n", v->run->io.port);
				stop = 1;
			}

			break;
		}


		// ako je spremna da primi prekid, ubaci joj prekid 32
        case KVM_EXIT_IRQ_WINDOW_OPEN:
            if (irq_pending > 0) {
                if (inject_irq(v, IRQ_NUM) < 0) {
                    stop = 1;
                }
                irq_pending--;
            }
            else {
                v->run->request_interrupt_window = 0;
            }
            break;


		// ovde se dolazi bas bas cesto 
		case KVM_EXIT_HLT:

			// // za faze bez prekida
		    // stop = 1;
			// break;

			if (stop_all) {
				if (v->mode == MODE_READER) {
					if (!v->stop_irq_sent) {
						inject_irq(v, 33);
						v->stop_irq_sent = 1;
					}
					else {
						stop = 1;
					}
				}

				else if (v->mode == MODE_WRITER) {
					stop = 1;
				}
			}

		    // ovo je prvi irq za mode 
			if (irq_pending > 0) {
				if (inject_irq(v, IRQ_NUM) < 0) {
					stop = 1;
				}
				else {
					irq_pending--;
				}
			}

			// writer je dosao do kraja svog fajla i postavio je ovaj flag, vise nece slati nista
			else if (transfer_finished) {
				int all_done = 0;

				pthread_mutex_lock(&buffer_mutex);
				if (readers_done == reader_count)
					all_done = 1;
				pthread_mutex_unlock(&buffer_mutex);


				// ako je ova nit reader i ako jeste procitala poslednju verziju, gotova je
				if (v->mode == MODE_READER && v->last_buffer_version == buffer_version) {
					stop = 1;
				}

				// writer zavrsava kad i svi readeri zavrse
				else if (v->mode == MODE_WRITER && all_done) {
					stop = 1;
				}

				// reader jos nije zavrsio poslednji deo iako je writer zavrsio sa upisima
				else if (v->mode == MODE_READER && v->last_buffer_version != buffer_version) {
					if (inject_irq(v, IRQ_NUM) < 0)
						stop = 1;
				}

				else {
					usleep(1000);
				}
			}

			// klasican writer ciklus 
			else if (v->mode == MODE_WRITER) {
				int can_write = 0;
				pthread_mutex_lock(&buffer_mutex);

				// ako ni jedan buffer nije napravljen 
				// ili ako nije vec spreman buffer
				if (buffer_version == 0 || (!buffer_ready && readers_done == reader_count)) {
					can_write = 1;
				}
				pthread_mutex_unlock(&buffer_mutex);

				if (can_write) {
					if (inject_irq(v, IRQ_NUM) < 0)
						stop = 1;
				}
				else {
					usleep(1000);
				}
			}

			// klasican reader 
			else if (v->mode == MODE_READER) {
				int has_new_buffer = 0;
				pthread_mutex_lock(&buffer_mutex);
				
				// ako je spreman bafer i ako je moja poslednja verzija manja zapravo od najnovije verzije
				if (buffer_ready && v->last_buffer_version != buffer_version) {
					has_new_buffer = 1;
				}

				pthread_mutex_unlock(&buffer_mutex);

				if (has_new_buffer) {
					if (inject_irq(v, IRQ_NUM) < 0)
						stop = 1;
				}
				else {
					usleep(1000);
				}
			}
			else {
				stop = 1;
			}

			break;

            
        case KVM_EXIT_SHUTDOWN:
            printf("SHUTDOWN\n");
            stop = 1;
            break;

        default:
            printf("Default - exit reason: %d\n", v->run->exit_reason);
            stop = 1;
            break;
        }
    }

    return NULL;
}


int main(int argc, char *argv[]){
	// za parsiranje struktura iz parser.h
	struct config config;
	// nase virtualne masine i niti 
	struct vm *vms;
	pthread_t *threads;

	if (parse_args(argc, argv, &config) < 0) {
        return 1;
    }

	// ovime se prave shared fajlovi za virtuelne masine, pre nego sto se one uopste i naprave
	if (file_io_init(config.shared_files, config.shared_file_count) < 0) {
		fprintf(stderr, "Failed to initialize file system\n");
		free_config(&config);
		return 1;
	}

	vms = malloc(config.guest_count * sizeof(struct vm));
	threads = malloc(config.guest_count * sizeof(pthread_t));

	if (vms == NULL || threads == NULL) {
		perror("malloc");
		free(vms);
		free(threads);
		free_config(&config);
		return 1;
	}


	// samo je prva vm writer, ostali su readeri 
	reader_count = config.guest_count - 1;
	readers_done = 0;

	for (int i = 0; i < config.guest_count; i++) {

		// ovo su opet predefinisani vec
		struct kvm_sregs sregs;
		struct kvm_regs regs;

		if (vm_init(&vms[i], config.memory_size)) {
			printf("Failed to init VM %d\n", i);

			for (int j = 0; j < i; j++) {
				vm_destroy(&vms[j]);
			}

			free(vms);
			free(threads);
			free_config(&config);
			return 1;
		}

		vms[i].id = i;

		if (i == 0)
			vms[i].mode = MODE_WRITER;
		else
			vms[i].mode = MODE_READER;

		// u sregs ucitavamo stanja specijalnih registara 
		if (ioctl(vms[i].vcpu_fd, KVM_GET_SREGS, &sregs) < 0) {
			perror("KVM_GET_SREGS");

			for (int j = 0; j <= i; j++) {
				vm_destroy(&vms[j]);
			}

			free(vms);
			free(threads);
			free_config(&config);
			return 1;
		}

		// pravi page tables u memoriji gosta, podesava specijalne 
		// registre tako da procesor koristi te page tables, radi u 64-bitnom modu
		setup_long_mode(&vms[i], &sregs, config.page_size);

		if (ioctl(vms[i].vcpu_fd, KVM_SET_SREGS, &sregs) < 0) {
			perror("KVM_SET_SREGS");

			for (int j = 0; j <= i; j++) {
				vm_destroy(&vms[j]);
			}

			free(vms);
			free(threads);
			free_config(&config);
			return 1;
		}

		// ubacivanje guest programa u RAM
		if (load_guest_image(&vms[i], config.guest_paths[i], GUEST_START_ADDR) < 0) {
			printf("Failed to load guest image %s\n",config.guest_paths[i]);

			for (int j = 0; j <= i; j++) {
				vm_destroy(&vms[j]);
			}

			free(vms);
			free(threads);
			free_config(&config);
			return 1;
		}

		// regs struktura na nule
		memset(&regs, 0, sizeof(regs));

		regs.rflags = 0x2;
		// insturction pointer
		regs.rip = 0;

		// stek je na samom vrhu memorije, ali vec posle njega su one tabele stranica
		// pa moramo da izracunamo gde da stavimo stack
		if (config.page_size == 4 * 1024) {
			size_t num_pts = vms[i].mem_size / (2 * 1024 * 1024);
			regs.rsp = vms[i].mem_size - (3 + num_pts) * 0x1000;
		}
		else {
			regs.rsp = vms[i].mem_size - 3 * 0x1000;
		}

		// sad zapravo zvanicno upisujemo ove regitre sto smo ih napravili 
		if (ioctl(vms[i].vcpu_fd, KVM_SET_REGS, &regs) < 0) {
			perror("KVM_SET_REGS");

			for (int j = 0; j <= i; j++) {
				vm_destroy(&vms[j]);
			}

			free(vms);
			free(threads);
			free_config(&config);
			return 1;
		}
	}

	for (int i = 0; i < config.guest_count; i++) {
		// kao argument za vm_thread prosledjujemo vm strukturu 
		if (pthread_create(&threads[i], NULL, vm_thread, &vms[i]) != 0) {
			perror("pthread_create");
			for (int j = 0; j < i; j++) {
				pthread_join(threads[j], NULL);
			}
			for (int j = 0; j < config.guest_count; j++) {
				vm_destroy(&vms[j]);
			}
			free(vms);
			free(threads);
			free_config(&config);
			return 1;
		}
	}

	for (int i = 0; i < config.guest_count; i++) {
		pthread_join(threads[i], NULL);
	}

	for (int i = 0; i < config.guest_count; i++) {
		vm_destroy(&vms[i]);
	}

	free(vms);
	free(threads);
	free_config(&config);

	return 0;

}
