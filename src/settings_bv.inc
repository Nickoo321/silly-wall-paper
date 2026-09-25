// settings_bv.inc -- brief BV (colour schemes) slider rows, included INSIDE the
// slider table in settings.cpp right after "third hue amount". Kept in its own
// file because branch ui1 replaces settings.cpp: the ui1 port copies these rows
// (label, min, max, step, decimals, field, section, key, help) into its own
// table instead of fighting a modify/delete merge conflict.
// Ranges from the A/B sheets in build2\shots\live\bv\ (slider-range-policy).
        { L"  third hue share",                 0,     1,   0.05f, 2, &c.acid.filmHue3Share, nullptr, L"liquid_acid", L"film_hue3_share", false, nullptr, 3, L"How much of the frame the third hue takes. 1 = as before (about 18%), 0.75 about 13%, 0.5 about 8%, 0 about 3%. Keep a bright yellow third small: a big yellow patch pushes the panel's ABL" },
        { L"Film equal load",                   0,     1,   0.05f, 2, &c.acid.filmEqualLoad, nullptr, L"liquid_acid", L"film_equal_load", false, nullptr, 3, L"Dims a yellow/green/cyan FILM to the brightness the same colour has at magenta, so every hue loads the panel alike (no blinding yellow). Patches are not touched. 0 = off" },
        { L"  second hue yellow lift",          0,     1,   0.05f, 2, &c.acid.filmHue2Lift, nullptr, L"liquid_acid", L"film_hue2_lift", false, nullptr, 3, L"Keeps a gold/amber/yellow/lime SECOND-hue patch bright (value up, a little saturation) so it never goes olive. Only patch pixels in the yellow band. 0 = off" },
        { L"  third hue yellow lift",           0,     1,   0.05f, 2, &c.acid.filmHue3Lift, nullptr, L"liquid_acid", L"film_hue3_lift", false, nullptr, 3, L"The same lift for the THIRD hue's patches. 0 = off" },
        { L"Hue rotation: linger at anchors",   0,     1,   0.05f, 2, &c.acid.hueAnchorWeight, nullptr, L"liquid_acid", L"hue_anchor_weight", false, nullptr, 3, L"Warps the hue rotation's clock so it lingers at the proven hues (magenta most, then blue, red, violet) and glides faster through lime/mustard/olive. Needs Hue rotation on. 0 = even speed" },
