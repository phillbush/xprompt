/* See README file for copyright and license details. */

#include <err.h>
#include <ctype.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <glob.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/Xresource.h>
#include <X11/XKBlib.h>
#include <X11/Xft/Xft.h>

#define PROGNAME "xprompt"

#define TEXTPART     7      /* completion word can be 1/7 of xprompt width */
#define MINTEXTWIDTH 200    /* minimum width of the completion word */
#define NLETTERS     'z' - 'a' + 1

/* macros */
#define LEN(x) (sizeof (x) / sizeof (x[0]))
#define MAX(x,y) ((x)>(y)?(x):(y))
#define MIN(x,y) ((x)<(y)?(x):(y))
#define ISSOUTH(x) ((x) == SouthGravity || (x) == SouthWestGravity || (x) == SouthEastGravity)

enum {ColorFG, ColorBG, ColorLast};
enum {LowerCase, UpperCase, CaseLast};
enum Press_ret {Draw, Esc, Enter, Noop};

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

/* draw context structure */
struct DC {
	XftColor normal[ColorLast];     /* bg and fg of normal text */
	XftColor selected[ColorLast];   /* bg and fg of the selected item */
	XftColor border;                /* color of the border */
	XftColor separator;             /* color of the separator */

	GC gc;                          /* graphics context */
	XftFont *font;                  /* font */
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

/* prompt */
struct Prompt {
	const char *promptstr;  /* string appearing before the input field */
	unsigned promptlen;     /* length of the prompt string */
	unsigned promptw;       /* prompt width */

	char *text;             /* input field */
	size_t textsize;        /* maximum size of the text in the input field */
	size_t cursor;          /* position of the cursor in the input field */
	size_t select;          /* position of the selection in the input field*/

	struct Item **itemarray; /* array containing nitems matching text */
	size_t curritem;        /* current item selected */
	size_t nitems;          /* number of items in itemarray */
	size_t maxitems;        /* maximum number of items in itemarray */

	int gravity;            /* where in the screen to map xprompt */
	int x, y;               /* position of xprompt */
	unsigned descx;         /* x position of the description field */
	unsigned w, h;          /* width and height of xprompt */
	unsigned border;        /* border width */
	unsigned separator;     /* separator width */

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

/* function declarations */
static unsigned textwidth(const char *str, size_t len);
static void getresources(void);
static void getcolor(const char *s, XftColor *color);
static void setupdc(void);
static void setupctrl(void);
static void setuppromptinput(struct Prompt *prompt);
static void setuppromptarray(struct Prompt *prompt);
static void setuppromptgeom(struct Prompt *prompt, Window parentwin);
static void setuppromptwin(struct Prompt *prompt, Window parentwin);
static struct Item *allocitem(unsigned level, const char *text, const char *description);
static struct Item *builditems(unsigned level, const char *text, const char *description);
static struct Item *parsestdin(FILE *fp);
static void loadhist(FILE *fp, struct History *hist);
static void grabkeyboard(void);
static void grabfocus(Window win);
static size_t resizeprompt(struct Prompt *prompt, size_t nitems_old);
static void drawitem(struct Prompt *prompt, size_t n);
static void drawprompt(struct Prompt *prompt);
static size_t nextrune(struct Prompt *prompt, int inc);
static void movewordedge(struct Prompt *prompt, int dir);
static void delselection(struct Prompt *prompt);
static void insert(struct Prompt *prompt, const char *str, ssize_t n);
static void delword(struct Prompt *prompt);
static void paste(struct Prompt *prompt);
static char *navhist(struct History *hist, int direction);
static struct Item *getcomplist(struct Prompt *prompt, struct Item *rootitem);
static struct Item *getfilelist(struct Prompt *prompt);
static size_t fillitemarray(struct Prompt *prompt, struct Item *complist, int direction);
static enum Ctrl getoperation(KeySym ksym, unsigned state);
static enum Press_ret keypress(struct Prompt *prompt, struct Item *rootitem, struct History *hist, XKeyEvent *ev);
static size_t getcurpos(struct Prompt *prompt, int x);
static enum Press_ret buttonpress(struct Prompt *prompt, XButtonEvent *ev);
static enum Press_ret buttonmotion(struct Prompt *prompt, XMotionEvent *ev);
static int run(struct Prompt *prompt, struct Item *rootitem, struct History *hist);
static void savehist(struct Prompt *prompt, struct History *hist, FILE *fp);
static void freeitem(struct Item *item);
static void freehist(struct History *hist);
static void freeprompt(struct Prompt *prompt);
static void cleanup(void);
static void usage(void);

/* X stuff */
static Display *dpy;
static int screen;
static Visual *visual;
static Window rootwin;
static Colormap colormap;
static XIC xic;
static struct DC dc;
static Atom utf8;
static Atom clip;

/* flags */
static int wflag = 0;   /* whether to enable embeded prompt */
static int fflag = 0;   /* whether to enable filename completion */
static int hflag = 0;   /* whether to enable history */
static int pflag = 0;   /* whether to enable password mode */

/* ctrl operations */
static enum Ctrl ctrl[CaseLast][NLETTERS];

/* comparison function */
static int (*fstrncmp)(const char *, const char *, size_t) = strncmp;

#include "config.h"

/* xprompt */
int
main(int argc, char *argv[])
{
	struct History hist = {.entries = NULL, .index = 0, .size = 0};
	struct Prompt prompt;
	struct Item *rootitem;
	Window parentwin = 0;
	FILE *histfp;
	char *histfile = NULL;
	char *str;
	int ch;
	size_t n;

	/* get environment */
	if ((str = getenv("XPROMPTHISTFILE")) != NULL)
		histfile = str;
	if ((str = getenv("XPROMPTHISTSIZE")) != NULL)
		if ((n = strtol(str, NULL, 10)) > 0)
			histsize = n;
	if ((str = getenv("XPROMPTCTRL")) != NULL)
		xpromptctrl = str;
	if ((str = getenv("WORDDELIMITERS")) != NULL)
		worddelimiters = str;

	/* get options */
	while ((ch = getopt(argc, argv, "fGgh:ipw:")) != -1) {
		switch (ch) {
		case 'f':
			fflag = 1;
			break;
		case 'G':
			gravityspec = optarg;
			break;
		case 'g':
			geometryspec = optarg;
			break;
		case 'h':
			histfile = optarg;
			break;
		case 'i':
			fstrncmp = strncasecmp;
			break;
		case 'p':
			pflag = 1;
			break;
		case 'w':
			wflag = 1;
			parentwin = strtol(optarg, &str, 0);
			break;
		default:
			usage();
			break;
		}
	}
	argc -= optind;
	argv += optind;

	if (argc > 1)
		usage();

	/* open connection to server and set X variables */
	if (!setlocale(LC_CTYPE, "") || !XSupportsLocale())
		warnx("warning: no locale support");
	if ((dpy = XOpenDisplay(NULL)) == NULL)
		errx(1, "cannot open display");
	screen = DefaultScreen(dpy);
	visual = DefaultVisual(dpy, screen);
	rootwin = RootWindow(dpy, screen);
	colormap = DefaultColormap(dpy, screen);

	/* setup */
	getresources();
	setupctrl();
	setupdc();
	utf8 = XInternAtom(dpy, "UTF8_STRING", False);
	clip = XInternAtom(dpy, "CLIPBOARD", False);

	/* initiate prompt */
	if (!parentwin)
		parentwin = rootwin;
	if (argc == 0) {
		prompt.promptstr = "\0";
		prompt.promptlen = 0;
	} else {
		prompt.promptstr = *argv;
		prompt.promptlen = strlen(*argv);
	}
	setuppromptinput(&prompt);
	setuppromptarray(&prompt);
	setuppromptgeom(&prompt, parentwin);
	setuppromptwin(&prompt, parentwin);

	/* initiate item list */
	rootitem = parsestdin(stdin);

	/* initiate history */
	if (histfile != NULL && *histfile != '\0') {
		if ((histfp = fopen(histfile, "a+")) == NULL)
			warn("%s", histfile);
		else {
			loadhist(histfp, &hist);
			if (!hflag)
				fclose(histfp);
		}
	}

	/* grab input */
	if (!wflag)
		grabkeyboard();

	/* run event loop; and, if run return nonzero, save the history */
	if (run(&prompt, rootitem, &hist))
		savehist(&prompt, &hist, histfp);

	/* freeing stuff */
	if (hflag)
		fclose(histfp);
	freeitem(rootitem);
	freehist(&hist);
	freeprompt(&prompt);
	cleanup();

	return EXIT_SUCCESS;
}

/* get width of a text drawn using dc */
static unsigned
textwidth(const char *str, size_t len)
{
	XGlyphInfo ext;

	XftTextExtentsUtf8(dpy, dc.font, (XftChar8 *)str, len, &ext);

	return ext.xOff;
}

/* read xrdb for configuration options */
static void
getresources(void)
{
	char *xrm;
	long n;
	char *type;
	XrmDatabase xdb;
	XrmValue xval;

	XrmInitialize();
	if ((xrm = XResourceManagerString(dpy)) == NULL)
		return;

	xdb = XrmGetStringDatabase(xrm);

	if (XrmGetResource(xdb, "xprompt.items", "*", &type, &xval) == True)
		if ((n = strtol(xval.addr, NULL, 10)) > 0)
			number_items = n;
	if (XrmGetResource(xdb, "xprompt.borderWidth", "*", &type, &xval) == True)
		if ((n = strtol(xval.addr, NULL, 10)) > 0)
			border_pixels = n;
	if (XrmGetResource(xdb, "xprompt.separatorWidth", "*", &type, &xval) == True)
		if ((n = strtol(xval.addr, NULL, 10)) > 0)
			separator_pixels = n;
	if (XrmGetResource(xdb, "xprompt.background", "*", &type, &xval) == True)
		background_color = strdup(xval.addr);
	if (XrmGetResource(xdb, "xprompt.foreground", "*", &type, &xval) == True)
		foreground_color = strdup(xval.addr);
	if (XrmGetResource(xdb, "xprompt.selbackground", "*", &type, &xval) == True)
		selbackground_color = strdup(xval.addr);
	if (XrmGetResource(xdb, "xprompt.selforeground", "*", &type, &xval) == True)
		selforeground_color = strdup(xval.addr);
	if (XrmGetResource(xdb, "xprompt.separator", "*", &type, &xval) == True)
		separator_color = strdup(xval.addr);
	if (XrmGetResource(xdb, "xprompt.border", "*", &type, &xval) == True)
		border_color = strdup(xval.addr);
	if (XrmGetResource(xdb, "xprompt.font", "*", &type, &xval) == True)
		font = strdup(xval.addr);
	if (XrmGetResource(xdb, "xprompt.geometry", "*", &type, &xval) == True)
		geometryspec = strdup(xval.addr);
	if (XrmGetResource(xdb, "xprompt.gravity", "*", &type, &xval) == True)
		gravityspec = strdup(xval.addr);

	XrmDestroyDatabase(xdb);
}

/* get color from color string */
static void
getcolor(const char *s, XftColor *color)
{
	if(!XftColorAllocName(dpy, visual, colormap, s, color))
		errx(1, "cannot allocate color: %s", s);
}

/* init draw context */
static void
setupdc(void)
{
	/* get color pixels */
	getcolor(background_color,    &dc.normal[ColorBG]);
	getcolor(foreground_color,    &dc.normal[ColorFG]);
	getcolor(selbackground_color, &dc.selected[ColorBG]);
	getcolor(selforeground_color, &dc.selected[ColorFG]);
	getcolor(separator_color,     &dc.separator);
	getcolor(border_color,        &dc.border);

	/* try to get font */
	if ((dc.font = XftFontOpenName(dpy, screen, font)) == NULL)
		errx(1, "cannot load font");

	/* create common GC */
	dc.gc = XCreateGC(dpy, rootwin, 0, NULL);
}

/* set control keybindings */
static void
setupctrl(void)
{
	size_t i, j;

	for (i = 0; i < CaseLast; i++) {
		for (j = 0; j < (NLETTERS); j++) {
			ctrl[i][j] = CTRLNOTHING;
		}
	}

	for (i = 0; i < CTRLNOTHING && xpromptctrl[i] != '\0'; i++) {
		if (!isalpha(xpromptctrl[i]))
			continue;
		if (isupper(xpromptctrl[i]))
			ctrl[UpperCase][xpromptctrl[i] - 'A'] = i;
		if (islower(xpromptctrl[i]))
			ctrl[LowerCase][xpromptctrl[i] - 'a'] = i;
	}
}

/* allocate memory for the text input field */
static void
setuppromptinput(struct Prompt *prompt)
{
	if ((prompt->text = malloc(BUFSIZ)) == NULL)
		err(EXIT_FAILURE, "malloc");
	prompt->textsize = BUFSIZ;
	prompt->text[0] = '\0';
	prompt->cursor = 0;
	prompt->select = 0;
}

/* allocate memory for the item list displayed when completion is active */
static void
setuppromptarray(struct Prompt *prompt)
{
	prompt->curritem = 0;
	prompt->nitems = 0;
	prompt->maxitems = number_items;
	if ((prompt->itemarray = calloc(sizeof *prompt->itemarray, prompt->maxitems)) == NULL)
		err(EXIT_FAILURE, "malloc");
}

/* calculate prompt geometry */
static void
setuppromptgeom(struct Prompt *prompt, Window parentwin)
{
	XWindowAttributes wa;   /* window attributes of the parent window */

	/* try to get attributes of parent window */
	if (!XGetWindowAttributes(dpy, parentwin, &wa))
		errx(1, "could not get window attributes of 0x%lx", parentwin);
	
	/* get width of border and separator */
	prompt->border = border_pixels;
	prompt->separator = separator_pixels;

	/* get prompt gravity */
	if (gravityspec == NULL || strcmp(gravityspec, "N") == 0)
		prompt->gravity = NorthGravity;
	else if (strcmp(gravityspec, "NW") == 0)
		prompt->gravity = NorthWestGravity;
	else if (strcmp(gravityspec, "NE") == 0)
		prompt->gravity = NorthEastGravity;
	else if (strcmp(gravityspec, "W") == 0)
		prompt->gravity = WestGravity;
	else if (strcmp(gravityspec, "C") == 0)
		prompt->gravity = CenterGravity;
	else if (strcmp(gravityspec, "E") == 0)
		prompt->gravity = EastGravity;
	else if (strcmp(gravityspec, "SW") == 0)
		prompt->gravity = SouthWestGravity;
	else if (strcmp(gravityspec, "S") == 0)
		prompt->gravity = SouthGravity;
	else if (strcmp(gravityspec, "SE") == 0)
		prompt->gravity = SouthEastGravity;
	else
		errx(EXIT_FAILURE, "Unknown gravity %s", gravityspec);

	/* get prompt geometry */
	XParseGeometry(geometryspec, &prompt->x, &prompt->y, &prompt->w, &prompt->h);

	/* update prompt size, based on parent window's size */
	if (prompt->w == 0)
		prompt->w = wa.width - prompt->border * 2;
	prompt->w = MIN(prompt->w, (unsigned) wa.width);
	prompt->h = MIN(prompt->h, (unsigned) wa.height);

	/* update prompt position, based on prompt's gravity */
	switch (prompt->gravity) {
	case NorthWestGravity:
		break;
	case NorthGravity:
		prompt->x += (wa.width - prompt->w)/2 - prompt->border;
		break;
	case NorthEastGravity:
		prompt->x += wa.width - prompt->w - prompt->border * 2;
		break;
	case WestGravity:
		prompt->y += (wa.height - prompt->h)/2 - prompt->border;
		break;
	case CenterGravity:
		prompt->x += (wa.width - prompt->w)/2 - prompt->border;
		prompt->y += (wa.height - prompt->h)/2 - prompt->border;
		break;
	case EastGravity:
		prompt->x += wa.width - prompt->w - prompt->border * 2;
		prompt->y += (wa.height - prompt->h)/2 - prompt->border;
		break;
	case SouthWestGravity:
		prompt->y += wa.height - prompt->h - prompt->border * 2;
		break;
	case SouthGravity:
		prompt->x += (wa.width - prompt->w)/2 - prompt->border;
		prompt->y += wa.height - prompt->h - prompt->border * 2;
		break;
	case SouthEastGravity:
		prompt->x += wa.width - prompt->w - prompt->border * 2;
		prompt->y += wa.height - prompt->h - prompt->border * 2;
		break;
	}

	/* calculate prompt string width */
	if (prompt->promptlen)
		prompt->promptw = textwidth(prompt->promptstr, prompt->promptlen) + dc.font->height * 2;
	else
		prompt->promptw = dc.font->height;

	/* description x position */
	prompt->descx = prompt->w / TEXTPART;
	prompt->descx = MAX(prompt->descx, MINTEXTWIDTH);
}

/* set up prompt window */
static void
setuppromptwin(struct Prompt *prompt, Window parentwin)
{
	XSetWindowAttributes swa;
	XSizeHints sizeh;
	XClassHint classh = {PROGNAME, PROGNAME};
	XIM xim;
	unsigned h;

	/* create prompt window */
	swa.override_redirect = True;
	swa.background_pixel = dc.normal[ColorBG].pixel;
	swa.border_pixel = dc.border.pixel;
	swa.event_mask = ExposureMask | KeyPressMask | VisibilityChangeMask
	               | ButtonPressMask | Button1MotionMask;
	prompt->win = XCreateWindow(dpy, parentwin,
	                            prompt->x, prompt->y, prompt->w, prompt->h, prompt->border,
	                            CopyFromParent, CopyFromParent, CopyFromParent,
	                            CWOverrideRedirect | CWBackPixel | CWBorderPixel | CWEventMask,
	                            &swa);
	XSetClassHint(dpy, prompt->win, &classh);

	/* set window normal hints */
	sizeh.flags = PMaxSize | PMinSize;
	sizeh.min_width = sizeh.max_width = prompt->w;
	sizeh.min_height = sizeh.max_height = prompt->h;
	XSetWMNormalHints(dpy, prompt->win, &sizeh);

	/* create drawables */
	h = prompt->separator + prompt->h * (prompt->maxitems + 1);
	prompt->pixmap = XCreatePixmap(dpy, prompt->win, prompt->w, h,
	                               DefaultDepth(dpy, screen));
	prompt->draw = XftDrawCreate(dpy, prompt->pixmap, visual, colormap);

	/* open input methods */
	if ((xim = XOpenIM(dpy, NULL, NULL, NULL)) == NULL)
		errx(EXIT_FAILURE, "XOpenIM: could not open input device");
	xic = XCreateIC(xim, XNInputStyle, XIMPreeditNothing | XIMStatusNothing,
	                XNClientWindow, prompt->win, XNFocusWindow, prompt->win, NULL);
	if (xic == NULL)
		errx(EXIT_FAILURE, "XCreateIC: could not obtain input method");

	/* map window */
	XMapRaised(dpy, prompt->win);

	/* selecect focus event mask for the parent window */
	if (wflag) {
		Window r, p;    /* unused variables */
		Window *children;
		unsigned i, nchildren;

		XSelectInput(dpy, parentwin, FocusChangeMask);
		if (XQueryTree(dpy, parentwin, &r, &p, &children, &nchildren)) {
			for (i = 0; i < nchildren && children[i] != prompt->win; i++)
				XSelectInput(dpy, children[i], FocusChangeMask);
			XFree(children);
		}
	}
}

/* allocate a completion item */
static struct Item *
allocitem(unsigned level, const char *text, const char *description)
{
	struct Item *item;

	if ((item = malloc(sizeof *item)) == NULL)
		err(EXIT_FAILURE, "malloc");
	if ((item->text = strdup(text)) == NULL)
		err(EXIT_FAILURE, "strdup");
	if (description != NULL) {
		if ((item->description = strdup(description)) == NULL)
			err(EXIT_FAILURE, "strdup");
	} else {
		item->description = NULL;
	}
	item->level = level;
	item->prev = item->next = NULL;
	item->parent = NULL;
	item->child = NULL;

	return item;
}

/* build the item tree */
static struct Item *
builditems(unsigned level, const char *text, const char *description)
{
	static struct Item *rootitem = NULL;
	static struct Item *previtem = NULL;
	struct Item *curritem;
	struct Item *item;
	unsigned i;

	curritem = allocitem(level, text, description);

	if (previtem == NULL) {               /* there is no item yet */
		curritem->parent = NULL;
		rootitem = curritem;
	} else if (level < previtem->level) { /* item is continuation of a parent item */
		/* go up the item tree until find the item the current one continues */
		for (item = previtem, i = level;
		     item != NULL && i != previtem->level;
		     item = item->parent, i++)
			;
		if (item == NULL)
			errx(EXIT_FAILURE, "reached NULL item");

		curritem->parent = item->parent;
		item->next = curritem;
		curritem->prev = item;
	} else if (level == previtem->level) { /* item is continues current item */
		curritem->parent = previtem->parent;
		previtem->next = curritem;
		curritem->prev = previtem;
	} else if (level > previtem->level) { /* item begins a new list */
		previtem->child = curritem;
		curritem->parent = previtem;
	}

	previtem = curritem;

	return rootitem;
}

/* create completion items from the stdin */
static struct Item *
parsestdin(FILE *fp)
{
	struct Item *rootitem;
	char *s, buf[BUFSIZ];
	char *text, *description;
	unsigned level = 0;

	rootitem = NULL;

	while (fgets(buf, BUFSIZ, fp) != NULL) {
		/* get the indentation level */
		level = strspn(buf, "\t");

		/* get the item text */
		s = buf + level;
		text = strtok(s, "\t\n");
		description = strtok(NULL, "\t\n");

		rootitem = builditems(level, text, description);
	}

	return rootitem;
}

/* parse the history file */
static void
loadhist(FILE *fp, struct History *hist)
{
	char buf[BUFSIZ];
	char *s;
	size_t len;

	if ((hist->entries = calloc(histsize, sizeof *hist)) == NULL)
		err(EXIT_FAILURE, "calloc");

	hist->size = 0;

	rewind(fp);
	while (hist->size < histsize && fgets(buf, sizeof buf, fp) != NULL) {
		len = strlen(buf);
		if (len && buf[--len] == '\n')
			buf[len] = '\0';
		if ((s = strdup(buf)) == NULL)
			err(EXIT_FAILURE, "strdup");
		hist->entries[hist->size++] = s;
	}

	if (hist->size)
		hist->index = hist->size;

	hflag = (ferror(fp)) ? 0 : 1;
}

/* try to grab keyboard, we may have to wait for another process to ungrab */
static void
grabkeyboard(void)
{
	struct timespec ts = { .tv_sec = 0, .tv_nsec = 1000000  };
	int i;

	for (i = 0; i < 1000; i++) {
		if (XGrabKeyboard(dpy, rootwin, True, GrabModeAsync,
		                  GrabModeAsync, CurrentTime) == GrabSuccess)
			return;
		nanosleep(&ts, NULL);
	}
	errx(1, "cannot grab keyboard");
}

/* try to grab focus, we may have to wait for another process to ungrab */
static void
grabfocus(Window win)
{
	struct timespec ts = { .tv_sec = 0, .tv_nsec = 10000000  };
	Window focuswin;
	int i, revertwin;

	for (i = 0; i < 100; ++i) {
		XGetInputFocus(dpy, &focuswin, &revertwin);
		if (focuswin == win)
			return;
		XSetInputFocus(dpy, win, RevertToParent, CurrentTime);
		nanosleep(&ts, NULL);
	}
	errx(1, "cannot grab focus");
}

/* resize xprompt window and return how many items it has on the dropdown list */
static size_t
resizeprompt(struct Prompt *prompt, size_t nitems_old)
{
	size_t nitems_new;
	unsigned h;
	int y;

	if (prompt->nitems && nitems_old != prompt->nitems) {
		h = prompt->h * (prompt->nitems + 1) + prompt->separator;
		y = prompt->y - h + prompt->h;

		nitems_new = prompt->nitems;
	} else if (nitems_old && !prompt->nitems) {
		h = prompt->h;
		y = prompt->y;

		nitems_new = 0;
	} else {
		nitems_new = nitems_old;
	}

	/*
	 * if the old and new number of items are the same,
	 * there's no need to resize the window
	 */
	if (nitems_old != nitems_new) {
		/* if gravity is south, resize and move, otherwise just resize */
		if (ISSOUTH(prompt->gravity))
			XMoveResizeWindow(dpy, prompt->win, prompt->x, y, prompt->w, h);
		else
			XResizeWindow(dpy, prompt->win, prompt->w, h);
	}

	return nitems_new;
}

/* draw nth item in the item array */
static void
drawitem(struct Prompt *prompt, size_t n)
{
	XftColor *color;
	int y;

	color = (n == prompt->curritem) ? dc.selected : dc.normal;
	y = (n + 1) * prompt->h + prompt->separator;

	/* if nth item is the selected one, draw a rectangle behind it */
	if (n == prompt->curritem) {
		XSetForeground(dpy, dc.gc, color[ColorBG].pixel);
		XFillRectangle(dpy, prompt->pixmap, dc.gc, 0, y, prompt->w, prompt->h);
	}

	/* draw item text */
	XftDrawStringUtf8(prompt->draw, &color[ColorFG], dc.font,
                      prompt->promptw, y + prompt->h/2 + dc.font->ascent/2 - 1,
                      prompt->itemarray[n]->text,
                      strlen(prompt->itemarray[n]->text));

	/* if item has a description, draw it */
	if (prompt->itemarray[n]->description != NULL) {
		XftDrawStringUtf8(prompt->draw, &color[ColorFG], dc.font,
                          prompt->descx, y + prompt->h/2 + dc.font->ascent/2 - 1,
                          prompt->itemarray[n]->description,
                          strlen(prompt->itemarray[n]->description));
    }
}

/* draw the prompt */
static void
drawprompt(struct Prompt *prompt)
{
	static size_t nitems = 0;       /* number of items in the dropdown list */
	unsigned curpos;                /* where to draw the cursor */
	unsigned h;
	int x, y;
	size_t i;

	x = dc.font->height;
	y = prompt->h/2 + dc.font->ascent/2 - 1;
	h = prompt->h;

	/* draw background */
	XSetForeground(dpy, dc.gc, dc.normal[ColorBG].pixel);
	XFillRectangle(dpy, prompt->pixmap, dc.gc, 0, 0,
	               prompt->w, prompt->h);

	/* draw the prompt string and update x to the end of it */
	if (prompt->promptlen != 0) {
		XftDrawStringUtf8(prompt->draw, &dc.normal[ColorFG], dc.font,
                      	  x, y, prompt->promptstr, prompt->promptlen);
		x = prompt->promptw;
	}

	/* draw input field text and set position of the cursor */
	if (!pflag) {
		XGlyphInfo ext;
		unsigned minpos, maxpos;
		int newx = x;
		char *cursortext;           /* text from the cursor til the end */

		ext.xOff = 0;

		minpos = MIN(prompt->cursor, prompt->select);
		maxpos = MAX(prompt->cursor, prompt->select);

		/* draw text before selection */
		XftTextExtentsUtf8(dpy, dc.font, (XftChar8 *)prompt->text, minpos, &ext);
		XftDrawStringUtf8(prompt->draw, &dc.normal[ColorFG], dc.font,
		                  newx, y, prompt->text, minpos);
		newx += ext.xOff;

		/* draw selected text */
		XftTextExtentsUtf8(dpy, dc.font, (XftChar8 *)prompt->text+minpos, maxpos-minpos, &ext);
		XSetForeground(dpy, dc.gc, dc.normal[ColorFG].pixel);
		XFillRectangle(dpy, prompt->pixmap, dc.gc, newx, 0, ext.xOff, prompt->h);
		XftDrawStringUtf8(prompt->draw, &dc.normal[ColorBG], dc.font,
		                  newx, y, prompt->text+minpos, maxpos - minpos);
		newx += ext.xOff;

		/* draw text after selection */
		XftDrawStringUtf8(prompt->draw, &dc.normal[ColorFG], dc.font,
		                  newx, y, prompt->text+maxpos, strlen(prompt->text) - maxpos);

		cursortext = prompt->text + prompt->cursor;
		curpos = x + textwidth(prompt->text, strlen(prompt->text)) - textwidth(cursortext, strlen(cursortext));
    } else {
		curpos = x;
    }

	/* draw cursor rectangle */
	y = prompt->h/2 - dc.font->height/2;
	XSetForeground(dpy, dc.gc, dc.normal[ColorFG].pixel);
	XFillRectangle(dpy, prompt->pixmap, dc.gc, curpos, y, 1, dc.font->height);

	/* resize window and get new value of number of items */
	nitems = resizeprompt(prompt, nitems);

	/* if there are no items to drawn, we are done */
	if (!nitems)
		goto done;

	/* background of items */
	y = prompt->h;
	h = prompt->h * prompt->nitems + prompt->separator;
	XSetForeground(dpy, dc.gc, dc.normal[ColorBG].pixel);
	XFillRectangle(dpy, prompt->pixmap, dc.gc, 0, y, prompt->w, h);

	/* draw separator line */
	y = prompt->h + prompt->separator/2;
	h = prompt->h * (prompt->nitems + 1) + prompt->separator;
	XSetForeground(dpy, dc.gc, dc.separator.pixel);
	XDrawLine(dpy, prompt->pixmap, dc.gc, 0, y, prompt->w, y);

	/* draw items */
	for (i = 0; i < prompt->nitems; i++)
		drawitem(prompt, i);

done:
	/* commit drawing */
	XCopyArea(dpy, prompt->pixmap, prompt->win, dc.gc, 0, 0,
	          prompt->w, h, 0, 0);
}

/* return location of next utf8 rune in the given direction (+1 or -1) */
static size_t
nextrune(struct Prompt *prompt, int inc)
{
	ssize_t n;

	for (n = prompt->cursor + inc;
	     n + inc >= 0 && (prompt->text[n] & 0xc0) == 0x80;
	     n += inc)
		;
	return n;
}

/* move cursor to start (dir = -1) or end (dir = +1) of the word */
static void
movewordedge(struct Prompt *prompt, int dir)
{
	if (dir < 0) {
		while (prompt->cursor > 0 && strchr(worddelimiters, prompt->text[nextrune(prompt, -1)]))
			prompt->cursor = nextrune(prompt, -1);
		while (prompt->cursor > 0 && !strchr(worddelimiters, prompt->text[nextrune(prompt, -1)]))
			prompt->cursor = nextrune(prompt, -1);
	} else {
		while (prompt->text[prompt->cursor] && strchr(worddelimiters, prompt->text[prompt->cursor]))
			prompt->cursor = nextrune(prompt, +1);
		while (prompt->text[prompt->cursor] && !strchr(worddelimiters, prompt->text[prompt->cursor]))
			prompt->cursor = nextrune(prompt, +1);
	}
}

/* delete selected text */
static void
delselection(struct Prompt *prompt)
{
	int minpos, maxpos;
	size_t len;

	if (prompt->select == prompt->cursor)
		return;

	minpos = MIN(prompt->cursor, prompt->select);
	maxpos = MAX(prompt->cursor, prompt->select);
	len = strlen(prompt->text + maxpos);

	memmove(prompt->text + minpos, prompt->text + maxpos, len + 1);

	prompt->cursor = prompt->select = minpos;
}

/* insert string on prompt->text and update prompt->cursor */
static void
insert(struct Prompt *prompt, const char *str, ssize_t n)
{
	if (strlen(prompt->text) + n > prompt->textsize - 1)
		return;

	/* move existing text out of the way, insert new text, and update cursor */
	memmove(&prompt->text[prompt->cursor + n], &prompt->text[prompt->cursor],
	        prompt->textsize - prompt->cursor - MAX(n, 0));
	if (n > 0)
		memcpy(&prompt->text[prompt->cursor], str, n);
	prompt->cursor += n;
	prompt->select = prompt->cursor;
}

/* delete word from the input field */
static void
delword(struct Prompt *prompt)
{
	while (prompt->cursor > 0 && strchr(worddelimiters, prompt->text[nextrune(prompt, -1)]))
		insert(prompt, NULL, nextrune(prompt, -1) - prompt->cursor);
	while (prompt->cursor > 0 && !strchr(worddelimiters, prompt->text[nextrune(prompt, -1)]))
		insert(prompt, NULL, nextrune(prompt, -1) - prompt->cursor);
}

/* we have been given the current selection, now insert it into input */
static void
paste(struct Prompt *prompt)
{
	char *p, *q;
	int di;             /* dummy variable */
	unsigned long dl;   /* dummy variable */
	Atom da;            /* dummy variable */

	if (XGetWindowProperty(dpy, prompt->win, utf8,
	                       0, prompt->textsize / 4 + 1, False,
	                       utf8, &da, &di, &dl, &dl, (unsigned char **)&p)
	    == Success && p) {
		insert(prompt, p, (q = strchr(p, '\n')) ? q - p : (ssize_t)strlen(p));
		XFree(p);
	}
}

/* navigate through history */
static char *
navhist(struct History *hist, int direction)
{
	if (direction < 0) {
		if (hist->index > 0)
			hist->index--;
	} else {
		if (hist->index + 1 < hist->size)
			hist->index++;
	}

	if (hist->index == hist->size)
		return NULL;

	return hist->entries[hist->index];
}

/* get list of possible completions */
static struct Item *
getcomplist(struct Prompt *prompt, struct Item *rootitem)
{
	struct Item *item, *curritem;
	char *beg;
	size_t nword = 0;
	size_t end, len;
	int found = 0;

	/* find list of possible completions */
	end = 0;
	curritem = rootitem;
	while (end < prompt->cursor) {
		nword++;
		beg = prompt->text + end;
		while (*beg != '\0' && strchr(worddelimiters, *beg))
			beg++;
		end = beg - prompt->text;
		while (end != prompt->cursor && prompt->text[end] != '\0'
			&& !strchr(worddelimiters, prompt->text[end]))
			end++;
		len = end - (beg - prompt->text);
		if (end != prompt->cursor) {
			for (item = curritem; item != NULL; item = item->next) {
				if ((*fstrncmp)(item->text, beg, len) == 0) {
					curritem = item->child;
					found = 1;
				}
			}
		}
	}

	if (!found && nword > 1)
		return NULL;

	return curritem;
}

/* get list of possible file completions */
static struct Item *
getfilelist(struct Prompt *prompt)
{
	struct Item *previtem, *item;
	struct Item *complist = NULL;
	char buf[BUFSIZ];
	size_t beg, len;
	size_t i;
	glob_t g;

	/* find filename to be completed */
	beg = prompt->cursor;
	if (beg)
		while (beg && !isspace(prompt->text[beg - 1]))
			beg--;
	len = prompt->cursor - beg;

	if (len >= BUFSIZ - 2)  /* 2 for '*' and NUL */
		return NULL;

	buf[0] = '\0';
	strncat(buf, prompt->text + beg, len);
	strcat(buf, "*");

	glob(buf, 0, NULL, &g);

	previtem = NULL;
	for (i = 0; i < g.gl_pathc; i++) {
		item = allocitem(0, g.gl_pathv[i], NULL);
		if (previtem) {
			item->prev = previtem;
			previtem->next = item;
		} else {
			complist = item;
		}
		previtem = item;
	}

	globfree(&g);

	return complist;
}

/* fill array of items to be printed in the window, return index of item to be highlighted*/
static size_t
fillitemarray(struct Prompt *prompt, struct Item *complist, int direction)
{
	struct Item *item;
	size_t beg, len;
	char *s;

	if (!prompt->cursor) {
		beg = 0;
		len = 0;
	} else {
		beg = prompt->cursor;
		while (beg > 0 && !strchr(worddelimiters, prompt->text[--beg]))
			;
		if (strchr(worddelimiters, prompt->text[beg]))
			beg++;
		len = prompt->cursor - beg;
	}

	if (direction >= 0) {
		item = (direction == 0) ? complist : prompt->itemarray[prompt->nitems - 1];
		for (prompt->nitems = 0;
		     prompt->nitems < prompt->maxitems && item != NULL;
		     item = item->next) {

			/* check if item->text matches prompt->text */
			s = item->text;
			while (*s != '\0') {
				if ((*fstrncmp)(s, prompt->text + beg, len) == 0) {
					prompt->itemarray[prompt->nitems++] = item;
					break;
				}
				while (*s != '\0' && strchr(worddelimiters, *s) == NULL)
					s++;
				while (*s != '\0' && strchr(worddelimiters, *s) != NULL)
					s++;
			}
		}
		return 0;
	} else {
		size_t i, n;

		item = prompt->itemarray[0];
		for (n = prompt->maxitems;
		     n > 0 && item != NULL;
		     item = item->prev) {

			/* check if item->text matches prompt->text */
			s = item->text;
			while (*s != '\0') {
				if ((*fstrncmp)(s, prompt->text + beg, len) == 0) {
					prompt->itemarray[--n] = item;
					break;
				}
				while (*s != '\0' && strchr(worddelimiters, *s) == NULL)
					s++;
				while (*s != '\0' && strchr(worddelimiters, *s) != NULL)
					s++;
			}
		}

		i = 0;
		while (n < prompt->maxitems) {
			prompt->itemarray[i++] = prompt->itemarray[n++];
		}
		prompt->nitems = i;

		return (prompt->nitems) ? prompt->nitems - 1 : 0;
	}
}

/* get Ctrl input operation */
static enum Ctrl
getoperation(KeySym ksym, unsigned state)
{
	switch (ksym) {
	case XK_Escape:         return CTRLCANCEL;
	case XK_Return:         return CTRLENTER;
	case XK_KP_Enter:       return CTRLENTER;
	case XK_ISO_Left_Tab:   return CTRLPREV;
	case XK_Tab:            return CTRLNEXT;
	case XK_Prior:          return CTRLPGUP;
	case XK_Next:           return CTRLPGDOWN;
	case XK_BackSpace:      return CTRLDELLEFT;
	case XK_Delete:         return CTRLDELRIGHT;
	case XK_Up:             return CTRLUP;
	case XK_Down:           return CTRLDOWN;
	case XK_Home:
		if (state & ShiftMask)
			return CTRLSELBOL;
		return CTRLBOL;
	case XK_End:
		if (state & ShiftMask)
			return CTRLSELEOL;
		return CTRLEOL;
	case XK_Left:
		if (state & ShiftMask && state & ControlMask)
			return CTRLSELWLEFT;
		if (state & ShiftMask)
			return CTRLSELLEFT;
		if (state & ControlMask)
			return CTRLWLEFT;
		return CTRLLEFT;
	case XK_Right:
		if (state & ShiftMask && state & ControlMask)
			return CTRLSELWRIGHT;
		if (state & ShiftMask)
			return CTRLSELRIGHT;
		if (state & ControlMask)
			return CTRLWRIGHT;
		return CTRLRIGHT;
	}

	/* handle Ctrl + Letter combinations */
	if (state & ControlMask) {
		if (ksym >= XK_a && ksym <= XK_z)
			return ctrl[LowerCase][ksym - XK_a];
		if (ksym >= XK_A && ksym <= XK_Z)
			return ctrl[UpperCase][ksym - XK_A];
		return CTRLNOTHING;
	}

	return INSERT;
}

/* handle key press */
static enum Press_ret
keypress(struct Prompt *prompt, struct Item *rootitem, struct History *hist, XKeyEvent *ev)
{
	static struct Item *complist;   /* list of possible completions */
	static int escaped = 1;         /* whether press escape will exit xprompt */
	static int filecomp = 0;        /* whether xprompt is in file completion */
	enum Ctrl operation;
	char buf[32];
	char *s;
	int len;
	int dir;
	KeySym ksym;
	Status status;

	len = XmbLookupString(xic, ev, buf, sizeof buf, &ksym, &status);
	switch (status) {
	default: /* XLookupNone, XBufferOverflow */
		return Noop;
	case XLookupChars:
		goto insert;
	case XLookupKeySym:
	case XLookupBoth:
		break;
	}

	switch (operation = getoperation(ksym, ev->state)) {
	case CTRLPASTE:
		delselection(prompt);
		XConvertSelection(dpy, clip, utf8, utf8, prompt->win, CurrentTime);
		return Noop;
	case CTRLCOPY:
		/* TODO */
		return Noop;
	case CTRLCANCEL:
		if (escaped || prompt->text[0] == '\0')
			return Esc;
		prompt->nitems = 0;
		escaped = 1;
		if (filecomp)
			freeitem(complist);
		break;
	case CTRLENTER:
		if (!escaped) {
			if (prompt->cursor && !strchr(worddelimiters, prompt->text[prompt->cursor - 1]))
				delword(prompt);
			if (!filecomp) {
				/*
				 * If not completing a file, insert item as is and
				 * append a space.
				 */
				insert(prompt, prompt->itemarray[prompt->curritem]->text,
				       strlen(prompt->itemarray[prompt->curritem]->text));
				insert(prompt, " ", 1);
			} else {
				/*
				 * If completing a file, insert only the basename (the
				 * part after the last slash).
				 */
				char *s, *p;
				for (p = prompt->itemarray[prompt->curritem]->text; *p; p++)
					if (strchr("/", *p))
						s = p + 1;
				insert(prompt, s, strlen(s));
			}
			prompt->nitems = 0;
			escaped = 1;
		} else {
			puts(prompt->text);
			return Enter;
		}
		break;
	case CTRLPREV:
		/* FALLTHROUGH */
	case CTRLNEXT:
		if (escaped) {
			complist = getcomplist(prompt, rootitem);
			prompt->curritem = 0;
			filecomp = 0;
		}
		if (complist == NULL && fflag) {
			complist = getfilelist(prompt);
			filecomp = 1;
		}
		if (complist == NULL) {
			filecomp = 0;
			break;
		}
		escaped = 0;
		if (prompt->nitems == 0) {
			prompt->curritem = fillitemarray(prompt, complist, 0);
		} else if (operation == CTRLNEXT) {
			if (prompt->curritem + 1 < prompt->nitems)
				prompt->curritem++;
			else if (prompt->itemarray[prompt->curritem]->next)
				prompt->curritem = fillitemarray(prompt, complist, +1);
		} else if (operation == CTRLPREV) {
			if (prompt->curritem > 0)
				prompt->curritem--;
			else if (prompt->itemarray[prompt->curritem]->prev)
				prompt->curritem = fillitemarray(prompt, complist, -1);
		}
		break;
	case CTRLPGUP:
	case CTRLPGDOWN:
		/* TODO */
		return Noop;
	case CTRLSELBOL:
	case CTRLBOL:
		prompt->cursor = 0;
		break;
	case CTRLSELEOL:
	case CTRLEOL:
		if (prompt->text[prompt->cursor] != '\0')
			prompt->cursor = strlen(prompt->text);
		break;
	case CTRLUP:
		/* FALLTHROUGH */
	case CTRLDOWN:
		dir = (operation == CTRLUP) ? -1 : +1;
		if (!hflag || !hist->size)
			return Noop;
		s = navhist(hist, dir);
		if (s) {
			insert(prompt, NULL, 0 - prompt->cursor);
			insert(prompt, s, strlen(s));
		}
		prompt->nitems = 0;
		escaped = 1;
		break;
	case CTRLSELLEFT:
	case CTRLLEFT:
		if (prompt->cursor > 0)
			prompt->cursor = nextrune(prompt, -1);
		else
			return Noop;
		break;
	case CTRLSELRIGHT:
	case CTRLRIGHT:
		if (prompt->text[prompt->cursor] != '\0')
			prompt->cursor = nextrune(prompt, +1);
		else
			return Noop;
		break;
	case CTRLSELWLEFT:
	case CTRLWLEFT:
		movewordedge(prompt, -1);
		break;
	case CTRLSELWRIGHT:
	case CTRLWRIGHT:
		movewordedge(prompt, +1);
		break;
	case CTRLDELBOL:
		insert(prompt, NULL, 0 - prompt->cursor);
		if (!escaped)
			goto match;
		break;
	case CTRLDELEOL:
		prompt->text[prompt->cursor] = '\0';
		if (!escaped)
			goto match;
		break;
	case CTRLDELRIGHT:
	case CTRLDELLEFT:
		if (prompt->cursor != prompt->select) {
			delselection(prompt);
			goto match;
		}
		if (operation == CTRLDELRIGHT) {
			if (prompt->text[prompt->cursor] == '\0')
				return Noop;
			prompt->cursor = nextrune(prompt, +1);
		}
		if (prompt->cursor == 0)
			return Noop;
		insert(prompt, NULL, nextrune(prompt, -1) - prompt->cursor);
		if (!escaped)
			goto match;
		break;
	case CTRLDELWORD:
		delword(prompt);
		if (!escaped)
			goto match;
		break;
	case CTRLNOTHING:
		return Noop;
	case INSERT:
insert:
		delselection(prompt);
		if (iscntrl(*buf))
			return Noop;
		insert(prompt, buf, len);
		if (!escaped)
			goto match;
		break;
	}

	if (prompt->nitems == 0)
		escaped = 1;

	/* if moving the cursor without selecting */
	if (operation == CTRLBOL ||
	    operation == CTRLEOL ||
	    operation == CTRLLEFT ||
	    operation == CTRLRIGHT ||
	    operation == CTRLWLEFT ||
	    operation == CTRLWRIGHT)
		prompt->select = prompt->cursor;

	return Draw;

match:
	if (filecomp) {     /* if in a file completion, cancel it */
		freeitem(complist);
		filecomp = 0;
		prompt->nitems = 0;
		escaped = 1;
	} else {            /* otherwise, rematch */
		complist = getcomplist(prompt, rootitem);
		if (complist == NULL)
			return Draw;
		prompt->curritem = fillitemarray(prompt, complist, 0);
		if (prompt->nitems == 0)
			escaped = 1;
	}
	return Draw;
}

/* get the position, in characters, of the cursor given a x position */
static size_t
getcurpos(struct Prompt *prompt, int x)
{
	char *s = prompt->text;
	int w = prompt->promptw;
	size_t len = 0;

	while (*s) {
		XGlyphInfo ext;

		if (x < w)
			break;

		len = strlen(prompt->text) - strlen(++s);
		XftTextExtentsUtf8(dpy, dc.font, (XftChar8 *)prompt->text, len, &ext);
		w = prompt->promptw + ext.xOff;
	}

	/* the loop returns len 1 char to the right */
	if (len && x + 3 < w)   /* 3 pixel tolerance */
		len--;

	return len;
}

/* handle button press */
static enum Press_ret
buttonpress(struct Prompt *prompt, XButtonEvent *ev)
{
	switch (ev->button) {
	case Button2:                               /* middle click paste */
		delselection(prompt);
		XConvertSelection(dpy, XA_PRIMARY, utf8, utf8, prompt->win, CurrentTime);
		return Noop;
	case Button1:
		if (ev->y < 0 || ev->x < 0)
			return Noop;
		if ((unsigned) ev->y <= prompt->h) {    /* point cursor position */
			prompt->select = prompt->cursor = getcurpos(prompt, ev->x);
			return Draw;
		}
		return Noop;
	default:
		return Noop;
	}

	return Noop;
}

/* handle button motion */
static enum Press_ret
buttonmotion(struct Prompt *prompt, XMotionEvent *ev)
{
	if (ev->y >= 0 && (unsigned) ev->y <= prompt->h) {
		prompt->select = getcurpos(prompt, ev->x);
	} else if (ev->y < 0) {    /* button motion to above the input field */
		prompt->select = 0;
	} else {            /* button motion to below the input field */
		if (prompt->text[prompt->cursor] != '\0')
			prompt->cursor = strlen(prompt->text);
	}

	return Draw;
}

/* run event loop, return 1 when user clicks Enter, 0 when user clicks Esc */
static int
run(struct Prompt *prompt, struct Item *rootitem, struct History *hist)
{
	XEvent ev;

	grabfocus(prompt->win);
	while (!XNextEvent(dpy, &ev)) {
		if (XFilterEvent(&ev, None))
			continue;
		switch (ev.type) {
		case Expose:
			if (ev.xexpose.count == 0)
				drawprompt(prompt);
			break;
		case FocusIn:
			/* regrab focus from parent window */
			if (ev.xfocus.window != prompt->win)
				grabfocus(prompt->win);
			break;
		case KeyPress:
			switch (keypress(prompt, rootitem, hist, &ev.xkey)) {
			case Esc:
				return 0;   /* return 0 to not save history */
			case Enter:
				return 1;   /* return 1 to save history */
			case Draw:
				drawprompt(prompt);
			    break;
			default:
				break;
			}
			break;
		case ButtonPress:
			switch (buttonpress(prompt, &ev.xbutton)) {
			case Draw:
				drawprompt(prompt);
			    break;
			default:
				break;
			}
			break;
		case MotionNotify:
			switch (buttonmotion(prompt, &ev.xmotion)) {
			case Draw:
				drawprompt(prompt);
			    break;
			default:
				break;
			}
			break;
		case VisibilityNotify:
			if (ev.xvisibility.state != VisibilityUnobscured)
				XRaiseWindow(dpy, prompt->win);
			break;
		case SelectionNotify:
			if (ev.xselection.property != utf8)
				break;
			paste(prompt);
			drawprompt(prompt);
			break;
		}
	}

	return 0;   /* UNREACHABLE */
}

/* save history in history file */
static void
savehist(struct Prompt *prompt, struct History *hist, FILE *fp)
{
	int diff;   /* whether the last history entry differs from prompt->text */
	int fd;

	if (hflag == 0)
		return;

	fd = fileno(fp);
	ftruncate(fd, 0);

	if (!hist->size) {
		fprintf(fp, "%s\n", prompt->text);
		return;
	}

	diff = strcmp(hist->entries[hist->size-1], prompt->text);

	hist->index = (diff && hist->size == histsize) ? 1 : 0;

	while (hist->index < hist->size)
		fprintf(fp, "%s\n", hist->entries[hist->index++]);

	if (diff)
		fprintf(fp, "%s\n", prompt->text);
}

/* free a item tree */
static void
freeitem(struct Item *root)
{
	struct Item *item, *tmp;

	item = root;
	while (item != NULL) {
		if (item->child != NULL)
			freeitem(item->child);
		tmp = item;
		item = item->next;
		free(tmp->text);
		free(tmp);
	}
}

/* free history entries */
static void
freehist(struct History *hist)
{
	size_t i;

	for (i = 0; i < hist->size; i++)
		free(hist->entries[i]);

	if (hist->entries)
		free(hist->entries);
}

/* free and clean up a prompt */
static void
freeprompt(struct Prompt *prompt)
{
	free(prompt->itemarray);

	XFreePixmap(dpy, prompt->pixmap);
	XftDrawDestroy(prompt->draw);
	XDestroyWindow(dpy, prompt->win);
}

/* clean up X stuff */
static void
cleanup(void)
{
	XftColorFree(dpy, visual, colormap, &dc.normal[ColorBG]);
	XftColorFree(dpy, visual, colormap, &dc.normal[ColorFG]);
	XftColorFree(dpy, visual, colormap, &dc.selected[ColorBG]);
	XftColorFree(dpy, visual, colormap, &dc.selected[ColorFG]);
	XftColorFree(dpy, visual, colormap, &dc.separator);
	XftColorFree(dpy, visual, colormap, &dc.border);

	XFreeGC(dpy, dc.gc);
	XCloseDisplay(dpy);
}

/* show usage */
static void
usage(void)
{
	(void)fprintf(stderr, "usage: xprompt [-fi] [-G gravity] [-g geometry]\n"
	                      "               [-h file] [-w windowid] [prompt]\n");
	exit(EXIT_FAILURE);
}
