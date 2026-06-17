# dmenu-wl - Dynamic Menu for Wayland

An optimized, highly performant fork of `dmenu-wl` rewritten with premium dynamic navigation features and modern subsequence fuzzy sorting engines. Built exclusively for Wayland utilizing native `wlr-layer-shell` mechanisms.

## Key Features & Enhancements

* **True `fzf`-Style Fuzzy Matching:** Replaced literal string scans with an advanced subsequence scoring algorithm. Perfectly targets items using sparse queries (e.g., typing `vsc` targets `visual-studio-code`) and automatically awards boundary bonuses for directory slashes (`/`), spaces, underscores, and dashes.
* **Smart Priority Sorting & Precision Ranking:** Features an exact-match override system and dynamic length penalties. Precise hits naturally float to the absolute top slot, eliminating long-string inflation.
* **Case-Insensitive By Default:** Case-insensitive search mechanics are enabled natively out-of-the-box. Passing the `-i` or `--insensitive` flag now forces strict, case-sensitive matching if needed.
* **Infinite Stream Parsing:** Upgraded the core standard input processing from fixed-size chunk tracking to dynamic `getline()` allocation. Supports massive data pipelines and infinite text line lengths without breaking paths or truncating names.
* **Classic Dynamic Horizontal Scrolling:** Fully restored traditional `dmenu` single-line viewport logic. When running horizontally (without `-l`), selections navigate seamlessly to the screen bounds before shifting your view smoothly item-by-item.
* **Symmetrical Grid Layout Navigation:** Completely resolved overlapping text issues in multi-column configurations. When combining `-l` (lines) and `-g` (grid columns), arrow keys map symmetrically across rows and columns ($1\times1 \rightarrow 2\times1$).
* **Safe Multi-byte Password Masking:** Implemented a UTF-8 character-boundary parser for the `-P` parameter. Obfuscates text using accurate visual glyph dimensions rather than exposing multi-byte character lengths.
* **Instant Paste Refreshing:** Clipboard pasting (`Ctrl + v`) hooks straight into your compositor's selection layer and instantly pushes an immediate sub-surface redraw, eliminating stale views or requiring extra key registrations.

## Requirements

Requires a compositor that implements the `wlr-layer-shell` and `xdg-output` protocols. This means a **wlroots-based compositor** is required (tested extensively with Sway). 

*Note: GNOME (Mutter) and KDE (KWin) do not support these protocols natively and are not supported.*

### Dependencies (Libraries and Headers)
* `wayland-client`
* `cairo`
* `pango-1.0`
* `pangocairo-1.0`
* `xkbcommon`
* `glib-2.0`
* `gobject-2.0`
* `wl-roots` (specifically `wl-paste` utility for clipboard features)

## Installation

The project uses the Meson build system to automatically generate native protocol headers via `wayland-scanner`:

```bash
mkdir build
meson setup build
ninja -C build
sudo ninja -C build install
