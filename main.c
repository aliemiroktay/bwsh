#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <termios.h>
#include <ctype.h>

#define INITIAL_BUFFER_SIZE 1024
#define PATH_MAX 4096

const char *PROMPT = "\033[34m>>bwsh==>>\033[0m ";
const size_t PROMPT_LEN = 12; // Visible length without ANSI codes

void prompt(void) {
    printf("%s", PROMPT);
    fflush(stdout);
}

void execute(char **args) {
    pid_t pid = fork();
    if (pid < 0) {
        perror("!!! Forking failed");
        exit(1);
    } else if (pid == 0) {
        execvp(args[0], args);
        perror("!!! Execution failed");
        exit(1);
    } else {
        int status;
        waitpid(pid, &status, 0);
    }
}

void parse_and_execute(char *input) {
    char **args = malloc(INITIAL_BUFFER_SIZE * sizeof(char *));
    if (!args) {
        perror("Memory allocation failed");
        exit(1);
    }

    char *token = strtok(input, " \t");
    int i = 0;
    while (token != NULL) {
        args[i++] = token;
        token = strtok(NULL, " \t");
    }
    args[i] = NULL;

    if (args[0] == NULL) {
        free(args);
        return;
    }

    if (strcmp(args[0], "exit") == 0) {
        free(args);
        exit(0);
    } else if (args[0] && strcmp(args[0], "cd") == 0) {
        char *target_dir = NULL;
        static char prev_dir[PATH_MAX]; // Stores last directory for "cd -"

        if (!args[1] || strcmp(args[1], "~") == 0) {
            // Case: "cd" or "cd ~" → go to $HOME
            target_dir = getenv("HOME");
        } else if (strcmp(args[1], "-") == 0) {
            // Case: "cd -" → go to previous directory
            if (prev_dir[0] == '\0') {
                fprintf(stderr, "cd: no previous directory\n");
                free(args);
                return;
            }
            target_dir = prev_dir;
        } else if (args[1][0] == '~') {
            // Case: "cd ~/path" → expand to $HOME/path
            char *home = getenv("HOME");
            if (!home) {
                fprintf(stderr, "cd: HOME not set\n");
                free(args);
                return;
            }
            char expanded_path[PATH_MAX];
            snprintf(expanded_path, sizeof(expanded_path), "%s%s", home, args[1] + 1);
            target_dir = expanded_path;
        } else {
            // Case: "cd /normal/path"
            target_dir = args[1];
        }

        // Save current dir before changing (for "cd -")
        char cwd[PATH_MAX];
        getcwd(cwd, sizeof(cwd));

        if (chdir(target_dir) != 0) {
            perror("cd failed");
        } else {
            // Update prev_dir only if cd succeeds
            strncpy(prev_dir, cwd, sizeof(prev_dir));
        }
    }else if (strcmp(args[0], "pwd") == 0) {
        char cwd[1024];
        getcwd(cwd, sizeof(cwd));
        printf("%s\n", cwd);
    } else if (strcmp(args[0], "export") == 0) {
        if (args[1]) {
            char *var = strtok(args[1], "=");
            char *value = strtok(NULL, "=");
            if (var && value) {
                setenv(var, value, 1); // Overwrite if exists
            } else {
                fprintf(stderr, "Usage: export VAR=value\n");
            }
        }
    } else {
        execute(args);
    }

    free(args);
}

char* read_input(void) {
    struct termios original, raw;
    tcgetattr(STDIN_FILENO, &original);
    raw = original;
    raw.c_lflag &= ~(ICANON | ECHO);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);

    char *buffer = malloc(INITIAL_BUFFER_SIZE);
    size_t size = INITIAL_BUFFER_SIZE;
    size_t len = 0;
    size_t cursor = 0;

    prompt();
    fflush(stdout);

    while (1) {
        char c;
        if (read(STDIN_FILENO, &c, 1) <= 0) continue;

        if (c == '\r' || c == '\n') {
            buffer[len] = '\0';
            break;
        } else if (c == 127 || c == '\b') { // Backspace
            if (cursor > 0 && len > 0) {
                memmove(&buffer[cursor-1], &buffer[cursor], len - cursor);
                len--;
                cursor--;
                printf("\r%s\033[K", PROMPT);
                fwrite(buffer, 1, len, stdout);
                printf("\033[%zuD", len - cursor); // Fix here
                fflush(stdout);
            }
        } else if (c == 27) { // Arrow keys
            char seq[2];
            if (read(STDIN_FILENO, &seq[0], 1) != 1) continue;
            if (read(STDIN_FILENO, &seq[1], 1) != 1) continue;
            
            if (seq[0] == '[') {
                if (seq[1] == 'D' && cursor > 0) { // Left
                    cursor--;
                    printf("\033[D");
                    fflush(stdout);
                } else if (seq[1] == 'C' && cursor < len) { // Right
                    cursor++;
                    printf("\033[C");
                    fflush(stdout);
                }
            }
        } else if (isprint(c)) { // Regular characters
            if (len + 1 >= size) {
                size *= 2;
                char *new_buf = realloc(buffer, size);
                if (!new_buf) {
                    free(buffer);
                    perror("Memory realloc failed");
                    exit(1);
                }
                buffer = new_buf;
            }
            memmove(&buffer[cursor+1], &buffer[cursor], len - cursor);
            buffer[cursor++] = c;
            len++;
            printf("\r%s\033[K", PROMPT);  // Clear line and reprint prompt
            fwrite(buffer, 1, len, stdout);
            // Fix: Only move cursor back if we're not at end of input
            if (cursor < len) {
                printf("\033[%zuD", len - cursor);
            }
            fflush(stdout);
        }
    }

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &original);
    printf("\n");
    return buffer;
}


int main(void) {
    while (1) {
        char *input = read_input();
        parse_and_execute(input);
        free(input);
    }
    return 0;
}