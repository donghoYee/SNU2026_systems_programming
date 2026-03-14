#include <stdio.h>
#include <assert.h>
#include <stdlib.h>

/* State definitions for DFA (Deterministic Finite Automaton) */
enum State {
    NORMAL,             // Default state
    SLASH,              // Encountered '/' (waiting to check if it's the start of a comment)
    IN_BLOCK_COMMENT,   // Inside a '/*' block comment
    BLOCK_COMMENT_STAR, // Encountered '*' inside a block comment (waiting to check for termination)
    IN_LINE_COMMENT,    // Inside a '//' single-line comment
    IN_STRING,          // Inside a '"' string constant
    STRING_ESCAPE,      // Encountered '\' (escape sequence) inside a string
    IN_CHAR,            // Inside a '\'' character constant
    CHAR_ESCAPE         // Encountered '\' (escape sequence) inside a character constant
};

/* This is skeleton code for reading characters from 
standard input (e.g., a file or console input) one by one until 
the end of the file (EOF) is reached. It keeps track of the current 
line number and is designed to be extended with additional 
functionality, such as processing or transforming the input data. 
In this specific task, the goal is to implement logic that removes 
C-style comments from the input. */

int main(void)
{
  // ich: int type variable to store character input from getchar()
  int ich;
  // line_cur & line_com: current line number and comment line number
  int line_cur, line_com;
  // ch: character that comes from casting (char) on ich
  char ch;
  
  // Initial state of the DFA
  enum State state = NORMAL;

  line_cur = 1;
  line_com = -1;

  // This while loop reads all characters from standard input one by one
  while (1) {
    ich = getchar();
    if (ich == EOF) 
      break;

    ch = (char)ich;

    // Process the input character (ch) based on the current state
    switch (state) {
        case NORMAL:
            if (ch == '/') state = SLASH;
            else if (ch == '"') { putchar(ch); state = IN_STRING; }
            else if (ch == '\'') { putchar(ch); state = IN_CHAR; }
            else putchar(ch);
            break;

        case SLASH:
            if (ch == '*') {
                putchar(' ');             // Replace the comment with a single space
                line_com = line_cur;      // Record the line number where the comment started
                state = IN_BLOCK_COMMENT;
            } else if (ch == '/') {
                putchar(' ');             // Replace the comment with a single space
                state = IN_LINE_COMMENT;
            } else if (ch == '"') {
                putchar('/');             // Print the previously held '/'
                putchar(ch);
                state = IN_STRING;
            } else if (ch == '\'') {
                putchar('/');             // Print the previously held '/'
                putchar(ch);
                state = IN_CHAR;
            } else {
                putchar('/');             // Process as a simple '/' character
                putchar(ch);
                state = NORMAL;
            }
            break;

        case IN_BLOCK_COMMENT:
            if (ch == '*') state = BLOCK_COMMENT_STAR;
            else if (ch == '\n') putchar('\n'); // Print newline to preserve original line numbering
            break;

        case BLOCK_COMMENT_STAR:
            if (ch == '/') state = NORMAL; // '*/' comment termination
            else if (ch == '*') {
                // Maintain state to handle consecutive asterisks (e.g., '/**')
            } else if (ch == '\n') {
                putchar('\n');
                state = IN_BLOCK_COMMENT;
            } else {
                state = IN_BLOCK_COMMENT;
            }
            break;

        case IN_LINE_COMMENT:
            if (ch == '\n') {
                putchar('\n');
                state = NORMAL;
            }
            break;

        case IN_STRING:
            if (ch == '"') {
                putchar(ch);
                state = NORMAL;
            } else if (ch == '\\') {
                putchar(ch);
                state = STRING_ESCAPE;
            } else {
                putchar(ch);
            }
            break;

        case STRING_ESCAPE:
            putchar(ch);
            state = IN_STRING;
            break;

        case IN_CHAR:
            if (ch == '\'') {
                putchar(ch);
                state = NORMAL;
            } else if (ch == '\\') {
                putchar(ch);
                state = CHAR_ESCAPE;
            } else {
                putchar(ch);
            }
            break;

        case CHAR_ESCAPE:
            putchar(ch);
            state = IN_CHAR;
            break;
    }

    // Keep track of the current line number
    if (ch == '\n')
      line_cur++;
  }
  
  // Handle exceptional states after the loop ends (EOF)
  if (state == SLASH) {
      // If the file ends right after a '/'
      putchar('/');
  } else if (state == IN_BLOCK_COMMENT || state == BLOCK_COMMENT_STAR) {
      // If the file ends with an unterminated block comment (Requirements 8, 9)
      fprintf(stderr, "Error: line %d: unterminated comment\n", line_com);
      return EXIT_FAILURE;
  }
  
  return EXIT_SUCCESS;
}