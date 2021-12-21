/* See LICENSE file for copyright and license details. */

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
#include <X11/cursorfont.h>
#include <X11/extensions/Xinerama.h>
#include "xprompt.h"

/* X stuff */
static Display *dpy;
static int screen;
static Visual *visual;
static Window root;
static Colormap colormap;
static XrmDatabase xdb;
static Cursor cursor;
static char *xrm;
static struct IC ic;
static struct DC dc;
static struct Monitor mon;
static Atom atoms[AtomLast];

/* flags */
static int aflag = 0;   /* whether to keep looking for arguments to complete */
static int cflag = 0;   /* whether to always show autocomplete */
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

/* whether xprompt is in file completion */
static int filecomp = 0;

/* Include defaults */
#include "config.h"

/* show usage */
static void
usage(void)
{
	(void)fprintf(stderr, "usage: xprompt [-adfips] [-G gravity] [-g geometry] [-h file]\n"
	                      "               [-m monitor] [-w windowid] [prompt]\n");
	exit(1);
}

/* call strdup checking for error */
static char *
estrdup(const char *s)
{
	char *t;

	if ((t = strdup(s)) == NULL)
		err(1, "strdup");
	return t;
}

/* call calloc checking for error */
static void *
ecalloc(size_t nmemb, size_t size)
{
	void *p;

	if ((p = calloc(nmemb, size)) == NULL)
		err(1, "calloc");
	return p;
}

/* call malloc checking for error */
static void *
emalloc(size_t size)
{
	void *p;

	if ((p = malloc(size)) == NULL)
		err(1, "malloc");
	return p;
}

/* get configuration from X resources */
static void
getresources(void)
{
	XrmValue xval;
	char *type;

	if (xrm == NULL || xdb == NULL)
		return;

	if (XrmGetResource(xdb, "xprompt.items", "*", &type, &xval) == True)
		config.number_items = strtoul(xval.addr, NULL, 10);
	if (XrmGetResource(xdb, "xprompt.borderWidth", "*", &type, &xval) == True)
		config.border_pixels = strtoul(xval.addr, NULL, 10);
	if (XrmGetResource(xdb, "xprompt.separatorWidth", "*", &type, &xval) == True)
		config.separator_pixels = strtoul(xval.addr, NULL, 10);
	if (XrmGetResource(xdb, "xprompt.background", "*", &type, &xval) == True)
		config.background_color = xval.addr;
	if (XrmGetResource(xdb, "xprompt.foreground", "*", &type, &xval) == True)
		config.foreground_color = xval.addr;
	if (XrmGetResource(xdb, "xprompt.description", "*", &type, &xval) == True)
		config.description_color = xval.addr;
	if (XrmGetResource(xdb, "xprompt.hoverbackground", "*", &type, &xval) == True)
		config.hoverbackground_color = xval.addr;
	if (XrmGetResource(xdb, "xprompt.hoverforeground", "*", &type, &xval) == True)
		config.hoverforeground_color = xval.addr;
	if (XrmGetResource(xdb, "xprompt.hoverdescription", "*", &type, &xval) == True)
		config.hoverdescription_color = xval.addr;
	if (XrmGetResource(xdb, "xprompt.selbackground", "*", &type, &xval) == True)
		config.selbackground_color = xval.addr;
	if (XrmGetResource(xdb, "xprompt.selforeground", "*", &type, &xval) == True)
		config.selforeground_color = xval.addr;
	if (XrmGetResource(xdb, "xprompt.seldescription", "*", &type, &xval) == True)
		config.seldescription_color = xval.addr;
	if (XrmGetResource(xdb, "xprompt.separator", "*", &type, &xval) == True)
		config.separator_color = xval.addr;
	if (XrmGetResource(xdb, "xprompt.border", "*", &type, &xval) == True)
		config.border_color = xval.addr;
	if (XrmGetResource(xdb, "xprompt.font", "*", &type, &xval) == True)
		config.font = xval.addr;
	if (XrmGetResource(xdb, "xprompt.geometry", "*", &type, &xval) == True)
		config.geometryspec = xval.addr;
	if (XrmGetResource(xdb, "xprompt.gravity", "*", &type, &xval) == True)
		config.gravityspec = xval.addr;
}

/* get configuration from environment variables */
static void
getenvironment(void)
{
	char *s;

	if ((s = getenv("XPROMPTHISTFILE")) != NULL)
		config.histfile = s;
	if ((s = getenv("XPROMPTHISTSIZE")) != NULL)
		config.histsize = strtoul(s, NULL, 10);
	if ((s = getenv("XPROMPTCTRL")) != NULL)
		config.xpromptctrl = s;
	if ((s = getenv("WORDDELIMITERS")) != NULL)
		config.worddelimiters = s;
}

/* get configuration from command-line options, return non-option argument */
static char *
getoptions(int argc, char *argv[], Window *win_ret)
{
	int ch;

	/* get options */
	while ((ch = getopt(argc, argv, "acdfG:g:h:im:psw:")) != -1) {
		switch (ch) {
		case 'a':
			aflag = 1;
			break;
		case 'c':
			cflag = 1;
			break;
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
			config.histfile = optarg;
			break;
		case 'i':
			fstrncmp = strncasecmp;
			break;
		case 'm':
			mflag = 1;
			mon.num = strtoul(optarg, NULL, 10);
			break;
		case 'p':
			pflag = 1;
			break;
		case 's':
			sflag = 1;
			break;
		case 'w':
			wflag = 1;
			*win_ret = strtoul(optarg, NULL, 0);
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
	else if (argc == 1)
		return *argv;
	return NULL;
}

/* get color from color string */
static void
ealloccolor(const char *s, XftColor *color)
{
	if(!XftColorAllocName(dpy, visual, colormap, s, color))
		errx(1, "could not allocate color: %s", s);
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
	if (win != root && win != None) {
		while (parentwin != root) {
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
	if (XQueryPointer(dpy, root, &dw, &dw, x_ret, y_ret, &di, &di, &du))
		return;

	x_ret = 0;
	y_ret = 0;
}

/* parse color string */
static void
parsefonts(const char *s)
{
	const char *p;
	char buf[INPUTSIZ];
	size_t nfont = 0;

	dc.nfonts = 1;
	for (p = s; *p; p++)
		if (*p == ',')
			dc.nfonts++;
	dc.fonts = ecalloc(dc.nfonts, sizeof *dc.fonts);
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

/* initialize atoms array */
static void
initatoms(void)
{
	char *atomnames[AtomLast] = {
		[Utf8String] = "UTF8_STRING",
		[Clipboard] = "CLIPBOARD",
		[Targets] = "TARGETS"
	};

	XInternAtoms(dpy, atomnames, AtomLast, False, atoms);
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

		XFree(info);
	} else {
		mon.x = mon.y = 0;
		mon.w = DisplayWidth(dpy, screen);
		mon.h = DisplayHeight(dpy, screen);
	}
}

/* init draw context */
static void
initdc(void)
{
	/* get colors */
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
	dc.gc = XCreateGC(dpy, root, 0, NULL);

	/* compute left text padding */
	dc.pad = dc.fonts[0]->height;
}

/* init cursors */
static void
initcursor(void)
{
	cursor = XCreateFontCursor(dpy, XC_xterm);
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
allocitem(const char *text, const char *description)
{
	struct Item *item;

	item = emalloc(sizeof *item);
	item->text = estrdup(text);
	item->description = description ? estrdup(description) : NULL;
	item->prevmatch = item->nextmatch = NULL;
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
	static unsigned prevlevel = 0;
	struct Item *curritem;
	struct Item *item;
	unsigned i;

	curritem = allocitem(text, description);

	if (previtem == NULL) {             /* there is no item yet */
		curritem->parent = NULL;
		rootitem = curritem;
	} else if (level < prevlevel) {     /* item is continuation of a parent item */
		/* go up the item tree until find the item the current one continues */
		for (item = previtem, i = level;
		     item != NULL && i != prevlevel;
		     item = item->parent, i++)
			;
		if (item == NULL)
			errx(1, "improper indentation detected");

		curritem->parent = item->parent;
		item->next = curritem;
		curritem->prev = item;
	} else if (level == prevlevel) {    /* item is continues current item */
		curritem->parent = previtem->parent;
		previtem->next = curritem;
		curritem->prev = previtem;
	} else if (level > prevlevel) {     /* item begins a new list */
		previtem->child = curritem;
		curritem->parent = previtem;
	}

	prevlevel = level;
	previtem = curritem;

	return rootitem;
}

/* create completion items from the stdin */
static struct Item *
parsestdin(FILE *fp)
{
	struct Item *rootitem;
	char *s, buf[INPUTSIZ];
	char *text, *description;
	unsigned level = 0;

	rootitem = NULL;

	while (fgets(buf, sizeof buf, fp) != NULL) {
		/* discard empty lines */
		if (*buf && *buf == '\n')
			continue;

		/* get the indentation level */
		level = strspn(buf, "\t");

		/* get the item text */
		s = buf + level;
		text = strtok(s, "\t\n");
		description = strtok(NULL, "\t\n");

		/* discard empty text entries */
		if (!text || *text == '\0')
			continue;

		rootitem = builditems(level, text, description);
	}

	return rootitem;
}

/* parse the history file */
static void
loadhist(FILE *fp, struct History *hist)
{
	char buf[INPUTSIZ];
	char *s;
	size_t len;

	hist->entries = ecalloc(config.histsize, sizeof *hist);
	hist->size = 0;
	rewind(fp);
	while (hist->size < config.histsize && fgets(buf, sizeof buf, fp) != NULL) {
		len = strlen(buf);
		if (len && buf[--len] == '\n')
			buf[len] = '\0';
		s = estrdup(buf);
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
	if (!BETWEEN(ucode, utfmin[usize], utfmax[usize]) || BETWEEN (ucode, 0xD800, 0xDFFF))
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
	FcResult result;
	XftFont *retfont = NULL;
	size_t i;

	/* search through the fonts supplied by the user for the first one supporting ucode */
	for (i = 0; i < dc.nfonts; i++)
		if (XftCharExists(dpy, dc.fonts[i], ucode) == FcTrue)
			return dc.fonts[i];

	/* if could not find a font in dc.fonts, search through system fonts */

	/* create a charset containing our code point */
	fccharset = FcCharSetCreate();
	FcCharSetAddChar(fccharset, ucode);

	/* create a pattern akin to the dc.pattern but containing our charset */
	if (fccharset) {
		fcpattern = FcPatternDuplicate(dc.pattern);
		FcPatternAddCharSet(fcpattern, FC_CHARSET, fccharset);
	}

	/* find font matching fcpattern */
	if (fcpattern) {
		FcDefaultSubstitute(fcpattern);
		FcConfigSubstitute(NULL, fcpattern, FcMatchPattern);
		match = FcFontMatch(NULL, fcpattern, &result);
	}

	/* if found a font, open it */
	if (match && result == FcResultMatch) {
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
drawtext(XftDraw *draw, XftColor *color, int x, int y, unsigned h, const char *text, size_t textlen)
{
	int textwidth = 0;
	XftFont *currfont, *nextfont;
	XGlyphInfo ext;
	FcChar32 ucode;
	const char *next, *tmp, *end;
	size_t len = 0;

	nextfont = dc.fonts[0];
	end = text + textlen;
	while (*text && (!textlen || text < end)) {
		tmp = text;
		do {
			next = tmp;
			currfont = nextfont;
			ucode = getnextutf8char(next, &tmp);
			nextfont = getfontucode(ucode);
		} while (*next && (!textlen || next < end) && currfont == nextfont);
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

	if (pflag)
		return;

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

	/* draw selected text or pre-edited text */
	xtext += widthpre;
	widthsel = 0;
	if (ic.composing) {                     /* draw pre-edit text and underline */
		widthsel = drawtext(NULL, NULL, 0, 0, 0, ic.text, 0);
		y = (prompt->h + dc.pad) / 2 + 1;
		XSetForeground(dpy, dc.gc, dc.normal[ColorFG].pixel);
		XFillRectangle(dpy, prompt->pixmap, dc.gc, xtext, y, widthsel, 1);
		drawtext(prompt->draw, &dc.normal[ColorFG], xtext, 0, prompt->h, ic.text, 0);
	} else if (maxpos - minpos > 0) {       /* draw seleceted text in reverse */
		widthsel = drawtext(NULL, NULL, 0, 0, 0, prompt->text+minpos, maxpos-minpos);
		XSetForeground(dpy, dc.gc, dc.normal[ColorFG].pixel);
		XFillRectangle(dpy, prompt->pixmap, dc.gc, xtext, 0, widthsel, prompt->h);
		drawtext(prompt->draw, &dc.normal[ColorBG], xtext, 0, prompt->h, prompt->text+minpos, maxpos-minpos);
	}

	/* draw text after selection */
	xtext += widthsel;
	widthpos = drawtext(prompt->draw, &dc.normal[ColorFG], xtext, 0, prompt->h,
	                    prompt->text+maxpos, 0);

	/* draw cursor rectangle */
	curpos = x + widthpre + ((ic.composing && ic.caret) ? drawtext(NULL, NULL, 0, 0, 0, ic.text, ic.caret) : 0);
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
	int x, y;

	color = (prompt->itemarray[n] == prompt->selitem) ? dc.selected
	      : (prompt->itemarray[n] == prompt->hoveritem) ? dc.hover
	      : dc.normal;
	y = (n + 1) * prompt->h + prompt->separator;
	x = config.indent ? prompt->promptw : dc.pad;

	/* draw background */
	XSetForeground(dpy, dc.gc, color[ColorBG].pixel);
	XFillRectangle(dpy, prompt->pixmap, dc.gc, 0, y, prompt->w, prompt->h);

	if (!(dflag && prompt->itemarray[n]->description)) {
		/* draw item text */
		textwidth = drawtext(prompt->draw, &color[ColorFG], x, y, prompt->h, prompt->itemarray[n]->text, 0);
		textwidth = x + textwidth + dc.pad * 2;
		textwidth = MAX(textwidth, prompt->descx);

		/* if item has a description, draw it */
		if (prompt->itemarray[n]->description != NULL)
			drawtext(prompt->draw, &color[ColorCM], textwidth, y, prompt->h,
			         prompt->itemarray[n]->description, 0);
	} else {    /* item has description and dflag is on */
		drawtext(prompt->draw, &color[ColorFG], x, y, prompt->h,
		         prompt->itemarray[n]->description, 0);
	}

	/* commit drawing */
	if (copy)
		XCopyArea(dpy, prompt->pixmap, prompt->win, dc.gc, x, y, prompt->w - x, prompt->h, x, y);
}

/* draw the prompt */
static void
drawprompt(struct Prompt *prompt)
{
	static size_t nitems = 0;       /* number of items in the dropdown list */
	unsigned h;
	int x, y;
	size_t i;

	x = prompt->promptw;

	/* draw input field text and set position of the cursor */
	drawinput(prompt, 0);

	/* resize window and get new value of number of items */
	nitems = resizeprompt(prompt, nitems);

	/* if there are no items to drawn, we are done */
	if (!nitems)
		goto done;

	/* background of items */
	y = prompt->h + prompt->separator;
	h = prompt->h * prompt->nitems;
	XSetForeground(dpy, dc.gc, dc.normal[ColorBG].pixel);
	XFillRectangle(dpy, prompt->pixmap, dc.gc, 0, y, prompt->w, h);

	/* draw items */
	for (i = 0; i < prompt->nitems; i++)
		drawitem(prompt, i, 0);

done:
	/* commit drawing */
	h = prompt->h * (prompt->nitems + 1) + prompt->separator;
	XCopyArea(dpy, prompt->pixmap, prompt->win, dc.gc, 0, 0, prompt->w, h, 0, 0);
}

/* return location of next utf8 rune in the given direction (+1 or -1) */
static size_t
nextrune(const char *text, size_t position, int inc)
{
	ssize_t n;

	for (n = position + inc; n + inc >= 0 && (text[n] & 0xc0) == 0x80; n += inc)
		;
	return n;
}

/* return bytes from beginning of text to nth utf8 rune to the right */
static size_t
runebytes(const char *text, size_t n)
{
	size_t ret;

	ret = 0;
	while (n-- > 0)
		ret += nextrune(text + ret, 0, 1);
	return ret;
}

/* return number of characters from beginning of text to nth byte to the right */
static size_t
runechars(const char *text, size_t n)
{
	size_t ret, i;

	ret = i = 0;
	while (i < n) {
		i += nextrune(text + i, 0, 1);
		ret++;
	}
	return ret;
}

/* move cursor to start (dir = -1) or end (dir = +1) of the word */
static size_t
movewordedge(const char *text, size_t pos, int dir)
{
	if (dir < 0) {
		while (pos > 0 && strchr(config.worddelimiters, text[nextrune(text, pos, -1)]))
			pos = nextrune(text, pos, -1);
		while (pos > 0 && !strchr(config.worddelimiters, text[nextrune(text, pos, -1)]))
			pos = nextrune(text, pos, -1);
	} else {
		while (text[pos] && strchr(config.worddelimiters, text[pos]))
			pos = nextrune(text, pos, +1);
		while (text[pos] && !strchr(config.worddelimiters, text[pos]))
			pos = nextrune(text, pos, +1);
	}
	return pos;
}

/* when this is called, the input method was closed */
static void
icdestroy(XIC xic, XPointer clientdata, XPointer calldata)
{
	(void)xic;
	(void)calldata;
	(void)clientdata;
	ic.xic = NULL;
}

/* start input method pre-editing */
static int
preeditstart(XIC xic, XPointer clientdata, XPointer calldata)
{
	(void)xic;
	(void)calldata;
	(void)clientdata;
	ic.composing = 1;
	ic.text = emalloc(INPUTSIZ);
	ic.text[0] = '\0';
	return INPUTSIZ;
}

/* end input method pre-editing */
static void
preeditdone(XIC xic, XPointer clientdata, XPointer calldata)
{
	(void)xic;
	(void)clientdata;
	(void)calldata;
	ic.composing = 0;
	free(ic.text);
}

/* draw input method pre-edit text */
static void
preeditdraw(XIC xic, XPointer clientdata, XPointer calldata)
{
	XIMPreeditDrawCallbackStruct *pdraw;
	struct Prompt *prompt;
	size_t beg, dellen, inslen, endlen;

	(void)xic;
	prompt = (struct Prompt *)clientdata;
	pdraw = (XIMPreeditDrawCallbackStruct *)calldata;
	if (!pdraw)
		return;

	/* we do not support wide characters */
	if (pdraw->text && pdraw->text->encoding_is_wchar == True) {
		warnx("warning: xprompt does not support wchar; use utf8!");
		return;
	}

	beg = runebytes(ic.text, pdraw->chg_first);
	dellen = runebytes(ic.text + beg, pdraw->chg_length);
	inslen = pdraw->text ? runebytes(pdraw->text->string.multi_byte, pdraw->text->length) : 0;
	endlen = 0;
	if (beg + dellen < strlen(ic.text))
		endlen = strlen(ic.text + beg + dellen);

	/* we cannot change text past the end of our pre-edit string */
	if (beg + dellen >= prompt->textsize || beg + inslen >= prompt->textsize)
		return;

	/* get space for text to be copied, and copy it */
	memmove(ic.text + beg + inslen, ic.text + beg + dellen, endlen + 1);
	if (pdraw->text && pdraw->text->length)
		memcpy(ic.text + beg, pdraw->text->string.multi_byte, inslen);
	(ic.text + beg + inslen + endlen)[0] = '\0';

	/* get caret position */
	ic.caret = runebytes(ic.text, pdraw->caret);

	drawinput(prompt, 1);
}

/* move caret on pre-edit text */
static void
preeditcaret(XIC xic, XPointer clientdata, XPointer calldata)
{
	XIMPreeditCaretCallbackStruct *pcaret;
	struct Prompt *prompt;

	(void)xic;
	prompt = (struct Prompt *)clientdata;
	pcaret = (XIMPreeditCaretCallbackStruct *)calldata;
	if (!pcaret)
		return;
	switch (pcaret->direction) {
	case XIMForwardChar:
		ic.caret = nextrune(ic.text, ic.caret, +1);
		break;
	case XIMBackwardChar:
		ic.caret = nextrune(ic.text, ic.caret, -1);
		break;
	case XIMForwardWord:
		ic.caret = movewordedge(ic.text, ic.caret, +1);
		break;
	case XIMBackwardWord:
		ic.caret = movewordedge(ic.text, ic.caret, -1);
		break;
	case XIMLineStart:
		ic.caret = 0;
		break;
	case XIMLineEnd:
		if (ic.text[ic.caret] != '\0')
			ic.caret = strlen(ic.text);
		break;
	case XIMAbsolutePosition:
		ic.caret = runebytes(ic.text, pcaret->position);
		break;
	case XIMDontChange:
		/* do nothing */
		break;
	case XIMCaretUp:
	case XIMCaretDown:
	case XIMNextLine:
	case XIMPreviousLine:
		/* not implemented */
		break;
	}
	pcaret->position = runechars(ic.text, ic.caret);
	drawinput(prompt, 1);
}

/* get number from *s into n, return 1 if error */
static int
getnum(const char **s, int *n)
{
	int retval;
	long num;
	char *endp;

	num = strtol(*s, &endp, 10);
	retval = (num > INT_MAX || num < 0 || endp == *s);
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

	if (*s != '+' && *s != '-') {
		/* get *w */
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

		/* get *h */
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
	}

	if (*s == '+' || *s == '-') {
		/* get *x */
		sign = (*s++ == '-') ? -1 : 1;
		if (getnum(&s, &n))
			goto error;
		*x = n * sign;
		if (*s != '+' && *s != '-')
			goto error;

		/* get *y */
		sign = (*s++ == '-') ? -1 : 1;
		if (getnum(&s, &n))
			goto error;
		*y = n * sign;
	}
	if (*s != '\0')
		goto error;

	return;

error:
	errx(1, "improper geometry specification %s", config.geometryspec);
}

/* allocate memory for the text input field */
static void
setpromptinput(struct Prompt *prompt)
{
	prompt->text = emalloc(INPUTSIZ);
	prompt->textsize = INPUTSIZ;
	prompt->text[0] = '\0';
	prompt->cursor = 0;
	prompt->select = 0;
	prompt->file = 0;
}

/* allocate memory for the undo list */
static void
setpromptundo(struct Prompt *prompt)
{
	/*
	 * the last entry of the undo list is a dummy entry with text
	 * set to NULL, we use it to know we are at the end of the list
	 */
	prompt->undo = emalloc(sizeof *prompt->undo);
	prompt->undo->text = NULL;
	prompt->undo->next = NULL;
	prompt->undo->prev = NULL;
	prompt->undocurr = NULL;
}

/* allocate memory for the item list displayed when completion is active */
static void
setpromptarray(struct Prompt *prompt)
{
	prompt->firstmatch = NULL;
	prompt->selitem = NULL;
	prompt->hoveritem = NULL;
	prompt->matchlist = NULL;
	prompt->maxitems = config.number_items;
	prompt->nitems = 0;
	prompt->itemarray = ecalloc(prompt->maxitems, sizeof *prompt->itemarray);
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
		errx(1, "Unknown gravity %s", config.gravityspec);

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
	XClassHint classh = {CLASSNAME, PROGNAME};

	/* create prompt window */
	swa.override_redirect = True;
	swa.background_pixel = dc.normal[ColorBG].pixel;
	swa.border_pixel = dc.border.pixel;
	prompt->win = XCreateWindow(dpy, parentwin,
	                            prompt->x, prompt->y, prompt->w, prompt->h, prompt->border,
	                            CopyFromParent, CopyFromParent, CopyFromParent,
	                            CWOverrideRedirect | CWBackPixel | CWBorderPixel,
	                            &swa);
	XSetClassHint(dpy, prompt->win, &classh);

	/* set window normal hints */
	sizeh.flags = PMaxSize | PMinSize;
	sizeh.min_width = sizeh.max_width = prompt->w;
	sizeh.min_height = sizeh.max_height = prompt->h;
	XSetWMNormalHints(dpy, prompt->win, &sizeh);
}

/* setup pixmap */
static void
setpromptpix(struct Prompt *prompt)
{
	int h, y;

	h = prompt->separator + prompt->h * (prompt->maxitems + 1);
	prompt->pixmap = XCreatePixmap(dpy, prompt->win, prompt->w, h,
	                               DefaultDepth(dpy, screen));
	prompt->draw = XftDrawCreate(dpy, prompt->pixmap, visual, colormap);

	/* draw the prompt string and update x to the end of it */
	if (prompt->promptstr) {
		XSetForeground(dpy, dc.gc, dc.normal[ColorBG].pixel);
		XFillRectangle(dpy, prompt->pixmap, dc.gc, 0, 0, prompt->promptw, prompt->h);
		drawtext(prompt->draw, &dc.normal[ColorFG], dc.pad, 0, prompt->h, prompt->promptstr, 0);
	}

	/* draw separator line */
	y = prompt->h + prompt->separator/2;
	XSetForeground(dpy, dc.gc, dc.separator.pixel);
	XDrawLine(dpy, prompt->pixmap, dc.gc, 0, y, prompt->w, y);
}

/* setup prompt input context */
static void
setpromptic(struct Prompt *prompt)
{
	XICCallback start, done, draw, caret, destroy;
	XVaNestedList preedit = NULL;
	XIMStyles *imstyles;
	XIMStyle preeditstyle;
	XIMStyle statusstyle;
	int i;

	/* open input method */
	if ((ic.xim = XOpenIM(dpy, NULL, NULL, NULL)) == NULL)
		errx(1, "XOpenIM: could not open input method");

	/* create callbacks for the input method */
	destroy.client_data = NULL;
	destroy.callback = (XICProc)icdestroy;

	/* set destroy callback for the input method */
	if (XSetIMValues(ic.xim, XNDestroyCallback, &destroy, NULL) != NULL)
		warnx("XSetIMValues: could not set input method values");

	/* get styles supported by input method */
	if (XGetIMValues(ic.xim, XNQueryInputStyle, &imstyles, NULL) != NULL)
		errx(1, "XGetIMValues: could not obtain input method values");

	/* check whether input method support on-the-spot pre-editing */
	preeditstyle = XIMPreeditNothing;
	statusstyle = XIMStatusNothing;
	for (i = 0; i < imstyles->count_styles; i++) {
		if (imstyles->supported_styles[i] & XIMPreeditCallbacks) {
			preeditstyle = XIMPreeditCallbacks;
			break;
		}
	}

	/* create callbacks for the input context */
	start.client_data = NULL;
	done.client_data = NULL;
	draw.client_data = (XPointer)prompt;
	caret.client_data = (XPointer)prompt;
	start.callback = (XICProc)preeditstart;
	done.callback = (XICProc)preeditdone;
	draw.callback = (XICProc)preeditdraw;
	caret.callback = (XICProc)preeditcaret;

	/* create list of values for input context */
	preedit = XVaCreateNestedList(0,
                                      XNPreeditStartCallback, &start,
                                      XNPreeditDoneCallback, &done,
                                      XNPreeditDrawCallback, &draw,
                                      XNPreeditCaretCallback, &caret,
                                      NULL);
	if (preedit == NULL)
		errx(1, "XVaCreateNestedList: could not create nested list");

	/* create input context */
	ic.xic = XCreateIC(ic.xim,
	                   XNInputStyle, preeditstyle | statusstyle,
	                   XNPreeditAttributes, preedit,
	                   XNClientWindow, prompt->win,
	                   XNDestroyCallback, &destroy,
	                   NULL);
	if (ic.xic == NULL)
		errx(1, "XCreateIC: could not obtain input method");

	/* get events the input method is interested in */
	if (XGetICValues(ic.xic, XNFilterEvents, &ic.eventmask, NULL))
		errx(1, "XGetICValues: could not obtain input context values");
	
	XFree(preedit);
}

/* select prompt window events */
static void
setpromptevents(struct Prompt *prompt, Window parentwin)
{
	Window r, p;    /* unused variables */
	Window *children;
	unsigned i, nchildren;

	XSelectInput(dpy, prompt->win,
	             ExposureMask | KeyPressMask | VisibilityChangeMask |
	             ButtonPressMask | PointerMotionMask | ic.eventmask);

	/* selecect focus event mask for the parent window */
	if (wflag) {
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
		if (XGrabKeyboard(dpy, root, True, GrabModeAsync,
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
		if (focuswin == win) {
			XSetICFocus(ic.xic);
			return;
		}
		XSetInputFocus(dpy, win, RevertToParent, CurrentTime);
		nanosleep(&ts, NULL);
	}
	errx(1, "cannot grab focus");
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
	while (prompt->cursor > 0 && strchr(config.worddelimiters, prompt->text[nextrune(prompt->text, prompt->cursor, -1)]))
		insert(prompt, NULL, nextrune(prompt->text, prompt->cursor, -1) - prompt->cursor);
	while (prompt->cursor > 0 && !strchr(config.worddelimiters, prompt->text[nextrune(prompt->text, prompt->cursor, -1)]))
		insert(prompt, NULL, nextrune(prompt->text, prompt->cursor, -1) - prompt->cursor);
}

/* insert selected item on prompt->text */
static void
insertselitem(struct Prompt *prompt)
{
	if (prompt->cursor && !strchr(config.worddelimiters, prompt->text[prompt->cursor - 1]))
		delword(prompt);
	if (!filecomp) {        /* If not completing a file, insert item as is */
		insert(prompt, prompt->selitem->text,
		       strlen(prompt->selitem->text));
	} else if (prompt->file > 0) {
		memmove(prompt->text + prompt->file, prompt->text + prompt->cursor, strlen(prompt->text + prompt->cursor) + 1);
		prompt->cursor = prompt->file;
		insert(prompt, prompt->selitem->text, strlen(prompt->selitem->text));
	}
}

/* add entry to undo list */
static void
addundo(struct Prompt *prompt, int editing)
{
	struct Undo *undo, *tmp;

	/* when adding a new entry to the undo list, delete the entries after current one */
	if (prompt->undocurr && prompt->undocurr->prev) {
		undo = prompt->undocurr->prev;
		while (undo) {
			tmp = undo;
			undo = undo->prev;
			free(tmp->text);
			free(tmp);
		}
		prompt->undocurr->prev = NULL;
		prompt->undo = prompt->undocurr;
	}

	/* add a new entry only if it differs from the one at the top of the list */
	if (!prompt->undo->text || strcmp(prompt->undo->text, prompt->text) != 0) {
		undo = emalloc(sizeof *undo);
		undo->text = estrdup(prompt->text);
		undo->next = prompt->undo;
		undo->prev = NULL;
		prompt->undo->prev = undo;
		prompt->undo = undo;

		/* if we are editing text, the current entry is the top one*/
		if (editing)
			prompt->undocurr = undo;
	}
}

/* we have been given the current selection, now insert it into input */
static void
paste(struct Prompt *prompt)
{
	char *p, *q;
	int di;             /* dummy variable */
	unsigned long dl;   /* dummy variable */
	Atom da;            /* dummy variable */

	if (XGetWindowProperty(dpy, prompt->win, atoms[Utf8String],
	                       0, prompt->textsize / 4 + 1, False,
	                       atoms[Utf8String], &da, &di, &dl, &dl, (unsigned char **)&p)
	    == Success && p) {
		addundo(prompt, 1);
		insert(prompt, p, (q = strchr(p, '\n')) ? q - p : (ssize_t)strlen(p));
		XFree(p);
	}
}

/* send SelectionNotify event to requestor window */
static void
copy(struct Prompt *prompt, XSelectionRequestEvent *ev)
{
	XSelectionEvent xselev;

	xselev.type = SelectionNotify;
	xselev.requestor = ev->requestor;
	xselev.selection = ev->selection;
	xselev.target = ev->target;
	xselev.time = ev->time;
	xselev.property = None;

	if (ev->property == None)
		ev->property = ev->target;

	if (ev->target == atoms[Targets]) {     /* respond with the supported type */
		XChangeProperty(dpy, ev->requestor, ev->property, XA_ATOM, 32,
		                PropModeReplace, (unsigned char *)&atoms[Utf8String], 1);
	} else if (ev->target == atoms[Utf8String] || ev->target == XA_STRING) {
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
					if (aflag && item->child == NULL && curritem != rootitem)
						return curritem;
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
	char buf[INPUTSIZ];
	size_t beg, len;
	size_t i;
	glob_t g;

	/* find filename to be completed */
	if (prompt->file > 0 && prompt->file <= prompt->cursor) {
		beg = prompt->file;
	} else if ((beg = prompt->cursor) > 0) {
		while (beg && !isspace(prompt->text[beg - 1])) {
			beg--;
		}
		prompt->file = beg;
	}
	len = prompt->cursor - beg;

	if (len >= INPUTSIZ - 2)  /* 2 for '*' and NUL */
		return NULL;

	buf[0] = '\0';
	strncat(buf, prompt->text + beg, len);
	strcat(buf, "*");

	glob(buf, 0, NULL, &g);

	previtem = NULL;
	for (i = 0; i < g.gl_pathc; i++) {
		item = allocitem(g.gl_pathv[i], NULL);
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

/* check whether item matches text */
static int
itemmatch(struct Item *item, const char *text, size_t textlen, int middle)
{
	const char *s;

	s = (dflag && item->description) ? item->description : item->text;
	while (*s) {
		if ((*fstrncmp)(s, text, textlen) == 0)
			return 1;
		if (middle) {
			s++;
		} else {
			while (*s && strchr(config.worddelimiters, *s) == NULL)
				s++;
			while (*s && strchr(config.worddelimiters, *s) != NULL)
				s++;
		}
	}

	return 0;
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

/* create list of matching items */
static void
getmatchlist(struct Prompt *prompt, struct Item *complist)
{
	struct Item *retitem = NULL;
	struct Item *previtem = NULL;
	struct Item *item = NULL;
	size_t beg, len;
	const char *text;

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
	text = prompt->text + beg;

	/* build list of matched items using the .nextmatch and .prevmatch pointers */
	for (item = complist; item; item = item->next) {
		if (itemmatch(item, text, len, 0)) {
			if (!retitem)
				retitem = item;
			item->prevmatch = previtem;
			if (previtem)
				previtem->nextmatch = item;
			previtem = item;
		}
	}
	/* now search for items that match in the middle of the item */
	for (item = complist; item; item = item->next) {
		if (!itemmatch(item, text, len, 0) && itemmatch(item, text, len, 1)) {
			if (!retitem)
				retitem = item;
			item->prevmatch = previtem;
			if (previtem)
				previtem->nextmatch = item;
			previtem = item;
		}
	}
	if (previtem)
		previtem->nextmatch = NULL;

	prompt->firstmatch = retitem;
	prompt->matchlist = retitem;
	prompt->selitem = retitem;
}

/* navigate through the list of matching items */
static void
navmatchlist(struct Prompt *prompt, int direction)
{
	struct Item *item;
	size_t i;

	if (!prompt->selitem)
		return;

	if (direction > 0 && prompt->selitem->nextmatch) {
		unsigned selnum;

		prompt->selitem = prompt->selitem->nextmatch;
		for (selnum = 0, item = prompt->matchlist; 
		     selnum < prompt->maxitems && item != prompt->selitem->prevmatch;
		     selnum++, item = item->nextmatch)
			;
		if (selnum + 1 >= prompt->maxitems) {
			for (i = 0, item = prompt->matchlist;
			     i < prompt->maxitems && item;
			     i++, item = item->nextmatch)
				;
			prompt->matchlist = (item) ? item : prompt->selitem;
		}
	} else if (direction < 0 && prompt->selitem->prevmatch) {
		prompt->selitem = prompt->selitem->prevmatch;
		if (prompt->selitem == prompt->matchlist->prevmatch) {
			for (i = 0, item = prompt->matchlist;
			     i < prompt->maxitems && item;
			     i++, item = item->prevmatch)
				;
			prompt->matchlist = (item) ? item : prompt->firstmatch;
		}
	}

	/* fill .itemarray */
	for (i = 0, item = prompt->matchlist;
	     i < prompt->maxitems && item;
	     i++, item = item->nextmatch)
		prompt->itemarray[i] = item;
	prompt->nitems = i;
}

/* zero variables for the list of matching items */
static void
delmatchlist(struct Prompt *prompt)
{
	prompt->matchlist = NULL;
	prompt->nitems = 0;
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
		if (!(state & ShiftMask) && ksym >= XK_a && ksym <= XK_z)
			return ctrl[LowerCase][ksym - XK_a];
		if ((state & ShiftMask) && ksym >= XK_a && ksym <= XK_z)
			return ctrl[UpperCase][ksym - XK_a];
		if (ksym >= XK_A && ksym <= XK_Z)
			return ctrl[UpperCase][ksym - XK_A];
		return CTRLNOTHING;
	}

	return INSERT;
}

/* copy entry from undo list into text */
static void
undo(struct Prompt *prompt)
{
	if (prompt->undocurr) {
		if (prompt->undocurr->text == NULL) {
			return;
		}
		if (strcmp(prompt->undocurr->text, prompt->text) == 0)
			prompt->undocurr = prompt->undocurr->next;
	}
	if (prompt->undocurr) {
		insert(prompt, NULL, 0 - prompt->cursor);
		insert(prompt, prompt->undocurr->text, strlen(prompt->undocurr->text));
		prompt->undocurr = prompt->undocurr->next;
	}
}

/* copy entry from undo list into text */
static void
redo(struct Prompt *prompt)
{
	if (prompt->undocurr && prompt->undocurr->prev)
		prompt->undocurr = prompt->undocurr->prev;
	if (prompt->undocurr && prompt->undocurr->prev && strcmp(prompt->undocurr->text, prompt->text) == 0)
		prompt->undocurr = prompt->undocurr->prev;
	if (prompt->undocurr) {
		insert(prompt, NULL, 0 - prompt->cursor);
		insert(prompt, prompt->undocurr->text, strlen(prompt->undocurr->text));
	}
}

/* handle key press */
static enum Press_ret
keypress(struct Prompt *prompt, struct Item *rootitem, struct History *hist, XKeyEvent *ev)
{
	static struct Item *complist;   /* list of possible completions */
	static char buf[INPUTSIZ];
	static enum Ctrl prevoperation = CTRLNOTHING;
	enum Ctrl operation;
	char *s;
	int len;
	int dir;
	KeySym ksym;
	Status status;

	len = XmbLookupString(ic.xic, ev, buf, sizeof buf, &ksym, &status);
	switch (status) {
	default: /* XLookupNone, XBufferOverflow */
		return Nop;
	case XLookupChars:
		goto insert;
	case XLookupKeySym:
	case XLookupBoth:
		break;
	}

	operation = getoperation(ksym, ev->state);
	if (operation == INSERT && (iscntrl(*buf) || *buf == '\0'))
		return Nop;
	if (ISUNDO(operation) && ISEDITING(prevoperation))
		addundo(prompt, 0);
	if (ISEDITING(operation) && operation != prevoperation)
		addundo(prompt, 1);
	prevoperation = operation;
	switch (operation) {
	case CTRLPASTE:
		XConvertSelection(dpy, atoms[Clipboard], atoms[Utf8String], atoms[Utf8String], prompt->win, CurrentTime);
		return Nop;
	case CTRLCOPY:
		XSetSelectionOwner(dpy, atoms[Clipboard], prompt->win, CurrentTime);
		return Nop;
	case CTRLCANCEL:
		if (sflag || !prompt->matchlist || prompt->text[0] == '\0')
			return Esc;
		delmatchlist(prompt);
		if (filecomp)
			cleanitem(complist);
		break;
	case CTRLENTER:
		if (prompt->matchlist)
			insertselitem(prompt);
		if (sflag || !prompt->matchlist) {
			puts(prompt->text);
			return Enter;
		}
		delmatchlist(prompt);
		break;
	case CTRLPREV:
		/* FALLTHROUGH */
	case CTRLNEXT:
tab:
		if (!prompt->matchlist) {
			complist = getcomplist(prompt, rootitem);
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
		if (!prompt->matchlist) {
			getmatchlist(prompt, complist);
			navmatchlist(prompt, 0);
		} else if (operation == CTRLNEXT) {
			navmatchlist(prompt, 1);
		} else if (operation == CTRLPREV) {
			navmatchlist(prompt, -1);
		}
		break;
	case CTRLPGUP:
	case CTRLPGDOWN:
		/* TODO */
		return Nop;
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
			return Nop;
		s = navhist(hist, dir);
		if (s) {
			insert(prompt, NULL, 0 - prompt->cursor);
			insert(prompt, s, strlen(s));
		}
		delmatchlist(prompt);
		break;
	case CTRLSELLEFT:
	case CTRLLEFT:
		if (prompt->cursor > 0)
			prompt->cursor = nextrune(prompt->text, prompt->cursor, -1);
		else
			return Nop;
		break;
	case CTRLSELRIGHT:
	case CTRLRIGHT:
		if (prompt->text[prompt->cursor] != '\0')
			prompt->cursor = nextrune(prompt->text, prompt->cursor, +1);
		else
			return Nop;
		break;
	case CTRLSELWLEFT:
	case CTRLWLEFT:
		prompt->cursor = movewordedge(prompt->text, prompt->cursor, -1);
		break;
	case CTRLSELWRIGHT:
	case CTRLWRIGHT:
		prompt->cursor = movewordedge(prompt->text, prompt->cursor, +1);
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
				return Nop;
			prompt->cursor = nextrune(prompt->text, prompt->cursor, +1);
		}
		if (prompt->cursor == 0)
			return Nop;
		insert(prompt, NULL, nextrune(prompt->text, prompt->cursor, -1) - prompt->cursor);
		break;
	case CTRLDELWORD:
		delword(prompt);
		break;
	case CTRLUNDO:
		undo(prompt);
		break;
	case CTRLREDO:
		redo(prompt);
		break;
	case CTRLNOTHING:
		return Nop;
	case INSERT:
insert:
		operation = INSERT;
		if (iscntrl(*buf) || *buf == '\0')
			return Nop;
		delselection(prompt);
		insert(prompt, buf, len);
		if (cflag)
			goto tab;
		break;
	}

	if (ISMOTION(operation)) {          /* moving cursor while selecting */
		prompt->select = prompt->cursor;
		delmatchlist(prompt);
		return DrawPrompt;
	}
	if (ISSELECTION(operation)) {       /* moving cursor while selecting */
		XSetSelectionOwner(dpy, XA_PRIMARY, prompt->win, CurrentTime);
		return DrawInput;
	}
	if (ISEDITING(operation) || ISUNDO(operation)) {
		if (prompt->matchlist && filecomp) {   /* if in a file completion, cancel it */
			cleanitem(complist);
			filecomp = 0;
			delmatchlist(prompt);
			return DrawPrompt;
		} else if (prompt->matchlist) {        /* if in regular completion, rematch */
			complist = getcomplist(prompt, rootitem);
			if (complist == NULL)
				return DrawPrompt;
			getmatchlist(prompt, complist);
			if (!prompt->matchlist)
				delmatchlist(prompt);
			else
				navmatchlist(prompt, 0);
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
	const char *next;
	int textwidth;

	while (*s) {
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
static struct Item *
getitem(struct Prompt *prompt, int y)
{
	struct Item *item;
	size_t i, n;

	y -= prompt->h + prompt->separator;
	y = MAX(y, 0);
	n = y / prompt->h;

	for (i = 0, item = prompt->matchlist;
	     i < n && item->nextmatch;
	     i++, item = item->nextmatch)
		;

	return item;
}

/* handle button press */
static enum Press_ret
buttonpress(struct Prompt *prompt, XButtonEvent *ev)
{
	static int word = 0;    /* whether a word was selected by double click */
	static Time lasttime = 0;
	size_t curpos;

	if (ic.composing)       /* we ignore mouse events when composing */
		return Nop;
	switch (ev->button) {
	case Button2:                               /* middle click paste */
		delselection(prompt);
		XConvertSelection(dpy, XA_PRIMARY, atoms[Utf8String], atoms[Utf8String], prompt->win, CurrentTime);
		return Nop;
	case Button1:
		if (ev->y < 0 || ev->x < 0)
			return Nop;
		if (ev->y <= prompt->h) {
			curpos = getcurpos(prompt, ev->x);
			if (word && ev->time - lasttime < DOUBLECLICK) {
				prompt->cursor = 0;
				if (prompt->text[prompt->cursor] != '\0')
					prompt->select = strlen(prompt->text);
				word = 0;
			} else if (ev->time - lasttime < DOUBLECLICK) {
				prompt->cursor = movewordedge(prompt->text, curpos, -1);
				prompt->select = movewordedge(prompt->text, curpos, +1);
				word = 1;
			} else {
				prompt->select = prompt->cursor = curpos;
				word = 0;
			}
			lasttime = ev->time;
			return DrawInput;
		} else if (ev->y > prompt->h + prompt->separator) {
			prompt->selitem = getitem(prompt, ev->y);
			insertselitem(prompt);
			if (sflag) {
				puts(prompt->text);
				return Enter;
			}
			delmatchlist(prompt);
			return DrawPrompt;
		}
		return Nop;
	default:
		return Nop;
	}

	return Nop;
}

/* handle button motion X event */
static enum Press_ret
buttonmotion(struct Prompt *prompt, XMotionEvent *ev)
{
	size_t prevselect, prevcursor;

	prevselect = prompt->select;
	prevcursor = prompt->cursor;

	if (ic.composing)       /* we ignore mouse events when composing */
		return Nop;
	if (ev->y >= 0 && ev->y <= prompt->h)
		prompt->select = getcurpos(prompt, ev->x);
	else if (ev->y < 0)
		prompt->select = 0;
	else if (prompt->text[prompt->cursor] != '\0')
		prompt->cursor = strlen(prompt->text);
	else
		return Nop;

	/* if the selection didn't change there's no need to redraw input */
	if (prompt->select == prevselect && prompt->cursor == prevcursor)
		return Nop;

	return DrawInput;
}

/* handle pointer motion X event */
static enum Press_ret
pointermotion(struct Prompt *prompt, XMotionEvent *ev)
{
	static int intext = 0;
	struct Item *prevhover;
	int miny, maxy;

	if (ev->y < prompt->h && !intext) {
		XDefineCursor(dpy, prompt->win, cursor);
		intext = 1;
	} else if (ev->y >= prompt->h && intext) {
		XUndefineCursor(dpy, prompt->win);
		intext = 0;
	}
	if (ic.composing)       /* we ignore mouse events when composing */
		return Nop;
	miny = prompt->h + prompt->separator;
	maxy = miny + prompt->h * prompt->nitems;
	prevhover = prompt->hoveritem;
	if (ev->y < miny || ev->y >= maxy)
		prompt->hoveritem = NULL;
	else
		prompt->hoveritem = getitem(prompt, ev->y);

	return (prevhover != prompt->hoveritem) ? DrawPrompt : Nop;
}

/* save history in history file */
static void
savehist(struct Prompt *prompt, struct History *hist)
{
	int diff;   /* whether the last history entry differs from prompt->text */
	int fd;

	if (hflag == 0)
		return;

	fd = fileno(hist->fp);
	ftruncate(fd, 0);

	if (!hist->size) {
		fprintf(hist->fp, "%s\n", prompt->text);
		return;
	}

	diff = strcmp(hist->entries[hist->size-1], prompt->text);

	hist->index = (diff && hist->size == config.histsize) ? 1 : 0;

	while (hist->index < hist->size)
		fprintf(hist->fp, "%s\n", hist->entries[hist->index++]);

	if (diff)
		fprintf(hist->fp, "%s\n", prompt->text);
}

/* run event loop */
static void
run(struct Prompt *prompt, struct Item *rootitem, struct History *hist)
{
	XEvent ev;
	enum Press_ret retval = Nop;

	XMapRaised(dpy, prompt->win);
	grabfocus(prompt->win);
	while (!XNextEvent(dpy, &ev)) {
		if (XFilterEvent(&ev, None))
			continue;
		retval = Nop;
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
			if (ev.xselection.property != atoms[Utf8String])
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
			return;
		case Enter:
			savehist(prompt, hist);
			return;
		case DrawInput:
			drawinput(prompt, 1);
			break;
		case DrawPrompt:
			drawprompt(prompt);
			break;
		default:
			break;
		}
	}
	/* UNREACHABLE */
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

/* free undo list */
static void
cleanundo(struct Undo *undo)
{
	struct Undo *tmp;

	while (undo) {
		tmp = undo;
		undo = undo->next;
		free(tmp->text);
		free(tmp);
	}
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

/* clean up draw context */
static void
cleandc(void)
{
	XftColorFree(dpy, visual, colormap, &dc.hover[ColorBG]);
	XftColorFree(dpy, visual, colormap, &dc.hover[ColorFG]);
	XftColorFree(dpy, visual, colormap, &dc.hover[ColorCM]);
	XftColorFree(dpy, visual, colormap, &dc.normal[ColorBG]);
	XftColorFree(dpy, visual, colormap, &dc.normal[ColorFG]);
	XftColorFree(dpy, visual, colormap, &dc.normal[ColorCM]);
	XftColorFree(dpy, visual, colormap, &dc.selected[ColorBG]);
	XftColorFree(dpy, visual, colormap, &dc.selected[ColorFG]);
	XftColorFree(dpy, visual, colormap, &dc.selected[ColorCM]);
	XftColorFree(dpy, visual, colormap, &dc.separator);
	XftColorFree(dpy, visual, colormap, &dc.border);
	XFreeGC(dpy, dc.gc);
}

/* clean up input context */
static void
cleanic(void)
{
	XDestroyIC(ic.xic);
	XCloseIM(ic.xim);
}

/* clean up cursor */
static void
cleancursor(void)
{
	XFreeCursor(dpy, cursor);
}

/* xprompt: a dmenu rip-off with contextual completion */
int
main(int argc, char *argv[])
{
	struct History hist = {.entries = NULL, .index = 0, .size = 0};
	struct Prompt prompt;
	struct Item *rootitem;
	Window parentwin;

	/* set locale and modifiers */
	if (!setlocale(LC_CTYPE, "") || !XSupportsLocale())
		warnx("warning: no locale support");
	if (!XSetLocaleModifiers(""))
		warnx("warning: could not set locale modifiers");

	/* open connection to server and set X variables */
	if ((dpy = XOpenDisplay(NULL)) == NULL)
		errx(1, "cannot open display");
	screen = DefaultScreen(dpy);
	visual = DefaultVisual(dpy, screen);
	root = RootWindow(dpy, screen);
	colormap = DefaultColormap(dpy, screen);

	/* initialize resource manager database */
	XrmInitialize();
	if ((xrm = XResourceManagerString(dpy)) != NULL)
		xdb = XrmGetStringDatabase(xrm);

	/* get configuration */
	parentwin = root;
	getresources();
	getenvironment();
	prompt.promptstr = getoptions(argc, argv, &parentwin);

	/* init */
	initatoms();
	initmonitor();
	initctrl();
	initdc();
	initcursor();

	/* setup prompt */
	setpromptinput(&prompt);
	setpromptundo(&prompt);
	setpromptarray(&prompt);
	setpromptgeom(&prompt, parentwin);
	setpromptwin(&prompt, parentwin);
	setpromptpix(&prompt);
	setpromptic(&prompt);
	setpromptevents(&prompt, parentwin);

	/* initiate item list */
	rootitem = parsestdin(stdin);

	/* open config.histfile and load history */
	if (config.histfile != NULL && *config.histfile != '\0') {
		if ((hist.fp = fopen(config.histfile, "a+")) == NULL)
			warn("%s", config.histfile);
		else {
			loadhist(hist.fp, &hist);
			if (!hflag)
				fclose(hist.fp);
		}
	}

	/* grab input */
	if (!wflag)
		grabkeyboard();

	/* run event loop; and, if run return nonzero, save the history */
	run(&prompt, rootitem, &hist);

	/* freeing stuff */
	if (hflag)
		fclose(hist.fp);
	cleanitem(rootitem);
	cleanhist(&hist);
	cleanundo(prompt.undo);
	cleanprompt(&prompt);
	cleandc();
	cleanic();
	cleancursor();
	XrmDestroyDatabase(xdb);
	XCloseDisplay(dpy);

	return 0;
}
