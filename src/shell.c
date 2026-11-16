#include "shell.h"

char* read_cmd(char* prompt, FILE* fp) {
    printf("%s", prompt);
    char* cmdline = (char*) malloc(sizeof(char) * MAX_LEN);
    int c, pos = 0;

    while ((c = getc(fp)) != EOF) {
        if (c == '\n') break;
        cmdline[pos++] = c;
    }

    if (c == EOF && pos == 0) {
        free(cmdline);
        return NULL; // Handle Ctrl+D
    }
    
    cmdline[pos] = '\0';
    return cmdline;
}

char** tokenize(char* cmdline) {
    // Edge case: empty command line
    if (cmdline == NULL || cmdline[0] == '\0' || cmdline[0] == '\n') {
        return NULL;
    }

    char** arglist = (char**)malloc(sizeof(char*) * (MAXARGS + 1));
    for (int i = 0; i < MAXARGS + 1; i++) {
        arglist[i] = (char*)malloc(sizeof(char) * ARGLEN);
        bzero(arglist[i], ARGLEN);
    }

    char* cp = cmdline;
    char* start;
    int len;
    int argnum = 0;

    while (*cp != '\0' && argnum < MAXARGS) {
        while (*cp == ' ' || *cp == '\t') cp++; // Skip leading whitespace
        
        if (*cp == '\0') break; // Line was only whitespace

        start = cp;
        // Recognize special operators: <, >, |, and &
        if (*cp == '<' || *cp == '>' || *cp == '|' || *cp == '&') {
            len = 1;
            cp++;
        } else if (*cp == '"' || *cp == '\'') {
            char quote = *cp++;
            start = cp;
            len = 0;
            while (*cp != '\0' && *cp != quote) {
                len++;
                cp++;
            }
            if (*cp == quote) cp++; // Skip closing quote
        } else {
            len = 0;
            while (*cp != '\0' && !(*cp == ' ' || *cp == '\t' || *cp == '<' || *cp == '>' || *cp == '|' || *cp == '&')) {
                len++;
                cp++;
            }
        }
        strncpy(arglist[argnum], start, len);
        arglist[argnum][len] = '\0';
        argnum++;
    }

    if (argnum == 0) { // No arguments were parsed
        for(int i = 0; i < MAXARGS + 1; i++) free(arglist[i]);
        free(arglist);
        return NULL;
    }

    arglist[argnum] = NULL;
    return arglist;
}

// History storage
static char* history[HISTORY_SIZE];
static int history_count = 0;

void add_to_history(char* cmd) {
    if (cmd == NULL || cmd[0] == '\0') {
        return;
    }

    if (history_count < HISTORY_SIZE) {
        history[history_count] = strdup(cmd);
        history_count++;
    } else {
        // History full: remove oldest, shift left, add newest at end
        free(history[0]);
        for (int i = 1; i < HISTORY_SIZE; i++) {
            history[i - 1] = history[i];
        }
        history[HISTORY_SIZE - 1] = strdup(cmd);
    }
}

void print_history(void) {
    for (int i = 0; i < history_count; i++) {
        printf("%4d  %s\n", i + 1, history[i]);
    }
}

char* get_history(int n) {
    if (n < 1 || n > history_count) {
        return NULL;
    }
    return history[n - 1];
}

// Background jobs management
typedef struct {
    pid_t pid;
    char cmd[MAX_LEN];
} Job;

static Job bg_jobs[MAX_JOBS];
static int job_count = 0;

void add_job(pid_t pid, char* cmd) {
    if (job_count < MAX_JOBS) {
        bg_jobs[job_count].pid = pid;
        strncpy(bg_jobs[job_count].cmd, cmd, MAX_LEN - 1);
        bg_jobs[job_count].cmd[MAX_LEN - 1] = '\0';
        printf("[%d] %d\n", job_count + 1, pid);
        job_count++;
    }
}

void remove_job(pid_t pid) {
    for (int i = 0; i < job_count; i++) {
        if (bg_jobs[i].pid == pid) {
            for (int j = i; j < job_count - 1; j++) {
                bg_jobs[j] = bg_jobs[j + 1];
            }
            job_count--;
            break;
        }
    }
}

void check_zombies(void) {
    int status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        remove_job(pid);
    }
}

void print_jobs(void) {
    check_zombies();
    for (int i = 0; i < job_count; i++) {
        printf("[%d] %d %s\n", i + 1, bg_jobs[i].pid, bg_jobs[i].cmd);
    }
}

// Shell Variables Linked List
typedef struct Variable {
    char name[MAX_LEN];
    char value[MAX_LEN];
    struct Variable* next;
} Variable;

static Variable* var_list = NULL;

int set_variable(const char* name, const char* value) {
    Variable* curr = var_list;
    while (curr != NULL) {
        if (strcmp(curr->name, name) == 0) {
            strncpy(curr->value, value, MAX_LEN - 1);
            curr->value[MAX_LEN - 1] = '\0';
            return 0;
        }
        curr = curr->next;
    }

    Variable* new_var = (Variable*)malloc(sizeof(Variable));
    if (new_var == NULL) {
        return -1;
    }
    strncpy(new_var->name, name, MAX_LEN - 1);
    new_var->name[MAX_LEN - 1] = '\0';
    strncpy(new_var->value, value, MAX_LEN - 1);
    new_var->value[MAX_LEN - 1] = '\0';
    new_var->next = var_list;
    var_list = new_var;
    return 0;
}

char* get_variable(const char* name) {
    Variable* curr = var_list;
    while (curr != NULL) {
        if (strcmp(curr->name, name) == 0) {
            return curr->value;
        }
        curr = curr->next;
    }
    return getenv(name);
}

void print_variables(void) {
    Variable* curr = var_list;
    while (curr != NULL) {
        printf("%s=%s\n", curr->name, curr->value);
        curr = curr->next;
    }
}

int handle_assignment(const char* str) {
    const char* eq = strchr(str, '=');
    if (eq == NULL || eq == str) {
        return 0;
    }

    // Variable name must contain only alphanumeric characters or underscore
    for (const char* p = str; p < eq; p++) {
        if (!isalnum((unsigned char)*p) && *p != '_') {
            return 0;
        }
    }

    int name_len = eq - str;
    char name[MAX_LEN];
    strncpy(name, str, name_len);
    name[name_len] = '\0';

    const char* val_start = eq + 1;
    char val[MAX_LEN];
    int val_len = strlen(val_start);

    // Strip surrounding quotes if present (e.g. MESSAGE="Hello, World!")
    if (val_len >= 2 && ((val_start[0] == '"' && val_start[val_len - 1] == '"') ||
                         (val_start[0] == '\'' && val_start[val_len - 1] == '\''))) {
        strncpy(val, val_start + 1, val_len - 2);
        val[val_len - 2] = '\0';
    } else {
        strncpy(val, val_start, MAX_LEN - 1);
        val[MAX_LEN - 1] = '\0';
    }

    set_variable(name, val);
    return 1;
}

void expand_variables(char** arglist) {
    if (arglist == NULL) return;

    for (int i = 0; arglist[i] != NULL; i++) {
        if (arglist[i][0] == '$') {
            char* var_name = &arglist[i][1];
            char* var_val = get_variable(var_name);
            if (var_val == NULL) {
                var_val = "";
            }
            free(arglist[i]);
            arglist[i] = strdup(var_val);
        }
    }
}

int handle_builtin(char** arglist) {
    if (arglist == NULL || arglist[0] == NULL) {
        return 0;
    }

    // exit: terminates the shell gracefully
    if (strcmp(arglist[0], "exit") == 0) {
        exit(0);
    }

    // cd <directory>: changes current directory
    if (strcmp(arglist[0], "cd") == 0) {
        if (arglist[1] == NULL) {
            char* home = getenv("HOME");
            if (home != NULL) {
                chdir(home);
            }
        } else {
            if (chdir(arglist[1]) != 0) {
                perror("cd");
            }
        }
        return 1;
    }

    // history: displays command history
    if (strcmp(arglist[0], "history") == 0) {
        print_history();
        return 1;
    }

    // help: displays list of built-in commands
    if (strcmp(arglist[0], "help") == 0) {
        printf("Available built-in commands:\n");
        printf("  cd <path> : Change directory\n");
        printf("  exit      : Exit the shell\n");
        printf("  help      : Show this help message\n");
        printf("  history   : Show command history\n");
        printf("  jobs      : List background jobs\n");
        printf("  set       : List defined shell variables\n");
        printf("  !n        : Re-execute command n from history\n");
        return 1;
    }

    // jobs: lists running background jobs
    if (strcmp(arglist[0], "jobs") == 0) {
        print_jobs();
        return 1;
    }

    // set: lists defined shell variables
    if (strcmp(arglist[0], "set") == 0) {
        print_variables();
        return 1;
    }

    return 0;
}

// Built-in command names for Tab completion
static char* builtin_names[] = {
    "cd",
    "exit",
    "help",
    "history",
    "jobs",
    "set",
    NULL
};

// Generator function for command completion
static char* command_generator(const char* text, int state) {
    static int list_index, len;
    char* name;

    if (!state) {
        list_index = 0;
        len = strlen(text);
    }

    while ((name = builtin_names[list_index++])) {
        if (strncmp(name, text, len) == 0) {
            return strdup(name);
        }
    }

    return NULL;
}

// Custom completion handler
static char** shell_completion(const char* text, int start, int end) {
    (void)end;
    rl_attempted_completion_over = 0;

    // At start of line, complete command names
    if (start == 0) {
        char** matches = rl_completion_matches(text, command_generator);
        if (matches != NULL) {
            return matches;
        }
    }

    // Default: filename completion
    return NULL;
}

void init_readline(void) {
    rl_bind_key('\t', rl_complete);
    rl_attempted_completion_function = shell_completion;
}

int run_single_command(char* cmd) {
    if (cmd == NULL || cmd[0] == '\0') {
        return 0;
    }

    // Check for variable assignment before tokenizing (e.g. MSG=hello or MESSAGE="Hello, World!")
    if (handle_assignment(cmd)) {
        return 0;
    }

    char job_name[MAX_LEN];
    strncpy(job_name, cmd, MAX_LEN - 1);
    job_name[MAX_LEN - 1] = '\0';

    char** arglist = tokenize(cmd);
    if (arglist == NULL) {
        return 0;
    }

    // Check if token was an assignment
    if (arglist[1] == NULL && handle_assignment(arglist[0])) {
        for (int i = 0; arglist[i] != NULL; i++) free(arglist[i]);
        free(arglist);
        return 0;
    }

    // Pre-execution variable expansion step (replaces $VAR with its value)
    expand_variables(arglist);

    int is_bg = 0;
    int last = 0;
    while (arglist[last] != NULL) last++;

    if (last > 0 && strcmp(arglist[last - 1], "&") == 0) {
        is_bg = 1;
        free(arglist[last - 1]);
        arglist[last - 1] = NULL;
    }

    int status = 0;
    if (arglist[0] != NULL) {
        if (handle_builtin(arglist) != 0) {
            status = 0; // Built-in commands execute successfully
        } else {
            status = execute(arglist, is_bg, job_name);
        }
    }

    for (int i = 0; arglist[i] != NULL; i++) {
        free(arglist[i]);
    }
    free(arglist);
    return status;
}

void handle_if_block(char* cmdline) {
    // 1. Extract condition command after "if"
    char* condition_cmd = cmdline + 2;
    while (*condition_cmd == ' ' || *condition_cmd == '\t') {
        condition_cmd++;
    }

    char* then_cmds[20];
    int then_count = 0;
    char* else_cmds[20];
    int else_count = 0;

    int section = 0; // 0 = before then, 1 = in then block, 2 = in else block
    char* line;

    // 2. Read multi-line block until "fi"
    while ((line = readline("> ")) != NULL) {
        char* trimmed = line;
        while (*trimmed == ' ' || *trimmed == '\t') trimmed++;

        int len = strlen(trimmed);
        while (len > 0 && (trimmed[len - 1] == ' ' || trimmed[len - 1] == '\t' || trimmed[len - 1] == '\n')) {
            trimmed[len - 1] = '\0';
            len--;
        }

        if (strcmp(trimmed, "then") == 0) {
            section = 1;
            free(line);
            continue;
        } else if (strcmp(trimmed, "else") == 0) {
            section = 2;
            free(line);
            continue;
        } else if (strcmp(trimmed, "fi") == 0) {
            free(line);
            break; // Finished reading block
        }

        if (len > 0) {
            if (section == 1 && then_count < 20) {
                then_cmds[then_count++] = strdup(trimmed);
            } else if (section == 2 && else_count < 20) {
                else_cmds[else_count++] = strdup(trimmed);
            }
        }
        free(line);
    }

    // 3. Execute condition command and check exit code
    int exit_code = run_single_command(condition_cmd);

    // 4. Execute corresponding block
    if (exit_code == 0) {
        // Condition succeeded (status 0): execute then block
        for (int i = 0; i < then_count; i++) {
            run_single_command(then_cmds[i]);
        }
    } else {
        // Condition failed (status non-zero): execute else block (if any)
        for (int i = 0; i < else_count; i++) {
            run_single_command(else_cmds[i]);
        }
    }

    // 5. Free stored lines
    for (int i = 0; i < then_count; i++) {
        free(then_cmds[i]);
    }
    for (int i = 0; i < else_count; i++) {
        free(else_cmds[i]);
    }
}
