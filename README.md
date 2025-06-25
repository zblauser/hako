# 箱 Hako
![til](./assets/hako-Demo.gif)

## Overview
In a minimalistic terminal environment, Hako contains your code without distraction.
A lean, focused text editing tool; 
designed for engineers who appreciate clarity, precision, and the elegance of minimalism.

---

## Change Log
[v0.0.3]

- Added familiar vi-inspired modes & commands (normal & insert, /, :, :w, :wq, :q); susceptible to change
- Added language support for c++, c#, java, Rust, SQL, HTML/CSS, and Javascript (in addition to Python & C)
- Established default font color theme
- Implemented tt (go to line 1) and bb (go to last line) normal mode shortcuts

---

## Usage
#### Compile `hako.c`(ex. gcc)
```bash
gcc hako.c -o hako
```
#### Then launch it with:
```bash
./hako or ./hako [yourfile]
```

Your terminal becomes a box that holds your code, structured and serene.

---

## Current Timeline

- Broadening syntax support
	- `C`									[X]
	- `C++`									[X]
	- `C#`									[X]
	- `Python`								[X]
	- `Java`								[X]
	- `Rust`								[X]
	- `SQL`									[X]
	- `HTML/CSS`							[X]
	- `Javascript`							[X]
	- New syntax support in progress

- Establishing a `.hakorc`file for configs
	- create .hakorc						[ ]
	- various settings [tabs, hotkeys, etc]	[X]
	- establish default theme				[X]
	- rig more settings to rc				[ ]
	- prime for plugin support				[ ]

- Refine and create new key bindings
	- modes [insert, normal]				[X]
	- / for search							[X]
	- :q for quit :w for write				[X]
	- undo/redo								[ ]
	- copy/paste							[ ]
	- line # search							[ ]
	- tt for top bb for bottom				[X]
	- visual mode							[ ]

---

## Contributing

If you share the belief that simplicity empowers creativity, feel free to contribute.

#### Contribution is welcome in the form of:
- Forking this repo
- Submiting a Pull Request
- Bug reports and feature requests

Please ensure your code follows the existing style.

---

## Thank you for your attention.
If you hit any issues, feel free to open an issue on GitHub.
Pull requests, suggestions, or even thoughtful discussions are welcome.
