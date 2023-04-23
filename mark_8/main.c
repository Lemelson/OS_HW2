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
#include <sys/sem.h>
#include <sys/shm.h>

struct Cell {
    int status; // -1 - dead, 0 - empty(alive), 1 - left Team, 2 - right Team.
    int pid; // id текущего процесса (при уничтожении объекта, оружие с этим pid перестает стрелять).
};

struct Cell *field; // Массив структур.
int field_fd;

int printDataID; // Используется для корреткного вывода информации(чтобы только один поток мог печатать что0то в какой-то момент времени).
int leftMortarsID;
int rightMortarsID;

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
    key_t shm_key = ftok("/tmp", 1200);
    field_fd  = shmget(shm_key, sizeof(struct Cell), 0666 | IPC_CREAT);
    if (field_fd == -1) {
        perror("shmget error");
        exit(-1);
    }

    field = shmat(field_fd, NULL, 0);
    if (field == (void*)-1) {
        perror("shmat error");
        exit(-1);
    }
    
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
    key_t sem_key1 = ftok("/tmp", 1201);
    printDataID = semget(sem_key1, 1, IPC_CREAT | 0666);
    if (printDataID < 0) {
        perror("Error with creation printData semaphore: ");
        exit(EXIT_FAILURE);
    }
}

void initLeftMortars() {
    key_t sem_key2 = ftok("/tmp", 1202);
    leftMortarsID = semget(sem_key2, 1, IPC_CREAT | 0666);
    if (leftMortarsID < 0) {
        perror("Error with creation leftMortar semaphore: ");
        exit(EXIT_FAILURE);
    }
}

void initRightMortars() {
    key_t sem_key3 = ftok("/tmp", 1203);
    rightMortarsID = semget(sem_key3, 1, IPC_CREAT | 0666);
    if (rightMortarsID < 0) {
        perror("Error with creation rightMortar semaphore: ");
        exit(EXIT_FAILURE);
    }
}

// Если игра еще идет, то возвращает 0, если победила левая команда 1, а если правая 2, если были уничтожены оба -1
int gameResult(int N) {
    bool leftAlive = false, rightAlive = false;
    for (int i = 0; i < N * N; ++i) {
        if (field[i].pid == -1) {
            continue;
        }
        
        if (field[i].status == 1) {
            leftAlive = true;
        } else if (field[i].status == 2) {
            rightAlive = true;
        }
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

void unlinkAll() {
    if (semctl(printDataID  , 0, IPC_RMID) == -1) {
        perror("semctl");
        exit(1);
    }
    
    if (semctl(leftMortarsID, 0, IPC_RMID) == -1) {
        perror("semctl");
        exit(1);
    }
    
    if (semctl(rightMortarsID, 0, IPC_RMID) == -1) {
        perror("semctl");
        exit(1);
    }
    
    shmdt(field);
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

