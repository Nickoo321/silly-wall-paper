# Queued brief (Sonnet-sized): "Advanced" tab in the settings window
User (2026-09-17 23:20): "Most of these things don't need to be settings — maybe move to an advanced settings tab."
Goal: the everyday settings panel (src/settings.cpp) shows only the handful of knobs a person actually touches per look
(look/preset, HDR peak, brightness, colour speed, rise speed, mouse mode, grain/softness on-off), and EVERYTHING else
(the dozens of liquid_acid/ink/post fine-tune sliders added since 54780a0) moves to an "Advanced" tab (or a collapsible
section) that is collapsed by default. No behaviour change, ini keys unchanged; just UI grouping. Keep the tray menu as is.
Rules as every executor (build2 only, no live-exe touching, no %APPDATA%); verify by building (the settings window is
windowed — do NOT open it from an agent; describe the layout in the report and let the user check it live).
