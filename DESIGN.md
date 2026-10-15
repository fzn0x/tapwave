---
name: Tapwave web UI
description: The setup and status pages a Tapwave box serves to a phone over its own Wi-Fi.
colors:
  apricot-ground: "#fff3e8"
  apricot-sunken: "#f8eadc"
  card-white: "#ffffff"
  line-soft: "#efdfcf"
  line-strong: "#dcc6b0"
  ink: "#2b241f"
  ink-soft: "#6b5e55"
  action-orange: "#d4421a"
  action-orange-hover: "#c03a14"
  action-orange-press: "#a93311"
  action-orange-text: "#c03a14"
  on-action: "#ffffff"
  action-tint: "#fde3d6"
  ok-green: "#276b43"
  ok-tint: "#e3f2e7"
  warn-amber: "#8a5300"
  warn-tint: "#fcefd6"
  bad-red: "#b42318"
  bad-tint: "#fde8e4"
  calm-tint: "#f6ebe0"
  ripple-peach: "#f3b896"
  dark-ground: "#1f1915"
  dark-sunken: "#18130f"
  dark-card: "#2a221d"
  dark-line-soft: "#3a2f28"
  dark-line-strong: "#4d4037"
  dark-ink: "#f5ebe1"
  dark-ink-soft: "#bcae9f"
  dark-action-orange: "#ff8a5c"
  dark-action-orange-hover: "#ff9c74"
  dark-action-orange-press: "#f07a4c"
  dark-action-orange-text: "#ff9c74"
  dark-on-action: "#1f1915"
  dark-action-tint: "#4a2b1d"
  dark-ok-green: "#7fd3a0"
  dark-ok-tint: "#1f3628"
  dark-warn-amber: "#f2c26b"
  dark-warn-tint: "#3d2f17"
  dark-bad-red: "#ff9a8a"
  dark-bad-tint: "#43211c"
  dark-calm-tint: "#33291f"
  dark-ripple: "#8c4a2c"
typography:
  display:
    fontFamily: "Nunito, ui-rounded, SF Pro Rounded, system-ui, -apple-system, Segoe UI, sans-serif"
    fontSize: "clamp(1.9rem, 1.35rem + 2.2vw, 2.75rem)"
    fontWeight: 800
    lineHeight: 1.2
    letterSpacing: "-0.02em"
  card-name:
    fontFamily: "Nunito, ui-rounded, SF Pro Rounded, system-ui, -apple-system, Segoe UI, sans-serif"
    fontSize: "clamp(1.45rem, 1.05rem + 1.9vw, 2.5rem)"
    fontWeight: 800
    lineHeight: 1.15
  headline:
    fontFamily: "Nunito, ui-rounded, SF Pro Rounded, system-ui, -apple-system, Segoe UI, sans-serif"
    fontSize: "1.3rem"
    fontWeight: 800
    lineHeight: 1.2
    letterSpacing: "-0.01em"
  title:
    fontFamily: "Nunito, ui-rounded, SF Pro Rounded, system-ui, -apple-system, Segoe UI, sans-serif"
    fontSize: "1.06rem"
    fontWeight: 800
    lineHeight: 1.2
    letterSpacing: "-0.01em"
  body:
    fontFamily: "Nunito, ui-rounded, SF Pro Rounded, system-ui, -apple-system, Segoe UI, sans-serif"
    fontSize: "17px"
    fontWeight: 500
    lineHeight: 1.5
    fontFeature: "lnum"
  body-small:
    fontFamily: "Nunito, ui-rounded, SF Pro Rounded, system-ui, -apple-system, Segoe UI, sans-serif"
    fontSize: "0.9rem"
    fontWeight: 500
    lineHeight: 1.5
  label:
    fontFamily: "Nunito, ui-rounded, SF Pro Rounded, system-ui, -apple-system, Segoe UI, sans-serif"
    fontSize: "0.82rem"
    fontWeight: 800
    lineHeight: 1.5
  nav-label:
    fontFamily: "Nunito, ui-rounded, SF Pro Rounded, system-ui, -apple-system, Segoe UI, sans-serif"
    fontSize: "0.78rem"
    fontWeight: 700
    lineHeight: 1.5
rounded:
  control: "14px"
  icon-tile: "14px"
  address: "16px"
  row: "18px"
  banner: "20px"
  nav-item: "20px"
  done-mark: "22px"
  card: "24px"
  nav-bar: "26px"
  pill: "999px"
spacing:
  xxs: "4px"
  xs: "6px"
  sm: "8px"
  md: "12px"
  lg: "16px"
  form: "18px"
  page: "20px"
  card-pad: "24px"
  xl: "28px"
  panel-desktop: "36px"
components:
  button-primary:
    backgroundColor: "{colors.action-orange}"
    textColor: "{colors.on-action}"
    typography: "{typography.body}"
    rounded: "{rounded.control}"
    padding: "0 22px"
    height: "48px"
  button-primary-hover:
    backgroundColor: "{colors.action-orange-hover}"
  button-primary-active:
    backgroundColor: "{colors.action-orange-press}"
  button-primary-small:
    backgroundColor: "{colors.action-orange}"
    textColor: "{colors.on-action}"
    rounded: "{rounded.control}"
    padding: "0 16px"
    height: "40px"
  button-secondary:
    backgroundColor: "transparent"
    textColor: "{colors.ink}"
    rounded: "{rounded.control}"
    padding: "0 22px"
    height: "48px"
  button-secondary-hover:
    backgroundColor: "{colors.apricot-ground}"
  button-quiet:
    backgroundColor: "{colors.calm-tint}"
    textColor: "{colors.ink}"
    rounded: "{rounded.control}"
    padding: "0 22px"
    height: "48px"
  button-quiet-hover:
    backgroundColor: "{colors.line-soft}"
  button-danger:
    backgroundColor: "{colors.bad-red}"
    textColor: "{colors.on-action}"
    rounded: "{rounded.control}"
    padding: "0 22px"
    height: "48px"
  button-disabled:
    backgroundColor: "{colors.apricot-sunken}"
    textColor: "{colors.ink-soft}"
    rounded: "{rounded.control}"
  card:
    backgroundColor: "{colors.card-white}"
    rounded: "{rounded.card}"
    padding: "24px"
  list-row:
    backgroundColor: "transparent"
    rounded: "{rounded.row}"
    padding: "11px 12px"
  list-row-hover:
    backgroundColor: "{colors.apricot-ground}"
  icon-tile:
    backgroundColor: "{colors.calm-tint}"
    textColor: "{colors.ink}"
    rounded: "{rounded.icon-tile}"
    size: "44px"
  chip:
    backgroundColor: "{colors.calm-tint}"
    textColor: "{colors.ink-soft}"
    typography: "{typography.label}"
    rounded: "{rounded.pill}"
    padding: "4px 10px"
  chip-ok:
    backgroundColor: "{colors.ok-tint}"
    textColor: "{colors.ok-green}"
  chip-warn:
    backgroundColor: "{colors.warn-tint}"
    textColor: "{colors.warn-amber}"
  chip-bad:
    backgroundColor: "{colors.bad-tint}"
    textColor: "{colors.bad-red}"
  banner-warn:
    backgroundColor: "{colors.warn-tint}"
    textColor: "{colors.ink}"
    rounded: "{rounded.banner}"
    padding: "16px 18px"
  banner-bad:
    backgroundColor: "{colors.bad-tint}"
    textColor: "{colors.ink}"
    rounded: "{rounded.banner}"
    padding: "16px 18px"
  banner-calm:
    backgroundColor: "{colors.calm-tint}"
    textColor: "{colors.ink}"
    rounded: "{rounded.banner}"
    padding: "16px 18px"
  input:
    backgroundColor: "{colors.card-white}"
    textColor: "{colors.ink}"
    rounded: "{rounded.control}"
    padding: "0 16px"
    height: "50px"
  nav-bar:
    backgroundColor: "{colors.card-white}"
    rounded: "{rounded.nav-bar}"
    padding: "6px"
  nav-item:
    backgroundColor: "transparent"
    textColor: "{colors.ink-soft}"
    typography: "{typography.nav-label}"
    rounded: "{rounded.nav-item}"
    height: "56px"
  nav-item-active:
    backgroundColor: "{colors.action-tint}"
    textColor: "{colors.action-orange-text}"
  toast:
    backgroundColor: "{colors.ink}"
    textColor: "{colors.apricot-ground}"
    rounded: "{rounded.pill}"
    padding: "12px 20px"
---

# Design System: Tapwave web UI

This file describes the web UI in `ui/` as it is built. The source of truth is the code: custom properties at the top of `ui/app.css`, components in `ui/app.js`, and every visible string in `ui/lib/i18n.js`. If this file and the code disagree, fix whichever one is wrong in the same change.

## Overview

**Creative North Star: "The Warm Consumer App"**

Tapwave is set up by a parent on a phone, often while connected to the box's own setup Wi-Fi. The UI reads like a friendly app, not a router admin page: soft white cards resting on a warm apricot ground, a rounded humanist sans at comfortable sizes, and plain sentences. It deliberately avoids the grey device dashboard of status tiles and dense tables.

There is one saturated colour, a warm orange, and it marks things you can press. State (working, needs attention, broken) is carried by soft tinted fills in green, amber and red, always paired with words, so it never competes with the orange. A warm dark variant follows the system setting for evenings.

The signature move is the **reader ripple**: when a card arrives, the reader glyph on Home sends out one soft ring, the card's text settles into fixed cells, and the glyph turns green while the card stays on the reader. Live changes never move the layout.

**Key Characteristics:**
- Warm tinted ground, white cards with large soft corners (24px), two-layer warm shadows.
- One orange for actions only; states are tinted fills plus words.
- Nunito, self-hosted, weights 400 to 800, body at 17px.
- Fixed cells for anything that updates live, so text changes in place.
- One state vocabulary for every control: rest, hover, pressed, focus, disabled, busy.
- Light and dark from `prefers-color-scheme`; reduced motion respected everywhere.
- Code-led: all icons and the reader glyph are inline SVG drawn for this UI. There are no photographs or generated raster images. The only binary asset is the Nunito subset (`ui/fonts/nunito.woff2`, SIL Open Font License).

## Colors

A warm, low-contrast apricot world with one loud orange and three soft state tints. Every colour has a light and a dark value; both are defined as custom properties on `:root` in `ui/app.css`, with the dark set inside `@media (prefers-color-scheme: dark)`. Components always use the custom properties, never raw hex.

### Primary
- **Action Orange** (`--action`): fills primary buttons, the checked switch track, range sliders, the focus ring, the text caret and the brand mark (which links home). Hover and pressed steps (`--action-hover`, `--action-press`) darken it in light mode and lighten it in dark mode.
- **Action Orange Text** (`--action-ink`): the orange used for text and line icons: links, link-buttons, the active tab label, a health row's action line and its chevron. It is one step deeper than the fill so small text keeps its contrast on white.
- **Action Tint** (`--action-tint`): the pill behind the active tab, the focus halo on inputs, and text selection.
- **On Action** (`--on-action`): text on an orange fill. White in light mode, near-black ground in dark mode, because the dark orange is light.

### Secondary (state)
- **Ok Green** (`--ok`, `--ok-tint`): working. Icon tiles and chips for healthy things, the "on reader" chip, the reader glyph while a card is present, setup stepper progress, progress bars, the setup done mark.
- **Warn Amber** (`--warn`, `--warn-tint`): needs attention but still working. Restart-needed and config banners, warning chips and problem lines.
- **Bad Red** (`--bad`, `--bad-tint`): broken or destructive. Error banners, invalid field borders, error problem lines, the erase-confirmation button.

### Neutral
- **Apricot Ground** (`--ground`): the page. Also the hover fill for list rows and secondary buttons, and the inset fill for addresses and record lists inside cards.
- **Apricot Sunken** (`--sunken`): disabled buttons and inputs, the empty part of progress bars.
- **Card White** (`--card`): cards, the tab bar, the live pill, inputs.
- **Calm Tint** (`--calm-tint`): the neutral tone. Default icon tiles, neutral chips, quiet buttons, calm banners, the reader glyph's resting tile.
- **Line Soft / Line Strong** (`--line`, `--line-strong`): row dividers and the last-card footer rule; input and secondary-button borders, the switch's off track, scrollbars.
- **Ink / Ink Soft** (`--ink`, `--ink-2`): text. Ink Soft is for descriptions, hints, timestamps and inactive tabs.
- **Ripple Peach** (`--ripple`): only the expanding ring of the reader ripple. It is a pale, non-saturated relative of the orange and never fills a surface.

### Named Rules
**The One Orange Rule.** A saturated orange fill means "press this". Each view has at most one filled orange button for its main action; other buttons are secondary (outlined), quiet (calm tint) or danger (red, only after a confirmation step). Orange text marks a link or the action a row leads to.

**The Words With Tints Rule.** A state colour is never the only signal. Every tinted tile, chip or banner has text next to it or, for a health row's tile, a visually hidden state sentence.

## Typography

**Display Font:** Nunito (with ui-rounded, SF Pro Rounded, system-ui fallbacks)
**Body Font:** Nunito, same stack
**Label/Mono Font:** none. IDs and numbers use Nunito with tabular figures.

**Character:** one rounded humanist family carries everything, from extra-bold headings to medium-weight body. Weight, not a second family, makes the hierarchy.

The face is self-hosted as a variable subset at `/fonts/nunito.woff2` (weights 400 to 800, `font-display: swap`) and preloaded from `index.html`. The UI never loads fonts from the network.

### Hierarchy
- **Display** (800, fluid 1.9rem to 2.75rem, 1.2): the page heading (`h1`) on every page.
- **Card name** (800, fluid 1.45rem to 2.5rem, 1.15): the last card's name or ID in the Home panel. Wraps anywhere so long UIDs never overflow.
- **Headline** (800, 1.3rem, 1.2): section headings (`h2`) inside cards.
- **Title** (800, 1.06rem, 1.2): sub-headings (`h3`). Row titles, labels and legends use body size at 800.
- **Body** (500, 17px, 1.5): everything else. Paragraphs cap at 68ch.
- **Small** (500, 0.9rem to 0.92rem): hints, row details, the row action line (at 800), problem lines (at 700).
- **Label** (800, 0.82rem): chips. Tab labels are 700 at 0.78rem on mobile and 1rem on desktop. No uppercase anywhere.

### Named Rules
**The Tabular Numbers Rule.** IDs, timestamps, versions and facts use `font-variant-numeric: tabular-nums` so live values do not jitter as digits change. Body text uses lining figures.

**The Sentence Case Rule.** Headings, buttons, tabs and chips are written in sentence case. Letter-spacing is only ever negative and only on headings (-0.01em, -0.02em on display).

## Layout

Mobile first. A single column inside `.main` (max 1160px, 16px side padding) with a sticky translucent top bar (box name and live pill) and a floating bottom tab bar. The main area reserves the tab bar's height (72px) plus the safe-area inset so nothing hides under it.

At **900px** the shell becomes a two-column grid: a 248px left column holding the navigation as a sticky vertical rail, and the content column with 32px padding. On Home, the last-card panel and the health list sit side by side (1.35fr and 1fr). At **640px** two-field form rows and the last-card footer go horizontal.

Setup and sign-in use a narrow solo column (max 560px); long settings pages use 44rem.

Spacing steps are 4, 6, 8, 12, 16, 18, 20, 24, 28 and 36px. Pages stack their blocks 20px apart, cards pad 24px, stacks inside cards use 16px, and forms use 18px between fields. Sections reached by a link land below the sticky header (`scroll-margin-top`).

**The Fixed Cells Rule.** Anything that updates live sits in a cell sized before the value arrives. The last-card footer uses fixed columns (8.75rem for the state chip, 16ch for the time) so a new time or state never shifts its neighbours or changes the line count.

## Elevation & Depth

Soft and lifted. Cards float on the ground with a warm, two-layer shadow tinted with brown in light mode and plain black in dark mode. There are no borders around cards; depth comes from the shadow and the white-on-apricot contrast.

### Shadow Vocabulary
- **Rest** (`--shadow`: `0 1px 2px rgb(92 54 22 / 6%), 0 10px 28px -6px rgb(92 54 22 / 12%)`): cards, the live pill, the skip link.
- **Lift** (`--shadow-lift`: `0 2px 4px rgb(92 54 22 / 8%), 0 16px 36px -8px rgb(92 54 22 / 18%)`): things that float over content: the tab bar and the toast.
- **Switch knob** (`0 1px 3px rgb(0 0 0 / 20%)`): the white knob inside the toggle only.

The top bar uses a 92% ground fill with `backdrop-filter: saturate(1.4) blur(12px)` instead of a shadow.

### Named Rules
**The Two Shadows Rule.** Use Rest for anything that sits on the page and Lift for anything that floats above it. Do not add new shadow values.

## Shapes

Generous, soft corners, scaled to the size of the object: 24px cards, 26px on the tab bar, 20px banners and tab pills, 18px list rows, 14px controls and icon tiles, and full pills for chips, the live indicator, the toast, progress bars and the stepper. The reader glyph's tile uses a 30% radius so it reads as a soft squircle at both its sizes (60px mobile, 116px desktop).

Icons are 24-unit inline SVG line drawings with a 1.9 stroke and round caps and joins, drawn for this UI and kept in the `ICONS` table in `ui/app.js`. Rows use 22px icons inside 44px tiles; buttons use 20px.

## Components

Components are plain DOM built with the `h()` helper in `ui/app.js`. Every interactive element shares one state vocabulary: rest, hover, pressed (a small spring scale), focus (a 3px orange outline offset 3px), disabled, and busy.

### Buttons
- **Shape:** gently rounded rectangle (14px), 48px tall, 22px side padding, 800 weight, optional 20px leading icon. The small size is 40px tall with 16px padding.
- **Primary:** orange fill with on-action text. Hover deepens, pressed deepens again and scales to 0.97 on the `--spring` curve.
- **Secondary:** transparent with a 2px Line Strong border; hover fills with ground and darkens the border.
- **Quiet:** calm-tint fill, for cancel and back.
- **Danger:** red fill with white text. Only used on the confirmation row after an explicit "erase" step.
- **Disabled:** sunken fill, soft ink, no scale. **Busy** (`aria-busy="true"`): disabled, 85% opacity and a 16px spinning ring after the label.
- **Link button:** orange text, 800, underlined with a 2px line offset 3px.

### Chips
- **Style:** full pill, 4px by 10px, 0.82rem at 800, calm tint with soft ink by default.
- **State:** `data-tone="ok" | "warn" | "bad"` swaps to the matching tint and colour. Chips are never pressable and never orange.

### Cards / Containers
- **Corner Style:** 24px.
- **Background:** Card White.
- **Shadow Strategy:** Rest.
- **Border:** none.
- **Internal Padding:** 24px, or 6px around a list so rows can reach the edges.

### Lists and health rows
A list is a card with 6px padding and 1px Line Soft dividers between items. Each row is a three-column grid: a 44px icon tile, a text stack (800 title, small soft-ink detail), and an end slot with a 20px chevron at the edge. Rows that link are fully pressable: ground fill on hover, scale 0.985 on press.

Health rows on Home tint the icon tile by state (ok, warn, bad) and include a visually hidden state sentence. When a row needs the user to do something, it adds an orange **action line** under the detail that says what to do, and the edge chevron turns orange; the whole row leads to the place where it is fixed.

### Last-card panel (signature)
A card on Home that shows the most recent tap. On mobile the reader glyph (60px) sits left of the card name, with the ID and its meaning below. On desktop the glyph grows to 116px and spans the title and body rows.

The footer sits under a 1px Line Soft rule and is a grid of fixed cells: a state chip ("On the reader" in ok tone, or neutral when lifted off), the relative time in tabular figures, and Copy ID as the one orange button. Below 640px Copy ID drops to its own row.

Motion when a card arrives: the glyph's ring (3px Ripple Peach) scales from 0.9 to 1.9 and fades over 0.9s on `--ease`; the title and body settle up 8px over 0.5s. While waiting for a first card the ring pulses slowly (2.6s). While a card is present the glyph's waves and tile turn ok green. The panel is `aria-live="polite"`.

### Inputs / Fields
- **Style:** 50px tall, 16px side padding, 2px Line Strong border, 14px radius, card fill. Labels above at 800, hints below in small soft ink.
- **Focus:** the border turns orange with a 4px Action Tint halo (no outline).
- **Error / Disabled:** an invalid field gets a red border and a problem line (18px alert icon plus text, red, 700) that says what to change. Disabled inputs use the sunken fill.
- **Switch:** 56 by 34px pill, off track in Line Strong, on track in orange, a white 26px knob that slides 22px on `--spring`. A real checkbox sits on top for keyboard and screen readers.
- **Advanced sections:** a `details` element with a rotating chevron summary (44px tall), so technical settings stay one tap away without being in the way.

### Banners
A 20px-radius tinted block with a 22px icon and a body that can hold a short heading, a sentence and one small button. Tones: warn (default), bad (`role="alert"`) and calm (for "coming in a later update" notes). A banner always ends with what to do next.

### Stepper and progress
The setup stepper is three 6px pills (max 168px wide) that fill ok green as steps complete, labelled for screen readers as "Step n of 3". Progress bars are 10px pills on a sunken track, filling ok green with a scaled inner bar.

### Navigation
- **Mobile:** a floating tab bar 12px from the bottom and sides, card fill, Lift shadow, 26px radius. Each tab is an icon over a 0.78rem label, at least 56px tall. The current tab gets an Action Tint pill and orange text; tabs scale to 0.94 when pressed.
- **Desktop (900px and up):** the same element becomes a sticky vertical rail in the left column, rows 50px tall with the icon beside a 1rem label.
- The top bar shows the brand mark, the box name (ellipsised) and a live pill whose dot is ok green when connected and amber otherwise.

### Toast
An ink pill with ground-coloured text, fixed above the tab bar, that rises in on `--spring` and leaves after 2.4s. Used for short confirmations such as "Copied".

## Do's and Don'ts

### Do:
- **Do** use the custom properties in `ui/app.css` for every colour, shadow, radius and curve, so dark mode keeps working.
- **Do** keep one filled orange button per view, for its main action; use secondary or quiet for the rest.
- **Do** pair every state tint with words.
- **Do** give live values fixed cells and tabular figures so updates never move the layout.
- **Do** use `--spring` for press and slide-in feedback and `--ease` for everything else.
- **Do** put technical detail behind an Advanced section instead of a separate page.
- **Do** end every error or warning with the next step, in `ui/lib/i18n.js`.

### Don't:
- **Don't** use orange for state, decoration or non-pressable surfaces; green, amber and red tints carry state.
- **Don't** add borders around cards or new shadow values; use Rest or Lift.
- **Don't** use uppercase labels, small caps or tracked-out text.
- **Don't** load anything from the network: no CDN fonts, scripts or images.
- **Don't** use text characters as icons; draw an SVG path in the icon table.
- **Don't** show a destructive red button until the user has asked for the destructive step on the page.

## Motion

Two curves, both custom properties:
- **`--ease`** (`cubic-bezier(0.16, 1, 0.3, 1)`): a fast, soft deceleration for colour changes, page entry (0.35s, 6px rise), settling text and the ripple.
- **`--spring`** (`cubic-bezier(0.3, 1.18, 0.6, 1)`): a deliberate small overshoot used only for press feedback and things that slide into place: button and row press scale, tab press, the switch knob and the toast. It is the world's soft press; do not use it for colour or opacity.

Under `prefers-reduced-motion: reduce` all animations and transitions collapse to near zero and the waiting pulse is removed entirely.

## Copy

- Calm and plain, for an adult reader. Short sentences, no jargon on main screens, never childish, no marketing language.
- Sentence case everywhere. No em-dashes.
- Every problem says what happened and what to do next, for example "The box did not answer. Check that your phone is on the same Wi-Fi, then try again."
- Pages for later milestones say what is coming, with no fake controls.
- Every visible string lives in `ui/lib/i18n.js` and is read through `t()`. Do not put literal copy in `app.js`. To add a language, copy the `en` block and keep the `{placeholders}`.

## Accessibility

The target is WCAG 2.2 AA in both light and dark mode.
- Text and action colours are chosen per mode for contrast; the orange used for text (`--action-ink`) is darker than the fill in light mode for that reason.
- Every control is at least 44px tall (buttons 48px, inputs 50px, tabs 56px, small buttons 40px with room around them).
- Visible focus on everything: a 3px orange outline offset 3px, or the orange border and halo on inputs.
- A skip link, a focusable `main` that receives focus on navigation, labels on every field, `role="status"` or `role="alert"` on banners and the live pill, and `aria-live` on the toast and last-card panel.
- State is never colour alone (see The Words With Tints Rule).
- Reduced motion is honoured globally.

## Size budget

The UI ships gzipped from the device's LittleFS partition. `scripts/build_ui.py` gzips `ui/` and fails the build if the total is over **96 KiB** (`--budget-kib`, default 96), so the UI fits the filesystem on 4 MB boards. Keep it plain HTML, CSS and JavaScript modules with no framework and no runtime dependencies, add icons as SVG paths to the existing table, and do not add raster images or more font files without checking the budget.
