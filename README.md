<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="https://raw.githubusercontent.com/hakocorp/hakocorp.github.io/1a50032f519ae4df58aed8394859084d39206955/assets/banner-dark.svg$0">
    <img src="https://raw.githubusercontent.com/hakocorp/hakocorp.github.io/1a50032f519ae4df58aed8394859084d39206955/assets/banner-light.svg$0" alt="HAKO" width="100%"/>
  </picture>
</p>

<p align="center">
  <em>A minimalistic modal text environment, containing your code without distraction.</em>
</p>

<p align="center">
  <img src="https://github.com/hakocorp/hakocorp.github.io/blob/main/assets/screenshot-splash.png?raw=true$0" alt="Splash" width="80%"/>
</p>

<table align="center">
  <tr>
    <td align="center" width="50%">
      <img src="https://github.com/hakocorp/hakocorp.github.io/blob/main/assets/screenshot-explorer.png?raw=true$0" alt="Kami explorer" width="100%"/><br/>
      <sub><b>紙 Kami</b> — file explorer</sub>
    </td>
    <td align="center" width="50%">
      <img src="https://github.com/hakocorp/hakocorp.github.io/blob/main/assets/screenshot-panes.png?raw=true$0" alt="Split panes with Rei" width="100%"/><br/>
      <sub>Split panes + <b>零 Rei</b> AI</sub>
    </td>
  </tr>
  <tr>
    <td align="center" width="50%">
      <img src="https://github.com/hakocorp/hakocorp.github.io/blob/main/assets/screenshot-editor.png?raw=true$0" alt="Editor with help and code" width="100%"/><br/>
      <sub>Editor with help overlay</sub>
    </td>
    <td align="center" width="50%">
      <img src="https://github.com/hakocorp/hakocorp.github.io/blob/main/assets/screenshot-themes.png?raw=true$0" alt="Theme picker" width="100%"/><br/>
      <sub>Theme picker · 17 presets</sub>
    </td>
  </tr>
</table>

<br>

零 **Rei** lives inside Hako — your in-editor AI assistant, drawn as a little squid:

```
            ███
          ███████         ----------
         █████████        Rei: v1
        ██ █████ ███      Provider: You Choose
      ███████████████     Model: You Choose
      █ ███████████ █     ----------
        ███████████
        █ █ █ █ █ █
```

## Overview

- **Modal Editing**: Vim-inspired normal, insert, visual, and visual-line modes
- **Full Motion Set**: counts, named registers, text objects (`diw`, `ci"`, `da(`), marks, jumplist, dot-repeat, `:s/` substitution, bracket match
- **Multi-Pane Support**: Split horizontally and vertically, resize with `Ctrl-W +/-/</>`
- **File Explorer**: 紙 Kami — directory navigation, hidden-file toggle, dirs-first sort
- **AI Assistant**: 零 Rei — multi-provider (Anthropic, OpenAI, Ollama), function-calling tool loop, SSE streaming, per-project trust and history
- **Skills**: drop `~/.hako/skills/*.md` into the system prompt, install from any URL with `/skill install`
- **Syntax Highlighting**: 40+ languages, search-match highlighting persists across edits
- **Undo/Redo**: time-bounded undo blocks, configurable depth
- **Visual Selection**: character and line modes with yank/delete/change, count feedback
- **Clipboard**: bracketed paste, system clipboard via `Ctrl-C`/`Ctrl-V`
- **Fast Search**: incremental with `n`/`N`, all matches highlighted
- **Line Numbers**: absolute or relative, dynamic width
- **Mouse Support**: click to position, scroll wheel moves cursor
- **17 Themes**: dark, light, gruvbox, nord, dracula, monokai, solarized, tokyonight, catppuccin, onedark, material, everforest, rosepine, github-dark, github-light, ayu, kanagawa
- **Customizable**: every setting in `.hakorc`, model/provider choices persist in `~/.hako/state`

## Build

```sh
# One-liner — works on Linux, macOS, Windows (mingw)
gcc hako.c -o hako -lpthread

# Or use the Makefile to also embed the executable icon where the OS allows
make
```

**Run**
```sh
./hako              # or  ./hako [filename]
```

**Install** — drop the binary into your PATH:
```sh
cp hako /usr/local/bin/hako          # Linux / macOS
```

> **Deps:** C standard library + POSIX/Win32 system headers + `pthread`. No third-party libraries linked. AI features shell out to `curl(1)` at runtime.

### Executable icon

The mascot icon files live in `icon/`:

| Platform | File          | What `make` does                                                                |
|----------|---------------|---------------------------------------------------------------------------------|
| Windows  | `hako.ico`    | Embeds icon into `.exe` via `windres` — real OS icon.                           |
| macOS    | `hako.icns`   | Attaches icon as a resource fork via `Rez`+`SetFile` (Xcode CLT). Best-effort.  |
| Linux    | `hako.png`    | ELF can't embed icons; ship `hako.png` and reference it from a `.desktop` file. |

A plain `gcc hako.c -o hako` produces a working binary on every platform — the icon is purely cosmetic.

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

### AI Assistant (零 Rei)

|Key        |Action                              |
|-----------|------------------------------------|
|`:ai`      |Toggle AI panel                     |
|`i`        |Enter prompt mode                   |
|`Enter`    |In INSERT: newline. In NORMAL: send |
|`Esc`      |Back to NORMAL                      |
|`v`        |Visual select (copy-only in history)|
|`j,k`      |Navigate history                    |
|`Ctrl-W w` |Switch pane                         |
|`Ctrl-C`   |Close panel                         |
|`:q`       |Close panel                         |

**Slash commands** (type inside the prompt):

|Command                 |Action                                       |
|------------------------|---------------------------------------------|
|`/help`                 |List commands                                |
|`/provider <name>`      |ollama, anthropic, openai                    |
|`/model <id>`           |Switch model (persists to `~/.hako/state`)   |
|`/tools on|off`         |Toggle function calling                      |
|`/trust` / `/trust revoke` | Grant or revoke file-ops in this project  |
|`/skills [reload]`      |List / reload `~/.hako/skills/*.md`          |
|`/skill install <url>`  |Download a skill into `~/.hako/skills/`      |
|`/history [local|global]`|Show path, or move to `<cwd>/.hako/history` |
|`/file <path>`          |Inject a local file into context             |
|`/clear`                |Wipe visible history                         |
|`/quit`                 |Close the panel                              |

**Tools** (when trusted): `read_file`, `list_dir`, `write_file` (path constrained to project), `run_shell` (10s timeout).

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

### v0.0.9 (Latest)<br>
Big one. HAKO goes from editor-with-AI-panel to modal editing environment with a real agent inside.

Editor
- Full vim-motion set: count prefix (`5j`, `3dd`), named registers (`"ayy`, `"ap`), text objects (`diw`, `ci"`, `da(`), marks (`ma`, `'a`), jumplist (`Ctrl-O` / `Tab`), dot-repeat (`.`), substitution (`:s/foo/bar/g`), bracket match (`%`), word-under-cursor search (`*`, `#`)
- Search highlighting persists across edits, all matches lit (not just current)
- Line-delete feedback in status (`N lines deleted`)
- `:e` expands `~/`, resolves relative and absolute paths so `:w` saves where you meant
- `:q` closes the active pane, not the whole program
- `dw`, `cw`, `:w file.txt` confirmed working
- 17 theme presets (dark, light, gruvbox, nord, dracula, monokai, solarized, tokyonight, catppuccin, onedark, material, everforest, rosepine, github-dark, github-light, ayu, kanagawa)
- Responsive layout — side panels auto-collapse on narrow terminals
- Explorer entries always sorted: directories first, then files, case-insensitive
- Status bar now uses the border color so panels feel like one chassis

Rei  — 零
- Multi-provider: Anthropic, OpenAI, Ollama (swap with `/provider`)
- Function-calling tool loop (Anthropic): `read_file`, `list_dir`, `write_file`, `run_shell`
- SSE streaming for text responses
- Project trust: first `:ai` asks before enabling file tools in a directory; grants stored in `<project>/.hako/trust`
- Per-project chat history in `<project>/.hako/history` when trusted, falls back to `~/.hako/history`
- Skills loader: `~/.hako/skills/*.md` injected into system prompt; `/skill install <url>` downloads a skill
- Multi-line input: Enter inserts newline in INSERT, sends in NORMAL. Input bar wraps and stays visible
- Slash commands: `/help`, `/provider`, `/model`, `/tools`, `/trust`, `/skills`, `/skill install`, `/history`, `/file`, `/clear`, `/quit`
- Model + provider choice persists across restarts (`~/.hako/state`)
- Configurable mascot (`ai_mascot=path/to/art.txt`), lucky-cat default

Fixes
- Pane focus no longer dangles when a side panel is closed from inside it
- Ctrl-W back from Rei does not lock the terminal
- Inline prompt reads (`"a`, `di"`, `ma`, `r`) skip mouse-motion events instead of consuming them as the expected letter
- Splash logo + status bar now theme-aware (match border color)
- Cursor lands in the selected explorer entry and in the Rei input bar instead of the status line
- Window-size changes resize panes immediately instead of drifting until the next SIGWINCH


### Previous Versions
<details>
<summary>Previous Changes</summary>

***v0.0.8***<br>
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
- [ ] Windows parity
- [ ] Diff-render (fewer redraws)
- [ ] In-editor slash menus for themes and settings
- [ ] Write-file diff preview with confirm
- [ ] OpenAI/Ollama tool parity
- [ ] Buffer list (`:ls`, `:b`)

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

