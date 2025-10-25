#define _DEFAULT_SOURCE
#define VERSION "2.1"
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <sys/stat.h>
#include <wchar.h>        
#include <locale.h>  
#ifndef MAX_MESSAGE_LEN
#define MAX_MESSAGE_LEN 200
#endif
#define LOG_FILE "chat_history.log"
#define MAX_USERNAME_LEN 20

// User names
#define JAINEEL_NAME "Jaineel"
#define GUL_NAME "Gul"

// ANSI Color Codes
#define COLOR_RESET   "\033[0m"
#define COLOR_RED     "\033[31m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_YELLOW  "\033[33m"
#define COLOR_BLUE    "\033[34m"
#define COLOR_MAGENTA "\033[35m"
#define COLOR_CYAN    "\033[36m"
#define COLOR_WHITE   "\033[37m"
#define COLOR_BOLD    "\033[1m"
#define COLOR_DIM     "\033[2m"

// User-specific colors
#define JAINEEL_COLOR COLOR_CYAN
#define GUL_COLOR     COLOR_GREEN
#define SYSTEM_COLOR  COLOR_YELLOW
#define ERROR_COLOR   COLOR_RED
#define INFO_COLOR    COLOR_BLUE
#define SUCCESS_COLOR COLOR_GREEN

// Message types
#define MSG_TYPE_NORMAL 0
#define MSG_TYPE_EXIT   1
#define MSG_TYPE_SYSTEM 2

struct chat_message {
    char content[MAX_MESSAGE_LEN];
    char sender[MAX_USERNAME_LEN];
    int type;  // MSG_TYPE_NORMAL, MSG_TYPE_EXIT, MSG_TYPE_SYSTEM
    int message_id;
};

struct shmseg {
    struct chat_message messages[10];  // Buffer for multiple messages
    int message_count;
    int current_message;
    int last_message_id;
    int system_ready;  // 0 = not ready, 1 = ready
};

// Semaphore helper functions
void sem_wait(int semid, int semnum) {
    struct sembuf sb = {semnum, -1, 0};
    if (semop(semid, &sb, 1) == -1) {
        perror("sem_wait failed");
        exit(1);
    }
}

void sem_signal(int semid, int semnum) {
    struct sembuf sb = {semnum, +1, 0};
    if (semop(semid, &sb, 1) == -1) {
        perror("sem_signal failed");
        exit(1);
    }
}

// Logging functions
void log_message(const char* user, const char* message) {
    // FIX: Check log file size and rotate if too large
    struct stat st;
    if (stat(LOG_FILE, &st) == 0 && st.st_size > 1024 * 1024) { // 1MB limit
        char old_log[64];
        snprintf(old_log, sizeof(old_log), "%s.old", LOG_FILE);
        rename(LOG_FILE, old_log);
        log_system_event("Log file rotated due to size limit");
    }
    
    FILE *log_file = fopen(LOG_FILE, "a");
    if (log_file == NULL) {
        perror("Failed to open log file");
        return;
    }
    
    time_t now;
    time(&now);
    struct tm *tm_info = localtime(&now);
    
    char timestamp[64];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info);
    
    fprintf(log_file, "[%s] %s: %s\n", timestamp, user, message);
    fclose(log_file);
}

// Check if message is an exit command
int is_exit_command(const char* message) {
    char lower[MAX_MESSAGE_LEN];
    strncpy(lower, message, MAX_MESSAGE_LEN);
    for (int i = 0; lower[i]; i++) {
        lower[i] = tolower(lower[i]);
    }
    return (strcmp(lower, "exit") == 0 || strcmp(lower, "bye") == 0 || 
            strcmp(lower, "quit") == 0 || strcmp(lower, "q") == 0);
}

// Display welcome message
void display_welcome(const char* username, const char* color) {
    printf("\n%s%s╔══════════════════════════════════════════════════════════════╗%s\n", COLOR_BOLD, color, COLOR_RESET);
    printf("%s%s║                    CHAT SYSTEM v%s                         ║%s\n", COLOR_BOLD, color, VERSION, COLOR_RESET);
    printf("%s%s║                                                              ║%s\n", COLOR_BOLD, color, COLOR_RESET);
    printf("%s%s║  Welcome %s%-20s%s!                                    ║%s\n", COLOR_BOLD, color, COLOR_WHITE, username, color, COLOR_RESET);
    printf("%s%s║                                                              ║%s\n", COLOR_BOLD, color, COLOR_RESET);
    printf("%s%s║  Commands: exit, bye, quit, q                              ║%s\n", COLOR_BOLD, color, COLOR_RESET);
    printf("%s%s║  Type your messages below...                               ║%s\n", COLOR_BOLD, color, COLOR_RESET);
    printf("%s%s╚══════════════════════════════════════════════════════════════╝%s\n", COLOR_BOLD, color, COLOR_RESET);
    printf("\n");
}

// Display typing indicator
void display_typing_indicator(const char* username, const char* color) {
    printf("%s%s%s is typing...%s\r", COLOR_DIM, color, username, COLOR_RESET);
    fflush(stdout);
    usleep(500000);  // 0.5 second delay
    printf("\r%*s\r", 50, "");  // Clear the line
    fflush(stdout);
}

// Display message with proper formatting
void display_message(const char* sender, const char* message, const char* color, int is_own) {
    time_t now;
    time(&now);
    struct tm *tm_info = localtime(&now);
    char timestamp[16];
    strftime(timestamp, sizeof(timestamp), "%H:%M", tm_info);
    
    if (is_own) {
        printf("%s[%s] %sYou: %s%s%s\n", COLOR_DIM, timestamp, color, COLOR_RESET, message, COLOR_RESET);
    } else {
        printf("%s[%s] %s%s: %s%s%s\n", COLOR_DIM, timestamp, color, sender, COLOR_RESET, message, COLOR_RESET);
    }
}

// Check and clean existing resources
void check_existing_resources() {
    // Check for existing shared memory
    int existing_shmid = shmget(SHM_KEY, 0, 0);
    if (existing_shmid != -1) {
        printf("%sFound existing shared memory (ID: %d), removing...%s\n", INFO_COLOR, existing_shmid, COLOR_RESET);
        shmctl(existing_shmid, IPC_RMID, NULL);
    }
    
    // Check for existing semaphores
    int existing_semid = semget(SEM_KEY, 0, 0);
    if (existing_semid != -1) {
        printf("%sFound existing semaphore set (ID: %d), removing...%s\n", INFO_COLOR, existing_semid, COLOR_RESET);
        semctl(existing_semid, 0, IPC_RMID);
    }
}

void sanitize_input(char* input) {
    // Remove control characters
    for (int i = 0; input[i]; i++) {
        if (input[i] < 32 && input[i] != '\n' && input[i] != '\t') {
            input[i] = ' ';
        }
    }
}

// Add function to clear processed messages
void clear_processed_messages(struct shmseg* shm, int up_to_id) {
    int write_index = 0;
    for (int i = 0; i < shm->message_count; i++) {
        if (shm->messages[i].message_id > up_to_id) {
            shm->messages[write_index++] = shm->messages[i];
        }
    }
    shm->message_count = write_index;
}

//brief Initialize Unicode and locale support for international text
 
void setup_unicode(void) {
    if (setlocale(LC_ALL, "en_US.UTF-8") == NULL) {
        // Fallback if UTF-8 locale is not available
        setlocale(LC_ALL, "C");
        printf("%sWarning: UTF-8 locale not available, using basic locale%s\n", 
               COLOR_YELLOW, COLOR_RESET);
    }
}
