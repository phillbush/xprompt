<p align="center">
  <img src="/demo.gif", title="demo"/>
</p>

# XPrompt

XPrompt is a prompt for X.
XPrompt features a text input field where the user can type in a text
subject to tab-completion.

See the [manual](https://github.com/phillbush/xprompt/wiki) for more information on how xprompt works.

When the user presses Return, the typed text is printed to the stdout.
When the user presses Tab, a dropdown menu appears showing the items
from the stdin that matches the word to be completed.  It can read a
list of newline-separated items from stdin.

XPrompt is conceptually equal to Suckless' dmenu, but they have several
differences.  Namely, the following are features of xprompt that are not
in dmenu:

* Text selection (both via mouse or via Shift+Arrow).
* Mouse support.
* Configurable keybindings (via the `XPROMPTCTRL` environment variable).
* History (via the `XPROMPTHISTFILE` and `XPROMPTHISTSIZE` environment
  variables).
* Contextual completion (the text to complete depends on what you have
  typed in).
* File name completion (with `-f` option).
* Each completion item can be followed by a description.
* Configurable size and position (via X resources).
* Configurable colors and fonts (via X resources).
* Items on the dropdown list can have descriptions attached to them
  (just separate the item from its description with a tab).
* The string that should be piped out into stdout can be hidden on the
  dropdown list (with `-d` option).
* The position of the xprompt on the screen or on the parent window can
  be set with the `-g` and the `-G` options.
* Support for input methods with on-the-spot pre-editing.
* Undo/redo (by default bound to Ctrl-Z and Ctrl-Shift-Z).
* Copy/paste both via mouse and keybindings.


## Files

The files are:

* `./README:`      This file.
* `./Makefile:`    The makefile.
* `./demo.gif:`    A gif demonstrating how XPrompt works.
* `./config.h:`    The hardcoded default configuration for XPrompt.
* `./config.mk:`   The setup for the makefile.
* `./xprompt.1:`   The manual file (man page) for XPrompt.
* `./xprompt.c:`   The source code of XPrompt.


## Installation

First, edit ./config.mk to match your local setup.

In order to build XPrompt you need the Xlib, Xft and Xinerama header files.
The default configuration for XPrompt is specified in the file `config.h`,
you can edit it, but most configuration can be changed at runtime via
X resources, environment variables and/or command-line arguments.
Enter the following command to build XPrompt.  This command creates the
binary file `./xprompt`.

	make

By default, XPrompt is installed into the `/usr/local` prefix.  Enter the
following command to install XPrompt (if necessary as root).  This command
installs the binary file `./xprompt` into the `${PREFIX}/bin/` directory, and
the manual file `./xprompt.1` into `${MANPREFIX}/man1/` directory.

	make install


## Running XPrompt


### Use case 1: contextual completion

Have you ever used dmenu to select a command that then spawned another
dmenu instance to select the argument of that command?

For example, you have a dmenu to select among the commands:
`man` that opens a manpage on zathura,
`open` that opens a file,
and `git` that do some operation on your dotfile repository.
You open dmenu and select `git`.
But that is not enough, you have to specify a subcommand to that command.
Then, it opens another dmenu, with the items `commit`, `add`, `rm`, etc.

Wouldn't it be easier to do all this in a single dmenu instance
rather than launching a dmenu from another dmenu?  
Well, `xprompt` does it. It is called contextual completion.

Contextual completion works like this:  
What xprompt lists for completion depends on what you have written:
xprompt shows a list of commands and then, after you have selected a command,
it shows a list of its options or subcommands.

You can see that in the gif, first it shows a list of commands (`git`,
`man` and `open`) and after selecting `git` it show a list of some git
options (`add`, `rm`, `mv`, and `commit`).

For achieving contextual completion you have to indent the options after
the command with tabs.  For example:

```
cat << EOF | xprompt | my_script
git
\t add
\t mv
\t rm
\t commit
man
open
EOF
```


### Use case 2: listing descriptions

Have you ever written a wrapper script for dmenu where you had to
generate input for dmenu and them decode this input because the printed
text is not in the format you want?

Let's do an example:
you want to select a window with dmenu and pipe it into a program
`winman` that manages a window in some way.  You have firefox, xterm and
kdenlive open. So you can do this:

```
cat << EOF | dmenu | xargs winman
firefox
xterm
kdenlive
EOF
```

But to use `winman`, you need the window id, not the window title.
So how do you get the window id? There are two solutions:
print the window id along with the window title,
or write a script that transform window titles in window ids.

Following the first solution, you do something like this:

```
cat << EOF | dmenu | cut -d: -f1 | xargs winman
0x00001: firefox
0x00002: xterm
0x00003: kdenlive
EOF
```

Following the second solution, you write a script called `title_to_id`
that loops over all windows and check if the title it got on stdin is
the same as the current window and print its id accordingly:

```
cat << EOF | dmenu | title_to_id | xargs winman
firefox
xterm
kdenlive
EOF
```

xprompt does something different:  
separating the command (the thing you want to go to the pipe)
from the description (the thing you want to be listed) with a tab,
and calling xprompt with the `-d` option,
you list all window titles and print the id of the selected one to
stdout (`\t` here represents a tab):

```
cat << EOF | xprompt -d | xargs winman
0x00001\tfirefox
0x00002\txterm
0x00003\tkdenlive
EOF
```

The window ids will not be listed by xprompt,
but if you select for example `firefox`,
the id of the firefox window will be piped to `xargs winman`.

Without the `-d` option, both the command and the description are listed,
side by side, as you can see in the gif.


### Specifying xprompt geometry

Xprompt by default opens on the top of the screen you are using.
But it can open in other locations by using the `-G` and `-g` options.

With the `-G` option you specify the gravity, that is, where in the
screen xprompt should be displayed.
For example, `-G N` opens Xprompt in the North (top center) of the screen,
`-G SE` opens Xprompt in the South East (bottom right corner) of the screen,
and `-G C` opens Xprompt in the Center of the screen.

With the `-g` option you specify the geometry, that is, the size and position of xprompt.
For example, `-g 800x20` makes Xpropmt 800 pixels wide and 20 pixels tall;
`-g 800x20-100+200` makes Xprompt have the same size as before,
but places it 100 pixelxs to the left and 200 pixels down.
`-g 80%x20` makes Xprompt's width be 80% of the screen width.


### Other options

Xprompt also features other options,
see the [manual](https://github.com/phillbush/xprompt/wiki) for a complete listing of all options.

* `-f`: Enables filename completion.
* `-h`: Sets the file for history.
* `-i`: Makes Xprompt case insensitive.
* `-p`: Enable password mode (typed text is not echoed in the input field).
* `-s`: Makes a single Enter or Esc keypresses exit xprompt.
* `-w`: Specify a window where Xprompt should be embedded.

Xprompt also uses some environment variables.

* `XPROMPTHISTFILE`: File for storing history.
* `XPROMPTHISTSIZE`: Size of the history file.
* `XPROMPTCTRL`:     Xprompt key bindings.
* `WORDDELIMITERS`:  A string of characters that delimits words.
