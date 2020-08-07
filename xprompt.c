/* See README file for copyright and license details. */

#include <err.h>
#include <errno.h>
#include <ctype.h>
#include <limits.h>
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
#include <X11/extensions/Xinerama.h>
#include "xprompt.h"

#define PROGNAME "xprompt"


/*
 * function declarations
 */

/* initializers, and their helper routine */
static void ealloccolor(const char *s, XftColor *color);
static void getreferencepos(int *x_ret, int *y_ret);
static void parsefonts(const char *s);
static void initmonitor(void);
static void initresources(void);
static void initdc(void);
static void initctrl(void);

/* parsers and structure builders */
static struct Item *allocitem(unsigned level, const char *text, const char *description);
static struct Item *builditems(unsigned level, const char *text, const char *description);
static struct Item *parsestdin(FILE *fp);
static void loadhist(FILE *fp, struct History *hist);

/* text drawer, and its helper routine */
static FcChar32 getnextutf8char(const char *s, const char **end_ret);
static XftFont *getfontucode(FcChar32 ucode);
static int drawtext(XftDraw *draw, XftColor *color, int x, int y, unsigned h, const char *text, size_t len);

/* prompt structure setters */
static void parsegeometryspec(int *x, int *y, int *w, int *h);
static void setpromptinput(struct Prompt *prompt);
static void setpromptarray(struct Prompt *prompt);
static void setpromptgeom(struct Prompt *prompt, Window parentwin);
static void setpromptwin(struct Prompt *prompt, Window parentwin);

/* grabbers */
static void grabkeyboard(void);
static void grabfocus(Window win);

/* prompt drawers and controllers */
static size_t resizeprompt(struct Prompt *prompt, size_t nitems_old);
static void drawinput(struct Prompt *prompt, int copy);
static void drawitem(struct Prompt *prompt, size_t n, int copy);
static void drawprompt(struct Prompt *prompt);

/* text operations */
static size_t nextrune(struct Prompt *prompt, size_t position, int inc);
static size_t movewordedge(struct Prompt *prompt, size_t position, int dir);
static void delselection(struct Prompt *prompt);
static void insert(struct Prompt *prompt, const char *str, ssize_t n);
static void delword(struct Prompt *prompt);

/* history and completion functions */
static char *navhist(struct History *hist, int direction);
static struct Item *getcomplist(struct Prompt *prompt, struct Item *rootitem);
static struct Item *getfilelist(struct Prompt *prompt);
static size_t fillitemarray(struct Prompt *prompt, struct Item *complist, int direction);
static void savehist(struct Prompt *prompt, struct History *hist, FILE *fp);

/* utils for the event handlers */
static enum Ctrl getoperation(KeySym ksym, unsigned state);
static size_t getcurpos(struct Prompt *prompt, int x);
static int getitem(struct Prompt *prompt, int y);

/* event loop function and event handlers */
static int run(struct Prompt *prompt, struct Item *rootitem, struct History *hist);
static enum Press_ret keypress(struct Prompt *prompt, struct Item *rootitem, struct History *hist, XKeyEvent *ev);
static enum Press_ret buttonpress(struct Prompt *prompt, XButtonEvent *ev);
static enum Press_ret buttonmotion(struct Prompt *prompt, XMotionEvent *ev);
static enum Press_ret pointermotion(struct Prompt *prompt, XMotionEvent *ev);
static void paste(struct Prompt *prompt);
static void copy(struct Prompt *prompt, XSelectionRequestEvent *ev);

/* cleaners */
static void cleanitem(struct Item *item);
static void cleanhist(struct History *hist);
static void cleanprompt(struct Prompt *prompt);
static void cleanX(void);

/* show usage */
static void usage(void);


/*
 * Global variables
 */

/* X stuff */
static Display *dpy;
static int screen;
static Visual *visual;
static Window rootwin;
static Colormap colormap;
static XIC xic;
static struct DC dc;
static struct Monitor mon;
static Atom utf8;
static Atom clip;

/* flags */
static int dflag = 0;   /* whether to show only item descriptions */
static int fflag = 0;   /* whether to enable filename completion */
static int hflag = 0;   /* whether to enable history */
static int mflag = 0;   /* whether the user specified a monitor */
static int pflag = 0;   /* whether to enable password mode */
static int sflag = 0;   /* whether a single enter or esc closes xprompt*/
static int wflag = 0;   /* whether to enable embeded prompt */

/* ctrl operations */
static enum Ctrl ctrl[CaseLast][NLETTERS];

/* comparison function */
static int (*fstrncmp)(const char *, const char *, size_t) = strncmp;


/*
 * Include defaults
 */

#include "config.h"


/*
 * Function implementations
 */

/* xprompt: a dmenu rip-off with contextual completion */
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
	long n;

	/* get environment */
	if ((str = getenv("XPROMPTHISTFILE")) != NULL)
		histfile = str;
	if ((str = getenv("XPROMPTHISTSIZE")) != NULL)
		if ((n = strtol(str, NULL, 10)) > 0)
			config.histsize = n;
	if ((str = getenv("XPROMPTCTRL")) != NULL)
		config.xpromptctrl = str;
	if ((str = getenv("WORDDELIMITERS")) != NULL)
		config.worddelimiters = str;

	/* get options */
	while ((ch = getopt(argc, argv, "dfG:g:h:im:psw:")) != -1) {
		switch (ch) {
		case 'd':
			dflag = 1;
			break;
		case 'f':
			fflag = 1;
			break;
		case 'G':
			config.gravityspec = optarg;
			break;
		case 'g':
			config.geometryspec = optarg;
			break;
		case 'h':
			histfile = optarg;
			break;
		case 'i':
			fstrncmp = strncasecmp;
			break;
		case 'm':
			mflag = 1;
			n = strtol(optarg, &str, 10);
			if (errno == ERANGE || n > UINT_MAX || n < 0 || str == optarg || *str != '\0')
				errx(1, "improper monitor %s", optarg);
			mon.num = n;
			break;
		case 'p':
			pflag = 1;
			break;
		case 's':
			sflag = 1;
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

	/* init */
	initmonitor();
	initresources();
	initctrl();
	initdc();
	utf8 = XInternAtom(dpy, "UTF8_STRING", False);
	clip = XInternAtom(dpy, "CLIPBOARD", False);

	/* setup prompt */
	if (!parentwin)
		parentwin = rootwin;
	if (argc == 0)
		prompt.promptstr = NULL;
	else
		prompt.promptstr = *argv;
	setpromptinput(&prompt);
	setpromptarray(&prompt);
	setpromptgeom(&prompt, parentwin);
	setpromptwin(&prompt, parentwin);

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
	cleanitem(rootitem);
	cleanhist(&hist);
	cleanprompt(&prompt);
	cleanX();

	return EXIT_SUCCESS;
}

/* get color from color string */
static void
ealloccolor(const char *s, XftColor *color)
{
	if(!XftColorAllocName(dpy, visual, colormap, s, color))
		errx(1, "cannot allocate color: %s", s);
}

/* get position of focused windows or of the cursor */
static void
getreferencepos(int *x_ret, int *y_ret)
{
	Window win, focuswin = None, parentwin = None;
	Window dw, *dws;    /* dummy variable */
	int di;             /* dummy variable */
	unsigned du;        /* dummy variable */
	XWindowAttributes wa;

	XGetInputFocus(dpy, &win, &di);
	if (win != rootwin && win != None) {
		while (parentwin != rootwin) {
			if (XQueryTree(dpy, win, &dw, &parentwin, &dws, &du) && dws)
				XFree(dws);
			focuswin = win;
			win = parentwin;
		}
		if (focuswin != None && XGetWindowAttributes(dpy, focuswin, &wa)) {
			*x_ret = wa.x + wa.width / 2;
			*y_ret = wa.y + wa.height / 2;
			return;
		}
	}
	if (XQueryPointer(dpy, rootwin, &dw, &dw, x_ret, y_ret, &di, &di, &du))
		return;

	x_ret = 0;
	y_ret = 0;
}

/* parse color string */
static void
parsefonts(const char *s)
{
	const char *p;
	char buf[1024];
	size_t nfont = 0;

	dc.nfonts = 1;
	for (p = s; *p; p++)
		if (*p == ',')
			dc.nfonts++;

	if ((dc.fonts = calloc(dc.nfonts, sizeof *dc.fonts)) == NULL)
		err(1, "calloc");

	p = s;
	while (*p != '\0') {
		size_t i;

		i = 0;
		while (isspace(*p))
			p++;
		while (i < sizeof buf && *p != '\0' && *p != ',')
			buf[i++] = *p++;
		if (i >= sizeof buf)
			errx(1, "font name too long");
		if (*p == ',')
			p++;
		buf[i] = '\0';
		if (nfont == 0)
			if ((dc.pattern = FcNameParse((FcChar8 *)buf)) == NULL)
				errx(1, "the first font in the cache must be loaded from a font string");
		if ((dc.fonts[nfont++] = XftFontOpenName(dpy, screen, buf)) == NULL)
			errx(1, "cannot load font");
	}
}

/* query monitor information and cursor position */
static void
initmonitor(void)
{
	XineramaScreenInfo *info = NULL;
	int nmons;
	int i;

	if ((info = XineramaQueryScreens(dpy, &nmons)) != NULL) {
		int selmon = 0;

		/* the user didn't specified a monitor, so let's use the monitor
		 * of the focused window or the monitor with the cursor */
		if (!mflag || mon.num < 0 || mon.num >= nmons) {
			int x, y;

			getreferencepos(&x, &y);
			for (i = 0; i < nmons; i++) {
				if (BETWEEN(x, info[i].x_org, info[i].x_org + info[i].width) &&
				    BETWEEN(y, info[i].y_org, info[i].y_org + info[i].height)) {
					selmon = i;
					break;
				}
			}
		} else {
			selmon = mon.num;
		}

		mon.x = info[selmon].x_org;
		mon.y = info[selmon].y_org;
		mon.w = info[selmon].width;
		mon.h = info[selmon].height;
	} else {
		mon.x = mon.y = 0;
		mon.w = DisplayWidth(dpy, screen);
		mon.h = DisplayHeight(dpy, screen);
	}
}

/* read xrdb for configuration options */
static void
initresources(void)
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
			config.number_items = n;
	if (XrmGetResource(xdb, "xprompt.borderWidth", "*", &type, &xval) == True)
		if ((n = strtol(xval.addr, NULL, 10)) > 0)
			config.border_pixels = n;
	if (XrmGetResource(xdb, "xprompt.separatorWidth", "*", &type, &xval) == True)
		if ((n = strtol(xval.addr, NULL, 10)) > 0)
			config.separator_pixels = n;
	if (XrmGetResource(xdb, "xprompt.background", "*", &type, &xval) == True)
		config.background_color = strdup(xval.addr);
	if (XrmGetResource(xdb, "xprompt.foreground", "*", &type, &xval) == True)
		config.foreground_color = strdup(xval.addr);
	if (XrmGetResource(xdb, "xprompt.description", "*", &type, &xval) == True)
		config.description_color = strdup(xval.addr);
	if (XrmGetResource(xdb, "xprompt.hoverbackground", "*", &type, &xval) == True)
		config.hoverbackground_color = strdup(xval.addr);
	if (XrmGetResource(xdb, "xprompt.hoverforeground", "*", &type, &xval) == True)
		config.hoverforeground_color = strdup(xval.addr);
	if (XrmGetResource(xdb, "xprompt.hoverdescription", "*", &type, &xval) == True)
		config.hoverdescription_color = strdup(xval.addr);
	if (XrmGetResource(xdb, "xprompt.selbackground", "*", &type, &xval) == True)
		config.selbackground_color = strdup(xval.addr);
	if (XrmGetResource(xdb, "xprompt.selforeground", "*", &type, &xval) == True)
		config.selforeground_color = strdup(xval.addr);
	if (XrmGetResource(xdb, "xprompt.seldescription", "*", &type, &xval) == True)
		config.seldescription_color = strdup(xval.addr);
	if (XrmGetResource(xdb, "xprompt.separator", "*", &type, &xval) == True)
		config.separator_color = strdup(xval.addr);
	if (XrmGetResource(xdb, "xprompt.border", "*", &type, &xval) == True)
		config.border_color = strdup(xval.addr);
	if (XrmGetResource(xdb, "xprompt.font", "*", &type, &xval) == True)
		config.font = strdup(xval.addr);
	if (XrmGetResource(xdb, "xprompt.geometry", "*", &type, &xval) == True)
		config.geometryspec = strdup(xval.addr);
	if (XrmGetResource(xdb, "xprompt.gravity", "*", &type, &xval) == True)
		config.gravityspec = strdup(xval.addr);

	XrmDestroyDatabase(xdb);
}

/* init draw context */
static void
initdc(void)
{
	/* get color pixels */
	ealloccolor(config.hoverbackground_color,   &dc.hover[ColorBG]);
	ealloccolor(config.hoverforeground_color,   &dc.hover[ColorFG]);
	ealloccolor(config.hoverdescription_color,  &dc.hover[ColorCM]);
	ealloccolor(config.background_color,        &dc.normal[ColorBG]);
	ealloccolor(config.foreground_color,        &dc.normal[ColorFG]);
	ealloccolor(config.description_color,       &dc.normal[ColorCM]);
	ealloccolor(config.selbackground_color,     &dc.selected[ColorBG]);
	ealloccolor(config.selforeground_color,     &dc.selected[ColorFG]);
	ealloccolor(config.seldescription_color,    &dc.selected[ColorCM]);
	ealloccolor(config.separator_color,         &dc.separator);
	ealloccolor(config.border_color,            &dc.border);

	/* try to get font */
	parsefonts(config.font);

	/* create common GC */
	dc.gc = XCreateGC(dpy, rootwin, 0, NULL);

	/* compute left text padding */
	dc.pad = dc.fonts[0]->height;
}

/* set control keybindings */
static void
initctrl(void)
{
	size_t i, j;

	for (i = 0; i < CaseLast; i++) {
		for (j = 0; j < (NLETTERS); j++) {
			ctrl[i][j] = CTRLNOTHING;
		}
	}

	for (i = 0; i < CTRLNOTHING && config.xpromptctrl[i] != '\0'; i++) {
		if (!isalpha(config.xpromptctrl[i]))
			continue;
		if (isupper(config.xpromptctrl[i]))
			ctrl[UpperCase][config.xpromptctrl[i] - 'A'] = i;
		if (islower(config.xpromptctrl[i]))
			ctrl[LowerCase][config.xpromptctrl[i] - 'a'] = i;
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
		/* discard empty lines */
		if (*buf && *buf == '\n')
			continue;

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

	if ((hist->entries = calloc(config.histsize, sizeof *hist)) == NULL)
		err(EXIT_FAILURE, "calloc");

	hist->size = 0;

	rewind(fp);
	while (hist->size < config.histsize && fgets(buf, sizeof buf, fp) != NULL) {
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

/* get next utf8 char from s return its codepoint and set next_ret to pointer to end of character */
static FcChar32
getnextutf8char(const char *s, const char **next_ret)
{
	static const unsigned char utfbyte[] = {0x80, 0x00, 0xC0, 0xE0, 0xF0};
	static const unsigned char utfmask[] = {0xC0, 0x80, 0xE0, 0xF0, 0xF8};
	static const FcChar32 utfmin[] = {0, 0x00,  0x80,  0x800,  0x10000};
	static const FcChar32 utfmax[] = {0, 0x7F, 0x7FF, 0xFFFF, 0x10FFFF};
	/* 0xFFFD is the replacement character, used to represent unknown characters */
	static const FcChar32 unknown = 0xFFFD;
	FcChar32 ucode;         /* FcChar32 type holds 32 bits */
	size_t usize = 0;       /* n' of bytes of the utf8 character */
	size_t i;

	*next_ret = s+1;

	/* get code of first byte of utf8 character */
	for (i = 0; i < sizeof utfmask; i++) {
		if (((unsigned char)*s & utfmask[i]) == utfbyte[i]) {
			usize = i;
			ucode = (unsigned char)*s & ~utfmask[i];
			break;
		}
	}

	/* if first byte is a continuation byte or is not allowed, return unknown */
	if (i == sizeof utfmask || usize == 0)
		return unknown;

	/* check the other usize-1 bytes */
	s++;
	for (i = 1; i < usize; i++) {
		*next_ret = s+1;
		/* if byte is nul or is not a continuation byte, return unknown */
		if (*s == '\0' || ((unsigned char)*s & utfmask[0]) != utfbyte[0])
			return unknown;
		/* 6 is the number of relevant bits in the continuation byte */
		ucode = (ucode << 6) | ((unsigned char)*s & ~utfmask[0]);
		s++;
	}

	/* check if ucode is invalid or in utf-16 surrogate halves */
	if (!BETWEEN(ucode, utfmin[usize], utfmax[usize])
	    || BETWEEN (ucode, 0xD800, 0xDFFF))
		return unknown;

	return ucode;
}

/* get which font contains a given code point */
static XftFont *
getfontucode(FcChar32 ucode)
{
	FcCharSet *fccharset = NULL;
	FcPattern *fcpattern = NULL;
	FcPattern *match = NULL;
	XftFont *retfont = NULL;
	XftResult result;
	size_t i;

	for (i = 0; i < dc.nfonts; i++)
		if (XftCharExists(dpy, dc.fonts[i], ucode) == FcTrue)
			return dc.fonts[i];

	/* create a charset containing our code point */
	fccharset = FcCharSetCreate();
	FcCharSetAddChar(fccharset, ucode);

	/* create a pattern akin to the dc.pattern but containing our charset */
	if (fccharset) {
		fcpattern = FcPatternDuplicate(dc.pattern);
		FcPatternAddCharSet(fcpattern, FC_CHARSET, fccharset);
	}

	/* find pattern matching fcpattern */
	if (fcpattern) {
		FcConfigSubstitute(NULL, fcpattern, FcMatchPattern);
		FcDefaultSubstitute(fcpattern);
		match = XftFontMatch(dpy, screen, fcpattern, &result);
	}

	/* if found a pattern, open its font */
	if (match) {
		retfont = XftFontOpenPattern(dpy, match);
		if (retfont && XftCharExists(dpy, retfont, ucode) == FcTrue) {
			if ((dc.fonts = realloc(dc.fonts, dc.nfonts+1)) == NULL)
				err(1, "realloc");
			dc.fonts[dc.nfonts] = retfont;
			return dc.fonts[dc.nfonts++];
		} else {
			XftFontClose(dpy, retfont);
		}
	}

	/* in case no fount was found, return the first one */
	return dc.fonts[0];
}

/* draw text into XftDraw, return width of text glyphs */
static int
drawtext(XftDraw *draw, XftColor *color, int x, int y, unsigned h, const char *text, size_t len)
{
	int textwidth = 0;
	const char *end;

	end = text + len;
	while (*text && (!len || text < end)) {
		XftFont *currfont;
		XGlyphInfo ext;
		FcChar32 ucode;
		const char *next;
		size_t len;

		ucode = getnextutf8char(text, &next);
		currfont = getfontucode(ucode);

		len = next - text;
		XftTextExtentsUtf8(dpy, currfont, (XftChar8 *)text, len, &ext);
		textwidth += ext.xOff;

		if (draw) {
			int texty;

			texty = y + (h - (currfont->ascent + currfont->descent))/2 + currfont->ascent;
			XftDrawStringUtf8(draw, color, currfont, x, texty, (XftChar8 *)text, len);
			x += ext.xOff;
		}

		text = next;
	}

	return textwidth;
}

/* get number from *s into n, return 1 if error */
static int
getnum(const char **s, int *n)
{
	int retval;
	long num;
	char *endp;

	num = strtol(*s, &endp, 10);
	retval = errno == ERANGE || num > INT_MAX || num < 0 || endp == *s;
	*s = endp;
	*n = num;
	return retval;
}

/* parse geometry specification and return geometry values */
static void
parsegeometryspec(int *x, int *y, int *w, int *h)
{
	int sign;
	int n;
	const char *s;

	*x = *y = *w = *h = 0;
	s = config.geometryspec;

	if (getnum(&s, &n))
		goto error;
	if (*s == '%') {
		if (n > 100)
			goto error;
		*w = (n * (mon.w - config.border_pixels * 2))/100;
		s++;
	} else {
		*w = n;
	}

	if (*s++ != 'x')
		goto error;

	if (getnum(&s, &n))
		goto error;
	if (*s == '%') {
		if (n > 100)
			goto error;
		*h = (n * (mon.h - config.border_pixels * 2))/100;
		s++;
	} else {
		*h = n;
	}

	if (*s == '+' || *s == '-') {
		sign = (*s++ == '-') ? -1 : 1;
		if (getnum(&s, &n))
			goto error;
		*x = n * sign;
		if (*s != '+' && *s != '-')
			goto error;

		sign = (*s++ == '-') ? -1 : 1;
		if (getnum(&s, &n))
			goto error;
		*y = n * sign;
	}
	if (*s != '\0')
		goto error;

	return;

error:
	errx(1, "improper geometry specification %s\n", config.geometryspec);
}

/* allocate memory for the text input field */
static void
setpromptinput(struct Prompt *prompt)
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
setpromptarray(struct Prompt *prompt)
{
	prompt->maxitems = config.number_items;
	prompt->hoveritem = prompt->maxitems;
	prompt->selitem = 0;
	prompt->nitems = 0;
	if ((prompt->itemarray = calloc(sizeof *prompt->itemarray, prompt->maxitems)) == NULL)
		err(EXIT_FAILURE, "malloc");
}

/* calculate prompt geometry */
static void
setpromptgeom(struct Prompt *prompt, Window parentwin)
{
	int x, y, w, h;     /* geometry of monitor or parent window */

	/* try to get attributes of parent window */
	if (wflag) {
		XWindowAttributes wa;   /* window attributes of the parent window */
		if (!XGetWindowAttributes(dpy, parentwin, &wa))
			errx(1, "could not get window attributes of 0x%lx", parentwin);
		x = y = 0;
		w = wa.width;
		h = wa.height;
	} else {
		x = mon.x;
		y = mon.y;
		w = mon.w;
		h = mon.h;
	}
	
	/* get width of border and separator */
	prompt->border = config.border_pixels;
	prompt->separator = config.separator_pixels;

	/* get prompt gravity */
	if (config.gravityspec == NULL || strcmp(config.gravityspec, "N") == 0)
		prompt->gravity = NorthGravity;
	else if (strcmp(config.gravityspec, "NW") == 0)
		prompt->gravity = NorthWestGravity;
	else if (strcmp(config.gravityspec, "NE") == 0)
		prompt->gravity = NorthEastGravity;
	else if (strcmp(config.gravityspec, "W") == 0)
		prompt->gravity = WestGravity;
	else if (strcmp(config.gravityspec, "C") == 0)
		prompt->gravity = CenterGravity;
	else if (strcmp(config.gravityspec, "E") == 0)
		prompt->gravity = EastGravity;
	else if (strcmp(config.gravityspec, "SW") == 0)
		prompt->gravity = SouthWestGravity;
	else if (strcmp(config.gravityspec, "S") == 0)
		prompt->gravity = SouthGravity;
	else if (strcmp(config.gravityspec, "SE") == 0)
		prompt->gravity = SouthEastGravity;
	else
		errx(EXIT_FAILURE, "Unknown gravity %s", config.gravityspec);

	/* get prompt geometry */
	parsegeometryspec(&prompt->x, &prompt->y, &prompt->w, &prompt->h);

	/* update prompt size, based on parent window's size */
	if (prompt->w == 0)
		prompt->w = w - prompt->border * 2;
	if (prompt->h == 0)
		prompt->h = DEFHEIGHT;
	prompt->w = MIN(prompt->w, w);
	prompt->h = MIN(prompt->h, h);

	/* update prompt position, based on prompt's gravity */
	prompt->x += x;
	prompt->y += y;
	switch (prompt->gravity) {
	case NorthWestGravity:
		break;
	case NorthGravity:
		prompt->x += (w - prompt->w)/2 - prompt->border;
		break;
	case NorthEastGravity:
		prompt->x += w - prompt->w - prompt->border * 2;
		break;
	case WestGravity:
		prompt->y += (h - prompt->h)/2 - prompt->border;
		break;
	case CenterGravity:
		prompt->x += (w - prompt->w)/2 - prompt->border;
		prompt->y += (h - prompt->h)/2 - prompt->border;
		break;
	case EastGravity:
		prompt->x += w - prompt->w - prompt->border * 2;
		prompt->y += (h - prompt->h)/2 - prompt->border;
		break;
	case SouthWestGravity:
		prompt->y += h - prompt->h - prompt->border * 2;
		break;
	case SouthGravity:
		prompt->x += (w - prompt->w)/2 - prompt->border;
		prompt->y += h - prompt->h - prompt->border * 2;
		break;
	case SouthEastGravity:
		prompt->x += w - prompt->w - prompt->border * 2;
		prompt->y += h - prompt->h - prompt->border * 2;
		break;
	}

	/* calculate prompt string width */
	if (prompt->promptstr && *prompt->promptstr)
		prompt->promptw = drawtext(NULL, NULL, 0, 0, 0, prompt->promptstr, 0) + dc.pad * 2;
	else
		prompt->promptw = dc.pad;

	/* description x position */
	prompt->descx = prompt->w / TEXTPART;
	prompt->descx = MAX(prompt->descx, MINTEXTWIDTH);
}

/* set up prompt window */
static void
setpromptwin(struct Prompt *prompt, Window parentwin)
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
	               | ButtonPressMask | PointerMotionMask;
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

/* draw the text on input field, return position of the cursor */
static void
drawinput(struct Prompt *prompt, int copy)
{
	unsigned minpos, maxpos;
	unsigned curpos;            /* where to draw the cursor */
	int x, y, xtext;
	int widthpre, widthsel, widthpos;

	x = prompt->promptw;

	minpos = MIN(prompt->cursor, prompt->select);
	maxpos = MAX(prompt->cursor, prompt->select);

	/* draw background */
	XSetForeground(dpy, dc.gc, dc.normal[ColorBG].pixel);
	XFillRectangle(dpy, prompt->pixmap, dc.gc, x, 0,
	               prompt->w - x, prompt->h);

	/* draw text before selection */
	xtext = x;
	widthpre = (minpos)
	         ? drawtext(prompt->draw, &dc.normal[ColorFG], xtext, 0, prompt->h,
	                    prompt->text, minpos)
	         : 0;

	/* draw selected text */
	xtext += widthpre;
	widthsel = (maxpos - minpos)
	         ? drawtext(NULL, NULL, 0, 0, 0, prompt->text+minpos, maxpos-minpos)
	         : 0;
	XSetForeground(dpy, dc.gc, dc.normal[ColorFG].pixel);
	XFillRectangle(dpy, prompt->pixmap, dc.gc, xtext, 0, widthsel, prompt->h);
	drawtext(prompt->draw, &dc.normal[ColorBG], xtext, 0, prompt->h,
	         prompt->text+minpos, maxpos-minpos);

	/* draw text after selection */
	xtext += widthsel;
	widthpos = drawtext(prompt->draw, &dc.normal[ColorFG], xtext, 0, prompt->h,
	                    prompt->text+maxpos, 0);

	/* draw cursor rectangle */
	curpos = x + widthpre;
	y = prompt->h/2 - dc.pad/2;
	XSetForeground(dpy, dc.gc, dc.normal[ColorFG].pixel);
	XFillRectangle(dpy, prompt->pixmap, dc.gc, curpos, y, 1, dc.pad);

	/* commit drawing */
	if (copy)
		XCopyArea(dpy, prompt->pixmap, prompt->win, dc.gc, x, 0,
		          prompt->w - x, prompt->h, x, 0);
}

/* draw nth item in the item array */
static void
drawitem(struct Prompt *prompt, size_t n, int copy)
{
	XftColor *color;
	int textwidth = 0;
	int y;

	color = (n == prompt->selitem) ? dc.selected
	      : (n == prompt->hoveritem) ? dc.hover
	      : dc.normal;
	y = (n + 1) * prompt->h + prompt->separator;

	/* draw background */
	XSetForeground(dpy, dc.gc, color[ColorBG].pixel);
	XFillRectangle(dpy, prompt->pixmap, dc.gc, 0, y, prompt->w, prompt->h);

	if (!(dflag && prompt->itemarray[n]->description)) {
		/* draw item text */
		textwidth = drawtext(prompt->draw, &color[ColorFG], prompt->promptw, y, prompt->h,
		            prompt->itemarray[n]->text, 0);
		textwidth = textwidth + dc.pad * 2 + prompt->promptw;
		textwidth = MAX(textwidth, prompt->descx);

		/* if item has a description, draw it */
		if (prompt->itemarray[n]->description != NULL)
			drawtext(prompt->draw, &color[ColorCM], textwidth, y, prompt->h,
			         prompt->itemarray[n]->description, 0);
	} else {    /* item has description and dflag is on */
		drawtext(prompt->draw, &color[ColorFG], prompt->promptw, y, prompt->h,
		         prompt->itemarray[n]->description, 0);
	}

	/* commit drawing */
	if (copy)
		XCopyArea(dpy, prompt->pixmap, prompt->win, dc.gc, prompt->promptw, y,
		          prompt->w - prompt->promptw, prompt->h, prompt->promptw, y);
}

/* draw the prompt */
static void
drawprompt(struct Prompt *prompt)
{
	static size_t nitems = 0;       /* number of items in the dropdown list */
	unsigned h;
	int x, y;
	size_t i;

	x = dc.pad;
	h = prompt->h;

	/* draw the prompt string and update x to the end of it */
	XSetForeground(dpy, dc.gc, dc.normal[ColorBG].pixel);
	XFillRectangle(dpy, prompt->pixmap, dc.gc, 0, 0, prompt->promptw, prompt->h);
	if (prompt->promptstr) {
		drawtext(prompt->draw, &dc.normal[ColorFG], x, 0, prompt->h,
		         prompt->promptstr, 0);
		x = prompt->promptw;
	}

	/* draw input field text and set position of the cursor */
	if (!pflag)
		drawinput(prompt, 0);

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
		drawitem(prompt, i, 0);

done:
	/* commit drawing */
	XCopyArea(dpy, prompt->pixmap, prompt->win, dc.gc, 0, 0,
	          prompt->w, h, 0, 0);
}

/* return location of next utf8 rune in the given direction (+1 or -1) */
static size_t
nextrune(struct Prompt *prompt, size_t position, int inc)
{
	ssize_t n;

	for (n = position + inc;
	     n + inc >= 0 && (prompt->text[n] & 0xc0) == 0x80;
	     n += inc)
		;
	return n;
}

/* move cursor to start (dir = -1) or end (dir = +1) of the word */
static size_t
movewordedge(struct Prompt *prompt, size_t pos, int dir)
{
	if (dir < 0) {
		while (pos > 0 && strchr(config.worddelimiters, prompt->text[nextrune(prompt, pos, -1)]))
			pos = nextrune(prompt, pos, -1);
		while (pos > 0 && !strchr(config.worddelimiters, prompt->text[nextrune(prompt, pos, -1)]))
			pos = nextrune(prompt, pos, -1);
	} else {
		while (prompt->text[pos] && strchr(config.worddelimiters, prompt->text[pos]))
			pos = nextrune(prompt, pos, +1);
		while (prompt->text[pos] && !strchr(config.worddelimiters, prompt->text[pos]))
			pos = nextrune(prompt, pos, +1);
	}

	return pos;
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
	while (prompt->cursor > 0 && strchr(config.worddelimiters, prompt->text[nextrune(prompt, prompt->cursor, -1)]))
		insert(prompt, NULL, nextrune(prompt, prompt->cursor, -1) - prompt->cursor);
	while (prompt->cursor > 0 && !strchr(config.worddelimiters, prompt->text[nextrune(prompt, prompt->cursor, -1)]))
		insert(prompt, NULL, nextrune(prompt, prompt->cursor, -1) - prompt->cursor);
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

/* send SelectionNotify event to requestor window */
static void
copy(struct Prompt *prompt, XSelectionRequestEvent *ev)
{
	XSelectionEvent xselev;
	Atom xa_targets;

	xselev.type = SelectionNotify;
	xselev.requestor = ev->requestor;
	xselev.selection = ev->selection;
	xselev.target = ev->target;
	xselev.time = ev->time;
	xselev.property = None;

	if (ev->property == None)
		ev->property = ev->target;

	xa_targets = XInternAtom(dpy, "TARGETS", 0);

	if (ev->target == xa_targets) {     /* respond with the supported type */
		XChangeProperty(dpy, ev->requestor, ev->property, XA_ATOM, 32,
		                PropModeReplace, (unsigned char *)&utf8, 1);
	} else if (ev->target == utf8 || ev->target == XA_STRING) {
		unsigned minpos, maxpos;
		char *seltext;

		if (prompt->cursor == prompt->select)
			goto done;  /* if nothing is selected, all done */

		minpos = MIN(prompt->cursor, prompt->select);
		maxpos = MAX(prompt->cursor, prompt->select);
		seltext = strndup(prompt->text + minpos, maxpos - minpos + 1);
		seltext[maxpos - minpos] = '\0';

		XChangeProperty(dpy, ev->requestor, ev->property, ev->target, 8,
		                PropModeReplace, (unsigned char *)seltext,
		                strlen(seltext));
		xselev.property = ev->property;

		free(seltext);
	}

done:
	/* all done, send SelectionNotify event to listener */
	if (!XSendEvent(dpy, ev->requestor, True, 0L, (XEvent *)&xselev))
		warnx("Error sending SelectionNotify event");
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
	char *beg, *text;
	size_t nword = 0;
	size_t end, len;
	int found = 0;

	/* find list of possible completions */
	end = 0;
	curritem = rootitem;
	while (end < prompt->cursor) {
		nword++;
		beg = prompt->text + end;
		while (*beg != '\0' && strchr(config.worddelimiters, *beg))
			beg++;
		end = beg - prompt->text;
		while (end != prompt->cursor && prompt->text[end] != '\0'
			&& !strchr(config.worddelimiters, prompt->text[end]))
			end++;
		len = end - (beg - prompt->text);
		if (end != prompt->cursor) {
			for (item = curritem; item != NULL; item = item->next) {
				text = (dflag && item->description) ? item->description : item->text;
				if ((*fstrncmp)(text, beg, len) == 0) {
					curritem = item->child;
					found = 1;
					break;
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
		while (beg > 0 && !strchr(config.worddelimiters, prompt->text[--beg]))
			;
		if (strchr(config.worddelimiters, prompt->text[beg]))
			beg++;
		len = prompt->cursor - beg;
	}

	if (direction >= 0) {
		item = (direction == 0) ? complist : prompt->itemarray[prompt->nitems - 1];
		for (prompt->nitems = 0;
		     prompt->nitems < prompt->maxitems && item != NULL;
		     item = item->next) {

			/* check if item->text matches prompt->text */
			s = (dflag && item->description) ? item->description : item->text;
			while (*s != '\0') {
				if ((*fstrncmp)(s, prompt->text + beg, len) == 0) {
					prompt->itemarray[prompt->nitems++] = item;
					break;
				}
				while (*s != '\0' && strchr(config.worddelimiters, *s) == NULL)
					s++;
				while (*s != '\0' && strchr(config.worddelimiters, *s) != NULL)
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
			s = (dflag && item->description) ? item->description : item->text;
			while (*s != '\0') {
				if ((*fstrncmp)(s, prompt->text + beg, len) == 0) {
					prompt->itemarray[--n] = item;
					break;
				}
				while (*s != '\0' && strchr(config.worddelimiters, *s) == NULL)
					s++;
				while (*s != '\0' && strchr(config.worddelimiters, *s) != NULL)
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
	static int completion = 0;      /* whether currently in completion */
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
		XConvertSelection(dpy, clip, utf8, utf8, prompt->win, CurrentTime);
		return Noop;
	case CTRLCOPY:
		XSetSelectionOwner(dpy, clip, prompt->win, CurrentTime);
		return Noop;
	case CTRLCANCEL:
		if (sflag || !completion || prompt->text[0] == '\0')
			return Esc;
		prompt->nitems = 0;
		completion = 0;
		if (filecomp)
			cleanitem(complist);
		break;
	case CTRLENTER:
		if (completion) {
			if (prompt->cursor && !strchr(config.worddelimiters, prompt->text[prompt->cursor - 1]))
				delword(prompt);
			if (!filecomp) {
				/*
				 * If not completing a file, insert item as is
				 */
				insert(prompt, prompt->itemarray[prompt->selitem]->text,
				       strlen(prompt->itemarray[prompt->selitem]->text));
			} else {
				/*
				 * If completing a file, insert only the basename (the
				 * part after the last slash).
				 */
				char *s, *p;
				for (p = prompt->itemarray[prompt->selitem]->text; *p; p++)
					if (strchr("/", *p))
						s = p + 1;
				insert(prompt, s, strlen(s));
			}
			prompt->nitems = 0;
			if (sflag)
				completion = 0;
		}
		if (!completion) {
			puts(prompt->text);
			return Enter;
		}
		completion = 0;
		break;
	case CTRLPREV:
		/* FALLTHROUGH */
	case CTRLNEXT:
		if (!completion) {
			complist = getcomplist(prompt, rootitem);
			prompt->selitem = 0;
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
		completion = 1;
		if (prompt->nitems == 0) {
			prompt->selitem = fillitemarray(prompt, complist, 0);
		} else if (operation == CTRLNEXT) {
			if (prompt->selitem + 1 < prompt->nitems)
				prompt->selitem++;
			else if (prompt->itemarray[prompt->selitem]->next)
				prompt->selitem = fillitemarray(prompt, complist, +1);
		} else if (operation == CTRLPREV) {
			if (prompt->selitem > 0)
				prompt->selitem--;
			else if (prompt->itemarray[prompt->selitem]->prev)
				prompt->selitem = fillitemarray(prompt, complist, -1);
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
		completion = 0;
		break;
	case CTRLSELLEFT:
	case CTRLLEFT:
		if (prompt->cursor > 0)
			prompt->cursor = nextrune(prompt, prompt->cursor, -1);
		else
			return Noop;
		break;
	case CTRLSELRIGHT:
	case CTRLRIGHT:
		if (prompt->text[prompt->cursor] != '\0')
			prompt->cursor = nextrune(prompt, prompt->cursor, +1);
		else
			return Noop;
		break;
	case CTRLSELWLEFT:
	case CTRLWLEFT:
		prompt->cursor = movewordedge(prompt, prompt->cursor, -1);
		break;
	case CTRLSELWRIGHT:
	case CTRLWRIGHT:
		prompt->cursor = movewordedge(prompt, prompt->cursor, +1);
		break;
	case CTRLDELBOL:
		insert(prompt, NULL, 0 - prompt->cursor);
		break;
	case CTRLDELEOL:
		prompt->text[prompt->cursor] = '\0';
		break;
	case CTRLDELRIGHT:
	case CTRLDELLEFT:
		if (prompt->cursor != prompt->select) {
			delselection(prompt);
			break;
		}
		if (operation == CTRLDELRIGHT) {
			if (prompt->text[prompt->cursor] == '\0')
				return Noop;
			prompt->cursor = nextrune(prompt, prompt->cursor, +1);
		}
		if (prompt->cursor == 0)
			return Noop;
		insert(prompt, NULL, nextrune(prompt, prompt->cursor, -1) - prompt->cursor);
		break;
	case CTRLDELWORD:
		delword(prompt);
		break;
	case CTRLNOTHING:
		return Noop;
	case INSERT:
insert:
		operation = INSERT;
		if (iscntrl(*buf))
			return Noop;
		delselection(prompt);
		insert(prompt, buf, len);
		break;
	}

	if (ISMOTION(operation)) {          /* moving cursor while selecting */
		prompt->select = prompt->cursor;
		return DrawInput;
	}
	if (ISSELECTION(operation)) {       /* moving cursor while selecting */
		XSetSelectionOwner(dpy, XA_PRIMARY, prompt->win, CurrentTime);
		return DrawInput;
	}
	if (ISEDITING(operation)) {
		if (completion && filecomp) {   /* if in a file completion, cancel it */
			cleanitem(complist);
			filecomp = 0;
			prompt->nitems = 0;
			completion = 0;
			return DrawPrompt;
		} else if (completion) {        /* if in regular completion, rematch */
			complist = getcomplist(prompt, rootitem);
			if (complist == NULL)
				return DrawPrompt;
			prompt->selitem = fillitemarray(prompt, complist, 0);
			if (prompt->nitems == 0)
				completion = 0;
			return DrawPrompt;
		} else {                        /* if not in completion just redraw input field */
			return DrawInput;
		}
	}
	return DrawPrompt;
}

/* get the position, in characters, of the cursor given a x position */
static size_t
getcurpos(struct Prompt *prompt, int x)
{
	const char *s = prompt->text;
	int w = prompt->promptw;
	size_t len = 0;

	while (*s) {
		const char *next;
		int textwidth;

		if (x < w)
			break;

		(void)getnextutf8char(s, &next);
		len = strlen(prompt->text) - strlen(++s);
		textwidth = drawtext(NULL, NULL, 0, 0, 0, prompt->text, len);
		w = prompt->promptw + textwidth;
		s = next;
	}

	/* the loop returns len 1 char to the right */
	if (len && x + 3 < w)   /* 3 pixel tolerance */
		len--;

	return len;
}

/* get item on a given y position */
static int
getitem(struct Prompt *prompt, int y)
{
	y -= prompt->h + prompt->separator;

	return y / prompt->h;
}

/* handle button press */
static enum Press_ret
buttonpress(struct Prompt *prompt, XButtonEvent *ev)
{
	static int word = 0;    /* whether a word was selected by double click */
	static Time lasttime = 0;
	size_t curpos;

	switch (ev->button) {
	case Button2:                               /* middle click paste */
		delselection(prompt);
		XConvertSelection(dpy, XA_PRIMARY, utf8, utf8, prompt->win, CurrentTime);
		return Noop;
	case Button1:
		if (ev->y < 0 || ev->x < 0)
			return Noop;
		if (ev->y <= prompt->h) {
			curpos = getcurpos(prompt, ev->x);
			if (word && ev->time - lasttime < DOUBLECLICK) {
				prompt->cursor = 0;
				if (prompt->text[prompt->cursor] != '\0')
					prompt->select = strlen(prompt->text);
				word = 0;
			} else if (ev->time - lasttime < DOUBLECLICK) {
				prompt->cursor = movewordedge(prompt, curpos, -1);
				prompt->select = movewordedge(prompt, curpos, +1);
				word = 1;
			} else {
				prompt->select = prompt->cursor = curpos;
				word = 0;
			}
			lasttime = ev->time;
			return DrawInput;
		} else if (ev->y > prompt->h + prompt->separator) {
			prompt->selitem = getitem(prompt, ev->y);
			if (sflag) {
				insert(prompt, prompt->itemarray[prompt->selitem]->text,
				       strlen(prompt->itemarray[prompt->selitem]->text));
				puts(prompt->text);
				return Enter;
			}
			return DrawPrompt;
		}
		return Noop;
	default:
		return Noop;
	}

	return Noop;
}

/* handle button motion X event */
static enum Press_ret
buttonmotion(struct Prompt *prompt, XMotionEvent *ev)
{
	size_t prevselect, prevcursor;

	prevselect = prompt->select;
	prevcursor = prompt->cursor;

	if (ev->y >= 0 && ev->y <= prompt->h)
		prompt->select = getcurpos(prompt, ev->x);
	else if (ev->y < 0)
		prompt->select = 0;
	else if (prompt->text[prompt->cursor] != '\0')
		prompt->cursor = strlen(prompt->text);
	else
		return Noop;

	/* if the selection didn't change there's no need to redraw input */
	if (prompt->select == prevselect && prompt->cursor == prevcursor)
		return Noop;

	return DrawInput;
}

/* handle pointer motion X event */
static enum Press_ret
pointermotion(struct Prompt *prompt, XMotionEvent *ev)
{
	int miny, maxy;

	miny = prompt->h + prompt->separator;
	maxy = miny + prompt->h * prompt->nitems;

	if (ev->y < miny || ev->y >= maxy)
		prompt->hoveritem = prompt->maxitems;
	else
		prompt->hoveritem = getitem(prompt, ev->y);

	return DrawPrompt;
}

/* run event loop, return 1 when user clicks Enter, 0 when user clicks Esc */
static int
run(struct Prompt *prompt, struct Item *rootitem, struct History *hist)
{
	XEvent ev;
	enum Press_ret retval = Noop;

	grabfocus(prompt->win);

	while (!XNextEvent(dpy, &ev)) {
		if (XFilterEvent(&ev, None))
			continue;

		switch (ev.type) {
		case Expose:
			if (ev.xexpose.count == 0)
				retval = DrawPrompt;
			break;
		case FocusIn:
			/* regrab focus from parent window */
			if (ev.xfocus.window != prompt->win)
				grabfocus(prompt->win);
			break;
		case KeyPress:
			retval = keypress(prompt, rootitem, hist, &ev.xkey);
			break;
		case ButtonPress:
			retval = buttonpress(prompt, &ev.xbutton);
			break;
		case MotionNotify:
			if (ev.xmotion.y <= prompt->h
			    && ev.xmotion.state == Button1Mask)
				retval = buttonmotion(prompt, &ev.xmotion);
			else
				retval = pointermotion(prompt, &ev.xmotion);
			break;
		case VisibilityNotify:
			if (ev.xvisibility.state != VisibilityUnobscured)
				XRaiseWindow(dpy, prompt->win);
			break;
		case SelectionNotify:
			if (ev.xselection.property != utf8)
				break;
			delselection(prompt);
			paste(prompt);
			retval = DrawInput;
			break;
		case SelectionRequest:
			copy(prompt, &ev.xselectionrequest);
			break;
		}

		switch (retval) {
		case Esc:
			return 0;   /* return 0 to not save history */
		case Enter:
			return 1;   /* return 1 to save history */
		case DrawInput:
			drawinput(prompt, 1);
			break;
		case DrawPrompt:
			drawprompt(prompt);
			break;
		default:
			break;
		}

		retval = Noop;
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

	hist->index = (diff && hist->size == config.histsize) ? 1 : 0;

	while (hist->index < hist->size)
		fprintf(fp, "%s\n", hist->entries[hist->index++]);

	if (diff)
		fprintf(fp, "%s\n", prompt->text);
}

/* free a item tree */
static void
cleanitem(struct Item *root)
{
	struct Item *item, *tmp;

	item = root;
	while (item != NULL) {
		if (item->child != NULL)
			cleanitem(item->child);
		tmp = item;
		item = item->next;
		free(tmp->text);
		free(tmp);
	}
}

/* free history entries */
static void
cleanhist(struct History *hist)
{
	size_t i;

	for (i = 0; i < hist->size; i++)
		free(hist->entries[i]);

	if (hist->entries)
		free(hist->entries);
}

/* free and clean up a prompt */
static void
cleanprompt(struct Prompt *prompt)
{
	free(prompt->text);
	free(prompt->itemarray);

	XFreePixmap(dpy, prompt->pixmap);
	XftDrawDestroy(prompt->draw);
	XDestroyWindow(dpy, prompt->win);
}

/* clean up X stuff */
static void
cleanX(void)
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
	(void)fprintf(stderr, "usage: xprompt [-dfips] [-G gravity] [-g geometry] [-h file]\n"
	                      "               [-m monitor] [-w windowid] [prompt]\n");
	exit(EXIT_FAILURE);
}
