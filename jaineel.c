#include "chat_common.h"

int shmid, semid;
struct shmseg *shm;

// Signal handler for cleanup
void signal_handler(int sig) {
    printf("\n%sReceived signal %d, cleaning up...%s\n", ERROR_COLOR, sig, COLOR_RESET);
    cleanup_resources(shmid, semid);
    exit(0);
}

int main() {
    // Set up signal handlers for graceful cleanup
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    display_welcome(JAINEEL_NAME, JAINEEL_COLOR);
    printf("%sWelcome to the OS Chat System!%s\n", SYSTEM_COLOR, COLOR_RESET);
    printf("%s========================================%s\n", SYSTEM_COLOR, COLOR_RESET); // <-- THIS IS THE NEW LINE
    log_system_event("Jaineel process started");
    
    // Check and clean any existing resources
    check_existing_resources();
    
    // Create or get shared memory segment
    printf("%sCreating shared memory segment...%s\n", INFO_COLOR, COLOR_RESET);
    shmid = shmget(SHM_KEY, sizeof(struct shmseg), IPC_CREAT | 0666);
    if (shmid == -1) {
        perror("shmget failed");
        printf("%sShared memory key: %d, Size: %zu%s\n", ERROR_COLOR, SHM_KEY, sizeof(struct shmseg), COLOR_RESET);
        exit(1);
    }
    printf("%sShared memory created successfully (ID: %d)%s\n", SUCCESS_COLOR, shmid, COLOR_RESET);
    
    // Attach to shared memory
    shm = (struct shmseg*) shmat(shmid, NULL, 0);
    if (shm == (void*)-1) {
        perror("shmat failed");
        exit(1);
    }
    
    // Initialize shared memory if this is the first process
    if (shm->system_ready == 0) {
        memset(shm, 0, sizeof(struct shmseg));
        shm->system_ready = 1;
        shm->last_message_id = 0;
        printf("%sSystem initialized. Waiting for Gul to connect...%s\n", INFO_COLOR, COLOR_RESET);
        log_system_event("Chat system initialized by Jaineel");
    } else {
        printf("%sConnected to existing chat system.%s\n", SUCCESS_COLOR, COLOR_RESET);
        log_system_event("Jaineel connected to existing system");
    }
    
    // Create or get semaphore set (3 semaphores: write, read, system)
    printf("%sCreating semaphore set...%s\n", INFO_COLOR, COLOR_RESET);
    semid = semget(SEM_KEY, 3, IPC_CREAT | 0666);
    if (semid == -1) {
        perror("semget failed");
        printf("%sSemaphore key: %d%s\n", ERROR_COLOR, SEM_KEY, COLOR_RESET);
        shmdt(shm);
        exit(1);
    }
    printf("%sSemaphore set created successfully (ID: %d)%s\n", SUCCESS_COLOR, semid, COLOR_RESET);
    
    // Initialize semaphores if this is the first process
    if (shm->system_ready == 1) {
        semctl(semid, 0, SETVAL, 1); // write semaphore = 1
        semctl(semid, 1, SETVAL, 0); // read semaphore = 0
        semctl(semid, 2, SETVAL, 1); // system semaphore = 1
    }
    
    printf("%sChat ready! You can start typing messages.%s\n", SUCCESS_COLOR, COLOR_RESET);
    printf("%sType 'exit', 'bye', 'quit', or 'q' to leave.%s\n\n", SYSTEM_COLOR, COLOR_RESET);
    
    char input[MAX_MESSAGE_LEN];
    int message_id = 0;
    
    while (1) {
        // Wait for write permission
        sem_wait(semid, 0);
        
        // Check if there are new messages from Gul
        if (shm->message_count > 0) {
            for (int i = 0; i < shm->message_count; i++) {
                if (shm->messages[i].message_id > message_id) {
                    display_message(GUL_NAME, shm->messages[i].content, GUL_COLOR, 0);
                    log_message(GUL_NAME, shm->messages[i].content);
                    message_id = shm->messages[i].message_id;
                    
                    // Check if Gul is leaving
                    if (is_exit_command(shm->messages[i].content)) {
                        printf("%s%s has left the chat.%s\n", SYSTEM_COLOR, GUL_NAME, COLOR_RESET);
                        log_system_event("Gul left the chat");
                        sem_signal(semid, 0);
                        goto cleanup;
                    }
                }
            }
        }
        
        // Get input from Jaineel
        printf("%s%s: %s", JAINEEL_COLOR, JAINEEL_NAME, COLOR_RESET);
        fflush(stdout);
        
        if (fgets(input, MAX_MESSAGE_LEN, stdin) == NULL) {
            printf("%sError reading input%s\n", ERROR_COLOR, COLOR_RESET);
            break;
        }
        
        // Remove newline
        input[strcspn(input, "\n")] = 0;
        
        // Check for exit command
        if (is_exit_command(input)) {
            printf("%sYou are leaving the chat...%s\n", SYSTEM_COLOR, COLOR_RESET);
            log_system_event("Jaineel initiated exit");
            
            // Send exit message to Gul
            if (shm->message_count < 10) {
                strcpy(shm->messages[shm->message_count].content, input);
                strcpy(shm->messages[shm->message_count].sender, JAINEEL_NAME);
                shm->messages[shm->message_count].type = MSG_TYPE_EXIT;
                shm->messages[shm->message_count].message_id = ++shm->last_message_id;
                shm->message_count++;
            }
            
            sem_signal(semid, 1); // Signal Gul to read
            break;
        }
        
        // Send message to Gul
        if (shm->message_count < 10) {
            strcpy(shm->messages[shm->message_count].content, input);
            strcpy(shm->messages[shm->message_count].sender, JAINEEL_NAME);
            shm->messages[shm->message_count].type = MSG_TYPE_NORMAL;
            shm->messages[shm->message_count].message_id = ++shm->last_message_id;
            shm->message_count++;
        }
        
        display_message(JAINEEL_NAME, input, JAINEEL_COLOR, 1);
        log_message(JAINEEL_NAME, input);
        
        // Signal Gul to read
        sem_signal(semid, 1);
    }
    
cleanup:
    printf("%sCleaning up Jaineel...%s\n", SYSTEM_COLOR, COLOR_RESET);
    log_system_event("Jaineel process ending");
    cleanup_resources(shmid, semid);
    
    return 0;
}