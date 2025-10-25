/*
 * jaineel.c
 * OS Chat System - Jaineel Side
 * Fixed version: resolves sem_wait() return type issue & uses MAX_USERNAME_LEN.
 */

#include "chat_common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <unistd.h>

/* Globals initialized to invalid states for safety */
int shmid = -1;
int semid = -1;
struct shmseg *shm = NULL;

/* Signal handler for cleanup */
void signal_handler(int sig) {
    printf("\n%sReceived signal %d, cleaning up...%s\n", ERROR_COLOR, sig, COLOR_RESET);

    // Call cleanup_resources if we have any valid IPC IDs.
    // The function itself (in chat_common.c) should handle detaching shm.
    if (shmid != -1 || semid != -1) {
        cleanup_resources(shmid, semid);
    }
    
    exit(0);
}

int main(void) {
    /* Handle interrupt signals */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    setup_unicode();

    display_welcome(JAINEEL_NAME, JAINEEL_COLOR);
    printf("%sWelcome to the OS Chat System!%s\n", SYSTEM_COLOR, COLOR_RESET);
    printf("%s========================================%s\n", SYSTEM_COLOR, COLOR_RESET);
    log_system_event("Jaineel process started");

    /* Check and clean existing resources if any */
    check_existing_resources();

    /* Create or get shared memory */
    printf("%sCreating shared memory segment...%s\n", INFO_COLOR, COLOR_RESET);
    shmid = shmget(SHM_KEY, sizeof(struct shmseg), IPC_CREAT | 0666);
    if (shmid == -1) {
        perror("shmget failed");
        printf("%sShared memory key: %d, Size: %zu%s\n",
               ERROR_COLOR, SHM_KEY, sizeof(struct shmseg), COLOR_RESET);
        return 1;
    }
    printf("%sShared memory created/obtained successfully (ID: %d)%s\n",
           SUCCESS_COLOR, shmid, COLOR_RESET);

    /* Attach to shared memory */
    shm = (struct shmseg *)shmat(shmid, NULL, 0);
    if (shm == (void *)-1) {
        perror("shmat failed");
        printf("%sFailed to attach to shared memory ID: %d%s\n", ERROR_COLOR, shmid, COLOR_RESET);
        shmid = -1;
        return 1;
    }

    /* Initialize shared memory if first process */
    if (shm->system_ready == 0) {
        memset(shm, 0, sizeof(struct shmseg));
        shm->system_ready = 1;
        shm->last_message_id = 0;
        shm->message_count = 0;
        printf("%sSystem initialized. Waiting for Gul to connect...%s\n",
               INFO_COLOR, COLOR_RESET);
        log_system_event("Chat system initialized by Jaineel");
    } else {
        printf("%sConnected to existing chat system.%s\n",
               SUCCESS_COLOR, COLOR_RESET);
        log_system_event("Jaineel connected to existing system");
    }

    /* Create or get semaphore set (3 semaphores) */
    printf("%sCreating/getting semaphore set...%s\n", INFO_COLOR, COLOR_RESET);

    semid = semget(SEM_KEY, 3, IPC_CREAT | IPC_EXCL | 0666);
    if (semid == -1) {
        if (errno == EEXIST) {
            semid = semget(SEM_KEY, 3, IPC_CREAT | 0666);
            if (semid == -1) {
                perror("semget (existing) failed");
                // --- NEW CHANGE START (BLOCK 1) ---
                printf("%sFailed to get existing semaphore with key: %d%s\n", ERROR_COLOR, SEM_KEY, COLOR_RESET);
                // --- NEW CHANGE END (BLOCK 1) ---
                shmdt(shm);
                shm = NULL;
                return 1;
            }
            printf("%sOpened existing semaphore set (ID: %d)%s\n",
                   SUCCESS_COLOR, semid, COLOR_RESET);
        } else {
            perror("semget failed");
            // --- NEW CHANGE START (BLOCK 2) ---
            printf("%sFailed to create new semaphore with key: %d%s\n", ERROR_COLOR, SEM_KEY, COLOR_RESET);
            // --- NEW CHANGE END (BLOCK 2) ---
            shmdt(shm);
            shm = NULL;
            return 1;
        }
    } else {
        /* We created it, initialize values */
        union semun {
            int val;
            struct semid_ds *buf;
            unsigned short *array;
#if defined(__linux__)
            struct seminfo *__buf;
#endif
        } arg;

        arg.val = 1;
        semctl(semid, 0, SETVAL, arg); // write semaphore
        arg.val = 0;
        semctl(semid, 1, SETVAL, arg); // read semaphore
        arg.val = 1;
        semctl(semid, 2, SETVAL, arg); // system semaphore

        printf("%sSemaphore set created and initialized (ID: %d)%s\n",
               SUCCESS_COLOR, semid, COLOR_RESET);
    }

    printf("%sConnection established! You can now chat live.%s\n",
           SUCCESS_COLOR, COLOR_RESET);
    printf("%sType 'exit', 'bye', 'quit', or 'q' to leave.%s\n\n",
           SYSTEM_COLOR, COLOR_RESET);

    char input[MAX_MESSAGE_LEN];
    int message_id = 0;

    while (1) {
        /* * === CRITICAL SECTION START ===
         * Acquire the main mutex lock (semaphore 0).
         * This ensures only one process can read/write to shared memory at a time.
         */
        sem_wait(semid, 0);

        /* Check new messages from Gul */
        if (shm->message_count > 0) {
            for (int i = 0; i < shm->message_count; i++) {
                if (shm->messages[i].message_id > message_id) {
                    
                    // Only process this message if the sender is NOT Jaineel
                    if (strncmp(shm->messages[i].sender, JAINEEL_NAME, MAX_USERNAME_LEN) != 0) {
                    
                        display_message(GUL_NAME, shm->messages[i].content, GUL_COLOR, 0);
                        log_message(GUL_NAME, shm->messages[i].content);

                        if (is_exit_command(shm->messages[i].content)) {
                            printf("%s%s has left the chat.%s\n",
                                   SYSTEM_COLOR, GUL_NAME, COLOR_RESET);
                            log_system_event("Gul left the chat");
                            sem_signal(semid, 0);
                            goto cleanup;
                        }
                    }
                    
                    // We update the message_id regardless of who sent it
                    // to prevent re-reading it.
                    message_id = shm->messages[i].message_id;
                }
            }
        }

        /* Check if message queue is full before asking for input */
        if (shm->message_count >= 10) {
            printf("%sMessage queue is full. Waiting for other user to read messages...%s\n", ERROR_COLOR, COLOR_RESET);
            sem_signal(semid, 0); // Release the lock
            sleep(1);             // Wait 1 second to prevent busy-looping
            continue;             // Go to the start of the while loop
        }

        /* Get input */
        printf("%s%s > %s", JAINEEL_COLOR, JAINEEL_NAME, COLOR_RESET);
        fflush(stdout);

        if (fgets(input, sizeof(input), stdin) == NULL) {
            printf("%sError reading input%s\n", ERROR_COLOR, COLOR_RESET);
            sem_signal(semid, 0);
            break;
        }

        input[strcspn(input, "\n")] = '\0';
        
        /* Check for empty input (user just pressed Enter) */
        if (input[0] == '\0') {
            sem_signal(semid, 0); // Release the lock
            continue; // Go to the next loop iteration without sending
        }
        
        /* Exit command */
        if (is_exit_command(input)) {
            printf("%sYou are leaving the chat...%s\n", SYSTEM_COLOR, COLOR_RESET);
            log_system_event("Jaineel initiated exit");

            if (shm->message_count < 10) {
                strncpy(shm->messages[shm->message_count].content, input, MAX_MESSAGE_LEN);
                shm->messages[shm->message_count].content[MAX_MESSAGE_LEN - 1] = '\0';

                strncpy(shm->messages[shm->message_count].sender, JAINEEL_NAME, MAX_USERNAME_LEN);
                shm->messages[shm->message_count].sender[MAX_USERNAME_LEN - 1] = '\0';

                shm->messages[shm->message_count].type = MSG_TYPE_EXIT;
                shm->messages[shm->message_count].message_id = ++shm->last_message_id;
                shm->message_count++;
            }

            /*
             * Signal the other process (semaphore 1) to notify it of a new message.
             * This assumes the other process is actively checking or waiting.
             */
            sem_signal(semid, 1);
            sem_signal(semid, 0);
            break;
        }

        /* Normal message */
        if (shm->message_count < 10) {
            strncpy(shm->messages[shm->message_count].content, input, MAX_MESSAGE_LEN);
            shm->messages[shm->message_count].content[MAX_MESSAGE_LEN - 1] = '\0';

            strncpy(shm->messages[shm->message_count].sender, JAINEEL_NAME, MAX_USERNAME_LEN);
            shm->messages[shm->message_count].sender[MAX_USERNAME_LEN - 1] = '\0';

            shm->messages[shm->message_count].type = MSG_TYPE_NORMAL;
            shm->messages[shm->message_count].message_id = ++shm->last_message_id;
            shm->message_count++;
        } else {
            printf("%sMessage queue full. Please wait...%s\n",
                   ERROR_COLOR, COLOR_RESET);
        }

        display_message(JAINEEL_NAME, input, JAINEEL_COLOR, 1);
        log_message(JAINEEL_NAME, input);

        sem_signal(semid, 1);
        
        /*
         * === CRITICAL SECTION END ===
         * Release the mutex lock (semaphore 0), allowing the other process
         * to enter its critical section.
         */
        sem_signal(semid, 0);
    }

cleanup:
    printf("%sCleaning up Jaineel...%s\n", SYSTEM_COLOR, COLOR_RESET);
    log_system_event("Jaineel process ending");

    if (shmid != -1 || semid != -1) {
        cleanup_resources(shmid, semid);
    }
    
    return 0;
}