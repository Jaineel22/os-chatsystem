#define _DEFAULT_SOURCE
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

#define SHM_KEY 1234
#define SEM_KEY 5678
#define MAX_MESSAGE_LEN 200
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

void log_system_event(const char* event) {
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
    
    fprintf(log_file, "[%s] SYSTEM: %s\n", timestamp, event);
    fclose(log_file);
}

// Check if message is an exit command
int is_exit_command(const char* message) {
    return (strcmp(message, "exit") == 0 || strcmp(message, "bye") == 0 || 
            strcmp(message, "quit") == 0 || strcmp(message, "q") == 0);
}

// Display welcome message
void display_welcome(const char* username, const char* color) {
    printf("\n%s%s╔══════════════════════════════════════════════════════════════╗%s\n", COLOR_BOLD, color, COLOR_RESET);
    printf("%s%s║                    CHAT SYSTEM v2.0                        ║%s\n", COLOR_BOLD, color, COLOR_RESET);
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

// Cleanup function for shared memory and semaphores
void cleanup_resources(int shmid, int semid) {
    printf("\n%s%sCleaning up resources...%s\n", COLOR_BOLD, SYSTEM_COLOR, COLOR_RESET);
    log_system_event("System cleanup initiated");
    
    // Detach from shared memory
    if (shmdt(NULL) == -1) {
        perror("shmdt failed");
    }
    
    // Remove shared memory segment
    if (shmctl(shmid, IPC_RMID, NULL) == -1) {
        perror("shmctl IPC_RMID failed");
    }
    
    // Remove semaphore set
    if (semctl(semid, 0, IPC_RMID) == -1) {
        perror("semctl IPC_RMID failed");
    }
    
    printf("%sResources cleaned up.%s\n", SYSTEM_COLOR, COLOR_RESET);
    log_system_event("System cleanup completed");
}