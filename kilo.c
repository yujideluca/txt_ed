//SPECIFIC FEATURES OF THIS CODE//
/*
escape sequence (\x1b[<command>): 
    "\x1b" means the hexadecimal value 1b (which represents Esc in ascii)
    [ separates Esc of the rest of the command (usually a number and a 
    letter, uppercase or not - it is case sensitive)
*/
//INCLUDES//
//those 3 are compiling reiquirements for getline()
#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

//DEFINES//
#define KILO_VERSION "0.0.1"
#define KILO_TAB_STOP 4
#define KILO_QUIT_TIMES 2

#define CTRL_KEY(k) ((k) & 0x1f)

/* by setting the first constant in the enum to 1000, the rest of the 
constants get incrementing values of 1001, 1002, 1003, and so on*/
enum editorKey {
    BACKSPACE = 127,
    ARROW_LEFT = 1000,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
    DEL_KEY,
    HOME_KEY,
    END_KEY,
    PAGE_UP,
    PAGE_DOWN
};

//DATA//
typedef struct erow {
    int size;
    int rsize;
    char *chars;
    char *render;
} erow;

struct editorConfig{
    int cx, cy;
    int rx;
    int rowoff;
    int coloff;
    int screenrows;
    int screencols;
    int numrows;
    //E.row is an array of erow structs
    erow *row; // editor row (can be called just as erow instead of struct erow thanks to typedef)
    int dirty;
    char *filename;
    char statusmsg[80];
    time_t statusmsg_time; 
    struct termios orig_termios;
};

struct editorConfig E;


/*The needs to know the arguments and return the value of 
that function. Prototype functions elp to sort things out*/
//PROTOTYPES//
void editorSetStatusMessage(const char *fmt, ...);
void editorRefreshScreen();
char *editorPrompt(char *prompt, void (*callback)(char *, int));

//TERMINAL// -> low-level terminal inputs

// die function is a error handler (prints error message and exits)
void die(const char *s){
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);
    //perror looks to the global errno variable and prints a error message
    //suposed to give context about the error
    perror(s);
    exit(1);
}

void disableRawMode() {
    //if tcsetattr fails to give a text file, point out an error
    //STDIN_FILENO accepts the input, but does nothing with it
    //TCSAFLUSH erases the data after repasing
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
        die("tcsetattr");
}

void enableRawMode() {
    //if tcgetattr fails to give a text file, point out an error
    //the struct orig_termios stores the current state of the terminal
    if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1) 
        die("tcgetattr");
    //calls disable when exits
    atexit(disableRawMode);
    //makes a copy of the struct of the terminal's state
    struct termios raw = E.orig_termios;
    //modifies the copy removing unwanted keys
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    //CS8 is a bit mask w/ multiple bits
    //the bitwise-OR (|) sets the character size (CS) to 8
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    //minimum value of bytes of input before read() be allowed to return
    raw.c_cc[VMIN] = 0;
    //maximum amount of time to wait before read() returns
    raw.c_cc[VTIME] = 1;
    //sets the atributes when finished
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) 
        die("tcgetattr");
}

//waits for a keypress and returns it
int editorReadKey() {
    int nread;
    char c;
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1){
        //EAGAIN  means that there is no data available right now
        //errno != EAGAIN means that your error cant be solved by 
        //checking if the data is available later
        if (nread == -1 && errno != EAGAIN)
            die("read");
    }

    if (c == '\x1b') {

        char seq[3];
        if (read(STDIN_FILENO, &seq[0], 1) != 1)
            return '\x1b';
        if (read(STDIN_FILENO, &seq[1], 1) != 1)
            return '\x1b';
        if (seq[0] == '[') {
            if (seq[1] >= '0' && seq[1] <= '9') {
                if (read(STDIN_FILENO, &seq[2], 1) != 1)
                    return '\x1b';
                if (seq[2] == '~') {
                    switch (seq[1]) {
                        case '1': return HOME_KEY;
                        case '3': return DEL_KEY;
                        case '4': return END_KEY;
                        case '5': return PAGE_UP;
                        case '6': return PAGE_DOWN;
                        case '7': return HOME_KEY;
                        case '8': return END_KEY;
                    }
                }
            } else {
                switch (seq[1]) {
                    case 'A': return ARROW_UP;
                    case 'B': return ARROW_DOWN;
                    case 'C': return ARROW_RIGHT;
                    case 'D': return ARROW_LEFT;
                    case 'H': return HOME_KEY;
                    case 'F': return END_KEY;
                }
            }
        } else if (seq[0] == 'O') {
            switch (seq[1]) {
                case 'H' : return HOME_KEY;
                case 'F' : return END_KEY;
            }
        }

        return '\x1b';
    } else {
        return c;
    } 
}

int getCursorPosition(int *rows, int *cols) {
    //assigning a buffer
    char buf[32];
    unsigned int i = 0;
    //checks the buffer size and checks if the stored data size makes sense
    if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4)
        return -1;
    //reads characters in the buffer until the R character
    while (i < sizeof(buf) -1) {
        if (read(STDIN_FILENO, &buf[i], 1) != 1)
            break;
        if (buf[i] == 'R')
            break;
        i++;
     }
     buf[i] = '\0'; 
    
    if (buf[0] != '\x1b' || buf[1] != '[')
        return -1;
    //reads the input of the buffer and passes a string
    //with two integer separated by ";" 
    //those integers are put into the variables rows and cols  
    if (sscanf(&buf[2], "%d;%d", rows, cols) != 2)
        return -1;
    return 0;
}

int getWindowSize(int *rows, int *cols) {
    struct winsize ws;

    // ioctl() place columns and rows to a winsize struct, in our case, the ws struct
    //when ioctl() fails, it returns -1
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        // Esc + number + C command moves the cursor to the right
        // Esc + number + B command moves the cursor down
        //the value 999 is large enough to ensure the cursor to go to the end of the screen
        if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12)
            return -1;
        return getCursorPosition(rows, cols);
    //when ioctl() succeeds, it makes the variables col and row
    //to access the data of the struct ws and returns zero.
    } else {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
}

//ROW OPERATIONS//
int editorRowCxToRx(erow *row, int cx) {
    int rx = 0;
    int j;
    for (j = 0; j < cx; j++) {
        if (row->chars[j] == '\t')
            rx += (KILO_TAB_STOP - 1) - (rx % KILO_TAB_STOP);
        rx++;
    }
    return rx;
}

int editorRowRxToCx(erow *row, int rx) {
    int cur_rx = 0;
    int cx;
    /*loop through the chars string calculating the rx value and stops 
    when cur_cx hits a given rx value and returns cx*/
    for (cx = 0; cx < row->size; cx++) {
        if (row->chars[cx] == '\t')
            cur_rx += (KILO_TAB_STOP - 1) - (cur_rx % KILO_TAB_STOP);
        cur_rx++;

        if (cur_rx > rx)
            return cx;
    }
    return cx;
}

//fills the render string with the content of an erow
void editorUpdateRow(erow *row) {
  int tabs = 0;
  int j;
  for (j = 0; j < row->size; j++)
    // '\t' is a escape sequence to tab
    if (row->chars[j] == '\t') 
        tabs++;

  free(row->render);
  /*allocates the memory of the size necessary: 
  1 byte for each character, 4 for each tab (because each tab is
  4 spaces in this code according to KILO_TAB_STOP 4)*/
  row->render = malloc(row->size + tabs*(KILO_TAB_STOP - 1) + 1);
  int idx = 0;

  for (j = 0; j < row->size; j++) {
    if (row->chars[j] == '\t') {
      row->render[idx++] = ' ';
      /*when the number of spaces added is the same as the size 
      of KILO_TAB_STOP, the while stops*/
      while (idx % KILO_TAB_STOP != 0) 
        row->render[idx++] = ' ';
    //if the char is not a tab, you just add it to the render  char 
    } else {
      row->render[idx++] = row->chars[j];
    }
  }
  //recieves the characters copied to row->render
  row->render[idx] = '\0';
  row->rsize = idx;
}

void editorInsertRow(int at, char *s, size_t len) {
    //at validation
    if (at < 0 || at > E.numrows)
        return;

    //allocates bytes for each row times the size of the  row
    E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));
    //makes room at the specified index for the new row
    memmove(&E.row[at + 1], &E.row[at], sizeof(erow) * (E.numrows - at));

    E.row[at].size = len;
    E.row[at].chars = malloc(len + 1);
    memcpy(E.row[at].chars, s, len);
    E.row[at].chars[len] = '\0';

    E.row[at].rsize = 0;
    E.row[at].render = NULL;
    editorUpdateRow(&E.row[at]);
    E.numrows++;
    E.dirty++;
}

void editorFreeRow(erow *row) { //frees the memory of the deleted erow
    free(row->render);
    free(row->chars);
}

void editorDelRow(int at) {
    if (at < 0 || at >= E.numrows)
        return;
    editorFreeRow(&E.row[at]);
    //copies the content of E.row[at +1] in E.row[at], which was freed at the command above
    memmove(&E.row[at], &E.row[at + 1], sizeof(erow) * (E.numrows - at - 1));
    E.numrows--; //one row less now
    E.dirty++;
}

void editorRowInsertChar(erow *row, int at, int c) {
    if (at < 0 || at > row->size)
        at = row->size;
    //reallocation with the size of the chars +2 because you have to fit the char and the null byte
    row->chars = realloc(row->chars, row->size +2);
    /*copies memory block into a new location, but not like memcpy
    "In general, memcpy is implemented in a simple (but fast) manner. 
    Simplistically, it just loops over the data (in order), copying 
    from one location to the other. This can result in the source 
    being overwritten while it's being read. Memmove does more work 
    to ensure it handles the overlap correctly."*/
    /*memmove(pointer to destination, pointer to source, number of bytes to copy)*/
    memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);
    row->size++;
    row->chars[at] = c;
    editorUpdateRow(row);
    E.dirty++; //the bigger the number, "dirtier" it is
}

//deletes a character in an erow (backspace)
void editorRowDelChar(erow *row, int at) {
    if (at < 0 || at >= row->size)
        return;
    //overwrite the deleted character with the characters that come after it 
    memmove(&row->chars[at], &row->chars[at + 1], row->size - at);
    row->size--;
    editorUpdateRow(row);
    E.dirty++;
}

//EDITOR OPERATIONS//
void editorInsertChar(int c) {
    /*If E.cy == E.numrows, then the cursor is on the tilde line after 
    the end of the file, so we need to append a new row to the file 
    before inserting a character there */
    if (E.cy == E.numrows) {
        editorInsertRow(E.numrows, "", 0);
    }
    //inserts char
    editorRowInsertChar(&E.row[E.cy], E.cx, c);
    //moves cursor forward, so the next char does not overlap the first
    E.cx++;
}

void editorinsertNewline() {
    if (E.cx == 0) { // if the cursor is in the start of the row
        editorInsertRow(E.cy, "", 0);
    } else {
        erow *row = &E.row[E.cy];
        editorInsertRow(E.cy + 1, &row->chars[E.cx], row->size - E.cx);
        row = &E.row[E.cy];
        row->size = E.cx;
        row->chars[row->size] = '\0';
        editorUpdateRow(row);
    }
    E.cy++;
    E.cx = 0;
}

void editorRowAppendString(erow *row, char *s, size_t len) {
    row->chars = realloc(row->chars, row->size + len + 1); //expand the row size
    memcpy(&row->chars[row->size], s,len); //copy the content to the end of the row
    row->size += len; //update row size
    row->chars[row->size] = '\0';
    editorUpdateRow(row);
    E.dirty++;
}

//applies editorRowDelChar to delete the character that is to the left of the cursor.
void editorDelChar() {
    if (E.cy == E.numrows)
        return;
    if (E.cx == 0 && E.cy ==0) //if the cursor is at the beggining of the first line
        return;

    erow *row = &E.row[E.cy];
    if (E.cx > 0) {
        editorRowDelChar(row, E.cx - 1);
        E.cx--; //moves the cursor to the left after deleting the character
    } else { //if the cursor is at the begining of the line
        E.cx = E.row[E.cy - 1].size;
        editorRowAppendString(&E.row[E.cy - 1], row->chars, row->size);
        editorDelRow(E.cy);
        E.cy--;
    }
}char *editorPrompt(char *prompt, void (*callback)(char *, int));

char *editorRowsToString(int *buflen) {
    int totlen = 0;
    int j;
    for (j = 0; j < E.numrows; j++)
        totlen += E.row[j].size + 1;
    *buflen = totlen;

    char *buf = malloc(totlen);
    char *p = buf;
    for (j = 0; j < E.numrows; j++) {
        memcpy(p, E.row[j].chars, E.row[j].size);
        p += E.row[j].size;
        *p = '\n';
        p++;
    }

    return buf;
}

//editorOpen() takes a filename and opens the file for reading using fopen()
void editorOpen(char *filename) {
    free(E.filename);
    // strdup() makes a copy of the given string (filename)
    //it alocates the required memory assuming you will free() it 
    E.filename = strdup(filename);
    FILE *fp = fopen(filename, "r");
    //! is changing fp from nonzero statement to zero and vice-versa
    if (!fp) 
        die("fopen");
    
    char *line = NULL;
    size_t linecap = 0;
    ssize_t linelen;
    //linelen = getline(&line, &linecap, fp);
    //if (linelen != -1) {
    while ((linelen = getline(&line, &linecap, fp)) != -1) {
        while (linelen > 0 && (line[linelen -1] == '\n' || line[linelen -1] == '\r'))
            linelen--;
        editorInsertRow(E.numrows, line, linelen); //it increments E.dirty, but it is not supposed to happen when it has just opened
        ////set size to o the length of the message
        //E.row.size = linelen; 
        //E.row.chars = malloc(linelen + 1);
        ////copies the num of characters of linelen from line to E.row.chars
        //memcpy(E.row.chars, line, linelen);
        //E.row.chars[linelen] = '\0';
        //E.numrows = 1;
    }
    free(line);
    fclose(fp);
    E.dirty = 0; //corrects the incrementation when the while loop calls editorInsertRow()
}

void editorSave() {
    /*Note: If you’re using Bash on Windows, you will have to press Escape 3 
    times to get one Escape keypress to register in our program, because the 
    read() calls in editorReadKey() that look for an escape sequence never time out*/
    if (E.filename == NULL) { //this means it is a new file
        E.filename = editorPrompt("Save as: %s (Esc to cancel)", NULL);
        //the file will not be null anymore if 
        if (E.filename == NULL) {
            editorSetStatusMessage("save aborted");
            return;
        }
    }
    int len;
    char *buf = editorRowsToString(&len);

    // O_CREAT creates a new file if it doesn't already existis
    // O_RDWR opens for read and writing 
    /*" 0644 is the standard permissions you usually want for text files.
    It gives the owner of the file permission to read and write the file, 
    and everyone else only gets permission to read the file"*/
    int fd = open(strcat(E.filename, ".txt"), O_RDWR | O_CREAT, 0644);
    /*"The normal way to overwrite a file is to pass the O_TRUNC 
    flag to open(), which truncates the file completely, making 
    it an empty file, before writing the new data into it. By 
    truncating the file ourselves to the same length as the data 
    we are planning to write into it, we are making the whole overwriting 
    operation a little bit safer in case the ftruncate() call succeeds but the write() call fails. "*/
    if (fd != -1) {
        if (ftruncate(fd, len) != -1) { //truncates the size of the file
            close(fd);
            free(buf);
            E.dirty = 0; //now the modified code was saved, so it is not "dirty" anymore
            editorSetStatusMessage("%d bytes written to disk", len);
            return;
        }
        close(fd);
    }
    free(buf);
    editorSetStatusMessage("Can't save! I/O error: %s", strerror(errno));
}

//FIND//
void editorFindCallBack(char *query, int key) {
    static int last_match = -1;
    static int direction = 1;
    //returns immediately Enter or Esc when one of them is presed
    if (key == '\r'|| key == '\x1b') {
        last_match = -1;
        direction = 1;
        return;
    } else if (key == ARROW_RIGHT || key == ARROW_DOWN) {
        direction = 1;
    } else if (key == ARROW_LEFT || key == ARROW_UP) {
        direction = -1;
    } else {
        last_match = -1;
        direction = 1;
    }

    if (last_match == -1)
        direction = 1;
    
    //current is the index of the current searched row 
    int current = last_match;
    int i;

    //loop through all the rows of the file    
    for (i = 0; i < E.numrows; i++) {
        current += direction;
        if (current == -1)
            current = E.numrows -1;
        else if (current == E.numrows)
            current = 0;

        // *row points to the currently analyzed row
        erow *row = &E.row[current];
        /*finds the first occurrence of the substring (query) in the string (row->render).
        The terminating '\0' characters are not compared. This function RETURNS A POINTER 
        TO THE FIRST OCCURENCE in row->render of any of the entire sequence of characters 
        specified in query, or a null pointer if the sequence is not present in haystack.*/
        char *match = strstr(row->render, query);
        if (match) {
            last_match = current;
            // i checks the row, so the current row analysed is always i
            E.cy = current;
            //subtracts row->render in order to know where in row->render the match happens
            E.cx = editorRowRxToCx(row, match - row->render);
            E.rowoff = E.numrows;
            break;
        }
    }
}

void editorFind() {
    int saved_cx = E.cx;
    int saved_cy = E.cy;
    int saved_coloff = E.coloff;
    int saved_rowoff = E.rowoff;
    //query is a substring of the current row
    char *query = editorPrompt("Search %s (Use Esc/Arrows/Enter)", editorFindCallBack);
    // query == NULL means Esc was
    if (query == NULL) {
        free(query);
        } else {
            E.cx = saved_cx;
            E.cy = saved_cy;
            E.coloff = saved_coloff;
            E.rowoff = saved_rowoff; 
        } 
}

//APPEND BUFFER//
//pointer to the buffer memory and a length
struct abuf {
    char *b;
    int len;
};
//ABUF_INIT is a constant that represents an empty buffer
//acts as an constructor to abuf type
#define ABUF_INIT {NULL, 0}

void abAppend(struct abuf *ab, const char *s, int len) {
    
    //realloc gives a block of memory resized
    //new size = current size + size of the appended string
    char *new = realloc(ab->b, ab->len + len);
    
    //to prevent to append nothing to the buffer
    if (new == NULL)
        return;
    //copies the memory of the string s after the end of the current
    //data in the buffer and updates the abuf's lenght
    memcpy(&new[ab->len], s, len);
    ab->b = new;
    ab->len += len;
}

void abFree(struct abuf *ab) {
    free(ab->b);
}

//OUTPUT//
void editorScroll() {
    E.rx = 0;
    //sets E.rx to its proper value
    if (E.cy < E.numrows) {
        E.rx = editorRowCxToRx(&E.row[E.cy], E.cx);
    }
    // E.rowoff is used to check if the cursor moved outside the visible window
    //if it is outside, is adjusts
    if (E.cy < E.rowoff) { //checks if the cursor is above the window
        E.rowoff = E.cy;
    }
    if (E.cy >= E.rowoff + E.screenrows) { //checks if the cursor is under the window
        E.rowoff = E.cy - E.screenrows + 1;
    }
    if (E.rx < E.coloff) { //checks if the cursor is left to the window
        E.coloff = E.rx;
    }
    if (E.rx >= E.coloff + E.screencols) { //checks if the cursor is right to the window
        E.coloff = E.rx - E.screencols + 1;
    }
}

//draws the selected symbol ("~" for now) in all columns read and stored in the E.screencols
void editorDrawRows(struct abuf *ab) {
    int y;
    for (y = 0; y < E.screenrows; y++) {
        //filerow gets the number row of the file and uses it as index of E.row
        int filerow = y + E.rowoff;
        //if the current drawing row is part of the text buffer
        if (filerow >= E.numrows) {
            if (E.numrows == 0 && y == E.screenrows / 3) {
                //welcome is a buffer to interpolate KILO_VERSION into the welcomimng page
                char welcome[80];
                int welcomelen = snprintf(welcome, sizeof(welcome),
                    "yeah %s", KILO_VERSION);
                //if the size of the buffer is too big, it adjusts to the screen size
                if (welcomelen > E.screencols) 
                    welcomelen = E.screencols;
                //finds the middle of the screen
                int padding = (E.screencols - welcomelen) / 2;
                // if (padding) is the same as if (padding != 0)
                if (padding) {
                    abAppend(ab, "~", 1);
                    padding--;
                } 
                // while (padding-1 != 0)
                while (padding--) 
                    abAppend(ab, " ", 1);
                abAppend(ab, welcome, welcomelen);
            } else {
                abAppend(ab, "~", 1);
            }
        } else {
            //if the current drawing row comes after the text buffer
            int len = E.row[filerow].rsize - E.coloff;
            if (len < 0) // happens when it is above the screen
                len = 0; //returns to the leftmost column
            if (len > E.screencols)
                len = E.screencols;
            abAppend(ab, &E.row[filerow].render[E.coloff], len);
        }
        //cleans each line afte it is redrawn
        // K erases in line (K2 erases the whole line)
        abAppend(ab, "\x1b[K", 3);
        //prints a newline after the last row it draws, 
        //since the status bar is the final line being drawn on the screen
        abAppend(ab, "\r\n", 2);
    }
}

void editorDrawStatusBar(struct abuf *ab) {
    //7m inverts the colors
    abAppend(ab, "\x1b[7m", 4);
    char status[80], rstatus[80];
    //if E.filename != 0, then E.filename = E.filename, else, E.filename = "[No Name]"
    //if E.dirty != 0, then "(modified)", else, ""
    int len = snprintf(status, sizeof(status), "%.20s - %d lines %s", E.filename ? E.filename : "[No Name]", E.numrows, E.dirty ? "(modified)" : "");
    //sums 1 to E.cy is zero indexed 
    int rlen = snprintf(rstatus, sizeof(rstatus), "%d/%d", E.cy + 1, E.numrows);
    if(len > E.screencols)
        len = E.screencols; 
    abAppend(ab, status, len);
    while (len < E.screencols) {
        if (E.screencols - len == rlen) {
            abAppend(ab, rstatus, rlen);
            break;
        } else {
            abAppend(ab, " ", 1);
            len++;   
        }
    }
    //the m command in causes the text printed after it to be 
    //printed with various possible attributes including: 
    //bold (1), underscore (4), blink (5), and inverted colors (7) 
    abAppend(ab, "\x1b[m", 3);
    abAppend(ab, "\r\n", 2);
}

void editorDrawMessageBar(struct abuf *ab){
    //clears message bar
    abAppend(ab, "\x1b[K", 3);
    int msglen = strlen(E.statusmsg);
    if (msglen > E.screencols) 
        msglen = E.screencols;
    if (msglen && time(NULL) - E.statusmsg_time < 5)
        abAppend(ab, E.statusmsg, msglen);
}

void editorRefreshScreen() {
    editorScroll();

    struct abuf ab = ABUF_INIT;
    // \x1b means 27 in hexa, whitch corresponds to Esc in the Ascii table
    // Esc sequences are used to instruct terminal to formatting tasks
    // [ is used as delimitator, what goes after is processed
    // l command turns off terminal features
    // ?25 is a doesnt document argument. so ?25l turns off the cursor
    abAppend(&ab, "\x1b[?25l", 6); 
    // H corresponds to cursor position
    // H command recieves 2 arguments, the vertical and horizontal positions
    // in a 20x10 terminal, move the cursor to center would be: \x1b[5;10H
    // ergo, \x1b[H is \x1b[0;0H which corresponds to put the cursor in the top left corner
    abAppend(&ab, "\x1b[H", 3);
    //after mapping the terminal, all the lines are drawn the selected symbol 
    editorDrawRows(&ab);
    editorDrawStatusBar(&ab);
    editorDrawMessageBar(&ab);
    //moves the cursor to the origin (terminal uses 1-indexed values)
    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.rowoff) + 1, (E.rx - E.coloff) + 1);
    abAppend(&ab, buf, strlen(buf));
    //?25H turns the cursor back up
    abAppend(&ab, "\x1b[?25h", 6);
    //writes everything that was previously appended to the buffer
    write(STDOUT_FILENO, ab.b, ab.len);
    // abFree() deallocates dynamic memory used by abuf
    abFree(&ab);
    // the \x1b[?25l and \x1b[?25H might not be supported, then they will be ignored 
}

/*"The three dots '...' are called an ellipsis. Using them in a function makes that 
function a variadic function. To use them in a function declaration means that the 
function will accept an arbitrary number of parameters after the ones already defined"*/
void editorSetStatusMessage(const char *fmt, ...) {
    va_list ap;
    //initialises va_list
    va_start(ap, fmt); //fmt is a reference to the function's last parameter
    vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
    va_end(ap); //cleans up
    E.statusmsg_time = time(NULL);
}

//INPUT//
char *editorPrompt(char *prompt, void (*callback)(char *, int)) {
    size_t bufsize = 128;
    //buf is where the string is dinamically alocated
    char *buf = malloc(bufsize);

    size_t buflen = 0;
    buf[0] = '\0';

    while (1) {
        editorSetStatusMessage(prompt, buf);
        editorRefreshScreen();

        int c = editorReadKey();
        if (c == DEL_KEY || c == CTRL_KEY('h') || c == BACKSPACE) {
            if (buflen != 0) buf[--buflen] = '\0';
        //if Esc (\x1b) is pressed 
        } else if (c == '\x1b') {
            editorSetStatusMessage("");
            //frees the buf's memory
            free(buf); 
            /*the three if (callbackbuf, c) allow the caller to pass NULL 
            for the callback if it is not needed*/
            if (callback)
                callback(buf, c);
            return NULL;
        } else if (c == '\r') {
            if (buflen !=0) {
                editorSetStatusMessage("");
                if (callback)
                    callback(buf, c);
                return buf;
            }
        } else if (!iscntrl(c) && c < 128) {
            //if buflen reached maximum capacity
            if (buflen == bufsize - 1) {
                //double the capacity
                bufsize *= 2;
                //reallocate the buffer to bufsize
                buf = realloc(buf, bufsize);
            }
            buf[buflen++] = c;
            /*buf always ends with '\0', because 
            editorSetStatusMessage() and editorPrompt() will use it to know where the string ends */
            buf[buflen] = '\0';
        }

        if (callback)
            callback(buf, c);
    }
}

void editorMoveCursor(int key) {
    // if (E.cy >= E.numrows) -> NULL, else: &E.row[E.cy]
    erow *row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];

    switch (key) {
        case ARROW_LEFT:
            if (E.cx != 0) {
                E.cx--;
            } else if (E.cy > 0) { // if you go left past the line
                E.cy--;  // go one row up
                E.cx = E.row[E.cy].size; // go to the last column
            }
            break;
        case ARROW_RIGHT:
            if (row && E.cx < row -> size) {
                E.cx++;
            } else if (row && E.cx == row -> size) { // if the cursor is at the end of the cursor
                E.cy++; //move to the next line
                E.cx = 0; //at the start of the line
            }
            break;
        case ARROW_UP:
            if (E.cy != 0) {
                E.cy--;
            }
            break;
        case ARROW_DOWN:
            if (E.cy < E.numrows) {
                E.cy++;
            }
            break;
    }

    row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
    int rowlen = row ? row -> size : 0;
    if (E.cx > rowlen) {
        E.cx = rowlen;
    }
}

//waits for a keypress and handles it
void editorProcessKeypress() {
    static int quit_times = KILO_QUIT_TIMES;

    int c = editorReadKey();
    //this switch has the keypress cases in it
    switch (c) {
        case '\r':
            editorinsertNewline();
            break;

        case CTRL_KEY('q'):
            if (E.dirty && quit_times > 0) {
                editorSetStatusMessage("WARNING!!! File has unsaved changes. Press Ctrl-Q %d more times to quit.", quit_times);
                quit_times--;
                return;
            }
            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H", 3);    
            exit(0);
            break;

        case CTRL_KEY('s'):
            editorSave();
            break;

        case HOME_KEY:
            E.cx = 0;
            break;

        // If there is no current line, then E.cx must be 0 and it should stay at 0, so there’s nothing to do
        case END_KEY:
            if (E.cy < E.numrows)
                E.cx = E.row[E.cy].size;
            break;
        
        case CTRL_KEY('f'):
            editorFind();
            break;

        case BACKSPACE:
        case CTRL_KEY('h'):
        case DEL_KEY:
            if (c == DEL_KEY)
                editorMoveCursor(ARROW_RIGHT);
            editorDelChar();
            break;
        //movescursor to the top of the page and simulates 
        //the command ARROW_UP the equivalent of times to move the entire screen 
        case PAGE_UP:
        //movescursor to the bottom of the page and simulates 
        //the command ARROW_DOWN the equivalent of times to move the entire screen 
        case PAGE_DOWN:
            {
                if (c == PAGE_UP) {
                    E.cy = E.rowoff;
                } else  if (c == PAGE_DOWN) {
                    E.cy = E.rowoff + E.screenrows -1;
                    if (E.cy > E.numrows) 
                        E.cy = E.numrows;
                }
                int times = E.screenrows;
                while (times--)
                    // if c == PAGE_UP, do ARROW_UP, else, ARROW_DOWN
                    editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
            }
            break;

        case ARROW_UP:
        case ARROW_DOWN:
        case ARROW_LEFT:
        case ARROW_RIGHT:
            editorMoveCursor(c);
            break;
        
        case CTRL_KEY('l'):
        case '\x1b':
            break;

        /* This will allow any keypress that isn’t mapped to another editor 
        function to be inserted directly into the text being edited*/
        default:
            editorInsertChar(c);
            break;
    }

    quit_times = KILO_QUIT_TIMES;
}

//INIT//
void initEditor() {
    //setting x and y coordinates of the cursor relative to the text file
    E.cx = 0; //horizontal index into the chars field of an erow
    E.cy = 0; //vertical
    E.rx = 0; //horizontal index into the render field of an erow
    E.rowoff = 0; //starts at the top row
    E.coloff = 0; //starts at the leftmost column
    E.numrows = 0; //couonter starts at zero
    E.row = NULL; //row depends of the generation of erow structs being attached to it
    E.dirty = 0; //tracksif the text loaded differs from whats in the file (can warn for unsaved changes)
    E.filename = NULL; //as long as the file is not opened, the value of E.filename is NULL
    E.statusmsg[0] = '\0';
    E.statusmsg_time = 0;

    if (getWindowSize(&E.screenrows, &E.screencols) == -1)
        die("getWindowSize");
    //decrements E.screenrows so that editorDrawRows() doesn’t try to draw a line of text at the bottom of the screen
    E.screenrows -= 2;
}

int main(int argc, char *argv[]) {
    enableRawMode();
    initEditor();
    if (argc >= 2) {
        editorOpen(argv[1]);
    }

    //argument is the inicial message (can be got passing NULL to time())
    editorSetStatusMessage("HELP: Ctrl-s = save | Ctrl-Q = quit | Ctrl-f = find");

    while (1) {
        editorRefreshScreen();
        editorProcessKeypress();
    }

    return 0;
}