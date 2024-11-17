#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/msg.h>
#include <unistd.h>
#include <errno.h>

#include <ft_malloc.h>
#include <lem_ipc.h>
#include <globals.h>

#define SHM_KEY 0x1234
#define SEM_KEY 0x5678
#define MSG_KEY_BASE 0x9ABC

int shm_id, sem_id;
int *shm_ptr = NULL;
int msg_ids[MAX_TEAMS] = {0};

struct message
{
    long mtype;
    char mtext[128];
};

static void lock_semaphore()
{
    struct sembuf sop = {0, -1, 0};
    if (semop(sem_id, &sop, 1) == -1)
    {
        perror("semop lock");
        exit(EXIT_FAILURE);
    }
}

static void unlock_semaphore()
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
        printf("\nLast process: Cleaning up resources.\n");
        shmctl(shm_id, IPC_RMID, NULL);
        semctl(sem_id, 0, IPC_RMID);
        for (int i = 0; i < MAX_TEAMS; i++)
        {
            if (msg_ids[i] != 0)
            {
                msgctl(msg_ids[i], IPC_RMID, NULL);
            }
        }
        shmdt(shm_ptr);
        cleanup_shared_matrix();
    }
    else
    {
        (*shm_ptr)--;
        printf("\nDetached. Remaining processes: %d\n", *shm_ptr);
        shmdt(shm_ptr);
        unlock_semaphore();
    }
    exit(0);
}

void force_cleanup()
{
    printf("Force cleaning up all shared resources.\n");
    shmctl(shm_id, IPC_RMID, NULL);
    semctl(sem_id, 0, IPC_RMID);
    for (int i = 0; i < MAX_TEAMS; i++)
    {
        if (msg_ids[i] != 0)
        {
            msgctl(msg_ids[i], IPC_RMID, NULL);
        }
    }
    cleanup_shared_matrix();
    exit(0);
}

void handle_sigint(int sig)
{
    (void)sig; // Suppress unused warning
    cleanup();
}

int initialize_msg_queues()
{
    for (int i = 0; i < MAX_TEAMS; i++)
    {
        msg_ids[i] = msgget(MSG_KEY_BASE + i, IPC_CREAT | 0666);
        if (msg_ids[i] == -1)
        {
            perror("msgget");
            return -1;
        }
    }
    return 0;
}

void send_message(int team, const char *message, long mtype)
{
    if (team < 0 || team >= MAX_TEAMS)
    {
        fprintf(stderr, "Invalid team number.\n");
        return;
    }

    struct message msg;
    msg.mtype = mtype;
    strncpy(msg.mtext, message, sizeof(msg.mtext));

    if (msgsnd(msg_ids[team], &msg, sizeof(msg.mtext), 0) == -1)
    {
        perror("msgsnd");
    }
}

void broadcast_message(int team, const char *message)
{
    if (team < 0 || team >= MAX_TEAMS)
    {
        fprintf(stderr, "Invalid team number.\n");
        return;
    }

    struct message msg;
    strncpy(msg.mtext, message, sizeof(msg.mtext));

    for (int i = 0; i < MAX_TEAMS; i++)
    {
        msg.mtype = i + 1; // Use process-specific mtype
        if (msgsnd(msg_ids[team], &msg, sizeof(msg.mtext), 0) == -1)
        {
            perror("msgsnd (broadcast)");
        }
    }
}

void receive_message(int team, long mtype)
{
    if (team < 0 || team >= MAX_TEAMS)
    {
        fprintf(stderr, "Invalid team number.\n");
        return;
    }

    struct message msg;
    if (msgrcv(msg_ids[team], &msg, sizeof(msg.mtext), mtype, IPC_NOWAIT) == -1)
    {
        if (errno != ENOMSG)
        {
            perror("msgrcv");
        }
    }
    else
    {
        printf("Received from team %d: %s\n", team, msg.mtext);
    }
}

void get_team_number(char *s, int *team)
{
    if (strlen(s) > 1)
        goto error;
    
    if (s[0] < '0' || s[0] > '9')
        goto error;
    
    *team = atoi(s);

    /* should never happen lol */
    if (*team < 0 || *team >= MAX_TEAMS)
        goto error;
    
    return;
error:
    fprintf(stderr, "Invalid team number. Valids are [0 - 9]\n");
    exit(EXIT_FAILURE);
}

void init()
{
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

    if (initialize_msg_queues() == -1)
    {
        cleanup();
    }
}


int main(int argc, char *argv[])
{
    int team = 0;

    if (argc < 2)
    {
        fprintf(stderr, "Usage: %s [--clean] [team]\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    if ((strcmp(argv[1], "--clean") == 0 ) || (strcmp(argv[1], "-c") == 0))
    {
        shm_id = shmget(SHM_KEY, sizeof(int), 0666);
        sem_id = semget(SEM_KEY, 1, 0666);
        for (int i = 0; i < MAX_TEAMS; i++)
        {
            msg_ids[i] = msgget(MSG_KEY_BASE + i, 0666);
        }

        if (shm_id == -1 || sem_id == -1)
        {
            perror("Error accessing shared resources");
            exit(EXIT_FAILURE);
        }

        force_cleanup();
    }

    get_team_number(argv[1], &team);

    signal(SIGINT, handle_sigint);

    init();

    lock_semaphore();

    /* trash */
    if (*shm_ptr == 0)
    {
        printf("First process: Initializing shared resources.\n");
    }
    /* trash */

    (*shm_ptr)++;
    /* trash */
    printf("Attached. Total processes: %d\n", *shm_ptr);
    /* trash */
    unlock_semaphore();

    /* trash */
    printf("Joined team %d\n", team);
    /* trash */

    /* trash */
    char join_message[128];
    snprintf(join_message, sizeof(join_message), "Process %d has joined the team.", getpid());
    send_message(team, join_message, 1);
    /* trash */

    printf("Joining team %d\n", team);

    play_game(team);

    return 0;
}
