/* jaineel.c
 * Corrected / improved version of your OS chat program for Jaineel.
 *
 * Key improvements:
 *  - Robust handling of semaphore creation vs. opening existing set (IPC_EXCL)
 *  - Global resource IDs initialized to -1 so cleanup is safe in signal handler
 *  - Defensive checks before performing cleanup
 *  - Preserved your strncpy changes for sender field
 *
 * Note: This file assumes the other helper functions/types (struct shmseg, cleanup_resources,
 *       check_existing_resources, display_welcome, display_message, log_message, log_system_event,
 *       sem_wait, sem_signal, is_exit_command, etc.) and macros (SHM_KEY, SEM_KEY, MAX_MESSAGE_LEN,
 *       MAX_NAME_LEN, JAINEEL_NAME, GUL_NAME, etc.) are defined in "chat_common.h".
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

/* Globals (initialized to invalid IDs so cleanup can check them) */
int shmid = -1;
int semid = -1;
struct shmseg *shm = NULL;

/* Signal handler for cleanup */
void signal_handler(int sig) {
    printf("\n%sReceived signal %d, cleaning up...%s\n", ERROR_COLOR, sig, COLOR_RESET);
    /* Only attempt cleanup if resources were actually created / attached */
    if (shmid != -1 || semid != -1) {
        cleanup_resources(shmid, semid);
    } else {
        /* If we attached to shm, detatch it */
        if (shm != NULL && shm != (void*) -1) {
            shmdt(shm);
            shm = NULL;
        }
    }
    exit(0);
}

int main(void) {
    /* Set up signal handlers for graceful cleanup */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    display_welcome(JAINEEL_NAME, JAINEEL_COLOR);
    printf("%sWelcome to the OS Chat System!%s\n", SYSTEM_COLOR, COLOR_RESET);
    printf("%s========================================%s\n", SYSTEM_COLOR, COLOR_RESET);
    log_system_event("Jaineel process started");

    /* Check and clean any existing resources (your helper) */
    check_existing_resources();

    /* Create or get shared memory segment */
    printf("%sCreating shared memory segment...%s\n", INFO_COLOR, COLOR_RESET);
    shmid = shmget(SHM_KEY, sizeof(struct shmseg), IPC_CREAT | 0666);
    if (shmid == -1) {
        perror("shmget failed");
        printf("%sShared memory key: %d, Size: %zu%s\n", ERROR_COLOR, SHM_KEY, sizeof(struct shmseg), COLOR_RESET);
        return 1;
    }
    printf("%sShared memory created/obtained successfully (ID: %d)%s\n", SUCCESS_COLOR, shmid, COLOR_RESET);

    /* Attach to shared memory */
    shm = (struct shmseg*) shmat(shmid, NULL, 0);
    if (shm == (void*) -1) {
        perror("shmat failed");
        shmid = -1;
        return 1;
    }

    /* Initialize shared memory if this appears to be the first process */
    /* We use system_ready == 0 to indicate uninitialized memory. After init set to 1. */
    if (shm->system_ready == 0) {
        /* Clear entire shared memory region then set initial flags */
        memset(shm, 0, sizeof(struct shmseg));
        shm->system_ready = 1;
        shm->last_message_id = 0;
        shm->message_count = 0;
        printf("%sSystem initialized. Waiting for Gul to connect...%s\n", INFO_COLOR, COLOR_RESET);
        log_system_event("Chat system initialized by Jaineel");
    } else {
        printf("%sConnected to existing chat system.%s\n", SUCCESS_COLOR, COLOR_RESET);
        log_system_event("Jaineel connected to existing system");
    }

    /* Create or get semaphore set (3 semaphores) */
    printf("%sCreating/getting semaphore set...%s\n", INFO_COLOR, COLOR_RESET);

    /* Try creating semaphores with IPC_EXCL so we can know if we created them */
    semid = semget(SEM_KEY, 3, IPC_CREAT | IPC_EXCL | 0666);
    if (semid == -1) {
        if (errno == EEXIST) {
            /* Already exists — get the existing set without EXCL */
            semid = semget(SEM_KEY, 3, IPC_CREAT | 0666);
            if (semid == -1) {
                perror("semget (existing) failed");
                shmdt(shm);
                shm = NULL;
                return 1;
            }
            printf("%sOpened existing semaphore set (ID: %d)%s\n", SUCCESS_COLOR, semid, COLOR_RESET);
        } else {
            perror("semget failed");
            shmdt(shm);
            shm = NULL;
            return 1;
        }
    } else {
        /* We successfully created the semaphore set: initialize them */
        union semun {
            int val;
            struct semid_ds *buf;
            unsigned short *array;
#if defined(__linux__)
            struct seminfo *__buf;
#endif
        } arg;

        arg.val = 1;
        if (semctl(semid, 0, SETVAL, arg) == -1) {
            perror("semctl SETVAL sem 0 failed");
        }
        arg.val = 0;
        if (semctl(semid, 1, SETVAL, arg) == -1) {
            perror("semctl SETVAL sem 1 failed");
        }
        arg.val = 1;
        if (semctl(semid, 2, SETVAL, arg) == -1) {
            perror("semctl SETVAL sem 2 failed");
        }
        printf("%sSemaphore set created and initialized (ID: %d)%s\n", SUCCESS_COLOR, semid, COLOR_RESET);
    }

    printf("%sConnection established! You can now chat live.%s\n", SUCCESS_COLOR, COLOR_RESET);
    printf("%sType 'exit', 'bye', 'quit', or 'q' to leave.%s\n\n", SYSTEM_COLOR, COLOR_RESET);

    char input[MAX_MESSAGE_LEN];
    int message_id = 0;

    while (1) {
        /* Wait for write permission before composing/writing a message.
         * Note: your design uses sem[0] as write semaphore and sem[1] as read semaphore.
         * We preserve that behavior here: acquire write sem before checking or adding our messages.
         */
        if (sem_wait(semid, 0) == -1) {
            /* If sem_wait returns error (interrupted), break and cleanup */
            if (errno == EINTR) continue;
            perror("sem_wait failed");
            break;
        }

        /* Check if there are new messages from Gul (we examine shared memory directly) */
        if (shm->message_count > 0) {
            for (int i = 0; i < shm->message_count; i++) {
                if (shm->messages[i].message_id > message_id) {
                    display_message(GUL_NAME, shm->messages[i].content, GUL_COLOR, 0);
                    log_message(GUL_NAME, shm->messages[i].content);
                    message_id = shm->messages[i].message_id;

                    /* Check if Gul is leaving */
                    if (is_exit_command(shm->messages[i].content)) {
                        printf("%s%s has left the chat.%s\n", SYSTEM_COLOR, GUL_NAME, COLOR_RESET);
                        log_system_event("Gul left the chat");
                        /* release write semaphore so other processes aren't blocked */
                        sem_signal(semid, 0);
                        goto cleanup;
                    }
                }
            }
        }

        /* Get input from Jaineel */
        printf("%s%s > %s", JAINEEL_COLOR, JAINEEL_NAME, COLOR_RESET);
        fflush(stdout);

        if (fgets(input, sizeof(input), stdin) == NULL) {
            printf("%sError reading input%s\n", ERROR_COLOR, COLOR_RESET);
            /* release write semaphore before breaking */
            sem_signal(semid, 0);
            break;
        }

        /* Remove newline */
        input[strcspn(input, "\n")] = '\0';

        /* Check for exit command */
        if (is_exit_command(input)) {
            printf("%sYou are leaving the chat...%s\n", SYSTEM_COLOR, COLOR_RESET);
            log_system_event("Jaineel initiated exit");

            /* Send exit message to Gul */
            if (shm->message_count < 10) {
                strncpy(shm->messages[shm->message_count].content, input, MAX_MESSAGE_LEN);
                shm->messages[shm->message_count].content[MAX_MESSAGE_LEN - 1] = '\0';

                /* Keep your robust strncpy for sender */
                strncpy(shm->messages[shm->message_count].sender, JAINEEL_NAME, MAX_NAME_LEN);
                shm->messages[shm->message_count].sender[MAX_NAME_LEN - 1] = '\0';

                shm->messages[shm->message_count].type = MSG_TYPE_EXIT;
                shm->messages[shm->message_count].message_id = ++shm->last_message_id;
                shm->message_count++;
            }

            /* Signal Gul to read */
            sem_signal(semid, 1);
            /* release write semaphore for completeness */
            sem_signal(semid, 0);
            break;
        }

        /* Send normal message to Gul */
        if (shm->message_count < 10) {
            strncpy(shm->messages[shm->message_count].content, input, MAX_MESSAGE_LEN);
            shm->messages[shm->message_count].content[MAX_MESSAGE_LEN - 1] = '\0';

            /* Robust sender copy */
            strncpy(shm->messages[shm->message_count].sender, JAINEEL_NAME, MAX_NAME_LEN);
            shm->messages[shm->message_count].sender[MAX_NAME_LEN - 1] = '\0';

            shm->messages[shm->message_count].type = MSG_TYPE_NORMAL;
            shm->messages[shm->message_count].message_id = ++shm->last_message_id;
            shm->message_count++;
        } else {
            printf("%sMessage queue full. Please wait...%s\n", ERROR_COLOR, COLOR_RESET);
        }

        display_message(JAINEEL_NAME, input, JAINEEL_COLOR, 1);
        log_message(JAINEEL_NAME, input);

        /* Signal Gul to read (we used sem[1] as read semaphore) */
        sem_signal(semid, 1);

        /* Release write semaphore so other writers can proceed */
        sem_signal(semid, 0);
    }

cleanup:
    printf("%sCleaning up Jaineel...%s\n", SYSTEM_COLOR, COLOR_RESET);
    log_system_event("Jaineel process ending");

    /* Defensive cleanup_resources call: it should internally handle invalid IDs,
     * but we also guard here by only calling if at least one resource exists.
     */
    if (shmid != -1 || semid != -1) {
        cleanup_resources(shmid, semid);
    } else {
        if (shm != NULL && shm != (void*) -1) {
            shmdt(shm);
        }
    }

    return 0;
}
