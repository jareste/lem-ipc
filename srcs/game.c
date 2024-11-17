#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <globals.h>

#define SHM_MATRIX_KEY 0x56789

struct game_state
{
    int current_team;
    int current_player_pid;
    int round_number;
};

static struct game_state *game = NULL;
static int *team_members[MAX_TEAMS] = {NULL};
static int shm_matrix_id;
static int *shared_matrix;
#define MATRIX(row, col) (shared_matrix[(row) * WIDTH + (col)])

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

void init_shared_matrix()
{
    size_t matrix_size = WIDTH * HEIGHT * sizeof(int);

    shm_matrix_id = shmget(SHM_MATRIX_KEY, matrix_size, IPC_CREAT | 0666);
    if (shm_matrix_id == -1)
    {
        perror("shmget (matrix)");
        exit(EXIT_FAILURE);
    }

    shared_matrix = (int *)shmat(shm_matrix_id, NULL, 0);
    if (shared_matrix == (void *)-1)
    {
        perror("shmat (matrix)");
        exit(EXIT_FAILURE);
    }

    lock_semaphore();
    struct shmid_ds shm_info;
    shmctl(shm_matrix_id, IPC_STAT, &shm_info);
    if (shm_info.shm_nattch == 1)
    {
        for (int i = 0; i < WIDTH * HEIGHT; i++)
        {
            shared_matrix[i] = 0;
        }
        printf("Shared matrix initialized (ID: %d, Size: %ld bytes).\n", shm_matrix_id, matrix_size);
    }
    unlock_semaphore();
}


int update_matrix_element(int row, int col, int value)
{
    if (MATRIX(row, col) != 0)
    {
        printf("Matrix[%d][%d] is already occupied.\n", row, col);
        return -1;
    } 
    printf("Matrix value: %d\n", MATRIX(row, col));
    MATRIX(row, col) = value;
    printf("Updated matrix[%d][%d] to %d.\n", row, col, value);

    return 1;
}

void print_matrix()
{
    for (int r = 0; r < HEIGHT; r++)
    {
        for (int c = 0; c < WIDTH; c++)
        {
            printf("%d ", MATRIX(r, c));
        }
        printf("\n");
    }
}

void cleanup_shared_matrix()
{
    if (shmdt(shared_matrix) == -1)
    {
        perror("shmdt (matrix)");
    }

    if (shmctl(shm_matrix_id, IPC_RMID, NULL) == -1)
    {
        perror("shmctl (matrix)");
    }

    printf("Shared matrix cleaned up.\n");
}

void place_player_random(int team)
{
    int row = rand() % HEIGHT;
    int col = rand() % WIDTH;
    printf("Placing player %d from Team %d at matrix[%d][%d].\n", getpid(), team, row, col);
    if (update_matrix_element(row, col, team) == -1)
    {
        place_player_random(team);
    }
}

void actual_play(int team)
{
    int played_round = 0;
    printf("Player %d from Team %d has joined the game.\n", getpid(), team);

    while (1)
    {
        lock_semaphore();

        int registered = 0;
        for (int i = 0; i < MAX_PROCESSES; i++)
        {
            if (team_members[team][i] == getpid())
            {
                registered = 1;
                break;
            }
        }

        if (!registered)
        {
            for (int i = 0; i < MAX_PROCESSES; i++)
            {
                if (team_members[team][i] == 0)
                {
                    team_members[team][i] = getpid();
                    break;
                }
            }
        }

        printf("current_team: %d\n", game->current_team);
        if (game->current_team == team)
        {
            played_round++;
            printf("played_round: %d\n", played_round);


            print_matrix();

            do {
                game->current_team = (game->current_team + 1) % MAX_TEAMS;
            } while (team_members[game->current_team][0] == 0); // Skip empty teams

            for (int i = 0; i < MAX_PROCESSES; i++)
            {
                if (team_members[game->current_team][i] != 0)
                {
                    game->current_player_pid = team_members[game->current_team][i];
                    break;
                }
            }

            if (game->current_team == 0)
            {
                game->round_number++;
            }

        }
        else
        {
            // printf("Player %d from Team %d is waiting for their turn.\n", getpid(), team);
            // printf("Current team: %d, Current player: %d\n", game->current_team, game->current_player_pid);
        }

        unlock_semaphore();
        sleep(1);
    }
}

void play_game(int team)
{
    game = (struct game_state *)shmat(shm_id, NULL, 0);
    if (game == (void *)-1)
    {
        perror("shmat");
        exit(EXIT_FAILURE);
    }

    for (int i = 0; i < MAX_TEAMS; i++)
    {
        team_members[i] = (int *)(game + 1) + i * MAX_PROCESSES;
    }

    printf("Game started. Team %d is playing.\n", team);

    lock_semaphore();
    if (*shm_ptr == 0)
    {
        game->current_team = 0;
        game->current_player_pid = 0;
        game->round_number = 1;

        for (int i = 0; i < MAX_TEAMS; i++)
        {
            for (int j = 0; j < MAX_PROCESSES; j++)
            {
                team_members[i][j] = 0;
            }
        }
    }
    unlock_semaphore();

    init_shared_matrix();

    place_player_random(team);

    actual_play(team);
}
