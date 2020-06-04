/* word delimiters */
static const char *worddelimiters = " /";

/* CTRL key bindings */
static const char *xpromptctrl = "vcmpn::ae::bf::ukhdw";

/* font */
static const char *font = "monospace:size=9";    /* for regular items */

/* colors */
static const char *background_color = "#FFFFFF";
static const char *foreground_color = "#2E3436";
static const char *selbackground_color = "#3584E4";
static const char *selforeground_color = "#FFFFFF";
static const char *separator_color = "#E6E6E6";
static const char *border_color = "#CDC7C2";

/* Default geometry */
static const char *geometryspec = "0x20+0+0";
static const char *gravityspec = "N";
static unsigned number_items = 8;   /* number of items listed for completion */
static int border_pixels = 2;       /* prompt border */
static int separator_pixels = 3;    /* space around separator */

/* history size */
static size_t histsize = 15;
