/**
 * Simple User-level Unix Shell
 *
 * @author Youngsoo Lee
 * @since 2018-03
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wait.h>

#define LINE_MAX_SIZE 4096

// Define Parsing States
#define PS_EMPTY 1
#define PS_NORMAL_STRING 2
#define PS_SINGLE_QUOTE 3
#define PS_DOUBLE_QUOTES 4

// Add pair information of start and end index to offsets, and set start var as 'end + 1'
#define add_offset_pair(offsets, length, start, end) \
	offsets[length++] = start; \
	offsets[length++] = end; \
	start = end + 1;

/**
 * Process command by original string, offset array and length.
 *
 * @param string: Original string (can be modified by this function)
 * @param offsets: Offset data that includes start and end index of each argument
 *    (Placed like pair (ex. start1, end1, start2, end2, ...))
 * @param offset_len: Length of offsets array
 * @return Pointer of command
 */
char** proc_command(char* string, int* offsets, int offset_len) {
	int command_len = offset_len / 2 + 1;
	char** command = (char**) malloc(sizeof(char*) * command_len);

	for (int i = 0; i < offset_len-1; i+=2) {
		command[i/2] = string + offsets[i]; //  Starting pointer of string
		string[offsets[i+1]] = '\0'; // Set null to end of argument
	}
	
	command[command_len - 1] = NULL;
	return command;
}

/**
 * Parse command from string
 *  Separates lines by semi-colon(`;`), separate arguments by space(` `), processes single-quote(`''`) and double-quotes(`""`)
 *  Semi-colons and spaces are ignored which are located inside of quote 
 *
 * @param string: String to parse
 * @return: Arrays of command
 */
char*** parse_commands(char* string) {
	int string_size = (int) strlen(string); // Save length of string

	int parsing_state = PS_EMPTY; // Inital state of parsing
		
	int *arg_offsets = (int*) malloc(sizeof(int) * string_size); // Contains starting end ending index of each arguments
	int arg_offset_starts = 0; // Current starting index
	int arg_offset_len = 0; // Length of current length of command

	char*** commands = (char***) malloc(sizeof(char**) * string_size); // Data for return
	int commands_len = 0; // Length of commands

	for (int i = 0; i < string_size + 1; i++) {
		char current_char = (i < string_size) ? string[i] : ';'; // If index exceeds length, set token to `;`

		switch (current_char) {
			case ';' :
				if (parsing_state == PS_SINGLE_QUOTE || parsing_state == PS_DOUBLE_QUOTES) {
					// Ignore token ';' if current character is in quotes
					continue;
				}

				if (parsing_state == PS_NORMAL_STRING) {
					// If there is remain string, add offset information to `arg_offsets`
					add_offset_pair(arg_offsets, arg_offset_len, arg_offset_starts, i);
				}

				// Save current command
				if (arg_offset_len > 0) {
					char** cmd = proc_command(string, arg_offsets, arg_offset_len);
					commands[commands_len++] = cmd;
				}

				// Reset arg_offsets
				arg_offset_len = 0; 
				arg_offset_starts = i+1;
				parsing_state = PS_EMPTY;
				break;

			case ' ' :
			case '\t' :
			case '\n' :
				if (parsing_state == PS_EMPTY) {
					// Ignore whitespace if current parsing state is EMPTY
					arg_offset_starts = i+1;

				}else if (parsing_state == PS_NORMAL_STRING) {
					// To parse string as argument, set offset of current loop index
					add_offset_pair(arg_offsets, arg_offset_len, arg_offset_starts, i);
					parsing_state = PS_EMPTY;
				}
				break;

			case '\'' :
				if (parsing_state == PS_SINGLE_QUOTE) {
					// Close single quote
					add_offset_pair(arg_offsets, arg_offset_len, arg_offset_starts, i);
					parsing_state = PS_EMPTY;

				}else if (parsing_state == PS_EMPTY) {
					// Open single quote
					parsing_state = PS_SINGLE_QUOTE;
					arg_offset_starts = i+1;
				}
				break;

			case '"' :
				if (parsing_state == PS_DOUBLE_QUOTES) {
					// Close double quote
					add_offset_pair(arg_offsets, arg_offset_len, arg_offset_starts, i);
					parsing_state = PS_EMPTY;

				}else if (parsing_state == PS_EMPTY) {
					// Open double quote
					parsing_state = PS_DOUBLE_QUOTES;
					arg_offset_starts = i+1;
				}
				break;

			default :
				if (parsing_state == PS_EMPTY) {
					// If some other chracter is shown on empty-state, set state to normal-string
					parsing_state = PS_NORMAL_STRING;
				}
				break;				
		}
	}
	
	free(arg_offsets);

	commands[commands_len++] = NULL;
	return commands;
}

int main (int argc, char *argv[]) {
	char buffer[LINE_MAX_SIZE];
	char*** commands;
	char** command;
	
	pid_t pid;
	int status;

	if (argc == 2) {
		// If there are two arguemnts, run as 'batch mode'
		// Then in this case, change 'stdin' stream to 'argv[1]'
		freopen(argv[1], "r", stdin);
	}

	while (1) {
		if (feof(stdin) != 0) {
			// If end of file, return
			return 0;
		}
		
		// Print "prompt" on interactive mode
		printf("prompt> ");

		// Get commands from stdin to buffer
		fgets(buffer, LINE_MAX_SIZE, stdin);

		// Parse commands from buffer
		commands = parse_commands(buffer);
		
		while ((command = *(commands++))) {
			// Iterate commands and set current object to `command` var

			if (strcmp(command[0], "quit") == 0) {
				// If command is "quit", exit program
				return 0;
			}

			if (strcmp(command[0], "") == 0) {
				// Ignore if command is blank
				break;
			}

			pid = fork(); // Make child process

			if (pid < 0) {
				printf("fork failed");

			}else if (pid == 0) {
				// Child process
				execvp(command[0], command); // Run commands
				exit(-1); // If error, this line would be executed

			}else {
				// Parent process
				wait(&status);

				if (status != 0) {
					printf("Command fails\n");
				}
			}
		}
	}

	return 0;
}
