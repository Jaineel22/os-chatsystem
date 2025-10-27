#include "chat_common.h"

#define HISTORY_SIZE 5
static char input_history[HISTORY_SIZE][MAX_MESSAGE_LEN];
static int history_index = 0;

// Function declarations
void show_message_history(void);
void cleanup_resources(int shmid, int semid);

int shmid, semid;
struct shmseg *shm;

// Signal handler for cleanup
void signal_handler(int sig) {
    const char msg[] = "\nSignal received, cleaning up...\n";
    write(STDOUT_FILENO, msg, sizeof(msg) - 1);
    cleanup_resources(shmid, semid);
    _exit(0);
}

// Add cleanup_resources if not in chat_common.h
void cleanup_resources(int shmid, int semid) {
    if (shmid != -1) {
        shmctl(shmid, IPC_RMID, NULL);
    }
    if (semid != -1) {
        semctl(semid, 0, IPC_RMID);
    }
}

int main() {
    // Set up signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    setup_unicode();
    
    display_welcome(GUL_NAME, GUL_COLOR);
    printf("%sWelcome to the OS Chat System!%s\n", SYSTEM_COLOR, COLOR_RESET);
    printf("%sCompiled on: %s at %s%s\n", COLOR_DIM, __DATE__, __TIME__, COLOR_RESET);
    log_system_event("Gul process started");

    // Get existing shared memory segment
    shmid = shmget(SHM_KEY, sizeof(struct shmseg), 0666);
    if (shmid == -1) {
        perror("shmget failed - make sure Jaineel is running first");
        exit(1);
    }
    
    // Attach to shared memory
    shm = (struct shmseg*) shmat(shmid, NULL, 0);
    if (shm == (void*)-1) {
        perror("shmat failed");
        exit(1);
    }
    
    // Wait for system to be ready
    while (shm->system_ready == 0) {
        printf("%sWaiting for system to initialize...%s\n", INFO_COLOR, COLOR_RESET);
        sleep(1);
    }
    
    printf("%sConnected to chat system.%s\n", SUCCESS_COLOR, COLOR_RESET);
    log_system_event("Gul connected to chat system");
    
    // Get existing semaphore set
    semid = semget(SEM_KEY, 3, 0666);
    if (semid == -1) {
        perror("semget failed - make sure Jaineel is running first");
        shmdt(shm);
        exit(1);
    }
    
    printf("%sChat ready! You can start typing messages.%s\n", SUCCESS_COLOR, COLOR_RESET);
    printf("%sType 'exit', 'bye', 'quit', or 'q' to leave.%s\n\n", SYSTEM_COLOR, COLOR_RESET);
    
    char input[MAX_MESSAGE_LEN];
    int message_id = 0;
    
    while (1) {
        // FIXED: Acquire write lock before reading shared memory
        sem_wait(semid, 0);
        
        // Check if there are new messages from Jaineel
        if (shm->message_count > 0) {
            for (int i = 0; i < shm->message_count; i++) {
                if (shm->messages[i].message_id > message_id) {
                    display_message(JAINEEL_NAME, shm->messages[i].content, JAINEEL_COLOR, 0);
                    log_message(JAINEEL_NAME, shm->messages[i].content);
                    message_id = shm->messages[i].message_id;
                    
                    // Check if Jaineel is leaving
                    if (is_exit_command(shm->messages[i].content)) {
                        printf("%s%s has left the chat.%s\n", SYSTEM_COLOR, JAINEEL_NAME, COLOR_RESET);
                        log_system_event("Jaineel left the chat");
                        sem_signal(semid, 0);
                        goto cleanup;
                    }
                }
            }
        }
        
        // Buffer full check should be here, before getting input
        if (shm->message_count >= 10) {
            printf("%sMessage buffer full! Waiting for space...%s\n", ERROR_COLOR, COLOR_RESET);
            sem_signal(semid, 0);
            sleep(2);
            continue;
        }
        
        // Release lock before getting user input (to avoid blocking)
        sem_signal(semid, 0);
        
        // Get input from Gul
        printf("%s%s: %s", GUL_COLOR, GUL_NAME, COLOR_RESET);
        fflush(stdout);
        
        if (fgets(input, MAX_MESSAGE_LEN, stdin) == NULL) {
            printf("%sError reading input%s\n", ERROR_COLOR, COLOR_RESET);
            break;
        }
        
        // Remove newline
        input[strcspn(input, "\n")] = 0;

        // Input validation
        if (strlen(input) == 0) {
            printf("%sEmpty message ignored%s\n", COLOR_DIM, COLOR_RESET);
            continue;
        }

        // Length validation
        if (strlen(input) >= MAX_MESSAGE_LEN - 1) {
            printf("%sMessage too long! Please shorten your message.%s\n", ERROR_COLOR, COLOR_RESET);
            continue;
        }
        
        // Check for exit command
        if (is_exit_command(input)) {
            printf("%sYou are leaving the chat...%s\n", SYSTEM_COLOR, COLOR_RESET);
            log_system_event("Gul initiated exit");
            
            // Re-acquire lock for shared memory access
            sem_wait(semid, 0);
            
            // Send exit message to Jaineel
            if (shm->message_count < 10) {
                strncpy(shm->messages[shm->message_count].content, input, MAX_MESSAGE_LEN-1);
                strncpy(shm->messages[shm->message_count].sender, GUL_NAME, MAX_USERNAME_LEN-1);
                shm->messages[shm->message_count].type = MSG_TYPE_EXIT;
                shm->messages[shm->message_count].message_id = ++shm->last_message_id;
                shm->message_count++;
            }
            
            sem_signal(semid, 0);
            sem_signal(semid, 1); // Signal Jaineel to read
            break;
        }

        // Re-acquire lock for sending message
        sem_wait(semid, 0);
        
        // Buffer check again (situation might have changed)
        if (shm->message_count >= 10) {
            printf("%sMessage buffer full! Waiting for space...%s\n", ERROR_COLOR, COLOR_RESET);
            sem_signal(semid, 0);
            sleep(2);
            continue;
        }
        
        // Send message to Jaineel
        strncpy(shm->messages[shm->message_count].content, input, MAX_MESSAGE_LEN-1);
        strncpy(shm->messages[shm->message_count].sender, GUL_NAME, MAX_USERNAME_LEN-1);
        shm->messages[shm->message_count].type = MSG_TYPE_NORMAL;
        shm->messages[shm->message_count].message_id = ++shm->last_message_id;
        shm->message_count++;
        
        // Store message in history
        if (history_index < HISTORY_SIZE) {
            strncpy(input_history[history_index], input, MAX_MESSAGE_LEN-1);
            input_history[history_index][MAX_MESSAGE_LEN-1] = '\0';
            history_index++;
        }

        // FIXED: Remove duplicates - display only once
        display_message(GUL_NAME, input, GUL_COLOR, 1);
        log_message(GUL_NAME, input);
        
        // Signal Jaineel to read and release lock
        sem_signal(semid, 1);
        sem_signal(semid, 0);
    }
    
cleanup:
    printf("%sCleaning up Gul...%s\n", SYSTEM_COLOR, COLOR_RESET);
    log_system_event("Gul process ending");
    shmdt(shm);
    
    return 0;
}

void show_message_history() {
    printf("%sRecent messages you sent:%s\n", INFO_COLOR, COLOR_RESET);
    for (int i = 0; i < history_index; i++) {
        printf("  %s%d. %s%s\n", COLOR_DIM, i+1, input_history[i], COLOR_RESET);
    }
}
