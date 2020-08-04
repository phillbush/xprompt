static struct Config config = {
	/* word delimiters */
	.worddelimiters = " .,/:;\\<>'[]{}()&$?!",

	/* CTRL key bindings */
	.xpromptctrl = "vcmpn::::aebf::ukhdwAEBF::",

	/* font */
	.font = "monospace:size=9",

	/* colors */
	.background_color = "#000000",
	.foreground_color = "#FFFFFF",
	.selbackground_color = "#3584E4",
	.selforeground_color = "#FFFFFF",
	.separator_color = "#555753",
	.border_color = "#555753",

	/* Default geometry */
	.geometryspec = "0x20+0+0",
	.gravityspec = "N",
	.number_items = 8,          /* number of items listed for completion */
	.border_pixels = 2,         /* prompt border */
	.separator_pixels = 3,      /* space around separator */

	/* history size */
	.histsize = 15
};
