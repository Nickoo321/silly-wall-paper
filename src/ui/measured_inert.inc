// Keys MEASURED byte-identical on a given preset (tools/key-effect.ps1 reports), whose inertness
// no key gate in keys.inc can express (the gate is data-dependent, e.g. the haloInk
// ink-brightness read, or was never traced). While that preset is the active one the row is
// disabled with the report as its reason; "Show everything" unlocks it. Structural gates
// (ink_water, droplets, film_grain, lid ...) live in keys.inc, not here.
//
// MEASURED_INERT(preset stem, section, key, evidence)
MEASURED_INERT("acid-rise-12", "liquid_acid", "rim_vary",       "key-effect BJ @1d2786a: byte-identical (haloInk gate, black ink surround)")
MEASURED_INERT("acid-rise-12", "liquid_acid", "rim_ink_follow", "key-effect BJ @1d2786a: byte-identical (haloInk gate, black ink surround)")
MEASURED_INERT("acid-rise-12", "liquid_acid", "rim_dark",       "key-effect BJ @1d2786a forced 0: byte-identical (haloInk gate)")
MEASURED_INERT("acid-rise-12", "liquid_acid", "meniscus",       "key-effect BJ @1d2786a forced 0: byte-identical (haloInk gate)")
MEASURED_INERT("acid-rise-12", "liquid_acid", "oil_ink_blur",   "key-effect BJ @1d2786a: byte-identical (gate not traced)")
MEASURED_INERT("acid-rise-12", "liquid_acid", "oil_dye_block",  "key-effect BJ @1d2786a: byte-identical (gate not traced)")
MEASURED_INERT("acid-rise-12", "post",        "lid_sheen_px",   "key-effect BJ @1d2786a: byte-identical (gate not traced)")
