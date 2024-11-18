#ifndef GLOBALS_H
#define GLOBALS_H

#define MAX_TEAMS 10
#define MAX_PROCESSES 100
#define WIDTH 5
#define HEIGHT 5

extern int shm_id;
extern int sem_id;
extern int *shm_ptr;
extern int msg_ids[MAX_TEAMS];

#endif // GLOBALS_H
