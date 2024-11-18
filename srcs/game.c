#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <globals.h>
#include <math.h>
#include <string.h>
#include <lem_ipc.h>

#define SHM_GAME_KEY 0xf001
#define SHM_MATRIX_KEY 0x56789

struct game_state
{
    int current_team;
    int current_player_pid;
    int game_started;
};

static struct game_state *game = NULL;
static int *team_members[MAX_TEAMS] = {NULL};
static int shm_matrix_id;
static int *shared_matrix;
static int my_position[2];
static int game_shm_id;
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
    if (row < 0 || row >= HEIGHT || col < 0 || col >= WIDTH)
    {
        return -1;
    }
    if (MATRIX(row, col) != 0)
    {
        return -1;
    } 
    MATRIX(row, col) = value;
    
    return 1;
}

void print_matrix()
{
    printf("\033[H\033[J");

    printf("Game Board:\n");
    for (int r = 0; r < HEIGHT; r++)
    {
        for (int c = 0; c < WIDTH; c++)
        {
            if (MATRIX(r, c) == 0)
            {
                printf(". ");
            }
            else if (MATRIX(r, c) == 1)
            {
                printf("\033[31m1 \033[0m"); // Team 1 (red)
            }
            else if (MATRIX(r, c) == 2)
            {
                printf("\033[34m2 \033[0m"); // Team 2 (blue)
            }
            else if (MATRIX(r, c) == 3)
            {
                printf("\033[32m3 \033[0m"); // Team 3 (green)
            }
            else if (MATRIX(r, c) == 4)
            {
                printf("\033[33m4 \033[0m"); // Team 4 (yellow)
            }
            else if (MATRIX(r, c) == 5)
            {
                printf("\033[35m5 \033[0m"); // Team 5 (purple)
            }
            else if(MATRIX(r, c) == 6)
            {
                printf("\033[36m6 \033[0m"); // Team 6 (cyan)
            }
            else if (MATRIX(r, c) == 7)
            {
                printf("\033[37m7 \033[0m"); // Team 7 (white)
            }
            else if (MATRIX(r, c) == 8)
            {
                printf("\033[91m8 \033[0m"); // Team 8 (light red)
            }
            else if (MATRIX(r, c) == 9)
            {
                printf("\033[94m9 \033[0m"); // Team 9 (light blue)
            }
            else
            {
                printf("? ");
            }
        }
        printf("\n");
    }

    fflush(stdout);
}

void restore_player_position()
{
    if (my_position[0] < 0 || my_position[0] >= HEIGHT || my_position[1] < 0 || my_position[1] >= WIDTH)
        return;
    printf("restoring my position [%d][%d]\n", my_position[0], my_position[1]);
    MATRIX(my_position[0], my_position[1]) = 0;
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

void place_player_first_spot(int team)
{
    for (int r = 0; r < HEIGHT; r++)
    {
        for (int c = 0; c < WIDTH; c++)
        {
            if (MATRIX(r, c) == 0)
            {
                MATRIX(r,c) = team;
                my_position[0] = r;
                my_position[1] = c;
                return;
            }
        }
    }

    my_position[0] = -1;
    my_position[1] = -1;
    fprintf(stderr, "Unable to set an initial position for player...\n");
    cleanup();
    exit(EXIT_FAILURE);
}

void place_player_random(int team)
{
    static int tries = 0;
    int row = rand() % HEIGHT;
    int col = rand() % WIDTH;
    if (update_matrix_element(row, col, team) == -1)
    {
        tries++;
        if (tries == 5)
            place_player_first_spot(team);
        else
            place_player_random(team);
    }
    else
    {
        my_position[0] = row;
        my_position[1] = col;
    }
}

int move_player(int new_row, int new_col, int team)
{
    if (update_matrix_element(new_row, new_col, team) == 1)
    {
        printf("Player %d from Team %d moved from [%d][%d] to [%d][%d].\n", getpid(), team, my_position[0], my_position[1], new_row, new_col);
        MATRIX(my_position[0], my_position[1]) = 0;
        my_position[0] = new_row;
        my_position[1] = new_col;
        return 1;
    }
    return -1;
}

void move_player_one_square_random(int team)
{
    int directions[4][2] = {
        {-1, 0}, // Up
        {1, 0},  // Down
        {0, -1}, // Left
        {0, 1}   // Right
    };

    /* Shuffle the directions array to randomize movement */
    for (int i = 0; i < 4; i++)
    {
        int j = rand() % 4;
        int temp[2] = {directions[i][0], directions[i][1]};
        directions[i][0] = directions[j][0];
        directions[i][1] = directions[j][1];
        directions[j][0] = temp[0];
        directions[j][1] = temp[1];
    }

    for (int i = 0; i < 4; i++)
    {
        int new_row = my_position[0] + directions[i][0];
        int new_col = my_position[1] + directions[i][1];

        if (new_row >= 0 && new_row < HEIGHT && new_col >= 0 && new_col < WIDTH)
        {
            if (MATRIX(new_row, new_col) == 0)
            {
                MATRIX(my_position[0], my_position[1]) = 0; /* Clear current position */
                MATRIX(new_row, new_col) = team;            /* Mark new position with the team */
                my_position[0] = new_row;
                my_position[1] = new_col;

                printf("Player %d from Team %d moved to [%d, %d].\n", getpid(), team, new_row, new_col);
                return;
            }
        }
    }

    printf("Player %d from Team %d could not move.\n", getpid(), team);
}

int manhattan_distance(int x1, int y1, int x2, int y2)
{
    return abs(x1 - x2) + abs(y1 - y2);
}

int move_towards_nearest_opponent(int team)
{
    int nearest_distance = WIDTH * HEIGHT;
    int target_row = -1, target_col = -1;

    for (int r = 0; r < HEIGHT; r++)
    {
        for (int c = 0; c < WIDTH; c++)
        {
            if (MATRIX(r, c) != 0 && MATRIX(r, c) != team)
            {
                int distance = manhattan_distance(my_position[0], my_position[1], r, c);
                if (distance < nearest_distance)
                {
                    nearest_distance = distance;
                    target_row = r;
                    target_col = c;
                }
            }
        }
    }

    if (target_row == -1 || target_col == -1)
    {
        printf("No opponents nearby for Team %d at [%d, %d].\n", team, my_position[0], my_position[1]);
        return 2;
    }

    int new_row = my_position[0];
    int new_col = my_position[1];

    if (my_position[0] < target_row)
    {
        new_row++; /* Move down */
    }
    else if (my_position[0] > target_row)
    {
        new_row--; /* Move up */
    }
    else if (my_position[1] < target_col)
    {
        new_col++; /* Move right */
    }
    else if (my_position[1] > target_col)
    {
        new_col--; /* Move left */
    }

    if (move_player(new_row, new_col, team) == 1)
    {
        printf("Player %d from Team %d moved towards opponent at [%d, %d].\n", getpid(), team, target_row, target_col);
    }
    else
    {
        /* players must allways move! */
        move_player_one_square_random(team);
    }
    return 1;
}

int have_i_lost()
{
    if (MATRIX(my_position[0], my_position[1]) == 0)
    {
        printf("player was at [%d, %d]\n", my_position[0], my_position[1]);
        return 1;
    }

    return -1;
}

int have_i_won(int team)
{
    for (int r = 0; r < HEIGHT; r++)
    {
        for (int c = 0; c < WIDTH; c++)
        {
            if (MATRIX(r, c) != 0 && MATRIX(r, c) != team)
            {
                return -1;
            }
        }
    }

    return 1;
}

void check_captured_enemy(int team)
{
    int enemy_team;
    int my_team;

    /* x+ */
    if (my_position[0] < HEIGHT - 2)
    {
        enemy_team = MATRIX(my_position[0] + 1, my_position[1]);
        my_team = MATRIX(my_position[0] + 2, my_position[1]);
        if ((enemy_team != 0) && (my_team == team) && (enemy_team != team))
        {
            printf("Player %d from Team %d captured an enemy at [%d, %d].\n", getpid(), team, my_position[0] + 1, my_position[1]);
            MATRIX(my_position[0] + 1, my_position[1]) = 0;
        }
    }

    /* x- */
    if (my_position[0] > 1)
    {
        enemy_team = MATRIX(my_position[0] - 1, my_position[1]);
        my_team = MATRIX(my_position[0] - 2, my_position[1]);
        if ((enemy_team != 0) && (my_team == team) && (enemy_team != team))
        {
            printf("Player %d from Team %d captured an enemy at [%d, %d].\n", getpid(), team, my_position[0] - 1, my_position[1]);
            MATRIX(my_position[0] - 1, my_position[1]) = 0;
        }
    }

    /* y+ */
    if (my_position[1] < WIDTH - 2)
    {
        enemy_team = MATRIX(my_position[0], my_position[1] + 1);
        my_team = MATRIX(my_position[0], my_position[1] + 2);
        if ((enemy_team != 0) && (my_team == team) && (enemy_team != team))
        {
            printf("Player %d from Team %d captured an enemy at [%d, %d].\n", getpid(), team, my_position[0], my_position[1] + 1);
            MATRIX(my_position[0], my_position[1] + 1) = 0;
        }
    }

    /* y- */
    if (my_position[1] > 1)
    {
        enemy_team = MATRIX(my_position[0], my_position[1] - 1);
        my_team = MATRIX(my_position[0], my_position[1] - 2);
        if ((enemy_team != 0) && (my_team == team) && (enemy_team != team))
        {
            printf("Player %d from Team %d captured an enemy at [%d, %d].\n", getpid(), team, my_position[0], my_position[1] - 1);
            MATRIX(my_position[0], my_position[1] - 1) = 0;
        }
    }

    /* x+, y+ */
    if (my_position[0] < HEIGHT - 2 && my_position[1] < WIDTH - 2)
    {
        enemy_team = MATRIX(my_position[0] + 1, my_position[1] + 1);
        my_team = MATRIX(my_position[0] + 2, my_position[1] + 2);
        if ((enemy_team != 0) && (my_team == team) && (enemy_team != team))
        {
            printf("Player %d from Team %d captured an enemy at [%d, %d].\n", getpid(), team, my_position[0] + 1, my_position[1] + 1);
            MATRIX(my_position[0] + 1, my_position[1] + 1) = 0;
        }
    }

    /* x+, y- */
    if (my_position[0] < HEIGHT - 2 && my_position[1] > 1)
    {
        enemy_team = MATRIX(my_position[0] + 1, my_position[1] - 1);
        my_team = MATRIX(my_position[0] + 2, my_position[1] - 2);
        if ((enemy_team != 0) && (my_team == team) && (enemy_team != team))
        {
            printf("Player %d from Team %d captured an enemy at [%d, %d].\n", getpid(), team, my_position[0] + 1, my_position[1] - 1);
            MATRIX(my_position[0] + 1, my_position[1] - 1) = 0;
        }
    }

    /* x-, y+ */
    if (my_position[0] > 1 && my_position[1] < WIDTH - 2)
    {
        enemy_team = MATRIX(my_position[0] - 1, my_position[1] + 1);
        my_team = MATRIX(my_position[0] - 2, my_position[1] + 2);
        if ((enemy_team != 0) && (my_team == team) && (enemy_team != team))
        {
            printf("Player %d from Team %d captured an enemy at [%d, %d].\n", getpid(), team, my_position[0] - 1, my_position[1] + 1);
            MATRIX(my_position[0] - 1, my_position[1] + 1) = 0;
        }
    }

    /* x-, y- */
    if (my_position[0] > 1 && my_position[1] > 1)
    {
        enemy_team = MATRIX(my_position[0] - 1, my_position[1] - 1);
        my_team = MATRIX(my_position[0] - 2, my_position[1] - 2);
        if ((enemy_team != 0) && (my_team == team) && (enemy_team != team))
        {
            printf("Player %d from Team %d captured an enemy at [%d, %d].\n", getpid(), team, my_position[0] - 1, my_position[1] - 1);
            MATRIX(my_position[0] - 1, my_position[1] - 1) = 0;
        }
    }
}


void has_game_started()
{
    int team_has_players[MAX_TEAMS];
    int total_teams = 0;

    if (game->game_started == 1)
        return;

    memset(team_has_players, 0, sizeof(team_has_players));

    for (int r = 0; r < HEIGHT; r++)
    {
        for (int c = 0; c < WIDTH; c++)
        {
            if (MATRIX(r, c) != 0)
            {
                team_has_players[MATRIX(r, c)] += 1;
                if (team_has_players[MATRIX(r, c)] == 1)
                {
                    total_teams++;
                }
                if (total_teams > 1)
                {
                    game->game_started = 1;
                    return;
                }
            }
        }
    }
    return;
}

void register_player(int team)
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

    has_game_started();

    unlock_semaphore();
}

void actual_play(int team)
{
    register_player(team);

    while (1)
    {
        lock_semaphore();

        if (game->game_started == 0)
        {
            print_matrix();
            printf("Waiting for game to start...\n");
            unlock_semaphore();
            usleep(50000);
            continue;            
        }

        game->current_team = team;

        if (have_i_won(team) == 1)
        {
            print_matrix();
            printf("Player %d from Team %d has won!\n", getpid(), team);
            unlock_semaphore();
            break;
        }

        if (have_i_lost() == 1)
        {
            // print_matrix();
            printf("Player %d from Team %d has lost.\n", getpid(), team);
            unlock_semaphore();
            break;
        }

        move_towards_nearest_opponent(team);
        check_captured_enemy(team);
        print_matrix();

        unlock_semaphore();
        /* Not really needed but this way we will let the CPU relax a bit. */
        usleep(10000);
    }
}

void play_game(int team)
{
    game_shm_id = shmget(SHM_GAME_KEY, sizeof(int), IPC_CREAT | 0666);
    if (game_shm_id == -1)
    {
        perror("shmget");
        exit(EXIT_FAILURE);
    }

    game = (struct game_state *)shmat(game_shm_id, NULL, 0);
    if (game == (void *)-1)
    {
        perror("shmat");
        exit(EXIT_FAILURE);
    }

    for (int i = 0; i < MAX_TEAMS; i++)
    {
        team_members[i] = (int *)(game + 1) + i * MAX_PROCESSES;
    }

    lock_semaphore();
    if (*shm_ptr == 1)
    {
        game->current_team = team;
        game->current_player_pid = 0;
        game->game_started = 0;

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
