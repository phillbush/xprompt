static struct Config config = {
	/* word delimiters */
	.worddelimiters = " .,/:;\\<>'[]{}()&$?!",

	/* CTRL key bindings */
	.xpromptctrl = "vcmpn::::aebf::ukhdwAEBF::zZ:",

	/* font */
	.font = "monospace:size=9",

	/* colors */
	.background_color = "#000000",
	.foreground_color = "#FFFFFF",
	.description_color = "#555753",
	.hoverbackground_color = "#121212",
	.hoverforeground_color = "#FFFFFF",
	.hoverdescription_color = "#555753",
	.selbackground_color = "#3465A4",
	.selforeground_color = "#FFFFFF",
	.seldescription_color = "#C5C8C6",
	.separator_color = "#555753",

	/* Default geometry */
	.geometryspec = "0x0",
	.number_items = 8,      /* number of items listed for completion */
	.separator_pixels = 3,  /* space around separator */

	/* history */
	.histfile = NULL,       /* keep NULL to set history via command-line options */
	.histsize = 15,         /* history size */

	/* if nonzero, indent items on dropdown menu (as in dmenu) */
	.indent = 0
};
