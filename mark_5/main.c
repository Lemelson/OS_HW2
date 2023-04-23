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
void fillField(int N, int pid, int status) {
    while (1) {
        int i = rand() % N;
        int j = rand() % N;
        
        // В этой ячейке уже есть орудие.
        if (field[i * N + j].status == 1 || field[i * N + j].status == 2) {
            continue;
        }
        
        field[i * N + j].pid = pid; // Сохраняем pid процесса-орудия, соответствующего данной клетке.
        field[i * N + j].status = status;
        break;
    }
}

void initPrintData() {
    int fd = shm_open("printData-semaphore", O_RDWR | O_CREAT, 0666);
    ftruncate(fd, sizeof(sem_t));
    printData = mmap(0, sizeof(sem_t), PROT_WRITE|PROT_READ, MAP_SHARED, fd, 0);
    sem_init(printData, 1, 1);
    
    if (close(fd) == -1) {
        perror("Error with closing arr_fd.");
        exit(-1);
    }
}

void initLeftMortars() {
    int fd = shm_open("leftMortars-semaphore", O_RDWR | O_CREAT, 0666);
    ftruncate(fd, sizeof(sem_t));
    leftMortars = mmap(0, sizeof(sem_t), PROT_WRITE|PROT_READ, MAP_SHARED, fd, 0);
    sem_init(leftMortars, 1, 1);
    
    if (close(fd) == -1) {
        perror("Error with closing arr_fd.");
        exit(-1);
    }
}

void initRightMortars() {
    int fd = shm_open("rightMortars-semaphore", O_RDWR | O_CREAT, 0666);
    ftruncate(fd, sizeof(sem_t));
    rightMortars = mmap(0, sizeof(sem_t), PROT_WRITE|PROT_READ, MAP_SHARED, fd, 0);
    sem_init(rightMortars, 1, 1);
    
    if (close(fd) == -1) {
        perror("Error with closing arr_fd.");
        exit(-1);
    }
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

// 0 false, 1 true
int isExist(int N) {
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
    
    int exist = 0;
    for (int i = 0; i < N * N; ++i) {
        if (addr[i].pid == getpid()) {
            exist = 1;
        }
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

// Ищет какой команде принадлежит процесса-орудия.
int findType(int N) {
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
    
    /*
    for (int i = 0; i < N; ++i) {
        for (int j = 0; j < N; ++j) {
            printf("%d", addr[i][j].pid);
        }
        printf("\n");
    }
    */
    
    for (int i = 0; i < N * N; ++i) {
        int pid = getpid();
        int checkPid = addr[i].pid;
        if (pid == checkPid) {
            if (close(fd) == -1) {
                perror("Error with closing acces to shared memory\n.");
                exit(-1);
            } 
            return addr[i].status;
        } 
    }
    
    if (close(fd) == -1) {
        perror("Error with closing acces to shared memory\n.");
        exit(-1);
    }
    return -1;
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
            printf("Proc with id : %d finished\n", getpid());
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
        
        // Пытаемся выстрелить сами в себя.
        if (addr[iShoot * N + jShoot].pid == pid) {
            continue;
        }
        
        // Пытаемся выстрелить в тиммейта.
        if (addr[iShoot * N + jShoot].status == team) {
            continue;
        }
        
        if (addr[iShoot * N + jShoot].pid != -1) {
            sem_wait(printData); // Блокирую потоки, для корректного вывода информации.
            printf("Процесс с id : %d, убил процесс с id : %d\n", pid, addr[iShoot * N + jShoot].pid);
            sem_post(printData); // Разблокирую все потоки.
            
            addr[iShoot * N + jShoot].pid = -1; // Помечаем уничтоженным.
        } else {
            sem_wait(printData); // Блокирую потоки, для корректного вывода информации.
            printf("Процесс с id : %d уничтожил пустую клетку(промазал)\n", pid);
            sem_post(printData); // Разблокирую все потоки.
           
            addr[iShoot * N + jShoot].status = -1;
        }
        
        break;
    }
    
    // Оружие выстрелило и ушло в перезарядку, затем оно перезарядится и будет снова готово к стрельбе, координируясь с командой.
    if (team == 1) { // Начало координации действий 1-ой команды.
        sem_post(leftMortars);
    } else if (team == 2) { // Начало координации действий 2 -ой команды.
        sem_post(rightMortars);
    }
    
    if (isExist(N) == 0) {
        printf("Proc with id : %d finished\n", getpid());
        exit(0);
    }
    
    sleep (2); // Имитация перезарядки орудия после выстрела.
    return;
}

void unlinkAll() {
    if(shm_unlink(fieldName) == -1) {
        printf("Shared memory is absent\n");
        perror("shm_unlink");
    }
    
    sem_destroy(printData);
    sem_destroy(leftMortars);
    sem_destroy(rightMortars);
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
    if (argc != 3) {
        printf("you need to enter 2 arguments: 1) size of the battlefield; 2) count of the mortars.");
        exit(0);
    }
    
    printf("Main process PID : %d\n", getpid());
    printf("Program successfully started!.\n");
    
    int N = atoi(argv[1]); // Размер поля боя.
    initPrintData();
    initLeftMortars();
    initRightMortars();
    initField(N); // Создаем предварительное поле боя.
    
    int mortarsCount = 2 * atoi(argv[2]); // Количество минометов(умноженное на 2, т.к. сражаются 2 стороны).
    srand(time(NULL)); // для рандомной генерации.
    
    int mainPid = getpid(); // pid главного процесса, создающего n других.
    for (int i = 0; i < mortarsCount; ++i) {
        if (mainPid == getpid()) { // Если это главный процесс, то создаем его дочерний.
            int pid = fork_();
            if (pid > 0) { // родительский процесс.
                if (i < mortarsCount / 2) {
                    fillField(N, pid, 1); // левая команда.
                } else {
                    fillField(N, pid, 2); // правая команда.
                }
                
                // Поле полностью заполнено и пора добавить его в разделяемые ресурсы.
                if (i == mortarsCount - 1) {
                    printf("Started field:\n");
                    for (int i = 0; i < N; ++i) {
                        for (int j = 0; j < N; ++j) {
                            printf("%d", field[i * N + j].status);
                        }
                        printf("\n");
                    }
                    
                    addFieldToSharedMemory(N);
                    printField(N);
                }
            } else {
                int cnt = 0;
                for (int i = 0; i < 2e8; ++i) {
                    ++cnt; // Хитрость, для того, чтобы основной процесс успел создать все дочерние.
                }
            }
        }
    }
    
    /*
    sem_wait(printData);
    printf("PID : %d\n", getpid());
    printf("----------------\n");
    printField(N);
    printf("----------------\n");
    sem_post(printData);
    */
    
    while (wait(NULL) > 0) { // Ждем пока поле полностью не будет очищено(битва не завершится).
    }

    if (getpid() != mainPid) { // Если это родительский процесс, то бива окончена, и надо очистить ресурсы, иначе сражение только начинается.
        // Таким образом к текущему моменту создано 1 + 2 * mortarsCount процессов, где 1 - изначальный, и 2 * mortarsCount - процессы мортиры, его дети.
        int team = findType(N);
        while (true) {
            if (isExist(N) == 0) {
                printf("Proc with id : %d finished\n", getpid());
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
                printf("Proc with id : %d finished\n", getpid());
                if (team == 1) { // Начало координации действий 1-ой команды.
                    sem_post(leftMortars);
                } else if (team == 2) { // Начало координации действий 2 -ой команды.
                    sem_post(rightMortars);
                }
                
                exit(0);
            }
            
            printf("Cur id : %d, team : %d prepairs for shooting \n", getpid(), team);
            // Поидее, если (team == -1), то мы сюда никогда не попадем, т.к. это значит, что процесс уже убит, но с чем черт не шутит...
            if (team != -1) {
                Shoot(N, team, getpid());
            }
        }
    }
    
    int result = gameResult(N);
    if (result == 1) {
        printf("Победила Анчуария!!!!");
    } else if (result == 2) {
        printf("Победила Тарантерия!!!!");
    } else {
        printf("0_0 НИЧЬЯ 0_0");
    }
    
    unlinkAll();
    return 0;
}

