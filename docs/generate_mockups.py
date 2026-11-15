#!/usr/bin/env python3
"""Generate 800x480 SVG screen mockups for Polyend Tracker CE (Original / MK66).

Outputs vector UI mockups matching the Original Polyend Tracker FT812 800x480
layout and default color palette (`core/source/Themes.cpp`) into
`docs/images/mockups/`.
"""

from html import escape
import math
import os
import xml.etree.ElementTree as ET

OUT_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "images", "mockups")

# Original Theme palette (`core/source/Themes.cpp`)
BLACK = "#000000"
GREY_08 = "#080808"
GREY_0A = "#0A0A0A"
GREY_12 = "#121212"
GREY_17 = "#171717"
GREY_22 = "#222222"
GREY_32 = "#323132"
GREY_37 = "#373737"
GREY_44 = "#444344"
GREY_57 = "#575757"
GREY_77 = "#777777"
WHITE = "#FFFFFF"
RED = "#cd1818"
GREEN = "#30cb3f"
CYAN = "#3FFFFF"
DARK_CYAN = "#00A4B3"
NOTE_TEAL = "#00e8be"
INSTR_YELLOW = "#fef749"
FX1_PURPLE = "#bb58f1"
FX2_CYAN = "#57f1ff"
ORANGE = "#D67022"

FONT_SANS = "'Inter', 'DejaVu Sans', 'Helvetica Neue', Arial, sans-serif"
FONT_MONO = "'JetBrains Mono', 'DejaVu Sans Mono', 'Menlo', monospace"


class SvgScreen:
    """Builder for an 800x480 Polyend Tracker FT812 UI mockup SVG."""

    def __init__(self, title_left: str, title_right: str = "", right_color: str = WHITE):
        self.elements = []
        self.title_left = title_left
        self.title_right = title_right
        self.right_color = right_color

    def add(self, raw_svg: str) -> None:
        self.elements.append(raw_svg)

    def rect(self, x, y, w, h, fill="none", stroke="none", stroke_width=1, rx=0, opacity=1.0):
        attrs = [
            f'x="{x}"', f'y="{y}"', f'width="{w}"', f'height="{h}"',
            f'fill="{fill}"'
        ]
        if stroke != "none":
            attrs.append(f'stroke="{stroke}" stroke-width="{stroke_width}"')
        if rx:
            attrs.append(f'rx="{rx}"')
        if opacity < 1.0:
            attrs.append(f'fill-opacity="{opacity}"')
        self.add(f'<rect {" ".join(attrs)} />')

    def line(self, x1, y1, x2, y2, stroke=GREY_22, stroke_width=1, dash=""):
        dash_attr = f' stroke-dasharray="{dash}"' if dash else ""
        self.add(
            f'<line x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}" '
            f'stroke="{stroke}" stroke-width="{stroke_width}"{dash_attr} />'
        )

    def text(
        self, x, y, content: str, fill=WHITE, size=13, weight="normal",
        anchor="start", font=FONT_SANS
    ):
        self.add(
            f'<text x="{x}" y="{y}" fill="{fill}" font-family="{font}" '
            f'font-size="{size}" font-weight="{weight}" text-anchor="{anchor}">'
            f'{escape(content)}</text>'
        )

    def path(self, d: str, stroke=WHITE, stroke_width=2, fill="none", opacity=1.0):
        op = f' stroke-opacity="{opacity}"' if opacity < 1.0 else ""
        self.add(
            f'<path d="{d}" stroke="{stroke}" stroke-width="{stroke_width}" '
            f'fill="{fill}" stroke-linejoin="round" stroke-linecap="round"{op} />'
        )

    def draw_frame(self, x, y, w, h, color=RED, width=2):
        self.rect(x, y, w, h, fill="none", stroke=color, stroke_width=width)

    def draw_header(self):
        self.rect(0, 0, 800, 28, fill=GREY_08)
        self.line(0, 28, 800, 28, stroke=GREY_22, stroke_width=1)
        self.text(12, 19, self.title_left, fill=WHITE, size=14, weight="bold")
        if self.title_right:
            self.text(788, 19, self.title_right, fill=self.right_color, size=13, anchor="end")

    def draw_bottom_buttons(self, buttons, focus_cols=None):
        """Render the bottom 8 screen button bar (y=424..479).

        buttons: list of tuples:
          (col_start, col_span, top_label, bottom_label, opts_dict)
        """
        focus_cols = focus_cols or set()
        self.rect(0, 424, 800, 56, fill=BLACK)
        self.line(0, 424, 800, 424, stroke=GREY_22, stroke_width=1)
        for col_start, span, top_lbl, bot_lbl, opts in buttons:
            x = col_start * 100 + 2
            w = span * 100 - 4
            y = 426
            h = 52
            bg = opts.get("bg", GREY_0A)
            top_col = opts.get("top_color", GREY_77 if opts.get("active", True) else GREY_37)
            bot_col = opts.get("bot_color", WHITE if opts.get("active", True) else GREY_57)
            is_focused = any(c in focus_cols for c in range(col_start, col_start + span))
            stroke = RED if is_focused else opts.get("border", GREY_22)
            sw = 2 if is_focused else 1
            self.rect(x, y, w, h, fill=bg, stroke=stroke, stroke_width=sw)

            # Subtle physical button index indicator(s)
            if opts.get("show_num", True):
                if span == 1:
                    self.text(x + 4, y + 10, f"{col_start + 1}", fill=GREY_44, size=8.5, font=FONT_MONO)
                else:
                    self.text(
                        x + 4, y + 10,
                        f"{col_start + 1}–{col_start + span}",
                        fill=GREY_44, size=8.5, font=FONT_MONO
                    )

            cx = x + w / 2
            if top_lbl and bot_lbl:
                self.text(cx, y + 24, top_lbl, fill=top_col, size=12, anchor="middle", weight="bold")
                self.text(cx, y + 43, bot_lbl, fill=bot_col, size=12, anchor="middle")
            elif top_lbl:
                self.text(cx, y + 33, top_lbl, fill=WHITE if opts.get("active", True) else GREY_57,
                          size=13, anchor="middle", weight="bold")

    def draw_list(
        self, x, y, w, h, header, items, selected_idx,
        active=True, show_scroll=True, right_tags=None
    ):
        right_tags = right_tags or {}
        self.rect(x, y, w, h, fill=GREY_0A, stroke=RED if active else GREY_22, stroke_width=2 if active else 1)
        row_y = y
        if header:
            self.rect(x + 1, y + 1, w - 2, 26, fill=GREY_22)
            self.text(x + 10, y + 18, header, fill=WHITE if active else GREY_77, size=12, weight="bold")
            row_y += 28

        row_h = 26
        scroll_w = 5 if show_scroll else 0
        for idx, item in enumerate(items):
            iy = row_y + idx * row_h
            if iy + row_h > y + h - 4:
                break
            is_sel = (idx == selected_idx)
            if is_sel:
                self.rect(x + 4, iy + 2, w - 10 - scroll_w, row_h - 3, fill=WHITE if active else GREY_32)
            txt_col = (BLACK if active else WHITE) if is_sel else (WHITE if active else GREY_77)
            prefix = ""
            if item.startswith("[") and item.endswith("]"):
                txt_col = BLACK if is_sel else (INSTR_YELLOW if active else GREY_77)
            self.text(x + 10, iy + 18, f"{prefix}{item}", fill=txt_col, size=12,
                      weight="bold" if is_sel else "normal")
            if idx in right_tags:
                tag = right_tags[idx]
                tag_col = CYAN if ("[S]" in tag and not is_sel) else (DARK_CYAN if is_sel else GREY_77)
                if not ("[S]" in tag):
                    tag_col = BLACK if is_sel else GREY_77
                self.text(x + w - 12 - scroll_w, iy + 18, tag, fill=tag_col, size=11,
                          anchor="end", font=FONT_MONO, weight="bold" if "[S]" in tag else "normal")

        if show_scroll and len(items) > 0:
            sb_x = x + w - 7
            sb_h = h - (row_y - y) - 6
            self.rect(sb_x, row_y + 2, 4, sb_h, fill=GREY_08)
            thumb_h = max(24, sb_h // max(2, len(items) // 3))
            thumb_y = row_y + 2 + int((sb_h - thumb_h) * (selected_idx / max(1, len(items) - 1)))
            self.rect(sb_x, thumb_y, 4, thumb_h, fill=GREY_57)

    def draw_bar(self, x, y, w, h, value_pct, title="", readout="", focused=False, bar_color=WHITE, bg=GREY_0A):
        self.rect(x, y, w, h, fill=bg, stroke=RED if focused else GREY_22, stroke_width=2 if focused else 1)
        pad_top = 26 if title else 10
        if title:
            self.text(x + w / 2, y + 18, title, fill=GREY_77, size=11, anchor="middle", weight="bold")
        bar_x = x + 16
        bar_w = w - 32
        bar_top = y + pad_top
        bar_bot = y + h - (24 if readout else 10)
        bar_h = max(20, bar_bot - bar_top)
        self.rect(bar_x, bar_top, bar_w, bar_h, fill=GREY_08, stroke=GREY_22, stroke_width=1)
        clamped = max(0.0, min(100.0, value_pct))
        fill_h = int(bar_h * (clamped / 100.0))
        if fill_h > 0:
            self.rect(bar_x + 1, bar_bot - fill_h, bar_w - 2, fill_h, fill=bar_color)
        if readout:
            self.text(x + w / 2, y + h - 8, readout, fill=WHITE, size=11, anchor="middle", font=FONT_MONO)

    def draw_bipolar_bar(self, x, y, w, h, val_x, val_y, max_abs=7, title="Warp X/Y", focused=False):
        self.rect(x, y, w, h, fill=GREY_0A, stroke=RED if focused else GREY_22, stroke_width=2 if focused else 1)
        self.text(x + w / 2, y + 18, title, fill=GREY_77, size=11, anchor="middle", weight="bold")
        bar_top = y + 26
        bar_bot = y + h - 24
        bar_h = bar_bot - bar_top
        mid_y = bar_top + bar_h // 2
        half_w = (w - 30) // 2
        left_x = x + 12
        right_x = x + 18 + half_w

        for bx, val, col in ((left_x, val_x, GREEN), (right_x, val_y, CYAN)):
            self.rect(bx, bar_top, half_w, bar_h, fill=GREY_08, stroke=GREY_22, stroke_width=1)
            self.line(bx, mid_y, bx + half_w, mid_y, stroke=GREY_57, stroke_width=1)
            fh = int((bar_h // 2) * (abs(val) / float(max_abs)))
            if val >= 0 and fh > 0:
                self.rect(bx + 1, mid_y - fh, half_w - 2, fh, fill=col)
            elif val < 0 and fh > 0:
                self.rect(bx + 1, mid_y, half_w - 2, fh, fill=RED)
        self.text(x + w / 2, y + h - 8, f"{val_x:+d}/{val_y:+d}", fill=WHITE, size=11, anchor="middle", font=FONT_MONO)

    def render(self) -> str:
        self.draw_header()
        body = "\n  ".join(self.elements)
        return (
            f'<svg xmlns="http://www.w3.org/2000/svg" width="800" height="480" viewBox="0 0 800 480">\n'
            f'  <rect x="0" y="0" width="800" height="480" fill="{BLACK}" />\n'
            f'  {body}\n'
            f'</svg>\n'
        )


def btn(col, top, bot="", **opts):
    return (col, opts.pop("span", 1), top, bot, opts)


def wave_path_from_fn(x0, y_mid, width, amp, fn, samples=240):
    pts = []
    for i in range(samples + 1):
        t = i / float(samples)
        px = x0 + t * width
        py = y_mid - fn(t) * amp
        cmd = "M" if i == 0 else "L"
        pts.append(f"{cmd}{px:.1f},{py:.1f}")
    return " ".join(pts)


def draw_play_mode_list(s: SvgScreen, active_idx: int, focused: bool = False):
    modes = [
        "1-Shot", "Forward Loop", "Backward Loop", "Pingpong Loop",
        "Slice", "Beat Slice", "Wavetable", "Granular", "Synth"
    ]
    s.draw_list(602, 31, 196, 391, "Play Mode", modes, active_idx, active=focused, show_scroll=True)


# ==============================================================================
# Individual Screen Mockup Generators (18 screens)
# ==============================================================================

def gen_01a_synth_page1():
    s = SvgScreen("Sample Playback", "1. Synth Bass")
    s.rect(2, 31, 596, 391, fill=GREY_0A, stroke=GREY_22)
    s.line(12, 226, 588, 226, stroke=GREY_22)
    s.line(300, 56, 300, 396, stroke=GREY_17, dash="4,4")
    s.text(16, 52, "Live 2-Cycle Synth Oscilloscope (Classic: Saw + Pulse, Detune 18)", fill=GREY_77, size=11)

    def classic_fn(t):
        ph1 = (t * 2.0) % 1.0
        saw = 1.0 - 2.0 * ph1
        ph2 = (t * 2.03 + 0.08) % 1.0
        pulse = 0.65 if ph2 < 0.5 else -0.65
        return max(-1.0, min(1.0, 0.58 * saw + 0.35 * pulse))

    s.path(wave_path_from_fn(14, 226, 572, 125, classic_fn, samples=260), stroke=WHITE, stroke_width=2.2)
    draw_play_mode_list(s, 8, focused=True)
    s.draw_bottom_buttons([
        btn(0, "Preview", "-"),
        btn(1, "Mode", "Classic"),
        btn(2, "Osc A", "Saw"),
        btn(3, "Osc B", "Pulse"),
        btn(4, "Detune", "18"),
        btn(5, "More", "•◦"),
        btn(6, "▲  Play Mode  ▼", "[7]       [8]", span=2),
    ], focus_cols={1})
    return s.render()


def gen_01b_synth_page2():
    s = SvgScreen("Sample Playback", "1. Synth Bass")
    s.rect(2, 31, 596, 391, fill=GREY_0A, stroke=GREY_22)
    s.line(12, 226, 588, 226, stroke=GREY_22)
    s.line(300, 56, 300, 396, stroke=GREY_17, dash="4,4")
    s.text(16, 52, "Live 2-Cycle Synth Oscilloscope (Page 2: DCW / Pulse W / Sub / Noise)", fill=GREY_77, size=11)

    def cz_pd_fn(t):
        ph = (t * 2.0) % 1.0
        dist_ph = ph ** 0.55 if ph < 0.7 else 0.7 + ((ph - 0.7) / 0.3) * 0.3
        pd = -math.cos(2.0 * math.pi * dist_ph)
        sub = 0.25 if ((t * 1.0) % 1.0) < 0.5 else -0.25
        noise = 0.06 * math.sin(t * 297.0)
        return max(-1.0, min(1.0, 0.72 * pd + sub + noise))

    s.path(wave_path_from_fn(14, 226, 572, 125, cz_pd_fn, samples=260), stroke=WHITE, stroke_width=2.2)
    s.text(16, 408, "* Button [3] automatically displays 'DCW' when Mode = CZ PD", fill=GREY_57, size=11)
    draw_play_mode_list(s, 8, focused=False)
    s.draw_bottom_buttons([
        btn(0, "Osc B Lvl", "50%"),
        btn(1, "Semi B", "+0 st"),
        btn(2, "Pulse W*", "50%"),
        btn(3, "Sub Lvl", "35%"),
        btn(4, "Noise", "10%"),
        btn(5, "More", "◦•"),
        btn(6, "▲  Play Mode  ▼", "[7]       [8]", span=2),
    ], focus_cols={2})
    return s.render()


def gen_02a_granular_cloud():
    s = SvgScreen("Sample Playback", "2. Pad Texture.wav")
    s.rect(2, 31, 596, 391, fill=GREY_0A, stroke=GREY_22)
    s.line(12, 226, 588, 226, stroke=GREY_22)
    s.text(16, 52, "Granular Cloud Engine: 4 overlapping staggered grains (±12.5% jitter, Gauss window)", fill=GREY_77, size=11)

    def pad_wave(t):
        return (0.65 * math.sin(t * 42.0) + 0.35 * math.sin(t * 113.0)) * (0.45 + 0.5 * math.sin(t * math.pi))

    s.path(wave_path_from_fn(14, 226, 572, 118, pad_wave, samples=240), stroke=GREY_77, stroke_width=1.5)
    # 4 active overlapping Cloud grain windows + anchor
    anchor_x = 240
    s.line(anchor_x, 86, anchor_x, 366, stroke=RED, stroke_width=2)
    s.text(anchor_x + 4, 80, "Pos", fill=RED, size=10, font=FONT_MONO)
    grains = [(205, 86), (225, 86), (255, 86), (275, 86)]
    for idx, (gx, gw) in enumerate(grains):
        s.rect(gx - gw // 2, 108, gw, 236, fill=CYAN, opacity=0.10, stroke=CYAN, stroke_width=1)
        # Gaussian bell envelope overlay
        env_d = wave_path_from_fn(
            gx - gw // 2, 344, gw, 210,
            lambda u: math.exp(-((u - 0.5) / 0.24) ** 2), samples=40
        )
        s.path(env_d, stroke=CYAN, stroke_width=1.6, opacity=0.9)
        s.line(gx, 108, gx, 344, stroke=CYAN, stroke_width=1.5)
    draw_play_mode_list(s, 7, focused=False)
    s.draw_bottom_buttons([
        btn(0, "Preview", "-"),
        btn(1, "Position", "0.420 s"),
        btn(2, "Length", "125.0 ms"),
        btn(3, "Shape", "Gauss"),
        btn(4, "Loop", "Cloud"),
        btn(5, "", ""),
        btn(6, "▲  Play Mode  ▼", "[7]       [8]", span=2),
    ], focus_cols={4})
    return s.render()


def gen_02b_slice_play_thru():
    s = SvgScreen("Sample Playback", "3. Amen Break.wav")
    s.rect(2, 31, 596, 391, fill=GREY_0A, stroke=GREY_22)
    s.line(12, 226, 588, 226, stroke=GREY_22)
    s.text(16, 52, "Slice / Beat Slice Page 2: Play Thru = On (continues past slice markers until Note Off)", fill=GREY_77, size=11)

    def break_wave(t):
        beat = (t * 8.0) % 1.0
        env = math.exp(-beat * 4.5)
        return env * math.sin(t * 175.0 + 0.5 * math.cos(t * 53.0))

    s.path(wave_path_from_fn(14, 226, 572, 118, break_wave, samples=260), stroke=WHITE, stroke_width=1.7)
    slices = [14 + int(i * (572 / 8)) for i in range(9)]
    for i, sx in enumerate(slices[:-1]):
        col = RED if i == 2 else INSTR_YELLOW
        s.line(sx, 84, sx, 366, stroke=col, stroke_width=2 if i == 2 else 1.3)
        s.text(sx + 4, 98, f"S{i+1:02d}", fill=col, size=10, font=FONT_MONO)
    # Continuous Play Thru arrow from S03 across S04..S07
    s.line(slices[2], 382, slices[6], 382, stroke=GREEN, stroke_width=2.5)
    s.text((slices[2] + slices[6]) / 2, 375, "Play Thru = On  ▶▶", fill=GREEN, size=11, anchor="middle", weight="bold")
    draw_play_mode_list(s, 4, focused=False)
    s.draw_bottom_buttons([
        btn(0, "Slice", "Auto"),
        btn(1, "Slice", "Evenly"),
        btn(2, "No of Slices", "16"),
        btn(3, "Play Thru", "On"),
        btn(4, "", ""),
        btn(5, "More", "◦•"),
        btn(6, "▲  Play Mode  ▼", "[7]       [8]", span=2),
    ], focus_cols={3})
    return s.render()


def draw_automation_screen(
    dest_idx, type_idx, wave_code, curve_fn, bars_spec, buttons, focus_cols, dests=None
):
    s = SvgScreen("Instrument Automation 2/2", "1. CZ PD Bass")
    if dests is None:
        dests = [
            "Volume *",
            "Panning",
            "Cutoff *",
            "Resonance",
            "DCW A *",
            "DCW B *",
            "Osc B Pitch",
            "Detune",
            "Osc B Level",
            "Sub Level",
            "Noise Level",
            "Finetune",
            "Pitch",
            "Overdrive",
        ]
    types = ["Off", "Envelope", "LFO", "WT Env", "WT LFO"]
    s.draw_list(2, 31, 196, 391, "Destination", dests, dest_idx, active=(0 in focus_cols))
    s.draw_list(202, 31, 96, 391, "Type", types, type_idx, active=(2 in focus_cols), show_scroll=False)

    # Top-right 2048-pt morphed Wavetable curve preview (x=302..798, y=31..169)
    s.rect(302, 31, 496, 138, fill=GREY_0A, stroke=GREY_22)
    s.line(314, 100, 786, 100, stroke=GREY_17)
    s.text(312, 48, f"2048-pt Shape Preview ({wave_code})", fill=GREY_77, size=11)
    s.path(wave_path_from_fn(314, 148, 472, 88, curve_fn, samples=180), stroke=CYAN, stroke_width=2.2)

    # 5 vertical parameter bars aligned above buttons [4]..[8] (x=302..798, y=173..422)
    for idx, kind in enumerate(bars_spec):
        bx = 300 + idx * 100 + 2
        bw = 96
        if kind["type"] == "bipolar":
            s.draw_bipolar_bar(bx, 173, bw, 249, kind["vx"], kind["vy"], title=kind["title"], focused=((3 + idx) in focus_cols))
        else:
            s.draw_bar(bx, 173, bw, 249, kind["pct"], title=kind["title"], readout=kind["readout"],
                       focused=((3 + idx) in focus_cols), bar_color=kind.get("color", WHITE))

    s.draw_bottom_buttons(buttons, focus_cols=focus_cols)
    return s.render()


def gen_03a_wt_env():
    def wt_env_fn(t):
        # Asymmetric punchy contour warped by +2/-3
        if t < 0.08:
            return t / 0.08
        return max(0.0, (1.0 - (t - 0.08) / 0.92) ** 1.7)

    bars = [
        {"type": "bar", "title": "Wave", "pct": 18, "readout": "F 001"},
        {"type": "bar", "title": "Duration", "pct": 25, "readout": "0.500s"},
        {"type": "bipolar", "title": "Warp X/Y", "vx": 2, "vy": -3},
        {"type": "bar", "title": "Sustain", "pct": 0, "readout": "Off"},
        {"type": "bar", "title": "Amount", "pct": 100, "readout": "100%"},
    ]
    buttons = [
        btn(0, "Destination", "", span=2),
        btn(2, "Type", "WT Env"),
        btn(3, "Wave", "F 001"),
        btn(4, "Duration", "0.500s"),
        btn(5, "Warp X/Y", "+2/-3"),
        btn(6, "Sustain", "Off"),
        btn(7, "Amount", "100%"),
    ]
    return draw_automation_screen(2, 3, "F 001 | Warp +2/-3", wt_env_fn, bars, buttons, {5})


def gen_03b_wt_lfo():
    def wt_lfo_fn(t):
        # Stepped warped triangle LFO
        w = 0.5 + 0.5 * math.sin(t * 4.0 * math.pi + 0.45 * math.sin(t * 8.0 * math.pi))
        return w

    bars = [
        {"type": "bar", "title": "Wave", "pct": 22, "readout": "F 014"},
        {"type": "bar", "title": "Speed", "pct": 52, "readout": "4 steps"},
        {"type": "bar", "title": "Warp X", "pct": 71, "readout": "+3", "color": GREEN},
        {"type": "bar", "title": "Warp Y", "pct": 36, "readout": "-2", "color": RED},
        {"type": "bar", "title": "Amount", "pct": 80, "readout": "80%"},
    ]
    buttons = [
        btn(0, "Destination", "", span=2),
        btn(2, "Type", "WT LFO"),
        btn(3, "Wave", "F 014"),
        btn(4, "Speed", "4 steps"),
        btn(5, "Warp X", "+3"),
        btn(6, "Warp Y", "-2"),
        btn(7, "Amount", "80%"),
    ]
    return draw_automation_screen(2, 4, "F 014 | LFO Warp +3/-2", wt_lfo_fn, bars, buttons, {3})


def gen_04_mod1_mod2():
    def mod_fn(t):
        return 0.5 + 0.48 * math.sin(t * 2.0 * math.pi) * math.cos(t * 6.0 * math.pi)

    bars = [
        {"type": "bar", "title": "Wave", "pct": 32, "readout": "F 021"},
        {"type": "bar", "title": "Speed", "pct": 62, "readout": "2 steps"},
        {"type": "bar", "title": "Warp X", "pct": 50, "readout": "+0"},
        {"type": "bar", "title": "Warp Y", "pct": 64, "readout": "+2", "color": CYAN},
        {"type": "bar", "title": "Amount", "pct": 65, "readout": "65%"},
    ]
    buttons = [
        btn(0, "Destination", "", span=2),
        btn(2, "Type", "WT LFO"),
        btn(3, "Wave", "F 021"),
        btn(4, "Speed", "2 steps"),
        btn(5, "Warp X", "+0"),
        btn(6, "Warp Y", "+2"),
        btn(7, "Amount", "65%"),
    ]
    return draw_automation_screen(5, 4, "DCW B | F 021 +0/+2", mod_fn, bars, buttons, {0, 1})


def gen_05a_pattern_mv():
    s = SvgScreen("Pattern 1   [4/32]   Tracks 1-4", "120.0 BPM   [Note -> m|V]")
    s.rect(2, 31, 796, 391, fill=GREY_08, stroke=GREY_22)
    # Column header
    s.rect(3, 32, 794, 24, fill=GREY_17)
    s.text(18, 49, "Stp", fill=GREY_77, size=11, font=FONT_MONO, anchor="middle")
    track_w = 190
    for t in range(4):
        tx = 38 + t * track_w
        s.line(tx, 32, tx, 421, stroke=BLACK, stroke_width=1.5)
        s.text(tx + 8, 49, f"T{t+1}: Note  In   m   V", fill=WHITE if t == 0 else GREY_77, size=11, font=FONT_MONO)

    rows = [
        ("00", [("C-3", "I01", "m--", "V--"), ("C-2", "I02", "m--", "V--"), ("---", "--", "m--", "V--"), ("---", "--", "m--", "V--")]),
        ("01", [("---", "--", "m--", "V--"), ("---", "--", "m--", "V--"), ("F#3", "I05", "m22", "V88"), ("---", "--", "m--", "V--")]),
        ("02", [("D#3", "I01", "m15", "V92"), ("---", "--", "m--", "V--"), ("---", "--", "m--", "V--"), ("C-4", "I08", "m00", "V75")]),
        ("03", [("---", "--", "m--", "V--"), ("G-2", "I02", "m40", "V64"), ("F#3", "I05", "m08", "V95"), ("---", "--", "m--", "V--")]),
        ("04", [("G-3", "I01", "m50", "V80"), ("---", "--", "m--", "V--"), ("---", "--", "m--", "V--"), ("OFF", "--", "m--", "V--")]),
        ("05", [("---", "--", "m--", "V--"), ("---", "--", "m--", "V--"), ("F#3", "I05", "m--", "V--"), ("---", "--", "m--", "V--")]),
        ("06", [("A#3", "I01", "m00", "V100"), ("D#2", "I02", "m12", "V85"), ("---", "--", "m--", "V--"), ("---", "--", "m--", "V--")]),
        ("07", [("---", "--", "m--", "V--"), ("---", "--", "m--", "V--"), ("F#3", "I05", "m30", "V72"), ("---", "--", "m--", "V--")]),
    ]
    for r_idx, (stp, cols) in enumerate(rows):
        ry = 60 + r_idx * 42
        if r_idx == 2:
            s.rect(3, ry, 794, 38, fill=GREY_17)
            s.rect(40, ry + 1, 186, 36, fill="none", stroke=RED, stroke_width=2)
        elif r_idx % 4 == 0:
            s.rect(3, ry, 794, 38, fill="#0e0e0e")
        s.text(19, ry + 24, stp, fill=WHITE if r_idx == 2 else GREY_57, size=12, font=FONT_MONO, anchor="middle")
        for t, (note, inst, m_val, v_val) in enumerate(cols):
            tx = 46 + t * track_w
            ncol = NOTE_TEAL if note not in ("---", "OFF") else (RED if note == "OFF" else GREY_37)
            icol = INSTR_YELLOW if inst != "--" else GREY_37
            mcol = FX1_PURPLE if m_val != "m--" else GREY_44
            vcol = FX2_CYAN if v_val != "V--" else GREY_44
            s.text(tx, ry + 24, note, fill=ncol, size=12, font=FONT_MONO)
            s.text(tx + 38, ry + 24, inst, fill=icol, size=12, font=FONT_MONO)
            s.text(tx + 78, ry + 24, m_val, fill=mcol, size=12, font=FONT_MONO)
            s.text(tx + 118, ry + 24, v_val, fill=vcol, size=12, font=FONT_MONO)

    s.text(16, 412, "Tip: Press [Note] on Note column to toggle [Fx1 | Fx2] <-> [m | V] (empty cells show dimmed m-- V--)",
           fill=GREY_77, size=11)
    s.draw_bottom_buttons([
        btn(0, "Pattern", "1"),
        btn(1, "Length", "32"),
        btn(2, "Step jump", "1"),
        btn(3, "Fill", ""),
        btn(4, "Preview", ""),
        btn(5, "Undo", ""),
        btn(6, "Redo", ""),
        btn(7, "More", "•◦"),
    ])
    return s.render()


def gen_05b_pattern_step_fx():
    s = SvgScreen("Pattern 1   [4/32]   Tracks 1-4", "Step 02 | Track 2 | Fx1")
    s.rect(2, 31, 796, 391, fill=GREY_08, stroke=GREY_22)
    # Dim background grid lines
    for y in range(64, 410, 42):
        s.line(10, y, 790, y, stroke=GREY_12)
    # Centered Step FX Popup
    px, py, pw, ph = 86, 56, 628, 336
    s.rect(px, py, pw, ph, fill=GREY_0A, stroke=RED, stroke_width=2)
    s.rect(px + 2, py + 2, pw - 4, 34, fill=WHITE)
    s.text(px + 16, py + 24, "Step FX Selector — New & Expanded Effects (Fx1 / Fx2)", fill=BLACK, size=13, weight="bold")

    fx_items = [
        ("C - Chance", "0..100% | GL | !L | 1:1..6:6 (Play N/Skip M) | S11..S66", FX1_PURPLE, False),
        ("w - WT Volume Env", "XY hex: X scales Duration (0.05x..4.00x), Y = Warp X (-7..+7)", CYAN, True),
        ("y - WT Filter Env", "XY hex: X scales Duration (0.05x..4.00x), Y = Warp X (-7..+7)", CYAN, False),
        ("z - Granular Length", "0..100%: 0-50% = 1.0-100.0 ms | 50-100% = 100.0-1000.0 ms", GREEN, False),
        ("0 - MIDI Chord", "68 Chords: 1..29 Root Voicings + 30..67 New Inversions", INSTR_YELLOW, False),
        ("A - Arp (Chords)", "Shares expanded 68-voicing chord & inversion table", NOTE_TEAL, False),
    ]
    for i, (name, desc, col, sel) in enumerate(fx_items):
        iy = py + 52 + i * 44
        if sel:
            s.rect(px + 12, iy - 4, pw - 24, 38, fill=WHITE)
        s.text(px + 22, iy + 14, name, fill=BLACK if sel else col, size=13, weight="bold", font=FONT_MONO)
        s.text(px + 210, iy + 14, desc, fill=BLACK if sel else WHITE, size=11)

    s.draw_bottom_buttons([
        btn(0, "Pattern", "1"),
        btn(1, "Length", "32"),
        btn(2, "Step jump", "1"),
        btn(3, "Fill", ""),
        btn(4, "Preview", ""),
        btn(5, "Undo", ""),
        btn(6, "Redo", ""),
        btn(7, "More", "•◦"),
    ])
    return s.render()


def gen_06a_master_drive_plus():
    s = SvgScreen("Master 1/3", "")
    s.draw_bar(2, 31, 96, 391, 82, "Volume", "0.0 dB")
    for idx, name in enumerate(("Reverb", "Delay", "Limiter", "EQ"), start=1):
        bx = idx * 100 + 2
        s.rect(bx, 31, 96, 391, fill=GREY_0A, stroke=GREY_22)
        s.text(bx + 48, 224, f"[{name}]", fill=GREY_57, size=12, anchor="middle")
    sat_items = ["Hard Clip", "Soft Clip", "Saturate", "Overdrive", "Drive+"]
    s.draw_list(502, 31, 96, 391, "Saturation", sat_items, 4, active=True, show_scroll=False)
    s.draw_bar(602, 31, 96, 391, 15, "Bass Boost", "15%")
    s.draw_bar(702, 31, 96, 391, 20, "Space", "20%")
    s.draw_bottom_buttons([
        btn(0, "Volume", "0.0 dB"),
        btn(1, "Reverb", ""),
        btn(2, "Delay", ""),
        btn(3, "Limiter", ""),
        btn(4, "EQ", ""),
        btn(5, "Saturation", "Drive+"),
        btn(6, "Bass Boost", "15%"),
        btn(7, "Space", "20%"),
    ], focus_cols={5})
    return s.render()


def gen_06b_master_delay_frz():
    s = SvgScreen("Master Delay", "")
    s.draw_list(2, 31, 96, 391, "PingPong", ["Off", "On"], 1, active=False, show_scroll=False)
    s.draw_list(102, 31, 96, 391, "Sync", ["Off", "On"], 1, active=False, show_scroll=False)
    s.draw_list(202, 31, 96, 391, "Rate", ["1/1", "1/2", "1/4", "1/8", "1/16", "1/8D"], 3, active=False)
    s.draw_bar(302, 31, 96, 391, 35, "Time", "--", bar_color=GREY_44)
    s.draw_bar(402, 31, 96, 391, 100, "Feedback", "Frz", focused=True, bar_color=CYAN)
    s.draw_bar(502, 31, 96, 391, 58, "Filter", "2500 Hz")
    s.draw_bar(602, 31, 96, 391, 0, "Line in", "-inf dB")
    s.rect(702, 31, 96, 391, fill=GREY_0A, stroke=GREY_22)
    s.text(750, 224, "Frz: infinite", fill=CYAN, size=11, anchor="middle")
    s.text(750, 242, "loop lock", fill=CYAN, size=11, anchor="middle")
    s.draw_bottom_buttons([
        btn(0, "PingPong", "On"),
        btn(1, "Sync", "On"),
        btn(2, "Rate", "1/8"),
        btn(3, "Time", "--", active=False),
        btn(4, "Feedback", "Frz", bot_color=CYAN),
        btn(5, "Filter", "2500 Hz"),
        btn(6, "Line in", "-inf dB"),
        btn(7, "Back", ""),
    ], focus_cols={4})
    return s.render()


def gen_07_sample_loader_stream():
    s = SvgScreen("Sample Loader", "1. Kick_909.wav")
    files = [
        "[..]", "[Drums]", "Vocal_Stem_120bpm.wav", "Pad_Long_Atmosphere.wav",
        "Bass_OneShot.wav", "Breakbeat_140bpm.wav", "Field_Rain_2m.wav"
    ]
    file_tags = {2: "4.2 MB", 3: "2.8 MB", 4: "180 KB", 5: "1.1 MB", 6: "10.5 MB"}
    s.draw_list(2, 31, 296, 391, "SD Card: /Samples/Stems", files, 2, active=True, right_tags=file_tags)

    instrs = [
        "01. Kick_909.wav", "02. Snare_Tight.wav", "03. Vocal_Stem_120",
        "04. Synth Bass", "05. Field_Rain_2m", "06. (empty)", "07. (empty)"
    ]
    instr_tags = {2: "[S]", 4: "[S]"}
    s.draw_list(302, 31, 296, 391, "Instruments (01-48)", instrs, 2, active=False, right_tags=instr_tags)

    s.draw_bar(602, 31, 96, 391, 68, "Preview Vol", "-3.50 dB")
    s.draw_bar(702, 31, 96, 391, 42, "Memory", "42%", bar_color=GREEN)

    s.draw_bottom_buttons([
        btn(0, "Add*", "Shift:Empty"),
        btn(1, "Add next", ""),
        btn(2, "Preview", ""),
        btn(3, "Rename", ""),
        btn(4, "Import", ""),
        btn(5, "Delete", "unused"),
        btn(6, "Preview Vol", "-3.50 dB"),
        btn(7, "Mem: Auto*", "42%"),
    ], focus_cols={7})
    return s.render()


def gen_08a_sample_editor_ptfx():
    s = SvgScreen("Sample Editor", "1. Kick_909.wav")
    s.draw_bar(2, 31, 96, 391, 72, "Attack", "+45%", focused=True, bar_color=GREEN)
    s.draw_bar(102, 31, 96, 391, 38, "Sustain", "-25%", bar_color=ORANGE)
    s.rect(202, 31, 96, 391, fill=GREY_0A, stroke=GREY_22)
    s.text(250, 224, "Param 3", fill=GREY_44, size=11, anchor="middle")
    s.rect(302, 31, 96, 391, fill=GREY_0A, stroke=GREY_22)
    s.text(350, 224, "Param 4", fill=GREY_44, size=11, anchor="middle")

    # Selection waveform preview (columns 4..5)
    s.rect(402, 31, 196, 391, fill=GREY_0A, stroke=GREY_22)
    s.line(410, 226, 590, 226, stroke=GREY_22)
    s.text(412, 50, "Selection Preview", fill=GREY_77, size=11)
    s.path(wave_path_from_fn(412, 226, 176, 110, lambda t: math.exp(-t * 3.5) * math.sin(t * 60.0), samples=120),
           stroke=WHITE, stroke_width=2)

    effects = ["Reverse", "Normalize", "Fade In / Out", "Transient Shaper", "Analog Kick Gen", "Chiptune Voice Gen", "Multi-Mode Filter", "Stereo Delay"]
    s.draw_list(602, 31, 196, 391, "Effect List (.ptfx)", effects, 3, active=False)

    s.draw_bottom_buttons([
        btn(0, "Attack", "+45%"),
        btn(1, "Sustain", "-25%"),
        btn(2, "Param 3", "--", active=False),
        btn(3, "Param 4", "--", active=False),
        btn(4, "More", "•◦"),
        btn(5, "Back", ""),
        btn(6, "Preview", ""),
        btn(7, "Apply", ""),
    ], focus_cols={0})
    return s.render()


def gen_08b_sample_editor_presets():
    s = SvgScreen("Sample Editor", "Transient Shaper (.ptfx)")
    s.rect(2, 31, 596, 391, fill=GREY_0A, stroke=GREY_22)
    s.text(24, 68, "Automatic .ptp Effect Preset Management (Page 2: More ◦•)", fill=WHITE, size=14, weight="bold")
    s.text(24, 98, "Preset Folder:  /Effects/Presets/Transient Shaper/*.ptp", fill=CYAN, size=12, font=FONT_MONO)
    s.text(24, 134, "• [1] Load Preset   — Opens Global File Browser to load .ptp parameters & file paths", fill=GREY_77, size=12)
    s.text(24, 162, "• [2] Save Preset   — Saves current 1..12 parameter values and file strings", fill=GREY_77, size=12)
    s.text(24, 190, "• [3] Reset Params  — Restores plugin factory defaults cleanly", fill=GREY_77, size=12)

    effects = ["Reverse", "Normalize", "Fade In / Out", "Transient Shaper", "Analog Kick Gen", "Chiptune Voice Gen", "Multi-Mode Filter", "Stereo Delay"]
    s.draw_list(602, 31, 196, 391, "Effect List (.ptfx)", effects, 3, active=False)

    s.draw_bottom_buttons([
        btn(0, "Load", "Preset"),
        btn(1, "Save", "Preset"),
        btn(2, "Reset", "Params"),
        btn(3, "", ""),
        btn(4, "More", "◦•"),
        btn(5, "Back", ""),
        btn(6, "Preview", ""),
        btn(7, "Apply", ""),
    ], focus_cols={0, 4})
    return s.render()


def gen_09_global_file_browser():
    s = SvgScreen("Open Project", "/Projects/LiveSet_01")
    files = ["[..]", "[Backups]", "[LiveSet_01]", "[Ambient_Idea]", "Techno_Jam_v2", "SynthWave_Sketch"]
    s.draw_list(2, 31, 346, 391, "Explorer (Left = Jump to [..])", files, 2, active=True)

    # Details inspector pane (340 px)
    s.rect(352, 31, 346, 391, fill=GREY_0A, stroke=GREY_22)
    s.rect(353, 32, 344, 26, fill=GREY_22)
    s.text(364, 50, "Details", fill=WHITE, size=12, weight="bold")
    s.text(366, 86, "Project: LiveSet_01", fill=WHITE, size=13, weight="bold")
    s.text(366, 114, "Format: Version 6 (.mt + .mtp)", fill=CYAN, size=12)
    s.text(366, 142, "Patterns: 12   Instruments: 18", fill=GREY_77, size=12)
    s.text(366, 170, "Streamed Samples [S]: 3", fill=INSTR_YELLOW, size=12)
    s.text(366, 214, "Supports Project Open/Save As,", fill=GREY_57, size=11)
    s.text(366, 232, ".mod/.it Import, .ptfx Presets,", fill=GREY_57, size=11)
    s.text(366, 250, "and PTFX_PARAM_FILE selectors.", fill=GREY_57, size=11)

    s.draw_bar(702, 31, 96, 391, 64, "Preview Vol", "-4.00 dB")
    s.draw_bottom_buttons([
        btn(0, "Enter", ""),
        btn(1, "Delete", ""),
        btn(2, "Preview", ""),
        btn(3, "New Folder", ""),
        btn(4, "", ""),
        btn(5, "Cancel", ""),
        btn(6, "Preview Vol", "-4.00 dB"),
        btn(7, "Open", ""),
    ], focus_cols={7})
    return s.render()


def gen_10a_config_themes_fonts():
    s = SvgScreen("Config", "Community Edition 1.0.0")
    cats = ["Project Settings", "General", "MIDI", "Metronome", "Firmware", "Info"]
    s.draw_list(2, 31, 196, 391, "Categories", cats, 1, active=False, show_scroll=False)

    items = [
        "Radio region", "Display brightness", "Display theme", "Display font",
        "Pads brightness", "Pads preview", "Recording options", "Recording Threshold"
    ]
    vals = {0: "Europe", 1: "High", 2: "Cyber_CRT", 3: "Pixel_Sharp", 4: "Medium", 5: "Pattern", 6: "Micro+Vel", 7: "-12"}
    s.draw_list(202, 31, 396, 391, "General Settings", items, 2, active=True, right_tags=vals)

    s.rect(602, 31, 196, 391, fill=GREY_0A, stroke=GREY_22)
    s.rect(603, 32, 194, 26, fill=GREY_22)
    s.text(612, 50, "Description", fill=WHITE, size=12, weight="bold")
    s.text(612, 82, "Selects the color", fill=GREY_77, size=12)
    s.text(612, 100, "scheme of the UI.", fill=GREY_77, size=12)
    s.text(612, 136, "/Themes/*.json", fill=CYAN, size=11, font=FONT_MONO)
    s.text(612, 154, "(up to 29 themes)", fill=GREY_57, size=11)
    s.text(612, 184, "/Fonts/*.ptfont", fill=CYAN, size=11, font=FONT_MONO)
    s.text(612, 202, "(up to 30 fonts)", fill=GREY_57, size=11)

    s.draw_bottom_buttons([
        btn(0, "▲  Category  ▼", "[1]       [2]", span=2),
        btn(2, "", ""),
        btn(3, "▲       Item       ▼", "[4]             [6]", span=3),
        btn(6, "", ""),
        btn(7, "Enter", ""),
    ], focus_cols={7})
    return s.render()


def gen_10b_image_viewer():
    s = SvgScreen("Reference", "[1/48]")
    s.rect(2, 31, 796, 391, fill=GREY_0A, stroke=GREY_22)
    # Simulated reference cheat-sheet image card inside 800x390 area
    s.rect(44, 54, 712, 344, fill=GREY_12, stroke=GREY_37)
    s.text(400, 120, "SD Card Image Viewer (800 x 390 px Baseline JPEG)", fill=WHITE, size=16, anchor="middle", weight="bold")
    s.text(400, 154, "Config > Info  ->  /Images/Reference/ | /Images/Notes/ | /Images/Custom/", fill=CYAN, size=13, anchor="middle", font=FONT_MONO)
    s.line(90, 184, 710, 184, stroke=GREY_32)
    s.text(400, 224, "Navigate:  [1] <<   |   [8] >>   |   Left / Right   |   Jog Wheel   |   Pads 1..48", fill=INSTR_YELLOW, size=13, anchor="middle")
    s.text(400, 260, "Load up to 48 reference charts, chord tables, or live setlists per folder (1.jpg .. 48.jpg).", fill=GREY_77, size=12, anchor="middle")
    s.draw_bottom_buttons([
        btn(0, "<<", ""),
        btn(1, "", ""),
        btn(2, "", ""),
        btn(3, "", ""),
        btn(4, "", ""),
        btn(5, "", ""),
        btn(6, "", ""),
        btn(7, ">>", ""),
    ], focus_cols={0, 7})
    return s.render()


def gen_10c_performance_midi():
    s = SvgScreen("Performance FX", "MIDI Tracks 9-16 (Press [Perform] to Toggle)")
    slots = [
        ("Pat1", "Vol"), ("Pat2", "Pan"), ("Pat1", "Tun"), ("Pat4", "LPF"),
        ("Pat1", "HPF"), ("Pat1", "Bit"), ("Pat3", "Ovd"), ("Pat2", "Dly"),
        ("---", "Rev"), ("---", "Stp"), ("---", "Rep"), ("---", "Rnd"),
    ]
    for i, (val, fx_name) in enumerate(slots):
        sx = 11 + i * 65
        s.rect(sx, 31, 62, 391, fill=GREY_0A, stroke=GREY_22)
        s.rect(sx + 2, 35, 58, 22, fill=GREY_17)
        s.text(sx + 31, 51, f"Slot {i+1}", fill=GREY_77, size=10, anchor="middle")
        s.text(sx + 31, 102, val, fill=GREEN if val != "---" else GREY_44, size=12, anchor="middle", font=FONT_MONO)
        s.rect(sx + 4, 210, 54, 40, fill=GREY_12)
        s.text(sx + 31, 235, fx_name, fill=WHITE, size=12, anchor="middle", weight="bold")
        s.text(sx + 31, 305, "---", fill=GREY_37, size=11, anchor="middle", font=FONT_MONO)

    s.draw_bottom_buttons([
        btn(0, "MIDI 1", "On", bg=RED, top_color=WHITE, bot_color=WHITE, border=RED),
        btn(1, "MIDI 2", ""),
        btn(2, "MIDI 3", "On", bg=RED, top_color=WHITE, bot_color=WHITE, border=RED),
        btn(3, "MIDI 4", "Muted", bot_color=BLACK, bg=GREY_44),
        btn(4, "MIDI 5", ""),
        btn(5, "MIDI 6", "On", bg=RED, top_color=WHITE, bot_color=WHITE, border=RED),
        btn(6, "MIDI 7", ""),
        btn(7, "MIDI 8", ""),
    ])
    return s.render()


GENERATORS = {
    "01a_synth_page1.svg": gen_01a_synth_page1,
    "01b_synth_page2.svg": gen_01b_synth_page2,
    "02a_granular_cloud.svg": gen_02a_granular_cloud,
    "02b_slice_play_thru.svg": gen_02b_slice_play_thru,
    "03a_wt_env.svg": gen_03a_wt_env,
    "03b_wt_lfo.svg": gen_03b_wt_lfo,
    "04_mod1_mod2.svg": gen_04_mod1_mod2,
    "05a_pattern_mv.svg": gen_05a_pattern_mv,
    "05b_pattern_step_fx.svg": gen_05b_pattern_step_fx,
    "06a_master_drive_plus.svg": gen_06a_master_drive_plus,
    "06b_master_delay_frz.svg": gen_06b_master_delay_frz,
    "07_sample_loader_stream.svg": gen_07_sample_loader_stream,
    "08a_sample_editor_ptfx.svg": gen_08a_sample_editor_ptfx,
    "08b_sample_editor_presets.svg": gen_08b_sample_editor_presets,
    "09_global_file_browser.svg": gen_09_global_file_browser,
    "10a_config_themes_fonts.svg": gen_10a_config_themes_fonts,
    "10b_image_viewer.svg": gen_10b_image_viewer,
    "10c_performance_midi.svg": gen_10c_performance_midi,
}


def main() -> None:
    os.makedirs(OUT_DIR, exist_ok=True)
    for filename, fn in GENERATORS.items():
        svg_text = fn()
        ET.fromstring(svg_text)  # Validate well-formed XML
        out_path = os.path.join(OUT_DIR, filename)
        with open(out_path, "w", encoding="utf-8") as f:
            f.write(svg_text)
    print(f"Generated and validated {len(GENERATORS)} SVG mockups in {OUT_DIR}")


if __name__ == "__main__":
    main()
