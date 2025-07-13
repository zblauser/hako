/*** HAKO ***/
#define HAKO_VERSION "0.0.6"


/*** includes ***/
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
#include <signal.h>


/*** constants ***/
const char *HAKO_LOGO[29] = {
" ++++++++++++++++++++++++++++++++++++++++++++++++++++++",
" ++++++++++++++++++++++++++++++++++++++++++++++++++++++",
" +++++++++++      ++++++++++++      +++++++++++++++++++",
" ++++++++++- @@@@ +++++++++++  @@@@ +++++++++++++++++++",
" ++++++++++  @@@               @@@                +++++",
" +++++++++  @@@@@@@@@@@@@@@  @@@@@@@@@@@@@@@@@@@@ +++++",
" ++++++++  @@@@@@@@@@@@@@@@ @@@@@@@@@@@@@@@@@@@@@ +++++",
" ++++++   @@@    @@@       @@@@     @@@           +++++",
" +++++   @@@  =   @@  +  @@@@   ++   @@@  +++++++++++++",
" +++++ @@@@  ++   @@@ + @@@   ++++++  @@@ +++++++++++++",
" +++++      +++ @@@   +                         +++++++",
" ++++++++++++++ @@@ +++++++ @@@@@@@@@@@@@@@@@@@ +++++++",
" ++++++         @@@         @@@             @@@ +++++++",
" ++++++ @@@@@@@@@@@@@@@@@@  @@@ ++++++++++  @@@ +++++++",
" ++++++ @@@@@@@@@@@@@@@@@@  @@@             @@@ +++++++",
" ++++++        @@@@         @@@@@@@@@@@@@@@@@@@ +++++++",
" ++++++++++-  @@@@@@   ++++ @@@             @@@ +++++++",
" +++++++++   @@:@@@@@@   ++ @@@ ++++++++++  @@@ +++++++",
" ++++++++   @@@ @@@ @@@@    @@@             @@@ +++++++",
" +++++++   @@@  @@@   @@@@  @@@@@@@@@@@@@@@@@@@ +++++++",
" +++++   @@@@   @@@ :  @@   @@@             @@@ +++++++",
" +++++  @@@   + @@@ ++    + @@@ ++++++++++  @@@ +++++++",
" +++++  @@   ++ @@@ +++++++ @@@             @@@ +++++++",
" ++++++    ++++ @@@ +++++++ @@@@@@@@@@@@@@@@@@@ +++++++",
" ++++++++++++++ @@@ +++++++ @@@             @@@ +++++++",
" ++++++++++++++     +++++++     ++++++++++-     +++++++",
" ++++++++++++++++++++++++++++++++++++++++++++++++++++++",
" ++++++++++++++++++++++++++++++++++++++++++++++++++++++",
NULL
};

const char *HAKO_HELP_TEXT = 
	"hako - A minimal text editor v" HAKO_VERSION "\n\n"
	"Usage: hako [options] [file]\n\n"
	"Options:\n"
	"  -h, --help     Show this help message\n"
	"  -v, --version  Show version information\n\n"
	"Commands:\n"
	"  Normal Mode:\n"
	"    i              Enter insert mode\n"
	"    v              Enter visual mode (character)\n"
	"    V              Enter visual mode (line)\n"
	"    h,j,k,l        Move cursor (or arrow keys)\n"
	"    0              Beginning of line\n"
	"    $              End of line\n"
	"    w              Next word\n"
	"    b              Previous word\n"
	"    :w             Save file\n"
	"    :q             Quit (fails if unsaved)\n"
	"    :q!            Force quit\n"
	"    :wq            Save and quit\n"
	"    :<number>      Jump to line number\n"
	"    /              Search\n"
	"    u              Undo\n"
	"    Ctrl-R         Redo\n"
	"    dd             Delete line\n"
	"    yy             Yank line\n"
	"    p              Paste after\n"
	"    P              Paste before\n"
	"    r              Replace character\n"
	"    J              Join lines\n"
	"    tt             Jump to top\n"
	"    bb             Jump to bottom\n"
	"    Ctrl-F         Page forward\n"
	"    Ctrl-B         Page backward\n\n"
	"  Visual Mode:\n"
	"    y              Yank selection\n"
	"    d,x            Delete/cut selection\n"
	"    c              Change selection\n"
	"    Esc            Exit visual mode\n\n"
	"  Insert Mode:\n"
	"    Esc            Return to normal mode\n";


/*** feature flags ***/
#define _DEFAULT_SOURCE
#define _GNU_SOURCE


/*** macros ***/
#define CTRL_KEY(k) ((k) & 0x1f)
#define ABUF_INIT {NULL, 0}
#define HL_HIGHLIGHT_NUMBERS (1<<0)
#define HL_HIGHLIGHT_STRINGS (1<<1)
#define HLDB_ENTRIES (sizeof(HLDB) / sizeof(HLDB[0]))


/*** cleanup macro ***/
void disableRawMode(void);
#undef exit
#define exit(code) do { \
	editorFreeUndoStack(&E.undo_stack); \
	editorFreeUndoStack(&E.redo_stack); \
	disableRawMode(); \
	fflush(stdout); \
	_exit(code); \
} while(0)


/*** data types ***/
enum editorMode {
	MODE_NORMAL,
	MODE_INSERT,
	MODE_VISUAL,
	MODE_VISUAL_LINE
};

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

enum editorHighlight {
	HL_NORMAL = 0,
	HL_COMMENT,
	HL_MLCOMMENT,
	HL_KEYWORD1,
	HL_KEYWORD2,
	HL_STRING,
	HL_NUMBER,
	HL_MATCH
};

typedef struct erow {
	int idx;
	int size;
	int rsize;
	char *chars;
	char *render;
	unsigned char *hl;
	int hl_open_comment;
} erow;

struct abuf {
	char *b;
	int len;
};

typedef struct undoState {
	erow *rows;
	int numrows;
	int cx, cy;
	char *filename;
	char *yank_buffer;
	int yank_buffer_len;
	struct undoState *next;
} undoState;


/*** syntax highlighting ***/
struct editorSyntax {
	char *filetype;
	char **filematch;
	char **keywords;
	char *singleline_comment_start;
	char *multiline_comment_start;
	char *multiline_comment_end;
	int flags;
};

// Language definitions
char *C_HL_extensions[] = { ".c", ".h", NULL };
char *C_HL_keywords[] = {
	"switch", "if", "while", "for", "break", "continue", "return", "else",
	"struct", "union", "typedef", "static", "enum", "class", "case",
	"int|", "long|", "double|", "float|", "char|", "unsigned|", "signed|", "void|",
	NULL
};

char *CPP_HL_extensions[] = { ".cpp", ".hpp", ".cc", ".cxx", NULL };
#define CPP_HL_keywords C_HL_keywords

char *PY_HL_extensions[] = { ".py", ".pyw", NULL };
char *PY_HL_keywords[] = {
	"def", "class", "if", "elif", "else", "while", "for", "in", "try", "except",
	"finally", "with", "as", "pass", "break", "continue", "return", "yield",
	"import", "from", "raise", "global", "nonlocal", "assert", "lambda",
	"True", "False", "None",
	"int|", "float|", "str|", "bool|", "list|", "dict|", "set|", "tuple|",
	"object|", "bytes|", "range|", "enumerate|", "len|", "open|", "print|",
	NULL
};

char *CS_HL_extensions[] = { ".cs", NULL };
char *CS_HL_keywords[] = {
	"using", "namespace", "class", "public", "private", "protected", "internal",
	"static", "readonly", "void|", "int|", "string|", "bool|", "float|", "double|",
	"if", "else", "for", "while", "switch", "case", "return", "new", "try", "catch",
	"finally", "true", "false", "null", NULL
};

char *JAVA_HL_extensions[] = { ".java", NULL };
char *JAVA_HL_keywords[] = {
	"package", "import", "class", "interface", "extends", "implements",
	"public", "private", "protected", "static", "void|", "int|", "float|",
	"double|", "boolean|", "char|", "new", "return", "this", "super",
	"if", "else", "for", "while", "switch", "case", "try", "catch", "finally",
	"null", "true", "false", NULL
};

char *JS_HL_extensions[] = { ".js", ".jsx", NULL };
char *JS_HL_keywords[] = {
	"function", "var", "let", "const", "return", "if", "else", "for",
	"while", "switch", "case", "break", "continue", "new", "this",
	"class", "extends", "super", "try", "catch", "finally", "true",
	"false", "null", "undefined", NULL
};

char *HTML_HL_extensions[] = { ".html", ".htm", ".css", NULL };
char *HTML_HL_keywords[] = {
	"html", "head", "body", "div", "span", "h1", "h2", "h3", "h4", "h5", "h6",
	"p", "a", "img", "ul", "li", "table", "tr", "td", "th", "form", "input",
	"button", "style", "script", "link", "meta", "class", "id",
	"color", "font-size", "margin", "padding", "border", "background",
	"display", "flex", "grid", "align-items", "justify-content", "position",
	NULL
};

char *RUST_HL_extensions[] = { ".rs", NULL };
char *RUST_HL_keywords[] = {
	"fn", "let", "mut", "const", "struct", "enum", "impl", "trait", "match",
	"if", "else", "for", "loop", "while", "break", "continue", "return",
	"pub", "use", "crate", "mod", "ref", "as", "in", "where", "move", "Self",
	"true", "false", "Option|", "Result|", "String|", "Vec|", NULL
};

char *SQL_HL_extensions[] = { ".sql", NULL };
char *SQL_HL_keywords[] = {
	"SELECT", "FROM", "WHERE", "INSERT", "INTO", "VALUES", "UPDATE",
	"SET", "DELETE", "JOIN", "INNER", "LEFT", "RIGHT", "ON", "AS", "AND",
	"OR", "NOT", "NULL", "LIKE", "IN", "IS", "CREATE", "TABLE", "PRIMARY",
	"KEY", "FOREIGN", "DROP", "ALTER", "INDEX", "VIEW", "DATABASE",
	"INT|", "VARCHAR|", "TEXT|", "DATE|", "BOOLEAN|", NULL
};

struct editorSyntax HLDB[] = {
	{ "c", C_HL_extensions, C_HL_keywords,
	"//", "/*", "*/", HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS },
	{ "c++", CPP_HL_extensions, CPP_HL_keywords,
	"//", "/*", "*/", HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS },
	{ "python", PY_HL_extensions, PY_HL_keywords,
	"#", "\"\"\"", "\"\"\"", HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS },
	{ "c#", CS_HL_extensions, CS_HL_keywords,
	"//", "/*", "*/", HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS },
	{ "java", JAVA_HL_extensions, JAVA_HL_keywords,
	"//", "/*", "*/", HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS },
	{ "javascript", JS_HL_extensions, JS_HL_keywords,
	"//", "/*", "*/", HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS },
	{ "html", HTML_HL_extensions, HTML_HL_keywords,
	"<!--", "<!--", "-->", HL_HIGHLIGHT_STRINGS },
	{ "rust", RUST_HL_extensions, RUST_HL_keywords,
	"//", "/*", "*/", HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS },
	{ "sql", SQL_HL_extensions, SQL_HL_keywords,
	"--", "/*", "*/", HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS }
};


/*** global state ***/
struct editorConfig {
	int cx, cy;
	int rx;
	int rowoff;
	int coloff;
	int screenrows, screencols;
	int numrows;
	erow *row;
	int dirty;
	char *filename;
	char statusmsg[80];
	time_t statusmsg_time;
	struct editorSyntax *syntax;
	struct termios orig_termios;
	
	// Configuration
	int tab_stop;
	enum editorMode mode;
	
	// Visual mode
	int visual_anchor_x, visual_anchor_y;
	
	// Yank buffer
	char *yank_buffer;
	int yank_buffer_len;
	
	// Line numbers
	int show_line_numbers;
	int line_number_width;
	
	// Undo/redo
	undoState *undo_stack;
	undoState *redo_stack;
	int max_undo_levels;
	int undo_block_active;
	time_t last_edit_time;
	int last_edit_cx, last_edit_cy;
	
	// Theme colors
	int fg_color;
	int bg_color;
	int line_number_color;
	int status_color;
	int color_comment;
	int color_keyword1;
	int color_keyword2;
	int color_string;
	int color_number;
	int color_match;
};

struct editorConfig E;


/*** prototypes ***/
void editorSetStatusMessage(const char *fmt, ...);
void editorRefreshScreen();
void editorDrawRows(struct abuf *ab);
void editorDrawStatusBar(struct abuf *ab);
void editorDrawMessageBar(struct abuf *ab);
void editorScroll();
void editorUpdateWindowSize();
void editorInsertRow(int at, char *s, size_t len);
void editorFreeRow(struct erow *row);
void editorUpdateRow(struct erow *row);
void editorUpdateSyntax(struct erow *row);
int editorSyntaxToColor(int hl);
int editorRowCxToRx(struct erow *row, int cx);
int getCursorPosition(int *rows, int *cols);
int editorReadKey();
char *editorPrompt(char *prompt, void (*callback)(char *, int));
char *editorRowsToString(int *buflen);
void editorRowAppendString(struct erow *row, char *s, size_t len);
void editorColonCommand();
void editorFind(void);
void editorJumpTop(void);
void editorJumpBottom(void);
int is_separator(int c);
void editorSaveState(void);
void editorUndo(void);
void editorRedo(void);
void editorFreeUndoStack(undoState **stack);
undoState *editorCreateUndoState(void);
void editorRestoreState(undoState *state);
void editorStartUndoBlock(void);
void editorEndUndoBlock(void);
int editorShouldBreakUndoBlock(void);
void editorUpdateEditTime(void);
void editorUpdateLineNumberWidth(void);
void editorDrawLineNumber(struct abuf *ab, int filerow);
int editorIsInVisualSelection(int row, int col);
void editorGetVisualSelection(int *start_x, int *start_y, int *end_x, int *end_y);
void editorYankVisualSelection();
void editorDeleteVisualSelection();
void editorMoveCursor(int key);
void editorInsertChar(int c);
void editorInsertNewLine();
void editorDelChar();


/*** terminal ***/
void die(const char *s) {
	write(STDOUT_FILENO, "\x1b[2J", 4);
	write(STDOUT_FILENO, "\x1b[H", 3);
	write(STDOUT_FILENO, "\033c", 2);
	
	fflush(stdout);
	
	perror(s);
	exit(1);
}

void disableRawMode() {
	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1) die("tcsetattr");
	// Exit alternate screen buffer and restore cursor
	write(STDOUT_FILENO, "\033[?1049l", 8);
	write(STDOUT_FILENO, "\033[?25h", 6);  // Show cursor
	// Reset ALL formatting and colors
	write(STDOUT_FILENO, "\033[0m", 4);     // Reset all attributes
	write(STDOUT_FILENO, "\033[39m", 5);    // Default foreground color
	write(STDOUT_FILENO, "\033[49m", 5);    // Default background color
	// Reset terminal colors to defaults (OSC sequences)
	write(STDOUT_FILENO, "\033]110\007", 7); // Reset foreground color
	write(STDOUT_FILENO, "\033]111\007", 7); // Reset background color
	fflush(stdout);
}

void enableRawMode() {
	if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1) die("tcgetattr");
	write(STDOUT_FILENO, "\033[?1049h", 8);
	fflush(stdout);
	atexit(disableRawMode);
	
	// Set terminal colors - multiple methods for compatibility
	{
		char seq[32];
		int len;
		
		// Method 1: OSC sequences (works in most modern terminals)
		len = snprintf(seq, sizeof(seq), "\033]10;#%06x\007", E.fg_color);
		write(STDOUT_FILENO, seq, len);
		len = snprintf(seq, sizeof(seq), "\033]11;#%06x\007", E.bg_color);
		write(STDOUT_FILENO, seq, len);
		
		// Method 2: Set default background color (for terminals that support it)
		write(STDOUT_FILENO, "\033[48;2;", 7);
		char r[4], g[4], b[4];
		snprintf(r, sizeof(r), "%d", (E.bg_color >> 16) & 0xFF);
		snprintf(g, sizeof(g), "%d", (E.bg_color >> 8) & 0xFF);
		snprintf(b, sizeof(b), "%d", E.bg_color & 0xFF);
		write(STDOUT_FILENO, r, strlen(r));
		write(STDOUT_FILENO, ";", 1);
		write(STDOUT_FILENO, g, strlen(g));
		write(STDOUT_FILENO, ";", 1);
		write(STDOUT_FILENO, b, strlen(b));
		write(STDOUT_FILENO, "m", 1);
		
		// Clear screen with new background
		write(STDOUT_FILENO, "\033[2J", 4);
		write(STDOUT_FILENO, "\033[H", 3);
		fflush(stdout);
	}
	
	struct termios raw = E.orig_termios;
	raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
	raw.c_oflag &= ~(OPOST);
	raw.c_cflag |= CS8;
	raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
	raw.c_cc[VMIN] = 0;
	raw.c_cc[VTIME] = 1;
	
	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr");
}

int getWindowSize(int *rows, int *cols) {
	struct winsize ws;
	
	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
		if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1;
		editorReadKey();
		return getCursorPosition(rows, cols);
	}
	else {
		*cols = ws.ws_col;
		*rows = ws.ws_row;
		return 0;
	}
}

volatile sig_atomic_t winch_received = 0;

static void on_sigwinch(int signo) {
	winch_received = 1;
	char dummy = 0;
	write(STDIN_FILENO, &dummy, 1);
}

int getCursorPosition(int *rows, int *cols) {
	char buf[32];
	unsigned int i = 0;
	
	if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;
	
	while (i < sizeof(buf) - 1) {
		if (read(STDIN_FILENO, &buf[i], 1) != 1) break;
		if (buf[i] == 'R') break;
		i++;
	}
	buf[i] = '\0';
	
	if (buf[0] != '\x1b' || buf[1] != '[') return -1;
	if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1;
	
	return 0;
}


/*** append buffer ***/
void abAppend(struct abuf *ab, const char *s, int len) {
	char *new = realloc(ab->b, ab->len + len);
	
	if (new == NULL) return;
	memcpy(&new[ab->len], s, len);
	ab->b = new;
	ab->len += len;
}

void abFree(struct abuf *ab) {
	free(ab->b);
}


/*** helpers ***/
// Switches editor mode and updates status message
void editorSetMode(enum editorMode mode) {
	E.mode = mode;
	switch (mode) {
		case MODE_NORMAL:
			editorSetStatusMessage("-- NORMAL --");
			break;
		case MODE_INSERT:
			editorSetStatusMessage("-- INSERT --");
			break;
		case MODE_VISUAL:
			editorSetStatusMessage("-- VISUAL --");
			break;
		case MODE_VISUAL_LINE:
			editorSetStatusMessage("-- VISUAL LINE --");
			break;
	}
}

// Checks if character is a word separator
int is_separator(int c) {
	return isspace(c) || c == '\0' || strchr(",.()+-/*=~%<>[];", c) != NULL;
}


/*** output ***/
void editorScroll() {
	E.rx = 0;
	if (E.cy < E.numrows) {
		E.rx = editorRowCxToRx(&E.row[E.cy], E.cx);
	}
	
	if (E.cy < E.rowoff) {
		E.rowoff = E.cy;
	}
	if (E.cy >= E.rowoff + E.screenrows) {
		E.rowoff = E.cy - E.screenrows + 1;
	}
	if (E.rx < E.coloff) {
		E.coloff = E.rx;
	}
	if (E.rx >= E.coloff + E.screencols - E.line_number_width) {
		E.coloff = E.rx - E.screencols + E.line_number_width + 1;
	}
}

void editorDrawRows(struct abuf *ab) {
	int y;
	for (y = 0; y < E.screenrows; y++) {
		int filerow = y + E.rowoff;
		
		if (filerow >= E.numrows) {
			if (E.show_line_numbers) {
				editorDrawLineNumber(ab, filerow);
			} else {
				abAppend(ab, "~", 1);
			}
			
			// Draw logo on empty screen
			if (E.numrows == 0) {
				int logo_start_row = 4;
				
				if (y == logo_start_row) {
					char version[80];
					int versionlen = snprintf(
						version, sizeof(version),
						"HAKO -- version %s", HAKO_VERSION
					);
					if (versionlen > E.screencols - E.line_number_width)
						versionlen = E.screencols - E.line_number_width;
					int padding = (E.screencols - E.line_number_width - versionlen) / 2;
					if (padding > 0 && !E.show_line_numbers) {
						padding--;
					}
					while (padding--) abAppend(ab, " ", 1);
					abAppend(ab, version, versionlen);
				}
				else if (y > logo_start_row && y <= logo_start_row + 28) {
					int logo_idx = y - (logo_start_row + 1);
					const char *line = HAKO_LOGO[logo_idx];
					int linelen = (line ? strlen(line) : 0);
					
					if (linelen > E.screencols - E.line_number_width)
						linelen = E.screencols - E.line_number_width;
					
					int padding = (E.screencols - E.line_number_width - linelen) / 2;
					if (padding > 0 && !E.show_line_numbers) {
						padding--;
					}
					while (padding--) abAppend(ab, " ", 1);
					
					if (line) {
						abAppend(ab, line, linelen);
					}
				}
			}
		}
		else {
			editorDrawLineNumber(ab, filerow);
			
			int len = E.row[filerow].rsize - E.coloff;
			if (len < 0) len = 0;
			if (len > E.screencols - E.line_number_width)
				len = E.screencols - E.line_number_width;
			char *c = &E.row[filerow].render[E.coloff];
			unsigned char *hl = &E.row[filerow].hl[E.coloff];
			int current_color = -1;
			
			for (int j = 0; j < len; j++) {
				// Check visual selection
				int is_selected = 0;
				if (E.mode == MODE_VISUAL || E.mode == MODE_VISUAL_LINE) {
					int col = E.coloff + j;
					is_selected = editorIsInVisualSelection(filerow, col);
				}
				
				if (is_selected) {
					abAppend(ab, "\x1b[7m", 4);  // Reverse video
				}
				
				if (iscntrl(c[j])) {
					char sym = (c[j] <= 26) ? '@' + c[j] : '?';
					abAppend(ab, "\x1b[7m", 4);
					abAppend(ab, &sym, 1);
					abAppend(ab, "\x1b[m", 3);
					if (current_color != -1) {
						char buf[16];
						int clen = snprintf(buf, sizeof(buf), "\x1b[%dm", current_color);
						abAppend(ab, buf, clen);
					}
				} else if (hl[j] == HL_NORMAL) {
					if (current_color != -1) {
						abAppend(ab, "\x1b[39m", 5);
						current_color = -1;
					}
					abAppend(ab, &c[j], 1);
				} else {
					int color = editorSyntaxToColor(hl[j]);
					if (color != current_color) {
						current_color = color;
						char buf[16];
						int clen = snprintf(buf, sizeof(buf), "\x1b[%dm", color);
						abAppend(ab, buf, clen);
					}
					abAppend(ab, &c[j], 1);
				}
				
				if (is_selected) {
					abAppend(ab, "\x1b[27m", 5);  // Turn off reverse
				}
			}
			abAppend(ab, "\x1b[39m", 5);
		}
		
		abAppend(ab, "\x1b[K", 3);
		abAppend(ab, "\r\n", 2);
	}
}

void editorDrawStatusBar(struct abuf *ab) {
	abAppend(ab, "\x1b[7m", 4);
	char status[80], rstatus[80];
	int len = snprintf(status, sizeof(status), "%.20s - %d lines %s",
		E.filename ? E.filename : "[No Name]", E.numrows,
		E.dirty ? ("modified") : "");
	int rlen = snprintf(rstatus, sizeof(rstatus), "%s | %d/%d",
		E.syntax ? E.syntax->filetype : "no ft", E.cy + 1, E.numrows);
	if (len > E.screencols) len = E.screencols;
	abAppend(ab, status, len);
	while (len < E.screencols) {
		if (E.screencols - len == rlen) {
			abAppend(ab, rstatus, rlen);
			break;
		}
		else {
			abAppend(ab, " ", 1);
			len++;
		}
	}
	abAppend(ab, "\x1b[m", 3);
	abAppend(ab, "\r\n", 2);
}

void editorDrawMessageBar(struct abuf *ab) {
	abAppend(ab, "\x1b[K", 3);
	int msglen = strlen(E.statusmsg);
	if (msglen > E.screencols) msglen = E.screencols;
	if (msglen && time(NULL) - E.statusmsg_time < 5)
		abAppend(ab, E.statusmsg, msglen);
}

void editorSetStatusMessage(const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
	va_end(ap);
	E.statusmsg_time = time(NULL);
}

void editorRefreshScreen() {
	editorUpdateWindowSize();
	editorUpdateLineNumberWidth();
	
	editorScroll();
	
	struct abuf ab = ABUF_INIT;
	
	abAppend(&ab, "\x1b[?25l", 6);
	abAppend(&ab, "\x1b[H", 3);
	
	editorDrawRows(&ab);
	editorDrawStatusBar(&ab);
	editorDrawMessageBar(&ab);
	
	char buf[32];
	snprintf(buf, sizeof(buf), "\x1b[%d;%dH",
		(E.cy - E.rowoff) + 1,
		(E.rx - E.coloff) + E.line_number_width + 1);
	abAppend(&ab, buf, strlen(buf));
	
	abAppend(&ab, "\x1b[?25h", 6);
	
	write(STDOUT_FILENO, ab.b, ab.len);
	abFree(&ab);
}


/*** syntax highlighting ***/
int editorSyntaxToColor(int hl) {
	switch (hl) {
		case HL_COMMENT:
		case HL_MLCOMMENT: return 36;
		case HL_KEYWORD1: return 33;
		case HL_KEYWORD2: return 32;
		case HL_STRING: return 35;
		case HL_NUMBER: return 31;
		case HL_MATCH: return 34;
		default: return 37;
	}
}

void editorUpdateSyntax(erow *row) {
	row->hl = realloc(row->hl, row->rsize);
	memset(row->hl, HL_NORMAL, row->rsize);
	
	if (E.syntax == NULL) return;
	
	char **keywords = E.syntax->keywords;
	
	char *scs = E.syntax->singleline_comment_start;
	char *mcs = E.syntax->multiline_comment_start;
	char *mce = E.syntax->multiline_comment_end;
	
	int scs_len = scs ? strlen(scs) : 0;
	int mcs_len = mcs ? strlen(mcs) : 0;
	int mce_len = mce ? strlen(mce) : 0;
	
	int prev_sep = 1;
	int in_string = 0;
	int in_comment = (row->idx > 0 && E.row[row->idx - 1].hl_open_comment);
	
	int i = 0;
	while (i < row->rsize) {
		char c = row->render[i];
		unsigned char prev_hl = (i > 0) ? row->hl[i - 1] : HL_NORMAL;
		
		// Single-line comments
		if (scs_len && !in_string && !in_comment) {
			if (!strncmp(&row->render[i], scs, scs_len)) {
				memset(&row->hl[i], HL_COMMENT, row->rsize - i);
				break;
			}
		}
		
		// Multi-line comments
		if (mcs_len && mce_len && !in_string) {
			if (in_comment) {
				row->hl[i] = HL_MLCOMMENT;
				if (!strncmp(&row->render[i], mce, mce_len)) {
					memset(&row->hl[i], HL_MLCOMMENT, mce_len);
					i += mce_len;
					in_comment = 0;
					prev_sep = 1;
					continue;
				}
				else {
					i++;
					continue;
				}
			}
			else if (!strncmp(&row->render[i], mcs, mcs_len)) {
				memset(&row->hl[i], HL_MLCOMMENT, mcs_len);
				i += mcs_len;
				in_comment = 1;
				continue;
			}
		}
		
		// Strings
		if (E.syntax->flags & HL_HIGHLIGHT_STRINGS) {
			if (in_string) {
				row->hl[i] = HL_STRING;
				if (c == '\\' && i + 1 < row->rsize) {
					row->hl[i + 1] = HL_STRING;
					i += 2;
					continue;
				}
				if (c == in_string) in_string = 0;
				i++;
				prev_sep = 1;
				continue;
			}
			else {
				if (c == '"' || c == '\'') {
					in_string = c;
					row->hl[i] = HL_STRING;
					i++;
					continue;
				}
			}
		}
		
		// Numbers
		if (E.syntax->flags & HL_HIGHLIGHT_NUMBERS) {
			if ((isdigit(c) && (prev_sep || prev_hl == HL_NUMBER)) || 
			    (c == '.' && prev_hl == HL_NUMBER)) {
				row->hl[i] = HL_NUMBER;
				i++;
				prev_sep = 0;
				continue;
			}
		}
		
		// Keywords
		if (prev_sep) {
			int j;
			for (j = 0; keywords[j]; j++) {
				int klen = strlen(keywords[j]);
				int kw2 = keywords[j][klen - 1] == '|';
				if (kw2) klen--;
				
				if (!strncmp(&row->render[i], keywords[j], klen) &&
					is_separator(row->render[i + klen])) {
					memset(&row->hl[i], kw2 ? HL_KEYWORD2 : HL_KEYWORD1, klen);
					i += klen;
					break;
				}
			}
			if (keywords[j] != NULL) {
				prev_sep = 0;
				continue;
			}
		}
		
		prev_sep = is_separator(c);
		i++;
	}
	
	// Propagate comment state to next line if needed
	int changed = (row->hl_open_comment != in_comment);
	row->hl_open_comment = in_comment;
	if (changed && row->idx + 1 < E.numrows)
		editorUpdateSyntax(&E.row[row->idx + 1]);
}

void editorSelectSyntaxHighlight() {
	E.syntax = NULL;
	if (E.filename == NULL) return;
	
	char *ext = strrchr(E.filename, '.');
	
	for (unsigned int j = 0; j < HLDB_ENTRIES; j++) {
		struct editorSyntax *s = &HLDB[j];
		unsigned int i = 0;
		while (s->filematch[i]) {
			int is_ext = (s->filematch[i][0] == '.');
			if ((is_ext && ext && !strcmp(ext, s->filematch[i])) ||
				(!is_ext && strstr(E.filename, s->filematch[i]))) {
				E.syntax = s;
				for (int filerow = 0; filerow < E.numrows; filerow++) {
					editorUpdateSyntax(&E.row[filerow]);
				}
				return;
			}
			i++;
		}
	}
}


/*** row operations ***/
int editorRowCxToRx(erow *row, int cx) {
	int rx = 0;
	int j;
	for (j = 0; j < cx; j++) {
		if (row->chars[j] == '\t')
			rx += (E.tab_stop - 1) - (rx % E.tab_stop);
		rx++;
	}
	return rx;
}

int editorRowRxToCx(erow *row, int rx) {
	int cur_rx = 0;
	int cx;
	for (cx = 0; cx < row->size; cx++) {
		if (row->chars[cx] == '\t')
			cur_rx += (E.tab_stop - 1) - (cur_rx % E.tab_stop);
		cur_rx++;
		
		if (cur_rx > rx) return cx;
	}
	return cx;
}

void editorUpdateRow(erow *row) {
	int tabs = 0;
	int j;
	for (j = 0; j < row->size; j++)
		if (row->chars[j] == '\t') tabs++;
	
	free(row->render);
	row->render = malloc(row->size + tabs * (E.tab_stop - 1) + 1);
	
	int idx = 0;
	for (j = 0; j < row->size; j++) {
		if (row->chars[j] == '\t') {
			row->render[idx++] = ' ';
			while (idx % E.tab_stop != 0) row->render[idx++] = ' ';
		}
		else {
			row->render[idx++] = row->chars[j];
		}
	}
	row->render[idx] = '\0';
	row->rsize = idx;
	
	editorUpdateSyntax(row);
}

void editorInsertRow(int at, char *s, size_t len) {
	if (at < 0 || at > E.numrows) return;
	
	E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));
	memmove(&E.row[at + 1], &E.row[at], sizeof(erow) * (E.numrows - at));
	for (int j = at + 1; j <= E.numrows; j++) E.row[j].idx++;
	
	E.row[at].idx = at;
	
	E.row[at].size = len;
	E.row[at].chars = malloc(len + 1);
	memcpy(E.row[at].chars, s, len);
	E.row[at].chars[len] = '\0';
	
	E.row[at].rsize = 0;
	E.row[at].render = NULL;
	E.row[at].hl = NULL;
	E.row[at].hl_open_comment = 0;
	editorUpdateRow(&E.row[at]);
	E.numrows++;
	E.dirty++;
}

void editorFreeRow(erow *row) {
	free(row->render);
	free(row->chars);
	free(row->hl);
}

void editorDelRow(int at) {
	if (at < 0 || at >= E.numrows) return;
	editorFreeRow(&E.row[at]);
	memmove(&E.row[at], &E.row[at + 1], sizeof(erow) * (E.numrows - at - 1));
	for (int j = at; j < E.numrows - 1; j++) E.row[j].idx--;
	E.numrows--;
	E.dirty++;
}

void editorRowInsertChar(erow *row, int at, int c) {
	if (at < 0 || at > row->size) at = row->size;
	row->chars = realloc(row->chars, row->size + 2);
	memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);
	row->size++;
	row->chars[at] = c;
	editorUpdateRow(row);
	E.dirty++;
}

void editorRowAppendString(erow *row, char *s, size_t len) {
	row->chars = realloc(row->chars, row->size + len + 1);
	memcpy(&row->chars[row->size], s, len);
	row->size += len;
	row->chars[row->size] = '\0';
	editorUpdateRow(row);
	E.dirty++;
}

void editorRowDelChar(erow *row, int at) {
	if (at < 0 || at >= row->size) return;
	memmove(&row->chars[at], &row->chars[at + 1], row->size - at);
	row->size--;
	editorUpdateRow(row);
	E.dirty++;
}


/*** editor operations ***/
void editorInsertChar(int c) {
	if (editorShouldBreakUndoBlock()) {
		editorEndUndoBlock();
		editorStartUndoBlock();
	} else if (!E.undo_block_active) {
		editorStartUndoBlock();
	}
	
	editorUpdateEditTime();
	
	if (E.cy == E.numrows) {
		editorInsertRow(E.numrows, "", 0);
	}
	editorRowInsertChar(&E.row[E.cy], E.cx, c);
	E.cx++;
}

void editorInsertNewLine() {
	if (E.undo_block_active) {
		editorEndUndoBlock();
	}
	editorStartUndoBlock();
	
	while (E.cy >= E.numrows) {
		editorInsertRow(E.numrows, "", 0);
	}
	
	if (E.cx == 0) {
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

void editorDelChar() {
	if (E.cy == E.numrows) return;
	if (E.cx == 0 && E.cy == 0) return;
	
	if (E.numrows == 0) return;
	
	if (editorShouldBreakUndoBlock()) {
		editorEndUndoBlock();
		editorStartUndoBlock();
	} else if (!E.undo_block_active) {
		editorStartUndoBlock();
	}
	
	editorUpdateEditTime();
	
	erow *row = &E.row[E.cy];
	if (E.cx > 0) {
		editorRowDelChar(row, E.cx - 1);
		E.cx--;
	}
	else {
		E.cx = E.row[E.cy - 1].size;
		editorRowAppendString(&E.row[E.cy - 1], row->chars, row->size);
		editorDelRow(E.cy);
		E.cy--;
	}
}


/*** file i/o ***/
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

void editorOpen(char *filename) {
	free(E.filename);
	E.filename = strdup(filename);
	
	editorSelectSyntaxHighlight();
	
	FILE *fp = fopen(filename, "r");
	
	if (!fp) {
		if (errno == ENOENT) {
			// New file
			editorInsertRow(0, "", 0);
			E.dirty = 0;
			editorSetStatusMessage("New file: %s", filename);
			return;
		} else {
			die("fopen");
		}
	}
	
	char *line = NULL;
	size_t linecap = 0;
	ssize_t linelen;
	while ((linelen = getline(&line, &linecap, fp)) != -1) {
		while (linelen > 0 && (line[linelen - 1] == '\n' ||
							   line[linelen - 1] == '\r'))
			linelen--;
		editorInsertRow(E.numrows, line, linelen);
	}
	free(line);
	fclose(fp);
	E.dirty = 0;
}

void editorSave() {
	if (E.filename == NULL) {
		E.filename = editorPrompt("Save as: %s (ESC to cancel)", NULL);
		if (E.filename == NULL) {
			editorSetStatusMessage("Save Aborted");
			return;
		}
		editorSelectSyntaxHighlight();
	}
	
	int len;
	char *buf = editorRowsToString(&len);
	
	int fd = open(E.filename, O_RDWR | O_CREAT, 0644);
	if (fd != -1) {
		if (ftruncate(fd, len) != -1) {
			if (write(fd, buf, len) == len) {
				close(fd);
				free(buf);
				E.dirty = 0;
				editorSetStatusMessage("%d bytes written to disk", len);
				return;
			}
		}
		
		close(fd);
	}
	
	free(buf);
	editorSetStatusMessage("Can't save! I/O error: %s", strerror(errno));
}


/*** find ***/
void editorFindCallback(char *query, int key) {
	static int last_match = -1;
	static int direction = 1;
	
	if (last_match != -1) {
		editorUpdateSyntax(&E.row[last_match]);
	}
	
	if (key == '\r' || key == '\x1b') {
		last_match = -1;
		direction = 1;
		return;
	}
	else if (key == ARROW_RIGHT || key == ARROW_DOWN) {
		direction = 1;
	}
	else if (key == ARROW_LEFT || key == ARROW_UP) {
		direction = -1;
	}
	else {
		last_match = -1;
		direction = 1;
	}
	
	int current = last_match;
	for (int i = 0; i < E.numrows; i++) {
		current += direction;
		if (current < 0) current = E.numrows - 1;
		else if (current >= E.numrows) current = 0;
		
		erow *row = &E.row[current];
		char *match = strstr(row->render, query);
		if (!match) continue;
		
		last_match = current;
		E.cy = current;
		E.cx = editorRowRxToCx(row, match - row->render);
		editorScroll();
		
		int off = match - row->render;
		int match_len = strlen(query);
		for (int j = 0; j < match_len; j++) {
			row->hl[off + j] = HL_MATCH;
		}
		break;
	}
}

void editorFind() {
	int saved_cx = E.cx;
	int saved_cy = E.cy;
	int saved_coloff = E.coloff;
	int saved_rowoff = E.rowoff;
	
	char *query = editorPrompt("/%s (Use ESC/Arrows/Enter)", editorFindCallback);
	
	if (query) {
		free(query);
	}
	else {
		E.cx = saved_cx;
		E.cy = saved_cy;
		E.coloff = saved_coloff;
		E.rowoff = saved_rowoff;
	}
}


/*** visual mode ***/
int editorIsInVisualSelection(int row, int col) {
	if (E.mode != MODE_VISUAL && E.mode != MODE_VISUAL_LINE) return 0;
	
	// Line mode selects entire lines
	if (E.mode == MODE_VISUAL_LINE) {
		int start_y = E.visual_anchor_y;
		int end_y = E.cy;
		
		if (start_y > end_y) {
			int tmp = start_y;
			start_y = end_y;
			end_y = tmp;
		}
		
		return (row >= start_y && row <= end_y);
	}
	
	// Character mode
	int start_y = E.visual_anchor_y;
	int start_x = E.visual_anchor_x;
	int end_y = E.cy;
	int end_x = E.cx;
	
	// Normalize selection
	if (start_y > end_y || (start_y == end_y && start_x > end_x)) {
		int tmp;
		tmp = start_y; start_y = end_y; end_y = tmp;
		tmp = start_x; start_x = end_x; end_x = tmp;
	}
	
	// Convert render position to character position
	if (row >= 0 && row < E.numrows) {
		int char_col = 0;
		int render_col = 0;
		erow *r = &E.row[row];
		
		for (char_col = 0; char_col < r->size && render_col < col; char_col++) {
			if (r->chars[char_col] == '\t') {
				render_col += (E.tab_stop - 1) - (render_col % E.tab_stop);
			}
			render_col++;
		}
		col = char_col;
	}
	
	// Check if position is in selection
	if (row < start_y || row > end_y) return 0;
	if (row == start_y && col < start_x) return 0;
	if (row == end_y && col > end_x) return 0;
	
	return 1;
}

void editorGetVisualSelection(int *start_x, int *start_y, int *end_x, int *end_y) {
	if (E.mode == MODE_VISUAL_LINE) {
		// Line mode selects full lines
		*start_x = 0;
		*start_y = E.visual_anchor_y;
		*end_x = (E.cy < E.numrows) ? E.row[E.cy].size - 1 : 0;
		if (*end_x < 0) *end_x = 0;
		*end_y = E.cy;
	} else {
		// Character mode
		*start_x = E.visual_anchor_x;
		*start_y = E.visual_anchor_y;
		*end_x = E.cx;
		*end_y = E.cy;
	}
	
	// Normalize selection
	if (*start_y > *end_y || (*start_y == *end_y && *start_x > *end_x)) {
		int tmp;
		tmp = *start_x; *start_x = *end_x; *end_x = tmp;
		tmp = *start_y; *start_y = *end_y; *end_y = tmp;
	}
	
	// Clamp to valid range
	if (*end_y < E.numrows && *end_x >= E.row[*end_y].size) {
		*end_x = E.row[*end_y].size - 1;
		if (*end_x < 0) *end_x = 0;
	}
}

void editorYankVisualSelection() {
	int start_x, start_y, end_x, end_y;
	editorGetVisualSelection(&start_x, &start_y, &end_x, &end_y);
	
	// Free old yank buffer
	free(E.yank_buffer);
	E.yank_buffer = NULL;
	E.yank_buffer_len = 0;
	
	// Calculate total size needed
	int total_len = 0;
	for (int y = start_y; y <= end_y; y++) {
		if (y >= E.numrows) break;
		
		int row_start = (y == start_y) ? start_x : 0;
		int row_end = (y == end_y) ? end_x : E.row[y].size - 1;
		
		if (row_start <= row_end) {
			total_len += row_end - row_start + 1;
			if (y < end_y) total_len++; // For newline
		}
	}
	
	if (total_len == 0) return;
	
	// Allocate and fill yank buffer
	E.yank_buffer = malloc(total_len + 1);
	E.yank_buffer_len = total_len;
	
	char *p = E.yank_buffer;
	for (int y = start_y; y <= end_y; y++) {
		if (y >= E.numrows) break;
		
		int row_start = (y == start_y) ? start_x : 0;
		int row_end = (y == end_y) ? end_x : E.row[y].size - 1;
		
		if (row_start <= row_end && row_start < E.row[y].size) {
			int len = row_end - row_start + 1;
			if (row_end >= E.row[y].size) len = E.row[y].size - row_start;
			
			memcpy(p, &E.row[y].chars[row_start], len);
			p += len;
			
			if (y < end_y) {
				*p++ = '\n';
			}
		}
	}
	*p = '\0';
	
	// Status message
	int lines = end_y - start_y + 1;
	if (lines > 1) {
		editorSetStatusMessage("%d lines yanked", lines);
	} else {
		editorSetStatusMessage("Yanked %d characters", E.yank_buffer_len);
	}
}

void editorDeleteVisualSelection() {
	editorStartUndoBlock();
	
	int start_x, start_y, end_x, end_y;
	editorGetVisualSelection(&start_x, &start_y, &end_x, &end_y);
	
	// Yank before deleting (like vim)
	editorYankVisualSelection();
	
	// Delete from end to start to maintain positions
	if (start_y == end_y) {
		// Single line deletion
		for (int i = end_x; i >= start_x; i--) {
			editorRowDelChar(&E.row[start_y], i);
		}
	} else {
		// Multi-line deletion
		if (start_x < E.row[start_y].size) {
			// Keep text before selection on first line
			E.row[start_y].size = start_x;
			E.row[start_y].chars[start_x] = '\0';
			
			// Append text after selection on last line
			if (end_x + 1 < E.row[end_y].size) {
				editorRowAppendString(&E.row[start_y],
					&E.row[end_y].chars[end_x + 1],
					E.row[end_y].size - end_x - 1);
			}
			editorUpdateRow(&E.row[start_y]);
		}
		
		// Delete middle lines and end line
		for (int i = 0; i < end_y - start_y; i++) {
			editorDelRow(start_y + 1);
		}
	}
	
	// Position cursor at start of deletion
	E.cy = start_y;
	E.cx = start_x;
	
	editorEndUndoBlock();
	editorSetStatusMessage("Deleted selection");
}


/*** undo/redo ***/
void editorStartUndoBlock(void) {
	if (!E.undo_block_active) {
		editorSaveState();
		E.undo_block_active = 1;
		E.last_edit_time = time(NULL);
		E.last_edit_cx = E.cx;
		E.last_edit_cy = E.cy;
	}
}

void editorEndUndoBlock(void) {
	E.undo_block_active = 0;
}

int editorShouldBreakUndoBlock(void) {
	if (!E.undo_block_active) return 0;
	
	time_t now = time(NULL);
	
	// Break on time boundary
	if (now - E.last_edit_time > 1) return 1;
	
	// Break on movement
	int cx_diff = abs(E.cx - E.last_edit_cx);
	int cy_diff = abs(E.cy - E.last_edit_cy);
	if (cy_diff > 0 || cx_diff > 1) return 1;
	
	return 0;
}

void editorUpdateEditTime(void) {
	E.last_edit_time = time(NULL);
	E.last_edit_cx = E.cx;
	E.last_edit_cy = E.cy;
}

undoState *editorCreateUndoState(void) {
	undoState *state = malloc(sizeof(undoState));
	if (!state) return NULL;
	
	state->numrows = E.numrows;
	state->cx = E.cx;
	state->cy = E.cy;
	state->filename = E.filename ? strdup(E.filename) : NULL;
	
	// Save yank buffer
	if (E.yank_buffer && E.yank_buffer_len > 0) {
		state->yank_buffer = malloc(E.yank_buffer_len + 1);
		memcpy(state->yank_buffer, E.yank_buffer, E.yank_buffer_len + 1);
		state->yank_buffer_len = E.yank_buffer_len;
	} else {
		state->yank_buffer = NULL;
		state->yank_buffer_len = 0;
	}
	
	state->next = NULL;
	
	// Save rows
	if (E.numrows > 0) {
		state->rows = malloc(sizeof(erow) * E.numrows);
		for (int i = 0; i < E.numrows; i++) {
			state->rows[i].size = E.row[i].size;
			state->rows[i].chars = malloc(E.row[i].size + 1);
			memcpy(state->rows[i].chars, E.row[i].chars, E.row[i].size + 1);
			
			state->rows[i].rsize = 0;
			state->rows[i].render = NULL;
			state->rows[i].hl = NULL;
			state->rows[i].hl_open_comment = 0;
			state->rows[i].idx = i;
		}
	} else {
		state->rows = NULL;
	}
	
	return state;
}

void editorSaveState(void) {
	// Limit undo levels
	int count = 0;
	undoState *current = E.undo_stack;
	while (current && count < E.max_undo_levels) {
		current = current->next;
		count++;
	}
	
	// Remove oldest states if at limit
	if (count >= E.max_undo_levels && E.undo_stack) {
		undoState *temp = E.undo_stack;
		while (temp->next && temp->next->next) {
			temp = temp->next;
		}
		if (temp->next) {
			editorFreeUndoStack(&temp->next);
			temp->next = NULL;
		}
	}
	
	// Create and push new state
	undoState *state = editorCreateUndoState();
	if (!state) return;
	
	state->next = E.undo_stack;
	E.undo_stack = state;
	
	// Clear redo stack
	editorFreeUndoStack(&E.redo_stack);
}

void editorRestoreState(undoState *state) {
	// Free current rows
	for (int i = 0; i < E.numrows; i++) {
		editorFreeRow(&E.row[i]);
	}
	free(E.row);
	
	// Restore state
	E.numrows = state->numrows;
	E.cx = state->cx;
	E.cy = state->cy;
	
	// Restore yank buffer
	free(E.yank_buffer);
	if (state->yank_buffer && state->yank_buffer_len > 0) {
		E.yank_buffer = malloc(state->yank_buffer_len + 1);
		memcpy(E.yank_buffer, state->yank_buffer, state->yank_buffer_len + 1);
		E.yank_buffer_len = state->yank_buffer_len;
	} else {
		E.yank_buffer = NULL;
		E.yank_buffer_len = 0;
	}
	
	// Restore rows
	if (state->numrows > 0) {
		E.row = malloc(sizeof(erow) * state->numrows);
		for (int i = 0; i < state->numrows; i++) {
			E.row[i].size = state->rows[i].size;
			E.row[i].chars = malloc(state->rows[i].size + 1);
			memcpy(E.row[i].chars, state->rows[i].chars, state->rows[i].size + 1);
			E.row[i].idx = i;
			
			E.row[i].rsize = 0;
			E.row[i].render = NULL;
			E.row[i].hl = NULL;
			E.row[i].hl_open_comment = 0;
			editorUpdateRow(&E.row[i]);
		}
	} else {
		E.row = NULL;
	}
	
	// Validate cursor position
	if (E.cy >= E.numrows) E.cy = E.numrows > 0 ? E.numrows - 1 : 0;
	if (E.cy < 0) E.cy = 0;
	
	erow *row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
	int rowlen = row ? row->size : 0;
	if (E.cx > rowlen) E.cx = rowlen;
	
	E.dirty++;
}

void editorUndo(void) {
	if (!E.undo_stack) {
		editorSetStatusMessage("Nothing to undo");
		return;
	}
	
	// Save current state to redo stack
	undoState *current_state = editorCreateUndoState();
	if (current_state) {
		current_state->next = E.redo_stack;
		E.redo_stack = current_state;
	}
	
	// Pop from undo stack
	undoState *state = E.undo_stack;
	E.undo_stack = state->next;
	
	// Restore state
	editorRestoreState(state);
	
	// Free state
	free(state->filename);
	free(state->yank_buffer);
	for (int i = 0; i < state->numrows; i++) {
		free(state->rows[i].chars);
	}
	free(state->rows);
	free(state);
	
	editorSetStatusMessage("Undo successful");
}

void editorRedo(void) {
	if (!E.redo_stack) {
		editorSetStatusMessage("Nothing to redo");
		return;
	}
	
	// Save current state to undo stack
	undoState *current_state = editorCreateUndoState();
	if (current_state) {
		current_state->next = E.undo_stack;
		E.undo_stack = current_state;
	}
	
	// Pop from redo stack
	undoState *state = E.redo_stack;
	E.redo_stack = state->next;
	
	// Restore state
	editorRestoreState(state);
	
	// Free state
	free(state->filename);
	free(state->yank_buffer);
	for (int i = 0; i < state->numrows; i++) {
		free(state->rows[i].chars);
	}
	free(state->rows);
	free(state);
	
	editorSetStatusMessage("Redo successful");
}

void editorFreeUndoStack(undoState **stack) {
	while (*stack) {
		undoState *temp = *stack;
		*stack = (*stack)->next;
		
		free(temp->filename);
		free(temp->yank_buffer);
		for (int i = 0; i < temp->numrows; i++) {
			free(temp->rows[i].chars);
		}
		free(temp->rows);
		free(temp);
	}
}


/*** line numbers ***/
void editorUpdateLineNumberWidth() {
	if (!E.show_line_numbers) {
		E.line_number_width = 0;
	} else {
		E.line_number_width = 4;
	}
}

void editorDrawLineNumber(struct abuf *ab, int filerow) {
	if (!E.show_line_numbers) return;
	
	int number;
	if (E.show_line_numbers == 1) {
		// Absolute line numbers
		number = filerow + 1;
	} else {
		// Relative line numbers
		if (filerow == E.cy) {
			number = 1;
		} else {
			number = abs(filerow - E.cy) + 1;
		}
	}
	
	char line_num[8];
	snprintf(line_num, sizeof(line_num), "%3d ", number);
	
	abAppend(ab, "\x1b[90m", 5);
	abAppend(ab, line_num, 4);
	abAppend(ab, "\x1b[39m", 5);
}


/*** navigation ***/
void editorMoveCursor(int key) {
	erow *row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
	
	switch (key) {
		case ARROW_LEFT:
		case 'h':
			if (E.cx != 0) {
				E.cx--;
			}
			else if (E.cy > 0) {
				E.cy--;
				E.cx = E.row[E.cy].size;
			}
			break;
		case ARROW_RIGHT:
		case 'l':
			if (row && E.cx < row->size) {
				E.cx++;
			}
			else if (row && E.cx == row->size && E.cy + 1 < E.numrows) {
				E.cy++;
				E.cx = 0;
			}
			break;
		case ARROW_UP:
		case 'k':
			if (E.cy != 0) {
				E.cy--;
			}
			break;
		case ARROW_DOWN:
		case 'j':
			if (E.cy + 1 < E.numrows) {
				E.cy++;
			}
			break;
	}
	
	// Snap cursor to valid position
	row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
	int rowlen = row ? row->size : 0;
	if (E.cx > rowlen) {
		E.cx = rowlen;
	}
}

void editorJumpTop() {
	E.cy = 0;
}

void editorJumpBottom() {
	E.cy = E.numrows > 0 ? E.numrows - 1 : 0;
}


/*** input ***/
char *editorPrompt(char *prompt, void (*callback)(char *, int)) {
	size_t bufsize = 128;
	char *buf = malloc(bufsize);
	
	size_t buflen = 0;
	buf[0] = '\0';
	
	while (1) {
		editorSetStatusMessage(prompt, buf);
		editorRefreshScreen();
		
		int c = editorReadKey();
		if (c == DEL_KEY || c == CTRL_KEY('h') || c == BACKSPACE) {
			if (buflen != 0) buf[--buflen] = '\0';
		}
		else if (c == '\x1b') {
			editorSetStatusMessage("");
			if (callback) callback(buf, c);
			free(buf);
			return NULL;
		}
		else if (c == '\r') {
			if (buflen != 0) {
				editorSetStatusMessage("");
				if (callback) callback(buf, c);
				return buf;
			}
		}
		else if (!iscntrl(c) && c < 128) {
			if (buflen == bufsize - 1) {
				bufsize *= 2;
				buf = realloc(buf, bufsize);
			}
			buf[buflen++] = c;
			buf[buflen] = '\0';
		}
		if (callback) callback(buf, c);
	}
}

int editorReadKey() {
	int nread;
	char c;
	while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
		if (nread == -1 && errno != EAGAIN) die("read");
	}
	
	if (c == '\x1b') {
		char seq[3];
		
		if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
		if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';
		
		if (seq[0] == '[') {
			if (seq[1] >= '0' && seq[1] <= '9') {
				if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
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
			}
			else {
				switch (seq[1]) {
					case 'A': return ARROW_UP;
					case 'B': return ARROW_DOWN;
					case 'C': return ARROW_RIGHT;
					case 'D': return ARROW_LEFT;
					case 'H': return HOME_KEY;
					case 'F': return END_KEY;
				}
			}
		}
		else if (seq[0] == 'O') {
			switch (seq[1]) {
				case 'H': return HOME_KEY;
				case 'F': return END_KEY;
			}
		}
		
		return '\x1b';
	}
	else {
		return c;
	}
}

void editorColonCommand() {
	char *cmd = editorPrompt(":%s", NULL);
	if (cmd == NULL) return;
	
	// Line jump
	if (isdigit((unsigned char)cmd[0])) {
		int line = atoi(cmd);
		if (line < 1) line = 1;
		
		if (E.numrows == 0) {
			editorSetStatusMessage("No lines in file");
			free(cmd);
			return;
		}
		
		if (line > E.numrows) {
			line = E.numrows;
			editorSetStatusMessage("Line %d out of range, moved to line %d", atoi(cmd), line);
		}
		
		E.cy = line - 1;
		E.cx = 0;
		E.rowoff = E.cy;
		free(cmd);
		return;
	}
	
	// Commands
	if (strcmp(cmd, "q") == 0 || strcmp(cmd, "quit") == 0) {
		if (E.dirty) {
			// Check if it's a new file that was never saved
			FILE *fp = fopen(E.filename, "r");
			if (!fp && errno == ENOENT) {
				// New file, never saved - just exit
				exit(0);
			}
			if (fp) fclose(fp);
			
			editorSetStatusMessage("Unsaved changes. Use :q! to force quit.");
		} else {
			exit(0);
		}
	} else if (strcmp(cmd, "q!") == 0) {
		exit(0);
	} else if (strcmp(cmd, "w") == 0 || strcmp(cmd, "write") == 0) {
		editorSave();
	} else if (strcmp(cmd, "wq") == 0 || strcmp(cmd, "x") == 0) {
		editorSave();
		exit(0);
	} else if (strcmp(cmd, "help") == 0 || strcmp(cmd, "h") == 0) {
		editorSetStatusMessage("Commands: :w :q :q! :wq :<num> :help | Keys: i v V / u ^R dd yy p P tt bb ^F ^B 0 $ w b J r");
	} else {
		editorSetStatusMessage("Unknown command: %s", cmd);
	}
	free(cmd);
}

void editorProcessKeyPress() {
	static int last_char = 0;
	
	int c = editorReadKey();
	
	if (E.mode == MODE_NORMAL) {
		switch (c) {
			case 'i':
				editorSetMode(MODE_INSERT);
				break;
				
			case 'V':
				E.visual_anchor_x = 0;
				E.visual_anchor_y = E.cy;
				editorSetMode(MODE_VISUAL_LINE);
				break;
				
			case 'v':
				E.visual_anchor_x = E.cx;
				E.visual_anchor_y = E.cy;
				editorSetMode(MODE_VISUAL);
				break;
				
			case 'u':
				editorUndo();
				break;
				
			case 'y':
				if (last_char == 'y') {
					editorStartUndoBlock();
					free(E.yank_buffer);
					if (E.cy < E.numrows) {
						E.yank_buffer_len = E.row[E.cy].size + 1;
						E.yank_buffer = malloc(E.yank_buffer_len + 1);
						memcpy(E.yank_buffer, E.row[E.cy].chars, E.row[E.cy].size);
						E.yank_buffer[E.row[E.cy].size] = '\n';
						E.yank_buffer[E.yank_buffer_len] = '\0';
						editorSetStatusMessage("1 line yanked");
					}
					editorEndUndoBlock();
					last_char = 0;
				} else {
					last_char = 'y';
				}
				break;
				
			case 'r': // Replace character
				if (E.cy < E.numrows && E.cx < E.row[E.cy].size) {
					int next_char = editorReadKey();
					if (next_char != '\x1b' && !iscntrl(next_char)) {
						editorStartUndoBlock();
						editorRowDelChar(&E.row[E.cy], E.cx);
						editorRowInsertChar(&E.row[E.cy], E.cx, next_char);
						editorEndUndoBlock();
					}
				}
				last_char = 0;
				break;
				
			case 'J': // Join lines
				if (E.cy < E.numrows - 1) {
					editorStartUndoBlock();
					E.cx = E.row[E.cy].size;
					if (E.row[E.cy].size > 0 && E.row[E.cy + 1].size > 0) {
						editorInsertChar(' ');
					}
					editorRowAppendString(&E.row[E.cy], E.row[E.cy + 1].chars, E.row[E.cy + 1].size);
					editorDelRow(E.cy + 1);
					editorEndUndoBlock();
				}
				last_char = 0;
				break;
				
			case 'w': // Next word
				if (E.cy < E.numrows) {
					erow *row = &E.row[E.cy];
					while (E.cx < row->size && !is_separator(row->chars[E.cx])) E.cx++;
					while (E.cx < row->size && is_separator(row->chars[E.cx])) E.cx++;
					if (E.cx >= row->size && E.cy < E.numrows - 1) {
						E.cy++;
						E.cx = 0;
					}
				}
				last_char = 0;
				break;
				
			case 'b':
				if (last_char == 'b') {
					editorJumpBottom();
					last_char = 0;
				} else {
					// Previous word
					if (E.cx > 0 || E.cy > 0) {
						if (E.cx == 0 && E.cy > 0) {
							E.cy--;
							E.cx = E.row[E.cy].size;
						} else {
							erow *row = &E.row[E.cy];
							while (E.cx > 0 && is_separator(row->chars[E.cx - 1])) E.cx--;
							while (E.cx > 0 && !is_separator(row->chars[E.cx - 1])) E.cx--;
						}
					}
					last_char = 'b';
				}
				break;
				
			case '0': // Beginning of line
				E.cx = 0;
				last_char = 0;
				break;
				
			case '$': // End of line
				if (E.cy < E.numrows) {
					E.cx = E.row[E.cy].size;
				}
				last_char = 0;
				break;
				
			case CTRL_KEY('r'):
				editorRedo();
				break;
				
			case 'p': // Paste after
				if (E.yank_buffer && E.yank_buffer_len > 0) {
					editorStartUndoBlock();
					
					if (strchr(E.yank_buffer, '\n')) {
						E.cx = (E.cy < E.numrows) ? E.row[E.cy].size : 0;
						editorInsertNewLine();
						
						for (int i = 0; i < E.yank_buffer_len; i++) {
							if (E.yank_buffer[i] == '\n') {
								editorInsertNewLine();
							} else {
								editorInsertChar(E.yank_buffer[i]);
							}
						}
					} else {
						if (E.cy < E.numrows) {
							E.cx++;
							if (E.cx > E.row[E.cy].size) E.cx = E.row[E.cy].size;
						}
						
						for (int i = 0; i < E.yank_buffer_len; i++) {
							editorInsertChar(E.yank_buffer[i]);
						}
					}
					
					editorEndUndoBlock();
					editorSetStatusMessage("Pasted %d bytes", E.yank_buffer_len);
				} else {
					editorSetStatusMessage("Nothing to paste");
				}
				break;
				
			case 'P': // Paste before
				if (E.yank_buffer && E.yank_buffer_len > 0) {
					editorStartUndoBlock();
					
					if (strchr(E.yank_buffer, '\n')) {
						E.cx = 0;
						editorInsertNewLine();
						E.cy--;
						E.cx = 0;
						
						for (int i = 0; i < E.yank_buffer_len; i++) {
							if (E.yank_buffer[i] == '\n') {
								editorInsertNewLine();
							} else {
								editorInsertChar(E.yank_buffer[i]);
							}
						}
					} else {
						for (int i = 0; i < E.yank_buffer_len; i++) {
							editorInsertChar(E.yank_buffer[i]);
						}
					}
					
					editorEndUndoBlock();
					editorSetStatusMessage("Pasted %d bytes", E.yank_buffer_len);
				} else {
					editorSetStatusMessage("Nothing to paste");
				}
				break;
				
			case 'q':
				editorSetStatusMessage("Use :q or :wq to quit/save");
				break;
				
			case ':':
				editorColonCommand();
				break;
				
			case '/':
				editorFind();
				break;
				
			case 'h':
			case 'j':
			case 'k':
			case 'l':
			case ARROW_LEFT:
			case ARROW_RIGHT:
			case ARROW_UP:
			case ARROW_DOWN:
				editorMoveCursor(c);
				break;
				
			case 't':
				if (last_char == 't') {
					editorJumpTop();
					last_char = 0;
					break;
				}
				last_char = 't';
				break;
				
			case 'd':
				if (last_char == 'd') {
					editorSaveState();
					editorDelRow(E.cy);
					
					if (E.numrows == 0) {
						editorInsertRow(0, "", 0);
						E.rowoff = E.coloff = 0;
					}
					
					if (E.cy >= E.numrows) E.cy = E.numrows - 1;
					if (E.cy < 0) E.cy = 0;
					
					E.cx = 0;
					
					last_char = 0;
				} else {
					last_char = 'd';
				}
				break;
				
			case CTRL_KEY('f'): // Page forward
				E.rowoff += E.screenrows;
				if (E.rowoff > E.numrows - E.screenrows)
					E.rowoff = E.numrows - E.screenrows;
				if (E.rowoff < 0) E.rowoff = 0;
				E.cy = E.rowoff;
				last_char = 0;
				break;
				
			case CTRL_KEY('b'): // Page backward
				E.rowoff -= E.screenrows;
				if (E.rowoff < 0) E.rowoff = 0;
				E.cy = E.rowoff;
				last_char = 0;
				break;
				
			default:
				last_char = 0;
				break;
		}
	}
	else if (E.mode == MODE_INSERT) {
		switch (c) {
			case '\r':
				editorInsertNewLine();
				break;
				
			case BACKSPACE:
			case CTRL_KEY('h'):
			case DEL_KEY:
				if (c == DEL_KEY) editorMoveCursor(ARROW_RIGHT);
				editorDelChar();
				break;
				
			case ARROW_LEFT:
			case ARROW_RIGHT:
			case ARROW_UP:
			case ARROW_DOWN:
				editorMoveCursor(c);
				break;
				
			case CTRL_KEY('q'):
				editorSetStatusMessage("Use :q in normal mode to quit.");
				break;
				
			case '\x1b':
				editorEndUndoBlock();
				editorSetMode(MODE_NORMAL);
				break;
				
			default:
				editorInsertChar(c);
				break;
		}
	}
	else if (E.mode == MODE_VISUAL || E.mode == MODE_VISUAL_LINE) {
		switch (c) {
			case '\x1b':
				editorSetMode(MODE_NORMAL);
				break;
				
			case 'j':
			case 'k':
			case ARROW_UP:
			case ARROW_DOWN:
				editorMoveCursor(c);
				break;
				
			// Allow h/l movement only in character mode
			case 'h':
			case 'l':
			case ARROW_LEFT:
			case ARROW_RIGHT:
				if (E.mode == MODE_VISUAL) {
					editorMoveCursor(c);
				}
				break;
				
			case 'd':
			case 'x':
				editorDeleteVisualSelection();
				editorSetMode(MODE_NORMAL);
				break;
				
			case 'y':
				editorYankVisualSelection();
				editorSetMode(MODE_NORMAL);
				break;
				
			case 'c':
				editorDeleteVisualSelection();
				editorSetMode(MODE_INSERT);
				break;
		}
	}
}


/*** config ***/
void editorLoadConfig() {
	FILE *fp = NULL;
	char *config_paths[] = {
		"./.hakorc",
		NULL,
		NULL
	};
	
	// Try home directory
	const char *home = getenv("HOME");
	if (home) {
		static char homerc[512];
		snprintf(homerc, sizeof(homerc), "%s/.hakorc", home);
		config_paths[1] = homerc;
	}
	
	// Try to open config file
	for (int i = 0; config_paths[i] && !fp; i++) {
		fp = fopen(config_paths[i], "r");
	}
	
	if (!fp) return;
	
	// Parse config
	char *line = NULL;
	size_t len = 0;
	while (getline(&line, &len, fp) != -1) {
		char *eq = strchr(line, '=');
		if (!eq) continue;
		*eq = '\0';
		char *key = line;
		char *val = eq + 1;
		
		// Remove trailing newline
		val[strcspn(val, "\r\n")] = 0;
		
		// Process settings
		if (strcmp(key, "tab_stop") == 0) {
			E.tab_stop = atoi(val);
		}
		else if (strcmp(key, "mode") == 0) {
			if (strcmp(val, "insert") == 0) E.mode = MODE_INSERT;
			else if (strcmp(val, "normal") == 0) E.mode = MODE_NORMAL;
		}
		else if (strcmp(key, "fg_color") == 0) {
			char *hex = val;
			if (*hex == '#') hex++;
			E.fg_color = (int)strtol(hex, NULL, 16);
		}
		else if (strcmp(key, "bg_color") == 0) {
			char *hex = val;
			if (*hex == '#') hex++;
			E.bg_color = (int)strtol(hex, NULL, 16);
		}
		else if (strcmp(key, "show_line_numbers") == 0) {
			E.show_line_numbers = atoi(val);
		}
		else if (strcmp(key, "max_undo_levels") == 0) {
			E.max_undo_levels = atoi(val);
		}
	}
	free(line);
	fclose(fp);
}


/*** init ***/
void initEditor() {
	E.cx = 0;
	E.cy = 0;
	E.rx = 0;
	E.rowoff = 0;
	E.coloff = 0;
	E.numrows = 0;
	E.row = NULL;
	E.dirty = 0;
	E.mode = MODE_NORMAL;
	E.visual_anchor_x = 0;
	E.visual_anchor_y = 0;
	E.yank_buffer = NULL;
	E.yank_buffer_len = 0;
	E.filename = NULL;
	E.statusmsg[0] = '\0';
	E.statusmsg_time = 0;
	E.syntax = NULL;
	E.tab_stop = 8;
	E.max_undo_levels = 100;
	E.undo_stack = NULL;
	E.redo_stack = NULL;
	E.undo_block_active = 0;
	E.last_edit_time = 0;
	E.last_edit_cx = 0;
	E.last_edit_cy = 0;
	E.show_line_numbers = 1;
	E.line_number_width = 0;
	
	// Default theme
	E.fg_color = 0xE0E0E0;
	E.bg_color = 0x1E1E1E;
	E.color_comment = 244;
	E.color_keyword1 = 33;
	E.color_keyword2 = 75;
	E.color_string = 173;
	E.color_number = 208;
	E.color_match = 42;
	
	editorLoadConfig();
	
	editorUpdateWindowSize();
}

void editorUpdateWindowSize() {
	int rows, cols;
	if (getWindowSize(&rows, &cols) == -1) die("getWindowSize");
	E.screenrows = rows - 2;
	E.screencols = cols;
}


/*** main ***/
int main(int argc, char *argv[]) {
	// Handle command line arguments
	if (argc >= 2) {
		if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
			printf("%s", HAKO_HELP_TEXT);
			return 0;
		}
		if (strcmp(argv[1], "-v") == 0 || strcmp(argv[1], "--version") == 0) {
			printf("hako version %s\n", HAKO_VERSION);
			return 0;
		}
	}
	
	// Initialize editor
	initEditor();
	enableRawMode();
	
	// Set up window resize handler
	struct sigaction sa;
	sa.sa_handler = on_sigwinch;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;
	if (sigaction(SIGWINCH, &sa, NULL) == -1) {
		die("sigaction");
	}
	
	// Open file if provided
	if (argc >= 2) {
		editorOpen(argv[1]);
	}
	
	editorSetStatusMessage(":h = help | :w = write | :q = quit | / = fuzz");
	
	// Main loop
	while (1) {
		editorRefreshScreen();
		
		// Handle window resize
		if (winch_received) {
			winch_received = 0;
			editorUpdateWindowSize();
			editorRefreshScreen();
		}
		
		editorProcessKeyPress();
	}
	
	return 0;
}