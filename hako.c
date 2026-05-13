/*
HAKO — a minimalistic text editor
Copyright (C) 2026 Zachary Blauser

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/
				
/*** includes ***/
#define HAKO_VERSION "0.1.1"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#include <conio.h>
#include <io.h>
#include <direct.h>
#include <sys/types.h>
#include <sys/stat.h>
#ifndef PATH_MAX
#define PATH_MAX MAX_PATH
#endif
#define popen _popen
#define pclose _pclose
#define getcwd _getcwd
#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define read(fd, buf, n) _read(fd, buf, n)
#define write(fd, buf, n) _write(fd, buf, n)
#define mkdir(p, m) _mkdir(p)
#define realpath(p, r) _fullpath((r), (p), PATH_MAX)
#ifdef _MSC_VER
#define S_ISDIR(m) (((m) & _S_IFMT) == _S_IFDIR)
#define S_ISREG(m) (((m) & _S_IFMT) == _S_IFREG)
#define strcasecmp _stricmp
#endif
#if defined(__MINGW32__) || defined(__MINGW64__)
#include <dirent.h>
#include <unistd.h>
#ifndef S_ISREG
#define S_ISREG(m) (((m) & _S_IFMT) == _S_IFREG)
#endif
#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & _S_IFMT) == _S_IFDIR)
#endif
#include <stdlib.h>
static long hk_getline(char **lineptr, size_t *n, FILE *stream) {
	if (!lineptr || !n || !stream) return -1;
	if (!*lineptr || *n == 0) { *n = 256; *lineptr = realloc(*lineptr, *n); if (!*lineptr) return -1; }
	size_t len = 0; int c;
	while ((c = fgetc(stream)) != EOF) {
		if (len + 1 >= *n) {
			size_t nn = *n * 2;
			char *t = realloc(*lineptr, nn);
			if (!t) return -1;
			*lineptr = t; *n = nn;
		}
		(*lineptr)[len++] = (char)c;
		if (c == '\n') break;
	}
	if (len == 0 && c == EOF) return -1;
	(*lineptr)[len] = '\0';
	return (long)len;
}
#define getline hk_getline
#endif
#else
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>
#include <signal.h>
#include <dirent.h>
#include <limits.h>
#endif
#include <pthread.h>

/*** defines ***/
#define CTRL_KEY(k) ((k) & 0x1f)
#define ABUF_INIT {NULL, 0, 0}
#define HL_HIGHLIGHT_NUMBERS (1<<0)
#define HL_HIGHLIGHT_STRINGS (1<<1)
#define HLDB_ENTRIES (sizeof(HLDB) / sizeof(HLDB[0]))
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#define TAB_STOP 8
#define MAX_PANES 32
#define PLUGIN_MAX 64
#define AI_HISTORY_MAX 1000
#define PASTE_BUFFER_MAX 65536

const char *HAKO_HELP_TEXT = 
	"hako - A minimal text editor v" HAKO_VERSION "\n\n"
	"Usage: hako [options] [file]\n\n"
	"Options:\n"
	"  -h, --help     Show this help message\n"
	"  -v, --version  Show version information\n\n"
	"Commands:\n"
	"  Normal Mode:\n"
	"    i              Enter insert mode\n"
	"    I              Insert at first non-blank\n"
	"    a              Append after cursor\n"
	"    A              Append at end of line\n"
	"    o              Open line below\n"
	"    O              Open line above\n"
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
	"    D              Delete to end of line\n"
	"    x              Delete character\n"
	"    C              Change to end of line\n"
	"    yy             Yank line\n"
	"    p              Paste after\n"
	"    P              Paste before\n"
	"    r              Replace character\n"
	"    J              Join lines\n"
	"    gg             Jump to top\n"
	"    G              Jump to bottom\n"
	"    Ctrl-F         Page forward\n"
	"    Ctrl-B         Page backward\n\n"
	"  Visual Mode:\n"
	"    y              Yank selection\n"
	"    d,x            Delete/cut selection\n"
	"    c              Change selection\n"
	"    Esc            Exit visual mode\n\n"
	"  Insert Mode:\n"
	"    Esc            Return to normal mode\n";

/*** enums ***/
enum editorMode {
	MODE_NORMAL,
	MODE_INSERT,
	MODE_VISUAL,
	MODE_VISUAL_LINE,
	MODE_AI,
	MODE_COMMAND
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
	PAGE_DOWN,
	MOUSE_WHEEL_UP,
	MOUSE_WHEEL_DOWN,
	MOUSE_CLICK,
	MOUSE_MOTION
};

enum editorHighlight {
	HL_NORMAL = 0,
	HL_COMMENT,
	HL_MLCOMMENT,
	HL_KEYWORD1,
	HL_KEYWORD2,
	HL_STRING,
	HL_NUMBER,
	HL_MATCH,
	HL_PREPROCESSOR,
	HL_FUNCTION,
	HL_TYPE,
	HL_OPERATOR,
	HL_BRACKET,
	HL_CONSTANT,
	HL_BUILTIN,
	HL_ATTRIBUTE,
	HL_CHAR,
	HL_ESCAPE,
	HL_LABEL
};

enum paneType {
	PANE_EDITOR,
	PANE_EXPLORER,
	PANE_AI,
	PANE_PLUGIN
};

enum terminalType {
	TERM_UNKNOWN,
	TERM_XTERM_256,
	TERM_TRUECOLOR,
	TERM_BASIC
};

enum themePreset {
	THEME_DARK,
	THEME_LIGHT,
	THEME_SOLARIZED,
	THEME_GRUVBOX,
	THEME_MONOKAI,
	THEME_NORD,
	THEME_DRACULA,
	THEME_TOKYONIGHT,
	THEME_CATPPUCCIN,
	THEME_ONEDARK,
	THEME_MATERIAL,
	THEME_EVERFOREST,
	THEME_ROSEPINE,
	THEME_GITHUB_DARK,
	THEME_GITHUB_LIGHT,
	THEME_AYU,
	THEME_KANAGAWA,
	THEME_CUSTOM
};

enum splitDirection {
	SPLIT_NONE,
	SPLIT_HORIZONTAL,
	SPLIT_VERTICAL
};

enum aiProviderType {
	AI_PROVIDER_NONE,
	AI_PROVIDER_OLLAMA,
	AI_PROVIDER_ANTHROPIC,
	AI_PROVIDER_OPENAI
};

/*** struct declarations ***/
typedef struct erow {
	int idx;
	int size;
	int rsize;
	char *chars;
	char *render;
	unsigned char *hl;
	int hl_open_comment;
	int indent;
} erow;

struct abuf {
	char *b;
	int len;
	int cap;
};

typedef struct {
	int r, g, b;
} Color;

typedef struct {
	char ch[5];
	Color fg;
	Color bg;
	signed char width;
} Cell;

typedef struct undoState {
	erow *rows;
	int numrows;
	int cx, cy;
	char *filename;
	char *yank_buffer;
	int yank_buffer_len;
	struct undoState *next;
	time_t timestamp;
	enum editorMode mode;
} undoState;

typedef struct explorerData {
	char *current_dir;
	char **entries;
	int num_entries;
	int selected;
	int scroll_offset;
} explorerData;

typedef struct pluginAPI {
	void (*insert_text)(const char *text);
	char *(*get_current_line)(void);
	void (*set_status)(const char *, ...);
	int (*get_cursor_pos)(int *x, int *y);
	void (*move_cursor)(int x, int y);
	char *(*get_selection)(void);
	void (*replace_selection)(const char *text);
} pluginAPI;

typedef struct pluginData {
	void *handle;
	char *name;
	char *path;
	void (*init)(struct pluginAPI *api);
	void (*update)(void);
	void (*cleanup)(void);
	void (*process_key)(int key);
	void (*process_command)(const char *cmd);
	void *context;
	int active;
} pluginData;

typedef struct aiProvider {
	char *name;
	char *endpoint;
	char *api_key;
	char *model;
	int (*send_request)(const char *prompt, char **response);
	void (*stream_response)(const char *prompt, void (*callback)(const char *chunk));
} aiProvider;

typedef struct aiMessage {
	char *role;
	char *content;
	int raw;
} aiMessage;

/* history role tags: render uses these to pick prefix + color, replacing
 * the old msg_idx%2 alternation that scrambled colors when system lines
 * were interleaved with user/assistant turns. */
#define HK_ROLE_SYSTEM 0
#define HK_ROLE_USER   1
#define HK_ROLE_AI     2

typedef struct aiData {
	aiProvider *provider;
	char **history;
	unsigned char *history_role;
	int history_count;
	int history_pos;
	int scroll_offset;
	int spinner_frame;
	int cursor_x;
	int cursor_y;
	enum editorMode mode;
	int visual_mode;
	int visual_start;
	int visual_end;
	char *prompt_buffer;
	int prompt_len;
	int prompt_cursor;
	int prompt_capacity;
	char *current_prompt;
	char *current_response;
	int active;
	pthread_t worker_thread;
	int streaming;
	pthread_mutex_t lock;

	aiMessage *messages;
	int message_count;
	int message_cap;
	char *system_prompt;

	int last_in_tokens;
	int last_out_tokens;
	long total_in_tokens;
	long total_out_tokens;
} aiData;

typedef struct editorPane {
	enum paneType type;
	int id;
	
	int x, y;
	int width, height;
	
	int cx, cy;
	int rx;
	int rowoff, coloff;
	
	int numrows;
	erow *row;
	char *filename;
	int dirty;
	struct editorSyntax *syntax;
	
	int visual_anchor_x, visual_anchor_y;
	
	undoState *undo_stack;
	undoState *redo_stack;
	int undo_stack_size;
	int redo_stack_size;
	
	explorerData *explorer;
	pluginData *plugin;
	aiData *ai;
	
	char *pane_yank_buffer;
	int pane_yank_buffer_len;
	
	struct editorPane *parent;
	struct editorPane *child1;
	struct editorPane *child2;
	enum splitDirection split_dir;
	float split_ratio;
	
	int is_focused;
	int wrap_lines;
} editorPane;

typedef struct {
	Color bg;
	Color fg;
	Color comment;
	Color keyword1;
	Color keyword2;
	Color string;
	Color number;
	Color match;
	Color preprocessor;
	Color function;
	Color type;
	Color operator;
	Color bracket;
	Color line_number;
	Color status_bg;
	Color status_fg;
	Color border;
	Color visual_bg;
	Color visual_fg;
	Color constant;
	Color builtin;
	Color attribute;
	Color char_literal;
	Color escape;
	Color label;
	Color ai_bg;
	Color ai_fg;
	Color ai_prompt;
	Color ai_response;
} Theme;

struct editorSyntax {
	char *filetype;
	char **filematch;
	char **keywords;
	char *singleline_comment_start;
	char *multiline_comment_start;
	char *multiline_comment_end;
	int flags;
};

typedef struct {
	char *query;
	int last_match_row;
	int last_match_col;
	int direction;
	int wrap_search;
} SearchState;

typedef struct pasteBuffer {
	char *data;
	int len;
	int is_line_mode;
	time_t timestamp;
} pasteBuffer;

struct editorConfig {
	int screenrows, screencols;
	enum editorMode mode;
#ifdef _WIN32
	int orig_in_mode;
	int orig_out_mode;
#else
	struct termios orig_termios;
#endif
	
	int tab_stop;
	int use_tabs;
	int word_wrap;
	int word_wrap_column;
	int show_line_numbers;
	int line_number_width;
	int max_undo_levels;
	
	char statusmsg[256];
	time_t statusmsg_time;
	
	pasteBuffer paste;
	pasteBuffer system_paste;
	
	editorPane *root_pane;
	editorPane *active_pane;
	editorPane *left_panel;
	editorPane *right_panel;
	int num_panes;
	int left_panel_width;
	int right_panel_width;
	
	int in_window_command;
	int force_window_command;
	int splash_active;
	int splash_dismissed;
	int full_redraw_pending;

	Cell *grid_front;
	Cell *grid_back;
	int grid_w, grid_h;

	char *explorer_name;
	char *explorer_kanji;
	char *ai_name;
	char *ai_kanji;

	int explorer_enabled;
	int explorer_width;
	int explorer_show_hidden;
	int explorer_auto_open;
	
	SearchState search;
	
	enum terminalType term_type;
	enum themePreset theme_preset;
	Theme theme;
	
	int is_pasting;
	int paste_indent_level;
	
	int mouse_enabled;
	int mouse_x, mouse_y;
	int mouse_button;
	
	pluginData *plugins[PLUGIN_MAX];
	int plugin_count;
	char *plugin_dir;
	pluginAPI plugin_api;
	
	enum aiProviderType ai_provider_type;
	aiProvider *ai_providers[8];
	int ai_provider_count;
	aiProvider *current_ai;
	char *ai_api_key;
	char *ai_endpoint;
	char *ai_model;
	int ai_temperature;
	int ai_max_tokens;
	int ai_tools_enabled;
	int ai_stream;
	int ai_autowrite;

	char *session_id;
	long session_started;
	long session_last_used;
	int session_turn_count;
	int session_resumed;
	
	int auto_indent;
	int smart_indent;
	int indent_guides;
	int scroll_speed;
	int relative_line_numbers;

	char *config_path;

	/* modal state: counts, registers, marks, jumps, dot-repeat */
	struct {
		int pending_count;
		char pending_reg;
		char pending_op;
		int op_start_x, op_start_y;
		int waiting_reg;

		pasteBuffer registers[32];

		int marks_x[26], marks_y[26];
		int marks_set;

		struct { int x, y; char *filename; } jumps[100];
		int jump_count;
		int jump_pos;

		char last_op;
		int last_count;
		char last_reg;
		char last_motion;
		int last_motion_arg;
		char *last_insert;
		int last_insert_len;
		int recording_insert;
	} hk;
};

struct editorConfig E;

/*** function prototypes ***/
void disableRawMode(void);
void editorCleanup(void);
void editorSetStatusMessage(const char *fmt, ...);
void editorRefreshScreen(void);
void editorUpdateWindowSize(void);
int editorReadKey(void);
char *editorPrompt(char *prompt, void (*callback)(char *, int));
void editorMoveCursor(int key);
void editorSave(void);
void editorOpen(char *filename);
void editorInsertRow(int at, char *s, size_t len);
void editorDelRow(int at);
void editorUpdateRow(erow *row);
void editorFreeRow(erow *row);
void editorRowInsertChar(erow *row, int at, int c);
void editorRowAppendString(erow *row, char *s, size_t len);
void editorRowDelChar(erow *row, int at);
int editorRowCxToRx(erow *row, int cx);
int editorRowRxToCx(erow *row, int rx);
void editorInsertChar(int c);
void editorDelChar(void);
void editorInsertNewLine(void);
void editorFind(void);
void editorFindNext(void);
void editorFindPrev(void);
void editorSaveState(void);
void editorUndo(void);
void editorRedo(void);
void editorYankVisualSelection(void);
void editorDeleteVisualSelection(void);
void editorGetVisualSelection(int *start_x, int *start_y, int *end_x, int *end_y);
void editorHandlePaste(const char *text, int len);
void editorScroll(void);
void editorDrawRows(struct abuf *ab);
void editorDrawStatusBar(struct abuf *ab);
void editorDrawMessageBar(struct abuf *ab);
void editorDrawSplash(void);
void editorProcessKeyPress(void);
int editorInputPending(void);
void editorLoadConfig(void);
void editorUpdateSyntax(erow *row);
void editorSelectSyntaxHighlight(void);
char *editorRowsToString(int *buflen);
int editorGetIndent(erow *row);
void editorSetMode(enum editorMode mode);
void editorColonCommand(void);
int editorIsInVisualSelection(int row, int col);
void editorProcessMouse(int button, int x, int y);
void editorHandleScroll(int direction);
void editorCopyToSystemClipboard(void);
void editorPasteFromSystemClipboard(void);
void editorInitPasteBuffer(void);
void editorFreePasteBuffer(void);
void editorSetPasteBuffer(const char *data, int len, int is_line_mode);
void hkSetRegister(char name, const char *data, int len, int is_line, int is_yank);
pasteBuffer *hkGetRegister(char name);
void hkFreeRegisters(void);
void hkPushJump(int x, int y);
void hkClampCursor(editorPane *pane);
void hkLogMessage(const char *role, const char *content);
void hkLoadHistoryTail(aiData *data, int max_msgs);
int hkLoadSkills(aiData *data);
void hkSaveSession(void);
void hkLoadSession(void);
const char *hkProviderName(enum aiProviderType t);
static int hkProjectDirPath(char *out, size_t n);
static int hkProjectTrusted(void);
static int hkGrantProjectTrust(void);
int hkHandleSlash(aiData *data, const char *prompt);
void aiPushMessage(aiData *data, const char *role, const char *content);
void aiPushMessageRaw(aiData *data, const char *role, const char *content_json);
void aiFreeMessages(aiData *data);

editorPane *editorCreatePane(enum paneType type, int x, int y, int width, int height);
void editorFreePane(editorPane *pane);
editorPane *editorGetActivePane(void);
void editorSplitPane(int vertical);
void editorClosePane(void);
void editorNextPane(void);
void editorResizePanes(void);
void editorUpdatePaneBounds(editorPane *pane);
void editorCollectLeafPanes(editorPane *root, editorPane ***panes, int *count);
editorPane *editorFindPaneById(editorPane *root, int id);
void editorDrawPane(editorPane *pane, struct abuf *ab);

undoState *editorCreateUndoState(void);
void editorFreeUndoState(undoState *state);
void editorRestoreState(undoState *state);

void explorerInit(editorPane *pane);
void explorerRefresh(editorPane *pane);
void explorerHandleKey(editorPane *pane, int key);
void explorerCleanup(editorPane *pane);
void explorerRender(editorPane *pane, struct abuf *ab);
void editorToggleExplorer(void);

void editorInitAI(void);
void editorCleanupAI(void);
void editorToggleAI(void);
void editorSendToAI(const char *prompt);
void aiRender(editorPane *pane, struct abuf *ab);
void aiInit(editorPane *pane);
void aiHandleKey(editorPane *pane, int key);
void editorGenerateConfig(void);
void aiWorkerSend(aiData *data);
void *aiWorkerThread(void *arg);
char *aiExtractResponse(const char *json, enum aiProviderType type);
char *aiBuildCurlCommand(aiData *data, enum aiProviderType type);
void aiAddHistory(aiData *data, const char *text);
void aiAddHistoryRole(aiData *data, const char *text, unsigned char role);
int explorerCompare(const void *a, const void *b);

void initTheme(void);
int editorApplyThemeByName(const char *name);
void initEditor(void);
void abAppend(struct abuf *ab, const char *s, int len);
void abFree(struct abuf *ab);
void gridResize(int w, int h);
void gridFree(void);
void gridClear(Color bg);
void gridInvalidateFront(void);
int gridPutStr(int x, int y, const char *s, int max_cols, Color fg, Color bg);
int gridPutCh(int x, int y, char c, Color fg, Color bg);
void gridFlush(struct abuf *out);
int is_separator(int c);
void setThemeColor(struct abuf *ab, Color color);
void setThemeBgColor(struct abuf *ab, Color color);
int editorSyntaxToColor(int hl);
Color editorSyntaxToRGB(int hl);
void setColor(struct abuf *ab, int color);
void setBgColor(struct abuf *ab, int color);
void detectTerminalType(void);
void enableRawMode(void);
int getWindowSize(int *rows, int *cols);
void die(const char *s);
int getDisplayWidth(const char *str, int len, int col);
int getCharWidth(char c, int col);
int utf8_byte_length(unsigned char c);
int utf8_char_width(const char *s, int pos);
int utf8_next_char(const char *s, int pos, int size);
int utf8_prev_char(const char *s, int pos);

#undef exit
#define exit(code) do { \
	editorCleanup(); \
	disableRawMode(); \
	fflush(stdout); \
	_exit(code); \
} while(0)

/*** syntax highlighting ***/

char *C_HL_extensions[] = {".c", ".h", NULL};
char *C_HL_keywords[] = {
	"switch", "if", "while", "for", "break", "continue", "return", "else",
	"goto", "do", "case", "default", "static", "extern", "register", "auto",
	"inline", "restrict", "struct", "union", "typedef", "enum",
	"int|", "long|", "double|", "float|", "char|", "unsigned|", "signed|",
	"void|", "short|", "size_t|", "ssize_t|", "uint8_t|", "uint16_t|",
	"uint32_t|", "uint64_t|", "int8_t|", "int16_t|", "int32_t|", "int64_t|",
	"bool|", "const|", "volatile|",
	"#define", "#include", "#if", "#ifdef", "#ifndef", "#elif", "#else",
	"#endif", "#pragma", "NULL", "EOF", "true", "false", NULL
};

char *CPP_HL_extensions[] = {".cpp", ".hpp", ".cc", ".cxx", ".C", ".h++", NULL};
char *CPP_HL_keywords[] = {
	"class", "public", "private", "protected", "virtual", "override", "final",
	"namespace", "using", "template", "typename", "try", "catch", "throw",
	"new", "delete", "this", "friend", "operator", "explicit", "export",
	"mutable", "noexcept", "constexpr", "nullptr", "decltype",
	"int|", "long|", "double|", "float|", "char|", "bool|", "void|",
	"auto|", "const|", "static|", "extern|", "inline|",
	"std|", "string|", "vector|", "map|", "set|", "unique_ptr|", "shared_ptr|",
	"true", "false", "nullptr", NULL
};

char *PY_HL_extensions[] = {".py", ".pyw", ".pyi", NULL};
char *PY_HL_keywords[] = {
	"def", "class", "if", "elif", "else", "while", "for", "in", "try",
	"except", "finally", "with", "as", "pass", "break", "continue",
	"print", "return", "yield", "raise", "assert", "import", "from", "lambda",
	"async", "await", "and", "or", "not", "is", "del",
	"int|", "float|", "str|", "bool|", "list|", "dict|", "set|", "tuple|",
	"None|", "True", "False", "__init__", "__name__", "__main__", NULL
};

char *JS_HL_extensions[] = {".js", ".jsx", ".mjs", NULL};
char *JS_HL_keywords[] = {
	"function", "if", "else", "for", "while", "do", "switch", "case",
	"break", "continue", "return", "try", "catch", "finally", "throw",
	"var", "let", "const", "class", "extends", "new", "this",
	"async", "await", "typeof", "instanceof", "in", "of", "delete",
	"true", "false", "null", "undefined", "NaN", "Infinity", NULL
};

char *TS_HL_extensions[] = {".ts", ".tsx", NULL};
char *TS_HL_keywords[] = {
	"interface", "type", "enum", "namespace", "module", "declare",
	"abstract", "implements", "private", "protected", "public", "readonly",
	"function", "class", "extends", "import", "export", "async", "await",
	"string|", "number|", "boolean|", "any|", "void|", "never|", "unknown|",
	"true", "false", "null", "undefined", NULL
};

char *JAVA_HL_extensions[] = {".java", NULL};
char *JAVA_HL_keywords[] = {
	"class", "interface", "enum", "extends", "implements", "package",
	"import", "public", "private", "protected", "static", "final",
	"abstract", "synchronized", "volatile", "transient",
	"if", "else", "for", "while", "do", "switch", "case", "default",
	"break", "continue", "return", "try", "catch", "finally", "throw",
	"new", "this", "super", "instanceof",
	"void|", "boolean|", "byte|", "char|", "short|", "int|", "long|",
	"float|", "double|", "String|", "true", "false", "null", NULL
};

char *GO_HL_extensions[] = {".go", NULL};
char *GO_HL_keywords[] = {
	"func", "package", "import", "var", "const", "type", "struct",
	"interface", "map", "chan", "if", "else", "for", "range", "switch",
	"case", "default", "break", "continue", "goto", "return", "defer",
	"go", "select", "fallthrough",
	"int|", "int8|", "int16|", "int32|", "int64|", "uint|", "uint8|",
	"uint16|", "uint32|", "uint64|", "float32|", "float64|", "bool|",
	"string|", "byte|", "rune|", "error|", "true", "false", "nil", "iota", NULL
};

char *RUST_HL_extensions[] = {".rs", NULL};
char *RUST_HL_keywords[] = {
	"fn", "let", "mut", "const", "static", "struct", "enum", "trait",
	"impl", "type", "pub", "use", "mod", "crate", "self", "super",
	"as", "async", "await", "move", "ref", "unsafe", "where",
	"if", "else", "match", "for", "while", "loop", "break", "continue", "return",
	"i8|", "i16|", "i32|", "i64|", "i128|", "u8|", "u16|", "u32|", "u64|",
	"u128|", "f32|", "f64|", "bool|", "char|", "str|", "String|",
	"true", "false", "Some", "None", "Ok", "Err", NULL
};

char *RUBY_HL_extensions[] = {".rb", ".rake", ".gemspec", NULL};
char *RUBY_HL_keywords[] = {
	"def", "class", "module", "if", "elsif", "else", "unless", "case",
	"when", "while", "until", "for", "in", "do", "begin", "rescue",
	"ensure", "end", "return", "yield", "break", "next", "redo", "retry",
	"require", "include", "extend", "attr_reader", "attr_writer",
	"attr_accessor", "alias", "super", "self", "nil", "true", "false",
	"and", "or", "not", "__FILE__", "__LINE__", NULL
};

char *SWIFT_HL_extensions[] = {".swift", NULL};
char *SWIFT_HL_keywords[] = {
	"class", "struct", "enum", "protocol", "extension", "func", "var", "let",
	"if", "else", "switch", "case", "default", "for", "while", "repeat",
	"break", "continue", "return", "guard", "defer", "do", "try", "catch",
	"throw", "import", "public", "private", "internal", "static", "final",
	"lazy", "weak", "unowned", "override", "init", "deinit", "subscript",
	"typealias", "associatedtype", "where", "some", "any",
	"Int|", "Double|", "Float|", "Bool|", "String|", "Character|", "Optional|",
	"Array|", "Dictionary|", "Set|", "Result|",
	"true", "false", "nil", "self", "super", "Self", NULL
};

char *ASM_HL_extensions[] = {".asm", ".s", ".S", NULL};
char *ASM_HL_keywords[] = {
	"mov", "movl", "movq", "movb", "movw", "lea", "push", "pop", "add",
	"sub", "mul", "div", "inc", "dec", "cmp", "jmp", "je", "jne", "jz",
	"jnz", "jg", "jge", "jl", "jle", "call", "ret", "nop", "int", "syscall",
	"and", "or", "xor", "not", "shl", "shr", "sal", "sar", "test",
	"eax", "ebx", "ecx", "edx", "esi", "edi", "esp", "ebp",
	"rax", "rbx", "rcx", "rdx", "rsi", "rdi", "rsp", "rbp",
	"r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15",
	"al", "bl", "cl", "dl", "ah", "bh", "ch", "dh",
	"section", "global", "extern", "db", "dw", "dd", "dq", "resb", "resw", "resd", "resq", NULL
};

char *ARM_HL_extensions[] = {".arm", ".s", NULL};
char *ARM_HL_keywords[] = {
	"add", "sub", "mul", "div", "and", "or", "xor", "not", "lsl", "lsr",
	"asr", "ror", "cmp", "tst", "teq", "mov", "mvn", "ldr", "str", "ldm",
	"stm", "push", "pop", "b", "bl", "bx", "beq", "bne", "bcs", "bcc",
	"bmi", "bpl", "bvs", "bvc", "bhi", "bls", "bge", "blt", "bgt", "ble",
	"r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7", "r8", "r9", "r10",
	"r11", "r12", "r13", "r14", "r15", "sp", "lr", "pc", "cpsr", "spsr", NULL
};

char *MIPS_HL_extensions[] = {".mips", ".s", NULL};
char *MIPS_HL_keywords[] = {
	"add", "addi", "sub", "mul", "div", "and", "andi", "or", "ori", "xor",
	"nor", "sll", "srl", "sra", "slt", "slti", "beq", "bne", "j", "jal",
	"jr", "lw", "sw", "lb", "sb", "lh", "sh", "lui", "mfhi", "mflo", "mthi",
	"mtlo", "syscall", "nop",
	"$zero", "$at", "$v0", "$v1", "$a0", "$a1", "$a2", "$a3",
	"$t0", "$t1", "$t2", "$t3", "$t4", "$t5", "$t6", "$t7",
	"$s0", "$s1", "$s2", "$s3", "$s4", "$s5", "$s6", "$s7",
	"$t8", "$t9", "$k0", "$k1", "$gp", "$sp", "$fp", "$ra", NULL
};

char *SQL_HL_extensions[] = {".sql", NULL};
char *SQL_HL_keywords[] = {
	"SELECT", "FROM", "WHERE", "INSERT", "UPDATE", "DELETE", "CREATE",
	"ALTER", "DROP", "TABLE", "DATABASE", "INDEX", "VIEW", "PROCEDURE",
	"JOIN", "INNER", "LEFT", "RIGHT", "FULL", "OUTER", "ON", "AS",
	"GROUP", "BY", "HAVING", "ORDER", "ASC", "DESC", "LIMIT", "OFFSET",
	"UNION", "INTERSECT", "EXCEPT", "AND", "OR", "NOT", "IN", "EXISTS",
	"BETWEEN", "LIKE", "IS", "NULL", "DISTINCT", "COUNT", "SUM", "AVG",
	"MIN", "MAX", "CASE", "WHEN", "THEN", "ELSE", "END",
	"INT|", "VARCHAR|", "TEXT|", "DATE|", "TIMESTAMP|", "BOOLEAN|", "FLOAT|",
	"PRIMARY", "KEY", "FOREIGN", "REFERENCES", "UNIQUE", "DEFAULT", NULL
};

char *PHP_HL_extensions[] = {".php", ".phtml", NULL};
char *PHP_HL_keywords[] = {
	"class", "function", "public", "private", "protected", "static",
	"if", "elseif", "else", "while", "for", "foreach", "switch", "case",
	"break", "continue", "return", "try", "catch", "finally", "throw",
	"new", "extends", "implements", "interface", "abstract", "trait",
	"namespace", "use", "as", "const", "var", "global",
	"int|", "float|", "string|", "bool|", "array|", "object|", "null|",
	"true", "false", "null", "$this", "echo", "print", "die", "exit", NULL
};

char *KOTLIN_HL_extensions[] = {".kt", ".kts", NULL};
char *KOTLIN_HL_keywords[] = {
	"class", "interface", "fun", "object", "val", "var", "package", "import",
	"if", "else", "when", "for", "while", "do", "break", "continue", "return",
	"try", "catch", "finally", "throw", "public", "private", "protected",
	"internal", "open", "final", "abstract", "data", "sealed", "companion",
	"Int|", "Long|", "Double|", "Float|", "Boolean|", "String|", "Char|",
	"Byte|", "Short|", "Any|", "Unit|", "Nothing|", "true", "false", "null", NULL
};

char *CS_HL_extensions[] = {".cs", NULL};
char *CS_HL_keywords[] = {
	"class", "struct", "interface", "enum", "namespace", "using", "public",
	"private", "protected", "internal", "static", "void", "if", "else",
	"switch", "case", "default", "for", "foreach", "while", "do", "break",
	"continue", "return", "try", "catch", "finally", "throw", "new", "this",
	"base", "virtual", "override", "abstract", "sealed", "async", "await",
	"bool|", "byte|", "char|", "decimal|", "double|", "float|", "int|",
	"long|", "object|", "sbyte|", "short|", "string|", "uint|", "ulong|",
	"ushort|", "true", "false", "null", NULL
};

char *LUA_HL_extensions[] = {".lua", NULL};
char *LUA_HL_keywords[] = {
	"and", "break", "do", "else", "elseif", "end", "false", "for",
	"function", "if", "in", "local", "nil", "not", "or", "repeat",
	"return", "then", "true", "until", "while", "goto",
	"assert", "collectgarbage", "dofile", "error", "getmetatable",
	"ipairs", "load", "loadfile", "next", "pairs", "pcall", "print",
	"rawequal", "rawget", "rawset", "require", "select", "setmetatable",
	"tonumber", "tostring", "type", "xpcall", "_G", "_VERSION", NULL
};

char *SHELL_HL_extensions[] = {".sh", ".bash", ".zsh", ".fish", NULL};
char *SHELL_HL_keywords[] = {
	"if", "then", "else", "elif", "fi", "for", "while", "do", "done",
	"case", "esac", "function", "return", "break", "continue", "exit",
	"export", "source", "alias", "unalias", "shift", "local", "declare",
	"echo", "printf", "read", "cd", "pwd", "ls", "cp", "mv", "rm", "mkdir",
	"rmdir", "touch", "cat", "grep", "sed", "awk", "find", "chmod", "chown",
	"ps", "kill", "bg", "fg", "jobs", "test", "[", "]", "true", "false", NULL
};

char *ELIXIR_HL_extensions[] = {".ex", ".exs", NULL};
char *ELIXIR_HL_keywords[] = {
	"def", "defp", "defmodule", "defprotocol", "defmacro", "defmacrop",
	"defdelegate", "defexception", "defstruct", "defimpl", "defcallback",
	"do", "end", "when", "and", "or", "not", "in", "if", "unless", "else",
	"case", "cond", "try", "catch", "rescue", "raise", "throw", "after",
	"receive", "send", "spawn", "spawn_link", "import", "require", "use",
	"alias", "nil", "true", "false", "__MODULE__", "__FILE__", "__DIR__",
	"__ENV__", "__CALLER__", NULL
};

char *HASKELL_HL_extensions[] = {".hs", ".lhs", NULL};
char *HASKELL_HL_keywords[] = {
	"module", "import", "qualified", "as", "hiding", "where", "data",
	"type", "newtype", "class", "instance", "deriving", "let", "in",
	"if", "then", "else", "case", "of", "do", "return", "infixl",
	"infixr", "infix", "forall", "foreign", "export", "dynamic",
	"safe", "unsafe", "threadsafe", "stdcall", "ccall",
	"Int|", "Integer|", "Float|", "Double|", "Char|", "String|", "Bool|",
	"Maybe|", "Either|", "IO|", "True", "False", "Nothing", "Just",
	"Left", "Right", "LT", "EQ", "GT", NULL
};

char *CLOJURE_HL_extensions[] = {".clj", ".cljs", ".cljc", NULL};
char *CLOJURE_HL_keywords[] = {
	"def", "defn", "defn-", "defmacro", "defmulti", "defmethod", "defonce",
	"declare", "ns", "in-ns", "import", "use", "require", "load", "compile",
	"let", "letfn", "binding", "if", "if-not", "when", "when-not", "cond",
	"case", "and", "or", "not", "do", "doto", "loop", "recur", "for",
	"doseq", "dotimes", "while", "try", "catch", "finally", "throw",
	"nil", "true", "false", NULL
};

char *DART_HL_extensions[] = {".dart", NULL};
char *DART_HL_keywords[] = {
	"abstract", "as", "assert", "async", "await", "break", "case", "catch",
	"class", "const", "continue", "default", "deferred", "do", "dynamic",
	"else", "enum", "export", "extends", "external", "factory", "false",
	"final", "finally", "for", "get", "if", "implements", "import", "in",
	"is", "library", "new", "null", "operator", "part", "rethrow", "return",
	"set", "static", "super", "switch", "sync", "this", "throw", "true",
	"try", "typedef", "var", "void", "while", "with", "yield",
	"int|", "double|", "num|", "String|", "bool|", "List|", "Map|", "Set|", NULL
};

char *JULIA_HL_extensions[] = {".jl", NULL};
char *JULIA_HL_keywords[] = {
	"abstract", "baremodule", "begin", "break", "catch", "const", "continue",
	"do", "else", "elseif", "end", "export", "false", "finally", "for",
	"function", "global", "if", "import", "let", "local", "macro", "module",
	"quote", "return", "struct", "true", "try", "using", "while", "where",
	"in", "isa", "mutable", "primitive", "type",
	"Int|", "Int8|", "Int16|", "Int32|", "Int64|", "Int128|", "UInt|",
	"UInt8|", "UInt16|", "UInt32|", "UInt64|", "UInt128|", "Float16|",
	"Float32|", "Float64|", "Bool|", "Char|", "String|", "Any|", "Union|",
	"Nothing|", "nothing", "missing", "Inf", "NaN", NULL
};

char *ZIG_HL_extensions[] = {".zig", NULL};
char *ZIG_HL_keywords[] = {
	"const", "var", "fn", "pub", "extern", "export", "inline", "comptime",
	"if", "else", "switch", "and", "or", "while", "for", "break", "continue",
	"return", "defer", "errdefer", "try", "catch", "orelse", "async", "await",
	"suspend", "resume", "cancel", "struct", "union", "enum", "error",
	"packed", "opaque", "test", "usingnamespace", "asm", "volatile",
	"i8|", "u8|", "i16|", "u16|", "i32|", "u32|", "i64|", "u64|", "i128|",
	"u128|", "isize|", "usize|", "f16|", "f32|", "f64|", "f128|", "bool|",
	"void|", "noreturn|", "type|", "anyerror|", "comptime_int|",
	"comptime_float|", "true", "false", "null", "undefined", NULL
};

char *NIM_HL_extensions[] = {".nim", ".nims", NULL};
char *NIM_HL_keywords[] = {
	"proc", "func", "method", "iterator", "template", "macro", "converter",
	"var", "let", "const", "type", "object", "tuple", "enum", "ref", "ptr",
	"if", "elif", "else", "when", "case", "of", "while", "for", "break",
	"continue", "return", "yield", "block", "try", "except", "finally",
	"raise", "defer", "import", "include", "from", "export", "as", "using",
	"int|", "int8|", "int16|", "int32|", "int64|", "uint|", "uint8|",
	"uint16|", "uint32|", "uint64|", "float|", "float32|", "float64|",
	"bool|", "char|", "string|", "seq|", "array|", "openarray|", "void|",
	"auto|", "any|", "true", "false", "nil", NULL
};

char *OCAML_HL_extensions[] = {".ml", ".mli", NULL};
char *OCAML_HL_keywords[] = {
	"let", "rec", "in", "fun", "function", "if", "then", "else", "match",
	"with", "when", "try", "raise", "begin", "end", "for", "to", "downto",
	"while", "do", "done", "ref", "mutable", "type", "of", "module",
	"struct", "sig", "functor", "val", "open", "include", "exception",
	"class", "object", "method", "inherit", "virtual", "private", "new",
	"int|", "float|", "bool|", "char|", "string|", "unit|", "list|",
	"array|", "option|", "ref|", "true", "false", "()", "[]", NULL
};

char *FSHARP_HL_extensions[] = {".fs", ".fsi", ".fsx", NULL};
char *FSHARP_HL_keywords[] = {
	"let", "rec", "mutable", "fun", "function", "if", "then", "else",
	"elif", "match", "with", "when", "try", "finally", "for", "to",
	"downto", "while", "do", "yield", "return", "use", "using", "new",
	"type", "of", "module", "namespace", "open", "import", "export",
	"public", "private", "internal", "static", "member", "val", "inherit",
	"abstract", "interface", "class", "struct", "exception",
	"int|", "float|", "bool|", "char|", "string|", "unit|", "list|",
	"array|", "seq|", "option|", "async|", "true", "false", "null", "()", NULL
};

char *R_HL_extensions[] = {".r", ".R", NULL};
char *R_HL_keywords[] = {
	"if", "else", "repeat", "while", "function", "for", "in", "next",
	"break", "TRUE", "FALSE", "NULL", "Inf", "NaN", "NA", "return",
	"invisible", "stop", "warning", "message", "library", "require",
	"source", "install.packages", "data.frame", "matrix", "array", "list",
	"vector", "factor", "character", "numeric", "integer", "logical",
	"complex", "as.numeric", "as.character", "as.logical", "as.factor",
	"length", "dim", "nrow", "ncol", "head", "tail", "summary", "str", NULL
};

char *PERL_HL_extensions[] = {".pl", ".pm", NULL};
char *PERL_HL_keywords[] = {
	"if", "elsif", "else", "unless", "while", "until", "for", "foreach",
	"do", "sub", "package", "use", "require", "my", "our", "local", "state",
	"given", "when", "default", "continue", "break", "return", "last", "next",
	"redo", "goto", "eval", "BEGIN", "END", "print", "printf", "say",
	"open", "close", "read", "write", "seek", "tell", "eof", "binmode",
	"undef", "defined", "ref", "bless", "tie", "untie", "tied", "scalar",
	"@", "$", "%", "&", "*", NULL
};

char *SCALA_HL_extensions[] = {".scala", ".sc", NULL};
char *SCALA_HL_keywords[] = {
	"class", "object", "trait", "extends", "with", "def", "val", "var",
	"if", "else", "match", "case", "for", "while", "do", "try", "catch",
	"finally", "throw", "return", "import", "package", "private", "protected",
	"public", "abstract", "final", "sealed", "implicit", "lazy", "override",
	"Int|", "Long|", "Double|", "Float|", "Boolean|", "String|", "Char|",
	"Byte|", "Short|", "Any|", "Unit|", "Nothing|", "true", "false", "null", NULL
};

char *HTML_HL_extensions[] = {".html", ".htm", ".xhtml", NULL};
char *HTML_HL_keywords[] = {
	"html", "head", "title", "body", "div", "span", "p", "a", "img",
	"ul", "ol", "li", "table", "tr", "td", "th", "form", "input",
	"button", "select", "option", "textarea", "label", "header", "footer",
	"nav", "section", "article", "aside", "main", "h1", "h2", "h3",
	"h4", "h5", "h6", "br", "hr", "link", "meta", "script", "style",
	"class", "id", "href", "src", "alt", "width", "height", "style",
	"type", "name", "value", "placeholder", "required", "disabled", NULL
};

char *CSS_HL_extensions[] = {".css", ".scss", ".sass", ".less", NULL};
char *CSS_HL_keywords[] = {
	"color", "background", "background-color", "border", "margin", "padding",
	"width", "height", "display", "position", "top", "right", "bottom", "left",
	"float", "clear", "font", "font-size", "font-family", "font-weight",
	"text-align", "text-decoration", "line-height", "z-index", "opacity",
	"visibility", "overflow", "cursor", "flex", "grid", "transform",
	"transition", "animation", "box-shadow", "border-radius",
	"absolute", "relative", "fixed", "static", "block", "inline", "none",
	"auto", "inherit", "initial", "px", "em", "rem", "%", "vh", "vw", NULL
};

char *MD_HL_extensions[] = {".md", ".markdown", NULL};
char *MD_HL_keywords[] = {
	"#", "##", "###", "####", "#####", "######", "*", "**", "_", "__",
	"-", "+", "1.", "2.", "3.", "[", "]", "(", ")", "![", ">", "`",
	"```", "~~~", "---", "***", "___", "|", NULL
};

char *YAML_HL_extensions[] = {".yml", ".yaml", NULL};
char *YAML_HL_keywords[] = {
	"true", "false", "null", "True", "False", "Null", "TRUE", "FALSE",
	"NULL", "yes", "no", "Yes", "No", "YES", "NO", "on", "off", "On",
	"Off", "ON", "OFF", NULL
};

char *JSON_HL_extensions[] = {".json", ".jsonc", NULL};
char *JSON_HL_keywords[] = {
	"true", "false", "null", NULL
};

char *XML_HL_extensions[] = {".xml", ".svg", ".xsl", ".xslt", NULL};
char *XML_HL_keywords[] = {
	"xml", "version", "encoding", "xmlns", "xsi", "schemaLocation",
	"id", "class", "style", "href", "src", "type", "name", "value", NULL
};

char *DOCKER_HL_extensions[] = {"Dockerfile", ".dockerfile", NULL};
char *DOCKER_HL_keywords[] = {
	"FROM", "RUN", "CMD", "LABEL", "EXPOSE", "ENV", "ADD", "COPY",
	"ENTRYPOINT", "VOLUME", "USER", "WORKDIR", "ARG", "ONBUILD",
	"STOPSIGNAL", "HEALTHCHECK", "SHELL", "MAINTAINER", NULL
};

char *MAKE_HL_extensions[] = {"Makefile", "makefile", ".mk", NULL};
char *MAKE_HL_keywords[] = {
	"ifeq", "ifneq", "ifdef", "ifndef", "else", "endif", "include",
	"define", "endef", "override", "export", "unexport", "vpath",
	".PHONY", ".SUFFIXES", ".DEFAULT", ".PRECIOUS", ".INTERMEDIATE",
	".SECONDARY", ".SECONDEXPANSION", ".DELETE_ON_ERROR", ".IGNORE",
	".LOW_RESOLUTION_TIME", ".SILENT", ".EXPORT_ALL_VARIABLES",
	".NOTPARALLEL", ".ONESHELL", ".POSIX", NULL
};

char *VIM_HL_extensions[] = {".vim", ".vimrc", NULL};
char *VIM_HL_keywords[] = {
	"if", "else", "elseif", "endif", "for", "endfor", "while", "endwhile",
	"function", "endfunction", "return", "call", "let", "set", "execute",
	"normal", "command", "autocmd", "augroup", "map", "nmap", "vmap",
	"imap", "noremap", "nnoremap", "vnoremap", "inoremap", "syntax",
	"highlight", "colorscheme", "source", "runtime", "echo", "echom",
	"silent", "vertical", "split", "vsplit", "tabnew", "buffer", "bnext",
	"bprevious", "bdelete", "write", "quit", "wq", "edit", NULL
};

char *TOML_HL_extensions[] = {".toml", NULL};
char *TOML_HL_keywords[] = {
	"true", "false", NULL
};

char *HAKO_HL_extensions[] = {".hakorc", "hakorc", NULL};
char *HAKO_HL_keywords[] = {
	"tab_stop", "use_tabs", "word_wrap", "word_wrap_column",
	"show_line_numbers", "max_undo_levels", "explorer_enabled",
	"explorer_width", "explorer_show_hidden", "show_splash", "mode",
	"theme", "theme_dark", "theme_light", "theme_solarized", "theme_gruvbox",
	"theme_monokai", "theme_nord", "theme_dracula",
	"theme_bg", "theme_fg", "theme_comment", "theme_keyword1",
	"theme_keyword2", "theme_string", "theme_number", "theme_match",
	"theme_preprocessor", "theme_function", "theme_type", "theme_operator",
	"theme_bracket", "theme_line_number", "theme_status_bg", "theme_status_fg",
	"theme_border", "theme_visual_bg", "theme_visual_fg",
	"auto_indent", "smart_indent", "mouse_enabled", "scroll_speed",
	"relative_numbers", "ai_provider", "ai_api_key", "ai_model",
	"plugin_dir", "true", "false", "normal", "insert", "claude", "gpt", "grok",
	"ollama", "local", "1", "0", NULL
};

char *DEFAULT_HL_extensions[] = {NULL};
char *DEFAULT_HL_keywords[] = {NULL};

struct editorSyntax HLDB[] = {
	{"c", C_HL_extensions, C_HL_keywords, "//", "/*", "*/", HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS},
	{"c++", CPP_HL_extensions, CPP_HL_keywords, "//", "/*", "*/", HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS},
	{"python", PY_HL_extensions, PY_HL_keywords, "#", "\"\"\"", "\"\"\"", HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS},
	{"javascript", JS_HL_extensions, JS_HL_keywords, "//", "/*", "*/", HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS},
	{"typescript", TS_HL_extensions, TS_HL_keywords, "//", "/*", "*/", HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS},
	{"java", JAVA_HL_extensions, JAVA_HL_keywords, "//", "/*", "*/", HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS},
	{"go", GO_HL_extensions, GO_HL_keywords, "//", "/*", "*/", HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS},
	{"rust", RUST_HL_extensions, RUST_HL_keywords, "//", "/*", "*/", HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS},
	{"ruby", RUBY_HL_extensions, RUBY_HL_keywords, "#", "=begin", "=end", HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS},
	{"swift", SWIFT_HL_extensions, SWIFT_HL_keywords, "//", "/*", "*/", HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS},
	{"php", PHP_HL_extensions, PHP_HL_keywords, "//", "/*", "*/", HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS},
	{"kotlin", KOTLIN_HL_extensions, KOTLIN_HL_keywords, "//", "/*", "*/", HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS},
	{"c#", CS_HL_extensions, CS_HL_keywords, "//", "/*", "*/", HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS},
	{"scala", SCALA_HL_extensions, SCALA_HL_keywords, "//", "/*", "*/", HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS},
	{"lua", LUA_HL_extensions, LUA_HL_keywords, "--", "--[[", "]]", HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS},
	{"perl", PERL_HL_extensions, PERL_HL_keywords, "#", NULL, NULL, HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS},
	{"r", R_HL_extensions, R_HL_keywords, "#", NULL, NULL, HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS},
	{"shell", SHELL_HL_extensions, SHELL_HL_keywords, "#", NULL, NULL, HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS},
	{"sql", SQL_HL_extensions, SQL_HL_keywords, "--", "/*", "*/", HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS},
	{"html", HTML_HL_extensions, HTML_HL_keywords, NULL, "<!--", "-->", HL_HIGHLIGHT_STRINGS},
	{"css", CSS_HL_extensions, CSS_HL_keywords, "//", "/*", "*/", HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS},
	{"markdown", MD_HL_extensions, MD_HL_keywords, NULL, NULL, NULL, HL_HIGHLIGHT_STRINGS},
	{"yaml", YAML_HL_extensions, YAML_HL_keywords, "#", NULL, NULL, HL_HIGHLIGHT_STRINGS},
	{"json", JSON_HL_extensions, JSON_HL_keywords, NULL, NULL, NULL, HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS},
	{"xml", XML_HL_extensions, XML_HL_keywords, NULL, "<!--", "-->", HL_HIGHLIGHT_STRINGS},
	{"docker", DOCKER_HL_extensions, DOCKER_HL_keywords, "#", NULL, NULL, HL_HIGHLIGHT_STRINGS},
	{"makefile", MAKE_HL_extensions, MAKE_HL_keywords, "#", NULL, NULL, HL_HIGHLIGHT_STRINGS},
	{"vim", VIM_HL_extensions, VIM_HL_keywords, "\"", NULL, NULL, HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS},
	{"elixir", ELIXIR_HL_extensions, ELIXIR_HL_keywords, "#", NULL, NULL, HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS},
	{"haskell", HASKELL_HL_extensions, HASKELL_HL_keywords, "--", "{-", "-}", HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS},
	{"clojure", CLOJURE_HL_extensions, CLOJURE_HL_keywords, ";", NULL, NULL, HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS},
	{"dart", DART_HL_extensions, DART_HL_keywords, "//", "/*", "*/", HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS},
	{"julia", JULIA_HL_extensions, JULIA_HL_keywords, "#", "#=", "=#", HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS},
	{"zig", ZIG_HL_extensions, ZIG_HL_keywords, "//", NULL, NULL, HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS},
	{"nim", NIM_HL_extensions, NIM_HL_keywords, "#", "#[", "]#", HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS},
	{"ocaml", OCAML_HL_extensions, OCAML_HL_keywords, NULL, "(*", "*)", HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS},
	{"f#", FSHARP_HL_extensions, FSHARP_HL_keywords, "//", "(*", "*)", HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS},
	{"assembly", ASM_HL_extensions, ASM_HL_keywords, ";", NULL, NULL, HL_HIGHLIGHT_NUMBERS},
	{"arm", ARM_HL_extensions, ARM_HL_keywords, ";", NULL, NULL, HL_HIGHLIGHT_NUMBERS},
	{"mips", MIPS_HL_extensions, MIPS_HL_keywords, "#", NULL, NULL, HL_HIGHLIGHT_NUMBERS},
	{"toml", TOML_HL_extensions, TOML_HL_keywords, "#", NULL, NULL, HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS},
	{"text", DEFAULT_HL_extensions, DEFAULT_HL_keywords, NULL, NULL, NULL, 0},
	{"hako", HAKO_HL_extensions, HAKO_HL_keywords, "#", NULL, NULL, HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS}
};


/*** terminal ***/
#ifdef _WIN32
static HANDLE hStdin, hStdout;
static DWORD w32_orig_in, w32_orig_out;

void disableRawMode() {
	SetConsoleMode(hStdin, w32_orig_in);
	SetConsoleMode(hStdout, w32_orig_out);
	write(STDOUT_FILENO, "\x1b[?2004l", 8);
	write(STDOUT_FILENO, "\x1b[?1049l", 8);
	write(STDOUT_FILENO, "\x1b[?25h", 6);
	write(STDOUT_FILENO, "\x1b[0 q", 5);
	write(STDOUT_FILENO, "\x1b[0m", 4);
	write(STDOUT_FILENO, "\x1b[39m\x1b[49m", 10);
}

void enableRawMode() {
	hStdin = GetStdHandle(STD_INPUT_HANDLE);
	hStdout = GetStdHandle(STD_OUTPUT_HANDLE);
	GetConsoleMode(hStdin, &w32_orig_in);
	GetConsoleMode(hStdout, &w32_orig_out);
	atexit(disableRawMode);

	DWORD in_mode = w32_orig_in;
	in_mode &= ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT);
	in_mode |= ENABLE_VIRTUAL_TERMINAL_INPUT;
	if (E.mouse_enabled) in_mode |= ENABLE_MOUSE_INPUT;
	SetConsoleMode(hStdin, in_mode);

	DWORD out_mode = w32_orig_out;
	out_mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING | DISABLE_NEWLINE_AUTO_RETURN;
	SetConsoleMode(hStdout, out_mode);

	write(STDOUT_FILENO, "\x1b[?1049h", 8);
	write(STDOUT_FILENO, "\x1b[2J", 4);
	write(STDOUT_FILENO, "\x1b[H", 3);
	write(STDOUT_FILENO, "\x1b[?2004h", 8);

	if (E.mouse_enabled) {
		write(STDOUT_FILENO, "\x1b[?1000h", 8);
		write(STDOUT_FILENO, "\x1b[?1002h", 8);
		write(STDOUT_FILENO, "\x1b[?1006h", 8);
	}
}

int getWindowSize(int *rows, int *cols) {
	CONSOLE_SCREEN_BUFFER_INFO csbi;
	if (!GetConsoleScreenBufferInfo(hStdout, &csbi)) return -1;
	*cols = csbi.srWindow.Right - csbi.srWindow.Left + 1;
	*rows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
	return 0;
}

int editorInputPending() {
	DWORD events = 0;
	GetNumberOfConsoleInputEvents(hStdin, &events);
	return events > 1;
}

#endif

void die(const char *s) {
	write(STDOUT_FILENO, "\x1b[2J", 4);
	write(STDOUT_FILENO, "\x1b[H", 3);
	perror(s);
	exit(1);
}

void detectTerminalType() {
	char *term = getenv("TERM");
	char *colorterm = getenv("COLORTERM");
	char *term_program = getenv("TERM_PROGRAM");

	E.term_type = TERM_BASIC;

	if (term_program && strcmp(term_program, "Apple_Terminal") == 0) {
		E.term_type = TERM_XTERM_256;
		return;
	}

	if (colorterm) {
		if (strcmp(colorterm, "truecolor") == 0 || strcmp(colorterm, "24bit") == 0) {
			E.term_type = TERM_TRUECOLOR;
			return;
		}
	}

	if (term) {
		if (strstr(term, "256color") || strstr(term, "kitty") ||
			strstr(term, "alacritty") || strstr(term, "ghostty") ||
			strstr(term, "wezterm") || strstr(term, "iterm")) {
			E.term_type = TERM_XTERM_256;
		} else if (strstr(term, "xterm")) {
			E.term_type = TERM_XTERM_256;
		}
	}
}

#ifndef _WIN32
volatile sig_atomic_t winch_received = 0;

static void on_sigwinch(int signo) {
	(void)signo;
	winch_received = 1;
}

void disableRawMode() {
	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1) die("tcsetattr");
	
	write(STDOUT_FILENO, "\x1b[?2004l", 8);
	write(STDOUT_FILENO, "\x1b[?1000l", 8);
	write(STDOUT_FILENO, "\x1b[?1002l", 8);
	write(STDOUT_FILENO, "\x1b[?1003l", 8);
	write(STDOUT_FILENO, "\x1b[?1015l", 8);
	write(STDOUT_FILENO, "\x1b[?1006l", 8);
	write(STDOUT_FILENO, "\x1b[?1049l", 8);
	write(STDOUT_FILENO, "\x1b[?25h", 6);
	write(STDOUT_FILENO, "\x1b[0 q", 5);
	write(STDOUT_FILENO, "\x1b[0m", 4);
	write(STDOUT_FILENO, "\x1b[39m\x1b[49m", 10);
}

void enableRawMode() {
	if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1) die("tcgetattr");
	atexit(disableRawMode);

	struct termios raw = E.orig_termios;
	raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
	raw.c_oflag &= ~(OPOST);
	raw.c_cflag |= CS8;
	raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
	raw.c_cc[VMIN] = 0;
	raw.c_cc[VTIME] = 1;

	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr");

	write(STDOUT_FILENO, "\x1b[?1049h", 8);
	write(STDOUT_FILENO, "\x1b[2J", 4);
	write(STDOUT_FILENO, "\x1b[H", 3);
	write(STDOUT_FILENO, "\x1b[?2004h", 8);

	if (E.mouse_enabled) {
		write(STDOUT_FILENO, "\x1b[?1000h", 8);
		write(STDOUT_FILENO, "\x1b[?1002h", 8);
		write(STDOUT_FILENO, "\x1b[?1003h", 8);
		write(STDOUT_FILENO, "\x1b[?1015h", 8);
		write(STDOUT_FILENO, "\x1b[?1006h", 8);
	}
}

int getWindowSize(int *rows, int *cols) {
	struct winsize ws;

	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
		if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1;
		return -1;
	} else {
		*cols = ws.ws_col;
		*rows = ws.ws_row;
		return 0;
	}
}

int editorInputPending() {
	fd_set fds;
	struct timeval tv = {0, 0};
	FD_ZERO(&fds);
	FD_SET(STDIN_FILENO, &fds);
	return select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv) > 0;
}

#endif

/*** buffer ***/
void abAppend(struct abuf *ab, const char *s, int len) {
	int needed = ab->len + len;
	if (needed > ab->cap) {
		int newcap = ab->cap < 1024 ? 1024 : ab->cap;
		while (newcap < needed) newcap *= 2;
		char *new = realloc(ab->b, newcap);
		if (new == NULL) return;
		ab->b = new;
		ab->cap = newcap;
	}
	memcpy(&ab->b[ab->len], s, len);
	ab->len += len;
}

void abFree(struct abuf *ab) {
	free(ab->b);
}

/*** cell grid (diff-render) ***/
static int gridColorEq(Color a, Color b) {
	return a.r == b.r && a.g == b.g && a.b == b.b;
}

void gridResize(int w, int h) {
	if (w < 1) w = 1;
	if (h < 1) h = 1;
	if (w > 4096) w = 4096;
	if (h > 4096) h = 4096;
	if (w == E.grid_w && h == E.grid_h && E.grid_front && E.grid_back) return;
	free(E.grid_front);
	free(E.grid_back);
	E.grid_w = w;
	E.grid_h = h;
	size_t n = (size_t)w * (size_t)h;
	E.grid_front = calloc(n, sizeof(Cell));
	E.grid_back = calloc(n, sizeof(Cell));
	if (!E.grid_front || !E.grid_back) {
		free(E.grid_front); free(E.grid_back);
		E.grid_front = E.grid_back = NULL;
		E.grid_w = E.grid_h = 0;
	}
}

void gridFree(void) {
	free(E.grid_front);
	free(E.grid_back);
	E.grid_front = NULL;
	E.grid_back = NULL;
	E.grid_w = 0;
	E.grid_h = 0;
}

void gridInvalidateFront(void) {
	if (E.grid_front && E.grid_w > 0 && E.grid_h > 0) {
		memset(E.grid_front, 0, (size_t)E.grid_w * E.grid_h * sizeof(Cell));
	}
}

void gridClear(Color bg) {
	if (!E.grid_back) return;
	int n = E.grid_w * E.grid_h;
	for (int i = 0; i < n; i++) {
		Cell *c = &E.grid_back[i];
		c->ch[0] = ' ';
		c->ch[1] = 0;
		c->ch[2] = 0;
		c->ch[3] = 0;
		c->ch[4] = 0;
		c->fg = E.theme.fg;
		c->bg = bg;
		c->width = 1;
	}
}

static void gridSetCell(int x, int y, const char *utf8, int byte_len, int width, Color fg, Color bg) {
	if (!E.grid_back) return;
	if (x < 0 || y < 0 || x >= E.grid_w || y >= E.grid_h) return;
	if (byte_len < 0) byte_len = 0;
	if (byte_len > 4) byte_len = 4;
	Cell *c = &E.grid_back[y * E.grid_w + x];
	memset(c->ch, 0, sizeof(c->ch));
	if (byte_len > 0) memcpy(c->ch, utf8, byte_len);
	else c->ch[0] = ' ';
	c->fg = fg;
	c->bg = bg;
	c->width = (signed char)width;
	if (width == 2 && x + 1 < E.grid_w) {
		Cell *cc = &E.grid_back[y * E.grid_w + x + 1];
		memset(cc->ch, 0, sizeof(cc->ch));
		cc->fg = fg;
		cc->bg = bg;
		cc->width = 0;
	}
}

int gridPutStr(int x, int y, const char *s, int max_cols, Color fg, Color bg) {
	if (!s || max_cols <= 0) return 0;
	int n = (int)strlen(s);
	int i = 0;
	int cols_used = 0;
	while (i < n && cols_used < max_cols) {
		unsigned char b = (unsigned char)s[i];
		if (b < 0x20) { i++; continue; }
		int blen = utf8_byte_length(b);
		if (i + blen > n) blen = n - i;
		int w = utf8_char_width(s, i);
		if (w < 1) w = 1;
		if (cols_used + w > max_cols) break;
		gridSetCell(x + cols_used, y, &s[i], blen, w, fg, bg);
		cols_used += w;
		i += blen;
	}
	return cols_used;
}

int gridPutCh(int x, int y, char c, Color fg, Color bg) {
	gridSetCell(x, y, &c, 1, 1, fg, bg);
	return 1;
}

void gridFlush(struct abuf *out) {
	if (!E.grid_back || !E.grid_front) return;
	Color cur_fg = { -1, -1, -1 };
	Color cur_bg = { -1, -1, -1 };
	int cur_x = -1, cur_y = -1;
	for (int y = 0; y < E.grid_h; y++) {
		for (int x = 0; x < E.grid_w; x++) {
			int idx = y * E.grid_w + x;
			Cell *bk = &E.grid_back[idx];
			Cell *fr = &E.grid_front[idx];
			if (bk->width == 0) continue;
			int same = (bk->width == fr->width
				&& memcmp(bk->ch, fr->ch, sizeof(bk->ch)) == 0
				&& gridColorEq(bk->fg, fr->fg)
				&& gridColorEq(bk->bg, fr->bg));
			if (same) continue;
			if (cur_y != y || cur_x != x) {
				char buf[32];
				snprintf(buf, sizeof(buf), "\x1b[%d;%dH", y + 1, x + 1);
				abAppend(out, buf, strlen(buf));
				cur_x = x;
				cur_y = y;
			}
			if (!gridColorEq(cur_fg, bk->fg)) {
				setThemeColor(out, bk->fg);
				cur_fg = bk->fg;
			}
			if (!gridColorEq(cur_bg, bk->bg)) {
				setThemeBgColor(out, bk->bg);
				cur_bg = bk->bg;
			}
			int blen = 0;
			while (blen < (int)sizeof(bk->ch) && bk->ch[blen]) blen++;
			if (blen == 0) { abAppend(out, " ", 1); }
			else abAppend(out, bk->ch, blen);
			cur_x += (bk->width > 0 ? bk->width : 1);
			*fr = *bk;
			if (bk->width == 2 && x + 1 < E.grid_w) {
				E.grid_front[idx + 1] = E.grid_back[idx + 1];
			}
		}
	}
}

/*** helpers ***/
int is_separator(int c) {
	return isspace(c) || c == '\0' || strchr(",.()+-/*=~%<>[];{}:\"'`!@#$%^&*|\\?", c) != NULL;
}

int getCharWidth(char c, int col) {
	if (c == '\t') {
		return E.tab_stop - (col % E.tab_stop);
	}
	return 1;
}

int getDisplayWidth(const char *str, int len, int col) {
	int width = 0;
	for (int i = 0; i < len; i++) {
		width += getCharWidth(str[i], col + width);
	}
	return width;
}

int utf8_byte_length(unsigned char c) {
	if ((c & 0x80) == 0) return 1;
	if ((c & 0xE0) == 0xC0) return 2;
	if ((c & 0xF0) == 0xE0) return 3;
	if ((c & 0xF8) == 0xF0) return 4;
	return 1;
}

int utf8_char_width(const char *s, int pos) {
	unsigned char c = s[pos];
	if ((c & 0x80) == 0) return 1;
	
	int len = utf8_byte_length(c);
	if (len == 3) {
		if (c == 0xE2) {
			unsigned char c1 = s[pos + 1];
			if ((c1 >= 0x94 && c1 <= 0x96) || (c1 >= 0x98 && c1 <= 0x9F)) {
				return 1;
			}
		}
		
		if ((c >= 0xE3 && c <= 0xE9) || c == 0xEF) {
			return 2;
		}
	}
	return 1;
}

int utf8_next_char(const char *s, int pos, int size) {
	if (pos >= size) return size;
	int len = utf8_byte_length((unsigned char)s[pos]);
	pos += len;
	if (pos > size) return size;
	return pos;
}

int utf8_prev_char(const char *s, int pos) {
	if (pos <= 0) return 0;
	pos--;
	while (pos > 0 && (s[pos] & 0xC0) == 0x80) pos--;
	return pos;
}

static int utf8_display_width(const char *s) {
	if (!s) return 0;
	int col = 0;
	int i = 0;
	int n = (int)strlen(s);
	while (i < n) {
		col += utf8_char_width(s, i);
		i = utf8_next_char(s, i, n);
	}
	return col;
}

/* Return byte offset+length covering display cols [start_col, start_col+max_cols).
 * Also returns the actual display-cell count of the slice in *out_cols. */
static void utf8_slice(const char *s, int start_col, int max_cols,
                       int *out_off, int *out_len, int *out_cols) {
	int col = 0, i = 0, n = s ? (int)strlen(s) : 0;
	int off = n, end = n, cols = 0;
	int found = 0;
	while (i < n) {
		int w = utf8_char_width(s, i);
		if (!found && col >= start_col) { off = i; found = 1; }
		if (found) {
			if (cols + w > max_cols) break;
			cols += w;
		}
		col += w;
		i = utf8_next_char(s, i, n);
		if (found) end = i;
	}
	if (!found) { off = n; end = n; }
	if (out_off)  *out_off  = off;
	if (out_len)  *out_len  = end - off;
	if (out_cols) *out_cols = cols;
}

/*** color ***/
void setThemeColor(struct abuf *ab, Color color) {
	char buf[32];
	
	if (E.term_type == TERM_TRUECOLOR) {
		snprintf(buf, sizeof(buf), "\x1b[38;2;%d;%d;%dm", color.r, color.g, color.b);
	} else if (E.term_type == TERM_XTERM_256) {
		int color_index = 16 + (36 * (color.r * 5 / 255)) + (6 * (color.g * 5 / 255)) + (color.b * 5 / 255);
		snprintf(buf, sizeof(buf), "\x1b[38;5;%dm", color_index);
	} else {
		int ansi_color = 40;

		if (color.r < 50 && color.g < 50 && color.b < 50) ansi_color = 40;
		else if (color.r > 180 && color.g < 100 && color.b < 100) ansi_color = 41;
		else if (color.r < 100 && color.g > 180 && color.b < 100) ansi_color = 42;
		else if (color.r > 180 && color.g > 180 && color.b < 100) ansi_color = 43;
		else if (color.r < 100 && color.g < 100 && color.b > 180) ansi_color = 44;
		else if (color.r > 180 && color.g < 100 && color.b > 180) ansi_color = 45;
		else if (color.r < 100 && color.g > 180 && color.b > 180) ansi_color = 46;
		else if (color.r > 200 && color.g > 200 && color.b > 200) ansi_color = 47;
		else {
			int intensity = (color.r + color.g + color.b) / 3;
			if (intensity > 127) ansi_color = 47;
			else if (color.b > color.r && color.b > color.g) ansi_color = 44;
			else if (color.g > color.r) ansi_color = 42;
			else if (color.r > color.g) ansi_color = 41;
		}

		snprintf(buf, sizeof(buf), "\x1b[%dm", ansi_color);
	}
	abAppend(ab, buf, strlen(buf));
}

void setThemeBgColor(struct abuf *ab, Color color) {
	char buf[32];
	
	if (E.term_type == TERM_TRUECOLOR) {
		snprintf(buf, sizeof(buf), "\x1b[48;2;%d;%d;%dm", color.r, color.g, color.b);
	} else if (E.term_type == TERM_XTERM_256) {
		int color_index = 16 + (36 * (color.r * 5 / 255)) + (6 * (color.g * 5 / 255)) + (color.b * 5 / 255);
		snprintf(buf, sizeof(buf), "\x1b[48;5;%dm", color_index);
	} else {
		int intensity = (color.r + color.g + color.b) / 3;
		int ansi_color = 40;
		
		if (intensity < 50) ansi_color = 40;
		else if (intensity > 200) ansi_color = 47;
		else ansi_color = 40;
		
		snprintf(buf, sizeof(buf), "\x1b[%dm", ansi_color);
	}
	abAppend(ab, buf, strlen(buf));
}

int editorSyntaxToColor(int hl) {
	switch (hl) {
	case HL_COMMENT:
	case HL_MLCOMMENT:
		return E.term_type == TERM_TRUECOLOR ? 0x6A9955 : (E.term_type == TERM_XTERM_256 ? 65 : 36);
	case HL_KEYWORD1:
		return E.term_type == TERM_TRUECOLOR ? 0x569CD6 : (E.term_type == TERM_XTERM_256 ? 69 : 34);
	case HL_KEYWORD2:
		return E.term_type == TERM_TRUECOLOR ? 0x4EC9B0 : (E.term_type == TERM_XTERM_256 ? 73 : 36);
	case HL_STRING:
		return E.term_type == TERM_TRUECOLOR ? 0xCE9178 : (E.term_type == TERM_XTERM_256 ? 173 : 33);
	case HL_NUMBER:
		return E.term_type == TERM_TRUECOLOR ? 0xB5CEA8 : (E.term_type == TERM_XTERM_256 ? 151 : 32);
	case HL_MATCH:
		return E.term_type == TERM_TRUECOLOR ? 0xFFFF00 : (E.term_type == TERM_XTERM_256 ? 226 : 33);
	case HL_PREPROCESSOR:
		return E.term_type == TERM_TRUECOLOR ? 0xC586C0 : (E.term_type == TERM_XTERM_256 ? 176 : 35);
	case HL_FUNCTION:
		return E.term_type == TERM_TRUECOLOR ? 0xDCDCAA : (E.term_type == TERM_XTERM_256 ? 187 : 33);
	case HL_TYPE:
		return E.term_type == TERM_TRUECOLOR ? 0x4EC9B0 : (E.term_type == TERM_XTERM_256 ? 73 : 36);
	case HL_OPERATOR:
		return E.term_type == TERM_TRUECOLOR ? 0xD4D4D4 : (E.term_type == TERM_XTERM_256 ? 188 : 37);
	case HL_BRACKET:
		return E.term_type == TERM_TRUECOLOR ? 0xDA70D6 : (E.term_type == TERM_XTERM_256 ? 170 : 35);
	case HL_CONSTANT:
		return E.term_type == TERM_TRUECOLOR ? 0x569CD6 : (E.term_type == TERM_XTERM_256 ? 69 : 34);
	case HL_BUILTIN:
		return E.term_type == TERM_TRUECOLOR ? 0xDCDCAA : (E.term_type == TERM_XTERM_256 ? 187 : 33);
	case HL_ATTRIBUTE:
		return E.term_type == TERM_TRUECOLOR ? 0x9CDCFE : (E.term_type == TERM_XTERM_256 ? 117 : 36);
	case HL_CHAR:
		return E.term_type == TERM_TRUECOLOR ? 0xE06C75 : (E.term_type == TERM_XTERM_256 ? 167 : 31);
	case HL_ESCAPE:
		return E.term_type == TERM_TRUECOLOR ? 0xD19A66 : (E.term_type == TERM_XTERM_256 ? 173 : 33);
	case HL_LABEL:
		return E.term_type == TERM_TRUECOLOR ? 0xC678DD : (E.term_type == TERM_XTERM_256 ? 176 : 35);
	default:
		return E.term_type == TERM_TRUECOLOR ? 0xD4D4D4 : (E.term_type == TERM_XTERM_256 ? 188 : 37);
	}
}

Color editorSyntaxToRGB(int hl) {
	int rgb;
	switch (hl) {
	case HL_COMMENT: case HL_MLCOMMENT: rgb = 0x6A9955; break;
	case HL_KEYWORD1: rgb = 0x569CD6; break;
	case HL_KEYWORD2: rgb = 0x4EC9B0; break;
	case HL_STRING: rgb = 0xCE9178; break;
	case HL_NUMBER: rgb = 0xB5CEA8; break;
	case HL_MATCH: rgb = 0xFFFF00; break;
	case HL_PREPROCESSOR: rgb = 0xC586C0; break;
	case HL_FUNCTION: rgb = 0xDCDCAA; break;
	case HL_TYPE: rgb = 0x4EC9B0; break;
	case HL_OPERATOR: rgb = 0xD4D4D4; break;
	case HL_BRACKET: rgb = 0xDA70D6; break;
	case HL_CONSTANT: rgb = 0x569CD6; break;
	case HL_BUILTIN: rgb = 0xDCDCAA; break;
	case HL_ATTRIBUTE: rgb = 0x9CDCFE; break;
	case HL_CHAR: rgb = 0xE06C75; break;
	case HL_ESCAPE: rgb = 0xD19A66; break;
	case HL_LABEL: rgb = 0xC678DD; break;
	default: rgb = 0xD4D4D4;
	}
	Color c = { (rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF };
	return c;
}

void setColor(struct abuf *ab, int color) {
	char buf[32];
	
	if (E.term_type == TERM_TRUECOLOR) {
		int r = (color >> 16) & 0xFF;
		int g = (color >> 8) & 0xFF;
		int b = color & 0xFF;
		snprintf(buf, sizeof(buf), "\x1b[38;2;%d;%d;%dm", r, g, b);
	} else if (E.term_type == TERM_XTERM_256) {
		snprintf(buf, sizeof(buf), "\x1b[38;5;%dm", color);
	} else {
		snprintf(buf, sizeof(buf), "\x1b[%dm", color);
	}
	abAppend(ab, buf, strlen(buf));
}

void setBgColor(struct abuf *ab, int color) {
	char buf[32];
	
	if (E.term_type == TERM_TRUECOLOR) {
		int r = (color >> 16) & 0xFF;
		int g = (color >> 8) & 0xFF;
		int b = color & 0xFF;
		snprintf(buf, sizeof(buf), "\x1b[48;2;%d;%d;%dm", r, g, b);
	} else if (E.term_type == TERM_XTERM_256) {
		snprintf(buf, sizeof(buf), "\x1b[48;5;%dm", color);
	} else {
		snprintf(buf, sizeof(buf), "\x1b[%dm", color + 10);
	}
	abAppend(ab, buf, strlen(buf));
}

/*** clipboard ***/
void editorInitPasteBuffer() {
	E.paste.data = NULL;
	E.paste.len = 0;
	E.paste.is_line_mode = 0;
	E.paste.timestamp = 0;
	
	E.system_paste.data = NULL;
	E.system_paste.len = 0;
	E.system_paste.is_line_mode = 0;
	E.system_paste.timestamp = 0;
}

void editorFreePasteBuffer() {
	free(E.paste.data);
	free(E.system_paste.data);
	E.paste.data = NULL;
	E.system_paste.data = NULL;
}

void editorSetPasteBuffer(const char *data, int len, int is_line_mode) {
	free(E.paste.data);

	if (len > PASTE_BUFFER_MAX) {
		len = PASTE_BUFFER_MAX;
		editorSetStatusMessage("Paste buffer truncated to %d bytes", PASTE_BUFFER_MAX);
	}

	E.paste.data = malloc(len + 1);
	if (!E.paste.data) return;

	memcpy(E.paste.data, data, len);
	E.paste.data[len] = '\0';
	E.paste.len = len;
	E.paste.is_line_mode = is_line_mode;
	E.paste.timestamp = time(NULL);
}

static int hkRegIndex(char name) {
	if (name >= 'a' && name <= 'z') return name - 'a';
	if (name >= 'A' && name <= 'Z') return name - 'A';
	if (name == '"' || name == 0) return 26;
	if (name == '0') return 27;
	if (name == '+' || name == '*') return 28;
	if (name == '-') return 29;
	return -1;
}

static void hkRegAssign(pasteBuffer *r, const char *data, int len, int is_line) {
	free(r->data);
	if (len > PASTE_BUFFER_MAX) len = PASTE_BUFFER_MAX;
	r->data = malloc(len + 1);
	if (!r->data) { r->len = 0; return; }
	memcpy(r->data, data, len);
	r->data[len] = '\0';
	r->len = len;
	r->is_line_mode = is_line;
	r->timestamp = time(NULL);
}

static void hkRegAppend(pasteBuffer *r, const char *data, int len, int is_line) {
	int newlen = r->len + len;
	if (newlen > PASTE_BUFFER_MAX) newlen = PASTE_BUFFER_MAX;
	char *nb = malloc(newlen + 1);
	if (!nb) return;
	if (r->data && r->len > 0) memcpy(nb, r->data, r->len);
	int copy = newlen - r->len;
	if (copy > 0) memcpy(nb + r->len, data, copy);
	nb[newlen] = '\0';
	free(r->data);
	r->data = nb;
	r->len = newlen;
	r->is_line_mode = is_line;
	r->timestamp = time(NULL);
}

void hkSetRegister(char name, const char *data, int len, int is_line, int is_yank) {
	if (name == '_') return;

	int append = (name >= 'A' && name <= 'Z');
	int idx = hkRegIndex(name);

	if (idx >= 0) {
		if (name == '+' || name == '*') {
			FILE *cmd = popen("xclip -selection clipboard 2>/dev/null || pbcopy 2>/dev/null", "w");
			if (cmd) { fwrite(data, 1, len, cmd); pclose(cmd); }
			hkRegAssign(&E.hk.registers[idx], data, len, is_line);
		} else if (append) {
			hkRegAppend(&E.hk.registers[idx], data, len, is_line);
		} else {
			hkRegAssign(&E.hk.registers[idx], data, len, is_line);
		}

		if (name != '"' && name != 0) {
			hkRegAssign(&E.hk.registers[26], data, len, is_line);
		}
	}

	editorSetPasteBuffer(data, len, is_line);

	if (is_yank) {
		hkRegAssign(&E.hk.registers[27], data, len, is_line);
	}
}

pasteBuffer *hkGetRegister(char name) {
	if (name == '+' || name == '*') {
		FILE *cmd = popen("xclip -selection clipboard -o 2>/dev/null || pbpaste 2>/dev/null", "r");
		if (cmd) {
			char buf[4096];
			size_t total = 0;
			char *accum = NULL;
			size_t got;
			while ((got = fread(buf, 1, sizeof(buf), cmd)) > 0) {
				accum = realloc(accum, total + got + 1);
				memcpy(accum + total, buf, got);
				total += got;
			}
			pclose(cmd);
			if (accum) {
				accum[total] = '\0';
				int is_line = (total > 0 && accum[total - 1] == '\n');
				int idx = hkRegIndex(name);
				hkRegAssign(&E.hk.registers[idx], accum, total, is_line);
				free(accum);
				return &E.hk.registers[idx];
			}
		}
	}
	int idx = hkRegIndex(name);
	if (idx < 0) return &E.paste;
	if (E.hk.registers[idx].data == NULL && (name == 0 || name == '"')) return &E.paste;
	return &E.hk.registers[idx];
}

void hkFreeRegisters(void) {
	for (int i = 0; i < 32; i++) {
		free(E.hk.registers[i].data);
		E.hk.registers[i].data = NULL;
		E.hk.registers[i].len = 0;
	}
}

void editorCopyToSystemClipboard() {
	if (E.mode == MODE_VISUAL || E.mode == MODE_VISUAL_LINE) {
		editorYankVisualSelection();
	}
	
	if (!E.paste.data || E.paste.len == 0) return;
	
	FILE *cmd = popen("xclip -selection clipboard 2>/dev/null || pbcopy 2>/dev/null", "w");
	if (cmd) {
		fwrite(E.paste.data, 1, E.paste.len, cmd);
		pclose(cmd);
		editorSetStatusMessage("Copied to system clipboard");
	}
}

void editorPasteFromSystemClipboard() {
	FILE *cmd = popen("xclip -selection clipboard -o 2>/dev/null || pbpaste 2>/dev/null", "r");
	if (!cmd) return;
	
	char buffer[4096];
	size_t total = 0;
	
	free(E.system_paste.data);
	E.system_paste.data = NULL;
	E.system_paste.len = 0;
	
	editorSaveState();
	E.is_pasting = 1;
	
	int saved_auto_indent = E.auto_indent;
	int saved_smart_indent = E.smart_indent;
	E.auto_indent = 0;
	E.smart_indent = 0;
	
	while (fgets(buffer, sizeof(buffer), cmd)) {
		int len = strlen(buffer);
		E.system_paste.data = realloc(E.system_paste.data, total + len + 1);
		memcpy(E.system_paste.data + total, buffer, len);
		total += len;
	}
	
	pclose(cmd);
	
	if (total > 0) {
		E.system_paste.len = total;
		E.system_paste.data[total] = '\0';
		
		for (int i = 0; i < total; i++) {
			char c = E.system_paste.data[i];
			if (c == '\n' || c == '\r') {
				if (c == '\r' && i + 1 < total && E.system_paste.data[i + 1] == '\n') {
					i++;
				}
				editorInsertNewLine();
			} else if (c != '\0') {
				editorInsertChar(c);
			}
		}
		
		editorSetStatusMessage("Pasted %d bytes from system clipboard", total);
	}
	
	E.auto_indent = saved_auto_indent;
	E.smart_indent = saved_smart_indent;
	E.is_pasting = 0;
}

/*** pane management ***/
editorPane *editorCreatePane(enum paneType type, int x, int y, int width, int height) {
	editorPane *pane = calloc(1, sizeof(editorPane));
	if (!pane) return NULL;

	static int pane_id_counter = 0;
	pane->id = pane_id_counter++;
	pane->type = type;
	pane->x = x;
	pane->y = y;
	pane->width = width;
	pane->height = height;
	pane->cx = 0;
	pane->cy = 0;
	pane->rx = 0;
	pane->rowoff = 0;
	pane->coloff = 0;
	pane->numrows = 0;
	pane->row = NULL;
	pane->filename = NULL;
	pane->dirty = 0;
	pane->syntax = NULL;
	pane->visual_anchor_x = 0;
	pane->visual_anchor_y = 0;
	pane->undo_stack = NULL;
	pane->redo_stack = NULL;
	pane->undo_stack_size = 0;
	pane->redo_stack_size = 0;
	pane->explorer = NULL;
	pane->plugin = NULL;
	pane->ai = NULL;
	pane->pane_yank_buffer = NULL;
	pane->pane_yank_buffer_len = 0;
	pane->parent = NULL;
	pane->child1 = NULL;
	pane->child2 = NULL;
	pane->split_dir = SPLIT_NONE;
	pane->split_ratio = 0.5;
	pane->is_focused = 0;
	pane->wrap_lines = E.word_wrap;

	return pane;
}

void editorFreePane(editorPane *pane) {
	if (!pane) return;

	if (pane->split_dir != SPLIT_NONE) {
		editorFreePane(pane->child1);
		editorFreePane(pane->child2);
		free(pane);
		return;
	}

	if (pane->type == PANE_EDITOR) {
		for (int i = 0; i < pane->numrows; i++) {
			editorFreeRow(&pane->row[i]);
		}
		free(pane->row);
		free(pane->filename);
		free(pane->pane_yank_buffer);

		while (pane->undo_stack) {
			undoState *temp = pane->undo_stack;
			pane->undo_stack = pane->undo_stack->next;
			editorFreeUndoState(temp);
		}

		while (pane->redo_stack) {
			undoState *temp = pane->redo_stack;
			pane->redo_stack = pane->redo_stack->next;
			editorFreeUndoState(temp);
		}
	}

	if (pane->type == PANE_EXPLORER) {
		explorerCleanup(pane);
	}

	if (pane->type == PANE_AI && pane->ai) {
		pthread_mutex_destroy(&pane->ai->lock);
		for (int i = 0; i < pane->ai->history_count; i++) {
			free(pane->ai->history[i]);
		}
		free(pane->ai->history);
		free(pane->ai->history_role);
		free(pane->ai->current_prompt);
		free(pane->ai->current_response);
		free(pane->ai->prompt_buffer);
		free(pane->ai->system_prompt);
		for (int i = 0; i < pane->ai->message_count; i++) {
			free(pane->ai->messages[i].role);
			free(pane->ai->messages[i].content);
		}
		free(pane->ai->messages);
		free(pane->ai);
	}

	free(pane);
}

editorPane *editorGetActivePane() {
	return E.active_pane;
}

editorPane *editorFindPaneById(editorPane *root, int id) {
	if (!root) return NULL;
	if (root->id == id) return root;
	
	if (root->split_dir != SPLIT_NONE) {
		editorPane *result = editorFindPaneById(root->child1, id);
		if (result) return result;
		return editorFindPaneById(root->child2, id);
	}
	
	return NULL;
}

void editorUpdatePaneBounds(editorPane *pane) {
	if (!pane || pane->split_dir == SPLIT_NONE) return;
	
	if (pane->split_dir == SPLIT_VERTICAL) {
		int split_pos = pane->x + (int)(pane->width * pane->split_ratio);
		
		pane->child1->x = pane->x;
		pane->child1->y = pane->y;
		pane->child1->width = split_pos - pane->x - 1;
		pane->child1->height = pane->height;
		
		pane->child2->x = split_pos;
		pane->child2->y = pane->y;
		pane->child2->width = pane->x + pane->width - split_pos;
		pane->child2->height = pane->height;
	} else {
		int split_pos = pane->y + (int)(pane->height * pane->split_ratio);
		
		pane->child1->x = pane->x;
		pane->child1->y = pane->y;
		pane->child1->width = pane->width;
		pane->child1->height = split_pos - pane->y - 1;
		
		pane->child2->x = pane->x;
		pane->child2->y = split_pos;
		pane->child2->width = pane->width;
		pane->child2->height = pane->y + pane->height - split_pos;
	}
	
	editorUpdatePaneBounds(pane->child1);
	editorUpdatePaneBounds(pane->child2);
}

void editorCollectLeafPanes(editorPane *root, editorPane ***panes, int *count) {
	if (!root) return;
	
	if (root->split_dir == SPLIT_NONE) {
		*panes = realloc(*panes, sizeof(editorPane*) * (*count + 1));
		(*panes)[*count] = root;
		(*count)++;
	} else {
		editorCollectLeafPanes(root->child1, panes, count);
		editorCollectLeafPanes(root->child2, panes, count);
	}
}

void editorSplitPane(int vertical) {
	editorPane *current = E.active_pane;
	if (!current) return;
	if (current->type != PANE_EDITOR) {
		editorSetStatusMessage("Cannot split side panel");
		return;
	}
	
	if (E.num_panes >= MAX_PANES) {
		editorSetStatusMessage("Maximum number of panes reached");
		return;
	}
	
	int min_size = vertical ? 20 : 6;
	if ((vertical && current->width < min_size * 2) || 
		(!vertical && current->height < min_size * 2)) {
		editorSetStatusMessage("Pane too small to split");
		return;
	}
	
	editorPane *new_pane = editorCreatePane(PANE_EDITOR, 0, 0, 0, 0);
	if (!new_pane) return;
	
	editorPane *container = editorCreatePane(PANE_EDITOR, current->x, current->y, 
											  current->width, current->height);
	container->split_dir = vertical ? SPLIT_VERTICAL : SPLIT_HORIZONTAL;
	container->child1 = current;
	container->child2 = new_pane;
	container->split_ratio = 0.5;
	
	if (current->parent) {
		if (current->parent->child1 == current) {
			current->parent->child1 = container;
		} else {
			current->parent->child2 = container;
		}
		container->parent = current->parent;
	} else {
		E.root_pane = container;
	}
	
	current->parent = container;
	new_pane->parent = container;
	
	editorUpdatePaneBounds(container);
	
	if (current->filename) {
		new_pane->filename = strdup(current->filename);
		new_pane->syntax = current->syntax;
		
		for (int i = 0; i < current->numrows; i++) {
			editorPane *temp = E.active_pane;
			E.active_pane = new_pane;
			editorInsertRow(new_pane->numrows, current->row[i].chars, current->row[i].size);
			E.active_pane = temp;
		}
		new_pane->dirty = 0;
	}
	
	E.active_pane = new_pane;
	E.num_panes++;
	
	editorSetStatusMessage("Split pane created");
}

void editorClosePane() {
	editorPane *pane = E.active_pane;
	if (!pane) return;

	if (!E.force_window_command && pane->type == PANE_EDITOR && pane->dirty) {
		editorSetStatusMessage("Pane has unsaved changes. Use Ctrl-W ! to force close");
		return;
	}

	if (pane == E.right_panel) {
		editorToggleAI();
		E.force_window_command = 0;
		return;
	}
	if (pane == E.left_panel) {
		editorToggleExplorer();
		E.force_window_command = 0;
		return;
	}

	int editor_leaves = 0;
	editorPane **leaves = NULL;
	editorCollectLeafPanes(E.root_pane, &leaves, &editor_leaves);
	free(leaves);

	if (pane->type == PANE_EDITOR && editor_leaves <= 1) {
		if (E.right_panel || E.left_panel) {
			editorSetStatusMessage("Cannot close last workspace while side pane open");
			return;
		}
		editorSetStatusMessage("Cannot close the last pane. Use :q to quit");
		return;
	}

	if (E.num_panes <= 1) {
		editorSetStatusMessage("Cannot close the last pane. Use :q to quit");
		return;
	}

	if (!pane->parent) {
		editorSetStatusMessage("Cannot close root pane");
		return;
	}
	
	editorPane *parent = pane->parent;
	editorPane *sibling = (parent->child1 == pane) ? parent->child2 : parent->child1;
	
	sibling->x = parent->x;
	sibling->y = parent->y;
	sibling->width = parent->width;
	sibling->height = parent->height;
	
	if (parent->parent) {
		if (parent->parent->child1 == parent) {
			parent->parent->child1 = sibling;
		} else {
			parent->parent->child2 = sibling;
		}
		sibling->parent = parent->parent;
	} else {
		E.root_pane = sibling;
		sibling->parent = NULL;
	}
	
	editorUpdatePaneBounds(sibling->parent ? sibling->parent : sibling);
	
	parent->child1 = NULL;
	parent->child2 = NULL;
	parent->split_dir = SPLIT_NONE;
	editorFreePane(pane);
	free(parent);
	
	E.active_pane = sibling;
	E.num_panes--;
	E.force_window_command = 0;
	
	editorSetStatusMessage("Pane closed");
}

void editorNextPane() {
	editorPane **all_panes = malloc(sizeof(editorPane*) * (MAX_PANES + 2));
	int total = 0;
	
	if (E.left_panel) {
		all_panes[total++] = E.left_panel;
	}
	
	editorPane **editor_panes = NULL;
	int editor_count = 0;
	editorCollectLeafPanes(E.root_pane, &editor_panes, &editor_count);
	
	for (int i = 0; i < editor_count; i++) {
		all_panes[total++] = editor_panes[i];
	}
	free(editor_panes);
	
	if (E.right_panel) {
		all_panes[total++] = E.right_panel;
	}
	
	if (total <= 1) {
		free(all_panes);
		return;
	}
	
	int current_idx = -1;
	for (int i = 0; i < total; i++) {
		if (all_panes[i] == E.active_pane) {
			current_idx = i;
			break;
		}
	}
	
	if (current_idx >= 0) {
		E.active_pane->is_focused = 0;
		E.active_pane = all_panes[(current_idx + 1) % total];
		E.active_pane->is_focused = 1;
	}
	
	free(all_panes);
}

void editorResizePanes() {
	if (!E.root_pane) return;

	int left_w = E.left_panel ? E.left_panel_width : 0;
	int right_w = E.right_panel ? E.right_panel_width : 0;
	int min_editor = 24;
	int stack_vertical = 0;

	/* AI too wide for side-by-side: stack below if rows allow */
	if (E.right_panel && E.screencols < min_editor + left_w + right_w && E.screenrows >= 16) {
		stack_vertical = 1;
		right_w = 0;
	}

	if (E.screencols < min_editor + left_w + right_w) {
		if (right_w > 0 && E.screencols < min_editor + right_w) right_w = 0;
		if (left_w > 0 && E.screencols < min_editor + left_w + right_w) left_w = 0;
	}

	int left_border = (E.left_panel && left_w > 0) ? 1 : 0;
	int ai_h = 0;
	if (stack_vertical) {
		ai_h = E.screenrows / 2;
		if (ai_h < 6) ai_h = 6;
		if (ai_h > E.screenrows - 6) ai_h = E.screenrows - 6;
	}

	E.root_pane->x = left_w + left_border;
	E.root_pane->y = 0;
	E.root_pane->width = E.screencols - left_w - left_border - right_w;
	if (E.root_pane->width < 1) E.root_pane->width = 1;
	E.root_pane->height = E.screenrows - ai_h;

	if (E.left_panel) {
		E.left_panel->x = 0;
		E.left_panel->y = 0;
		E.left_panel->width = left_w;
		E.left_panel->height = E.screenrows - ai_h;
	}

	if (E.right_panel) {
		if (stack_vertical) {
			E.right_panel->x = 0;
			E.right_panel->y = E.screenrows - ai_h;
			E.right_panel->width = E.screencols;
			E.right_panel->height = ai_h;
		} else {
			E.right_panel->x = E.screencols - right_w;
			E.right_panel->y = 0;
			E.right_panel->width = right_w;
			E.right_panel->height = E.screenrows;
		}
	}

	editorUpdatePaneBounds(E.root_pane);
}

/*** row operations ***/
int editorRowCxToRx(erow *row, int cx) {
	int rx = 0;
	int bytes = 0;
	
	while (bytes < cx && bytes < row->size) {
		if (row->chars[bytes] == '\t') {
			rx += (E.tab_stop - 1) - (rx % E.tab_stop);
			rx++;
			bytes++;
		} else {
			int width = utf8_char_width(row->chars, bytes);
			rx += width;
			bytes = utf8_next_char(row->chars, bytes, row->size);
		}
	}
	return rx;
}

int editorRowRxToCx(erow *row, int rx) {
	int cur_rx = 0;
	int cx = 0;
	
	while (cx < row->size) {
		if (row->chars[cx] == '\t') {
			cur_rx += (E.tab_stop - 1) - (cur_rx % E.tab_stop);
			cur_rx++;
			cx++;
		} else {
			int width = utf8_char_width(row->chars, cx);
			cur_rx += width;
			if (cur_rx > rx) return cx;
			cx = utf8_next_char(row->chars, cx, row->size);
		}
		if (cur_rx > rx) return cx;
	}
	return cx;
}

void editorUpdateSyntax(erow *row) {
	row->hl = realloc(row->hl, row->rsize);
	memset(row->hl, HL_NORMAL, row->rsize);

	editorPane *pane = E.active_pane;
	if (!pane || pane->type != PANE_EDITOR || !pane->syntax) return;

	char **keywords = pane->syntax->keywords;
	char *scs = pane->syntax->singleline_comment_start;
	char *mcs = pane->syntax->multiline_comment_start;
	char *mce = pane->syntax->multiline_comment_end;

	int scs_len = scs ? strlen(scs) : 0;
	int mcs_len = mcs ? strlen(mcs) : 0;
	int mce_len = mce ? strlen(mce) : 0;

	int prev_sep = 1;
	int in_string = 0;
	int in_comment = (row->idx > 0 && pane->row[row->idx - 1].hl_open_comment);

	int i = 0;
	while (i < row->rsize) {
		char c = row->render[i];
		unsigned char prev_hl = (i > 0) ? row->hl[i - 1] : HL_NORMAL;

		if (row->hl[i] == HL_MATCH) {
			i++;
			prev_sep = 0;
			continue;
		}

		if (scs_len && !in_string && !in_comment) {
			if (!strncmp(&row->render[i], scs, scs_len)) {
				memset(&row->hl[i], HL_COMMENT, row->rsize - i);
				break;
			}
		}

		if (mcs_len && mce_len && !in_string) {
			if (in_comment) {
				row->hl[i] = HL_MLCOMMENT;
				if (!strncmp(&row->render[i], mce, mce_len)) {
					memset(&row->hl[i], HL_MLCOMMENT, mce_len);
					i += mce_len;
					in_comment = 0;
					prev_sep = 1;
					continue;
				} else {
					i++;
					continue;
				}
			} else if (!strncmp(&row->render[i], mcs, mcs_len)) {
				memset(&row->hl[i], HL_MLCOMMENT, mcs_len);
				i += mcs_len;
				in_comment = 1;
				continue;
			}
		}

		if (pane->syntax->flags & HL_HIGHLIGHT_STRINGS) {
			if (in_string) {
				row->hl[i] = HL_STRING;
				if (c == '\\' && i + 1 < row->rsize) {
					row->hl[i + 1] = HL_ESCAPE;
					i += 2;
					continue;
				}
				if (c == in_string) in_string = 0;
				i++;
				prev_sep = 1;
				continue;
			} else {
				if (c == '"' || c == '\'') {
					in_string = c;
					row->hl[i] = HL_STRING;
					i++;
					continue;
				}
			}
		}

		if (pane->syntax->flags & HL_HIGHLIGHT_NUMBERS) {
			if ((isdigit(c) && (prev_sep || prev_hl == HL_NUMBER)) ||
				(c == '.' && prev_hl == HL_NUMBER)) {
				row->hl[i] = HL_NUMBER;
				i++;
				prev_sep = 0;
				continue;
			}
		}

		if (row->render[0] == '#' && pane->syntax == &HLDB[0]) {
			int j = 0;
			while (j < row->rsize && row->render[j] != ' ' && row->render[j] != '\t') {
				row->hl[j] = HL_PREPROCESSOR;
				j++;
			}
		}

		if (strchr("+-*/%=<>!&|^~?:,;(){}[]", c)) {
			row->hl[i] = (strchr("(){}[]", c)) ? HL_BRACKET : HL_OPERATOR;
			i++;
			prev_sep = 1;
			continue;
		}

		if (prev_sep && keywords) {
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

	int changed = (row->hl_open_comment != in_comment);
	row->hl_open_comment = in_comment;
	if (changed && row->idx + 1 < pane->numrows)
		editorUpdateSyntax(&pane->row[row->idx + 1]);

	if (E.search.query && E.search.query[0]) {
		int qlen = strlen(E.search.query);
		if (qlen > 0 && qlen <= row->rsize) {
			char *m = strstr(row->render, E.search.query);
			while (m) {
				int pos = m - row->render;
				for (int j = 0; j < qlen && pos + j < row->rsize; j++) {
					row->hl[pos + j] = HL_MATCH;
				}
				m = strstr(m + 1, E.search.query);
			}
		}
	}
}

void editorUpdateRow(erow *row) {
	int tabs = 0;
	for (int j = 0; j < row->size; j++)
		if (row->chars[j] == '\t') tabs++;

	free(row->render);
	row->render = malloc(row->size + tabs * (E.tab_stop - 1) + 1);

	int idx = 0;
	int j = 0;
	while (j < row->size) {
		if (row->chars[j] == '\t') {
			row->render[idx++] = ' ';
			while (idx % E.tab_stop != 0) row->render[idx++] = ' ';
			j++;
		} else {
			int len = utf8_byte_length((unsigned char)row->chars[j]);
			if (j + len > row->size) len = row->size - j;
			for (int k = 0; k < len; k++) {
				row->render[idx++] = row->chars[j + k];
			}
			j += len;
		}
	}
	row->render[idx] = '\0';
	row->rsize = idx;

	row->indent = 0;
	int i = 0;
	while (i < row->size) {
		if (row->chars[i] == ' ') {
			row->indent++;
			i++;
		} else if (row->chars[i] == '\t') {
			row->indent += E.tab_stop;
			i++;
		} else {
			break;
		}
	}

	editorUpdateSyntax(row);
}

void editorInsertRow(int at, char *s, size_t len) {
	editorPane *pane = E.active_pane;
	if (!pane || pane->type != PANE_EDITOR) return;

	if (at < 0 || at > pane->numrows) return;

	pane->row = realloc(pane->row, sizeof(erow) * (pane->numrows + 1));
	memmove(&pane->row[at + 1], &pane->row[at], sizeof(erow) * (pane->numrows - at));
	for (int j = at + 1; j <= pane->numrows; j++) pane->row[j].idx++;

	pane->row[at].idx = at;
	pane->row[at].size = len;
	pane->row[at].chars = malloc(len + 1);
	memcpy(pane->row[at].chars, s, len);
	pane->row[at].chars[len] = '\0';

	pane->row[at].rsize = 0;
	pane->row[at].render = NULL;
	pane->row[at].hl = NULL;
	pane->row[at].hl_open_comment = 0;
	pane->row[at].indent = 0;
	editorUpdateRow(&pane->row[at]);
	pane->numrows++;

	if (pane->numrows > 1 || len > 0) {
		pane->dirty++;
	}
}

void editorFreeRow(erow *row) {
	free(row->render);
	free(row->chars);
	free(row->hl);
}

void editorDelRow(int at) {
	editorPane *pane = E.active_pane;
	if (!pane || pane->type != PANE_EDITOR) return;

	if (at < 0 || at >= pane->numrows) return;
	editorFreeRow(&pane->row[at]);
	memmove(&pane->row[at], &pane->row[at + 1], sizeof(erow) * (pane->numrows - at - 1));
	for (int j = at; j < pane->numrows - 1; j++) pane->row[j].idx--;
	pane->numrows--;
	pane->dirty++;
}

void editorRowInsertChar(erow *row, int at, int c) {
	if (at < 0 || at > row->size) at = row->size;
	row->chars = realloc(row->chars, row->size + 2);
	memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);
	row->size++;
	row->chars[at] = c;
	editorUpdateRow(row);

	editorPane *pane = E.active_pane;
	if (pane && pane->type == PANE_EDITOR) {
		pane->dirty++;
	}
}

void editorRowAppendString(erow *row, char *s, size_t len) {
	row->chars = realloc(row->chars, row->size + len + 1);
	memcpy(&row->chars[row->size], s, len);
	row->size += len;
	row->chars[row->size] = '\0';
	editorUpdateRow(row);

	editorPane *pane = E.active_pane;
	if (pane && pane->type == PANE_EDITOR) {
		pane->dirty++;
	}
}

void editorRowDelChar(erow *row, int at) {
	if (at < 0 || at >= row->size) return;
	memmove(&row->chars[at], &row->chars[at + 1], row->size - at);
	row->size--;
	editorUpdateRow(row);

	editorPane *pane = E.active_pane;
	if (pane && pane->type == PANE_EDITOR) {
		pane->dirty++;
	}
}

/*** editor operations ***/
void editorInsertChar(int c) {
	editorPane *pane = E.active_pane;
	if (!pane || pane->type != PANE_EDITOR) return;

	if (pane->cy == pane->numrows) {
		editorInsertRow(pane->numrows, "", 0);
	}
	editorRowInsertChar(&pane->row[pane->cy], pane->cx, c);
	pane->cx++;
}

void editorDelChar() {
	editorPane *pane = E.active_pane;
	if (!pane || pane->type != PANE_EDITOR) return;

	if (pane->cy == pane->numrows) return;
	if (pane->cx == 0 && pane->cy == 0) return;
	if (pane->numrows == 0) return;

	erow *row = &pane->row[pane->cy];
	if (pane->cx > 0) {
		int prev_pos = utf8_prev_char(row->chars, pane->cx);
		int char_len = pane->cx - prev_pos;
		
		memmove(&row->chars[prev_pos], &row->chars[pane->cx], row->size - pane->cx + 1);
		row->size -= char_len;
		pane->cx = prev_pos;
		editorUpdateRow(row);
		pane->dirty++;
	} else {
		pane->cx = pane->row[pane->cy - 1].size;
		editorRowAppendString(&pane->row[pane->cy - 1], row->chars, row->size);
		editorDelRow(pane->cy);
		pane->cy--;
	}
}

int editorGetIndent(erow *row) {
	return row ? row->indent : 0;
}

void editorInsertNewLine() {
	editorPane *pane = E.active_pane;
	if (!pane || pane->type != PANE_EDITOR) return;

	while (pane->cy >= pane->numrows) {
		editorInsertRow(pane->numrows, "", 0);
	}

	int indent = 0;
	if (E.auto_indent && E.mode == MODE_INSERT && pane->cy < pane->numrows && E.is_pasting == 0) {
		erow *row = &pane->row[pane->cy];
		
		if (pane->cx > 0 || (pane->cx == 0 && row->size > 0 && isspace(row->chars[0]))) {
			indent = editorGetIndent(row);
			
			if (E.smart_indent && row->size > 0 && E.is_pasting == 0) {
				int i = row->size - 1;
				while (i >= 0 && isspace(row->chars[i])) i--;
				
				if (i >= 0) {
					char end_char = row->chars[i];
					if (strchr("{:[(", end_char)) {
						indent += E.tab_stop;
					} else if (i > 0 && row->chars[i-1] == ':' && end_char != ':') {
						indent += E.tab_stop;
					}
				}
			}
		}
	}

	if (pane->cx == 0) {
		editorInsertRow(pane->cy, "", 0);
	} else {
		erow *row = &pane->row[pane->cy];
		editorInsertRow(pane->cy + 1, &row->chars[pane->cx], row->size - pane->cx);
		row = &pane->row[pane->cy];
		row->size = pane->cx;
		row->chars[row->size] = '\0';
		editorUpdateRow(row);
	}

	pane->cy++;
	pane->cx = 0;

	if (indent > 0 && E.is_pasting == 0) {
		if (E.use_tabs) {
			int num_tabs = indent / E.tab_stop;
			int num_spaces = indent % E.tab_stop;
			for (int i = 0; i < num_tabs; i++) {
				editorInsertChar('\t');
			}
			for (int i = 0; i < num_spaces; i++) {
				editorInsertChar(' ');
			}
		} else {
			for (int i = 0; i < indent; i++) {
				editorInsertChar(' ');
			}
		}
	}
}

void editorHandlePaste(const char *text, int len) {
	editorPane *pane = E.active_pane;
	if (!pane || pane->type != PANE_EDITOR || !text || len <= 0) return;
	
	editorSaveState();
	E.is_pasting = 1;
	
	for (int i = 0; i < len; i++) {
		if (text[i] == '\n' || text[i] == '\r') {
			if (text[i] == '\r' && i + 1 < len && text[i + 1] == '\n') {
				i++;
			}
			editorInsertNewLine();
		} else if (text[i] == '\t') {
			editorInsertChar('\t');
		} else if (text[i] >= 32 || text[i] < 0) {
			editorInsertChar(text[i]);
		}
	}
	
	E.is_pasting = 0;
	editorSetStatusMessage("Pasted %d bytes", len);
}

/*** cursor movement ***/
void editorMoveCursor(int key) {
	editorPane *pane = E.active_pane;
	if (!pane || pane->type != PANE_EDITOR) return;

	erow *row = (pane->cy >= pane->numrows) ? NULL : &pane->row[pane->cy];

	switch (key) {
	case ARROW_LEFT:
	case 'h':
		if (pane->cx != 0 && row) {
			pane->cx = utf8_prev_char(row->chars, pane->cx);
		} else if (pane->cy > 0) {
			pane->cy--;
			if (pane->cy < pane->numrows) {
				pane->cx = pane->row[pane->cy].size;
			}
		}
		break;

	case ARROW_RIGHT:
	case 'l':
		if (row && pane->cx < row->size) {
			pane->cx = utf8_next_char(row->chars, pane->cx, row->size);
		} else if (row && pane->cx == row->size && pane->cy < pane->numrows - 1) {
			pane->cy++;
			pane->cx = 0;
		}
		break;

	case ARROW_UP:
	case 'k':
		if (E.word_wrap && pane->wrap_lines && row) {
			int wrap_width = pane->width - E.line_number_width;
			if (wrap_width < 1) wrap_width = 1;
			
			int rx = editorRowCxToRx(row, pane->cx);
			int current_wrap_line = rx / wrap_width;
			
			if (current_wrap_line > 0) {
				int target_rx = rx - wrap_width;
				if (target_rx < 0) target_rx = 0;
				pane->cx = editorRowRxToCx(row, target_rx);
				break;
			}
		}
		
		if (pane->cy > 0) {
			pane->cy--;
			if (E.word_wrap && pane->wrap_lines && pane->cy < pane->numrows) {
				int wrap_width = pane->width - E.line_number_width;
				if (wrap_width < 1) wrap_width = 1;
				
				erow *prev_row = &pane->row[pane->cy];
				int prev_lines = (prev_row->rsize + wrap_width - 1) / wrap_width;
				if (prev_lines > 1) {
					int target_rx = (prev_lines - 1) * wrap_width + (pane->rx % wrap_width);
					if (target_rx > prev_row->rsize) target_rx = prev_row->rsize;
					pane->cx = editorRowRxToCx(prev_row, target_rx);
				}
			}
		}
		break;

	case ARROW_DOWN:
	case 'j':
		if (E.word_wrap && pane->wrap_lines && row) {
			int wrap_width = pane->width - E.line_number_width;
			if (wrap_width < 1) wrap_width = 1;
			
			int rx = editorRowCxToRx(row, pane->cx);
			int current_wrap_line = rx / wrap_width;
			int total_wrap_lines = (row->rsize + wrap_width - 1) / wrap_width;
			
			if (current_wrap_line < total_wrap_lines - 1) {
				int target_rx = rx + wrap_width;
				if (target_rx > row->rsize) target_rx = row->rsize;
				pane->cx = editorRowRxToCx(row, target_rx);
				break;
			}
		}
		
		if (pane->numrows > 0 && pane->cy < pane->numrows - 1) {
			pane->cy++;
			if (E.word_wrap && pane->wrap_lines && pane->cy < pane->numrows) {
				int wrap_width = pane->width - E.line_number_width;
				if (wrap_width < 1) wrap_width = 1;
				
				int target_rx = pane->rx % wrap_width;
				erow *next_row = &pane->row[pane->cy];
				if (target_rx > next_row->rsize) target_rx = next_row->rsize;
				pane->cx = editorRowRxToCx(next_row, target_rx);
			}
		}
		break;
	}

	row = (pane->cy >= pane->numrows) ? NULL : &pane->row[pane->cy];
	int rowlen = row ? row->size : 0;
	if (pane->cx > rowlen) {
		pane->cx = rowlen;
	}
}

/*** scrolling ***/
void editorScroll() {
	editorPane *pane = E.active_pane;
	if (!pane || pane->type != PANE_EDITOR) return;

	if (pane->cy >= pane->numrows) {
		pane->cy = pane->numrows > 0 ? pane->numrows - 1 : 0;
	}

	pane->rx = 0;
	if (pane->cy < pane->numrows) {
		pane->rx = editorRowCxToRx(&pane->row[pane->cy], pane->cx);
	}

	if (E.word_wrap && pane->wrap_lines) {
		int wrap_width = pane->width - E.line_number_width;
		if (wrap_width < 1) wrap_width = 1;
		
		int visual_row = 0;
		for (int i = 0; i < pane->cy; i++) {
			int lines = (pane->row[i].rsize + wrap_width - 1) / wrap_width;
			if (lines < 1) lines = 1;
			visual_row += lines;
		}
		
		if (pane->cy < pane->numrows) {
			visual_row += pane->rx / wrap_width;
		}
		
		if (visual_row < pane->rowoff) {
			pane->rowoff = visual_row;
		}
		if (visual_row >= pane->rowoff + pane->height) {
			pane->rowoff = visual_row - pane->height + 1;
		}
		
		pane->coloff = 0;
	} else {
		if (pane->cy < pane->rowoff) {
			pane->rowoff = pane->cy;
		}
		if (pane->cy >= pane->rowoff + pane->height) {
			pane->rowoff = pane->cy - pane->height + 1;
		}

		if (pane->rx < pane->coloff) {
			pane->coloff = pane->rx;
		}
		if (pane->rx >= pane->coloff + pane->width - E.line_number_width) {
			pane->coloff = pane->rx - pane->width + E.line_number_width + 1;
		}
	}
}

/*** visual mode ***/
int editorIsInVisualSelection(int row, int col) {
	editorPane *pane = E.active_pane;
	if (!pane || pane->type != PANE_EDITOR) return 0;

	if (E.mode != MODE_VISUAL && E.mode != MODE_VISUAL_LINE) return 0;

	if (E.mode == MODE_VISUAL_LINE) {
		int start_y = pane->visual_anchor_y;
		int end_y = pane->cy;

		if (start_y > end_y) {
			int tmp = start_y;
			start_y = end_y;
			end_y = tmp;
		}

		return (row >= start_y && row <= end_y);
	}

	int start_y = pane->visual_anchor_y;
	int start_x = pane->visual_anchor_x;
	int end_y = pane->cy;
	int end_x = pane->cx;

	if (start_y > end_y || (start_y == end_y && start_x > end_x)) {
		int tmp;
		tmp = start_y; start_y = end_y; end_y = tmp;
		tmp = start_x; start_x = end_x; end_x = tmp;
	}

	if (row >= 0 && row < pane->numrows) {
		int char_col = 0;
		int render_col = 0;
		erow *r = &pane->row[row];

		for (char_col = 0; char_col < r->size && render_col < col; char_col++) {
			if (r->chars[char_col] == '\t') {
				render_col += (E.tab_stop - 1) - (render_col % E.tab_stop);
			}
			render_col++;
		}
		col = char_col;
	}

	if (row < start_y || row > end_y) return 0;
	if (row == start_y && col < start_x) return 0;
	if (row == end_y && col > end_x) return 0;

	return 1;
}

void editorGetVisualSelection(int *start_x, int *start_y, int *end_x, int *end_y) {
	editorPane *pane = E.active_pane;
	if (!pane || pane->type != PANE_EDITOR) return;

	if (E.mode == MODE_VISUAL_LINE) {
		*start_x = 0;
		*start_y = pane->visual_anchor_y;
		*end_x = (pane->cy < pane->numrows) ? pane->row[pane->cy].size - 1 : 0;
		if (*end_x < 0) *end_x = 0;
		*end_y = pane->cy;
	} else {
		*start_x = pane->visual_anchor_x;
		*start_y = pane->visual_anchor_y;
		*end_x = pane->cx;
		*end_y = pane->cy;
	}

	if (*start_y > *end_y || (*start_y == *end_y && *start_x > *end_x)) {
		int tmp;
		tmp = *start_x; *start_x = *end_x; *end_x = tmp;
		tmp = *start_y; *start_y = *end_y; *end_y = tmp;
	}

	if (*end_y < pane->numrows && *end_x >= pane->row[*end_y].size) {
		*end_x = pane->row[*end_y].size - 1;
		if (*end_x < 0) *end_x = 0;
	}
}

void editorYankVisualSelection() {
	editorPane *pane = E.active_pane;
	if (!pane || pane->type != PANE_EDITOR) return;

	int start_x, start_y, end_x, end_y;
	editorGetVisualSelection(&start_x, &start_y, &end_x, &end_y);

	int total_len = 0;
	int is_line_mode = (E.mode == MODE_VISUAL_LINE);
	
	for (int y = start_y; y <= end_y; y++) {
		if (y >= pane->numrows) break;

		int row_start = (y == start_y && !is_line_mode) ? start_x : 0;
		int row_end = (y == end_y && !is_line_mode) ? end_x : pane->row[y].size - 1;

		if (row_start <= row_end) {
			total_len += row_end - row_start + 1;
			if (y < end_y || is_line_mode) total_len++;
		}
	}

	if (total_len == 0) return;

	char *buffer = malloc(total_len + 1);
	char *p = buffer;
	
	for (int y = start_y; y <= end_y; y++) {
		if (y >= pane->numrows) break;

		int row_start = (y == start_y && !is_line_mode) ? start_x : 0;
		int row_end = (y == end_y && !is_line_mode) ? end_x : pane->row[y].size - 1;

		if (row_start <= row_end && row_start < pane->row[y].size) {
			int len = row_end - row_start + 1;
			if (row_end >= pane->row[y].size) len = pane->row[y].size - row_start;

			memcpy(p, &pane->row[y].chars[row_start], len);
			p += len;

			if (y < end_y || is_line_mode) {
				*p++ = '\n';
			}
		}
	}
	*p = '\0';

	hkSetRegister(E.hk.pending_reg, buffer, p - buffer, is_line_mode, 1);
	E.hk.pending_reg = 0;
	free(buffer);

	int lines = end_y - start_y + 1;
	if (lines > 1) {
		editorSetStatusMessage("%d lines yanked", lines);
	} else {
		editorSetStatusMessage("Yanked %d characters", total_len);
	}
}

void editorDeleteVisualSelection() {
	editorPane *pane = E.active_pane;
	if (!pane || pane->type != PANE_EDITOR) return;

	int start_x, start_y, end_x, end_y;
	editorGetVisualSelection(&start_x, &start_y, &end_x, &end_y);

	editorYankVisualSelection();

	if (start_y == end_y) {
		for (int i = end_x; i >= start_x; i--) {
			editorRowDelChar(&pane->row[start_y], i);
		}
	} else {
		if (start_x < pane->row[start_y].size) {
			pane->row[start_y].size = start_x;
			pane->row[start_y].chars[start_x] = '\0';

			if (end_x + 1 < pane->row[end_y].size) {
				editorRowAppendString(&pane->row[start_y],
					&pane->row[end_y].chars[end_x + 1],
					pane->row[end_y].size - end_x - 1);
			}
			editorUpdateRow(&pane->row[start_y]);
		}

		for (int i = 0; i < end_y - start_y; i++) {
			editorDelRow(start_y + 1);
		}
	}

	pane->cy = start_y;
	pane->cx = start_x;

	int deleted_lines = end_y - start_y + 1;
	if (E.mode == MODE_VISUAL_LINE) {
		editorSetStatusMessage("%d line%s deleted", deleted_lines, deleted_lines == 1 ? "" : "s");
	} else if (start_y == end_y) {
		int chars = end_x - start_x + 1;
		editorSetStatusMessage("%d char%s deleted", chars, chars == 1 ? "" : "s");
	} else {
		editorSetStatusMessage("%d line%s deleted", deleted_lines, deleted_lines == 1 ? "" : "s");
	}
}

/*** undo/redo ***/
undoState *editorCreateUndoState() {
	editorPane *pane = E.active_pane;
	if (!pane || pane->type != PANE_EDITOR) return NULL;

	undoState *state = calloc(1, sizeof(undoState));
	if (!state) return NULL;

	state->numrows = pane->numrows;
	state->cx = pane->cx;
	state->cy = pane->cy;
	state->filename = pane->filename ? strdup(pane->filename) : NULL;
	state->timestamp = time(NULL);
	state->mode = E.mode;

	if (E.paste.data && E.paste.len > 0) {
		state->yank_buffer = malloc(E.paste.len + 1);
		if (state->yank_buffer) {
			memcpy(state->yank_buffer, E.paste.data, E.paste.len + 1);
			state->yank_buffer_len = E.paste.len;
		}
	}

	state->next = NULL;

	if (pane->numrows > 0) {
		state->rows = calloc(pane->numrows, sizeof(erow));
		if (!state->rows) {
			free(state->filename);
			free(state->yank_buffer);
			free(state);
			return NULL;
		}

		for (int i = 0; i < pane->numrows; i++) {
			state->rows[i].size = pane->row[i].size;
			state->rows[i].chars = malloc(pane->row[i].size + 1);
			if (!state->rows[i].chars) {
				for (int j = 0; j < i; j++) {
					free(state->rows[j].chars);
				}
				free(state->rows);
				free(state->filename);
				free(state->yank_buffer);
				free(state);
				return NULL;
			}
			memcpy(state->rows[i].chars, pane->row[i].chars, pane->row[i].size + 1);

			state->rows[i].rsize = 0;
			state->rows[i].render = NULL;
			state->rows[i].hl = NULL;
			state->rows[i].hl_open_comment = 0;
			state->rows[i].indent = pane->row[i].indent;
			state->rows[i].idx = i;
		}
	} else {
		state->rows = NULL;
	}

	return state;
}

void editorFreeUndoState(undoState *state) {
	if (!state) return;

	free(state->filename);
	free(state->yank_buffer);

	if (state->rows) {
		for (int i = 0; i < state->numrows; i++) {
			free(state->rows[i].chars);
			free(state->rows[i].render);
			free(state->rows[i].hl);
		}
		free(state->rows);
	}

	free(state);
}

void editorSaveState() {
	editorPane *pane = E.active_pane;
	if (!pane || pane->type != PANE_EDITOR) return;

	if (pane->undo_stack && pane->undo_stack->timestamp >= time(NULL) - 1) {
		return;
	}

	if (pane->undo_stack_size >= E.max_undo_levels) {
		undoState *curr = pane->undo_stack;
		undoState *prev = NULL;
		while (curr && curr->next) {
			prev = curr;
			curr = curr->next;
		}

		if (prev) {
			editorFreeUndoState(curr);
			prev->next = NULL;
			pane->undo_stack_size--;
		}
	}

	undoState *state = editorCreateUndoState();
	if (!state) return;

	state->next = pane->undo_stack;
	pane->undo_stack = state;
	pane->undo_stack_size++;

	while (pane->redo_stack) {
		undoState *temp = pane->redo_stack;
		pane->redo_stack = pane->redo_stack->next;
		editorFreeUndoState(temp);
		pane->redo_stack_size--;
	}
}

void editorRestoreState(undoState *state) {
	editorPane *pane = E.active_pane;
	if (!pane || pane->type != PANE_EDITOR || !state) return;

	for (int i = 0; i < pane->numrows; i++) {
		editorFreeRow(&pane->row[i]);
	}
	free(pane->row);

	pane->numrows = state->numrows;
	pane->cx = state->cx;
	pane->cy = state->cy;

	if (state->yank_buffer && state->yank_buffer_len > 0) {
		editorSetPasteBuffer(state->yank_buffer, state->yank_buffer_len, 0);
	}

	if (state->numrows > 0) {
		pane->row = calloc(state->numrows, sizeof(erow));
		for (int i = 0; i < state->numrows; i++) {
			pane->row[i].size = state->rows[i].size;
			pane->row[i].chars = malloc(state->rows[i].size + 1);
			memcpy(pane->row[i].chars, state->rows[i].chars, state->rows[i].size + 1);
			pane->row[i].idx = i;
			pane->row[i].indent = state->rows[i].indent;

			pane->row[i].rsize = 0;
			pane->row[i].render = NULL;
			pane->row[i].hl = NULL;
			pane->row[i].hl_open_comment = 0;
			editorUpdateRow(&pane->row[i]);
		}
	} else {
		pane->row = NULL;
	}

	if (pane->cy >= pane->numrows) pane->cy = pane->numrows > 0 ? pane->numrows - 1 : 0;
	if (pane->cy < 0) pane->cy = 0;

	erow *row = (pane->cy >= pane->numrows) ? NULL : &pane->row[pane->cy];
	int rowlen = row ? row->size : 0;
	if (pane->cx > rowlen) pane->cx = rowlen;

	pane->dirty++;
}

void editorUndo() {
	editorPane *pane = E.active_pane;
	if (!pane || pane->type != PANE_EDITOR) return;

	if (!pane->undo_stack) {
		editorSetStatusMessage("Nothing to undo");
		return;
	}

	undoState *current_state = editorCreateUndoState();
	if (current_state) {
		current_state->next = pane->redo_stack;
		pane->redo_stack = current_state;
		pane->redo_stack_size++;

		if (pane->redo_stack_size > E.max_undo_levels) {
			undoState *curr = pane->redo_stack;
			undoState *prev = NULL;
			while (curr && curr->next) {
				prev = curr;
				curr = curr->next;
			}
			if (prev) {
				editorFreeUndoState(curr);
				prev->next = NULL;
				pane->redo_stack_size--;
			}
		}
	}

	undoState *state = pane->undo_stack;
	pane->undo_stack = state->next;
	pane->undo_stack_size--;

	editorRestoreState(state);
	editorFreeUndoState(state);

	editorSetStatusMessage("Undo");
}

void editorRedo() {
	editorPane *pane = E.active_pane;
	if (!pane || pane->type != PANE_EDITOR) return;

	if (!pane->redo_stack) {
		editorSetStatusMessage("Nothing to redo");
		return;
	}

	undoState *current_state = editorCreateUndoState();
	if (current_state) {
		current_state->next = pane->undo_stack;
		pane->undo_stack = current_state;
		pane->undo_stack_size++;

		if (pane->undo_stack_size > E.max_undo_levels) {
			undoState *curr = pane->undo_stack;
			undoState *prev = NULL;
			while (curr && curr->next) {
				prev = curr;
				curr = curr->next;
			}
			if (prev) {
				editorFreeUndoState(curr);
				prev->next = NULL;
				pane->undo_stack_size--;
			}
		}
	}

	undoState *state = pane->redo_stack;
	pane->redo_stack = state->next;
	pane->redo_stack_size--;

	editorRestoreState(state);
	editorFreeUndoState(state);

	editorSetStatusMessage("Redo");
}

/*** search ***/
void editorFindNext() {
	editorPane *pane = E.active_pane;
	if (!pane || pane->type != PANE_EDITOR || !E.search.query) return;

	for (int i = 0; i < pane->numrows; i++) {
		erow *row = &pane->row[i];
		for (int j = 0; j < row->rsize; j++) {
			if (row->hl[j] == HL_MATCH) {
				row->hl[j] = HL_NORMAL;
			}
		}
		editorUpdateSyntax(row);
	}

	int query_len = strlen(E.search.query);
	for (int i = 0; i < pane->numrows; i++) {
		erow *row = &pane->row[i];
		char *m = strstr(row->render, E.search.query);
		while (m) {
			int pos = m - row->render;
			for (int j = 0; j < query_len && pos + j < row->rsize; j++) {
				row->hl[pos + j] = HL_MATCH;
			}
			m = strstr(m + 1, E.search.query);
		}
	}

	int current_row = pane->cy;
	int current_col = pane->cx;

	for (int i = 0; i < pane->numrows; i++) {
		int row_idx = (current_row + i) % pane->numrows;
		erow *row = &pane->row[row_idx];

		char *match = strstr(row->render, E.search.query);
		if (!match) continue;

		if (row_idx == current_row && i == 0) {
			int rx = editorRowCxToRx(row, current_col);
			if (match - row->render <= rx) {
				match = strstr(row->render + rx + 1, E.search.query);
				if (!match) continue;
			}
		}

		E.search.last_match_row = row_idx;
		E.search.last_match_col = editorRowRxToCx(row, match - row->render);

		pane->cy = row_idx;
		pane->cx = E.search.last_match_col;

		editorSetStatusMessage("/%s (n=next, N=prev)", E.search.query);
		return;
	}

	if (E.search.wrap_search) {
		editorSetStatusMessage("Search wrapped to beginning");
	} else {
		editorSetStatusMessage("Pattern not found: %s", E.search.query);
	}
}

void editorFindPrev() {
	editorPane *pane = E.active_pane;
	if (!pane || pane->type != PANE_EDITOR || !E.search.query) return;

	for (int i = 0; i < pane->numrows; i++) {
		erow *row = &pane->row[i];
		for (int j = 0; j < row->rsize; j++) {
			if (row->hl[j] == HL_MATCH) {
				row->hl[j] = HL_NORMAL;
			}
		}
		editorUpdateSyntax(row);
	}

	int query_len = strlen(E.search.query);
	for (int i = 0; i < pane->numrows; i++) {
		erow *row = &pane->row[i];
		char *m = strstr(row->render, E.search.query);
		while (m) {
			int pos = m - row->render;
			for (int j = 0; j < query_len && pos + j < row->rsize; j++) {
				row->hl[pos + j] = HL_MATCH;
			}
			m = strstr(m + 1, E.search.query);
		}
	}

	int current_row = pane->cy;
	int current_col = pane->cx;

	for (int i = 0; i < pane->numrows; i++) {
		int row_idx = (current_row - i + pane->numrows) % pane->numrows;
		erow *row = &pane->row[row_idx];

		char *match = strstr(row->render, E.search.query);
		char *last_match = NULL;
		int last_match_pos = -1;

		while (match) {
			int match_pos = match - row->render;

			if (row_idx == current_row && i == 0) {
				int rx = editorRowCxToRx(row, current_col);
				if (match_pos >= rx) break;
			}

			last_match = match;
			last_match_pos = match_pos;
			match = strstr(match + 1, E.search.query);
		}

		if (last_match) {
			E.search.last_match_row = row_idx;
			E.search.last_match_col = editorRowRxToCx(row, last_match_pos);

			pane->cy = row_idx;
			pane->cx = E.search.last_match_col;

			editorSetStatusMessage("?%s (n=next, N=prev)", E.search.query);
			return;
		}
	}

	if (E.search.wrap_search) {
		editorSetStatusMessage("Search wrapped to end");
	} else {
		editorSetStatusMessage("Pattern not found: %s", E.search.query);
	}
}

void editorFind() {
	editorPane *pane = E.active_pane;
	if (!pane || pane->type != PANE_EDITOR) return;

	int saved_cx = pane->cx;
	int saved_cy = pane->cy;
	int saved_coloff = pane->coloff;
	int saved_rowoff = pane->rowoff;

	char *query = editorPrompt("Search: %s (ESC to cancel, Enter to search)", NULL);

	if (query) {
		free(E.search.query);
		E.search.query = strdup(query);
		E.search.direction = 1;
		E.search.wrap_search = 1;

		editorFindNext();

		free(query);
	} else {
		for (int i = 0; i < pane->numrows; i++) {
			erow *row = &pane->row[i];
			for (int j = 0; j < row->rsize; j++) {
				if (row->hl[j] == HL_MATCH) {
					row->hl[j] = HL_NORMAL;
				}
			}
			editorUpdateSyntax(row);
		}

		pane->cx = saved_cx;
		pane->cy = saved_cy;
		pane->coloff = saved_coloff;
		pane->rowoff = saved_rowoff;
	}
}

/*** file i/o ***/
char *editorRowsToString(int *buflen) {
	editorPane *pane = E.active_pane;
	if (!pane || pane->type != PANE_EDITOR) {
		*buflen = 0;
		return NULL;
	}

	int totlen = 0;
	for (int j = 0; j < pane->numrows; j++)
		totlen += pane->row[j].size + 1;
	*buflen = totlen;

	char *buf = malloc(totlen);
	char *p = buf;
	for (int j = 0; j < pane->numrows; j++) {
		memcpy(p, pane->row[j].chars, pane->row[j].size);
		p += pane->row[j].size;
		*p = '\n';
		p++;
	}

	return buf;
}

void editorSelectSyntaxHighlight() {
	editorPane *pane = E.active_pane;
	if (!pane || pane->type != PANE_EDITOR) return;

	pane->syntax = &HLDB[HLDB_ENTRIES - 1];
	if (pane->filename == NULL) return;

	char *ext = strrchr(pane->filename, '.');

	for (unsigned int j = 0; j < HLDB_ENTRIES - 1; j++) {
		struct editorSyntax *s = &HLDB[j];
		unsigned int i = 0;
		while (s->filematch[i]) {
			int is_ext = (s->filematch[i][0] == '.');
			if ((is_ext && ext && !strcasecmp(ext, s->filematch[i])) ||
				(!is_ext && strstr(pane->filename, s->filematch[i]))) {
				pane->syntax = s;
				for (int filerow = 0; filerow < pane->numrows; filerow++) {
					editorUpdateSyntax(&pane->row[filerow]);
				}
				return;
			}
			i++;
		}
	}
}

void editorOpen(char *filename) {
	editorPane *pane = E.active_pane;
	if (!pane || pane->type != PANE_EDITOR) return;

	free(pane->filename);
	
	char resolved[PATH_MAX];
#ifdef _WIN32
	if (_fullpath(resolved, filename, PATH_MAX)) {
#else
	if (realpath(filename, resolved)) {
#endif
		pane->filename = strdup(resolved);
	} else {
		pane->filename = strdup(filename);
	}

	editorSelectSyntaxHighlight();

	FILE *fp = fopen(filename, "r");

	if (!fp) {
		if (errno == ENOENT) {
			pane->dirty = 0;
			editorSetStatusMessage("New file: %s", pane->filename);
			return;
		} else {
			editorSetStatusMessage("Error opening file: %s", strerror(errno));
			return;
		}
	}

	unsigned char peek[4096];
	size_t peek_n = fread(peek, 1, sizeof(peek), fp);
	for (size_t i = 0; i < peek_n; i++) {
		if (peek[i] == 0) {
			fclose(fp);
			editorSetStatusMessage("Binary file refused: %s", pane->filename);
			pane->dirty = 0;
			return;
		}
	}
	rewind(fp);

	char *line = NULL;
	size_t linecap = 0;
	ssize_t linelen;
	while ((linelen = getline(&line, &linecap, fp)) != -1) {
		while (linelen > 0 && (line[linelen - 1] == '\n' ||
							   line[linelen - 1] == '\r'))
			linelen--;
		editorInsertRow(pane->numrows, line, linelen);
	}
	free(line);
	fclose(fp);
	pane->dirty = 0;
}

void editorSave() {
	editorPane *pane = E.active_pane;
	if (!pane || pane->type != PANE_EDITOR) return;

	if (pane->filename == NULL) {
		pane->filename = editorPrompt("Save as: %s (ESC to cancel)", NULL);
		if (pane->filename == NULL) {
			editorSetStatusMessage("Save aborted");
			return;
		}
		editorSelectSyntaxHighlight();
	}

	int len;
	char *buf = editorRowsToString(&len);

	int fd = open(pane->filename, O_RDWR | O_CREAT, 0644);
	if (fd != -1) {
		if (ftruncate(fd, len) != -1) {
			if (write(fd, buf, len) == len) {
				close(fd);
				free(buf);
				pane->dirty = 0;
				editorSetStatusMessage("%d bytes written to disk", len);
				return;
			}
		}
		close(fd);
	}

	free(buf);
	editorSetStatusMessage("Can't save! I/O error: %s", strerror(errno));
}

/*** drawing ***/
void editorDrawPane(editorPane *pane, struct abuf *ab) {
	(void)ab;
	if (!pane) return;

	if (pane->split_dir != SPLIT_NONE) {
		editorDrawPane(pane->child1, ab);
		editorDrawPane(pane->child2, ab);

		Color border_fg = E.theme.border;
		Color border_bg = E.theme.border;
		if (pane->split_dir == SPLIT_VERTICAL) {
			int split_x = pane->child1->x + pane->child1->width;
			for (int y = pane->y; y < pane->y + pane->height; y++) {
				gridPutStr(split_x, y, "│", 1, border_fg, border_bg);
			}
		} else {
			int split_y = pane->child1->y + pane->child1->height;
			int gx = pane->x;
			for (int x = 0; x < pane->width; x++) {
				gx += gridPutStr(gx, split_y, "─", 1, border_fg, border_bg);
			}
		}
		return;
	}

	if (pane->type == PANE_EXPLORER) {
		explorerRender(pane, ab);
		return;
	}

	if (pane->type == PANE_AI) {
		aiRender(pane, ab);
		return;
	}

	int wrap_width = pane->width - E.line_number_width;
	if (wrap_width < 1) wrap_width = 1;

	for (int screen_y = 0; screen_y < pane->height; screen_y++) {
		int gy = pane->y + screen_y;
		int gx = pane->x;

		int filerow = -1;
		int wrap_line = 0;

		if (E.word_wrap && pane->wrap_lines) {
			int visual_row = 0;
			for (int i = 0; i < pane->numrows; i++) {
				int lines = (pane->row[i].rsize + wrap_width - 1) / wrap_width;
				if (lines < 1) lines = 1;
				if (visual_row + lines > pane->rowoff + screen_y) {
					filerow = i;
					wrap_line = pane->rowoff + screen_y - visual_row;
					break;
				}
				visual_row += lines;
			}
		} else {
			filerow = pane->rowoff + screen_y;
			wrap_line = 0;
		}

		if (filerow >= 0 && filerow < pane->numrows) {
			erow *row = &pane->row[filerow];

			if (E.show_line_numbers) {
				if (wrap_line == 0) {
					char line_num[16];
					int number = E.show_line_numbers == 2 && filerow != pane->cy ?
								abs(filerow - pane->cy) : filerow + 1;
					int w = E.line_number_width - 1;
					snprintf(line_num, sizeof(line_num), "%*d ", w, number);
					gridPutStr(gx, gy, line_num, E.line_number_width, E.theme.line_number, E.theme.bg);
				}
				gx += E.line_number_width;
			}

			int start_col, end_col;
			if (E.word_wrap && pane->wrap_lines) {
				start_col = wrap_line * wrap_width;
				end_col = MIN(start_col + wrap_width, row->rsize);
			} else {
				start_col = pane->coloff;
				end_col = MIN(pane->coloff + wrap_width, row->rsize);
			}

			int drawn = 0;
			for (int j = start_col; j < end_col && drawn < wrap_width; j++) {
				int is_selected = pane == E.active_pane &&
					(E.mode == MODE_VISUAL || E.mode == MODE_VISUAL_LINE) &&
					editorIsInVisualSelection(filerow, j);

				Color cell_fg, cell_bg;
				if (is_selected) {
					cell_fg = E.theme.visual_fg;
					cell_bg = E.theme.visual_bg;
				} else {
					cell_bg = E.theme.bg;
					if (row->hl && j < row->rsize && row->hl[j] == HL_NORMAL) {
						cell_fg = E.theme.fg;
					} else if (row->hl && j < row->rsize) {
						cell_fg = editorSyntaxToRGB(row->hl[j]);
					} else {
						cell_fg = E.theme.fg;
					}
				}

				if (j < row->rsize) {
					unsigned char c = row->render[j];
					if (c < 32 || c == 127) {
						gridPutCh(gx + drawn, gy, '?', cell_fg, cell_bg);
						drawn++;
					} else if (c < 128) {
						gridPutCh(gx + drawn, gy, (char)c, cell_fg, cell_bg);
						drawn++;
					} else {
						int char_len = utf8_byte_length(c);
						if (j + char_len <= row->rsize) {
							int w = utf8_char_width(row->render, j);
							if (w < 1) w = 1;
							if (drawn + w > wrap_width) break;
							gridPutStr(gx + drawn, gy, &row->render[j], w, cell_fg, cell_bg);
							j += char_len - 1;
							drawn += w;
						} else {
							gridPutCh(gx + drawn, gy, '?', cell_fg, cell_bg);
							drawn++;
						}
					}
				}
			}

			int fill_start = (E.show_line_numbers ? E.line_number_width : 0) + drawn;
			for (int x = fill_start; x < pane->width; x++) {
				gridPutCh(pane->x + x, gy, ' ', E.theme.fg, E.theme.bg);
			}
		} else {
			if (E.show_line_numbers) {
				for (int p = 0; p < E.line_number_width - 2; p++)
					gridPutCh(pane->x + p, gy, ' ', E.theme.line_number, E.theme.bg);
				gridPutStr(pane->x + E.line_number_width - 2, gy, "~ ", 2,
					E.theme.line_number, E.theme.bg);
			} else {
				gridPutCh(pane->x, gy, '~', E.theme.line_number, E.theme.bg);
			}
			for (int i = E.show_line_numbers ? E.line_number_width : 1; i < pane->width; i++) {
				gridPutCh(pane->x + i, gy, ' ', E.theme.fg, E.theme.bg);
			}
		}
	}
}

/*** input ***/
int editorReadKey() {
	static int was_streaming = 0;
	int nread;
	char c;

	while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
		if (nread == -1 && errno != EAGAIN) die("read");

#ifndef _WIN32
		if (winch_received) {
			winch_received = 0;
			editorUpdateWindowSize();
			editorResizePanes();
			return CTRL_KEY('l');
		}
#endif
		int is_streaming = (E.right_panel && E.right_panel->type == PANE_AI && E.right_panel->ai && E.right_panel->ai->streaming);
		if (is_streaming) { was_streaming = 1; return 0; }
		if (was_streaming) { was_streaming = 0; return 0; }
	}

	if (c == '\x1b') {
		char seq[32];

		if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
		if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';

		if (seq[0] == '[') {
			if (seq[1] == '<') {
				int idx = 2;
				while (idx < 31) {
					if (read(STDIN_FILENO, &seq[idx], 1) != 1) break;
					if (seq[idx] == 'M' || seq[idx] == 'm') {
						seq[idx + 1] = '\0';
						break;
					}
					idx++;
				}

				int button, x, y;
				char release;
				if (sscanf(&seq[2], "%d;%d;%d%c", &button, &x, &y, &release) == 4) {
					if (button == 64) {
						return MOUSE_WHEEL_UP;
					} else if (button == 65) {
						return MOUSE_WHEEL_DOWN;
					} else if ((button == 0 || button == 1 || button == 2) && release == 'M') {
						E.mouse_x = x - 1;
						E.mouse_y = y - 1;
						E.mouse_button = button;
						return MOUSE_CLICK;
					} else if (button >= 32 && button <= 35) {
						return MOUSE_MOTION;
					}
				}
				return 0;
			}
			
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
				if (seq[1] == '1' && seq[2] == ';') {
					if (read(STDIN_FILENO, &seq[3], 1) != 1) return '\x1b';
					if (read(STDIN_FILENO, &seq[4], 1) != 1) return '\x1b';
					switch (seq[4]) {
					case 'A': return ARROW_UP;
					case 'B': return ARROW_DOWN;
					case 'C': return ARROW_RIGHT;
					case 'D': return ARROW_LEFT;
					case 'H': return HOME_KEY;
					case 'F': return END_KEY;
					}
					return '\x1b';
				}
				if (seq[1] == '2' && seq[2] == '0') {
					if (read(STDIN_FILENO, &seq[3], 1) != 1) return '\x1b';
					if (seq[3] == '0') {
						if (read(STDIN_FILENO, &seq[4], 1) != 1) return '\x1b';
						if (seq[4] == '~') {
							char paste_buffer[65536];
							int paste_len = 0;
							
							editorSaveState();
							E.is_pasting = 1;
							int saved_auto_indent = E.auto_indent;
							int saved_smart_indent = E.smart_indent;
							E.auto_indent = 0;
							E.smart_indent = 0;
							
							while (1) {
								if (read(STDIN_FILENO, &c, 1) != 1) break;
								
								if (c == '\x1b') {
									char end_seq[5];
									if (read(STDIN_FILENO, &end_seq[0], 1) == 1 &&
										read(STDIN_FILENO, &end_seq[1], 1) == 1 &&
										read(STDIN_FILENO, &end_seq[2], 1) == 1 &&
										read(STDIN_FILENO, &end_seq[3], 1) == 1) {
										if (end_seq[0] == '[' && end_seq[1] == '2' &&
											end_seq[2] == '0' && end_seq[3] == '1') {
											if (read(STDIN_FILENO, &c, 1) == 1 && c == '~') {
												paste_buffer[paste_len] = '\0';
												
												for (int i = 0; i < paste_len; i++) {
													if (paste_buffer[i] == '\n' || paste_buffer[i] == '\r') {
														if (paste_buffer[i] == '\r' && i + 1 < paste_len && paste_buffer[i + 1] == '\n') {
															i++;
														}
														editorInsertNewLine();
													} else if (paste_buffer[i] != '\0') {
														editorInsertChar(paste_buffer[i]);
													}
												}
												
												E.auto_indent = saved_auto_indent;
												E.smart_indent = saved_smart_indent;
												E.is_pasting = 0;
												editorSetStatusMessage("Pasted %d bytes", paste_len);
												return 0;
											}
										}
									}
								}
								
								if (paste_len < sizeof(paste_buffer) - 1) {
									paste_buffer[paste_len++] = c;
								}
							}
							
							E.auto_indent = saved_auto_indent;
							E.smart_indent = saved_smart_indent;
							E.is_pasting = 0;
						}
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
			case 'H': return HOME_KEY;
			case 'F': return END_KEY;
			}
		}

		return '\x1b';
	} else {
		return c;
	}
}

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
		} else if (c == '\x1b') {
			editorSetStatusMessage("");
			if (callback) callback(buf, c);
			free(buf);
			return NULL;
		} else if (c == '\r') {
			if (buflen != 0) {
				editorSetStatusMessage("");
				if (callback) callback(buf, c);
				return buf;
			}
		} else if (!iscntrl(c) && c < 128) {
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

void editorSetStatusMessage(const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
	va_end(ap);
	E.statusmsg_time = time(NULL);
}

static editorPane *editorPaneAtCoord(int x, int y) {
	if (E.left_panel && x >= E.left_panel->x && x < E.left_panel->x + E.left_panel->width &&
		y >= E.left_panel->y && y < E.left_panel->y + E.left_panel->height) {
		return E.left_panel;
	}
	if (E.right_panel && x >= E.right_panel->x && x < E.right_panel->x + E.right_panel->width &&
		y >= E.right_panel->y && y < E.right_panel->y + E.right_panel->height) {
		return E.right_panel;
	}
	editorPane **leaves = NULL;
	int count = 0;
	editorCollectLeafPanes(E.root_pane, &leaves, &count);
	editorPane *hit = NULL;
	for (int i = 0; i < count; i++) {
		editorPane *p = leaves[i];
		if (x >= p->x && x < p->x + p->width && y >= p->y && y < p->y + p->height) {
			hit = p;
			break;
		}
	}
	free(leaves);
	return hit;
}

void editorProcessMouse(int button, int x, int y) {
	editorPane *pane = editorPaneAtCoord(x, y);
	if (pane) {
		E.active_pane = pane;
		pane->is_focused = 1;
		if (E.left_panel && pane != E.left_panel) E.left_panel->is_focused = 0;
		if (E.right_panel && pane != E.right_panel) E.right_panel->is_focused = 0;
	}
	editorPane **panes = NULL;
	int count = 0;
	editorCollectLeafPanes(E.root_pane, &panes, &count);

	for (int i = 0; i < count; i++) {
		if (panes[i] == pane) {
			
			if (pane->type == PANE_EDITOR) {
				int rel_y = y - pane->y;
				int file_y, file_x;
				if (E.word_wrap && pane->wrap_lines) {
					int wrap_width = pane->width - E.line_number_width;
					if (wrap_width < 1) wrap_width = 1;
					int target_visual = rel_y + pane->rowoff;
					int visual = 0;
					int found = 0;
					for (int r = 0; r < pane->numrows; r++) {
						int lines = (pane->row[r].rsize + wrap_width - 1) / wrap_width;
						if (lines < 1) lines = 1;
						if (visual + lines > target_visual) {
							file_y = r;
							int seg = target_visual - visual;
							file_x = seg * wrap_width + (x - pane->x - E.line_number_width);
							found = 1;
							break;
						}
						visual += lines;
					}
					if (!found) {
						file_y = pane->numrows - 1;
						file_x = 0;
					}
				} else {
					file_y = rel_y + pane->rowoff;
					file_x = x - pane->x - E.line_number_width + pane->coloff;
				}
				if (file_y >= 0 && file_y < pane->numrows && file_x >= 0) {
					pane->cy = file_y;
					pane->cx = editorRowRxToCx(&pane->row[file_y], file_x);
				}
			}
			
			for (int j = 0; j < count; j++) {
				if (j != i) panes[j]->is_focused = 0;
			}
			break;
		}
	}
	
	free(panes);
}

void editorHandleScroll(int direction) {
	editorPane *pane = editorPaneAtCoord(E.mouse_x, E.mouse_y);
	if (!pane) pane = E.active_pane;
	if (!pane) return;

	int amount = E.scroll_speed ? E.scroll_speed : 3;

	if (pane->type == PANE_AI && pane->ai) {
		if (direction == MOUSE_WHEEL_UP) pane->ai->scroll_offset += amount;
		else if (direction == MOUSE_WHEEL_DOWN) {
			pane->ai->scroll_offset -= amount;
			if (pane->ai->scroll_offset < 0) pane->ai->scroll_offset = 0;
		}
		return;
	}

	if (pane->type == PANE_EXPLORER && pane->explorer) {
		explorerData *d = pane->explorer;
		if (direction == MOUSE_WHEEL_UP) {
			d->scroll_offset -= amount;
			if (d->scroll_offset < 0) d->scroll_offset = 0;
		} else if (direction == MOUSE_WHEEL_DOWN) {
			int max_scroll = d->num_entries - (pane->height - 2);
			if (max_scroll < 0) max_scroll = 0;
			d->scroll_offset += amount;
			if (d->scroll_offset > max_scroll) d->scroll_offset = max_scroll;
		}
		return;
	}

	if (pane->type != PANE_EDITOR) return;

	if (direction == MOUSE_WHEEL_UP) {
		for (int i = 0; i < amount && pane->cy > 0; i++)
			pane->cy--;
	} else if (direction == MOUSE_WHEEL_DOWN) {
		for (int i = 0; i < amount && pane->cy < pane->numrows - 1; i++)
			pane->cy++;
	}

	if (pane->cy < pane->numrows) {
		int rowlen = pane->row[pane->cy].size;
		if (pane->cx > rowlen) pane->cx = rowlen;
	}
}

/*** mode and commands ***/
void editorSetMode(enum editorMode mode) {
	E.mode = mode;
	const char *cursor_seq = NULL;
	switch (mode) {
	case MODE_NORMAL:
		editorSetStatusMessage("-- NORMAL --");
		cursor_seq = "\x1b[2 q";
		break;
	case MODE_INSERT:
		editorSetStatusMessage("-- INSERT --");
		cursor_seq = "\x1b[6 q";
		break;
	case MODE_VISUAL:
		editorSetStatusMessage("-- VISUAL --");
		cursor_seq = "\x1b[2 q";
		break;
	case MODE_VISUAL_LINE:
		editorSetStatusMessage("-- VISUAL LINE --");
		cursor_seq = "\x1b[2 q";
		break;
	case MODE_AI:
		editorSetStatusMessage("-- AI --");
		cursor_seq = "\x1b[6 q";
		break;
	case MODE_COMMAND:
		editorSetStatusMessage("-- COMMAND --");
		cursor_seq = "\x1b[6 q";
		break;
	}
	if (cursor_seq) write(STDOUT_FILENO, cursor_seq, strlen(cursor_seq));
}

void editorColonCommand() {
	char *cmd = editorPrompt(":%s", NULL);
	if (cmd == NULL) return;

	editorPane *pane = E.active_pane;

	if (isdigit((unsigned char)cmd[0])) {
		if (!pane || pane->type != PANE_EDITOR) {
			editorSetStatusMessage("Not in an editor pane");
			free(cmd);
			return;
		}

		int line = atoi(cmd);
		if (line < 1) line = 1;

		if (pane->numrows == 0) {
			editorSetStatusMessage("No lines in file");
			free(cmd);
			return;
		}

		if (line > pane->numrows) {
			line = pane->numrows;
			editorSetStatusMessage("Line %d out of range, moved to line %d", atoi(cmd), line);
		}

		hkPushJump(pane->cx, pane->cy);
		pane->cy = line - 1;
		pane->cx = 0;
		pane->rowoff = pane->cy;
		free(cmd);
		return;
	}

	if (strncmp(cmd, "q ", 2) == 0 || strncmp(cmd, "quit ", 5) == 0
		|| strncmp(cmd, "close ", 6) == 0) {
		const char *target = strchr(cmd, ' ');
		while (target && *target == ' ') target++;
		if (target && *target) {
			if (strcmp(target, "rei") == 0 || strcmp(target, "ai") == 0
				|| strcmp(target, "right") == 0) {
				if (E.right_panel) editorToggleAI();
				else editorSetStatusMessage("rei not open");
			} else if (strcmp(target, "kami") == 0 || strcmp(target, "explorer") == 0
				|| strcmp(target, "left") == 0) {
				if (E.left_panel) editorToggleExplorer();
				else editorSetStatusMessage("kami not open");
			} else {
				editorSetStatusMessage("unknown pane: %s (rei|kami)", target);
			}
		}
		free(cmd);
		return;
	}

	if (strcmp(cmd, "q") == 0 || strcmp(cmd, "quit") == 0) {
		editorPane **leaves = NULL;
		int lcount = 0;
		editorCollectLeafPanes(E.root_pane, &leaves, &lcount);
		int side_count = (E.left_panel ? 1 : 0) + (E.right_panel ? 1 : 0);
		int total_panes = lcount + side_count;
		free(leaves);

		if (E.active_pane == E.right_panel && E.right_panel) {
			editorToggleAI();
			free(cmd);
			return;
		}
		if (E.active_pane == E.left_panel && E.left_panel) {
			editorToggleExplorer();
			free(cmd);
			return;
		}

		if (total_panes > 1 && lcount > 1) {
			if (E.active_pane && E.active_pane->type == PANE_EDITOR && E.active_pane->dirty) {
				editorSetStatusMessage("Unsaved changes. :w then :q, or :q! to force.");
			} else {
				editorClosePane();
			}
			free(cmd);
			return;
		}

		int has_unsaved = 0;
		editorPane **panes = NULL;
		int count = 0;
		editorCollectLeafPanes(E.root_pane, &panes, &count);
		for (int i = 0; i < count; i++) {
			if (panes[i]->type == PANE_EDITOR && panes[i]->dirty) { has_unsaved = 1; break; }
		}
		free(panes);

		if (has_unsaved) {
			editorSetStatusMessage("Unsaved changes in panes. Use :q! to force quit.");
		} else {
			exit(0);
		}
	} else if (strcmp(cmd, "q!") == 0) {
		editorPane **leaves = NULL;
		int lcount = 0;
		editorCollectLeafPanes(E.root_pane, &leaves, &lcount);
		int side_count = (E.left_panel ? 1 : 0) + (E.right_panel ? 1 : 0);
		int total_panes = lcount + side_count;
		free(leaves);
		if (total_panes > 1 && lcount > 1) {
			E.force_window_command = 1;
			if (E.active_pane == E.right_panel && E.right_panel) editorToggleAI();
			else if (E.active_pane == E.left_panel && E.left_panel) editorToggleExplorer();
			else editorClosePane();
			free(cmd);
			return;
		}
		exit(0);
	} else if (strcmp(cmd, "w") == 0 || strcmp(cmd, "write") == 0) {
		editorSave();
	} else if (strncmp(cmd, "w ", 2) == 0) {
		char *filename = cmd + 2;
		while (*filename == ' ') filename++;
		if (*filename && pane && pane->type == PANE_EDITOR) {
			free(pane->filename);
			pane->filename = strdup(filename);
			editorSelectSyntaxHighlight();
			editorSave();
		}
	} else if (strcmp(cmd, "wq") == 0 || strcmp(cmd, "x") == 0) {
		editorSave();
		if (!pane || !pane->dirty) {
			exit(0);
		}
	} else if (strcmp(cmd, "help") == 0 || strcmp(cmd, "h") == 0) {
		editorSetStatusMessage("Commands: :w :q :q! :wq :e <file> :split :vsplit :explorer :ai :config :<num> | Keys: i v V / u ^R dd yy p P gg G ^F ^B 0 $ w b J r ^W");
	} else if (strcmp(cmd, "split") == 0 || strcmp(cmd, "sp") == 0) {
		editorSplitPane(0);
	} else if (strcmp(cmd, "vsplit") == 0 || strcmp(cmd, "vs") == 0) {
		editorSplitPane(1);
	} else if (strcmp(cmd, "explorer") == 0 || strcmp(cmd, "Explorer") == 0 || strcmp(cmd, "ex") == 0) {
		editorToggleExplorer();
	} else if (strcmp(cmd, "rei") == 0 || strcmp(cmd, "ai") == 0) {
		editorToggleAI();
	} else if (strcmp(cmd, "config") == 0 || strcmp(cmd, "genconfig") == 0) {
		editorGenerateConfig();
	} else if (strncmp(cmd, "theme ", 6) == 0 || strncmp(cmd, "colorscheme ", 12) == 0) {
		const char *name = strchr(cmd, ' ');
		if (name) {
			while (*name == ' ') name++;
			if (editorApplyThemeByName(name)) {
				editorSetStatusMessage("theme: %s", name);
			} else {
				editorSetStatusMessage("unknown theme: %s", name);
			}
		}
	} else if (strncmp(cmd, "open ", 5) == 0 || strncmp(cmd, "e ", 2) == 0) {
		char *filename = cmd + (cmd[0] == 'o' ? 5 : 2);
		while (*filename == ' ') filename++;

		char expanded[PATH_MAX];
		if (filename[0] == '~') {
			const char *home = getenv("HOME");
			if (home) {
				snprintf(expanded, sizeof(expanded), "%s%s", home, filename + 1);
				filename = expanded;
			}
		}

		char resolved[PATH_MAX];
		if (*filename) {
#ifndef _WIN32
			char *rp = realpath(filename, resolved);
			if (rp) {
				filename = resolved;
			} else if (filename[0] != '/') {
				char cwd[PATH_MAX];
				if (getcwd(cwd, sizeof(cwd))) {
					snprintf(resolved, sizeof(resolved), "%s/%s", cwd, filename);
					filename = resolved;
				}
			}
#else
			char rp[PATH_MAX];
			if (_fullpath(rp, filename, PATH_MAX)) {
				strncpy(resolved, rp, sizeof(resolved));
				resolved[sizeof(resolved) - 1] = '\0';
				filename = resolved;
			}
#endif
		}

		if (*filename && pane && pane->type == PANE_EDITOR) {
			for (int i = 0; i < pane->numrows; i++) {
				editorFreeRow(&pane->row[i]);
			}
			free(pane->row);
			pane->row = NULL;
			pane->numrows = 0;
			pane->cx = pane->cy = 0;
			pane->rowoff = pane->coloff = 0;

			editorOpen(filename);
		} else {
			editorSetStatusMessage("Usage: :e <filename>");
		}
	} else if (strncmp(cmd, "s/", 2) == 0 || strncmp(cmd, "%s/", 3) == 0 ||
			   strncmp(cmd, ".s/", 3) == 0) {
		if (!pane || pane->type != PANE_EDITOR) {
			editorSetStatusMessage("Not in editor pane");
			free(cmd);
			return;
		}
		int all_lines = (cmd[0] == '%');
		char *p = cmd + (all_lines ? 3 : (cmd[0] == '.' ? 3 : 2));
		char delim = '/';
		char *pat = p;
		char *rep = strchr(pat, delim);
		if (!rep) {
			editorSetStatusMessage("Usage: :s/pat/rep/[g]");
			free(cmd);
			return;
		}
		*rep++ = '\0';
		char *flags = strchr(rep, delim);
		if (flags) { *flags++ = '\0'; } else flags = "";
		int global = strchr(flags, 'g') != NULL;
		int plen = strlen(pat);
		int rlen = strlen(rep);
		if (plen == 0) {
			editorSetStatusMessage("Empty pattern");
			free(cmd);
			return;
		}

		editorSaveState();
		int start_y = all_lines ? 0 : pane->cy;
		int end_y = all_lines ? pane->numrows - 1 : pane->cy;
		int total_subs = 0;
		for (int y = start_y; y <= end_y && y < pane->numrows; y++) {
			erow *row = &pane->row[y];
			int i = 0;
			while (i <= row->size - plen) {
				if (memcmp(&row->chars[i], pat, plen) == 0) {
					int new_size = row->size - plen + rlen;
					char *nb = malloc(new_size + 1);
					memcpy(nb, row->chars, i);
					memcpy(nb + i, rep, rlen);
					memcpy(nb + i + rlen, row->chars + i + plen, row->size - i - plen);
					nb[new_size] = '\0';
					free(row->chars);
					row->chars = nb;
					row->size = new_size;
					editorUpdateRow(row);
					pane->dirty++;
					total_subs++;
					if (!global) break;
					i += rlen;
				} else {
					i++;
				}
			}
		}
		editorSetStatusMessage("%d substitution%s", total_subs, total_subs == 1 ? "" : "s");
	} else if (strcmp(cmd, "reg") == 0 || strcmp(cmd, "registers") == 0) {
		char out[512] = "reg: ";
		int off = 5;
		for (int i = 0; i < 26; i++) {
			if (E.hk.registers[i].data && E.hk.registers[i].len > 0) {
				int n = snprintf(out + off, sizeof(out) - off, "\"%c=%d ", 'a' + i, E.hk.registers[i].len);
				if (n < 0 || n >= (int)(sizeof(out) - off)) break;
				off += n;
			}
		}
		if (E.hk.registers[26].data && E.hk.registers[26].len > 0) {
			snprintf(out + off, sizeof(out) - off, "\"\"=%d ", E.hk.registers[26].len);
		}
		editorSetStatusMessage("%s", out);
	} else {
		editorSetStatusMessage("Unknown command: %s", cmd);
	}
	free(cmd);
}

/*** main drawing functions ***/
void editorDrawRows(struct abuf *ab) {
	(void)ab;
	if (E.left_panel && E.left_panel->width > 0) {
		editorDrawPane(E.left_panel, ab);
		int border_x = E.left_panel->x + E.left_panel->width;
		for (int y = 0; y < E.screenrows; y++) {
			gridPutStr(border_x, y, "│", 1, E.theme.border, E.theme.border);
		}
	}

	if (E.right_panel && E.right_panel->width > 0) {
		editorDrawPane(E.right_panel, ab);
	}

	if (E.root_pane) {
		editorDrawPane(E.root_pane, ab);
	}
}

void editorDrawStatusBar(struct abuf *ab) {
	(void)ab;
	int gy = E.screenrows;
	Color sb_bg = E.theme.border;
	Color sb_fg = E.theme.status_fg;

	for (int i = 0; i < E.screencols; i++) {
		gridPutCh(i, gy, ' ', sb_fg, sb_bg);
	}

	editorPane *pane = E.active_pane;
	char status[80], rstatus[80];

	if (pane && pane->type == PANE_EDITOR) {
		const char *display_name = pane->filename;
		if (display_name) {
			const char *slash = strrchr(display_name, '/');
			if (slash) display_name = slash + 1;
		}
		int len = snprintf(status, sizeof(status), " %.20s %s",
			display_name ? display_name : "[No Name]",
			pane->dirty ? "[+]" : "");
		int rlen = snprintf(rstatus, sizeof(rstatus), "%s | %d:%d ",
			pane->syntax ? pane->syntax->filetype : "text",
			pane->cy + 1, pane->cx + 1);
		if (len > E.screencols) len = E.screencols;
		gridPutStr(0, gy, status, len, sb_fg, sb_bg);
		if (rlen <= E.screencols - len) {
			gridPutStr(E.screencols - rlen, gy, rstatus, rlen, sb_fg, sb_bg);
		}
	} else if (pane && pane->type == PANE_EXPLORER) {
		explorerData *data = pane->explorer;
		int len = snprintf(status, sizeof(status), " %s %s: %.60s",
			E.explorer_kanji, E.explorer_name,
			data && data->current_dir ? data->current_dir : "/");
		int selected_info_len = 0;
		if (data && data->num_entries > 0 && data->selected >= 0 && data->selected < data->num_entries) {
			selected_info_len = snprintf(rstatus, sizeof(rstatus), "%d/%d ",
				data->selected + 1, data->num_entries);
		}
		if (len > E.screencols) len = E.screencols;
		gridPutStr(0, gy, status, len, sb_fg, sb_bg);
		if (selected_info_len > 0 && selected_info_len <= E.screencols - len) {
			gridPutStr(E.screencols - selected_info_len, gy, rstatus, selected_info_len, sb_fg, sb_bg);
		}
	} else if (pane && pane->type == PANE_AI) {
		aiData *data = pane->ai;
		int len = snprintf(status, sizeof(status), " %s %s: %s",
			E.ai_kanji, E.ai_name,
			data && data->mode == MODE_INSERT ? "Listening..." : "Ready");
		int history_info_len = 0;
		if (data && data->history_count > 0) {
			history_info_len = snprintf(rstatus, sizeof(rstatus), "%d messages ",
				data->history_count);
		}
		if (len > E.screencols) len = E.screencols;
		gridPutStr(0, gy, status, len, sb_fg, sb_bg);
		if (history_info_len > 0 && history_info_len <= E.screencols - len) {
			gridPutStr(E.screencols - history_info_len, gy, rstatus, history_info_len, sb_fg, sb_bg);
		}
	}
}

void editorDrawMessageBar(struct abuf *ab) {
	(void)ab;
	int gy = E.screenrows + 1;
	Color msg_bg = {37, 37, 38};
	Color msg_fg = E.theme.fg;

	for (int i = 0; i < E.screencols; i++) {
		gridPutCh(i, gy, ' ', msg_fg, msg_bg);
	}

	int msglen = strlen(E.statusmsg);
	if (msglen > E.screencols) msglen = E.screencols;
	if (msglen && time(NULL) - E.statusmsg_time < 5) {
		gridPutStr(0, gy, E.statusmsg, msglen, msg_fg, msg_bg);
	} else {
		const char *mode_str = "";
		switch (E.mode) {
		case MODE_NORMAL: mode_str = "-- NORMAL --"; break;
		case MODE_INSERT: mode_str = "-- INSERT --"; break;
		case MODE_VISUAL: mode_str = "-- VISUAL --"; break;
		case MODE_VISUAL_LINE: mode_str = "-- VISUAL LINE --"; break;
		case MODE_AI: mode_str = "-- AI --"; break;
		case MODE_COMMAND: mode_str = "-- COMMAND --"; break;
		}
		int mlen = strlen(mode_str);
		if (mlen > E.screencols) mlen = E.screencols;
		gridPutStr(0, gy, mode_str, mlen, msg_fg, msg_bg);
	}
}

void editorRefreshScreen() {
	if (E.splash_active) {
		editorDrawSplash();
		return;
	}

#ifndef _WIN32
	if (winch_received) {
		winch_received = 0;
		editorUpdateWindowSize();
		editorResizePanes();
		E.full_redraw_pending = 1;
	}
#endif
	editorUpdateWindowSize();

	if (E.show_line_numbers) {
		editorPane **all_leaves = NULL;
		int leaf_count = 0;
		editorCollectLeafPanes(E.root_pane, &all_leaves, &leaf_count);
		int max_line = 0;
		for (int i = 0; i < leaf_count; i++) {
			if (all_leaves[i] && all_leaves[i]->type == PANE_EDITOR
				&& all_leaves[i]->numrows > max_line) {
				max_line = all_leaves[i]->numrows;
			}
		}
		free(all_leaves);
		if (max_line < 1) max_line = 1;
		E.line_number_width = 2;
		int n = max_line;
		while (n >= 10) { E.line_number_width++; n /= 10; }
		if (E.line_number_width < 4) E.line_number_width = 4;
	} else {
		E.line_number_width = 0;
	}

	editorScroll();

	struct abuf ab = ABUF_INIT;

	abAppend(&ab, "\x1b[?25l", 6);
	if (E.full_redraw_pending) {
		abAppend(&ab, "\x1b[2J", 4);
		abAppend(&ab, "\x1b[H", 3);
		gridInvalidateFront();
		E.full_redraw_pending = 0;
	}

	gridClear(E.theme.bg);
	editorDrawRows(&ab);
	editorDrawStatusBar(&ab);
	editorDrawMessageBar(&ab);
	gridFlush(&ab);

	editorPane *pane = E.active_pane;
	if (pane && pane->type == PANE_EDITOR) {
		int cursor_x = pane->x + E.line_number_width + 1;
		int cursor_y = pane->y + 1;

		if (E.word_wrap && pane->wrap_lines && pane->cy < pane->numrows) {
			int wrap_width = pane->width - E.line_number_width;
			if (wrap_width <= 0) wrap_width = 1;

			int visual_row = 0;
			for (int i = 0; i < pane->cy; i++) {
				int lines = (pane->row[i].rsize + wrap_width - 1) / wrap_width;
				if (lines < 1) lines = 1;
				visual_row += lines;
			}

			int rx = editorRowCxToRx(&pane->row[pane->cy], pane->cx);
			int line_in_wrap = rx / wrap_width;
			int col_in_line = rx % wrap_width;
			visual_row += line_in_wrap;
			
			cursor_y = pane->y + (visual_row - pane->rowoff) + 1;
			cursor_x = pane->x + E.line_number_width + col_in_line + 1;
		} else {
			cursor_y = pane->y + (pane->cy - pane->rowoff) + 1;
			cursor_x = pane->x + (pane->rx - pane->coloff) + E.line_number_width + 1;
		}

		if (cursor_x < pane->x + 1) cursor_x = pane->x + 1;
		if (cursor_x > pane->x + pane->width) cursor_x = pane->x + pane->width;
		if (cursor_y < pane->y + 1) cursor_y = pane->y + 1;
		if (cursor_y > pane->y + pane->height) cursor_y = pane->y + pane->height;

		char buf[32];
		snprintf(buf, sizeof(buf), "\x1b[%d;%dH", cursor_y, cursor_x);
		abAppend(&ab, buf, strlen(buf));
	} else if (pane && pane->type == PANE_EXPLORER && pane->explorer) {
		explorerData *edata = pane->explorer;
		int row = pane->y + 2 + (edata->selected - edata->scroll_offset) + 1;
		int col = pane->x + 2;
		char buf[32];
		snprintf(buf, sizeof(buf), "\x1b[%d;%dH", row, col);
		abAppend(&ab, buf, strlen(buf));
	} else if (pane && pane->type == PANE_AI && pane->ai) {
		aiData *adata = pane->ai;
		int inner_w = pane->width - 1 - 2;
		if (inner_w < 4) inner_w = 4;
		int seg_w = inner_w - 2;
		if (seg_w < 2) seg_w = 2;

		int vline = 0;
		int col_in_line = 0;
		int total_vlines = 0;
		int pcur = adata->prompt_cursor;
		if (pcur < 0) pcur = 0;
		if (pcur > adata->prompt_len) pcur = adata->prompt_len;
		if (adata->prompt_buffer && adata->prompt_len > 0) {
			int i = 0;
			while (i <= adata->prompt_len) {
				int line_start = i;
				while (i < adata->prompt_len && adata->prompt_buffer[i] != '\n') i++;
				int line_end = i;
				int seg = line_start;
				do {
					int take = (line_end - seg > seg_w) ? seg_w : (line_end - seg);
					if (pcur >= seg && pcur <= seg + take && pcur <= line_end) {
						vline = total_vlines;
						col_in_line = pcur - seg;
					}
					total_vlines++;
					seg += take;
				} while (seg < line_end);
				if (line_end == adata->prompt_len && (adata->prompt_len == 0 || adata->prompt_buffer[adata->prompt_len - 1] != '\n')) break;
				i++;
				if (i == adata->prompt_len + 1) break;
			}
			if (adata->prompt_len > 0 && adata->prompt_buffer[adata->prompt_len - 1] == '\n' && pcur == adata->prompt_len) {
				vline = total_vlines;
				col_in_line = 0;
				total_vlines++;
			}
		} else {
			total_vlines = 1;
		}

		int plines = total_vlines > 5 ? 5 : total_vlines;
		int pscroll = total_vlines > plines ? total_vlines - plines : 0;
		int prompt_rows = 1 + plines + 1;
		int history_end = pane->height - prompt_rows - 1;
		if (history_end < 2) history_end = 2;

		int prow = vline - pscroll;
		if (prow < 0) prow = 0;
		if (prow >= plines) prow = plines - 1;

		int row = pane->y + history_end + 2 + prow + 1;
		int col = pane->x + 1 + 1 + 2 + col_in_line + 1;
		char buf[32];
		snprintf(buf, sizeof(buf), "\x1b[%d;%dH", row, col);
		abAppend(&ab, buf, strlen(buf));
	}

	abAppend(&ab, "\x1b[?25h", 6);

	write(STDOUT_FILENO, ab.b, ab.len);
	abFree(&ab);
}

/*** input processing ***/
static int hkReadKeyBlocking(void) {
	int k;
	do { k = editorReadKey(); }
	while (k == MOUSE_MOTION || k == MOUSE_CLICK || k == MOUSE_WHEEL_UP || k == MOUSE_WHEEL_DOWN || k == 0);
	return k;
}

static int hkConsumeCount(void) {
	int n = E.hk.pending_count;
	E.hk.pending_count = 0;
	return n > 0 ? n : 1;
}

static void hkRecordStart(char op, int count) {
	E.hk.last_op = op;
	E.hk.last_count = count;
	free(E.hk.last_insert);
	E.hk.last_insert = NULL;
	E.hk.last_insert_len = 0;
	E.hk.recording_insert = 1;
}

static void hkRecordChar(int c) {
	if (!E.hk.recording_insert) return;
	E.hk.last_insert = realloc(E.hk.last_insert, E.hk.last_insert_len + 2);
	if (!E.hk.last_insert) return;
	E.hk.last_insert[E.hk.last_insert_len++] = (char)c;
	E.hk.last_insert[E.hk.last_insert_len] = '\0';
}

static void hkRecordBackspace(void) {
	if (!E.hk.recording_insert) return;
	if (E.hk.last_insert_len > 0) E.hk.last_insert_len--;
	else {
		E.hk.last_insert = realloc(E.hk.last_insert, E.hk.last_insert_len + 2);
		if (E.hk.last_insert) E.hk.last_insert[E.hk.last_insert_len++] = 0x08;
	}
	if (E.hk.last_insert) E.hk.last_insert[E.hk.last_insert_len] = '\0';
}

static void hkRecordStop(void) {
	E.hk.recording_insert = 0;
}

static void hkReplayInsert(void) {
	if (!E.hk.last_insert) return;
	for (int i = 0; i < E.hk.last_insert_len; i++) {
		char c = E.hk.last_insert[i];
		if (c == '\n') editorInsertNewLine();
		else if (c == 0x08) editorDelChar();
		else editorInsertChar((unsigned char)c);
	}
}

static void hkDotRepeat(void) {
	editorPane *pane = E.active_pane;
	if (!pane || pane->type != PANE_EDITOR) return;
	if (E.hk.last_op == 0) {
		editorSetStatusMessage("Nothing to repeat");
		return;
	}
	int n = E.hk.last_count > 0 ? E.hk.last_count : 1;
	editorSaveState();
	switch (E.hk.last_op) {
	case 'i':
	case 'I':
		hkReplayInsert();
		break;
	case 'a':
		if (pane->cy < pane->numrows && pane->cx < pane->row[pane->cy].size) pane->cx++;
		hkReplayInsert();
		break;
	case 'A':
		if (pane->cy < pane->numrows) pane->cx = pane->row[pane->cy].size;
		hkReplayInsert();
		break;
	case 'o':
		if (pane->cy < pane->numrows) pane->cx = pane->row[pane->cy].size;
		editorInsertNewLine();
		hkReplayInsert();
		break;
	case 'O':
		pane->cx = 0;
		editorInsertRow(pane->cy, "", 0);
		hkReplayInsert();
		break;
	case 'x':
		for (int k = 0; k < n; k++) {
			if (pane->cy < pane->numrows && pane->cx < pane->row[pane->cy].size) {
				editorRowDelChar(&pane->row[pane->cy], pane->cx);
				if (pane->cx >= pane->row[pane->cy].size && pane->cx > 0) pane->cx--;
			}
		}
		break;
	case 'd':
		for (int k = 0; k < n && pane->cy < pane->numrows; k++) {
			editorDelRow(pane->cy);
		}
		if (pane->numrows == 0) editorInsertRow(0, "", 0);
		if (pane->cy >= pane->numrows) pane->cy = pane->numrows - 1;
		pane->cx = 0;
		break;
	case 'J':
		for (int k = 0; k < n; k++) {
			if (pane->cy < pane->numrows - 1) {
				pane->cx = pane->row[pane->cy].size;
				if (pane->row[pane->cy].size > 0 && pane->row[pane->cy + 1].size > 0) editorInsertChar(' ');
				editorRowAppendString(&pane->row[pane->cy], pane->row[pane->cy + 1].chars, pane->row[pane->cy + 1].size);
				editorDelRow(pane->cy + 1);
			}
		}
		break;
	case 'w':
	case 'W':
		if (pane->cy < pane->numrows) {
			erow *row = &pane->row[pane->cy];
			int start = pane->cx;
			int end = pane->cx;
			while (end < row->size && !is_separator(row->chars[end])) end++;
			while (end < row->size && is_separator(row->chars[end]) && row->chars[end] != '\n') end++;
			if (end > start) {
				memmove(&row->chars[start], &row->chars[end], row->size - end + 1);
				row->size -= (end - start);
				editorUpdateRow(row);
				pane->dirty++;
				if (pane->cx >= row->size && pane->cx > 0) pane->cx = row->size > 0 ? row->size - 1 : 0;
			}
			if (E.hk.last_op == 'W') hkReplayInsert();
		}
		break;
	}
}

static int hkTextObject(editorPane *pane, int inner, int obj, int *sx, int *ex) {
	if (pane->cy >= pane->numrows) return -1;
	erow *row = &pane->row[pane->cy];
	int cx = pane->cx;
	if (cx > row->size) cx = row->size;

	if (obj == 'w') {
		int s = cx, e = cx;
		if (s >= row->size) return -1;
		if (is_separator(row->chars[s])) {
			if (!inner) {
				while (e < row->size && is_separator(row->chars[e])) e++;
				*sx = s; *ex = e - 1;
				return 0;
			}
			while (s < row->size && is_separator(row->chars[s])) s++;
			if (s >= row->size) return -1;
			e = s;
		} else {
			while (s > 0 && !is_separator(row->chars[s - 1])) s--;
			e = s;
		}
		while (e < row->size && !is_separator(row->chars[e])) e++;
		if (!inner) {
			while (e < row->size && is_separator(row->chars[e])) e++;
		}
		*sx = s; *ex = e - 1;
		return 0;
	}

	char open = 0, close = 0;
	int is_pair = 0;
	switch (obj) {
	case '"': open = close = '"'; break;
	case '\'': open = close = '\''; break;
	case '`': open = close = '`'; break;
	case '(': case ')': open = '('; close = ')'; is_pair = 1; break;
	case '[': case ']': open = '['; close = ']'; is_pair = 1; break;
	case '{': case '}': open = '{'; close = '}'; is_pair = 1; break;
	default: return -1;
	}

	int left = -1, right = -1;
	if (is_pair) {
		int depth = 0;
		for (int i = cx; i >= 0; i--) {
			if (row->chars[i] == close) depth++;
			else if (row->chars[i] == open) {
				if (depth == 0) { left = i; break; }
				depth--;
			}
		}
		if (left < 0) return -1;
		depth = 0;
		for (int i = left + 1; i < row->size; i++) {
			if (row->chars[i] == open) depth++;
			else if (row->chars[i] == close) {
				if (depth == 0) { right = i; break; }
				depth--;
			}
		}
	} else {
		for (int i = cx; i >= 0; i--) {
			if (row->chars[i] == open) { left = i; break; }
		}
		if (left < 0) {
			for (int i = cx + 1; i < row->size; i++) {
				if (row->chars[i] == open) { left = i; break; }
			}
		}
		if (left < 0) return -1;
		for (int i = left + 1; i < row->size; i++) {
			if (row->chars[i] == close) { right = i; break; }
		}
	}
	if (right < 0) return -1;

	if (inner) { *sx = left + 1; *ex = right - 1; }
	else { *sx = left; *ex = right; }
	if (*ex < *sx) return -1;
	return 0;
}

static void hkApplyLineOp(editorPane *pane, int op, int sx, int ex) {
	erow *row = &pane->row[pane->cy];
	int len = ex - sx + 1;
	if (len <= 0) return;
	if (op == 'y') {
		hkSetRegister(E.hk.pending_reg, &row->chars[sx], len, 0, 1);
		E.hk.pending_reg = 0;
		return;
	}
	hkSetRegister(E.hk.pending_reg, &row->chars[sx], len, 0, 0);
	E.hk.pending_reg = 0;
	memmove(&row->chars[sx], &row->chars[ex + 1], row->size - ex - 1);
	row->size -= len;
	row->chars[row->size] = '\0';
	editorUpdateRow(row);
	pane->dirty++;
	pane->cx = sx;
	if (pane->cx > row->size) pane->cx = row->size > 0 ? row->size - 1 : 0;
	if (op == 'c') editorSetMode(MODE_INSERT);
}

void hkPushJump(int x, int y) {
	if (E.hk.jump_count >= 100) {
		free(E.hk.jumps[0].filename);
		memmove(&E.hk.jumps[0], &E.hk.jumps[1], sizeof(E.hk.jumps[0]) * 99);
		E.hk.jump_count = 99;
	}
	E.hk.jumps[E.hk.jump_count].x = x;
	E.hk.jumps[E.hk.jump_count].y = y;
	E.hk.jumps[E.hk.jump_count].filename = NULL;
	E.hk.jump_count++;
	E.hk.jump_pos = E.hk.jump_count;
}

void hkClampCursor(editorPane *pane) {
	if (pane->cy < 0) pane->cy = 0;
	if (pane->cy >= pane->numrows) pane->cy = pane->numrows > 0 ? pane->numrows - 1 : 0;
	if (pane->cy < pane->numrows && pane->cx > pane->row[pane->cy].size) {
		pane->cx = pane->row[pane->cy].size;
	}
	if (pane->cx < 0) pane->cx = 0;
}

void editorProcessKeyPress() {
	static int last_char = 0;
	static int g_pressed = 0;
	static int d_pressed = 0;
	static time_t last_undo_time = 0;
	static int last_action = 0;

	int c = editorReadKey();
	
	if (E.splash_active) {
		if (c == MOUSE_WHEEL_UP || c == MOUSE_WHEEL_DOWN || c == MOUSE_MOTION || c == 0) {
			return;
		}

		E.splash_active = 0;
		E.splash_dismissed = 1;
		editorSetStatusMessage(":h = help | :w = write | :q = quit | / = find | ^W = window | :rei/:ai = agent");
		return;
	}

	if (c == CTRL_KEY('l')) {
		return;
	}

	if (c == MOUSE_WHEEL_UP || c == MOUSE_WHEEL_DOWN) {
		editorHandleScroll(c);
		return;
	}

	if (c == MOUSE_CLICK) {
		editorProcessMouse(0, E.mouse_x, E.mouse_y);
		if (E.mode == MODE_INSERT) editorSetMode(MODE_INSERT);
		return;
	}

	if (c == MOUSE_MOTION || c == 0) {
		return;
	}

	if (E.in_window_command) {
		E.in_window_command = 0;
		switch (c) {
		case 's':
			editorSplitPane(0);
			break;
		case 'v':
			editorSplitPane(1);
			break;
		case 'w':
		case CTRL_KEY('w'):
			editorNextPane();
			break;
		case 'c':
			editorClosePane();
			break;
		case '!':
			E.force_window_command = 1;
			editorClosePane();
			break;
		case '+':
		case '-':
		case '<':
		case '>': {
			int delta_cols = (c == '+' || c == '>') ? 4 : -4;
			if (E.active_pane == E.left_panel && E.left_panel) {
				int nw = E.left_panel_width + delta_cols;
				int max_w = E.screencols - (E.right_panel ? E.right_panel_width : 0) - 10;
				if (nw >= 10 && nw <= max_w) {
					E.left_panel_width = nw;
					editorResizePanes();
				}
			} else if (E.active_pane == E.right_panel && E.right_panel) {
				int nw = E.right_panel_width - delta_cols;
				int max_w = E.screencols - (E.left_panel ? E.left_panel_width : 0) - 10;
				if (nw >= 10 && nw <= max_w) {
					E.right_panel_width = nw;
					editorResizePanes();
				}
			} else if (E.active_pane && E.active_pane->parent && E.active_pane->parent->split_dir != SPLIT_NONE) {
				float delta = (c == '+' || c == '>') ? 0.05 : -0.05;
				float new_ratio = E.active_pane->parent->split_ratio + delta;
				if (E.active_pane->parent->child2 == E.active_pane) new_ratio = E.active_pane->parent->split_ratio - delta;
				if (new_ratio >= 0.2 && new_ratio <= 0.8) {
					E.active_pane->parent->split_ratio = new_ratio;
					editorUpdatePaneBounds(E.active_pane->parent);
				}
			}
			break;
		}
		default:
			editorSetStatusMessage("Unknown window command");
			break;
		}
		return;
	}

	if (c == CTRL_KEY('w')) {
		E.in_window_command = 1;
		editorSetStatusMessage("Window command (s/v/w/c/!)");
		return;
	}

	editorPane *pane = E.active_pane;

	if (pane && pane->type == PANE_EXPLORER) {
		explorerHandleKey(pane, c);
		return;
	}

	if (pane && pane->type == PANE_AI) {
		aiHandleKey(pane, c);
		return;
	}

	if (!pane || pane->type != PANE_EDITOR) return;

	if (E.mode == MODE_NORMAL) {
		if (getenv("HAKO_TRACE")) {
			editorSetStatusMessage("key=%d('%c') last=%d('%c') reg=%d", c,
				(c >= 32 && c < 127) ? c : '?', last_char,
				(last_char >= 32 && last_char < 127) ? last_char : '?',
				E.hk.pending_reg);
		}
		if ((c >= '1' && c <= '9') || (c == '0' && E.hk.pending_count > 0)) {
			long next = (long)E.hk.pending_count * 10 + (c - '0');
			if (next > 100000) next = 100000;
			E.hk.pending_count = (int)next;
			return;
		}

		if (E.hk.waiting_reg) {
			E.hk.waiting_reg = 0;
			if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
				c == '"' || c == '+' || c == '*' || c == '0' || c == '_' || c == '-') {
				E.hk.pending_reg = (char)c;
				editorSetStatusMessage("reg=\"%c (key=%d)", c, c);
			} else {
				E.hk.pending_reg = 0;
				editorSetStatusMessage("register canceled (key=%d)", c);
			}
			return;
		}

		if (c == '"') {
			last_char = 0; g_pressed = 0; d_pressed = 0;
			E.hk.waiting_reg = 1;
			editorSetStatusMessage("\" (waiting for register name)");
			return;
		}

		if ((last_char == 'd' || last_char == 'c' || last_char == 'y') && (c == 'i' || c == 'a')) {
			int op = last_char;
			int inner = (c == 'i');
			int obj = hkReadKeyBlocking();
			int sx, ex;
			if (hkTextObject(pane, inner, obj, &sx, &ex) == 0) {
				editorSaveState();
				hkApplyLineOp(pane, op, sx, ex);
			} else {
				editorSetStatusMessage("No text object: %c%c%c at %d,%d (cursor must be inside)", op, c, (obj >= 32 && obj < 127) ? obj : '?', pane->cx, pane->cy);
			}
			last_char = 0; g_pressed = 0; d_pressed = 0;
			return;
		}

		switch (c) {
		case '%': {
			if (pane->cy >= pane->numrows) break;
			erow *row = &pane->row[pane->cy];
			int start = pane->cx;
			int open = -1, close = -1, dir = 0;
			const char *pairs = "()[]{}";
			while (start < row->size) {
				const char *p = strchr(pairs, row->chars[start]);
				if (p) {
					int i = p - pairs;
					if (i % 2 == 0) { open = pairs[i]; close = pairs[i+1]; dir = 1; }
					else { open = pairs[i]; close = pairs[i-1]; dir = -1; }
					break;
				}
				start++;
			}
			if (dir == 0) { last_char = 0; g_pressed = 0; break; }
			hkPushJump(pane->cx, pane->cy);
			int depth = 1;
			int y = pane->cy, x = start;
			if (dir > 0) {
				x++;
				while (y < pane->numrows) {
					erow *r = &pane->row[y];
					while (x < r->size) {
						if (r->chars[x] == open) depth++;
						else if (r->chars[x] == close) {
							depth--;
							if (depth == 0) { pane->cy = y; pane->cx = x; goto pct_done; }
						}
						x++;
					}
					y++; x = 0;
				}
			} else {
				x--;
				while (y >= 0) {
					erow *r = &pane->row[y];
					while (x >= 0) {
						if (r->chars[x] == open) depth++;
						else if (r->chars[x] == close) {
							depth--;
							if (depth == 0) { pane->cy = y; pane->cx = x; goto pct_done; }
						}
						x--;
					}
					y--;
					if (y >= 0) x = pane->row[y].size - 1;
				}
			}
			editorSetStatusMessage("No matching bracket");
		pct_done:
			last_char = 0; g_pressed = 0;
			break;
		}

		case '*':
		case '#': {
			if (pane->cy >= pane->numrows) break;
			erow *row = &pane->row[pane->cy];
			int s = pane->cx, e = pane->cx;
			if (s >= row->size) break;
			if (is_separator(row->chars[s])) {
				while (s < row->size && is_separator(row->chars[s])) s++;
				if (s >= row->size) break;
				e = s;
			} else {
				while (s > 0 && !is_separator(row->chars[s - 1])) s--;
				e = s;
			}
			while (e < row->size && !is_separator(row->chars[e])) e++;
			int wlen = e - s;
			if (wlen <= 0) break;
			free(E.search.query);
			E.search.query = malloc(wlen + 1);
			memcpy(E.search.query, &row->chars[s], wlen);
			E.search.query[wlen] = '\0';
			E.search.direction = (c == '*') ? 1 : -1;
			hkPushJump(pane->cx, pane->cy);
			if (c == '*') editorFindNext(); else editorFindPrev();
			last_char = 0; g_pressed = 0;
			break;
		}

		case 'm': {
			int mk = hkReadKeyBlocking();
			if (mk >= 'a' && mk <= 'z') {
				int i = mk - 'a';
				E.hk.marks_x[i] = pane->cx;
				E.hk.marks_y[i] = pane->cy;
				E.hk.marks_set |= (1 << i);
				editorSetStatusMessage("Mark '%c set", mk);
			}
			last_char = 0; g_pressed = 0;
			break;
		}

		case '`':
		case '\'': {
			int squote = (c == '\'');
			int mk = hkReadKeyBlocking();
			if (mk >= 'a' && mk <= 'z') {
				int i = mk - 'a';
				if (E.hk.marks_set & (1 << i)) {
					hkPushJump(pane->cx, pane->cy);
					pane->cy = E.hk.marks_y[i];
					pane->cx = squote ? 0 : E.hk.marks_x[i];
					if (squote && pane->cy < pane->numrows) {
						erow *r = &pane->row[pane->cy];
						while (pane->cx < r->size && isspace((unsigned char)r->chars[pane->cx])) pane->cx++;
					}
					hkClampCursor(pane);
				} else {
					editorSetStatusMessage("Mark '%c not set", mk);
				}
			}
			last_char = 0; g_pressed = 0;
			break;
		}

		case CTRL_KEY('o'):
			if (E.hk.jump_pos > 0) {
				if (E.hk.jump_pos == E.hk.jump_count) {
					hkPushJump(pane->cx, pane->cy);
					E.hk.jump_pos--;
				}
				E.hk.jump_pos--;
				pane->cx = E.hk.jumps[E.hk.jump_pos].x;
				pane->cy = E.hk.jumps[E.hk.jump_pos].y;
				hkClampCursor(pane);
			} else {
				editorSetStatusMessage("Jumplist empty");
			}
			last_char = 0; g_pressed = 0;
			break;

		case '\t':
			if (E.hk.jump_pos + 1 < E.hk.jump_count) {
				E.hk.jump_pos++;
				pane->cx = E.hk.jumps[E.hk.jump_pos].x;
				pane->cy = E.hk.jumps[E.hk.jump_pos].y;
				hkClampCursor(pane);
			}
			last_char = 0; g_pressed = 0;
			break;

		case 'i':
			hkRecordStart('i', hkConsumeCount());
			editorSetMode(MODE_INSERT);
			editorSaveState();
			break;

		case 'I':
			pane->cx = 0;
			if (pane->cy < pane->numrows) {
				erow *row = &pane->row[pane->cy];
				while (pane->cx < row->size && isspace(row->chars[pane->cx]))
					pane->cx++;
			}
			hkRecordStart('I', hkConsumeCount());
			editorSetMode(MODE_INSERT);
			editorSaveState();
			break;

		case 'a':
			if (pane->cy < pane->numrows && pane->cx < pane->row[pane->cy].size) {
				pane->cx = utf8_next_char(pane->row[pane->cy].chars, pane->cx, pane->row[pane->cy].size);
			}
			hkRecordStart('a', hkConsumeCount());
			editorSetMode(MODE_INSERT);
			editorSaveState();
			editorSetStatusMessage("INSERT via 'a' (waiting_reg was %d)", E.hk.waiting_reg);
			break;

		case 'A':
			if (pane->cy < pane->numrows) {
				pane->cx = pane->row[pane->cy].size;
			}
			hkRecordStart('A', hkConsumeCount());
			editorSetMode(MODE_INSERT);
			editorSaveState();
			break;

		case 'o':
			if (pane->cy < pane->numrows) {
				pane->cx = pane->row[pane->cy].size;
			}
			editorSaveState();
			editorInsertNewLine();
			hkRecordStart('o', hkConsumeCount());
			editorSetMode(MODE_INSERT);
			break;

		case 'O':
			pane->cx = 0;
			editorSaveState();
			editorInsertRow(pane->cy, "", 0);
			hkRecordStart('O', hkConsumeCount());
			editorSetMode(MODE_INSERT);
			break;

		case '.':
			hkDotRepeat();
			last_char = 0; g_pressed = 0;
			break;

		case 'x': {
			int n = hkConsumeCount();
			E.hk.last_op = 'x'; E.hk.last_count = n;
			editorSaveState();
			char *buf = NULL; int blen = 0; int bcap = 0;
			for (int k = 0; k < n; k++) {
				if (pane->cy < pane->numrows && pane->cx < pane->row[pane->cy].size) {
					char ch = pane->row[pane->cy].chars[pane->cx];
					if (blen + 1 > bcap) { bcap = bcap ? bcap * 2 : 16; buf = realloc(buf, bcap); }
					buf[blen++] = ch;
					editorRowDelChar(&pane->row[pane->cy], pane->cx);
					if (pane->cx >= pane->row[pane->cy].size && pane->cx > 0) pane->cx--;
				}
			}
			if (blen > 0) hkSetRegister(E.hk.pending_reg, buf, blen, 0, 0);
			free(buf);
			E.hk.pending_reg = 0;
			last_char = 0; g_pressed = 0;
			break;
		}

		case 'D':
			if (pane->cy < pane->numrows) {
				editorSaveState();
				erow *row = &pane->row[pane->cy];
				if (pane->cx < row->size) {
					hkSetRegister(E.hk.pending_reg, &row->chars[pane->cx], row->size - pane->cx, 0, 0);
					row->size = pane->cx;
					row->chars[row->size] = '\0';
					editorUpdateRow(row);
					pane->dirty++;
				}
			}
			E.hk.pending_reg = 0;
			last_char = 0;
			g_pressed = 0;
			break;

		case 'C':
			if (pane->cy < pane->numrows) {
				editorSaveState();
				erow *row = &pane->row[pane->cy];
				if (pane->cx < row->size) {
					hkSetRegister(E.hk.pending_reg, &row->chars[pane->cx], row->size - pane->cx, 0, 0);
					row->size = pane->cx;
					row->chars[row->size] = '\0';
					editorUpdateRow(row);
					pane->dirty++;
				}
			}
			E.hk.pending_reg = 0;
			editorSetMode(MODE_INSERT);
			break;

		case 'V':
			pane->visual_anchor_x = 0;
			pane->visual_anchor_y = pane->cy;
			editorSetMode(MODE_VISUAL_LINE);
			break;

		case 'v':
			pane->visual_anchor_x = pane->cx;
			pane->visual_anchor_y = pane->cy;
			editorSetMode(MODE_VISUAL);
			break;

		case 'u': {
			int n = hkConsumeCount();
			for (int k = 0; k < n; k++) editorUndo();
			break;
		}

		case 'n': {
			int n = hkConsumeCount();
			if (E.search.query) {
				for (int k = 0; k < n; k++) editorFindNext();
			} else {
				editorSetStatusMessage("No previous search");
			}
			last_char = 0; g_pressed = 0;
			break;
		}

		case 'g':
			if (g_pressed) {
				int target = E.hk.pending_count > 0 ? E.hk.pending_count - 1 : 0;
				if (target < 0) target = 0;
				if (pane->numrows > 0 && target >= pane->numrows) target = pane->numrows - 1;
				hkPushJump(pane->cx, pane->cy);
				pane->cy = target;
				pane->cx = 0;
				E.hk.pending_count = 0;
				g_pressed = 0;
				if (d_pressed) {
					editorSaveState();
					while (pane->numrows > 0) {
						editorDelRow(0);
					}
					editorInsertRow(0, "", 0);
					pane->cx = pane->cy = 0;
					editorSetStatusMessage("Deleted all");
					d_pressed = 0;
				}
			} else {
				g_pressed = 1;
			}
			last_char = 0;
			break;

		case 'G':
			if (d_pressed) {
				editorSaveState();
				while (pane->cy < pane->numrows - 1) {
					editorDelRow(pane->cy + 1);
				}
				if (pane->cy < pane->numrows) {
					pane->row[pane->cy].size = pane->cx;
					pane->row[pane->cy].chars[pane->cx] = '\0';
					editorUpdateRow(&pane->row[pane->cy]);
				}
				editorSetStatusMessage("Deleted to end of file");
				d_pressed = 0;
				E.hk.pending_count = 0;
			} else {
				hkPushJump(pane->cx, pane->cy);
				if (E.hk.pending_count > 0) {
					int target = E.hk.pending_count - 1;
					if (pane->numrows > 0 && target >= pane->numrows) target = pane->numrows - 1;
					if (target < 0) target = 0;
					pane->cy = target;
					pane->cx = 0;
				} else {
					pane->cy = pane->numrows > 0 ? pane->numrows - 1 : 0;
				}
				E.hk.pending_count = 0;
			}
			g_pressed = 0;
			last_char = 0;
			break;

		case 'y':
			if (last_char == 'y') {
				int n = hkConsumeCount();
				if (pane->cy < pane->numrows) {
					int end = pane->cy + n;
					if (end > pane->numrows) end = pane->numrows;
					int total = 0;
					for (int k = pane->cy; k < end; k++) total += pane->row[k].size + 1;
					char *buf = malloc(total + 1);
					int off = 0;
					for (int k = pane->cy; k < end; k++) {
						memcpy(buf + off, pane->row[k].chars, pane->row[k].size);
						off += pane->row[k].size;
						buf[off++] = '\n';
					}
					buf[off] = '\0';
					hkSetRegister(E.hk.pending_reg, buf, off, 1, 1);
					free(buf);
					editorSetStatusMessage("%d line%s yanked", end - pane->cy, (end - pane->cy) == 1 ? "" : "s");
				}
				E.hk.pending_reg = 0;
				last_char = 0;
			} else {
				last_char = 'y';
				if (E.hk.pending_reg) editorSetStatusMessage("\"%c y", E.hk.pending_reg);
				else editorSetStatusMessage("y");
			}
			g_pressed = 0;
			break;

		case 'r':
			if (pane->cy < pane->numrows && pane->cx < pane->row[pane->cy].size) {
				int next_char = hkReadKeyBlocking();
				if (next_char != '\x1b' && !iscntrl(next_char)) {
					editorSaveState();
					editorRowDelChar(&pane->row[pane->cy], pane->cx);
					editorRowInsertChar(&pane->row[pane->cy], pane->cx, next_char);
				}
			}
			last_char = 0;
			g_pressed = 0;
			break;

		case 'J': {
			int n = hkConsumeCount();
			E.hk.last_op = 'J'; E.hk.last_count = n;
			editorSaveState();
			for (int k = 0; k < n; k++) {
				if (pane->cy < pane->numrows - 1) {
					pane->cx = pane->row[pane->cy].size;
					if (pane->row[pane->cy].size > 0 && pane->row[pane->cy + 1].size > 0) {
						editorInsertChar(' ');
					}
					editorRowAppendString(&pane->row[pane->cy], pane->row[pane->cy + 1].chars, pane->row[pane->cy + 1].size);
					editorDelRow(pane->cy + 1);
				}
			}
			last_char = 0; g_pressed = 0;
			break;
		}

		case 'w':
			if (d_pressed || last_char == 'c') {
				int was_change = (last_char == 'c');
				editorSaveState();
				if (pane->cy < pane->numrows) {
					erow *row = &pane->row[pane->cy];
					int start = pane->cx;
					int end = pane->cx;
					while (end < row->size && !is_separator(row->chars[end])) end++;
					while (end < row->size && is_separator(row->chars[end]) && row->chars[end] != '\n') end++;
					if (end > start) {
						hkSetRegister(E.hk.pending_reg, &row->chars[start], end - start, 0, 0);
						memmove(&row->chars[start], &row->chars[end], row->size - end + 1);
						row->size -= (end - start);
						editorUpdateRow(row);
						pane->dirty++;
						if (pane->cx >= row->size && pane->cx > 0) pane->cx = row->size > 0 ? row->size - 1 : 0;
					}
					if (was_change) {
						hkRecordStart('W', 1);
						editorSetMode(MODE_INSERT);
					} else {
						E.hk.last_op = 'w';
						E.hk.last_count = 1;
					}
				}
				E.hk.pending_reg = 0;
				last_char = 0;
				d_pressed = 0;
				g_pressed = 0;
				break;
			}
			{
				int n = hkConsumeCount();
				for (int k = 0; k < n; k++) {
					if (pane->cy >= pane->numrows) break;
					erow *row = &pane->row[pane->cy];
					while (pane->cx < row->size && !is_separator(row->chars[pane->cx])) pane->cx++;
					while (pane->cx < row->size && is_separator(row->chars[pane->cx])) pane->cx++;
					if (pane->cx >= row->size && pane->cy < pane->numrows - 1) {
						pane->cy++;
						pane->cx = 0;
					}
				}
			}
			last_char = 0;
			g_pressed = 0;
			break;

		case 'b': {
			int n = hkConsumeCount();
			for (int k = 0; k < n; k++) {
				if (pane->cx == 0 && pane->cy == 0) break;
				if (pane->cx == 0 && pane->cy > 0) {
					pane->cy--;
					pane->cx = pane->row[pane->cy].size;
				} else {
					erow *row = &pane->row[pane->cy];
					while (pane->cx > 0 && is_separator(row->chars[pane->cx - 1])) pane->cx--;
					while (pane->cx > 0 && !is_separator(row->chars[pane->cx - 1])) pane->cx--;
				}
			}
			last_char = 0;
			g_pressed = 0;
			break;
		}

		case '0':
		case HOME_KEY:
			pane->cx = 0;
			E.hk.pending_count = 0;
			last_char = 0;
			g_pressed = 0;
			break;

		case '$':
		case END_KEY:
			if (pane->cy < pane->numrows) {
				pane->cx = pane->row[pane->cy].size;
			}
			E.hk.pending_count = 0;
			last_char = 0;
			g_pressed = 0;
			break;

		case 'N': {
			int n = hkConsumeCount();
			if (E.search.query) {
				for (int k = 0; k < n; k++) editorFindPrev();
			} else {
				editorSetStatusMessage("No previous search");
			}
			last_char = 0; g_pressed = 0;
			break;
		}

		case CTRL_KEY('r'): {
			int n = hkConsumeCount();
			for (int k = 0; k < n; k++) editorRedo();
			break;
		}

		case CTRL_KEY('c'):
			editorCopyToSystemClipboard();
			break;

		case CTRL_KEY('v'):
			editorPasteFromSystemClipboard();
			break;

		case 'p': {
			int n = hkConsumeCount();
			pasteBuffer *pb = hkGetRegister(E.hk.pending_reg);
			E.hk.pending_reg = 0;
			if (pb && pb->data && pb->len > 0) {
				editorSaveState();
				for (int k = 0; k < n; k++) {
					if (pb->is_line_mode) {
						pane->cx = (pane->cy < pane->numrows) ? pane->row[pane->cy].size : 0;
						editorInsertNewLine();
						editorHandlePaste(pb->data, pb->len);
					} else {
						if (pane->cy < pane->numrows) {
							pane->cx++;
							if (pane->cx > pane->row[pane->cy].size) pane->cx = pane->row[pane->cy].size;
						}
						editorHandlePaste(pb->data, pb->len);
					}
				}
			} else {
				editorSetStatusMessage("Nothing to paste");
			}
			break;
		}

		case 'P': {
			int n = hkConsumeCount();
			pasteBuffer *pb = hkGetRegister(E.hk.pending_reg);
			E.hk.pending_reg = 0;
			if (pb && pb->data && pb->len > 0) {
				editorSaveState();
				for (int k = 0; k < n; k++) {
					if (pb->is_line_mode) {
						pane->cx = 0;
						editorInsertNewLine();
						pane->cy--;
						pane->cx = 0;
						editorHandlePaste(pb->data, pb->len);
					} else {
						editorHandlePaste(pb->data, pb->len);
					}
				}
			} else {
				editorSetStatusMessage("Nothing to paste");
			}
			break;
		}

		case ':':
			editorColonCommand();
			break;

		case '/':
			editorFind();
			break;

		case '\x1b':
			if (E.search.query) {
				for (int i = 0; i < pane->numrows; i++) {
					erow *row = &pane->row[i];
					for (int j = 0; j < row->rsize; j++) {
						if (row->hl[j] == HL_MATCH) {
							row->hl[j] = HL_NORMAL;
						}
					}
					editorUpdateSyntax(row);
				}
				editorSetStatusMessage("Search cleared");
			}
			E.hk.pending_count = 0;
			last_char = 0;
			g_pressed = 0;
			d_pressed = 0;
			break;

		case 'h':
		case 'j':
		case 'k':
		case 'l':
		case ARROW_LEFT:
		case ARROW_RIGHT:
		case ARROW_UP:
		case ARROW_DOWN: {
			int n = hkConsumeCount();
			for (int k = 0; k < n; k++) editorMoveCursor(c);
			last_char = 0; g_pressed = 0;
			break;
		}

		case 'd':
			if (last_char == 'd') {
				int n = hkConsumeCount();
				E.hk.last_op = 'd'; E.hk.last_count = n;
				editorSaveState();

				if (pane->cy < pane->numrows) {
					int end = pane->cy + n;
					if (end > pane->numrows) end = pane->numrows;
					int total = 0;
					for (int k = pane->cy; k < end; k++) total += pane->row[k].size + 1;
					char *buf = malloc(total + 1);
					int off = 0;
					for (int k = pane->cy; k < end; k++) {
						memcpy(buf + off, pane->row[k].chars, pane->row[k].size);
						off += pane->row[k].size;
						buf[off++] = '\n';
					}
					buf[off] = '\0';
					hkSetRegister(E.hk.pending_reg, buf, off, 1, 0);
					free(buf);

					int to_del = end - pane->cy;
					for (int k = 0; k < to_del; k++) editorDelRow(pane->cy);
				}
				E.hk.pending_reg = 0;

				if (pane->numrows == 0) {
					editorInsertRow(0, "", 0);
					pane->rowoff = pane->coloff = 0;
				}

				if (pane->cy >= pane->numrows) pane->cy = pane->numrows - 1;
				if (pane->cy < 0) pane->cy = 0;

				pane->cx = 0;

				editorSetStatusMessage("%d line%s deleted", n, n == 1 ? "" : "s");

				last_char = 0;
				d_pressed = 0;
			} else {
				last_char = 'd';
				d_pressed = 1;
			}
			g_pressed = 0;
			break;

		case 'c':
			if (last_char == 'c') {
				editorSaveState();
				if (pane->cy < pane->numrows) {
					erow *row = &pane->row[pane->cy];
					hkSetRegister(E.hk.pending_reg, row->chars, row->size, 0, 0);
					row->size = 0;
					row->chars[0] = '\0';
					editorUpdateRow(row);
					pane->cx = 0;
					pane->dirty++;
				}
				E.hk.pending_reg = 0;
				editorSetMode(MODE_INSERT);
				last_char = 0;
				d_pressed = 0;
			} else {
				last_char = 'c';
				d_pressed = 0;
			}
			g_pressed = 0;
			break;

		case CTRL_KEY('f'):
		case PAGE_DOWN:
			if (E.word_wrap && pane->wrap_lines) {
				int wrap_width = pane->width - E.line_number_width;
				if (wrap_width < 1) wrap_width = 1;
				
				int visual_rows = 0;
				for (int i = 0; i < pane->numrows; i++) {
					int lines = (pane->row[i].rsize + wrap_width - 1) / wrap_width;
					if (lines < 1) lines = 1;
					visual_rows += lines;
				}
				
				int old_rowoff = pane->rowoff;
				pane->rowoff = MIN(pane->rowoff + pane->height, MAX(0, visual_rows - pane->height));
				
				if (pane->rowoff != old_rowoff) {
					int current_visual = 0;
					for (int i = 0; i < pane->numrows; i++) {
						int lines = (pane->row[i].rsize + wrap_width - 1) / wrap_width;
						if (lines < 1) lines = 1;
						if (current_visual + lines > pane->rowoff) {
							pane->cy = i;
							pane->cx = 0;
							break;
						}
						current_visual += lines;
					}
				}
			} else {
				pane->rowoff = MIN(pane->rowoff + pane->height, MAX(0, pane->numrows - pane->height));
				pane->cy = MIN(pane->rowoff + pane->height - 1, pane->numrows - 1);
				if (pane->cy < pane->rowoff) pane->cy = pane->rowoff;
			}
			last_char = 0;
			g_pressed = 0;
			break;

		case CTRL_KEY('b'):
		case PAGE_UP:
			if (E.word_wrap && pane->wrap_lines) {
				int old_rowoff = pane->rowoff;
				pane->rowoff = MAX(0, pane->rowoff - pane->height);
				
				if (pane->rowoff != old_rowoff) {
					int wrap_width = pane->width - E.line_number_width;
					if (wrap_width < 1) wrap_width = 1;
					
					int current_visual = 0;
					for (int i = 0; i < pane->numrows; i++) {
						int lines = (pane->row[i].rsize + wrap_width - 1) / wrap_width;
						if (lines < 1) lines = 1;
						if (current_visual + lines > pane->rowoff + pane->height - 1) {
							pane->cy = i;
							pane->cx = 0;
							break;
						}
						current_visual += lines;
					}
				}
			} else {
				pane->rowoff = MAX(0, pane->rowoff - pane->height);
				pane->cy = pane->rowoff;
			}
			last_char = 0;
			g_pressed = 0;
			break;

		default:
			last_char = 0;
			g_pressed = 0;
			d_pressed = 0;
			E.hk.pending_count = 0;
			break;
		}
	} else if (E.mode == MODE_INSERT) {
		int should_save = 0;
		time_t now = time(NULL);
		
		if (now - last_undo_time > 2) should_save = 1;

		if (!E.is_pasting && editorInputPending()) {
			E.is_pasting = 1;
			editorSaveState();
			last_undo_time = now;
			should_save = 0;
		}

		if (E.is_pasting && !editorInputPending()) {
			E.is_pasting = 0;
		}

		switch (c) {
		case '\r':
			if (last_action != c) should_save = 1;
			if (should_save) {
				editorSaveState();
				last_undo_time = now;
			}
			editorInsertNewLine();
			hkRecordChar('\n');
			break;

		case BACKSPACE:
		case CTRL_KEY('h'):
		case DEL_KEY:
			if (last_action != c) should_save = 1;
			if (should_save) {
				editorSaveState();
				last_undo_time = now;
			}
			if (c == DEL_KEY) editorMoveCursor(ARROW_RIGHT);
			editorDelChar();
			hkRecordBackspace();
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
			editorSaveState();
			hkRecordStop();
			editorSetMode(MODE_NORMAL);
			break;

		default:
			if (!isprint(c) || (last_action != 'i' && isprint(last_action))) {
				should_save = 1;
			}
			if (should_save) {
				editorSaveState();
				last_undo_time = now;
			}
			editorInsertChar(c);
			hkRecordChar(c);
			break;
		}
		
		last_action = c;
	} else if (E.mode == MODE_VISUAL || E.mode == MODE_VISUAL_LINE) {
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

		case 'h':
		case ARROW_LEFT:
			if (E.mode == MODE_VISUAL) {
				editorMoveCursor(c);
			}
			break;

		case 'l':
		case ARROW_RIGHT:
			if (E.mode == MODE_VISUAL) {
				editorMoveCursor(c);
			}
			break;

		case 'w':
			if (pane->cy < pane->numrows) {
				erow *row = &pane->row[pane->cy];
				while (pane->cx < row->size && !is_separator(row->chars[pane->cx])) pane->cx++;
				while (pane->cx < row->size && is_separator(row->chars[pane->cx])) pane->cx++;
				if (pane->cx >= row->size && pane->cy < pane->numrows - 1) {
					pane->cy++;
					pane->cx = 0;
				}
			}
			break;

		case 'b':
			if (pane->cx > 0 || pane->cy > 0) {
				if (pane->cx == 0 && pane->cy > 0) {
					pane->cy--;
					pane->cx = pane->row[pane->cy].size;
				} else {
					erow *row = &pane->row[pane->cy];
					while (pane->cx > 0 && is_separator(row->chars[pane->cx - 1])) pane->cx--;
					while (pane->cx > 0 && !is_separator(row->chars[pane->cx - 1])) pane->cx--;
				}
			}
			break;

		case 'G':
			pane->cy = pane->numrows > 0 ? pane->numrows - 1 : 0;
			if (pane->cy < pane->numrows)
				pane->cx = pane->row[pane->cy].size;
			break;

		case 'g':
			pane->cy = 0;
			pane->cx = 0;
			break;

		case '0':
			pane->cx = 0;
			break;

		case '$':
			if (pane->cy < pane->numrows)
				pane->cx = pane->row[pane->cy].size;
			break;

		case CTRL_KEY('f'):
		case PAGE_DOWN:
			{
				int times = pane->height;
				while (times-- && pane->cy < pane->numrows - 1) {
					pane->cy++;
				}
			}
			break;

		case CTRL_KEY('b'):
		case PAGE_UP:
			{
				int times = pane->height;
				while (times-- && pane->cy > 0) {
					pane->cy--;
				}
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
			
		case CTRL_KEY('c'):
			editorYankVisualSelection();
			editorCopyToSystemClipboard();
			editorSetMode(MODE_NORMAL);
			break;
		}
	}
}

/*** splash screen ***/
void editorDrawSplash() {
	struct abuf ab = ABUF_INIT;

	abAppend(&ab, "\x1b[?25l", 6);
	abAppend(&ab, "\x1b[2J", 4);
	abAppend(&ab, "\x1b[H", 3);

	setThemeBgColor(&ab, E.theme.bg);

	for (int y = 0; y <= E.screenrows + 2; y++) {
		char buf[32];
		snprintf(buf, sizeof(buf), "\x1b[%d;1H", y + 1);
		abAppend(&ab, buf, strlen(buf));
		setThemeBgColor(&ab, E.theme.bg);
		for (int x = 0; x < E.screencols; x++) {
			abAppend(&ab, " ", 1);
		}
	}

	int center_y = (E.screenrows + 2) / 2 - 6;
	int center_x = E.screencols / 2;

	if (center_y < 1) center_y = 1;

	char buf[64];

	const char *logo[] = {
		"██╗  ██╗ █████╗ ██╗  ██╗ ██████╗",
		"██║  ██║██╔══██╗██║ ██╔╝██╔═══██╗",
		"███████║███████║█████╔╝ ██║   ██║",
		"██╔══██║██╔══██║██╔═██╗ ██║   ██║",
		"██║  ██║██║  ██║██║  ██╗╚██████╔╝",
		"╚═╝  ╚═╝╚═╝  ╚═╝╚═╝  ╚═╝ ╚═════╝"
	};

	int logo_width = 34;
	int logo_start = center_y;

	Color logo_color = E.theme.border;

	for (int i = 0; i < 6; i++) {
		int x_pos = center_x - logo_width / 2;
		if (x_pos < 0) x_pos = 0;

		snprintf(buf, sizeof(buf), "\x1b[%d;%dH", logo_start + i, x_pos + 1);
		abAppend(&ab, buf, strlen(buf));

		setThemeBgColor(&ab, E.theme.bg);
		setThemeColor(&ab, logo_color);
		abAppend(&ab, logo[i], strlen(logo[i]));
	}

	const char *kanji = "箱";
	int kanji_col = (E.screencols - 3) / 2;
	snprintf(buf, sizeof(buf), "\x1b[%d;%dH", center_y + 7, kanji_col + 1);
	abAppend(&ab, buf, strlen(buf));

	setThemeBgColor(&ab, E.theme.bg);
	Color kanji_color = E.theme.border;
	setThemeColor(&ab, kanji_color);
	abAppend(&ab, kanji, strlen(kanji));

	char version[80];
	snprintf(version, sizeof(version), "HAKO v%s", HAKO_VERSION);
	int ver_col = (E.screencols - strlen(version)) / 2;
	snprintf(buf, sizeof(buf), "\x1b[%d;%dH", center_y + 9, ver_col + 1);
	abAppend(&ab, buf, strlen(buf));

	setThemeBgColor(&ab, E.theme.bg);
	Color version_color = {130, 130, 130};
	setThemeColor(&ab, version_color);
	abAppend(&ab, version, strlen(version));

	const char *subtitle = "minimal text editor";
	int sub_col = (E.screencols - strlen(subtitle)) / 2;
	snprintf(buf, sizeof(buf), "\x1b[%d;%dH", center_y + 10, sub_col + 1);
	abAppend(&ab, buf, strlen(buf));

	setThemeBgColor(&ab, E.theme.bg);
	Color subtitle_color = {100, 100, 100};
	setThemeColor(&ab, subtitle_color);
	abAppend(&ab, subtitle, strlen(subtitle));

	/* # NEW + # TIPS — only render if screen has the vertical room. */
	if (E.screenrows >= center_y + 20) {
		static const char *HK_NEW[] = {
			"Windows build link fix (detectTerminalType)",
			"iSh hardening: bounded skill loader, gridResize cap",
			"Auto-resume hint in status bar",
			NULL
		};
		static const char *HK_TIPS[] = {
			":help opens the full command list",
			":q rei|kami closes a side pane from any focused pane",
			":theme <name> swaps theme at runtime (try gruvbox-dark)",
			"/trust grants the AI panel file-op access",
			"Ctrl-W cycles focus between editor / explorer / AI",
			"gg/G + Ctrl-D/U scroll AI history in visual mode",
			NULL
		};

		Color label_color = {180, 180, 180};
		Color bullet_color = E.theme.border;
		Color body_color = {130, 130, 130};

		const char *new_label = "# NEW";
		int nl_col = (E.screencols - (int)strlen(new_label)) / 2;
		snprintf(buf, sizeof(buf), "\x1b[%d;%dH", center_y + 12, nl_col + 1);
		abAppend(&ab, buf, strlen(buf));
		setThemeBgColor(&ab, E.theme.bg);
		setThemeColor(&ab, label_color);
		abAppend(&ab, new_label, strlen(new_label));

		for (int i = 0; i < 2 && HK_NEW[i]; i++) {
			char line[160];
			snprintf(line, sizeof(line), "· %s", HK_NEW[i]);
			int col = (E.screencols - (int)strlen(line)) / 2;
			snprintf(buf, sizeof(buf), "\x1b[%d;%dH", center_y + 13 + i, col + 1);
			abAppend(&ab, buf, strlen(buf));
			setThemeBgColor(&ab, E.theme.bg);
			setThemeColor(&ab, bullet_color);
			abAppend(&ab, "·", strlen("·"));
			setThemeColor(&ab, body_color);
			abAppend(&ab, line + strlen("·"), strlen(line) - strlen("·"));
		}

		/* Rotate tips by day-of-year so the splash refreshes daily. */
		int total = 0; while (HK_TIPS[total]) total++;
		time_t now = time(NULL);
		struct tm *tm = localtime(&now);
		int seed = tm ? tm->tm_yday : 0;
		int t0 = total ? (seed % total) : 0;
		int t1 = total ? ((seed + 1) % total) : 0;

		const char *tip_label = "# TIPS";
		int tl_col = (E.screencols - (int)strlen(tip_label)) / 2;
		snprintf(buf, sizeof(buf), "\x1b[%d;%dH", center_y + 16, tl_col + 1);
		abAppend(&ab, buf, strlen(buf));
		setThemeBgColor(&ab, E.theme.bg);
		setThemeColor(&ab, label_color);
		abAppend(&ab, tip_label, strlen(tip_label));

		int pick[2] = {t0, t1};
		for (int i = 0; i < 2; i++) {
			char line[160];
			snprintf(line, sizeof(line), "· %s", HK_TIPS[pick[i]]);
			int col = (E.screencols - (int)strlen(line)) / 2;
			snprintf(buf, sizeof(buf), "\x1b[%d;%dH", center_y + 17 + i, col + 1);
			abAppend(&ab, buf, strlen(buf));
			setThemeBgColor(&ab, E.theme.bg);
			setThemeColor(&ab, bullet_color);
			abAppend(&ab, "·", strlen("·"));
			setThemeColor(&ab, body_color);
			abAppend(&ab, line + strlen("·"), strlen(line) - strlen("·"));
		}
	}

	const char *instruction = "Press any key to start";
	int instr_col = (E.screencols - strlen(instruction)) / 2;
	snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.screenrows, instr_col + 1);
	abAppend(&ab, buf, strlen(buf));

	setThemeBgColor(&ab, E.theme.bg);
	Color dim_color = {80, 80, 80};
	setThemeColor(&ab, dim_color);
	abAppend(&ab, "\x1b[5m", 4);
	abAppend(&ab, instruction, strlen(instruction));
	abAppend(&ab, "\x1b[25m", 5);

	abAppend(&ab, "\x1b[0m", 4);
	
	snprintf(buf, sizeof(buf), "\x1b[%d;1H", E.screenrows + 2);
	abAppend(&ab, buf, strlen(buf));
	
	write(STDOUT_FILENO, ab.b, ab.len);
	abFree(&ab);
}

/*** explorer ***/
int explorerCompare(const void *a, const void *b) {
	const char *sa = *(const char **)a;
	const char *sb = *(const char **)b;
	int a_dir = (strchr(sa, '/') != NULL);
	int b_dir = (strchr(sb, '/') != NULL);
	if (strcmp(sa, "../") == 0) return -1;
	if (strcmp(sb, "../") == 0) return 1;
	if (a_dir != b_dir) return b_dir - a_dir;
	return strcasecmp(sa, sb);
}

void explorerInit(editorPane *pane) {
	explorerData *data = calloc(1, sizeof(explorerData));
	if (!data) return;

	data->current_dir = getcwd(NULL, 0);
	data->selected = 0;
	data->scroll_offset = 0;
	data->entries = NULL;
	data->num_entries = 0;
	pane->explorer = data;
	
	explorerRefresh(pane);
}

void explorerRefresh(editorPane *pane) {
	explorerData *data = pane->explorer;
	if (!data) return;

	for (int i = 0; i < data->num_entries; i++) {
		free(data->entries[i]);
	}
	free(data->entries);
	data->entries = NULL;
	data->num_entries = 0;

	DIR *dir = opendir(data->current_dir);
	if (!dir) return;

	struct dirent *ent;
	while ((ent = readdir(dir))) {
		if (!E.explorer_show_hidden && ent->d_name[0] == '.' && strcmp(ent->d_name, "..") != 0) continue;
		if (strcmp(ent->d_name, ".") == 0) continue;
		data->num_entries++;
	}

	if (data->num_entries == 0) {
		closedir(dir);
		return;
	}

	data->entries = malloc(sizeof(char*) * data->num_entries);
	rewinddir(dir);

	int i = 0;
	while ((ent = readdir(dir)) && i < data->num_entries) {
		if (!E.explorer_show_hidden && ent->d_name[0] == '.' && strcmp(ent->d_name, "..") != 0) continue;
		if (strcmp(ent->d_name, ".") == 0) continue;

		char full_path[PATH_MAX];
		snprintf(full_path, sizeof(full_path), "%s/%s", data->current_dir, ent->d_name);

		struct stat st;
		int is_dir = 0;
		if (stat(full_path, &st) == 0) {
			is_dir = S_ISDIR(st.st_mode);
		}

		size_t bufsz = strlen(ent->d_name) + 4;
		data->entries[i] = malloc(bufsz);
		if (is_dir) {
			snprintf(data->entries[i], bufsz, "%s/", ent->d_name);
		} else {
			snprintf(data->entries[i], bufsz, "%s", ent->d_name);
		}
		i++;
	}

	closedir(dir);

	qsort(data->entries, data->num_entries, sizeof(char*), explorerCompare);

	if (data->selected >= data->num_entries) {
		data->selected = data->num_entries > 0 ? data->num_entries - 1 : 0;
	}
	
	if (data->selected < data->scroll_offset) {
		data->scroll_offset = data->selected;
	}
	if (data->selected >= data->scroll_offset + pane->height - 2) {
		data->scroll_offset = data->selected - pane->height + 3;
	}
}

void explorerRender(editorPane *pane, struct abuf *ab) {
	(void)ab;
	explorerData *data = pane->explorer;
	if (!data) return;

	for (int y = 0; y < pane->height; y++) {
		int gy = pane->y + y;
		int gx = pane->x;

		if (y == 0) {
			Color hf = E.theme.status_fg;
			Color hb = E.theme.status_bg;
			char header[64];
			snprintf(header, sizeof(header), " %s %s ", E.explorer_kanji, E.explorer_name);
			int used = gridPutStr(gx, gy, header, pane->width, hf, hb);
			for (int x = used; x < pane->width; x++) {
				gridPutCh(gx + x, gy, ' ', hf, hb);
			}
		} else if (y == 1) {
			int used = 0;
			while (used < pane->width) {
				int w = gridPutStr(gx + used, gy, "─", pane->width - used, E.theme.border, E.theme.bg);
				if (w <= 0) break;
				used += w;
			}
		} else {
			int idx = (y - 2) + data->scroll_offset;
			if (idx < data->num_entries && data->entries && data->entries[idx]) {
				Color row_fg, row_bg;
				if (idx == data->selected) {
					row_fg = E.theme.visual_fg;
					row_bg = E.theme.visual_bg;
				} else {
					int is_dir = (strchr(data->entries[idx], '/') != NULL);
					row_fg = is_dir ? E.theme.keyword2 : E.theme.fg;
					row_bg = E.theme.bg;
				}
				gridPutCh(gx, gy, ' ', row_fg, row_bg);
				int name_len = strlen(data->entries[idx]);
				int max = pane->width - 2;
				if (name_len > max) name_len = max;
				int used = gridPutStr(gx + 1, gy, data->entries[idx], max, row_fg, row_bg);
				for (int x = 1 + used; x < pane->width; x++) {
					gridPutCh(gx + x, gy, ' ', row_fg, row_bg);
				}
			} else {
				for (int x = 0; x < pane->width; x++) {
					gridPutCh(gx + x, gy, ' ', E.theme.fg, E.theme.bg);
				}
			}
		}
	}
}

void explorerHandleKey(editorPane *pane, int key) {
	explorerData *data = pane->explorer;
	if (!data) return;

	switch (key) {
	case 'j':
	case ARROW_DOWN:
		if (data->selected < data->num_entries - 1) {
			data->selected++;
			if (data->selected >= data->scroll_offset + pane->height - 2) {
				data->scroll_offset++;
			}
		}
		break;

	case 'k':
	case ARROW_UP:
		if (data->selected > 0) {
			data->selected--;
			if (data->selected < data->scroll_offset) {
				data->scroll_offset--;
			}
		}
		break;

	case 'g':
		data->selected = 0;
		data->scroll_offset = 0;
		break;

	case 'G':
		if (data->num_entries > 0) {
			data->selected = data->num_entries - 1;
			data->scroll_offset = MAX(0, data->num_entries - pane->height + 2);
		}
		break;

	case ':':
		editorColonCommand();
		break;

	case '\r':
	case 'l':
	case ARROW_RIGHT:
		if (data->num_entries > 0 && data->entries && data->entries[data->selected]) {
			char full_path[PATH_MAX];
			snprintf(full_path, sizeof(full_path), "%s/%s", data->current_dir, data->entries[data->selected]);
			
			if (strchr(data->entries[data->selected], '/')) {
#ifdef _WIN32
				char rd[PATH_MAX];
				char *new_dir = _fullpath(rd, full_path, PATH_MAX) ? strdup(rd) : NULL;
#else
				char *new_dir = realpath(full_path, NULL);
#endif
				if (new_dir) {
					free(data->current_dir);
					data->current_dir = new_dir;
					data->selected = 0;
					data->scroll_offset = 0;
					explorerRefresh(pane);
				}
			} else {
				editorPane *target = NULL;
				editorPane **panes = NULL;
				int count = 0;
				editorCollectLeafPanes(E.root_pane, &panes, &count);
				
				for (int i = 0; i < count; i++) {
					if (panes[i] && panes[i]->type == PANE_EDITOR) {
						target = panes[i];
						break;
					}
				}
				free(panes);
				
				if (target) {
					E.active_pane = target;
					
					for (int i = 0; i < target->numrows; i++) {
						editorFreeRow(&target->row[i]);
					}
					free(target->row);
					target->row = NULL;
					target->numrows = 0;
					target->cx = target->cy = 0;
					target->rowoff = target->coloff = 0;
					
					editorOpen(full_path);
					editorSetStatusMessage("Opened: %s", full_path);
				}
			}
		}
		break;

	case 'h':
	case ARROW_LEFT:
		if (strcmp(data->current_dir, "/") != 0) {
			char *parent = strdup(data->current_dir);
			char *last_slash = strrchr(parent, '/');
			if (last_slash && last_slash != parent) {
				*last_slash = '\0';
			} else {
				strcpy(parent, "/");
			}
			free(data->current_dir);
			data->current_dir = parent;
			data->selected = 0;
			data->scroll_offset = 0;
			explorerRefresh(pane);
		}
		break;

	case 'o':
		if (data->num_entries > 0 && data->entries && data->entries[data->selected] &&
			!strchr(data->entries[data->selected], '/')) {
			char full_path[PATH_MAX];
			snprintf(full_path, sizeof(full_path), "%s/%s", data->current_dir, data->entries[data->selected]);

			editorPane *target = NULL;
			editorPane **panes = NULL;
			int count = 0;
			editorCollectLeafPanes(E.root_pane, &panes, &count);
			for (int i = 0; i < count; i++) {
				if (panes[i] && panes[i]->type == PANE_EDITOR) { target = panes[i]; break; }
			}
			free(panes);
			if (!target) {
				editorSetStatusMessage("No editor pane available");
				break;
			}

			if (target->width < 40) {
				editorSetStatusMessage("Not enough room to split — use <Enter> to open in current pane");
				break;
			}

			int prev_num_panes = E.num_panes;
			E.active_pane = target;
			editorSplitPane(1);
			if (E.num_panes == prev_num_panes) {
				break;
			}

			editorPane *np = E.active_pane;
			for (int i = 0; i < np->numrows; i++) editorFreeRow(&np->row[i]);
			free(np->row);
			np->row = NULL;
			np->numrows = 0;
			np->cx = np->cy = np->rowoff = np->coloff = 0;
			free(np->filename);
			np->filename = NULL;
			np->dirty = 0;

			editorOpen(full_path);
			editorSetStatusMessage("Opened in new pane: %s", full_path);
		}
		break;

	case '.':
		E.explorer_show_hidden = !E.explorer_show_hidden;
		explorerRefresh(pane);
		editorSetStatusMessage("Hidden files: %s", E.explorer_show_hidden ? "shown" : "hidden");
		break;

	case 'r':
		explorerRefresh(pane);
		editorSetStatusMessage("Explorer refreshed");
		break;

	case 'q':
	case '\x1b':
		editorToggleExplorer();
		break;
	}
}

void explorerCleanup(editorPane *pane) {
	explorerData *data = pane->explorer;
	if (!data) return;

	free(data->current_dir);
	for (int i = 0; i < data->num_entries; i++) {
		free(data->entries[i]);
	}
	free(data->entries);
	free(data);
	pane->explorer = NULL;
}

void editorToggleExplorer() {
	if (E.left_panel && E.left_panel->type == PANE_EXPLORER) {
		if (E.active_pane == E.left_panel) {
			editorPane **leaves = NULL;
			int lcount = 0;
			editorCollectLeafPanes(E.root_pane, &leaves, &lcount);
			if (lcount > 0) {
				E.active_pane = leaves[0];
				E.active_pane->is_focused = 1;
			} else if (E.right_panel) {
				E.active_pane = E.right_panel;
				E.active_pane->is_focused = 1;
			} else {
				E.active_pane = NULL;
			}
			free(leaves);
		}
		editorFreePane(E.left_panel);
		E.left_panel = NULL;

		editorResizePanes();
		return;
	}
	
	if (E.left_panel && E.left_panel->type == PANE_AI) {
		editorSetStatusMessage("AI panel is on the left. Close it first or move it");
		return;
	}
	
	E.left_panel_width = E.explorer_width > 0 ? E.explorer_width : 30;
	E.left_panel = editorCreatePane(PANE_EXPLORER, 0, 0, E.left_panel_width, E.screenrows);
	E.left_panel->wrap_lines = 0;
	explorerInit(E.left_panel);

	editorResizePanes();
	
	editorSetStatusMessage("Explorer opened");
}

/*** AI assistant ***/
void editorInitAI() {
	E.ai_provider_type = AI_PROVIDER_NONE;
	E.ai_provider_count = 0;
	E.current_ai = NULL;
	E.ai_api_key = NULL;
	E.ai_endpoint = NULL;
	E.ai_model = NULL;
	E.ai_temperature = 70;
	E.ai_max_tokens = 2048;
	E.ai_tools_enabled = 1;
	E.ai_stream = 1;
	E.ai_autowrite = 1;
	E.session_id = NULL;
	E.session_started = 0;
	E.session_last_used = 0;
	E.session_turn_count = 0;
	E.session_resumed = 0;
}

void editorCleanupAI() {
	for (int i = 0; i < E.ai_provider_count; i++) {
		free(E.ai_providers[i]->name);
		free(E.ai_providers[i]->endpoint);
		free(E.ai_providers[i]->api_key);
		free(E.ai_providers[i]->model);
		free(E.ai_providers[i]);
	}
	free(E.ai_api_key);
	free(E.ai_endpoint);
	free(E.ai_model);
}

void aiInit(editorPane *pane) {
	aiData *data = calloc(1, sizeof(aiData));
	if (!data) return;
	
	data->history = malloc(sizeof(char*) * AI_HISTORY_MAX);
	data->history_role = calloc(AI_HISTORY_MAX, 1);
	data->history_count = 0;
	data->history_pos = 0;
	data->scroll_offset = 0;
	data->cursor_x = 0;
	data->cursor_y = 0;
	data->mode = MODE_NORMAL;
	data->visual_mode = 0;
	data->visual_start = -1;
	data->visual_end = -1;
	data->prompt_buffer = malloc(256);
	data->prompt_buffer[0] = '\0';
	data->prompt_capacity = 256;
	data->prompt_len = 0;
	data->prompt_cursor = 0;
	data->current_prompt = NULL;
	data->current_response = NULL;
	data->active = 1;
	data->streaming = 0;
	pthread_mutex_init(&data->lock, NULL);
	
	const char *default_mascot[] = {
    	"  ▄█████▄ ",
  		" ██ ███ ██",
   		" █████████",
   		" ▀█▀▀█▀▀█▀",
		NULL
	};
	for (int i = 0; default_mascot[i]; i++) {
		if (data->history_count < AI_HISTORY_MAX) {
			data->history[data->history_count++] = strdup(default_mascot[i]);
		}
	}

	const char *pname = hkProviderName(E.ai_provider_type);

	data->history[data->history_count++] = strdup("-----");
	char line[128];
	snprintf(line, sizeof(line), "%s v1.1", E.ai_name);
	data->history[data->history_count++] = strdup(line);
	snprintf(line, sizeof(line), "provider: %s", pname);
	data->history[data->history_count++] = strdup(line);
	if (E.ai_model) {
		snprintf(line, sizeof(line), "model: %s", E.ai_model);
		data->history[data->history_count++] = strdup(line);
	}
	if (E.ai_provider_type == AI_PROVIDER_NONE) {
		data->history[data->history_count++] = strdup("(set ai_provider in .hakorc)");
	}
	snprintf(line, sizeof(line), "trust: %s", hkProjectTrusted() ? "granted" : "off");
	data->history[data->history_count++] = strdup(line);
	if (E.session_resumed) {
		long ago = (long)time(NULL) - E.session_last_used;
		const char *unit;
		long n;
		if (ago < 3600) { n = ago / 60; unit = "m"; }
		else if (ago < 86400) { n = ago / 3600; unit = "h"; }
		else { n = ago / 86400; unit = "d"; }
		snprintf(line, sizeof(line), "session: resumed (%ld%s, %d turns)", n, unit, E.session_turn_count);
	} else {
		snprintf(line, sizeof(line), "session: new");
	}
	data->history[data->history_count++] = strdup(line);
	data->history[data->history_count++] = strdup("-----");
	data->history[data->history_count++] = strdup("'i' chat | /help");

	pane->ai = data;

	hkLoadHistoryTail(data, 40);
	int sk = hkLoadSkills(data);
	if (sk > 0) {
		char msg[64];
		snprintf(msg, sizeof(msg), "loaded %d skill(s)", sk);
		data->history[data->history_count++] = strdup(msg);
	}
}

static int aiSubmitPrompt(aiData *data) {
	if (!data || data->prompt_len == 0) return 0;
	if (data->prompt_buffer[0] == '/') {
		int r = hkHandleSlash(data, data->prompt_buffer);
		data->prompt_buffer[0] = '\0';
		data->prompt_len = 0;
		data->prompt_cursor = 0;
		data->mode = MODE_NORMAL;
		data->history_pos = MAX(0, data->history_count - 20);
		editorSetStatusMessage("-- AI NORMAL --");
		return r;
	}
	aiAddHistoryRole(data, data->prompt_buffer, HK_ROLE_USER);
	hkLogMessage("user", data->prompt_buffer);
	E.session_turn_count++;
	hkSaveSession();

	free(data->current_prompt);
	data->current_prompt = strdup(data->prompt_buffer);

	data->prompt_buffer[0] = '\0';
	data->prompt_len = 0;
	data->mode = MODE_NORMAL;

	if (E.ai_provider_type == AI_PROVIDER_NONE) {
		aiAddHistory(data, "Set ai_provider in .hakorc");
		aiAddHistory(data, "(ollama/anthropic/openai)");
	} else if (data->streaming) {
		aiAddHistory(data, "Waiting for response...");
	} else {
		aiAddHistory(data, "...");
		data->history_pos = MAX(0, data->history_count - 20);
		aiWorkerSend(data);
	}
	editorSetStatusMessage("-- AI NORMAL --");
	return 1;
}

void aiHandleKey(editorPane *pane, int key) {
	aiData *data = pane->ai;
	if (!data) return;

	pthread_mutex_lock(&data->lock);

	if (data->mode == MODE_INSERT) {
		if (data->prompt_cursor < 0) data->prompt_cursor = 0;
		if (data->prompt_cursor > data->prompt_len) data->prompt_cursor = data->prompt_len;
		switch (key) {
		case '\x1b':
			data->mode = MODE_NORMAL;
			editorSetStatusMessage("-- AI NORMAL --");
			break;

		case BACKSPACE:
		case CTRL_KEY('h'):
			if (data->prompt_cursor > 0) {
				memmove(data->prompt_buffer + data->prompt_cursor - 1,
					data->prompt_buffer + data->prompt_cursor,
					data->prompt_len - data->prompt_cursor + 1);
				data->prompt_cursor--;
				data->prompt_len--;
			}
			break;

		case DEL_KEY:
			if (data->prompt_cursor < data->prompt_len) {
				memmove(data->prompt_buffer + data->prompt_cursor,
					data->prompt_buffer + data->prompt_cursor + 1,
					data->prompt_len - data->prompt_cursor);
				data->prompt_len--;
			}
			break;

		case ARROW_LEFT:
			if (data->prompt_cursor > 0) data->prompt_cursor--;
			break;
		case ARROW_RIGHT:
			if (data->prompt_cursor < data->prompt_len) data->prompt_cursor++;
			break;
		case ARROW_UP: {
			int line_start = data->prompt_cursor;
			while (line_start > 0 && data->prompt_buffer[line_start - 1] != '\n') line_start--;
			int col = data->prompt_cursor - line_start;
			if (line_start == 0) break;
			int prev_end = line_start - 1;
			int prev_start = prev_end;
			while (prev_start > 0 && data->prompt_buffer[prev_start - 1] != '\n') prev_start--;
			int prev_len = prev_end - prev_start;
			data->prompt_cursor = prev_start + (col < prev_len ? col : prev_len);
			break;
		}
		case ARROW_DOWN: {
			int line_start = data->prompt_cursor;
			while (line_start > 0 && data->prompt_buffer[line_start - 1] != '\n') line_start--;
			int col = data->prompt_cursor - line_start;
			int line_end = data->prompt_cursor;
			while (line_end < data->prompt_len && data->prompt_buffer[line_end] != '\n') line_end++;
			if (line_end >= data->prompt_len) break;
			int next_start = line_end + 1;
			int next_end = next_start;
			while (next_end < data->prompt_len && data->prompt_buffer[next_end] != '\n') next_end++;
			int next_len = next_end - next_start;
			data->prompt_cursor = next_start + (col < next_len ? col : next_len);
			break;
		}
		case HOME_KEY: {
			int ls = data->prompt_cursor;
			while (ls > 0 && data->prompt_buffer[ls - 1] != '\n') ls--;
			data->prompt_cursor = ls;
			break;
		}
		case END_KEY: {
			int le = data->prompt_cursor;
			while (le < data->prompt_len && data->prompt_buffer[le] != '\n') le++;
			data->prompt_cursor = le;
			break;
		}

		case '\r':
		case '\n':
			if (data->prompt_len >= data->prompt_capacity - 1) {
				data->prompt_capacity *= 2;
				data->prompt_buffer = realloc(data->prompt_buffer, data->prompt_capacity);
			}
			memmove(data->prompt_buffer + data->prompt_cursor + 1,
				data->prompt_buffer + data->prompt_cursor,
				data->prompt_len - data->prompt_cursor + 1);
			data->prompt_buffer[data->prompt_cursor++] = '\n';
			data->prompt_len++;
			break;

		default:
			if (!iscntrl(key) && key < 128) {
				if (data->prompt_len >= data->prompt_capacity - 1) {
					data->prompt_capacity *= 2;
					data->prompt_buffer = realloc(data->prompt_buffer, data->prompt_capacity);
				}
				memmove(data->prompt_buffer + data->prompt_cursor + 1,
					data->prompt_buffer + data->prompt_cursor,
					data->prompt_len - data->prompt_cursor + 1);
				data->prompt_buffer[data->prompt_cursor++] = key;
				data->prompt_len++;
			}
			break;
		}
	} else if (data->visual_mode) {
		switch (key) {
		case '\x1b':
			data->visual_mode = 0;
			data->visual_start = data->visual_end = -1;
			editorSetStatusMessage("-- AI NORMAL --");
			break;
			
		case 'j':
		case ARROW_DOWN:
			if (data->cursor_y < data->history_count - 1) {
				data->cursor_y++;
				data->visual_end = data->cursor_y;
			}
			break;
			
		case 'k':
		case ARROW_UP:
			if (data->cursor_y > 0) {
				data->cursor_y--;
				data->visual_end = data->cursor_y;
			}
			break;
			
		case 'y':
			{
				int start = MIN(data->visual_start, data->visual_end);
				int end = MAX(data->visual_start, data->visual_end);
				
				size_t total_len = 0;
				for (int i = start; i <= end && i < data->history_count; i++) {
					total_len += strlen(data->history[i]) + 1;
				}
				
				if (total_len > 0) {
					char *yanked = malloc(total_len + 1);
					char *p = yanked;
					
					for (int i = start; i <= end && i < data->history_count; i++) {
						int len = strlen(data->history[i]);
						memcpy(p, data->history[i], len);
						p += len;
						*p++ = '\n';
					}
					*p = '\0';
					
					editorSetPasteBuffer(yanked, p - yanked, 1);
					free(yanked);
					editorSetStatusMessage("Yanked %d lines", end - start + 1);
				}
				
				data->visual_mode = 0;
				data->visual_start = data->visual_end = -1;
			}
			break;
			
		case CTRL_KEY('w'):
			data->visual_mode = 0;
			pthread_mutex_unlock(&data->lock);
			editorNextPane();
			return;

		case 'g':
			data->cursor_y = 0;
			data->visual_end = 0;
			data->scroll_offset = 1000000;
			break;

		case 'G':
			data->cursor_y = data->history_count > 0 ? data->history_count - 1 : 0;
			data->visual_end = data->cursor_y;
			data->scroll_offset = 0;
			break;

		case CTRL_KEY('d'):
		case CTRL_KEY('f'): {
			int step = pane->height / 2;
			if (step < 1) step = 1;
			data->cursor_y += step;
			if (data->cursor_y > data->history_count - 1)
				data->cursor_y = data->history_count > 0 ? data->history_count - 1 : 0;
			data->visual_end = data->cursor_y;
			data->scroll_offset -= step;
			if (data->scroll_offset < 0) data->scroll_offset = 0;
			break;
		}

		case CTRL_KEY('u'):
		case CTRL_KEY('b'): {
			int step = pane->height / 2;
			if (step < 1) step = 1;
			data->cursor_y -= step;
			if (data->cursor_y < 0) data->cursor_y = 0;
			data->visual_end = data->cursor_y;
			data->scroll_offset += step;
			break;
		}

		case 'V':
			data->visual_start = data->visual_end = data->cursor_y;
			break;

		case CTRL_KEY('c'):
			data->visual_mode = 0;
			data->visual_start = data->visual_end = -1;
			editorSetStatusMessage("-- AI NORMAL --");
			break;
		}
	} else {
		switch (key) {
		case 'i':
			data->mode = MODE_INSERT;
			editorSetStatusMessage("-- AI INSERT -- (Esc then Enter to send)");
			break;

		case 'v':
			data->visual_mode = 1;
			data->visual_start = data->visual_end = data->cursor_y;
			editorSetStatusMessage("-- AI VISUAL --");
			break;

		case '\r':
		case '\n':
			if (data->prompt_len > 0) {
				int r = aiSubmitPrompt(data);
				if (r == 2) {
					pthread_mutex_unlock(&data->lock);
					editorToggleAI();
					return;
				}
			}
			break;
			
		case 'j':
		case ARROW_DOWN:
			if (data->scroll_offset > 0) data->scroll_offset--;
			break;

		case 'k':
		case ARROW_UP:
			data->scroll_offset++;
			break;

		case CTRL_KEY('d'):
			data->scroll_offset -= pane->height / 2;
			if (data->scroll_offset < 0) data->scroll_offset = 0;
			break;

		case CTRL_KEY('u'):
			data->scroll_offset += pane->height / 2;
			break;
			
		case 'h':
		case ARROW_LEFT:
			if (data->cursor_x > 0) {
				data->cursor_x--;
			}
			break;
			
		case 'l':
		case ARROW_RIGHT:
			if (data->cursor_y < data->history_count) {
				int len = strlen(data->history[data->cursor_y]);
				if (data->cursor_x < len - 1) {
					data->cursor_x++;
				}
			}
			break;
			
		case 'g':
			data->scroll_offset = 1000000;
			break;

		case 'G':
			data->scroll_offset = 0;
			break;
			
		case ':':
			{
				pthread_mutex_unlock(&data->lock);
				char *cmd = editorPrompt(":%s", NULL);
				pthread_mutex_lock(&data->lock);
				if (cmd) {
					if (strcmp(cmd, "q") == 0) {
						free(cmd);
						pthread_mutex_unlock(&data->lock);
						editorToggleAI();
						return;
					} else if (strcmp(cmd, "w") == 0 || strcmp(cmd, "wq") == 0) {
						FILE *fp = fopen("ai_chat.txt", "w");
						if (fp) {
							for (int i = 0; i < data->history_count; i++) {
								fprintf(fp, "%s\n", data->history[i]);
							}
							fclose(fp);
							editorSetStatusMessage("AI chat saved to ai_chat.txt");
						}
						if (strcmp(cmd, "wq") == 0) {
							free(cmd);
							pthread_mutex_unlock(&data->lock);
							editorToggleAI();
							return;
						}
					} else if (strcmp(cmd, "clear") == 0) {
						for (int i = 0; i < data->history_count; i++) {
							free(data->history[i]);
						}
						data->history_count = 0;
						data->cursor_x = data->cursor_y = 0;
						data->history_pos = 0;
						editorSetStatusMessage("AI history cleared");
					}
					free(cmd);
				}
			}
			break;
			
		case CTRL_KEY('w'):
			pthread_mutex_unlock(&data->lock);
			editorNextPane();
			return;

		case CTRL_KEY('c'):
			pthread_mutex_unlock(&data->lock);
			editorToggleAI();
			return;
		}
	}

	pthread_mutex_unlock(&data->lock);
}

void editorToggleAI() {
	if (E.right_panel && E.right_panel->type == PANE_AI) {
		if (E.active_pane == E.right_panel) {
			editorPane **leaves = NULL;
			int lcount = 0;
			editorCollectLeafPanes(E.root_pane, &leaves, &lcount);
			if (lcount > 0) {
				E.active_pane = leaves[0];
				E.active_pane->is_focused = 1;
			} else if (E.left_panel) {
				E.active_pane = E.left_panel;
				E.active_pane->is_focused = 1;
			} else {
				E.active_pane = NULL;
			}
			free(leaves);
		}
		editorFreePane(E.right_panel);
		E.right_panel = NULL;

		editorResizePanes();
		return;
	}
	
	if (!hkProjectTrusted()) {
		char cwd[PATH_MAX];
		if (getcwd(cwd, sizeof(cwd))) {
			char q[PATH_MAX + 64];
			snprintf(q, sizeof(q), "Trust %s for Rei file ops? (y/n/s=skip): %%s", cwd);
			char *ans = editorPrompt(q, NULL);
			if (ans) {
				if (ans[0] == 'y' || ans[0] == 'Y') {
					if (hkGrantProjectTrust()) {
						editorSetStatusMessage("Trusted. Rei may edit files here.");
					} else {
						editorSetStatusMessage("Could not create .hako/");
					}
				} else if (ans[0] == 'n' || ans[0] == 'N') {
					editorSetStatusMessage("Rei read-only in this dir.");
				}
				free(ans);
			}
		}
	}

	E.right_panel_width = E.explorer_width > 0 ? E.explorer_width : 30;
	E.right_panel = editorCreatePane(PANE_AI, E.screencols - E.right_panel_width, 0, E.right_panel_width, E.screenrows);
	E.right_panel->wrap_lines = 1;
	aiInit(E.right_panel);

	editorResizePanes();
	
	editorSetStatusMessage("Rei Assistant - 'i' for insert, ESC for normal, 'v' for visual");
}

void editorSendToAI(const char *prompt) {
	editorPane **panes = NULL;
	int count = 0;
	editorCollectLeafPanes(E.root_pane, &panes, &count);
	
	for (int i = 0; i < count; i++) {
		if (panes[i]->type == PANE_AI && panes[i]->ai) {
			aiData *data = panes[i]->ai;
			pthread_mutex_lock(&data->lock);
			
			if (data->history_count < AI_HISTORY_MAX) {
				data->history_role[data->history_count] = HK_ROLE_USER;
				data->history[data->history_count] = strdup(prompt);
				data->history_count++;
			}
			
			free(data->current_prompt);
			data->current_prompt = strdup(prompt);
			free(data->current_response);
			data->current_response = strdup("AI response would appear here");
			
			pthread_mutex_unlock(&data->lock);
			break;
		}
	}
	
	free(panes);
}

void aiRender(editorPane *pane, struct abuf *ab) {
	if (!pane || !pane->ai) return;

	aiData *data = pane->ai;

	pthread_mutex_lock(&data->lock);

	int inner_w = pane->width - 1 - 2;
	if (inner_w < 4) inner_w = 4;
	int seg_w = inner_w - 2;
	if (seg_w < 2) seg_w = 2;

	int pmax = 32;
	int *pstart = malloc(sizeof(int) * pmax);
	int *pend = malloc(sizeof(int) * pmax);
	int plines_total = 0;
	if (data->prompt_buffer && data->prompt_len > 0) {
		int i = 0;
		while (i <= data->prompt_len) {
			int line_start = i;
			while (i < data->prompt_len && data->prompt_buffer[i] != '\n') i++;
			int line_end = i;
			int seg = line_start;
			do {
				int take = (line_end - seg > seg_w) ? seg_w : (line_end - seg);
				if (plines_total >= pmax) {
					pmax *= 2;
					pstart = realloc(pstart, sizeof(int) * pmax);
					pend = realloc(pend, sizeof(int) * pmax);
				}
				pstart[plines_total] = seg;
				pend[plines_total] = seg + take;
				plines_total++;
				seg += take;
			} while (seg < line_end);
			if (line_end == data->prompt_len && (data->prompt_len == 0 || data->prompt_buffer[data->prompt_len - 1] != '\n')) break;
			i++;
			if (i == data->prompt_len + 1) break;
		}
	}
	if (plines_total == 0) {
		pstart[0] = 0; pend[0] = 0; plines_total = 1;
	}
	int plines = plines_total > 5 ? 5 : plines_total;
	int pscroll = plines_total > plines ? plines_total - plines : 0;
	int prompt_rows = 1 + plines + 1;
	int history_end = pane->height - prompt_rows - 1;
	if (history_end < 2) history_end = 2;

	int total_vis = 0;
	int *line_owner = NULL;
	int *line_sub = NULL;
	int owner_cap = 0;
	for (int i = 0; i < data->history_count; i++) {
		int seg_cols_w = inner_w - 2;
		if (seg_cols_w < 1) seg_cols_w = 1;
		int dw = utf8_display_width(data->history[i]);
		int rows = dw > 0 ? (dw + seg_cols_w - 1) / seg_cols_w : 1;
		if (rows < 1) rows = 1;
		for (int s = 0; s < rows; s++) {
			if (total_vis >= owner_cap) {
				owner_cap = owner_cap ? owner_cap * 2 : 64;
				line_owner = realloc(line_owner, sizeof(int) * owner_cap);
				line_sub = realloc(line_sub, sizeof(int) * owner_cap);
			}
			line_owner[total_vis] = i;
			line_sub[total_vis] = s;
			total_vis++;
		}
	}
	int spinner_row = -1;
	if (data->streaming) {
		if (total_vis >= owner_cap) {
			owner_cap = owner_cap ? owner_cap * 2 : 64;
			line_owner = realloc(line_owner, sizeof(int) * owner_cap);
			line_sub = realloc(line_sub, sizeof(int) * owner_cap);
		}
		line_owner[total_vis] = -1;
		line_sub[total_vis] = 0;
		spinner_row = total_vis;
		total_vis++;
	}

	int history_rows = history_end - 2 + 1;
	if (history_rows < 0) history_rows = 0;
	int max_scroll = total_vis > history_rows ? total_vis - history_rows : 0;
	if (data->scroll_offset < 0) data->scroll_offset = 0;
	if (data->scroll_offset > max_scroll) data->scroll_offset = max_scroll;
	int scroll = max_scroll - data->scroll_offset;
	if (scroll < 0) scroll = 0;

	for (int y = 0; y < pane->height; y++) {
		int gy = pane->y + y;
		int gx = pane->x;

		gridPutStr(gx, gy, "│", 1, E.theme.border, E.theme.border);

		if (y == 0) {
			Color hf = E.theme.status_fg;
			Color hb = E.theme.status_bg;
			char header[64];
			snprintf(header, sizeof(header), " %s %s ", E.ai_kanji, E.ai_name);
			int max = pane->width - 1;
			int used = gridPutStr(gx + 1, gy, header, max, hf, hb);
			for (int x = used; x < max; x++) {
				gridPutCh(gx + 1 + x, gy, ' ', hf, hb);
			}
		} else if (y == 1 || y == history_end + 1) {
			int used = 0;
			while (used < pane->width - 1) {
				int w = gridPutStr(gx + 1 + used, gy, "─", pane->width - 1 - used, E.theme.border, E.theme.bg);
				if (w <= 0) break;
				used += w;
			}
		} else if (y >= history_end + 2) {
			int prow = y - (history_end + 2);
			int cx = gx + 1;
			gridPutCh(cx, gy, ' ', E.theme.fg, E.theme.bg);
			cx++;
			int vline = prow + pscroll;
			if (prow < plines) {
				Color pfx_fg = (data->mode == MODE_INSERT) ? E.theme.keyword1 : E.theme.comment;
				const char *pfx = (vline == 0) ? (data->mode == MODE_INSERT ? "> " : "  ") : "  ";
				gridPutStr(cx, gy, pfx, 2, pfx_fg, E.theme.bg);
				cx += 2;

				int pw = inner_w - 2;
				int shown = 0;
				if (data->prompt_buffer && data->prompt_len > 0 && vline < plines_total) {
					int len = pend[vline] - pstart[vline];
					if (len > pw) len = pw;
					if (len > 0) {
						char tmp[1024];
						if (len > (int)sizeof(tmp) - 1) len = (int)sizeof(tmp) - 1;
						memcpy(tmp, data->prompt_buffer + pstart[vline], len);
						tmp[len] = '\0';
						shown = gridPutStr(cx, gy, tmp, pw, E.theme.fg, E.theme.bg);
					}
				} else if (vline == 0 && data->mode != MODE_INSERT) {
					const char *hint = "press 'i' to type, Enter to send";
					shown = gridPutStr(cx, gy, hint, pw, E.theme.comment, E.theme.bg);
				}
				for (int x = shown; x < pw; x++) {
					gridPutCh(cx + x, gy, ' ', E.theme.fg, E.theme.bg);
				}
				cx += pw;
			} else {
				for (int x = 0; x < inner_w; x++) {
					gridPutCh(cx + x, gy, ' ', E.theme.fg, E.theme.bg);
				}
				cx += inner_w;
			}
			gridPutCh(cx, gy, ' ', E.theme.fg, E.theme.bg);
		} else {
			int vrow = (y - 2) + scroll;
			int cx = gx + 1;
			gridPutCh(cx, gy, ' ', E.theme.fg, E.theme.bg);
			cx++;
			if (vrow == spinner_row && spinner_row >= 0) {
				static const char *frames[] = {"⠋","⠙","⠹","⠸","⠼","⠴","⠦","⠧","⠇","⠏"};
				data->spinner_frame = (data->spinner_frame + 1) % 10;
				int f = data->spinner_frame;
				int sx = cx;
				sx += gridPutStr(sx, gy, frames[f], 1, E.theme.string, E.theme.bg);
				sx += gridPutStr(sx, gy, " thinking...", 12, E.theme.string, E.theme.bg);
				int filled = sx - cx;
				for (int x = filled; x < inner_w; x++) {
					gridPutCh(cx + x, gy, ' ', E.theme.fg, E.theme.bg);
				}
				cx += inner_w;
				gridPutCh(cx, gy, ' ', E.theme.fg, E.theme.bg);
				continue;
			}
			if (vrow >= 0 && vrow < total_vis) {
				int msg_idx = line_owner[vrow];
				int sub = line_sub[vrow];
				char *text = data->history[msg_idx];
				int seg_max = inner_w - 2;
				int byte_off = 0, seg_len = 0, seg_cols = 0;
				utf8_slice(text, sub * seg_max, seg_max, &byte_off, &seg_len, &seg_cols);

				int is_selected = (data->visual_mode &&
					msg_idx >= MIN(data->visual_start, data->visual_end) &&
					msg_idx <= MAX(data->visual_start, data->visual_end));
				unsigned char role = data->history_role[msg_idx];

				Color bar_fg, body_fg, row_bg;
				if (is_selected) {
					bar_fg = E.theme.visual_fg;
					body_fg = E.theme.visual_fg;
					row_bg = E.theme.visual_bg;
				} else {
					row_bg = E.theme.bg;
					if (role == HK_ROLE_USER) { bar_fg = E.theme.keyword1; body_fg = E.theme.keyword1; }
					else if (role == HK_ROLE_AI) { bar_fg = E.theme.string; body_fg = E.theme.fg; }
					else { bar_fg = E.theme.comment; body_fg = E.theme.comment; }
					if (role == HK_ROLE_AI && text && text[0] == '>' && (text[1] == ' ' || text[1] == '\0')) {
						body_fg = E.theme.comment;
					}
				}

				const char *bar = (role == HK_ROLE_USER || role == HK_ROLE_AI) ? "▌ " : "  ";
				gridPutStr(cx, gy, bar, 2, bar_fg, row_bg);
				cx += 2;

				if (seg_len > 0) {
					char tmp[2048];
					int n = seg_len < (int)sizeof(tmp) - 1 ? seg_len : (int)sizeof(tmp) - 1;
					memcpy(tmp, text + byte_off, n);
					tmp[n] = '\0';
					gridPutStr(cx, gy, tmp, seg_max, body_fg, row_bg);
				}
				for (int x = seg_cols; x < seg_max; x++) {
					gridPutCh(cx + x, gy, ' ', body_fg, row_bg);
				}
				cx += seg_max;
			} else {
				for (int x = 0; x < inner_w; x++) {
					gridPutCh(cx + x, gy, ' ', E.theme.fg, E.theme.bg);
				}
				cx += inner_w;
			}
			gridPutCh(cx, gy, ' ', E.theme.fg, E.theme.bg);
		}
	}

	free(line_owner);
	free(line_sub);
	free(pstart);
	free(pend);
	pthread_mutex_unlock(&data->lock);
	(void)ab;
}

/*** ai transport ***/
static void hkHakoDirPath(char *out, size_t n) {
	const char *home = getenv("HOME");
	if (!home) { out[0] = '\0'; return; }
	snprintf(out, n, "%s/.hako", home);
#ifdef _WIN32
	_mkdir(out);
#else
	mkdir(out, 0755);
#endif
}

static int hkProjectDirPath(char *out, size_t n) {
	char cwd[PATH_MAX];
	if (!getcwd(cwd, sizeof(cwd))) return 0;
	snprintf(out, n, "%s/.hako", cwd);
	return 1;
}

static int hkProjectTrusted(void) {
	char dir[PATH_MAX];
	if (!hkProjectDirPath(dir, sizeof(dir))) return 0;
	struct stat st;
	if (stat(dir, &st) != 0 || !S_ISDIR(st.st_mode)) return 0;
	char trust[PATH_MAX + 16];
	snprintf(trust, sizeof(trust), "%s/trust", dir);
	return (stat(trust, &st) == 0 && S_ISREG(st.st_mode)) ? 1 : 0;
}

static void hkHistoryPath(char *out, size_t n) {
	if (hkProjectTrusted()) {
		char dir[PATH_MAX];
		hkProjectDirPath(dir, sizeof(dir));
		snprintf(out, n, "%s/history", dir);
		return;
	}
	char cwd[PATH_MAX];
	if (getcwd(cwd, sizeof(cwd))) {
		char local[PATH_MAX + 8];
		snprintf(local, sizeof(local), "%s/.rei", cwd);
		struct stat st;
		if (stat(local, &st) == 0 && S_ISREG(st.st_mode)) {
			snprintf(out, n, "%s", local);
			return;
		}
	}
	char dir[512];
	hkHakoDirPath(dir, sizeof(dir));
	snprintf(out, n, "%s/history", dir);
}

static int hkGrantProjectTrust(void) {
	char dir[PATH_MAX];
	if (!hkProjectDirPath(dir, sizeof(dir))) return 0;
	mkdir(dir, 0755);
	char trust[PATH_MAX + 16];
	snprintf(trust, sizeof(trust), "%s/trust", dir);
	FILE *fp = fopen(trust, "w");
	if (!fp) return 0;
	fprintf(fp, "granted=%ld\n", (long)time(NULL));
	fclose(fp);
	return 1;
}

static void hkJsonEscapeInto(const char *s, char *out, int cap) {
	int j = 0;
	for (int i = 0; s[i] && j < cap - 6; i++) {
		unsigned char ch = (unsigned char)s[i];
		if (ch == '"') { out[j++] = '\\'; out[j++] = '"'; }
		else if (ch == '\\') { out[j++] = '\\'; out[j++] = '\\'; }
		else if (ch == '\n') { out[j++] = '\\'; out[j++] = 'n'; }
		else if (ch == '\r') { out[j++] = '\\'; out[j++] = 'r'; }
		else if (ch == '\t') { out[j++] = '\\'; out[j++] = 't'; }
		else if (ch < 0x20) { j += snprintf(out + j, cap - j, "\\u%04x", ch); }
		else out[j++] = s[i];
	}
	out[j] = '\0';
}

void hkLogMessage(const char *role, const char *content) {
	char path[512];
	hkHistoryPath(path, sizeof(path));
	if (!path[0]) return;
	FILE *fp = fopen(path, "a");
	if (!fp) return;
	int clen = content ? (int)strlen(content) : 0;
	int cap = clen * 6 + 32;
	char *esc = malloc(cap);
	if (!esc) { fclose(fp); return; }
	hkJsonEscapeInto(content ? content : "", esc, cap);
	fprintf(fp, "{\"ts\":%ld,\"sid\":\"%s\",\"role\":\"%s\",\"content\":\"%s\"}\n",
		(long)time(NULL), E.session_id ? E.session_id : "", role, esc);
	free(esc);
	fclose(fp);
}

static char *hkJsonUnescape(const char *s, int len) {
	char *out = malloc(len + 1);
	if (!out) return NULL;
	int j = 0;
	for (int i = 0; i < len; i++) {
		if (s[i] == '\\' && i + 1 < len) {
			i++;
			switch (s[i]) {
			case 'n': out[j++] = '\n'; break;
			case 'r': out[j++] = '\r'; break;
			case 't': out[j++] = '\t'; break;
			case '"': out[j++] = '"'; break;
			case '\\': out[j++] = '\\'; break;
			case '/': out[j++] = '/'; break;
			default: out[j++] = s[i]; break;
			}
		} else out[j++] = s[i];
	}
	out[j] = '\0';
	return out;
}

void aiAddHistory(aiData *data, const char *text);

void hkLoadHistoryTail(aiData *data, int max_msgs) {
	char path[512];
	hkHistoryPath(path, sizeof(path));
	if (!path[0]) return;
	FILE *fp = fopen(path, "r");
	if (!fp) return;

	char **lines = NULL;
	int lcount = 0, lcap = 0;
	char *line = NULL;
	size_t cap = 0;
	ssize_t n;
	while ((n = getline(&line, &cap, fp)) != -1) {
		if (lcount >= lcap) { lcap = lcap ? lcap * 2 : 64; lines = realloc(lines, sizeof(char*) * lcap); }
		lines[lcount++] = strndup(line, n);
	}
	free(line);
	fclose(fp);

	int kept = 0;
	int *keep_idx = malloc(sizeof(int) * lcount);
	for (int i = 0; i < lcount; i++) {
		char *l = lines[i];
		if (E.session_id) {
			char tag[64];
			snprintf(tag, sizeof(tag), "\"sid\":\"%s\"", E.session_id);
			if (!strstr(l, tag)) continue;
		}
		keep_idx[kept++] = i;
	}
	int start = kept > max_msgs ? kept - max_msgs : 0;
	for (int k = start; k < kept; k++) {
		int i = keep_idx[k];
		char *l = lines[i];
		char *role = strstr(l, "\"role\":\"");
		char *content = strstr(l, "\"content\":\"");
		if (role && content) {
			role += 8;
			char *rend = strchr(role, '"');
			content += 11;
			char *cend = content;
			while (*cend) {
				if (*cend == '"' && *(cend - 1) != '\\') break;
				cend++;
			}
			if (rend && cend > content) {
				char rtag = *role;
				char *text = hkJsonUnescape(content, cend - content);
				if (text) {
					unsigned char r = (rtag == 'u') ? HK_ROLE_USER
						: (rtag == 'a') ? HK_ROLE_AI : HK_ROLE_SYSTEM;
					aiAddHistoryRole(data, text, r);
					free(text);
				}
			}
		}
	}
	free(keep_idx);
	for (int i = 0; i < lcount; i++) free(lines[i]);
	free(lines);
}

int hkLoadSkills(aiData *data) {
	if (!data) return 0;
	char dir[512];
	hkHakoDirPath(dir, sizeof(dir));
	if (!dir[0]) return 0;
	char skills[512];
	snprintf(skills, sizeof(skills), "%s/skills", dir);
	DIR *d = opendir(skills);
	if (!d) return 0;

	enum { MAX_SKILLS = 64, MAX_TOTAL = 1u << 20 }; /* 1 MiB cap on system prompt */
	char *buf = NULL;
	size_t total = 0;
	int loaded = 0;
	struct dirent *e;
	while ((e = readdir(d)) && loaded < MAX_SKILLS) {
		if (e->d_name[0] == '.') continue;
		int nlen = strlen(e->d_name);
		if (nlen < 4 || strcmp(e->d_name + nlen - 3, ".md") != 0) continue;
		char path[1024];
		snprintf(path, sizeof(path), "%s/%s", skills, e->d_name);
		FILE *fp = fopen(path, "r");
		if (!fp) continue;
		fseek(fp, 0, SEEK_END);
		long sz = ftell(fp);
		fseek(fp, 0, SEEK_SET);
		if (sz < 0 || sz > 200000) { fclose(fp); continue; }
		char header[256];
		int hlen = snprintf(header, sizeof(header), "\n<skill name=\"%s\">\n", e->d_name);
		const char *tail = "\n</skill>\n";
		int tlen = (int)strlen(tail);
		size_t need = total + (size_t)hlen + (size_t)sz + (size_t)tlen + 1;
		if (need > MAX_TOTAL) { fclose(fp); break; }
		char *nb = realloc(buf, need);
		if (!nb) { fclose(fp); break; }
		buf = nb;
		memcpy(buf + total, header, hlen); total += hlen;
		size_t got = fread(buf + total, 1, (size_t)sz, fp);
		total += got;
		memcpy(buf + total, tail, tlen); total += tlen;
		fclose(fp);
		loaded++;
	}
	closedir(d);
	if (buf) buf[total] = '\0';

	free(data->system_prompt);
	data->system_prompt = buf;
	return loaded;
}

const char *hkProviderName(enum aiProviderType t) {
	switch (t) {
	case AI_PROVIDER_OLLAMA: return "ollama";
	case AI_PROVIDER_ANTHROPIC: return "anthropic";
	case AI_PROVIDER_OPENAI: return "openai";
	default: return "none";
	}
}

static enum aiProviderType hkParseProvider(const char *s) {
	if (strcmp(s, "ollama") == 0) return AI_PROVIDER_OLLAMA;
	if (strcmp(s, "anthropic") == 0 || strcmp(s, "claude") == 0) return AI_PROVIDER_ANTHROPIC;
	if (strcmp(s, "openai") == 0 || strcmp(s, "gpt") == 0 || strcmp(s, "groq") == 0) return AI_PROVIDER_OPENAI;
	if (strcmp(s, "deepseek") == 0 || strcmp(s, "mistral") == 0 || strcmp(s, "together") == 0
		|| strcmp(s, "fireworks") == 0 || strcmp(s, "openrouter") == 0
		|| strcmp(s, "xai") == 0 || strcmp(s, "grok") == 0) return AI_PROVIDER_OPENAI;
	return AI_PROVIDER_NONE;
}

static const char *hkProviderDefaultEndpoint(const char *s) {
	if (strcmp(s, "deepseek") == 0)   return "https://api.deepseek.com";
	if (strcmp(s, "mistral") == 0)    return "https://api.mistral.ai";
	if (strcmp(s, "together") == 0)   return "https://api.together.xyz";
	if (strcmp(s, "fireworks") == 0)  return "https://api.fireworks.ai/inference";
	if (strcmp(s, "openrouter") == 0) return "https://openrouter.ai/api";
	if (strcmp(s, "groq") == 0)       return "https://api.groq.com/openai";
	if (strcmp(s, "xai") == 0 || strcmp(s, "grok") == 0) return "https://api.x.ai";
	return NULL;
}

static void hkApplyProviderAlias(const char *val) {
	enum aiProviderType t = hkParseProvider(val);
	if (t == AI_PROVIDER_NONE) return;
	E.ai_provider_type = t;
	const char *ep = hkProviderDefaultEndpoint(val);
	if (ep && !E.ai_endpoint) E.ai_endpoint = strdup(ep);
}

static void hkWriteSessionFile(const char *path) {
	FILE *fp = fopen(path, "w");
	if (!fp) return;
	fprintf(fp, "ai_provider=%s\n", hkProviderName(E.ai_provider_type));
	if (E.ai_model) fprintf(fp, "ai_model=%s\n", E.ai_model);
	if (E.ai_endpoint) fprintf(fp, "ai_endpoint=%s\n", E.ai_endpoint);
	fprintf(fp, "ai_tools_enabled=%d\n", E.ai_tools_enabled);
	fprintf(fp, "ai_stream=%d\n", E.ai_stream);
	fprintf(fp, "ai_autowrite=%d\n", E.ai_autowrite);
	if (E.session_id) fprintf(fp, "session_id=%s\n", E.session_id);
	fprintf(fp, "session_started=%ld\n", E.session_started);
	fprintf(fp, "session_last_used=%ld\n", (long)time(NULL));
	fprintf(fp, "session_turn_count=%d\n", E.session_turn_count);
	fclose(fp);
}

void hkSaveSession(void) {
	char dir[512];
	hkHakoDirPath(dir, sizeof(dir));
	if (dir[0]) {
		char path[640];
		snprintf(path, sizeof(path), "%s/state", dir);
		hkWriteSessionFile(path);
	}
	char pdir[PATH_MAX];
	if (hkProjectDirPath(pdir, sizeof(pdir))) {
		char ppath[PATH_MAX + 8];
		snprintf(ppath, sizeof(ppath), "%s/state", pdir);
		hkWriteSessionFile(ppath);
	}
}

static void hkLoadSessionFile(const char *path, int allow_session_fields) {
	FILE *fp = fopen(path, "r");
	if (!fp) return;
	char *line = NULL;
	size_t cap = 0;
	while (getline(&line, &cap, fp) != -1) {
		char *nl = strchr(line, '\n'); if (nl) *nl = '\0';
		char *eq = strchr(line, '='); if (!eq) continue;
		*eq = '\0';
		char *key = line, *val = eq + 1;
		if (strcmp(key, "ai_provider") == 0) {
			enum aiProviderType t = hkParseProvider(val);
			if (t != AI_PROVIDER_NONE) E.ai_provider_type = t;
		} else if (strcmp(key, "ai_model") == 0) {
			free(E.ai_model);
			E.ai_model = strdup(val);
		} else if (strcmp(key, "ai_endpoint") == 0) {
			free(E.ai_endpoint);
			E.ai_endpoint = strdup(val);
		} else if (strcmp(key, "ai_tools_enabled") == 0) {
			E.ai_tools_enabled = atoi(val) ? 1 : 0;
		} else if (strcmp(key, "ai_stream") == 0) {
			E.ai_stream = atoi(val) ? 1 : 0;
		} else if (strcmp(key, "ai_autowrite") == 0) {
			E.ai_autowrite = atoi(val) ? 1 : 0;
		} else if (allow_session_fields && strcmp(key, "session_id") == 0) {
			free(E.session_id);
			E.session_id = strdup(val);
		} else if (allow_session_fields && strcmp(key, "session_started") == 0) {
			E.session_started = atol(val);
		} else if (allow_session_fields && strcmp(key, "session_last_used") == 0) {
			E.session_last_used = atol(val);
		} else if (allow_session_fields && strcmp(key, "session_turn_count") == 0) {
			E.session_turn_count = atoi(val);
		}
	}
	free(line);
	fclose(fp);
}

static void hkGenSessionId(void) {
	free(E.session_id);
	E.session_id = malloc(17);
	long now = (long)time(NULL);
	srand((unsigned)(now ^ getpid()));
	snprintf(E.session_id, 17, "%lx%04x", now & 0xffffffff, rand() & 0xffff);
}

void hkLoadSession(void) {
	char dir[512];
	hkHakoDirPath(dir, sizeof(dir));
	if (dir[0]) {
		char path[640];
		snprintf(path, sizeof(path), "%s/state", dir);
		hkLoadSessionFile(path, 0);
	}
	char pdir[PATH_MAX];
	int has_project = 0;
	char ppath[PATH_MAX + 8];
	if (hkProjectDirPath(pdir, sizeof(pdir))) {
		struct stat st;
		snprintf(ppath, sizeof(ppath), "%s/state", pdir);
		if (stat(ppath, &st) == 0) {
			has_project = 1;
			hkLoadSessionFile(ppath, 1);
		}
	}

	long now = (long)time(NULL);
	int recent = E.session_last_used > 0 && (now - E.session_last_used) < 7 * 24 * 3600;
	if (has_project && E.session_id && recent) {
		E.session_resumed = 1;
	} else {
		E.session_resumed = 0;
		E.session_started = now;
		E.session_last_used = 0;
		E.session_turn_count = 0;
		hkGenSessionId();
	}
}

int hkHandleSlash(aiData *data, const char *prompt) {
	if (prompt[0] != '/') return 0;
	const char *cmd = prompt + 1;
	const char *arg = strchr(cmd, ' ');
	int cmdlen = arg ? (arg - cmd) : (int)strlen(cmd);
	if (arg) { while (*arg == ' ') arg++; }

	if (strncmp(cmd, "help", cmdlen) == 0 && cmdlen == 4) {
		aiAddHistory(data, "/clear  /help  /model <id>  /provider <name>");
		aiAddHistory(data, "/file <path>  /history [local|global]  /skills [reload]");
		aiAddHistory(data, "/skill install <url>  /skill uninstall <name>");
		aiAddHistory(data, "/tools on|off  /trust [revoke]  /usage  /quit");
		aiAddHistory(data, "/sessions  /resume <id>  /session [new]");
		return 1;
	}
	if (strncmp(cmd, "clear", cmdlen) == 0 && cmdlen == 5) {
		for (int i = 0; i < data->history_count; i++) free(data->history[i]);
		memset(data->history_role, 0, AI_HISTORY_MAX);
		data->history_count = 0;
		data->cursor_y = data->history_pos = 0;
		aiAddHistory(data, "(cleared)");
		return 1;
	}
	if (strncmp(cmd, "model", cmdlen) == 0 && cmdlen == 5) {
		if (arg && *arg) {
			free(E.ai_model);
			E.ai_model = strdup(arg);
			hkSaveSession();
			char msg[256];
			snprintf(msg, sizeof(msg), "model: %s (saved)", E.ai_model);
			aiAddHistory(data, msg);
		} else {
			char msg[256];
			snprintf(msg, sizeof(msg), "model: %s", E.ai_model ? E.ai_model : "(unset)");
			aiAddHistory(data, msg);
		}
		return 1;
	}
	if (strncmp(cmd, "provider", cmdlen) == 0 && cmdlen == 8) {
		if (arg && *arg) {
			enum aiProviderType t = hkParseProvider(arg);
			if (t == AI_PROVIDER_NONE) {
				aiAddHistory(data, "unknown (ollama/anthropic/openai/groq/deepseek/mistral/together/fireworks/openrouter/xai)");
			} else {
				hkApplyProviderAlias(arg);
				hkSaveSession();
				char msg[256];
				snprintf(msg, sizeof(msg), "provider: %s (saved)%s%s",
					hkProviderName(t),
					hkProviderDefaultEndpoint(arg) ? " endpoint=" : "",
					hkProviderDefaultEndpoint(arg) ? hkProviderDefaultEndpoint(arg) : "");
				aiAddHistory(data, msg);
			}
		} else {
			char msg[256];
			snprintf(msg, sizeof(msg), "provider: %s", hkProviderName(E.ai_provider_type));
			aiAddHistory(data, msg);
		}
		return 1;
	}
	if (strncmp(cmd, "file", cmdlen) == 0 && cmdlen == 4) {
		if (!arg || !*arg) { aiAddHistory(data, "usage: /file <path>"); return 1; }
		FILE *fp = fopen(arg, "r");
		if (!fp) { aiAddHistory(data, "file not found"); return 1; }
		fseek(fp, 0, SEEK_END);
		long sz = ftell(fp);
		fseek(fp, 0, SEEK_SET);
		if (sz < 0 || sz > 200000) { fclose(fp); aiAddHistory(data, "file too large"); return 1; }
		char *buf = malloc(sz + 1);
		fread(buf, 1, sz, fp);
		buf[sz] = '\0';
		fclose(fp);
		char header[512];
		snprintf(header, sizeof(header), "injected file: %s (%ld bytes)", arg, sz);
		aiAddHistory(data, header);
		free(data->current_prompt);
		int need = sz + strlen(arg) + 128;
		data->current_prompt = malloc(need);
		snprintf(data->current_prompt, need, "<file path=\"%s\">\n%s\n</file>\n", arg, buf);
		free(buf);
		aiWorkerSend(data);
		return 1;
	}
	if (strncmp(cmd, "history", cmdlen) == 0 && cmdlen == 7) {
		if (arg && strcmp(arg, "local") == 0) {
			char cwd[PATH_MAX];
			if (!getcwd(cwd, sizeof(cwd))) { aiAddHistory(data, "cwd error"); return 1; }
			char local[PATH_MAX + 8];
			snprintf(local, sizeof(local), "%s/.rei", cwd);
			struct stat st;
			if (stat(local, &st) != 0) {
				FILE *fp = fopen(local, "w");
				if (!fp) { aiAddHistory(data, "cannot create .rei"); return 1; }
				fclose(fp);
				aiAddHistory(data, "created .rei in cwd");
			} else {
				aiAddHistory(data, ".rei already exists");
			}
			char msg[PATH_MAX + 16];
			snprintf(msg, sizeof(msg), "using: %s", local);
			aiAddHistory(data, msg);
			return 1;
		}
		if (arg && strcmp(arg, "global") == 0) {
			char cwd[PATH_MAX];
			if (getcwd(cwd, sizeof(cwd))) {
				char local[PATH_MAX + 8];
				snprintf(local, sizeof(local), "%s/.rei", cwd);
				if (unlink(local) == 0) aiAddHistory(data, "removed local .rei");
				else aiAddHistory(data, "no local .rei to remove");
			}
			char p[512];
			hkHistoryPath(p, sizeof(p));
			char msg[600];
			snprintf(msg, sizeof(msg), "using: %s", p);
			aiAddHistory(data, msg);
			return 1;
		}
		char p[512];
		hkHistoryPath(p, sizeof(p));
		aiAddHistory(data, p);
		aiAddHistory(data, "(use /history local | /history global)");
		return 1;
	}
	if (strncmp(cmd, "skills", cmdlen) == 0 && cmdlen == 6) {
		if (arg && strncmp(arg, "reload", 6) == 0) {
			int n = hkLoadSkills(data);
			char msg[64];
			snprintf(msg, sizeof(msg), "reloaded %d skill(s)", n);
			aiAddHistory(data, msg);
			return 1;
		}
		char dir[512];
		hkHakoDirPath(dir, sizeof(dir));
		char skills[512];
		snprintf(skills, sizeof(skills), "%s/skills", dir);
		DIR *d = opendir(skills);
		if (!d) { aiAddHistory(data, "no skills dir (~/.hako/skills)"); return 1; }
		struct dirent *e;
		int n = 0;
		while ((e = readdir(d))) {
			if (e->d_name[0] == '.') continue;
			aiAddHistory(data, e->d_name);
			n++;
		}
		closedir(d);
		if (n == 0) aiAddHistory(data, "(no skills)");
		return 1;
	}
	if (strncmp(cmd, "skill", cmdlen) == 0 && cmdlen == 5) {
		if (arg && strncmp(arg, "uninstall ", 10) == 0) {
			const char *name = arg + 10;
			while (*name == ' ') name++;
			if (!*name) { aiAddHistory(data, "usage: /skill uninstall <name>"); return 1; }
			char dir[512];
			hkHakoDirPath(dir, sizeof(dir));
			char path[1024];
			int nlen = strlen(name);
			if (nlen >= 4 && strcmp(name + nlen - 3, ".md") == 0) {
				snprintf(path, sizeof(path), "%s/skills/%s", dir, name);
			} else {
				snprintf(path, sizeof(path), "%s/skills/%s.md", dir, name);
			}
			if (unlink(path) == 0) {
				int n = hkLoadSkills(data);
				char msg[128];
				snprintf(msg, sizeof(msg), "uninstalled: %s (%d remain)", name, n);
				aiAddHistory(data, msg);
			} else {
				aiAddHistory(data, "skill not found");
			}
			return 1;
		}
		if (!arg || strncmp(arg, "install ", 8) != 0) {
			aiAddHistory(data, "usage: /skill install <url>  |  /skill uninstall <name>");
			return 1;
		}
		const char *url = arg + 8;
		while (*url == ' ') url++;
		if (!*url) { aiAddHistory(data, "usage: /skill install <url>"); return 1; }
		char dir[512];
		hkHakoDirPath(dir, sizeof(dir));
		char skills[512];
		snprintf(skills, sizeof(skills), "%s/skills", dir);
		mkdir(dir, 0755);
		mkdir(skills, 0755);
		const char *slash = strrchr(url, '/');
		const char *name = slash ? slash + 1 : url;
		char outpath[1024];
		snprintf(outpath, sizeof(outpath), "%s/%s", skills, name);
		int nlen = strlen(name);
		if (nlen < 4 || strcmp(name + nlen - 3, ".md") != 0) {
			snprintf(outpath, sizeof(outpath), "%s/%s.md", skills, name);
		}
		char cmdbuf[2048];
		snprintf(cmdbuf, sizeof(cmdbuf), "curl -sfL -o %s %s", outpath, url);
		int rc = system(cmdbuf);
		if (rc != 0) { aiAddHistory(data, "download failed"); return 1; }
		int n = hkLoadSkills(data);
		char msg[128];
		snprintf(msg, sizeof(msg), "installed: %s (%d total)", name, n);
		aiAddHistory(data, msg);
		return 1;
	}
	if (strncmp(cmd, "tools", cmdlen) == 0 && cmdlen == 5) {
		int changed = 0;
		if (arg && strcmp(arg, "on") == 0) { E.ai_tools_enabled = 1; changed = 1; }
		else if (arg && strcmp(arg, "off") == 0) { E.ai_tools_enabled = 0; changed = 1; }
		if (changed) hkSaveSession();
		char msg[64];
		snprintf(msg, sizeof(msg), "tools: %s%s", E.ai_tools_enabled ? "on" : "off", changed ? " (saved)" : "");
		aiAddHistory(data, msg);
		return 1;
	}
	if (strncmp(cmd, "trust", cmdlen) == 0 && cmdlen == 5) {
		if (arg && strcmp(arg, "revoke") == 0) {
			char dir[PATH_MAX];
			hkProjectDirPath(dir, sizeof(dir));
			char trust[PATH_MAX + 16];
			snprintf(trust, sizeof(trust), "%s/trust", dir);
			if (unlink(trust) == 0) aiAddHistory(data, "trust revoked");
			else aiAddHistory(data, "no trust to revoke");
			return 1;
		}
		if (hkProjectTrusted()) {
			aiAddHistory(data, "already trusted");
		} else if (hkGrantProjectTrust()) {
			aiAddHistory(data, "trusted. Rei may edit files.");
		} else {
			aiAddHistory(data, "could not grant trust");
		}
		return 1;
	}
	if (strncmp(cmd, "quit", cmdlen) == 0 && cmdlen == 4) {
		return 2;
	}
	if (strncmp(cmd, "usage", cmdlen) == 0 && cmdlen == 5) {
		char msg[256];
		snprintf(msg, sizeof(msg), "provider: %s", hkProviderName(E.ai_provider_type));
		aiAddHistory(data, msg);
		snprintf(msg, sizeof(msg), "model:    %s", E.ai_model ? E.ai_model : "(unset)");
		aiAddHistory(data, msg);
		snprintf(msg, sizeof(msg), "tools:    %s", E.ai_tools_enabled ? "on" : "off");
		aiAddHistory(data, msg);
		snprintf(msg, sizeof(msg), "trust:    %s", hkProjectTrusted() ? "granted" : "not granted");
		aiAddHistory(data, msg);
		snprintf(msg, sizeof(msg), "skills:   %d loaded", hkLoadSkills(data));
		aiAddHistory(data, msg);
		snprintf(msg, sizeof(msg), "stream:   %s", E.ai_stream ? "on" : "off");
		aiAddHistory(data, msg);
		snprintf(msg, sizeof(msg), "session:  %s (%d turns)",
			E.session_id ? E.session_id : "(none)", E.session_turn_count);
		aiAddHistory(data, msg);
		char hpath[PATH_MAX];
		hkHistoryPath(hpath, sizeof(hpath));
		snprintf(msg, sizeof(msg), "history:  %s", hpath);
		aiAddHistory(data, msg);
		snprintf(msg, sizeof(msg), "tokens:   last %d in / %d out  total %ld in / %ld out (cap %d)",
			data->last_in_tokens, data->last_out_tokens,
			data->total_in_tokens, data->total_out_tokens, E.ai_max_tokens);
		aiAddHistory(data, msg);
		return 1;
	}
	if (strncmp(cmd, "sessions", cmdlen) == 0 && cmdlen == 8) {
		char path[512];
		hkHistoryPath(path, sizeof(path));
		FILE *fp = fopen(path, "r");
		if (!fp) { aiAddHistory(data, "(no history)"); return 1; }
		char ids[16][32];
		long lasts[16];
		int counts[16];
		char firsts[16][80];
		int n = 0;
		char *line = NULL;
		size_t cap = 0;
		while (getline(&line, &cap, fp) != -1) {
			char *sidp = strstr(line, "\"sid\":\"");
			if (!sidp) continue;
			sidp += 7;
			char *send = strchr(sidp, '"');
			if (!send) continue;
			char id[32];
			int idlen = send - sidp;
			if (idlen >= (int)sizeof(id)) idlen = sizeof(id) - 1;
			memcpy(id, sidp, idlen); id[idlen] = '\0';
			if (!*id) continue;
			char *tsp = strstr(line, "\"ts\":");
			long ts = tsp ? atol(tsp + 5) : 0;
			int idx = -1;
			for (int i = 0; i < n; i++) if (strcmp(ids[i], id) == 0) { idx = i; break; }
			if (idx < 0) {
				if (n >= 16) continue;
				idx = n++;
				snprintf(ids[idx], sizeof(ids[idx]), "%s", id);
				firsts[idx][0] = '\0';
				counts[idx] = 0;
				char *role = strstr(line, "\"role\":\"user\"");
				if (role) {
					char *cp = strstr(line, "\"content\":\"");
					if (cp) {
						cp += 11;
						int j = 0;
						while (*cp && *cp != '"' && j < 60) firsts[idx][j++] = *cp++;
						firsts[idx][j] = '\0';
					}
				}
			}
			counts[idx]++;
			lasts[idx] = ts;
		}
		free(line); fclose(fp);
		if (n == 0) { aiAddHistory(data, "(no sessions)"); return 1; }
		long now = (long)time(NULL);
		for (int i = 0; i < n; i++) {
			long age = now - lasts[i];
			char unit; long val;
			if (age < 3600) { val = age / 60; unit = 'm'; }
			else if (age < 86400) { val = age / 3600; unit = 'h'; }
			else { val = age / 86400; unit = 'd'; }
			char msg[200];
			const char *cur = (E.session_id && strcmp(E.session_id, ids[i]) == 0) ? "* " : "  ";
			snprintf(msg, sizeof(msg), "%s%s %ld%c %dt %.40s", cur, ids[i], val, unit, counts[i], firsts[i]);
			aiAddHistory(data, msg);
		}
		aiAddHistory(data, "(/resume <id> to switch)");
		return 1;
	}
	if (strncmp(cmd, "resume", cmdlen) == 0 && cmdlen == 6) {
		if (!arg || !*arg) { aiAddHistory(data, "usage: /resume <id>"); return 1; }
		free(E.session_id);
		E.session_id = strdup(arg);
		E.session_resumed = 1;
		E.session_started = (long)time(NULL);
		hkSaveSession();
		for (int i = 0; i < data->history_count; i++) free(data->history[i]);
		memset(data->history_role, 0, AI_HISTORY_MAX);
		data->history_count = 0;
		data->cursor_y = data->history_pos = 0;
		aiFreeMessages(data);
		char msg[128];
		snprintf(msg, sizeof(msg), "resumed: %s", E.session_id);
		aiAddHistory(data, msg);
		hkLoadHistoryTail(data, 200);
		return 1;
	}
	if (strncmp(cmd, "session", cmdlen) == 0 && cmdlen == 7) {
		if (arg && strcmp(arg, "new") == 0) {
			for (int i = 0; i < data->history_count; i++) free(data->history[i]);
			memset(data->history_role, 0, AI_HISTORY_MAX);
			data->history_count = 0;
			data->cursor_y = data->history_pos = 0;
			aiFreeMessages(data);
			E.session_started = (long)time(NULL);
			E.session_turn_count = 0;
			E.session_resumed = 0;
			hkGenSessionId();
			hkSaveSession();
			char msg[128];
			snprintf(msg, sizeof(msg), "new session: %s", E.session_id);
			aiAddHistory(data, msg);
			return 1;
		}
		char msg[256];
		snprintf(msg, sizeof(msg), "id:      %s", E.session_id ? E.session_id : "(none)");
		aiAddHistory(data, msg);
		long age = (long)time(NULL) - E.session_started;
		snprintf(msg, sizeof(msg), "started: %ld min ago", age / 60);
		aiAddHistory(data, msg);
		snprintf(msg, sizeof(msg), "turns:   %d", E.session_turn_count);
		aiAddHistory(data, msg);
		snprintf(msg, sizeof(msg), "state:   %s", E.session_resumed ? "resumed" : "new");
		aiAddHistory(data, msg);
		aiAddHistory(data, "(/session new to reset)");
		return 1;
	}
	aiAddHistory(data, "unknown command (/help)");
	return 1;
}

static void aiPushHistoryLine(aiData *data, const char *text, int len, unsigned char role) {
	if (data->history_count >= AI_HISTORY_MAX) return;
	char *line = malloc(len + 1);
	if (!line) return;
	memcpy(line, text, len);
	line[len] = '\0';
	data->history_role[data->history_count] = role;
	data->history[data->history_count++] = line;
}

static void aiAddHistoryLine(aiData *data, const char *text, int text_len, unsigned char role) {
	int max_width = 60;
	if (text_len <= 0) {
		aiPushHistoryLine(data, "", 0, role);
		return;
	}
	if (text_len <= max_width) {
		aiPushHistoryLine(data, text, text_len, role);
		return;
	}
	int pos = 0;
	while (pos < text_len) {
		if (data->history_count >= AI_HISTORY_MAX) break;
		int end = pos + max_width;
		if (end >= text_len) {
			aiPushHistoryLine(data, text + pos, text_len - pos, role);
			break;
		}
		int wrap = end;
		while (wrap > pos && text[wrap] != ' ') wrap--;
		if (wrap == pos) wrap = end;
		aiPushHistoryLine(data, text + pos, wrap - pos, role);
		pos = wrap;
		while (pos < text_len && text[pos] == ' ') pos++;
	}
}

void aiAddHistoryRole(aiData *data, const char *text, unsigned char role) {
	if (!data || !text) return;
	if (data->history_count > 0 && data->history_count < AI_HISTORY_MAX - 1) {
		unsigned char prev = data->history_role[data->history_count - 1];
		int role_swap = (prev != role) && (prev == HK_ROLE_USER || prev == HK_ROLE_AI || role == HK_ROLE_USER || role == HK_ROLE_AI);
		char *prev_text = data->history[data->history_count - 1];
		if (role_swap && prev_text && *prev_text != '\0') {
			aiPushHistoryLine(data, "", 0, HK_ROLE_SYSTEM);
		}
	}
	const char *p = text;
	while (1) {
		const char *nl = strchr(p, '\n');
		int seg = nl ? (int)(nl - p) : (int)strlen(p);
		aiAddHistoryLine(data, p, seg, role);
		if (!nl) break;
		p = nl + 1;
	}
}

void aiAddHistory(aiData *data, const char *text) {
	aiAddHistoryRole(data, text, HK_ROLE_SYSTEM);
}

void aiPushMessage(aiData *data, const char *role, const char *content) {
	if (!data || !role || !content) return;
	if (data->message_count >= data->message_cap) {
		data->message_cap = data->message_cap ? data->message_cap * 2 : 16;
		data->messages = realloc(data->messages, sizeof(aiMessage) * data->message_cap);
	}
	data->messages[data->message_count].role = strdup(role);
	data->messages[data->message_count].content = strdup(content);
	data->messages[data->message_count].raw = 0;
	data->message_count++;
}

void aiPushMessageRaw(aiData *data, const char *role, const char *content_json) {
	if (!data || !role || !content_json) return;
	if (data->message_count >= data->message_cap) {
		data->message_cap = data->message_cap ? data->message_cap * 2 : 16;
		data->messages = realloc(data->messages, sizeof(aiMessage) * data->message_cap);
	}
	data->messages[data->message_count].role = strdup(role);
	data->messages[data->message_count].content = strdup(content_json);
	data->messages[data->message_count].raw = 1;
	data->message_count++;
}

void aiFreeMessages(aiData *data) {
	if (!data || !data->messages) return;
	for (int i = 0; i < data->message_count; i++) {
		free(data->messages[i].role);
		free(data->messages[i].content);
	}
	free(data->messages);
	data->messages = NULL;
	data->message_count = 0;
	data->message_cap = 0;
}

static char *aiBuildMessagesJson(aiData *data) {
	int cap = 4096;
	char *out = malloc(cap);
	if (!out) return NULL;
	int len = 0;
	out[len++] = '[';
	for (int i = 0; i < data->message_count; i++) {
		const char *content = data->messages[i].content ? data->messages[i].content : "";
		int clen = strlen(content);
		int need = len + clen * 6 + 128;
		if (need >= cap) {
			while (cap < need) cap *= 2;
			out = realloc(out, cap);
			if (!out) return NULL;
		}
		if (i > 0) out[len++] = ',';
		if (data->messages[i].raw) {
			int need2 = len + clen + 64;
			if (need2 >= cap) { while (cap < need2) cap *= 2; out = realloc(out, cap); }
			len += snprintf(out + len, cap - len, "{\"role\":\"%s\",\"content\":", data->messages[i].role);
			memcpy(out + len, content, clen);
			len += clen;
			out[len++] = '}';
		} else {
			len += snprintf(out + len, cap - len, "{\"role\":\"%s\",\"content\":\"", data->messages[i].role);
			char *esc = malloc(clen * 6 + 8);
			hkJsonEscapeInto(content, esc, clen * 6 + 8);
			int elen = strlen(esc);
			if (len + elen + 8 >= cap) {
				cap = (len + elen) * 2;
				out = realloc(out, cap);
			}
			memcpy(out + len, esc, elen);
			len += elen;
			free(esc);
			out[len++] = '"';
			out[len++] = '}';
		}
	}
	out[len++] = ']';
	out[len] = '\0';
	return out;
}

static char *aiWriteRequestFile(const char *json) {
	char path[256];
	snprintf(path, sizeof(path), "/tmp/hako-req-%d.json", (int)getpid());
	FILE *fp = fopen(path, "w");
	if (!fp) return NULL;
	fputs(json, fp);
	fclose(fp);
	return strdup(path);
}

static char *hkBuildToolsSchema(int provider_format);

char *aiBuildCurlCommand(aiData *data, enum aiProviderType type) {
	char *msgs = aiBuildMessagesJson(data);
	if (!msgs) return NULL;

	const char *endpoint = E.ai_endpoint;
	const char *model = E.ai_model;
	const char *api_key = E.ai_api_key;
	int max_tokens = E.ai_max_tokens > 0 ? E.ai_max_tokens : 2048;

	int bodycap = strlen(msgs) + 4096;
	char *body = malloc(bodycap);
	if (!body) { free(msgs); return NULL; }

	const char *sys = (data->system_prompt && *data->system_prompt) ? data->system_prompt : "";
	char *sys_esc = NULL;
	if (*sys) {
		int slen = strlen(sys);
		sys_esc = malloc(slen * 6 + 8);
		hkJsonEscapeInto(sys, sys_esc, slen * 6 + 8);
	}

	int tools_on = E.ai_tools_enabled;
	char *anth_tools = NULL, *fn_tools = NULL;
	if (tools_on) {
		anth_tools = hkBuildToolsSchema(0);
		fn_tools = hkBuildToolsSchema(1);
	}

	switch (type) {
	case AI_PROVIDER_OLLAMA: {
		if (!endpoint) endpoint = "http://localhost:11434";
		if (!model) model = "llama3.2";
		int tlen = tools_on ? strlen(fn_tools) + 16 : 0;
		int need = bodycap + tlen + 96;
		if (need > bodycap) { body = realloc(body, need); bodycap = need; }
		if (tools_on) {
			snprintf(body, bodycap,
				"{\"model\":\"%s\",\"messages\":%s,\"stream\":false,\"tools\":%s}",
				model, msgs, fn_tools);
		} else {
			snprintf(body, bodycap,
				"{\"model\":\"%s\",\"messages\":%s,\"stream\":false}",
				model, msgs);
		}
		break;
	}
	case AI_PROVIDER_ANTHROPIC: {
		if (!endpoint) endpoint = "https://api.anthropic.com";
		if (!model) model = "claude-sonnet-4-20250514";
		int anth_on = tools_on && hkProjectTrusted();
		int stream_on = E.ai_stream && !anth_on;
		int tlen = anth_on ? strlen(anth_tools) + 16 : 0;
		int need = bodycap + tlen + 96;
		if (need > bodycap) { body = realloc(body, need); bodycap = need; }
		const char *stream_field = stream_on ? ",\"stream\":true" : "";
		char tools_field[64];
		tools_field[0] = '\0';
		if (*sys) {
			if (anth_on) {
				snprintf(body, bodycap,
					"{\"model\":\"%s\",\"max_tokens\":%d,\"system\":\"%s\",\"messages\":%s,\"tools\":%s%s}",
					model, max_tokens, sys_esc, msgs, anth_tools, stream_field);
			} else {
				snprintf(body, bodycap,
					"{\"model\":\"%s\",\"max_tokens\":%d,\"system\":\"%s\",\"messages\":%s%s}",
					model, max_tokens, sys_esc, msgs, stream_field);
			}
		} else {
			if (anth_on) {
				snprintf(body, bodycap,
					"{\"model\":\"%s\",\"max_tokens\":%d,\"messages\":%s,\"tools\":%s%s}",
					model, max_tokens, msgs, anth_tools, stream_field);
			} else {
				snprintf(body, bodycap,
					"{\"model\":\"%s\",\"max_tokens\":%d,\"messages\":%s%s}",
					model, max_tokens, msgs, stream_field);
			}
		}
		(void)tools_field;
		break;
	}
	case AI_PROVIDER_OPENAI: {
		if (!endpoint) endpoint = "https://api.openai.com";
		if (!model) model = "gpt-4o-mini";
		int tlen = tools_on ? strlen(fn_tools) + 16 : 0;
		int need = bodycap + tlen + 96;
		if (need > bodycap) { body = realloc(body, need); bodycap = need; }
		if (tools_on) {
			snprintf(body, bodycap,
				"{\"model\":\"%s\",\"max_tokens\":%d,\"messages\":%s,\"tools\":%s}",
				model, max_tokens, msgs, fn_tools);
		} else {
			snprintf(body, bodycap,
				"{\"model\":\"%s\",\"max_tokens\":%d,\"messages\":%s}",
				model, max_tokens, msgs);
		}
		break;
	}
	default:
		free(body); free(msgs); free(sys_esc); free(anth_tools); free(fn_tools); return NULL;
	}

	free(msgs);
	free(sys_esc);
	free(anth_tools);
	free(fn_tools);

	char *reqfile = aiWriteRequestFile(body);
	free(body);
	if (!reqfile) return NULL;

	char *cmd = malloc(4096);
	if (!cmd) { free(reqfile); return NULL; }

	switch (type) {
	case AI_PROVIDER_OLLAMA:
		snprintf(cmd, 4096,
			"curl -s -X POST %s/api/chat -H 'Content-Type: application/json' --data @%s 2>/dev/null",
			endpoint, reqfile);
		break;
	case AI_PROVIDER_ANTHROPIC:
		if (!api_key) { free(cmd); free(reqfile); return NULL; }
		snprintf(cmd, 4096,
			(E.ai_stream && !E.ai_tools_enabled)
				? "curl -sN -X POST %s/v1/messages -H 'Content-Type: application/json' -H 'x-api-key: %s' -H 'anthropic-version: 2023-06-01' -H 'accept: text/event-stream' --data @%s 2>/dev/null"
				: "curl -s -X POST %s/v1/messages -H 'Content-Type: application/json' -H 'x-api-key: %s' -H 'anthropic-version: 2023-06-01' --data @%s 2>/dev/null",
			endpoint, api_key, reqfile);
		break;
	case AI_PROVIDER_OPENAI:
		if (!api_key) { free(cmd); free(reqfile); return NULL; }
		snprintf(cmd, 4096,
			"curl -s -X POST %s/v1/chat/completions -H 'Content-Type: application/json' -H 'Authorization: Bearer %s' --data @%s 2>/dev/null",
			endpoint, api_key, reqfile);
		break;
	default: break;
	}

	free(reqfile);
	return cmd;
}


char *aiExtractResponse(const char *json, enum aiProviderType type) {
	if (!json) return NULL;

	const char *search_key = NULL;
	switch (type) {
	case AI_PROVIDER_OLLAMA:
		search_key = "\"content\":\"";
		break;
	case AI_PROVIDER_ANTHROPIC:
		search_key = "\"text\":\"";
		break;
	case AI_PROVIDER_OPENAI:
		search_key = "\"content\":\"";
		break;
	default:
		return NULL;
	}

	char *start = strstr(json, search_key);
	if (!start) {
		char *err = strstr(json, "\"error\"");
		if (err) {
			char *msg = strstr(err, "\"message\":\"");
			if (msg) {
				msg += 11;
				char *end = strchr(msg, '"');
				if (end) {
					int len = end - msg;
					char *result = malloc(len + 8);
					snprintf(result, len + 8, "Error: %.*s", len, msg);
					return result;
				}
			}
		}
		return strdup("Error: Could not parse response");
	}

	start += strlen(search_key);

	int cap = 4096;
	char *result = malloc(cap);
	int len = 0;

	while (*start && !(*start == '"' && *(start - 1) != '\\')) {
		if (len >= cap - 4) {
			cap *= 2;
			result = realloc(result, cap);
		}
		if (*start == '\\' && *(start + 1)) {
			start++;
			switch (*start) {
			case 'n': result[len++] = '\n'; break;
			case 't': result[len++] = '\t'; break;
			case '"': result[len++] = '"'; break;
			case '\\': result[len++] = '\\'; break;
			case '/': result[len++] = '/'; break;
			default: result[len++] = '\\'; result[len++] = *start; break;
			}
		} else {
			result[len++] = *start;
		}
		start++;
	}
	result[len] = '\0';

	return result;
}

static char *hkReadFileAll(const char *path, long max_bytes) {
	FILE *fp = fopen(path, "r");
	if (!fp) return NULL;
	fseek(fp, 0, SEEK_END);
	long sz = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	if (sz < 0) { fclose(fp); return NULL; }
	if (max_bytes > 0 && sz > max_bytes) sz = max_bytes;
	char *buf = malloc(sz + 1);
	if (!buf) { fclose(fp); return NULL; }
	fread(buf, 1, sz, fp);
	buf[sz] = '\0';
	fclose(fp);
	return buf;
}

static char *hkRunShellCapture(const char *cmd, long max_bytes) {
	char full[2048];
	snprintf(full, sizeof(full), "timeout 10 sh -c %c%s%c 2>&1", '"', cmd, '"');
	FILE *fp = popen(full, "r");
	if (!fp) return strdup("error: popen failed");
	char *out = NULL;
	size_t total = 0;
	char buf[4096];
	while (fgets(buf, sizeof(buf), fp)) {
		int n = strlen(buf);
		if (max_bytes > 0 && (long)(total + n) > max_bytes) n = max_bytes - total;
		if (n <= 0) break;
		out = realloc(out, total + n + 1);
		memcpy(out + total, buf, n);
		total += n;
	}
	pclose(fp);
	if (!out) return strdup("");
	out[total] = '\0';
	return out;
}

static char *hkListDir(const char *path) {
	DIR *d = opendir(path);
	if (!d) return NULL;
	char *out = NULL;
	size_t total = 0;
	struct dirent *e;
	while ((e = readdir(d))) {
		if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
		int n = strlen(e->d_name);
		out = realloc(out, total + n + 2);
		memcpy(out + total, e->d_name, n);
		out[total + n] = '\n';
		total += n + 1;
	}
	closedir(d);
	if (!out) return strdup("");
	out[total] = '\0';
	return out;
}

static char *hkExtractJsonString(const char *src, const char *key) {
	char pat[64];
	snprintf(pat, sizeof(pat), "\"%s\":\"", key);
	const char *p = strstr(src, pat);
	if (!p) return NULL;
	p += strlen(pat);
	const char *end = p;
	while (*end && !(*end == '"' && *(end - 1) != '\\')) end++;
	if (!*end) return NULL;
	int len = end - p;
	char *out = malloc(len + 1);
	memcpy(out, p, len);
	out[len] = '\0';
	return out;
}

static int hkExtractJsonInt(const char *src, const char *key) {
	if (!src) return -1;
	char pat[64];
	snprintf(pat, sizeof(pat), "\"%s\":", key);
	const char *p = strstr(src, pat);
	if (!p) return -1;
	p += strlen(pat);
	while (*p == ' ') p++;
	if (*p < '0' || *p > '9') return -1;
	return atoi(p);
}

static void hkUpdateUsage(aiData *data, const char *resp) {
	if (!data || !resp) return;
	int in = hkExtractJsonInt(resp, "input_tokens");
	int out = hkExtractJsonInt(resp, "output_tokens");
	if (in < 0) in = hkExtractJsonInt(resp, "prompt_tokens");
	if (out < 0) out = hkExtractJsonInt(resp, "completion_tokens");
	if (in < 0) in = hkExtractJsonInt(resp, "prompt_eval_count");
	if (out < 0) out = hkExtractJsonInt(resp, "eval_count");
	if (in >= 0) { data->last_in_tokens = in; data->total_in_tokens += in; }
	if (out >= 0) { data->last_out_tokens = out; data->total_out_tokens += out; }
}

static char *hkExtractJsonObject(const char *src, const char *key) {
	char pat[64];
	snprintf(pat, sizeof(pat), "\"%s\":{", key);
	const char *p = strstr(src, pat);
	if (!p) return NULL;
	p += strlen(pat) - 1;
	int depth = 0;
	const char *start = p;
	int in_str = 0, esc = 0;
	while (*p) {
		if (esc) { esc = 0; p++; continue; }
		if (*p == '\\') { esc = 1; p++; continue; }
		if (*p == '"') in_str = !in_str;
		else if (!in_str) {
			if (*p == '{') depth++;
			else if (*p == '}') { depth--; if (depth == 0) { p++; break; } }
		}
		p++;
	}
	int len = p - start;
	char *out = malloc(len + 1);
	memcpy(out, start, len);
	out[len] = '\0';
	return out;
}

static int hkResolveInProject(const char *path, char *out_full, size_t out_cap) {
	if (!path || !*path) return -1;
	char cwd[PATH_MAX];
	if (!getcwd(cwd, sizeof(cwd))) return -1;
	char resolved[PATH_MAX];
	char parent[PATH_MAX];
	snprintf(parent, sizeof(parent), "%s", path);
	char *slash = strrchr(parent, '/');
	const char *filename_part = NULL;
	if (slash) {
		*slash = '\0';
		filename_part = slash + 1;
		if (!realpath(parent[0] ? parent : ".", resolved)) return -1;
	} else {
		strncpy(resolved, cwd, sizeof(resolved));
		resolved[sizeof(resolved) - 1] = '\0';
		filename_part = path;
	}
	int cwd_len = strlen(cwd);
	if (strncmp(resolved, cwd, cwd_len) != 0 ||
		(resolved[cwd_len] != '\0' && resolved[cwd_len] != '/')) return -1;
	if (filename_part && *filename_part)
		snprintf(out_full, out_cap, "%s/%s", resolved, filename_part);
	else
		snprintf(out_full, out_cap, "%s", resolved);
	return 0;
}

static char *hkReadOpenFile(const char *path) {
	if (!path) return NULL;
	editorPane **panes = NULL;
	int count = 0;
	editorCollectLeafPanes(E.root_pane, &panes, &count);
	char *out = NULL;
	for (int i = 0; i < count; i++) {
		editorPane *p = panes[i];
		if (p->type != PANE_EDITOR || !p->filename) continue;
		const char *f = p->filename;
		const char *slash = strrchr(f, '/');
		const char *base = slash ? slash + 1 : f;
		if (strcmp(f, path) == 0 || strcmp(base, path) == 0) {
			size_t total = 0;
			for (int r = 0; r < p->numrows; r++) total += p->row[r].size + 1;
			out = malloc(total + 1);
			size_t off = 0;
			for (int r = 0; r < p->numrows; r++) {
				memcpy(out + off, p->row[r].chars, p->row[r].size);
				off += p->row[r].size;
				out[off++] = '\n';
			}
			out[off] = '\0';
			break;
		}
	}
	free(panes);
	return out;
}

static char *hkListOpenFiles(void) {
	editorPane **panes = NULL;
	int count = 0;
	editorCollectLeafPanes(E.root_pane, &panes, &count);
	size_t cap = 256, len = 0;
	char *out = malloc(cap);
	out[0] = '\0';
	for (int i = 0; i < count; i++) {
		editorPane *p = panes[i];
		if (p->type != PANE_EDITOR || !p->filename) continue;
		size_t fl = strlen(p->filename);
		if (len + fl + 16 >= cap) { cap = (len + fl + 16) * 2; out = realloc(out, cap); }
		len += snprintf(out + len, cap - len, "%s%s", p->filename, p->dirty ? " (modified)\n" : "\n");
	}
	free(panes);
	if (len == 0) { free(out); return strdup("(no files open)"); }
	return out;
}

typedef struct hkToolDef {
	const char *name;
	const char *description;
	const char *props;
	const char *required;
} hkToolDef;

static const hkToolDef HK_TOOLS[] = {
	{"read_file",
	 "Read contents of a file inside the project directory.",
	 "\"path\":{\"type\":\"string\"}",
	 "\"path\""},
	{"list_dir",
	 "List entries in a directory inside the project.",
	 "\"path\":{\"type\":\"string\"}",
	 "\"path\""},
	{"list_open_files",
	 "List files currently open in editor panes (with modification status).",
	 "",
	 ""},
	{"read_open_file",
	 "Read live (possibly unsaved) buffer of a file open in an editor pane. Match by full path or basename.",
	 "\"path\":{\"type\":\"string\"}",
	 "\"path\""},
	{"write_file",
	 "Create or overwrite a file in the trusted project directory.",
	 "\"path\":{\"type\":\"string\"},\"content\":{\"type\":\"string\"}",
	 "\"path\",\"content\""},
	{"run_shell",
	 "Run a non-interactive shell command. 10s timeout. Requires project trust.",
	 "\"cmd\":{\"type\":\"string\"}",
	 "\"cmd\""},
};
static const int HK_TOOL_COUNT = sizeof(HK_TOOLS) / sizeof(HK_TOOLS[0]);

static char *hkBuildToolsSchema(int provider_format) {
	size_t cap = 4096, len = 0;
	char *out = malloc(cap);
	out[len++] = '[';
	for (int i = 0; i < HK_TOOL_COUNT; i++) {
		const hkToolDef *t = &HK_TOOLS[i];
		if (i > 0) { if (len + 1 >= cap) { cap *= 2; out = realloc(out, cap); } out[len++] = ','; }
		size_t need = len + strlen(t->name) + strlen(t->description) + strlen(t->props) + strlen(t->required) + 256;
		if (need >= cap) { while (cap < need) cap *= 2; out = realloc(out, cap); }
		const char *props = t->props;
		const char *req_close = (*t->required) ? "],\"required\":[" : "";
		const char *req_close_end = (*t->required) ? "]" : "";
		if (provider_format == 0) {
			len += snprintf(out + len, cap - len,
				"{\"name\":\"%s\",\"description\":\"%s\",\"input_schema\":{\"type\":\"object\",\"properties\":{%s}%s%s%s}}",
				t->name, t->description, props,
				(*t->required) ? ",\"required\":[" : "",
				t->required, req_close_end);
		} else {
			len += snprintf(out + len, cap - len,
				"{\"type\":\"function\",\"function\":{\"name\":\"%s\",\"description\":\"%s\",\"parameters\":{\"type\":\"object\",\"properties\":{%s}%s%s%s}}}",
				t->name, t->description, props,
				(*t->required) ? ",\"required\":[" : "",
				t->required, req_close_end);
		}
		(void)req_close;
	}
	if (len + 2 >= cap) { cap += 4; out = realloc(out, cap); }
	out[len++] = ']';
	out[len] = '\0';
	return out;
}

static char *hkExecTool(const char *name, const char *input_json) {
	if (strcmp(name, "read_file") == 0) {
		char *path = hkExtractJsonString(input_json, "path");
		if (!path) return strdup("error: missing path");
		char full[PATH_MAX];
		if (hkResolveInProject(path, full, sizeof(full)) != 0) {
			free(path);
			return strdup("error: path outside project");
		}
		free(path);
		char *c = hkReadFileAll(full, 100000);
		return c ? c : strdup("error: cannot read");
	}
	if (strcmp(name, "list_dir") == 0) {
		char *path = hkExtractJsonString(input_json, "path");
		if (!path) return strdup("error: missing path");
		char full[PATH_MAX];
		if (hkResolveInProject(path, full, sizeof(full)) != 0) {
			free(path);
			return strdup("error: path outside project");
		}
		free(path);
		char *c = hkListDir(full);
		return c ? c : strdup("error: cannot list");
	}
	if (strcmp(name, "read_open_file") == 0) {
		char *path = hkExtractJsonString(input_json, "path");
		if (!path) return strdup("error: missing path");
		char *c = hkReadOpenFile(path);
		free(path);
		return c ? c : strdup("error: file not open in any editor pane");
	}
	if (strcmp(name, "list_open_files") == 0) {
		return hkListOpenFiles();
	}
	if (strcmp(name, "run_shell") == 0) {
		if (!hkProjectTrusted()) return strdup("error: project not trusted");
		char *shcmd = hkExtractJsonString(input_json, "cmd");
		if (!shcmd) return strdup("error: missing cmd");
		char *c = hkRunShellCapture(shcmd, 50000);
		free(shcmd);
		return c;
	}
	if (strcmp(name, "write_file") == 0) {
		if (!hkProjectTrusted()) return strdup("error: project not trusted");
		char *path = hkExtractJsonString(input_json, "path");
		char *content = hkExtractJsonString(input_json, "content");
		if (!path || !content) {
			free(path); free(content);
			return strdup("error: missing path or content");
		}
		char full[PATH_MAX];
		if (hkResolveInProject(path, full, sizeof(full)) != 0) {
			free(path); free(content);
			return strdup("error: path outside trusted project");
		}
		size_t clen = strlen(content);
		int new_lines = 0;
		for (size_t i = 0; i < clen; i++) if (content[i] == '\n') new_lines++;
		if (clen > 0 && content[clen - 1] != '\n') new_lines++;
		long old_size = -1;
		int old_lines = 0;
		FILE *ef = fopen(full, "r");
		if (ef) {
			fseek(ef, 0, SEEK_END);
			old_size = ftell(ef);
			fseek(ef, 0, SEEK_SET);
			int ch;
			while ((ch = fgetc(ef)) != EOF) if (ch == '\n') old_lines++;
			fclose(ef);
		}
		if (!E.ai_autowrite) {
			char pending[PATH_MAX + 16];
			snprintf(pending, sizeof(pending), "%s.hako-pending", full);
			FILE *fp = fopen(pending, "w");
			if (!fp) { free(path); free(content); return strdup("error: cannot stage pending"); }
			fwrite(content, 1, clen, fp);
			fclose(fp);
			char *out = malloc(512);
			snprintf(out, 512, "preview staged at %s (new %zu bytes/%d lines, old %ld/%d). ai_autowrite=0; user must `mv` pending to apply.",
				pending, clen, new_lines, old_size, old_lines);
			free(path); free(content);
			return out;
		}
		FILE *fp = fopen(full, "w");
		if (!fp) { free(path); free(content); return strdup("error: cannot open for write"); }
		size_t wrote = fwrite(content, 1, clen, fp);
		fclose(fp);
		char *out = malloc(256);
		snprintf(out, 256, "wrote %zu bytes to %s (new %d lines, old %d)", wrote, full, new_lines, old_lines);
		free(path); free(content);
		return out;
	}
	return strdup("error: unknown tool");
}

static void hkAnnounceTool(aiData *data, const char *fname, const char *args_obj);
static void hkAnnounceToolResult(aiData *data, const char *result);

static char *hkBuildToolResults(aiData *data, const char *content_array) {
	char *out = malloc(32);
	int cap = 32, len = 0;
	out[0] = '[';
	len = 1;
	const char *p = content_array;
	int first = 1;
	while ((p = strstr(p, "\"type\":\"tool_use\""))) {
		const char *block_start = p;
		while (block_start > content_array && *block_start != '{') block_start--;
		char *id = hkExtractJsonString(block_start, "id");
		char *name = hkExtractJsonString(block_start, "name");
		char *input_obj = hkExtractJsonObject(block_start, "input");
		if (!id || !name || !input_obj) {
			free(id); free(name); free(input_obj);
			p++; continue;
		}
		hkAnnounceTool(data, name, input_obj);
		char *result = hkExecTool(name, input_obj);
		hkAnnounceToolResult(data, result);
		int rlen = strlen(result);
		char *esc = malloc(rlen * 6 + 8);
		hkJsonEscapeInto(result, esc, rlen * 6 + 8);
		int need = len + strlen(id) + strlen(esc) + 128;
		if (need >= cap) { while (cap < need) cap *= 2; out = realloc(out, cap); }
		if (!first) out[len++] = ',';
		first = 0;
		len += snprintf(out + len, cap - len,
			"{\"type\":\"tool_result\",\"tool_use_id\":\"%s\",\"content\":\"%s\"}",
			id, esc);
		free(id); free(name); free(input_obj); free(result); free(esc);
		p++;
	}
	if (len + 2 >= cap) { cap += 4; out = realloc(out, cap); }
	out[len++] = ']';
	out[len] = '\0';
	return out;
}

static void hkAnnounceTool(aiData *data, const char *fname, const char *args_obj) {
	char arg_summary[128] = "";
	char *path = hkExtractJsonString(args_obj, "path");
	char *cmd = hkExtractJsonString(args_obj, "cmd");
	if (path) { snprintf(arg_summary, sizeof(arg_summary), "%s", path); free(path); }
	else if (cmd) { snprintf(arg_summary, sizeof(arg_summary), "%.80s", cmd); free(cmd); }
	char msg[256];
	snprintf(msg, sizeof(msg), "→ %s(%s)", fname, arg_summary);
	pthread_mutex_lock(&data->lock);
	aiAddHistory(data, msg);
	pthread_mutex_unlock(&data->lock);
}

static void hkAnnounceToolResult(aiData *data, const char *result) {
	int len = result ? (int)strlen(result) : 0;
	int is_err = result && strncmp(result, "error:", 6) == 0;
	char msg[128];
	if (is_err) snprintf(msg, sizeof(msg), "  %s", result);
	else snprintf(msg, sizeof(msg), "  ← %d bytes", len);
	pthread_mutex_lock(&data->lock);
	aiAddHistory(data, msg);
	pthread_mutex_unlock(&data->lock);
}

static char *hkJsonUnescape(const char *s, int n);

static int hkFnToolExecAll(aiData *data, const char *response) {
	const char *p = strstr(response, "\"tool_calls\":[");
	if (!p) return 0;
	int count = 0;
	while ((p = strstr(p, "\"function\""))) {
		char *fname = hkExtractJsonString(p, "name");
		char *args_obj = hkExtractJsonObject(p, "arguments");
		if (!args_obj) {
			char *args_str = hkExtractJsonString(p, "arguments");
			if (args_str) {
				args_obj = hkJsonUnescape(args_str, (int)strlen(args_str));
				free(args_str);
			}
		}
		if (!fname || !args_obj) { free(fname); free(args_obj); p++; continue; }
		hkAnnounceTool(data, fname, args_obj);
		char *result = hkExecTool(fname, args_obj);
		hkAnnounceToolResult(data, result);
		pthread_mutex_lock(&data->lock);
		aiPushMessage(data, "tool", result ? result : "");
		pthread_mutex_unlock(&data->lock);
		free(fname); free(args_obj); free(result);
		count++;
		p++;
	}
	return count;
}

static char *hkExtractContentArray(const char *response) {
	const char *p = strstr(response, "\"content\":[");
	if (!p) return NULL;
	p += strlen("\"content\":") ;
	const char *start = p;
	int depth = 0, in_str = 0, esc = 0;
	while (*p) {
		if (esc) { esc = 0; p++; continue; }
		if (*p == '\\') { esc = 1; p++; continue; }
		if (*p == '"') in_str = !in_str;
		else if (!in_str) {
			if (*p == '[') depth++;
			else if (*p == ']') { depth--; if (depth == 0) { p++; break; } }
		}
		p++;
	}
	int len = p - start;
	char *out = malloc(len + 1);
	memcpy(out, start, len);
	out[len] = '\0';
	return out;
}

void *aiWorkerThread(void *arg) {
	aiData *data = (aiData *)arg;

	pthread_mutex_lock(&data->lock);
	char *prompt = data->current_prompt ? strdup(data->current_prompt) : NULL;
	if (prompt) aiPushMessage(data, "user", prompt);
	pthread_mutex_unlock(&data->lock);
	free(prompt);

	int max_iters = 6;
	int iter = 0;
	int used_tool = 0;

	while (iter++ < max_iters) {
		pthread_mutex_lock(&data->lock);
		char *cmd = aiBuildCurlCommand(data, E.ai_provider_type);
		pthread_mutex_unlock(&data->lock);

		if (!cmd) {
			pthread_mutex_lock(&data->lock);
			aiAddHistory(data, "Error: Provider not configured.");
			data->streaming = 0;
			pthread_mutex_unlock(&data->lock);
			return NULL;
		}

		FILE *fp = popen(cmd, "r");
		free(cmd);
		if (!fp) {
			pthread_mutex_lock(&data->lock);
			aiAddHistory(data, "Error: Could not execute curl");
			data->streaming = 0;
			pthread_mutex_unlock(&data->lock);
			return NULL;
		}

		int streaming_mode = (E.ai_provider_type == AI_PROVIDER_ANTHROPIC
			&& E.ai_stream && !E.ai_tools_enabled);

		char buffer[8192];
		char *full_response = NULL;
		size_t total = 0;

		if (streaming_mode) {
			pthread_mutex_lock(&data->lock);
			int stream_idx = data->history_count;
			if (stream_idx < AI_HISTORY_MAX) {
				data->history_role[stream_idx] = HK_ROLE_AI;
				data->history[stream_idx] = strdup("");
				data->history_count++;
			} else {
				stream_idx = -1;
			}
			pthread_mutex_unlock(&data->lock);

			char *acc = calloc(1, 1);
			size_t acc_len = 0;

			while (fgets(buffer, sizeof(buffer), fp)) {
				int blen = strlen(buffer);
				full_response = realloc(full_response, total + blen + 1);
				memcpy(full_response + total, buffer, blen);
				total += blen;

				if (strncmp(buffer, "data: ", 6) != 0) continue;
				const char *payload = buffer + 6;
				if (!strstr(payload, "content_block_delta")) continue;
				char *text = hkExtractJsonString(payload, "text");
				if (!text) continue;

				size_t tlen = strlen(text);
				acc = realloc(acc, acc_len + tlen + 1);
				memcpy(acc + acc_len, text, tlen);
				acc_len += tlen;
				acc[acc_len] = '\0';
				free(text);

				if (stream_idx >= 0) {
					pthread_mutex_lock(&data->lock);
					free(data->history[stream_idx]);
					char *disp = strdup(acc);
					if (disp) {
						for (char *q = disp; *q; q++) if (*q == '\n') *q = ' ';
					}
					data->history[stream_idx] = disp;
					data->history_pos = MAX(0, data->history_count - 20);
					pthread_mutex_unlock(&data->lock);
				}
			}
			pclose(fp);
			if (full_response) full_response[total] = '\0';

			if (acc_len > 0) {
				pthread_mutex_lock(&data->lock);
				hkUpdateUsage(data, full_response);
				if (stream_idx >= 0 && stream_idx == data->history_count - 1) {
					free(data->history[stream_idx]);
					data->history[stream_idx] = NULL;
					data->history_count--;
					aiAddHistoryRole(data, acc, HK_ROLE_AI);
					data->history_pos = MAX(0, data->history_count - 20);
				}
				aiPushMessage(data, "assistant", acc);
				hkLogMessage("assistant", acc);
				free(data->current_response);
				data->current_response = acc;
				pthread_mutex_unlock(&data->lock);
			} else {
				free(acc);
				pthread_mutex_lock(&data->lock);
				aiAddHistory(data, "Error: Empty stream");
				pthread_mutex_unlock(&data->lock);
			}

			free(full_response);
			break;
		}

		while (fgets(buffer, sizeof(buffer), fp)) {
			int blen = strlen(buffer);
			full_response = realloc(full_response, total + blen + 1);
			memcpy(full_response + total, buffer, blen);
			total += blen;
		}
		pclose(fp);
		if (full_response) full_response[total] = '\0';

		int tool_use_anthropic = E.ai_provider_type == AI_PROVIDER_ANTHROPIC
			&& full_response
			&& strstr(full_response, "\"stop_reason\":\"tool_use\"");
		int tool_use_fn = (E.ai_provider_type == AI_PROVIDER_OLLAMA || E.ai_provider_type == AI_PROVIDER_OPENAI)
			&& full_response
			&& strstr(full_response, "\"tool_calls\":[");

		char *content = aiExtractResponse(full_response, E.ai_provider_type);

		pthread_mutex_lock(&data->lock);
		hkUpdateUsage(data, full_response);
		if (content && *content) {
			aiAddHistoryRole(data, content, HK_ROLE_AI);
			hkLogMessage("assistant", content);
			free(data->current_response);
			data->current_response = strdup(content);
		}
		pthread_mutex_unlock(&data->lock);

		if (tool_use_anthropic) {
			char *content_array = hkExtractContentArray(full_response);
			if (!content_array) { free(content); free(full_response); break; }

			pthread_mutex_lock(&data->lock);
			aiPushMessageRaw(data, "assistant", content_array);
			pthread_mutex_unlock(&data->lock);

			char *results = hkBuildToolResults(data, content_array);

			pthread_mutex_lock(&data->lock);
			aiPushMessageRaw(data, "user", results);
			pthread_mutex_unlock(&data->lock);

			free(content_array);
			free(results);
			free(content);
			free(full_response);
			used_tool = 1;
			continue;
		}

		if (tool_use_fn) {
			pthread_mutex_lock(&data->lock);
			aiPushMessage(data, "assistant", content && *content ? content : "(calling tools)");
			pthread_mutex_unlock(&data->lock);

			int n = hkFnToolExecAll(data, full_response);
			(void)n;

			free(content);
			free(full_response);
			if (n == 0) break;
			used_tool = 1;
			continue;
		}

		if (content) {
			pthread_mutex_lock(&data->lock);
			aiPushMessage(data, "assistant", content);
			pthread_mutex_unlock(&data->lock);
		} else {
			pthread_mutex_lock(&data->lock);
			aiAddHistory(data, "Error: Empty response");
			pthread_mutex_unlock(&data->lock);
		}

		free(content);
		free(full_response);
		break;
	}

	pthread_mutex_lock(&data->lock);
	if (iter >= max_iters && used_tool) aiAddHistory(data, "(tool loop cap reached)");
	data->streaming = 0;
	data->history_pos = MAX(0, data->history_count - 20);
	pthread_mutex_unlock(&data->lock);

	return NULL;
}

void aiWorkerSend(aiData *data) {
	if (!data || data->streaming) return;

	data->streaming = 1;
	pthread_t thread;
	if (pthread_create(&thread, NULL, aiWorkerThread, data) == 0) {
		pthread_detach(thread);
	} else {
		data->streaming = 0;
	}
}

void editorGenerateConfig() {
	const char *home = getenv("HOME");
	if (!home) {
		editorSetStatusMessage("Cannot find HOME directory");
		return;
	}
	
	char path[512];
	snprintf(path, sizeof(path), "%s/.hakorc", home);
	
	FILE *fp = fopen(path, "w");
	if (!fp) {
		editorSetStatusMessage("Cannot create config file: %s", strerror(errno));
		return;
	}
	
	fprintf(fp, "# 箱 Hako Configuration\n");
	fprintf(fp, "# Every knob is listed below. Uncomment to change.\n");
	fprintf(fp, "# Last-wins: later values override earlier ones.\n\n");

	fprintf(fp, "# ============================================================\n");
	fprintf(fp, "#  Editor basics\n");
	fprintf(fp, "# ============================================================\n\n");

	fprintf(fp, "# Tab width in columns.\n");
	fprintf(fp, "tab_stop=4\n\n");

	fprintf(fp, "# 1 = real tab char. 0 = expand tabs to spaces.\n");
	fprintf(fp, "use_tabs=1\n\n");

	fprintf(fp, "# 1 = soft-wrap long lines. 0 = horizontal scroll.\n");
	fprintf(fp, "word_wrap=1\n\n");

	fprintf(fp, "# 0=off, 1=absolute, 2=relative.\n");
	fprintf(fp, "show_line_numbers=1\n\n");

	fprintf(fp, "# Undo stack depth.\n");
	fprintf(fp, "max_undo_levels=100\n\n");

	fprintf(fp, "# Match previous line's indent when opening a new line.\n");
	fprintf(fp, "auto_indent=1\n\n");

	fprintf(fp, "# Add one indent level after '{', ':', etc.\n");
	fprintf(fp, "smart_indent=1\n\n");

	fprintf(fp, "# Mouse click-to-position + scroll wheel.\n");
	fprintf(fp, "mouse_enabled=1\n\n");

	fprintf(fp, "# Lines per mouse-wheel tick.\n");
	fprintf(fp, "scroll_speed=3\n\n");

	fprintf(fp, "# ============================================================\n");
	fprintf(fp, "#  紙 Kami — file explorer (left panel)\n");
	fprintf(fp, "# ============================================================\n\n");

	fprintf(fp, "# Show explorer panel at startup.\n");
	fprintf(fp, "explorer_enabled=1\n\n");

	fprintf(fp, "# Panel width in columns (10-60).\n");
	fprintf(fp, "explorer_width=30\n\n");

	fprintf(fp, "# Show dotfiles in explorer.\n");
	fprintf(fp, "explorer_show_hidden=0\n\n");

	fprintf(fp, "# ============================================================\n");
	fprintf(fp, "#  零 Rei — AI assistant (right panel)\n");
	fprintf(fp, "# ============================================================\n\n");

	fprintf(fp, "# Provider: ollama | anthropic | openai\n");
	fprintf(fp, "# (anthropic also accepts 'claude', openai also accepts 'gpt' or 'groq')\n");
	fprintf(fp, "ai_provider=ollama\n\n");

	fprintf(fp, "# API key. Leave blank for ollama (local).\n");
	fprintf(fp, "ai_api_key=\n\n");

	fprintf(fp, "# Endpoint root (scheme + host). Paths appended per provider.\n");
	fprintf(fp, "#   ollama    -> http://localhost:11434\n");
	fprintf(fp, "#   anthropic -> https://api.anthropic.com\n");
	fprintf(fp, "#   openai    -> https://api.openai.com\n");
	fprintf(fp, "#   groq      -> https://api.groq.com/openai\n");
	fprintf(fp, "ai_endpoint=http://localhost:11434\n\n");

	fprintf(fp, "# Model id.\n");
	fprintf(fp, "#   ollama    -> llama3.2, codellama, mistral, qwen2.5-coder, ...\n");
	fprintf(fp, "#   anthropic -> claude-sonnet-4-20250514, claude-opus-4-20250514, ...\n");
	fprintf(fp, "#   openai    -> gpt-4o-mini, gpt-4o, o1-mini, ...\n");
	fprintf(fp, "#   groq      -> llama-3.1-70b-versatile, mixtral-8x7b-32768, ...\n");
	fprintf(fp, "ai_model=llama3.2\n\n");

	fprintf(fp, "# Max tokens per response.\n");
	fprintf(fp, "ai_max_tokens=2048\n\n");

	fprintf(fp, "# Function-calling / tool loop (Anthropic only for now).\n");
	fprintf(fp, "# 1 = read_file/list_dir/run_shell available to the model.\n");
	fprintf(fp, "ai_tools_enabled=1\n\n");

	fprintf(fp, "# SSE token streaming. Auto-disabled when tools are on.\n");
	fprintf(fp, "ai_stream=1\n\n");

	fprintf(fp, "# ============================================================\n");
	fprintf(fp, "#  Theme preset\n");
	fprintf(fp, "# ============================================================\n");
	fprintf(fp, "# Dark:  dark, gruvbox, nord, dracula, monokai, solarized,\n");
	fprintf(fp, "#        tokyonight, catppuccin, onedark, material,\n");
	fprintf(fp, "#        everforest, rosepine, github-dark, ayu, kanagawa\n");
	fprintf(fp, "# Light: light, github-light\n");
	fprintf(fp, "theme=dark\n\n");

	fprintf(fp, "# ============================================================\n");
	fprintf(fp, "#  Custom colors (RGB 0-255). Override preset values.\n");
	fprintf(fp, "# ============================================================\n\n");

	fprintf(fp, "# Surfaces\n");
	fprintf(fp, "# theme_bg=30,30,30\n");
	fprintf(fp, "# theme_fg=212,212,212\n\n");

	fprintf(fp, "# Syntax\n");
	fprintf(fp, "# theme_comment=106,153,85\n");
	fprintf(fp, "# theme_keyword1=86,156,214\n");
	fprintf(fp, "# theme_keyword2=78,201,176\n");
	fprintf(fp, "# theme_string=206,145,120\n");
	fprintf(fp, "# theme_number=181,206,168\n\n");

	fprintf(fp, "# Chrome\n");
	fprintf(fp, "# theme_line_number=133,133,133\n");
	fprintf(fp, "# theme_status_bg=50,50,50\n");
	fprintf(fp, "# theme_status_fg=212,212,212\n");
	fprintf(fp, "# theme_border=80,80,80\n");
	fprintf(fp, "# theme_visual_bg=38,79,120\n");
	fprintf(fp, "# theme_visual_fg=255,255,255\n\n");

	fprintf(fp, "# ============================================================\n");
	fprintf(fp, "#  Starter combos — uncomment one block\n");
	fprintf(fp, "# ============================================================\n\n");

	fprintf(fp, "# --- Prose ---\n");
	fprintf(fp, "# theme=github-light\n");
	fprintf(fp, "# show_line_numbers=0\n");
	fprintf(fp, "# word_wrap=1\n");
	fprintf(fp, "# explorer_enabled=0\n\n");

	fprintf(fp, "# --- Keyboard-only C ---\n");
	fprintf(fp, "# theme=gruvbox\n");
	fprintf(fp, "# tab_stop=4\n");
	fprintf(fp, "# show_line_numbers=2\n");
	fprintf(fp, "# mouse_enabled=0\n\n");

	fprintf(fp, "# --- Local LLM dev ---\n");
	fprintf(fp, "# theme=kanagawa\n");
	fprintf(fp, "# ai_provider=ollama\n");
	fprintf(fp, "# ai_model=qwen2.5-coder\n");
	fprintf(fp, "# ai_tools_enabled=0\n\n");

	fprintf(fp, "# --- Remote agentic ---\n");
	fprintf(fp, "# theme=tokyonight\n");
	fprintf(fp, "# ai_provider=anthropic\n");
	fprintf(fp, "# ai_model=claude-sonnet-4-20250514\n");
	fprintf(fp, "# ai_tools_enabled=1\n");
	fprintf(fp, "# ai_stream=0\n\n");
	
	fclose(fp);
	editorSetStatusMessage("Config created: %s", path);
}

/*** init ***/
int editorApplyThemeByName(const char *name) {
	if (!name) return 0;
	if (strcmp(name, "dark") == 0) {
		E.theme_preset = THEME_DARK;
		E.theme.bg = (Color){30, 30, 30};
		E.theme.fg = (Color){212, 212, 212};
		E.theme.comment = (Color){106, 153, 85};
		E.theme.keyword1 = (Color){86, 156, 214};
		E.theme.keyword2 = (Color){78, 201, 176};
		E.theme.string = (Color){206, 145, 120};
		E.theme.number = (Color){181, 206, 168};
		E.theme.line_number = (Color){133, 133, 133};
		E.theme.status_bg = (Color){50, 50, 50};
		E.theme.status_fg = (Color){212, 212, 212};
		E.theme.border = (Color){80, 80, 80};
		E.theme.visual_bg = (Color){38, 79, 120};
		E.theme.visual_fg = (Color){255, 255, 255};
	} else if (strcmp(name, "light") == 0) {
		E.theme_preset = THEME_LIGHT;
		E.theme.bg = (Color){255, 255, 255};
		E.theme.fg = (Color){30, 30, 30};
		E.theme.comment = (Color){0, 128, 0};
		E.theme.keyword1 = (Color){0, 0, 200};
		E.theme.keyword2 = (Color){0, 128, 128};
		E.theme.string = (Color){163, 21, 21};
		E.theme.number = (Color){9, 134, 88};
		E.theme.line_number = (Color){150, 150, 150};
		E.theme.status_bg = (Color){220, 220, 220};
		E.theme.status_fg = (Color){30, 30, 30};
		E.theme.border = (Color){180, 180, 180};
		E.theme.visual_bg = (Color){173, 214, 255};
		E.theme.visual_fg = (Color){0, 0, 0};
	} else if (strcmp(name, "gruvbox") == 0) {
		E.theme_preset = THEME_GRUVBOX;
		E.theme.bg = (Color){40, 40, 40};
		E.theme.fg = (Color){235, 219, 178};
		E.theme.comment = (Color){146, 131, 116};
		E.theme.keyword1 = (Color){251, 73, 52};
		E.theme.keyword2 = (Color){184, 187, 38};
		E.theme.string = (Color){184, 187, 38};
		E.theme.number = (Color){211, 134, 155};
		E.theme.line_number = (Color){124, 111, 100};
		E.theme.status_bg = (Color){80, 73, 69};
		E.theme.status_fg = (Color){235, 219, 178};
		E.theme.border = (Color){80, 73, 69};
		E.theme.visual_bg = (Color){68, 65, 59};
		E.theme.visual_fg = (Color){235, 219, 178};
	} else if (strcmp(name, "nord") == 0) {
		E.theme_preset = THEME_NORD;
		E.theme.bg = (Color){46, 52, 64};
		E.theme.fg = (Color){216, 222, 233};
		E.theme.comment = (Color){76, 86, 106};
		E.theme.keyword1 = (Color){129, 161, 193};
		E.theme.keyword2 = (Color){136, 192, 208};
		E.theme.string = (Color){163, 190, 140};
		E.theme.number = (Color){180, 142, 173};
		E.theme.line_number = (Color){76, 86, 106};
		E.theme.status_bg = (Color){59, 66, 82};
		E.theme.status_fg = (Color){216, 222, 233};
		E.theme.border = (Color){59, 66, 82};
		E.theme.visual_bg = (Color){67, 76, 94};
		E.theme.visual_fg = (Color){216, 222, 233};
	} else if (strcmp(name, "dracula") == 0) {
		E.theme_preset = THEME_DRACULA;
		E.theme.bg = (Color){40, 42, 54};
		E.theme.fg = (Color){248, 248, 242};
		E.theme.comment = (Color){98, 114, 164};
		E.theme.keyword1 = (Color){255, 121, 198};
		E.theme.keyword2 = (Color){139, 233, 253};
		E.theme.string = (Color){241, 250, 140};
		E.theme.number = (Color){189, 147, 249};
		E.theme.line_number = (Color){98, 114, 164};
		E.theme.status_bg = (Color){68, 71, 90};
		E.theme.status_fg = (Color){248, 248, 242};
		E.theme.border = (Color){68, 71, 90};
		E.theme.visual_bg = (Color){68, 71, 90};
		E.theme.visual_fg = (Color){248, 248, 242};
	} else if (strcmp(name, "monokai") == 0) {
		E.theme_preset = THEME_MONOKAI;
		E.theme.bg = (Color){39, 40, 34};
		E.theme.fg = (Color){248, 248, 242};
		E.theme.comment = (Color){117, 113, 94};
		E.theme.keyword1 = (Color){249, 38, 114};
		E.theme.keyword2 = (Color){102, 217, 239};
		E.theme.string = (Color){230, 219, 116};
		E.theme.number = (Color){174, 129, 255};
		E.theme.line_number = (Color){117, 113, 94};
		E.theme.status_bg = (Color){60, 61, 55};
		E.theme.status_fg = (Color){248, 248, 242};
		E.theme.border = (Color){60, 61, 55};
		E.theme.visual_bg = (Color){73, 72, 62};
		E.theme.visual_fg = (Color){248, 248, 242};
	} else if (strcmp(name, "solarized") == 0) {
		E.theme_preset = THEME_SOLARIZED;
		E.theme.bg = (Color){0, 43, 54};
		E.theme.fg = (Color){131, 148, 150};
		E.theme.comment = (Color){88, 110, 117};
		E.theme.keyword1 = (Color){38, 139, 210};
		E.theme.keyword2 = (Color){42, 161, 152};
		E.theme.string = (Color){42, 161, 152};
		E.theme.number = (Color){211, 54, 130};
		E.theme.line_number = (Color){88, 110, 117};
		E.theme.status_bg = (Color){7, 54, 66};
		E.theme.status_fg = (Color){147, 161, 161};
		E.theme.border = (Color){7, 54, 66};
		E.theme.visual_bg = (Color){7, 54, 66};
		E.theme.visual_fg = (Color){147, 161, 161};
	} else if (strcmp(name, "tokyonight") == 0) {
		E.theme_preset = THEME_TOKYONIGHT;
		E.theme.bg = (Color){26, 27, 38};
		E.theme.fg = (Color){192, 202, 245};
		E.theme.comment = (Color){86, 95, 137};
		E.theme.keyword1 = (Color){187, 154, 247};
		E.theme.keyword2 = (Color){125, 207, 255};
		E.theme.string = (Color){158, 206, 106};
		E.theme.number = (Color){255, 158, 100};
		E.theme.line_number = (Color){60, 68, 102};
		E.theme.status_bg = (Color){36, 40, 59};
		E.theme.status_fg = (Color){192, 202, 245};
		E.theme.border = (Color){60, 68, 102};
		E.theme.visual_bg = (Color){40, 52, 87};
		E.theme.visual_fg = (Color){192, 202, 245};
	} else if (strcmp(name, "catppuccin") == 0) {
		E.theme_preset = THEME_CATPPUCCIN;
		E.theme.bg = (Color){30, 30, 46};
		E.theme.fg = (Color){205, 214, 244};
		E.theme.comment = (Color){108, 112, 134};
		E.theme.keyword1 = (Color){203, 166, 247};
		E.theme.keyword2 = (Color){137, 220, 235};
		E.theme.string = (Color){166, 227, 161};
		E.theme.number = (Color){250, 179, 135};
		E.theme.line_number = (Color){88, 91, 112};
		E.theme.status_bg = (Color){49, 50, 68};
		E.theme.status_fg = (Color){205, 214, 244};
		E.theme.border = (Color){69, 71, 90};
		E.theme.visual_bg = (Color){69, 71, 90};
		E.theme.visual_fg = (Color){205, 214, 244};
	} else if (strcmp(name, "onedark") == 0) {
		E.theme_preset = THEME_ONEDARK;
		E.theme.bg = (Color){40, 44, 52};
		E.theme.fg = (Color){171, 178, 191};
		E.theme.comment = (Color){92, 99, 112};
		E.theme.keyword1 = (Color){198, 120, 221};
		E.theme.keyword2 = (Color){86, 182, 194};
		E.theme.string = (Color){152, 195, 121};
		E.theme.number = (Color){209, 154, 102};
		E.theme.line_number = (Color){76, 82, 99};
		E.theme.status_bg = (Color){33, 37, 43};
		E.theme.status_fg = (Color){171, 178, 191};
		E.theme.border = (Color){60, 64, 72};
		E.theme.visual_bg = (Color){62, 68, 81};
		E.theme.visual_fg = (Color){220, 223, 228};
	} else if (strcmp(name, "material") == 0) {
		E.theme_preset = THEME_MATERIAL;
		E.theme.bg = (Color){38, 50, 56};
		E.theme.fg = (Color){238, 255, 255};
		E.theme.comment = (Color){84, 110, 122};
		E.theme.keyword1 = (Color){199, 146, 234};
		E.theme.keyword2 = (Color){130, 170, 255};
		E.theme.string = (Color){195, 232, 141};
		E.theme.number = (Color){247, 140, 108};
		E.theme.line_number = (Color){63, 81, 88};
		E.theme.status_bg = (Color){32, 43, 48};
		E.theme.status_fg = (Color){238, 255, 255};
		E.theme.border = (Color){63, 81, 88};
		E.theme.visual_bg = (Color){51, 71, 79};
		E.theme.visual_fg = (Color){238, 255, 255};
	} else if (strcmp(name, "everforest") == 0) {
		E.theme_preset = THEME_EVERFOREST;
		E.theme.bg = (Color){45, 53, 59};
		E.theme.fg = (Color){211, 198, 170};
		E.theme.comment = (Color){133, 146, 137};
		E.theme.keyword1 = (Color){230, 126, 128};
		E.theme.keyword2 = (Color){127, 187, 179};
		E.theme.string = (Color){167, 192, 128};
		E.theme.number = (Color){219, 188, 127};
		E.theme.line_number = (Color){86, 99, 98};
		E.theme.status_bg = (Color){55, 63, 68};
		E.theme.status_fg = (Color){211, 198, 170};
		E.theme.border = (Color){67, 76, 81};
		E.theme.visual_bg = (Color){67, 76, 81};
		E.theme.visual_fg = (Color){211, 198, 170};
	} else if (strcmp(name, "rosepine") == 0) {
		E.theme_preset = THEME_ROSEPINE;
		E.theme.bg = (Color){25, 23, 36};
		E.theme.fg = (Color){224, 222, 244};
		E.theme.comment = (Color){110, 106, 134};
		E.theme.keyword1 = (Color){196, 167, 231};
		E.theme.keyword2 = (Color){156, 207, 216};
		E.theme.string = (Color){246, 193, 119};
		E.theme.number = (Color){235, 188, 186};
		E.theme.line_number = (Color){82, 79, 103};
		E.theme.status_bg = (Color){38, 35, 58};
		E.theme.status_fg = (Color){224, 222, 244};
		E.theme.border = (Color){49, 46, 73};
		E.theme.visual_bg = (Color){64, 61, 82};
		E.theme.visual_fg = (Color){224, 222, 244};
	} else if (strcmp(name, "github-dark") == 0) {
		E.theme_preset = THEME_GITHUB_DARK;
		E.theme.bg = (Color){13, 17, 23};
		E.theme.fg = (Color){201, 209, 217};
		E.theme.comment = (Color){139, 148, 158};
		E.theme.keyword1 = (Color){255, 123, 114};
		E.theme.keyword2 = (Color){121, 192, 255};
		E.theme.string = (Color){165, 214, 255};
		E.theme.number = (Color){121, 192, 255};
		E.theme.line_number = (Color){72, 79, 88};
		E.theme.status_bg = (Color){22, 27, 34};
		E.theme.status_fg = (Color){201, 209, 217};
		E.theme.border = (Color){48, 54, 61};
		E.theme.visual_bg = (Color){33, 38, 45};
		E.theme.visual_fg = (Color){201, 209, 217};
	} else if (strcmp(name, "github-light") == 0) {
		E.theme_preset = THEME_GITHUB_LIGHT;
		E.theme.bg = (Color){255, 255, 255};
		E.theme.fg = (Color){36, 41, 47};
		E.theme.comment = (Color){106, 115, 125};
		E.theme.keyword1 = (Color){207, 34, 46};
		E.theme.keyword2 = (Color){5, 80, 174};
		E.theme.string = (Color){10, 48, 105};
		E.theme.number = (Color){5, 80, 174};
		E.theme.line_number = (Color){140, 149, 159};
		E.theme.status_bg = (Color){240, 246, 252};
		E.theme.status_fg = (Color){36, 41, 47};
		E.theme.border = (Color){208, 215, 222};
		E.theme.visual_bg = (Color){221, 244, 255};
		E.theme.visual_fg = (Color){36, 41, 47};
	} else if (strcmp(name, "ayu") == 0) {
		E.theme_preset = THEME_AYU;
		E.theme.bg = (Color){15, 20, 25};
		E.theme.fg = (Color){191, 189, 182};
		E.theme.comment = (Color){92, 103, 115};
		E.theme.keyword1 = (Color){255, 143, 64};
		E.theme.keyword2 = (Color){57, 186, 230};
		E.theme.string = (Color){170, 217, 76};
		E.theme.number = (Color){215, 155, 255};
		E.theme.line_number = (Color){60, 75, 90};
		E.theme.status_bg = (Color){20, 25, 31};
		E.theme.status_fg = (Color){191, 189, 182};
		E.theme.border = (Color){38, 48, 59};
		E.theme.visual_bg = (Color){38, 48, 59};
		E.theme.visual_fg = (Color){191, 189, 182};
	} else if (strcmp(name, "kanagawa") == 0) {
		E.theme_preset = THEME_KANAGAWA;
		E.theme.bg = (Color){31, 31, 40};
		E.theme.fg = (Color){220, 215, 186};
		E.theme.comment = (Color){114, 113, 105};
		E.theme.keyword1 = (Color){149, 127, 184};
		E.theme.keyword2 = (Color){127, 180, 202};
		E.theme.string = (Color){152, 187, 108};
		E.theme.number = (Color){210, 126, 153};
		E.theme.line_number = (Color){84, 84, 109};
		E.theme.status_bg = (Color){42, 42, 55};
		E.theme.status_fg = (Color){220, 215, 186};
		E.theme.border = (Color){54, 54, 70};
		E.theme.visual_bg = (Color){43, 57, 63};
		E.theme.visual_fg = (Color){220, 215, 186};
	} else {
		return 0;
	}
	gridInvalidateFront();
	E.full_redraw_pending = 1;
	return 1;
}

void initTheme() {
	E.theme.bg = (Color){30, 30, 30};
	E.theme.fg = (Color){212, 212, 212};
	E.theme.comment = (Color){106, 153, 85};
	E.theme.keyword1 = (Color){86, 156, 214};
	E.theme.keyword2 = (Color){78, 201, 176};
	E.theme.string = (Color){206, 145, 120};
	E.theme.number = (Color){181, 206, 168};
	E.theme.match = (Color){255, 255, 0};
	E.theme.preprocessor = (Color){197, 134, 192};
	E.theme.function = (Color){220, 220, 170};
	E.theme.type = (Color){78, 201, 176};
	E.theme.operator = (Color){212, 212, 212};
	E.theme.bracket = (Color){218, 112, 214};
	E.theme.line_number = (Color){133, 133, 133};
	E.theme.status_bg = (Color){50, 50, 50};
	E.theme.status_fg = (Color){212, 212, 212};
	E.theme.border = (Color){80, 80, 80};
	E.theme.visual_bg = (Color){38, 79, 120};
	E.theme.visual_fg = (Color){255, 255, 255};
	E.theme.constant = (Color){86, 156, 214};
	E.theme.builtin = (Color){220, 220, 170};
	E.theme.attribute = (Color){156, 220, 254};
	E.theme.char_literal = (Color){224, 108, 117};
	E.theme.escape = (Color){209, 154, 102};
	E.theme.label = (Color){198, 120, 221};
	E.theme.ai_bg = (Color){25, 25, 25};
	E.theme.ai_fg = (Color){200, 200, 200};
	E.theme.ai_prompt = (Color){100, 200, 100};
	E.theme.ai_response = (Color){150, 150, 200};
}

void editorLoadConfig() {
	FILE *fp = NULL;
	char *config_paths[] = {
		"./.hakorc",
		NULL,
		NULL
	};

	const char *home = getenv("HOME");
	if (home) {
		static char homerc[512];
		snprintf(homerc, sizeof(homerc), "%s/.hakorc", home);
		config_paths[1] = homerc;
	}

	for (int i = 0; config_paths[i] && !fp; i++) {
		fp = fopen(config_paths[i], "r");
		if (fp) E.config_path = strdup(config_paths[i]);
	}

	if (!fp) { hkLoadSession(); return; }

	char *line = NULL;
	size_t len = 0;
	while (getline(&line, &len, fp) != -1) {
		if (line[0] == '#' || line[0] == '\n') continue;

		char *eq = strchr(line, '=');
		if (!eq) continue;
		*eq = '\0';
		char *key = line;
		char *val = eq + 1;

		val[strcspn(val, "\r\n")] = 0;

		while (*key == ' ' || *key == '\t') key++;
		while (*val == ' ' || *val == '\t') val++;

		if (strcmp(key, "tab_stop") == 0) {
			E.tab_stop = atoi(val);
			if (E.tab_stop < 1) E.tab_stop = TAB_STOP;
		} else if (strcmp(key, "use_tabs") == 0) {
			E.use_tabs = atoi(val);
		} else if (strcmp(key, "word_wrap") == 0) {
			E.word_wrap = atoi(val);
		} else if (strcmp(key, "show_line_numbers") == 0) {
			E.show_line_numbers = atoi(val);
		} else if (strcmp(key, "max_undo_levels") == 0) {
			E.max_undo_levels = atoi(val);
		} else if (strcmp(key, "mouse_enabled") == 0) {
			E.mouse_enabled = atoi(val);
		} else if (strcmp(key, "auto_indent") == 0) {
			E.auto_indent = atoi(val);
		} else if (strcmp(key, "smart_indent") == 0) {
			E.smart_indent = atoi(val);
		} else if (strcmp(key, "scroll_speed") == 0) {
			E.scroll_speed = atoi(val);
			if (E.scroll_speed < 1) E.scroll_speed = 3;
		} else if (strcmp(key, "relative_line_numbers") == 0) {
			E.relative_line_numbers = atoi(val);
			if (E.relative_line_numbers) E.show_line_numbers = 2;
		} else if (strcmp(key, "explorer_enabled") == 0) {
			E.explorer_enabled = atoi(val);
		} else if (strcmp(key, "explorer_width") == 0) {
			E.explorer_width = atoi(val);
			if (E.explorer_width < 10) E.explorer_width = 25;
			if (E.explorer_width > 60) E.explorer_width = 60;
		} else if (strcmp(key, "explorer_show_hidden") == 0) {
			E.explorer_show_hidden = atoi(val);
		} else if (strcmp(key, "theme") == 0) {
			editorApplyThemeByName(val);
		} else if (strcmp(key, "ai_provider") == 0) {
			hkApplyProviderAlias(val);
		} else if (strcmp(key, "ai_api_key") == 0) {
			free(E.ai_api_key);
			if (strlen(val) > 0) E.ai_api_key = strdup(val);
		} else if (strcmp(key, "ai_endpoint") == 0) {
			free(E.ai_endpoint);
			if (strlen(val) > 0) E.ai_endpoint = strdup(val);
		} else if (strcmp(key, "ai_model") == 0) {
			free(E.ai_model);
			if (strlen(val) > 0) E.ai_model = strdup(val);
		} else if (strcmp(key, "ai_max_tokens") == 0) {
			E.ai_max_tokens = atoi(val);
			if (E.ai_max_tokens < 1) E.ai_max_tokens = 2048;
		} else if (strcmp(key, "ai_tools_enabled") == 0) {
			E.ai_tools_enabled = atoi(val) ? 1 : 0;
		} else if (strcmp(key, "ai_stream") == 0) {
			E.ai_stream = atoi(val) ? 1 : 0;
		} else if (strcmp(key, "ai_autowrite") == 0) {
			E.ai_autowrite = atoi(val) ? 1 : 0;
		} else if (strncmp(key, "theme_", 6) == 0) {
			int r, g, b;
			if (sscanf(val, "%d,%d,%d", &r, &g, &b) == 3) {
				Color color = {r, g, b};
				if (strcmp(key, "theme_bg") == 0) E.theme.bg = color;
				else if (strcmp(key, "theme_fg") == 0) E.theme.fg = color;
				else if (strcmp(key, "theme_comment") == 0) E.theme.comment = color;
				else if (strcmp(key, "theme_keyword1") == 0) E.theme.keyword1 = color;
				else if (strcmp(key, "theme_keyword2") == 0) E.theme.keyword2 = color;
				else if (strcmp(key, "theme_string") == 0) E.theme.string = color;
				else if (strcmp(key, "theme_number") == 0) E.theme.number = color;
				else if (strcmp(key, "theme_line_number") == 0) E.theme.line_number = color;
				else if (strcmp(key, "theme_status_bg") == 0) E.theme.status_bg = color;
				else if (strcmp(key, "theme_status_fg") == 0) E.theme.status_fg = color;
				else if (strcmp(key, "theme_visual_bg") == 0) E.theme.visual_bg = color;
				else if (strcmp(key, "theme_visual_fg") == 0) E.theme.visual_fg = color;
			}
		}
	}
	free(line);
	fclose(fp);
	hkLoadSession();
}

void initEditor() {
	E.screenrows = 0;
	E.screencols = 0;
	E.mode = MODE_NORMAL;
	E.statusmsg[0] = '\0';
	E.statusmsg_time = 0;
	E.tab_stop = TAB_STOP;
	E.use_tabs = 1;
	E.word_wrap = 1;
	E.word_wrap_column = 0;
	E.max_undo_levels = 100;
	E.show_line_numbers = 1;
	E.line_number_width = 0;
	editorInitPasteBuffer();
	E.in_window_command = 0;
	E.force_window_command = 0;
	E.splash_active = 1;
	E.splash_dismissed = 0;
	E.full_redraw_pending = 1;
	E.grid_front = NULL;
	E.grid_back = NULL;
	E.grid_w = 0;
	E.grid_h = 0;
	E.explorer_enabled = 1;
	E.explorer_width = 25;
	E.explorer_show_hidden = 0;
	E.is_pasting = 0;
	E.paste_indent_level = 0;
	E.root_pane = NULL;
	E.active_pane = NULL;
	E.left_panel = NULL;
	E.right_panel = NULL;
	E.left_panel_width = 0;
	E.right_panel_width = 0;
	E.num_panes = 0;
	E.mouse_enabled = 1;
	E.mouse_x = 0;
	E.mouse_y = 0;
	E.mouse_button = 0;
	E.plugin_count = 0;
	E.plugin_dir = NULL;
	E.auto_indent = 1;
	E.smart_indent = 1;

	E.explorer_kanji = strdup("紙");
	E.explorer_name = strdup("Kami");
	E.ai_kanji = strdup("零");
	E.ai_name = strdup("Rei");

	E.indent_guides = 0;
	E.scroll_speed = 3;
	E.config_path = NULL;
	E.theme_preset = THEME_DARK;

	E.search.query = NULL;
	E.search.last_match_row = -1;
	E.search.last_match_col = -1;
	E.search.direction = 1;
	E.search.wrap_search = 1;

	E.hk.pending_count = 0;
	E.hk.pending_reg = 0;
	E.hk.pending_op = 0;
	E.hk.op_start_x = E.hk.op_start_y = 0;
	memset(E.hk.registers, 0, sizeof(E.hk.registers));
	memset(E.hk.marks_x, 0, sizeof(E.hk.marks_x));
	memset(E.hk.marks_y, 0, sizeof(E.hk.marks_y));
	E.hk.marks_set = 0;
	memset(E.hk.jumps, 0, sizeof(E.hk.jumps));
	E.hk.jump_count = 0;
	E.hk.jump_pos = 0;
	E.hk.last_op = 0;
	E.hk.last_count = 0;
	E.hk.last_reg = 0;
	E.hk.last_motion = 0;
	E.hk.last_motion_arg = 0;
	E.hk.last_insert = NULL;
	E.hk.last_insert_len = 0;
	E.hk.recording_insert = 0;

	initTheme();
	editorInitAI();
	editorLoadConfig();

	editorUpdateWindowSize();

	editorPane *pane = editorCreatePane(PANE_EDITOR, 0, 0, E.screencols, E.screenrows);
	if (pane) {
		E.root_pane = pane;
		E.active_pane = pane;
		E.num_panes = 1;
		pane->is_focused = 1;
	}
}

void editorUpdateWindowSize() {
	int rows, cols;
	if (getWindowSize(&rows, &cols) == -1) die("getWindowSize");
	if (rows < 2) rows = 24;
	if (cols < 1) cols = 80;
	if (rows > 4096) rows = 4096;
	if (cols > 4096) cols = 4096;
	int new_rows = rows - 2;
	int new_cols = cols;
	if (new_rows != E.screenrows || new_cols != E.screencols) {
		E.screenrows = new_rows;
		E.screencols = new_cols;
		if (E.root_pane) editorResizePanes();
		E.full_redraw_pending = 1;
	}
	if (E.screencols > 0 && rows > 0) {
		gridResize(E.screencols, rows);
	}
}

void editorCleanup() {
	if (E.root_pane) {
		editorFreePane(E.root_pane);
	}
	if (E.left_panel) {
		editorFreePane(E.left_panel);
	}
	if (E.right_panel) {
		editorFreePane(E.right_panel);
	}

	editorFreePasteBuffer();
	hkFreeRegisters();
	free(E.search.query);
	free(E.plugin_dir);
	free(E.config_path);

	free(E.explorer_kanji);
	free(E.explorer_name);
	free(E.ai_kanji);
	free(E.ai_name);
	free(E.session_id);

	editorCleanupAI();
	gridFree();
}

/*** main ***/
int main(int argc, char *argv[]) {
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

	initEditor();
	detectTerminalType();
	enableRawMode();

#ifndef _WIN32
	struct sigaction sa;
	sa.sa_handler = on_sigwinch;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;
	if (sigaction(SIGWINCH, &sa, NULL) == -1) {
		die("sigaction");
	}
#endif

	if (argc >= 2) {
		E.splash_active = 0;
		editorOpen(argv[1]);
	}

	if (E.session_resumed && E.session_id) {
		editorSetStatusMessage("HAKO v%s | resumed session %s (%d turns) | /sessions to switch",
			HAKO_VERSION, E.session_id, E.session_turn_count);
	} else {
		editorSetStatusMessage("HAKO v%s | :help for commands", HAKO_VERSION);
	}
	while (1) {
		editorRefreshScreen();
		editorProcessKeyPress();
	}
	return 0;
}
