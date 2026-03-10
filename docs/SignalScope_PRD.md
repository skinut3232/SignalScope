# SignalScope — Project Brief for Claude Code

## Project Overview

**SignalScope** is a free, open-source oscilloscope VST3 plugin for Windows, built with JUCE and C++. It is the first tool in a planned collection of free audio analysis tools for music producers — a suite aimed at filling the gap between expensive commercial metering tools and the low-quality free alternatives currently available.

The defining characteristic of SignalScope is its **visual quality**. Most free oscilloscopes look like test equipment from 1987. SignalScope's goal is to look beautiful — a refined CRT/phosphor aesthetic with smooth rendering, glow effects, and trace persistence. Think analog oscilloscope, but polished for a modern DAW.

This project also serves as a **learning vehicle** for the developer. The goal is to understand core audio and DSP concepts through building — not just to ship code. Please explain what code is doing as we go, especially DSP-related logic.

---

## Tech Stack

- **Framework**: JUCE (C++) — latest stable version
- **Build system**: Projucer + Visual Studio 2022 (Windows)
- **Plugin format**: VST3 (primary), AU optional later
- **Rendering**: JUCE Graphics initially, OpenGL (via JUCE's OpenGLRenderer) for Phase 4 visual effects
- **Platform**: Windows 10/11 (primary), macOS later
- **Language**: C++17

---

## Core Features

### Audio Engine
- Capture incoming audio buffer from the DAW host
- Support stereo input (display left, right, or mid/side)
- Adjustable time window (how many milliseconds of signal is shown)
- Trigger detection — lock the waveform so it doesn't scroll chaotically
  - Rising edge trigger (most common)
  - Falling edge trigger
  - Adjustable trigger level threshold

### Display
- Real-time waveform trace drawn on screen
- X axis = time, Y axis = amplitude (-1.0 to +1.0)
- Grid overlay with labeled divisions
- Clip indicator (subtle visual warning when signal hits 0dBFS)

### Visual Style (the differentiator)
- **Dark background** — deep near-black, not pure black
- **Phosphor glow effect** — the trace has a soft luminous bloom around it, like a real CRT
- **Trace persistence / afterglow** — when the signal moves, the old position fades out slowly rather than snapping off instantly. This is what makes CRT oscilloscopes look alive.
- **Single accent color** — a soft green, amber, or blue trace (user selectable)
- **Subtle scanline or glass texture overlay** — sells the analog aesthetic
- Clean, minimal UI chrome — the waveform display should be 80%+ of the plugin window

### Controls
- Time scale knob (zoom in/out on time axis)
- Trigger level control
- Channel select (L / R / Mid / Side / Sum)
- Color theme selector (green phosphor / amber / blue)
- Persistence amount knob (how long the afterglow lasts)

---

## Build Phases

### Phase 1 — Scaffolding (get something loading in the DAW)
- Create JUCE Audio Plugin project in Projucer
- Configure for VST3 on Windows
- Build successfully in Visual Studio
- Plugin loads in DAW (Ableton Live) with no errors
- Shows a black window — nothing else yet
- **Milestone**: plugin loads

### Phase 2 — Draw the signal
- Capture audio buffer in `processBlock()`
- Store samples in a circular buffer
- Draw samples as a polyline in `paint()`
- **Milestone**: waveform is visible, even if it scrolls chaotically

### Phase 3 — Stability and readability
- Implement rising-edge trigger detection so waveform locks in place
- Add time scale control
- Add basic grid / axis labels
- Add channel select
- **Milestone**: oscilloscope is actually usable and readable

### Phase 4 — Visual treatment (the main event)
- Implement phosphor glow using layered rendering or OpenGL shaders
- Implement trace persistence / afterglow using a fade buffer
- Add glass/scanline texture overlay
- Add color theme selector
- Polish all UI elements
- **Milestone**: visually impressive, shareable screenshots

### Phase 5 — Release prep
- Final bug fixes and performance pass
- Build release binary (not debug)
- Write a simple README
- Package for distribution
- **Milestone**: publicly shareable

---

## DSP Concepts to Learn Along the Way

As we build each feature, please explain the underlying concept:

- **Phase 1-2**: What is a plugin's `processBlock()`? What is an audio buffer? What is a sample?
- **Phase 2**: What is a circular buffer and why do we use one for audio?
- **Phase 3**: What is trigger detection? How does an oscilloscope decide where to "start" drawing?
- **Phase 4**: How does persistence/afterglow work technically? (accumulation buffer, alpha blending)
- **Phase 4**: What is OpenGL and when does it make sense vs JUCE's built-in graphics?

---

## Project Constraints

- **No audio processing** — this is analysis only. The plugin reads audio but does not modify it. `processBlock()` should pass audio through unchanged.
- **Real-time safe audio thread** — no memory allocation, no file I/O, no blocking calls on the audio thread. All heavy lifting happens on the message thread.
- **Performance** — the UI repaints at ~60fps. Keep paint() efficient.
- **Free and open source** — no licensing restrictions on dependencies

---

## File Structure (expected after Projucer setup)

```
SignalScope/
├── SignalScope.jucer          # Projucer project file
├── Source/
│   ├── PluginProcessor.h      # Audio engine — processBlock(), buffer management
│   ├── PluginProcessor.cpp
│   ├── PluginEditor.h         # UI — paint(), controls, OpenGL rendering
│   ├── PluginEditor.cpp
│   └── OscilloscopeComponent.h/cpp  # (Phase 3+) Dedicated display component
├── Builds/
│   └── VisualStudio2022/      # Generated by Projucer, do not edit manually
└── README.md
```

---

## Longer-Term Vision (context only, not current scope)

SignalScope is the first tool in a free producer toolkit called **SignalFlow Tools** (working name). Future tools in the collection may include:

- LUFS Meter
- Vectorscope / Stereo Field Display
- Peak / RMS Meter
- Phase Correlation Meter
- Dynamic Range Meter

Each tool will share the same visual language and be distributed as a collection. The goal is to build a genuine community resource for producers who can't afford expensive metering suites.

---

## How to Work With Me

- I am a beginner-to-intermediate programmer. Python is my stronger language; C++ is new to me.
- Please explain JUCE and C++ patterns as we go — don't assume prior knowledge
- When writing DSP-related code, explain what it's doing conceptually, not just syntactically
- Prefer clear, readable code over clever code
- Build incrementally — get each phase working before moving to the next
- If something could go wrong with the build setup, flag it proactively

---

*Last updated: March 2026*
