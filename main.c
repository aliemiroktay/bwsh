#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <pwd.h>
#include <termios.h>
#include <ctype.h>

#define INITIAL_BUFFER_SIZE 1024

char *PROMPT;
size_t PROMPT_LEN; // Visible length without ANSI codes

void prompt(void) {
    if(PROMPT != NULL) free(PROMPT);
    PROMPT = malloc(1);  // Start with an empty string
    PROMPT[0] = '\0';  // Null-terminate the empty string

    struct passwd *pw = getpwuid(getuid());

    char *cwd = getcwd(NULL, 0);
    if(cwd != NULL){
    
        // Concatenate ">>"
        PROMPT = realloc(PROMPT, strlen(PROMPT) + strlen(">>") + 1);
        strcat(PROMPT, ">>");

        // Concentrate the user
        PROMPT = realloc(PROMPT, strlen(PROMPT) + strlen(pw->pw_name) + 1);
        strcat(PROMPT, pw->pw_name);

        // Concentrate @
        PROMPT = realloc(PROMPT, strlen(PROMPT) + 2);
        strcat(PROMPT, "@");

        // Concatenate the current directory
        if(strcmp(cwd, getenv("HOME"))){
            PROMPT = realloc(PROMPT, strlen(PROMPT) + strlen(cwd) + 1);
            strcat(PROMPT, cwd);
        } else {
            PROMPT = realloc(PROMPT, strlen(PROMPT) + 2);
            strcat(PROMPT, "~");
        }
        // Concatenate " ==>>"
        PROMPT = realloc(PROMPT, strlen(PROMPT) + strlen("==>>") + 1);
        strcat(PROMPT, "==>> ");
        PROMPT_LEN = strlen(PROMPT);
    }
    free(cwd);
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

char* expand_env_vars(const char *input) {
    char *result = malloc(INITIAL_BUFFER_SIZE);
    size_t result_len = 0;
    size_t buf_size = INITIAL_BUFFER_SIZE;
    result[0] = '\0';

    for (size_t i = 0; input[i] != '\0';) {
        if (input[i] == '$' && (isalnum(input[i+1]) || input[i+1] == '_')) {
            // Extract variable name
            size_t var_start = ++i;
            while (isalnum(input[i]) || input[i] == '_') i++;
            size_t var_len = i - var_start;

            char var_name[var_len + 1];
            strncpy(var_name, &input[var_start], var_len);
            var_name[var_len] = '\0';

            char *var_value = getenv(var_name);
            if (var_value) {
                size_t value_len = strlen(var_value);
                while (result_len + value_len + 1 >= buf_size) {
                    buf_size *= 2;
                    result = realloc(result, buf_size);
                }
                strcat(result, var_value);
                result_len += value_len;
            }
        } else {
            // Normal character
            if (result_len + 1 >= buf_size) {
                buf_size *= 2;
                result = realloc(result, buf_size);
            }
            result[result_len++] = input[i++];
            result[result_len] = '\0';
        }
    }
    return result;
}

void parse_and_execute(char *input) {
    // Handle empty inputs
    if (input == NULL || strlen(input) == 0) {
        return;
    }

    char **args = malloc(INITIAL_BUFFER_SIZE * sizeof(char *));
    // Tokenize with strdup
    char *token = strtok(input, " \t");
    int i = 0;
    while (token != NULL) {
        args[i] = strdup(token);
        if (!args[i]) {
            perror("!!! strdup failed");
            exit(1);
        }
        i++;
        token = strtok(NULL, " \t");
    }
    args[i] = NULL;

    // Expand variables in all arguments
    for (int j = 0; j < i; j++) {
        char *expanded = expand_env_vars(args[j]);
        free(args[j]);
        args[j] = expanded;
    }

    if (strcmp(args[0], "exit") == 0) {
        for (int j = 0; j < i; j++) {
	    free(args[j]);
        }
        free(args);
	if(PROMPT != NULL) free(PROMPT);
        exit(0);
    } else if (args[0] && strcmp(args[0], "cd") == 0) {
        char *target_dir = NULL;
        static char *prev_dir = NULL; // Stores last directory for "cd -"

        if (!args[0] || strcmp(args[0], "cd") != 0) {
            return;
        }

        if (!args[1] || strcmp(args[1], "~") == 0) {
            // Case: "cd" or "cd ~" → go to $HOME
            target_dir = getenv("HOME");
        } else if (strcmp(args[1], "-") == 0) {
            // Case: "cd -" → go to previous directory
            if (prev_dir == NULL) {
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
            size_t expanded_size = strlen(home) + strlen(args[1] + 1) + 1;
            char *expanded_path = malloc(expanded_size);
            snprintf(expanded_path, expanded_size, "%s%s", home, args[1] + 1);
            target_dir = expanded_path;
            free(expanded_path);
        } else {
            // Case: "cd /normal/path"
            target_dir = args[1];
        }

        // Save current dir before changing (for "cd -")
        char *cwd = getcwd(NULL, 0);

        if (chdir(target_dir) != 0) {
            perror("cd failed");
        } else {
            // Update prev_dir only if cd succeeds
            if (prev_dir != NULL) {
                free(prev_dir); // Free the previous directory if it was allocated
            }
            prev_dir = cwd; // Store the current directory in prev_dir
        }
    }else if (strcmp(args[0], "pwd") == 0) {
        char *cwd = getcwd(NULL, 0);
        if(cwd == NULL){
            perror("getcwd failed");
            exit(1);
        }else{
            printf("%s\n", cwd);
        }
        free(cwd);
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
    for (int j = 0; j < i; j++) {
        free(args[j]);
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
        }else if (c == 127 || c == '\b') { // Backspace
            if (cursor > 0 && len > 0) {
                memmove(&buffer[cursor-1], &buffer[cursor], len - cursor);
                len--;
                cursor--;
                
                // Clear and redraw line
                printf("\r%s\033[K", PROMPT);  // Move to start, clear line
                fwrite(buffer, 1, len, stdout);
                
                // Move cursor to CORRECT position
                if (cursor < len) {
                    printf("\033[%zuD", len - cursor);  // FIXED: Use LEFT moves
                }
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
            printf("\r%s\033[K", PROMPT);
            fwrite(buffer, 1, len, stdout);
            if (cursor < len) {
                printf("\033[%zuD", len - cursor); // Adjust only if needed
            }
            fflush(stdout);
        }
    }
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &original);
    printf("\n");
    return buffer;
}

/* here DOES need to work on it. do not use! */
/*
void run(char *entry){
    for(int i = 0; i < strlen(entry); i++){
        
        if(entry[i] == '$' && entry[i + 1] != '$'){
            char *second = malloc(1);
            if(entry[i+1] == '('){
                for (int loc = i+2; entry[loc] != ')'; loc++){
                    realloc(second, strlen(second) + 1);
                    second[loc - (i + 2)] = entry[loc]; 
                }
                run(second);
                free(second);
            }
        }
    }
}
*/

int main(void) {
    while (1) {
        char *input = read_input();
        if (input == NULL || strlen(input) == 0) {
            free(input);
            continue; // Skip empty input
        }
        parse_and_execute(input);
        free(input);
    }
    return 0;
}
