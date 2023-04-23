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

void loadPrintData() {
    if((printData = sem_open(printData_sem, 0)) == SEM_FAILED) {
        perror("sem_open: Can't create printData semaphore");
        exit(-1);
    };
}

void loadLeftMortars() {
    if((leftMortars = sem_open(leftMortars_sem, 0)) == SEM_FAILED) {
        perror("sem_open: Can't create leftMortars semaphore");
        exit(-1);
    };
}

void loadRightMortars() {
    if((rightMortars = sem_open(rightMortars_sem, 0)) == SEM_FAILED) {
        perror("sem_open: Can't create rightMortars semaphore");
        exit(-1);
    };
}

// 0 false, 1 true
int isExist(int N, int coord) {
    //shm_unlink(field);
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
    
    int exist = 1;
    if (addr[coord].status == -1) {
        exist = 0;
    }

    if (close(fd) == -1) {
        perror("Error with closing acces to shared memory\n.");
        exit(-1);
    }
    
    return exist;
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
        if (addr[i].status == -1 || addr[i].status == 0) {
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

void Shoot(int N, int team, int pid) {
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
    
    while (1) {
        // На всякий случай проверяем, что игра еще не завершилась.
        int curStatus = gameResult(N);
        if (curStatus == 1 || curStatus == 2 || curStatus == -1) { // Игра завершилась, очищаем все процессы.
            printf("Процесс-оружия с координатами : (%d, %d) завершился.\n", pid / N, pid - (pid / N) * N);
            if (team == 1) { // Начало координации действий 1-ой команды.
                sem_post(leftMortars);
            } else if (team == 2) { // Начало координации действий 2 -ой команды.
                sem_post(rightMortars);
            }
            
            exit(0);
        }
        
        int iShoot = rand() % N;
        int jShoot = rand() % N;
        
        // Цель уже уничтожена.
        if (addr[iShoot * N + jShoot].status == -1) {
            continue;
        }
        
        // Пытаемся выстрелить в тиммейта.
        if (addr[iShoot * N + jShoot].status == team) {
            continue;
        }
        
        if (addr[iShoot * N + jShoot].status > 0) {
            sem_wait(printData); // Блокирую потоки, для корректного вывода информации.
            printf("Оружие с координатами : (%d, %d), убило процесс с координатами : (%d, %d).\n", pid / N, pid - (pid / N) * N, iShoot, jShoot);
            sem_post(printData); // Разблокирую все потоки.
        } else {
            sem_wait(printData); // Блокирую потоки, для корректного вывода информации.
            printf("Оружие с координатами : (%d, %d) уничтожило пустую клетку c координатами : (%d, %d).\n", pid / N, pid - (pid / N) * N, iShoot, jShoot);
            sem_post(printData); // Разблокирую все потоки.
        }
        
        addr[iShoot * N + jShoot].status = -1;
        break;
    }
    
    // Оружие выстрелило и ушло в перезарядку, затем оно перезарядится и будет снова готово к стрельбе, координируясь с командой.
    if (team == 1) { // Начало координации действий 1-ой команды.
        sem_post(leftMortars);
    } else if (team == 2) { // Начало координации действий 2 -ой команды.
        sem_post(rightMortars);
    }
    
    if (isExist(N, pid) == 0) {
        printf("Процесс с id : (%d, %d) завершился.\n", pid / N, pid - pid / N);
        exit(0);
    }
    
    sleep (2); // Имитация перезарядки орудия после выстрела.
    return;
}

void printField(int N) {
    //shm_unlink(field);
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
    for (int i = 0; i < N; ++i) {
        for (int j = 0; j < N; ++j) {
            printf("%d", addr[i * N + j].pid);
        }
        printf("\n");
    }
    
    if (close(fd) == -1) {
        perror("Error with closing acces to shared memory\n.");
        exit(-1);
    }
    
}

int main(int argc, char *argv[])
{
    if (argc != 4) {
        printf("you need to enter 1 argument: 1) Field size (N). 2) Team (1 - left, 2 - right). 3) Coordinate (X * N + Y)");
        exit(0);
    }
    
    int N = atoi(argv[1]);
    int team = atoi(argv[2]);
    int coord = atoi(argv[3]);

    loadPrintData();
    loadLeftMortars();
    loadRightMortars();
    
    sem_wait(printData);
    printf("Процесс-оружие с координатами: (%d, %d) и командой: %d - активировано.\n", coord / N, coord - (coord / N) * N, team);
    sem_post(printData);
    
    sleep(1);
    srand(time(NULL)); // для рандомной генерации.
    while (true) {
        if (isExist(N, coord) == 0) {
            sem_wait(printData);
            printf("Процесс-оружие с координатами: (%d, %d) завершился.\n", coord / N, coord - (coord / N) * N);
            sem_post(printData);
            exit(0);
        }
        
        // С помощью семафоров иметирую координацию пушек(стреляет одновременно только 1 оружие каждой команды).
        if (team == 1) { // Начало координации действий 1-ой команды.
            sem_wait(leftMortars);
        } else if (team == 2) { // Начало координации действий 2 -ой команды.
            sem_wait(rightMortars);
        }
        
        int curStatus = gameResult(N);
        if (curStatus == 1 || curStatus == 2 || curStatus == -1) { // Игра завершилась, очищаем все процессы.
            sem_wait(printData);
            printf("Процесс-оружие с координатами: (%d, %d) завершился.\n", coord / N, coord - (coord / N) * N);
            sem_post(printData);
            
            if (team == 1) { // Начало координации действий 1-ой команды.
                sem_post(leftMortars);
            } else if (team == 2) { // Начало координации действий 2 -ой команды.
                sem_post(rightMortars);
            }
            
            exit(0);
        }
        
        sem_wait(printData);
        printf("Оружие с координатами : (%d, %d), команда : %d готовится к выстрелу.\n", coord / N, coord - (coord / N) * N, team);
        sem_post(printData);
        
        // Поидее, если (team == -1), то мы сюда никогда не попадем, т.к. это значит, что процесс уже убит, но с чем черт не шутит...
        if (team != -1) {
            Shoot(N, team, coord);
        }
    }
    
    exit(0);
}

