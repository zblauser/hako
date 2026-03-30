# 箱 HAKO
## Overview
A minimalistic modal text environment, containing your code without distraction.<br>

<p align="center">
	<img src="./assets/hako-logo.jpeg" alt="Hako logo" width="200" /><br>
</p><br>
<table align="center">
	<tr>
		<td align="center">
			<img src="./assets/hako-explorer.jpeg" alt="Explorer pane" width="200"/><br/>
			[紙 Kami Explorer]
		</td>
		<td align="center">
			<img src="./assets/hako-split.jpeg" alt="Split window system" width="200"/><br/>
			[Split Window Panes]
		</td>
	</tr>
	<tr>
		<td align="center">
			<img src="./assets/hako-search.jpeg" alt="Help system" width="200"/><br/>
			[Search/Replace]
		</td>
		<td align="center">
			<img src="./assets/hako-ai.jpeg" alt="AI Integration" width="200"/><br/>
		[角 Kaku AI Assistant]
		</td>
	</tr>
</table><br>

- **Modal Editing**: Vi-inspired normal, insert, visual, and visual-line modes
- **Multi-Pane Support**: Split windows horizontally and vertically
- **File Explorer**: 紙 Kami — built-in directory navigation with hidden file toggle
- **AI Assistant**: 角 Kaku — panel scaffolding for code assistance (integration coming)
- **Syntax Highlighting**: Support for 40+ programming languages
- **Undo/Redo**: Intelligent undo blocks with configurable history depth
- **Visual Selection**: Character and line-based selection with yank/delete operations
- **Clipboard**: Bracketed paste support, system clipboard integration (Ctrl-C/Ctrl-V)
- **Fast Search**: Incremental search with next/previous navigation
- **Line Numbers**: Absolute or relative numbering, dynamic width
- **Mouse Support**: Click to position, scroll wheel cursor movement
- **Customizable**: Configuration via `.hakorc` file with theme presets

## Launch Hako
Compile
```
# Grab a C compiler (ex. gcc)
gcc hako.c -o hako
```
Run
```
# If not installed, from file path
./hako or ./hako [filename]

# If installed, from anywhere
hako or hako [filename]
```
"Install"
```
# From file path (Mac)
cp hako /usr/local/bin/hako 

# From file path (Linux)
cp hako /usr/local/bin/hako
```

## Key Bindings

### Normal Mode

|Key      |Action                 |
|---------|-----------------------|
|`i`      |Enter insert mode      |
|`I`      |Insert at first non-blank|
|`a`      |Append after cursor    |
|`A`      |Append at end of line  |
|`o`      |Open line below        |
|`O`      |Open line above        |
|`v`      |Visual mode (character)|
|`V`      |Visual mode (line)     |
|`h,j,k,l`|Navigate (←↓↑→)        |
|`w,b`    |Next/previous word     |
|`0,$`    |Start/end of line      |
|`gg,G`   |Top/bottom of file     |
|`dd`     |Delete line (yanks)    |
|`D`      |Delete to end of line  |
|`x`      |Delete character       |
|`C`      |Change to end of line  |
|`yy`     |Yank (copy) line       |
|`p,P`    |Paste after/before     |
|`u`      |Undo                   |
|`Ctrl-R` |Redo                   |
|`r`      |Replace character      |
|`J`      |Join lines             |
|`/`      |Search                 |
|`n,N`    |Next/previous match    |
|`Ctrl-C` |Copy to system clipboard|
|`Ctrl-V` |Paste from system clipboard|
|`:w`     |Save                   |
|`:e [filename]`     |Open file   |
|`:q`     |Quit                   |
|`:wq`    |Save and quit          |
|`Ctrl-F` |Page forward           |
|`Ctrl-B` |Page backward          |

### Visual Mode

|Key       |Action                 |
|----------|-----------------------|
|`h,j,k,l` |Extend selection      |
|`w,b`     |Word-wise selection    |
|`0,$`     |Select to start/end   |
|`gg,G`    |Select to top/bottom   |
|`Ctrl-F`  |Page forward selection |
|`Ctrl-B`  |Page backward selection|
|`y`       |Yank selection         |
|`d,x`     |Delete selection       |
|`c`       |Change selection       |
|`Ctrl-C`  |Copy to system clipboard|
|`Esc`     |Exit visual mode       |

### Window Management

|Key       |Action          |
|----------|----------------|
|`Ctrl-W s`|Split horizontal|
|`Ctrl-W v`|Split vertical  |
|`Ctrl-W w`|Switch pane     |
|`Ctrl-W c`|Close pane      |

### File Explorer (紙 Kami)

|Key        |Action             |
|-----------|-------------------|
|`:e,:explorer`|Toggle explorer |
|`j,k`      |Navigate files     |
|`h,l`      |Parent/open        |
|`g,G`      |Top/bottom of list |
|`.`        |Toggle hidden files|
|`r`        |Refresh            |
|`q,Esc`    |Close explorer     |

### AI Assistant (角 Kaku)

|Key        |Action             |
|-----------|-------------------|
|`:ai`      |Toggle AI panel    |
|`i`        |Enter prompt mode  |
|`v`        |Visual select      |
|`j,k`      |Navigate history   |
|`Esc`      |Back to normal     |
|`:clear`   |Clear history      |

## Configuration

Use supplied config, or generate one with `:config` inside the editor. HAKO looks for `.hakorc` in the current directory first, then your home directory.

```
# Example .hakorc
tab_stop=4
use_tabs=1
show_line_numbers=2
auto_indent=1
smart_indent=1
mouse_enabled=1
theme=dark
explorer_width=30
```

Run `:config` to generate a fully documented `.hakorc` with all options.

## Supported Languages

HAKO provides syntax color for 40+ languages, some of which include:

**Systems**: C, C++, Rust, Go, Zig, Assembly  
**Scripting**: Python, Ruby, Perl, Lua, Shell, PHP  
**Web**: JavaScript, TypeScript, HTML, CSS, JSON  
**JVM**: Java, Kotlin, Scala, Clojure  
**Functional**: Haskell, OCaml, F#, Elixir  
**Modern**: Swift, Dart, Julia, Nim  
**Config**: YAML, TOML, Dockerfile, Makefile

## Change Log

### v0.0.8 (Latest)<br>
Major stability and usability update

Features
- Bracketed paste mode support — no more staircase indentation on paste
- Rapid-input paste detection fallback for terminals without bracketed paste
- System clipboard integration (Ctrl-C to copy, Ctrl-V to paste)
- New vim motions: I, a, A, o, O (insert variants), x (delete char), D (delete to EOL), C (change to EOL)
- Visual mode navigation: w/b, 0/$, gg/G, Ctrl-F/Ctrl-B for fast selection
- dd now yanks the deleted line into the paste buffer (vim behavior)
- Scroll wheel moves cursor naturally (like j/k) instead of jumping the viewport
- Mouse click works in insert mode without producing artifacts
- Dynamic line number width — scales for 10,000+ line files
- Improved terminal type detection (Apple Terminal 256-color support)
- Improved ANSI color fallback for basic terminals (visual selection visible everywhere)

Bug Fixes
- Fixed file save writing to wrong location when opened from a path
- Fixed pane tree memory leaks on close and exit
- Fixed right panel (AI) not being freed on cleanup
- Reduced screen flicker via buffer optimization (geometric growth in append buffer)
- Consistent naming across codebase (紙 Kami = explorer, 角 Kaku = AI)
- Explicit smart_indent initialization

### Previous Versions
<details>
<summary>Previous Changes</summary>

***v0.0.7***<br>
Huge update to the previous version
- New splash screen
- Complete visual mode implementation (v/V)
- Full copy/paste system with yank buffer
- Additional vim motions (w/b, 0/$, r, J)
- Screen splitting
- 紙 Kami explorer panel
- 角 Kaku AI assistant panel (scaffolding)
- Enhanced help system
- Improved terminal cleanup
- Themes in .hakorc
- Config generation via :config command
- Mouse support with click-to-position and scroll wheel

***v0.0.6***<br>
Features & Quality of Life
- Full visual mode implementation (v for character, V for line selection)
- Complete copy/paste system with yank buffer (y to copy, p/P to paste)
- Additional vim motions: w/b (word movement), 0/$ (line start/end)
- Character replacement with r, line joining with J
- Enhanced help system (:help in editor, -h/--help from command line)
- New file creation - editing non-existent files creates them on save
- Improved terminal cleanup on exit

Bug Fixes
- Fixed undo/redo segfault when undoing paste operations
- Resolved terminal color persistence after exit
- Corrected visual selection boundary handling
- Fixed edge cases with empty line selections

***v0.0.5***<br>
Features & Quality of Life
- While I'm not looking to create another neovim, there are many features of vim I do enjoy
- More vi-like functionality with undo (u in normal)/redo (ctrl + r in normal) blocks
- Undo blocks function with time, action, and mode based boundaries
- Configurable undo levels, located in .hakorc (default=100)
- Options for number and relative number mode, replacing (ex. "~" with "1") available in .hakorc
- Number mode currently supports up 4 characters (ex. 9999 lines)

Bug Fixes
- Fixed terminal crashing from invalid line jumps
- Corrected cursor positioning and edge cases pertaining to empty file navigation
- Removed duplicate code and minor debug output left in from previous testing

**v0.0.4**
- Added familiar vi-inspired modes & commands (normal & insert, /, :, :w, :wq, :q)
- Line jump function via :NUMBER (ex. :120 goes to line 120)
- Ctrl + f pages fwd ctrl + b pages bwd

**v0.0.3**
- Implemented tt (go to line 1) and bb (go to last line) normal mode shortcuts
- Improved the fuzzy find triggered by /, use arrow keys or scroll to move between finds, enter/esc to exit
- Added language support for c++, c#, java, Rust, SQL, HTML/CSS, and Javascript (in addition to Python & C)

**v0.0.2**
- Established default font color theme, found in .hakorc
- Alternate-screen & clean exit, wiping the terminal

**v0.0.1**
- Initial push
- Basic text editing
- Ability to save and load files
</details>

## Roadmap
- [ ] **v0.0.9**: Agnostic AI integration for code assistance, local LLM or major models via your API (grok, gpt, claude, ollama)

## Contributing
If you share the belief that simplicity empowers creativity, feel free to contribute.

#### Contribution is welcome in the form of:
- Forking this repo
- Submitting a Pull Request
- Bug reports and feature requests

Please ensure your code follows the existing style. I realize the single file is a personal choice, though if you choose to assist, I have divided the code.<br>

Sections of codebase are as follows:
1. Includes
2. Defines
3. Enums
4. Struct Declarations
5. Function Prototypes
6. Syntax Highlighting
7. Terminal
8. Buffer
9. Helpers
10. Color
11. Clipboard
12. Pane Management
13. Row Operations
14. Editor Operations
15. Cursor Movement
16. Scrolling
17. Visual Mode
18. Undo/Redo
19. Search
20. File I/O
21. Drawing
22. Input
23. Mode and Commands
24. Main Drawing Functions
25. Input Processing
26. Splash Screen
27. Explorer
28. AI Assistant
29. Init
30. Main

## Thank you for your attention.
This project started out of curiosity and a simple C text editor tutorial. If you hit any issues, feel free to open an issue on GitHub.
Pull requests, suggestions, or even thoughtful discussions are welcome.
