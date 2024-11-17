#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/msg.h>
#include <unistd.h>

#define SHM_KEY 0x1234
#define SEM_KEY 0x5678
#define MSG_KEY 0x9ABC

int shm_id, sem_id, msg_id;
int *shm_ptr = NULL;

void lock_semaphore()
{
    struct sembuf sop = {0, -1, 0};
    if (semop(sem_id, &sop, 1) == -1)
    {
        perror("semop lock");
        exit(EXIT_FAILURE);
    }
}

void unlock_semaphore()
{
    struct sembuf sop = {0, 1, 0};
    if (semop(sem_id, &sop, 1) == -1)
    {
        perror("semop unlock");
        exit(EXIT_FAILURE);
    }
}

void cleanup()
{
    lock_semaphore();

    if (*shm_ptr == 1)
    {
        printf("Last process: Cleaning up resources.\n");
        unlock_semaphore();
        shmctl(shm_id, IPC_RMID, NULL);
        semctl(sem_id, 0, IPC_RMID);
        msgctl(msg_id, IPC_RMID, NULL);
    }
    else
    {
        (*shm_ptr)--;
        printf("Detached. Remaining processes: %d\n", *shm_ptr);
        unlock_semaphore();
    }

    shmdt(shm_ptr);
    exit(0);
}

void force_cleanup()
{
    printf("Force cleaning up all shared resources.\n");
    shmctl(shm_id, IPC_RMID, NULL);
    semctl(sem_id, 0, IPC_RMID);
    msgctl(msg_id, IPC_RMID, NULL);
    exit(0);
}

void handle_sigint(int sig)
{
    (void)sig;
    cleanup();
}

int main(int argc, char *argv[])
{
    if ((argc > 1) && (strcmp(argv[1], "--clean") == 0 || strcmp(argv[1], "-c") == 0))
    {
        shm_id = shmget(SHM_KEY, sizeof(int), 0666);
        sem_id = semget(SEM_KEY, 1, 0666);
        msg_id = msgget(MSG_KEY, 0666);

        if (shm_id == -1 || sem_id == -1 || msg_id == -1)
        {
            perror("Error accessing shared resources");
            exit(EXIT_FAILURE);
        }

        force_cleanup();
    }

    signal(SIGINT, handle_sigint);

    shm_id = shmget(SHM_KEY, sizeof(int), IPC_CREAT | 0666);
    if (shm_id == -1)
    {
        perror("shmget");
        exit(EXIT_FAILURE);
    }

    shm_ptr = (int *)shmat(shm_id, NULL, 0);
    if (shm_ptr == (void *)-1)
    {
        perror("shmat");
        exit(EXIT_FAILURE);
    }

    sem_id = semget(SEM_KEY, 1, IPC_CREAT | 0666);
    if (sem_id == -1)
    {
        perror("semget");
        exit(EXIT_FAILURE);
    }

    if (semctl(sem_id, 0, GETVAL) == 0)
    {
        semctl(sem_id, 0, SETVAL, 1);
    }

    msg_id = msgget(MSG_KEY, IPC_CREAT | 0666);
    if (msg_id == -1)
    {
        perror("msgget");
        exit(EXIT_FAILURE);
    }

    lock_semaphore();

    if (*shm_ptr == 0)
    {
        printf("First process: Initializing shared resources.\n");
    }

    (*shm_ptr)++;
    printf("Attached. Total processes: %d\n", *shm_ptr);

    unlock_semaphore();

    while (1)
    {
        printf("Process %d running...\n", getpid());
        sleep(1);
    }

    return 0;
}
