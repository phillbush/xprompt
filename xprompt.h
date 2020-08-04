#define DOUBLECLICK  250    /* time in miliseconds of a double click */
#define TEXTPART     7      /* completion word can be 1/7 of xprompt width */
#define MINTEXTWIDTH 200    /* minimum width of the completion word */
#define NLETTERS     'z' - 'a' + 1

/* macros */
#define LEN(x) (sizeof (x) / sizeof (x[0]))
#define MAX(x,y) ((x)>(y)?(x):(y))
#define MIN(x,y) ((x)<(y)?(x):(y))
#define BETWEEN(x, a, b)    ((a) <= (x) && (x) <= (b))
#define ISSOUTH(x) ((x) == SouthGravity || (x) == SouthWestGravity || (x) == SouthEastGravity)
#define ISMOTION(x) ((x) == CTRLBOL || (x) == CTRLEOL || (x) == CTRLLEFT \
                    || (x) == CTRLRIGHT || (x) == CTRLWLEFT || (x) == CTRLWRIGHT)
#define ISSELECTION(x) ((x) == CTRLSELBOL || (x) == CTRLSELEOL || (x) == CTRLSELLEFT \
                       || (x) == CTRLSELRIGHT || (x) == CTRLSELWLEFT || (x) == CTRLSELWRIGHT)
#define ISEDITING(x) ((x) == CTRLDELBOL || (x) == CTRLDELEOL || (x) == CTRLDELLEFT \
                     || (x) == CTRLDELRIGHT || (x) == CTRLDELWORD || (x) == INSERT)

enum {ColorFG, ColorBG, ColorCM, ColorLast};
enum {LowerCase, UpperCase, CaseLast};
enum Press_ret {DrawPrompt, DrawInput, Esc, Enter, Noop};

/* Input operations */
enum Ctrl {
	CTRLPASTE = 0,  /* Paste from clipboard */
	CTRLCOPY,       /* Copy into clipboard */
	CTRLENTER,      /* Choose item */
	CTRLPREV,       /* Select previous item */
	CTRLNEXT,       /* Select next item */
	CTRLPGUP,       /* Select item one screen above */
	CTRLPGDOWN,     /* Select item one screen below */
	CTRLUP,         /* Select previous item in the history */
	CTRLDOWN,       /* Select next item in the history */
	CTRLBOL,        /* Move cursor to beginning of line */
	CTRLEOL,        /* Move cursor to end of line */
	CTRLLEFT,       /* Move cursor one character to the left */
	CTRLRIGHT,      /* Move cursor one character to the right */
	CTRLWLEFT,      /* Move cursor one word to the left */
	CTRLWRIGHT,     /* Move cursor one word to the right */
	CTRLDELBOL,     /* Delete from cursor to beginning of line */
	CTRLDELEOL,     /* Delete from cursor to end of line */
	CTRLDELLEFT,    /* Delete character to left of cursor */
	CTRLDELRIGHT,   /* Delete character to right of cursor */
	CTRLDELWORD,    /* Delete from cursor to beginning of word */
	CTRLSELBOL,     /* Select from cursor to beginning of line */
	CTRLSELEOL,     /* Select from cursor to end of line */
	CTRLSELLEFT,    /* Select from cursor to one character to the left */
	CTRLSELRIGHT,   /* Select from cursor to one character to the right */
	CTRLSELWLEFT,   /* Select from cursor to one word to the left */
	CTRLSELWRIGHT,  /* Select from cursor to one word to the right */
	CTRLCANCEL,     /* Cancel */
	CTRLNOTHING,    /* Control does nothing */
	INSERT          /* Insert character as is */
};

/* configuration structure */
struct Config {
	const char *worddelimiters;
	const char *xpromptctrl;
	const char *font;

	const char *background_color;
	const char *foreground_color;
	const char *description_color;
	const char *hoverbackground_color;
	const char *hoverforeground_color;
	const char *hoverdescription_color;
	const char *selbackground_color;
	const char *selforeground_color;
	const char *seldescription_color;
	const char *separator_color;
	const char *border_color;

	const char *geometryspec;
	const char *gravityspec;

	unsigned number_items;

	int border_pixels;
	int separator_pixels;

	size_t histsize;
};

/* draw context structure */
struct DC {
	XftColor hover[ColorLast];      /* bg and fg of hovered item */
	XftColor normal[ColorLast];     /* bg and fg of normal text */
	XftColor selected[ColorLast];   /* bg and fg of the selected item */
	XftColor border;                /* color of the border */
	XftColor separator;             /* color of the separator */

	GC gc;                          /* graphics context */

	FcPattern *pattern;
	XftFont **fonts;
	size_t nfonts;

	int pad;                        /* padding around text */
};

/* completion items */
struct Item {
	char *text;                 /* content of the completion item */
	char *description;          /* description of the completion item */
	unsigned level;             /* word level the item completes */
	struct Item *prev, *next;   /* previous and next items */
	struct Item *parent;        /* parent item */
	struct Item *child;         /* point to the list of child items */
};

/* monitor geometry structure */
struct Monitor {
	int num;                /* monitor number */
	int x, y, w, h;         /* monitor geometry */
};

/* prompt */
struct Prompt {
	const char *promptstr;  /* string appearing before the input field */
	unsigned promptw;       /* prompt width */

	char *text;             /* input field */
	size_t textsize;        /* maximum size of the text in the input field */
	size_t cursor;          /* position of the cursor in the input field */
	size_t select;          /* position of the selection in the input field*/

	struct Item **itemarray; /* array containing nitems matching text */
	size_t hoveritem;       /* item hovered */
	size_t selitem;         /* item selected */
	size_t nitems;          /* number of items in itemarray */
	size_t maxitems;        /* maximum number of items in itemarray */

	int gravity;            /* where in the screen to map xprompt */
	int x, y;               /* position of xprompt */
	unsigned descx;         /* x position of the description field */
	unsigned w, h;          /* width and height of xprompt */
	unsigned border;        /* border width */
	unsigned separator;     /* separator width */

	unsigned monitor;       /* monitor to draw the prompt in */

	Drawable pixmap;        /* where to draw shapes on */
	XftDraw *draw;          /* where to draw text on */
	Window win;             /* xprompt window */
};

/* history */
struct History {
	char **entries;     /* array of history entries */
	size_t index;       /* index to the selected entry in the array */
	size_t size;        /* how many entries there are in the array */
};
