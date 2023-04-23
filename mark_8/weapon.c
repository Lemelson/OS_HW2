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

void loadPrintData() {
    key_t sem_key1 = ftok("/tmp", 1201);
    printDataID = semget(sem_key1, 1, 0666);
    if (printDataID < 0) {
        perror("Error with opening printData semaphore: ");
        exit(EXIT_FAILURE);
    }
}

void loadLeftMortars() {
    key_t sem_key2 = ftok("/tmp", 1202);
    leftMortarsID = semget(sem_key2, 1, 0666);
    if (leftMortarsID < 0) {
        perror("Error with opening leftMortar semaphore: ");
        exit(EXIT_FAILURE);
    }
}

void loadRightMortars() {
    key_t sem_key3 = ftok("/tmp", 1203);
    rightMortarsID = semget(sem_key3, 1, 0666);
    if (rightMortarsID < 0) {
        perror("Error with opening rightMortar semaphore: ");
        exit(EXIT_FAILURE);
    }
}

void wait_semaphore(int sem_id) {
    struct sembuf sem_op;
    sem_op.sem_num = 0;
    sem_op.sem_op = 0;
    sem_op.sem_flg = 0;
    semop(sem_id, &sem_op, 1);
    semctl(sem_id, 0, SETVAL, 1);
}

// 0 false, 1 true
int isExist(int N, int coord) {
    int exist = 1;
    if (field[coord].status == -1) {
        exist = 0;
    }
    return exist;
}

// Если игра еще идет, то возвращает 0, если победила левая команда 1, а если правая 2, если были уничтожены оба -1
int gameResult(int N) {
    bool leftAlive = false, rightAlive = false;
    for (int i = 0; i < N * N; ++i) {
        if (field[i].status == -1 || field[i].status == 0) {
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

void Shoot(int N, int team, int pid) {
    while (1) {
        // На всякий случай проверяем, что игра еще не завершилась.
        int curStatus = gameResult(N);
        if (curStatus == 1 || curStatus == 2 || curStatus == -1) { // Игра завершилась, очищаем все процессы.
            printf("Процесс-оружия с координатами : (%d, %d) завершился.\n", pid / N, pid - (pid / N) * N);
            if (team == 1) { // Начало координации действий 1-ой команды.
                semctl(leftMortarsID, 0, SETVAL, 0);
            } else if (team == 2) { // Начало координации действий 2 -ой команды.
                semctl(rightMortarsID, 0, SETVAL, 0);
            }
            
            exit(0);
        }
        
        int iShoot = rand() % N;
        int jShoot = rand() % N;
        
        // Цель уже уничтожена.
        if (field[iShoot * N + jShoot].status == -1) {
            continue;
        }
        
        // Пытаемся выстрелить в тиммейта.
        if (field[iShoot * N + jShoot].status == team) {
            continue;
        }
        
        if (field[iShoot * N + jShoot].status > 0) {
            wait_semaphore(printDataID); // Блокирую потоки, для корректного вывода информации.
            printf("Оружие с координатами : (%d, %d), убило процесс с координатами : (%d, %d).\n", pid / N, pid - (pid / N) * N, iShoot, jShoot);
            semctl(printDataID, 0, SETVAL, 0); // Разблокирую все потоки.
        } else {
            wait_semaphore(printDataID); // Блокирую потоки, для корректного вывода информации.
            printf("Оружие с координатами : (%d, %d) уничтожило пустую клетку c координатами : (%d, %d).\n", pid / N, pid - (pid / N) * N, iShoot, jShoot);
            semctl(printDataID, 0, SETVAL, 0); // Разблокирую все потоки.
        }
        
        field[iShoot * N + jShoot].status = -1;
        break;
    }
    
    // Оружие выстрелило и ушло в перезарядку, затем оно перезарядится и будет снова готово к стрельбе, координируясь с командой.
    if (team == 1) { // Начало координации действий 1-ой команды.
        semctl(leftMortarsID, 0, SETVAL, 0);
    } else if (team == 2) { // Начало координации действий 2 -ой команды.
        semctl(rightMortarsID, 0, SETVAL, 0);
    }
    
    if (isExist(N, pid) == 0) {
        printf("Процесс с id : (%d, %d) завершился.\n", pid / N, pid - pid / N);
        exit(0);
    }
    
    sleep (2); // Имитация перезарядки орудия после выстрела.
    return;
}

void printField(int N) {
    bool leftAlive = false, rightAlive = false;
    for (int i = 0; i < N; ++i) {
        for (int j = 0; j < N; ++j) {
            printf("%d", field[i * N + j].pid);
        }
        printf("\n");
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

    key_t shm_key = ftok("/tmp", 1200);
    field_fd  = shmget(shm_key, sizeof(struct Cell), 0666);
    if (field_fd == -1) {
        perror("shmget error");
        exit(-1);
    }

    field = shmat(field_fd, NULL, 0);
    if (field == (void*)-1) {
        perror("shmat error");
        exit(-1);
    }
    
    loadPrintData();
    loadLeftMortars();
    loadRightMortars();
    
    wait_semaphore(printDataID);
    printf("Процесс-оружие с координатами: (%d, %d) и командой: %d - активировано.\n", coord / N, coord - (coord / N) * N, team);
    semctl(printDataID, 0, SETVAL, 0);
    
    wait_semaphore(printDataID);
    printField(N);
    sleep(5);
    semctl(printDataID, 0, SETVAL, 0);
    
    sleep(5);
    srand(time(NULL)); // для рандомной генерации.
    while (true) {
        if (isExist(N, coord) == 0) {
            wait_semaphore(printDataID);
            printf("Процесс-оружие с координатами: (%d, %d) завершился.\n", coord / N, coord - (coord / N) * N);
            semctl(printDataID, 0, SETVAL, 0);
            exit(0);
        }
        
        // С помощью семафоров иметирую координацию пушек(стреляет одновременно только 1 оружие каждой команды).
        if (team == 1) { // Начало координации действий 1-ой команды.
            wait_semaphore(leftMortarsID);
        } else if (team == 2) { // Начало координации действий 2 -ой команды.
            wait_semaphore(rightMortarsID);
        }
        
        int curStatus = gameResult(N);
        if (curStatus == 1 || curStatus == 2 || curStatus == -1) { // Игра завершилась, очищаем все процессы.
            wait_semaphore(printDataID);
            printf("Процесс-оружие с координатами: (%d, %d) завершился.\n", coord / N, coord - (coord / N) * N);
            semctl(printDataID, 0, SETVAL, 0);
            
            if (team == 1) { // Начало координации действий 1-ой команды.
                semctl(leftMortarsID, 0, SETVAL, 0);
            } else if (team == 2) { // Начало координации действий 2 -ой команды.
                semctl(rightMortarsID, 0, SETVAL, 0);
            }
            
            exit(0);
        }
        
        wait_semaphore(printDataID);
        printf("Оружие с координатами : (%d, %d), команда : %d готовится к выстрелу.\n", coord / N, coord - (coord / N) * N, team);
        semctl(printDataID, 0, SETVAL, 0);
        
        // Поидее, если (team == -1), то мы сюда никогда не попадем, т.к. это значит, что процесс уже убит, но с чем черт не шутит...
        if (team != -1) {
            Shoot(N, team, coord);
        }
    }
    
    exit(0);
}

