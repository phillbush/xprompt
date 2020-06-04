/* See README file for copyright and license details. */

#include <err.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <glob.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xresource.h>
#include <X11/XKBlib.h>
#include <X11/Xft/Xft.h>

/* macros */
#define LEN(x) (sizeof (x) / sizeof (x[0]))
#define MAX(x,y) ((x)>(y)?(x):(y))
#define MIN(x,y) ((x)<(y)?(x):(y))

enum {ColorFG, ColorBG, ColorLast};
enum {LowerCase, UpperCase, CaseLast};
enum Keypress_ret {Draw, Esc, Enter, Noop};

/* Input operations */
enum Ctrl {
	CTRLPASTE = 0,  /* Paste from clipboard */
	CTRLCANCEL,     /* Cancel */
	CTRLENTER,      /* Choose item */
	CTRLPREV,       /* Select previous item */
	CTRLNEXT,       /* Select next item */
	CTRLPGUP,       /* Select item one screen above */
	CTRLPGDOWN,     /* Select item one screen below */
	CTRLBOL,        /* Move cursor to beginning of line */
	CTRLEOL,        /* Move cursor to end of line */
	CTRLUP,         /* Select previous item in the history */
	CTRLDOWN,       /* Select next item in the history */
	CTRLLEFT,       /* Move cursor one character the left */
	CTRLRIGHT,      /* Move cursor one character the right */
	CTRLWLEFT,      /* Move cursor one word to the left */
	CTRLWRIGHT,     /* Move cursor one word to the right */
	CTRLDELBOL,     /* Delete from cursor to beginning of line */
	CTRLDELEOL,     /* Delete from cursor to end of line */
	CTRLDELLEFT,    /* Delete character to left of cursor */
	CTRLDELRIGHT,   /* Delete character to right of cursor */
	CTRLDELWORD,    /* Delete from cursor to beginning of word */
	CTRLNOTHING,    /* Control does nothing */
	INSERT          /* Insert character as is */
};

/* draw context structure */
struct DC {
	XftColor normal[ColorLast];
	XftColor selected[ColorLast];
	XftColor border;
	XftColor separator;

	GC gc;
	XftFont *font;
};

/* completion items */
struct Item {
	char *text;
	unsigned level;
	struct Item *prev, *next;
	struct Item *parent;
	struct Item *child;
};

/* prompt */
struct Prompt {
	const char *promptstr;
	unsigned promptlen;
	unsigned promptw;       /* prompt width */

	char *text;             /* input field */
	size_t textsize;
	size_t cursor;

	struct Item **itemarray; /* array containing nitems matching text */
	size_t curritem;         /* current item selected */
	size_t nitems;           /* number of items in itemarray */
	size_t maxitems;         /* maximum number of items in itemarray */

	int gravity;
	int x, y;
	unsigned w, h;
	unsigned border;
	unsigned separator;

	Drawable pixmap;
	XftDraw *draw;
	Window win;
};

/* history */
struct History {
	char **entries;
	size_t index;
	size_t size;
};

/* function declarations */
static void getresources(void);
static void getcolor(const char *s, XftColor *color);
static void setupdc(void);
static void setupctrl(void);
static void setuppromptinput(struct Prompt *prompt);
static void setuppromptarray(struct Prompt *prompt);
static void setuppromptgeom(struct Prompt *prompt, Window parentwin);
static void setuppromptwin(struct Prompt *prompt, Window parentwin);
static struct Item *allocitem(unsigned level, const char *text);
static struct Item *builditems(unsigned level, const char *text);
static struct Item *parsestdin(FILE *fp);
static void parsehistfile(FILE *fp, struct History *hist);
static void grabkeyboard(void);
static void grabfocus(Window win);
static void drawprompt(struct Prompt *prompt);
static size_t nextrune(struct Prompt *prompt, int inc);
static void movewordedge(struct Prompt *prompt, int dir);
static void insert(struct Prompt *prompt, const char *str, ssize_t n);
static void delword(struct Prompt *prompt);
static char *navhist(struct History *hist, int direction);
static struct Item *getcomplist(struct Prompt *prompt, struct Item *rootitem);
static struct Item *getfilelist(struct Prompt *prompt);
static size_t fillitemarray(struct Prompt *prompt, struct Item *complist, int direction);
static enum Ctrl getoperation(KeySym ksym, unsigned state);
static enum Keypress_ret keypress(struct Prompt *prompt, struct Item *rootitem, struct History *hist, XKeyEvent *ev);
static int run(struct Prompt *prompt, struct Item *rootitem, struct History *hist);
static void savehist(struct History *hist, FILE *fp);
static void freeitem(struct Item *item);
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

/* flags */
static int wflag = 0;   /* whether to enable embeded prompt */
static int fflag = 0;   /* whether to enable filename completion */
static int hflag = 0;   /* whether to enable history */

/* ctrl operations */
static enum Ctrl ctrl['z' - 'a' + 1];

/* comparison function */
static int (*fstrncmp)(const char *, const char *, size_t);

#include "config.h"

/* xprompt */
int
main(int argc, char *argv[])
{
	struct History hist;
	struct Prompt prompt;
	struct Item *rootitem;
	Window parentwin = 0;
	FILE *histfp;
	char *histfile = NULL;
	char *str;
	int ch;
	int dosavehist;

	if ((str = getenv("XPROMPTHISTFILE")) != NULL)
		histfile = str;
	if ((str = getenv("XPROMPTCTRL")) != NULL)
		xpromptctrl = str;
	if ((str = getenv("WORDDELIMITERS")) != NULL)
		worddelimiters = str;

	fstrncmp = strncmp;
	while ((ch = getopt(argc, argv, "fh:iw:")) != -1) {
		switch (ch) {
		case 'f':
			fflag = 1;
			break;
		case 'h':
			histfile = optarg;
			break;
		case 'i':
			fstrncmp = strncasecmp;
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

	/* setup history */
	if (histfile != NULL && *histfile != '\0') {
		if ((histfp = fopen(histfile, "a+")) == NULL)
			warn("%s", histfile);
		else
			parsehistfile(histfp, &hist);
	}

	/* grab input */
	if (!wflag)
		grabkeyboard();

	/* run event loop */
	dosavehist = run(&prompt, rootitem, &hist);

	/* save history, if needed */
	if (dosavehist && hflag)
		savehist(&hist, histfp);

	/* freeing stuff */
	freeitem(rootitem);
	freeprompt(&prompt);
	cleanup();

	return 0;
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
	size_t i;
	char c;

	for (i = 0; i < ('z' - 'a' + 1); i++) {
		ctrl[i] = CTRLNOTHING;
	}

	for (i = 0; i < CTRLNOTHING && xpromptctrl[i] != '\0'; i++) {
		if (isalpha(c = tolower(xpromptctrl[i])))
			ctrl[c - 'a'] = i;
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
}

/* set up prompt window */
static void
setuppromptwin(struct Prompt *prompt, Window parentwin)
{
	XSetWindowAttributes swa;
	XSizeHints sizeh;
	XGlyphInfo ext;
	XIM xim;
	unsigned h;

	/* set window attributes */
	swa.override_redirect = True;
	swa.background_pixel = dc.normal[ColorBG].pixel;
	swa.border_pixel = dc.border.pixel;
	swa.event_mask = ExposureMask | KeyPressMask | VisibilityChangeMask;

	/* create window */
	prompt->win = XCreateWindow(dpy, parentwin,
	                            prompt->x, prompt->y, prompt->w, prompt->h, prompt->border,
	                            CopyFromParent, CopyFromParent, CopyFromParent,
	                            CWOverrideRedirect | CWBackPixel | CWBorderPixel | CWEventMask,
	                            &swa);

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
	xim = XOpenIM(dpy, NULL, NULL, NULL);
	xic = XCreateIC(xim, XNInputStyle, XIMPreeditNothing | XIMStatusNothing,
	                XNClientWindow, prompt->win, XNFocusWindow, prompt->win, NULL);

	/* calculate prompt string width */
	XftTextExtentsUtf8(dpy, dc.font, (XftChar8 *)prompt->promptstr,
	                   prompt->promptlen, &ext);
	prompt->promptw = ext.xOff + dc.font->height * 2;

	/* map window */
	XMapRaised(dpy, prompt->win);

	/* set input focus for the input method */
	XSetInputFocus(dpy, prompt->win, RevertToParent, CurrentTime);

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
allocitem(unsigned level, const char *text)
{
	struct Item *item;

	if ((item = malloc(sizeof *item)) == NULL)
		err(EXIT_FAILURE, "malloc");
	if ((item->text = strdup(text)) == NULL)
		err(EXIT_FAILURE, "strdup");
	item->level = level;
	item->prev = item->next = NULL;
	item->parent = NULL;
	item->child = NULL;

	return item;
}

/* build the item tree */
static struct Item *
builditems(unsigned level, const char *text)
{
	static struct Item *rootitem = NULL;
	static struct Item *previtem = NULL;
	struct Item *curritem;
	struct Item *item;
	unsigned i;

	curritem = allocitem(level, text);

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
	char *text;
	unsigned level = 0;

	rootitem = NULL;

	while (fgets(buf, BUFSIZ, fp) != NULL) {
		/* get the indentation level */
		level = strspn(buf, "\t");

		/* get the item text */
		s = buf + level;
		text = strtok(s, "\n");

		rootitem = builditems(level, text);
	}

	return rootitem;
}

/* parse the history file */
static void
parsehistfile(FILE *fp, struct History *hist)
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

	if (hist->size == 0) {
		hflag = 0;
	} else {
		hist->index = hist->size;
		hflag = 1;
	}
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

static void
drawprompt(struct Prompt *prompt)
{
	static size_t nitems = 0;
	static unsigned h;
	XGlyphInfo exttext, extcurs;
	unsigned curpos;
	int x, y;

	x = dc.font->height;
	y = prompt->h/2 + dc.font->ascent/2 - 1;
	h = prompt->h;

	/* draw background */
	XSetForeground(dpy, dc.gc, dc.normal[ColorBG].pixel);
	XFillRectangle(dpy, prompt->pixmap, dc.gc, 0, 0,
	               prompt->w, prompt->h);

	/* draw prompt string */
	if (prompt->promptlen != 0) {
		XftDrawStringUtf8(prompt->draw, &dc.normal[ColorFG], dc.font,
                      	  x, y, prompt->promptstr, prompt->promptlen);
		x = prompt->promptw;
	}

	/* draw input field text */
	XftDrawStringUtf8(prompt->draw, &dc.normal[ColorFG], dc.font,
                    x, y, prompt->text, strlen(prompt->text));

	/* draw cursor rectangle */
	y = prompt->h/2 - dc.font->height/2;
	XSetForeground(dpy, dc.gc, dc.normal[ColorFG].pixel);
	XftTextExtentsUtf8(dpy, dc.font, (XftChar8 *)prompt->text,
	                   strlen(prompt->text), &exttext);
	XftTextExtentsUtf8(dpy, dc.font, (XftChar8 *)prompt->text+prompt->cursor,
	                   strlen(prompt->text+prompt->cursor), &extcurs);
	curpos = exttext.xOff - extcurs.xOff;
	XFillRectangle(dpy, prompt->pixmap, dc.gc, x + curpos, y, 1, dc.font->height);

	/* resize window if needed */
	if (prompt->nitems && nitems != prompt->nitems) {
		h = prompt->h * (prompt->nitems + 1) + prompt->separator;

		if (prompt->gravity == SouthGravity
		    || prompt->gravity == SouthWestGravity
		    || prompt->gravity == SouthEastGravity) {
			XMoveResizeWindow(dpy, prompt->win, prompt->x,
			                  prompt->y - h + prompt->h,
			                  prompt->w, h);
		} else {
			XResizeWindow(dpy, prompt->win, prompt->w, h);
		}

		nitems = prompt->nitems;
	} else if (nitems && !prompt->nitems) {
		h = prompt->h;

		if (prompt->gravity == SouthGravity
		    || prompt->gravity == SouthWestGravity
		    || prompt->gravity == SouthEastGravity) {
			XMoveResizeWindow(dpy, prompt->win, prompt->x, prompt->y,
			                  prompt->w, prompt->h);
		} else {
			XResizeWindow(dpy, prompt->win, prompt->w, prompt->h);
		}
		nitems = 0;
	}

	if (nitems) {
		size_t i;

		y = prompt->h;
		h = prompt->h * prompt->nitems + prompt->separator;
		XSetForeground(dpy, dc.gc, dc.normal[ColorBG].pixel);
		XFillRectangle(dpy, prompt->pixmap, dc.gc, 0, y, prompt->w, h);

		y = prompt->h + prompt->separator/2;
		h = prompt->h * (prompt->nitems + 1) + prompt->separator;
		XSetForeground(dpy, dc.gc, dc.separator.pixel);
		XDrawLine(dpy, prompt->pixmap, dc.gc, 0, y, prompt->w, y);

		y = prompt->h + prompt->separator;
		for (i = 0; i < prompt->nitems; i++) {
			if (i == prompt->curritem) {
				XSetForeground(dpy, dc.gc, dc.selected[ColorBG].pixel);
				XFillRectangle(dpy, prompt->pixmap, dc.gc, 0, y, prompt->w, prompt->h);
			}
			XftDrawStringUtf8(prompt->draw, &dc.selected[ColorFG], dc.font,
                              x, y + prompt->h/2 + dc.font->ascent/2 - 1,
                              prompt->itemarray[i]->text,
                              strlen(prompt->itemarray[i]->text));
			y += prompt->h;
		}
	}

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
	while (end != prompt->cursor) {
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
		item = allocitem(0, g.gl_pathv[i]);
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

/* fill array of items to be printed in the window */
static size_t
fillitemarray(struct Prompt *prompt, struct Item *complist, int direction)
{
	struct Item *item;
	size_t beg, len;

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
			if ((*fstrncmp)(item->text, prompt->text + beg, len) == 0) {
				prompt->itemarray[prompt->nitems++] = item;
			}
		}
		return 0;
	} else {
		size_t i, n;

		item = prompt->itemarray[0];
		for (n = prompt->maxitems;
		     n > 0 && item != NULL;
		     item = item->prev) {
			if ((*fstrncmp)(item->text, prompt->text + beg, len) == 0) {
				prompt->itemarray[--n] = item;
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
	case XK_Home:           return CTRLBOL;
	case XK_End:            return CTRLEOL;
	case XK_BackSpace:      return CTRLDELLEFT;
	case XK_Delete:         return CTRLDELRIGHT;
	case XK_Up:             return CTRLUP;
	case XK_Down:           return CTRLDOWN;
	case XK_Left:
		if (state & ControlMask)
			return CTRLWLEFT;
		return CTRLLEFT;
	case XK_Right:
		if (state & ControlMask)
			return CTRLWRIGHT;
		return CTRLRIGHT;
	}

	if (state & ControlMask) {
		switch (ksym) {
		case XK_a: case XK_A:   return ctrl['a' - 'a'];
		case XK_b: case XK_B:   return ctrl['b' - 'a'];
		case XK_c: case XK_C:   return ctrl['c' - 'a'];
		case XK_d: case XK_D:   return ctrl['d' - 'a'];
		case XK_e: case XK_E:   return ctrl['e' - 'a'];
		case XK_f: case XK_F:   return ctrl['f' - 'a'];
		case XK_g: case XK_G:   return ctrl['g' - 'a'];
		case XK_h: case XK_H:   return ctrl['h' - 'a'];
		case XK_i: case XK_I:   return ctrl['i' - 'a'];
		case XK_j: case XK_J:   return ctrl['j' - 'a'];
		case XK_k: case XK_K:   return ctrl['k' - 'a'];
		case XK_l: case XK_L:   return ctrl['l' - 'a'];
		case XK_m: case XK_M:   return ctrl['m' - 'a'];
		case XK_n: case XK_N:   return ctrl['n' - 'a'];
		case XK_o: case XK_O:   return ctrl['o' - 'a'];
		case XK_p: case XK_P:   return ctrl['p' - 'a'];
		case XK_q: case XK_Q:   return ctrl['q' - 'a'];
		case XK_r: case XK_R:   return ctrl['r' - 'a'];
		case XK_s: case XK_S:   return ctrl['s' - 'a'];
		case XK_t: case XK_T:   return ctrl['t' - 'a'];
		case XK_u: case XK_U:   return ctrl['u' - 'a'];
		case XK_v: case XK_V:   return ctrl['v' - 'a'];
		case XK_w: case XK_W:   return ctrl['w' - 'a'];
		case XK_x: case XK_X:   return ctrl['x' - 'a'];
		case XK_y: case XK_Y:   return ctrl['y' - 'a'];
		case XK_z: case XK_Z:   return ctrl['z' - 'a'];
		default:                return CTRLNOTHING;
		}
	}

	return INSERT;
}

/* handle key press */
static enum Keypress_ret
keypress(struct Prompt *prompt, struct Item *rootitem, struct History *hist, XKeyEvent *ev)
{
	static struct Item *complist;   /* list of possible completions */
	static int escaped = 1;         /* whether press escape will exit xprompt */
	static int filecomp = 0;
	char buf[32];
	char *s;
	int len;
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

	switch (getoperation(ksym, ev->state)) {
	case CTRLPASTE:
		break;
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
			insert(prompt, prompt->itemarray[prompt->curritem]->text,
			       strlen(prompt->itemarray[prompt->curritem]->text));
			prompt->nitems = 0;
			escaped = 1;
		} else {
			puts(prompt->text);
			return Enter;
		}
		break;
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
		} else if (prompt->curritem + 1 < prompt->nitems) {
			prompt->curritem++;
		} else if (prompt->itemarray[prompt->curritem]->next) {
			prompt->curritem = fillitemarray(prompt, complist, +1);
		}
		break;
	case CTRLPREV:
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
		} else if (prompt->curritem > 0) {
			prompt->curritem--;
		} else if (prompt->itemarray[prompt->curritem]->prev) {
			prompt->curritem = fillitemarray(prompt, complist, -1);
		}
		break;
	case CTRLPGUP:
	case CTRLPGDOWN:
		return Noop;
	case CTRLBOL:
		prompt->cursor = 0;
		break;
	case CTRLEOL:
		if (prompt->text[prompt->cursor] != '\0')
			prompt->cursor = strlen(prompt->text);
		break;
	case CTRLLEFT:
		if (prompt->cursor > 0)
			prompt->cursor = nextrune(prompt, -1);
		else
			return Noop;
		break;
	case CTRLRIGHT:
		if (prompt->text[prompt->cursor] != '\0')
			prompt->cursor = nextrune(prompt, +1);
		else
			return Noop;
		break;
	case CTRLUP:
		if (!hflag)
			return Noop;
		s = navhist(hist, -1);
		if (s) {
			insert(prompt, NULL, 0 - prompt->cursor);
			insert(prompt, s, strlen(s));
		}
		prompt->nitems = 0;
		escaped = 1;
		break;
	case CTRLDOWN:
		if (!hflag)
			return Noop;
		s = navhist(hist, +1);
		if (s) {
			insert(prompt, NULL, 0 - prompt->cursor);
			insert(prompt, s, strlen(s));
		}
		prompt->nitems = 0;
		escaped = 1;
		break;
	case CTRLWLEFT:
		movewordedge(prompt, -1);
		break;
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
		if (prompt->text[prompt->cursor] == '\0')
			return Noop;
		prompt->cursor = nextrune(prompt, +1);
		/* FALLTHROUGH */
	case CTRLDELLEFT:
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
		if (iscntrl(*buf))
			return Noop;
		insert(prompt, buf, len);
		if (!escaped)
			goto match;
		break;
	}

	if (prompt->nitems == 0)
		escaped = 1;

	return Draw;

match:
	if (filecomp) {
		freeitem(complist);
		filecomp = 0;
		prompt->nitems = 0;
		escaped = 1;
	} else {
		complist = getcomplist(prompt, rootitem);
		if (complist == NULL)
			return Draw;
		prompt->curritem = fillitemarray(prompt, complist, 0);
		if (prompt->nitems == 0)
			escaped = 1;
	}
	return Draw;
}

/* run event loop, return 1 when user clicks Enter, 0 when user clicks Esc */
static int
run(struct Prompt *prompt, struct Item *rootitem, struct History *hist)
{
	XEvent ev;

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
				return 0;
			case Enter:
				return 1;
			case Draw:
				drawprompt(prompt);
			    break;
			default:
				break;
			}
		case VisibilityNotify:
			if (ev.xvisibility.state != VisibilityUnobscured)
				XRaiseWindow(dpy, prompt->win);
			break;
		}
	}

	return 0;   /* UNREACHABLE */
}

/* save history in history file */
static void
savehist(struct History *hist, FILE *fp)
{
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
	(void)fprintf(stderr, "usage: xprompt [-fi] [-h file] [-w windowid] [prompt]\n");
	exit(EXIT_FAILURE);
}
