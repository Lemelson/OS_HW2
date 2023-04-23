#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <semaphore.h>
#include <unistd.h>
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdbool.h>
#include <pthread.h>

struct Cell {
    int status; // -1 - dead, 0 - empty(alive), 1 - left Team, 2 - right Team.
    int pid; // id текущего процесса (при уничтожении объекта, оружие с этим pid перестает стрелять).
};

struct Cell *field; // Массив структур.
int field_fd;
const char *fieldName = "field";

const char *printData_sem = "/printData-semaphore";
sem_t *printData; // Используется для корреткного вывода информации(чтобы только один поток мог печатать что0то в какой-то момент времени).

const char *leftMortars_sem = "/Anchuriya(left team)-semaphore";
sem_t *leftMortars;

const char *rightMortars_sem = "/Taranteriya(right team)-semaphore";
sem_t *rightMortars;

int fork_() {
    int result = fork();
    if (result < 0) {
        printf("Error while forking\n");
        exit(-1);
    }
    return result;
}

// Заполняет поле пустыми клетками(пока без орудий).
void initField(int N) {
    field = malloc(N * N * sizeof(struct Cell));
    for (int i = 0; i < N * N; i++) {
        field[i].status = 0;
        field[i].pid = -1;
    }
}

// N - размер поля, pid - pid процесса-орудия, status - число 1 или 2, в зависимости от команды.
int fillField(int N, int status) {
    while (1) {
        int i = (rand() % N);
        int j = (rand() % N);
        
        // В этой ячейке уже есть орудие.
        if (field[i * N + j].status == 1 || field[i * N + j].status == 2) {
            continue;
        }
        
        field[i * N + j].pid = i * N + j; // Сохраняем pid процесса-орудия, соответствующего данной клетке.
        field[i * N + j].status = status;
        return (i * N + j);
    }
}

void initPrintData() {
    if((printData = sem_open(printData_sem, O_CREAT, 0666, 1)) == SEM_FAILED) {
        perror("sem_open: Can't create printData semaphore");
        exit(-1);
    };
}

void initLeftMortars() {
    if((leftMortars = sem_open(leftMortars_sem, O_CREAT, 0666, 1)) == SEM_FAILED) {
        perror("sem_open: Can't create leftMortars semaphore");
        exit(-1);
    };
}

void initRightMortars() {
    if((rightMortars = sem_open(rightMortars_sem, O_CREAT, 0666, 1)) == SEM_FAILED) {
        perror("sem_open: Can't create rightMortars semaphore");
        exit(-1);
    };
}

// Добавление поля в разделяемую память.
void addFieldToSharedMemory(int N) {
    int fd;
    if ((fd = shm_open(fieldName, O_CREAT|O_RDWR, 0666)) == -1 ) {
        perror("Field shm_open Error.");
        exit(-1);
    }
    
    size_t size = N * N * sizeof(struct Cell);
    if (ftruncate(fd, size) == -1) {
        perror("Error with ftruncate");
        exit(-1);
    }
    
    struct Cell *addr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (addr == MAP_FAILED) {
        perror("Error with mmap");
        exit(-1);
    }
    
    memcpy(addr, field, size);
    if (close(fd) == -1) {
        perror("Error with closing arr_fd.");
        exit(-1);
    }
    
    printf("Battle Field successfully created.\n");
}

// Если игра еще идет, то возвращает 0, если победила левая команда 1, а если правая 2, если были уничтожены оба -1
int gameResult(int N) {
    int fd = shm_open(fieldName, O_RDWR, 0666);
    if (fd == -1) {
        perror("Error with opening field shm_open.");
        exit(-1);
    }
    
    size_t size = N * N * sizeof(struct Cell);
    struct Cell *addr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (addr == MAP_FAILED) {
        perror("Error with mmap");
        exit(-1);
    }
    
    bool leftAlive = false, rightAlive = false;
    for (int i = 0; i < N * N; ++i) {
        if (addr[i].pid == -1) {
            continue;
        }
        
        if (addr[i].status == 1) {
            leftAlive = true;
        } else if (addr[i].status == 2) {
            rightAlive = true;
        }
    }
    
    if (close(fd) == -1) {
        perror("Error with closing acces to shared memory\n.");
        exit(-1);
    }
    
    if (leftAlive == false && rightAlive == false) {
        return -1;
    }
    
    if (leftAlive == false) {
        return 2;
    }
    
    if (rightAlive == false) {
        return 1;
    }
    
    return 0;
}

void closeAll() {
    if(sem_close(leftMortars) == -1) {
        perror("sem_close: Incorrect close of leftMortars semaphore");
        exit(-1);
    }
    
    if(sem_close(rightMortars) == -1) {
        perror("sem_close: Incorrect close of rightMortars semaphore");
        exit(-1);
    }
    
    if(sem_close(printData) == -1) {
        perror("sem_close: Incorrect close of printData semaphore");
        exit(-1);
    }
}

void unlinkAll() {
    if(shm_unlink(fieldName) == -1) {
        printf("Shared memory is absent\n");
        perror("shm_unlink");
    }

    if(sem_unlink(leftMortars_sem) == -1) {
        perror("leftMortars sem_unlink");
    }

    if(sem_unlink(rightMortars_sem) == -1) {
        perror("rightMortars sem_unlink");
    }   

    if(sem_unlink(printData_sem) == -1) {
        perror("printData sem_unlink");
    }
    
    closeAll();
}

int main(int argc, char *argv[])
{
    if (argc != 3) {
        printf("you need to enter 2 arguments: 1) size of the battlefield; 2) count of the mortars.");
        exit(0);
    }
    
    printf("Program successfully started!.\n");
    
    int N = atoi(argv[1]); // Размер поля боя.
    initPrintData();
    initLeftMortars();
    initRightMortars();
    initField(N); // Создаем предварительное поле боя.
    
    int mortarsCount = 2 * atoi(argv[2]); // Количество минометов(умноженное на 2, т.к. сражаются 2 стороны).
    srand(time(NULL)); // для рандомной генерации.
    
    int* my_array = malloc(mortarsCount * sizeof(int));
    for (int i = 0; i < mortarsCount; ++i) {
        if (i % 2 == 0) {
            my_array[i] = fillField(N, 1); // левая команда.
        } else {
            my_array[i] = fillField(N, 2); // правая команда.
        }
    }
    addFieldToSharedMemory(N);
    
    printf("Сгенерированное поле:\n");
    for (int i = 0; i < N; ++i) {
        for (int j = 0; j < N; ++j) {
            printf("%d", field[i * N + j].status);
        }
        printf("\n");
    }
    
    int mainPid = getpid();
    for (int i = 0; i < mortarsCount; ++i) {
        char first[sizeof(int)];
        char third[sizeof(int)];
        sprintf(first, "%d", N);
        sprintf(third, "%d", my_array[i]);
        
        if (fork_() == 0) {
            int ret;
            if (i % 2 == 0) {
                ret = execl("./weapon", "./weapon", first, "1", third, NULL);
            } else {
                ret = execl("./weapon", "./weapon", first, "2", third, NULL);
            }
            
            if (ret == -1) {
                printf("execl failed!\n");
            }
            
            exit(0);
        }
    }
    
    sleep(5);
    while (wait(NULL) > 0) { // Ждем пока поле полностью не будет очищено(битва не завершится).
    }
    
    int result = gameResult(N);
    if (result == 1) {
        printf("Победила Анчуария!!!!");
    } else if (result == 2) {
        printf("Победила Тарантерия!!!!");
    } else {
        printf("0_0 НИЧЬЯ 0_0");
    }
    
    free(my_array);
    free(field);
    unlinkAll();
    return 0;
}

