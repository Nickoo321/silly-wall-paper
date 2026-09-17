# Liquid Acid — web research (2026-09-17)

Research pass for the oil-on-inked-water end goal (`style=liquid_acid`). Sources are web
search / fetch only; no files downloaded, no code touched. Everything the creator himself
says is marketing copy — the physics section is reconstructed from the wider macro-liquid
and liquid-light-show literature, and is flagged as such where it is inference.

---

## 1. Creator & pack

**Steven McFarlane Design** (`stevenmcfarlanedesign` on Instagram/Facebook/YouTube) is a
one-person shop selling "analog" texture and loop packs to motion designers, filmmakers and
VJs — VHS Textures, CRT Textures, Super 8mm Film Textures, Minimal Noise Loops, and
**Liquid Acid Visuals**. Everything is Shopify-hosted at `stevenmcfarlane.design`.

**Liquid Acid Visuals (Project #001)** — £29.99, or £49.99 inside "The Complete Collection".
Product facts from the official page:

- 52 liquid loops, 25 of them seamless, 10+ long loops (20 s+)
- 4K (3840×2160), Apple ProRes 422, `.mov`
- 20+ still liquid JPGs bundled
- Pitched for "backgrounds, live shows & animated posters", VJ use, and as luma mattes for
  liquid transitions

The only production claim anywhere on the site is that the loops are *"created from real
liquid acid"* — which is brand voice, not method. **There is no BTS, breakdown, tutorial,
interview transcript or blog post from him describing the actual shoot.** I checked:

- the product page and the Shopify product JSON (one preview asset only)
- the site's other pages (no About / process page)
- his YouTube channel and the trailer page (fetch returns YouTube's SPA shell, no metadata)
- the Motion Science TV podcast page — it only says he talks about "textures, color, and
  technology" for "#analog #motiongraphics"; the substance is inside the video, which I
  cannot transcribe with these tools
- Behance / Vimeo — no profile found under that name for this work

Facebook carries four video posts of the same trailer; Instagram and Facebook video pages
are login-walled, so they are listed below but may not open cleanly on the phone.

So: **the technique is undocumented by the creator.** Section 3 is genre reconstruction.

---

## 2. Reference image URLs

All verified to return HTTP 200 with an image content type (HEAD request only, nothing
downloaded). Phone-friendly — direct image or thumbnail URLs first.

| URL | What it shows | Palette |
| --- | --- | --- |
| `https://cdn.shopify.com/s/files/1/0500/3322/9978/products/stevenmcfarlanedesign-liquid-acid-visuals-compressed_4fb076e1-66fa-4ae2-a906-f63c9ad21eac.gif` | **Best single reference.** The official animated product preview — a moving multi-panel montage of the pack. Shows how the discs actually drift and merge, and how fast the ink underneath moves relative to the oil (oil is slow, ink is fast). 343 KB, plays inline on a phone. | mixed; cyan/magenta/orange |
| `https://cdn.shopify.com/s/files/1/0500/3322/9978/files/stevenmcfarlanedesign-liquidacid.jpg` | Trailer poster frame used on the site. Full-frame still, heavy oil coverage, web/filament topology with a dense hole swarm. | teal + magenta + orange |
| `https://i.ytimg.com/vi/kClNPciY9aU/maxresdefault.jpg` | YouTube trailer thumbnail, 1280×720. Cleanest large still of the pack's "hero" frame. | as above |
| `https://www.youtube.com/watch?v=kClNPciY9aU` | The trailer itself (1 min-ish). The only way to see motion characteristics at full quality. | — |
| `https://www.stevenmcfarlane.design/products/liquid-acid-visuals` | Shop page; the GIF above is embedded and autoplays. | — |
| `https://www.facebook.com/stevenmcfarlanedesign/videos/822695418459642/` | Trailer repost #1 (login wall likely). | — |
| `https://www.facebook.com/stevenmcfarlanedesign/videos/289279828875558/` | Trailer repost #2, caption includes the old `/downloads/liquid-acid-visuals` URL. | — |
| `https://www.instagram.com/stevenmcfarlanedesign/` | The account the user's screenshots came from; the reel is an ad, so it may not be on the grid. | — |
| `https://i.ytimg.com/vi/CAd5sxSWLCg/maxresdefault.jpg` | Thumbnail for "Liquid Light Show Oil and Water Clock Face Technique — 1080 HD". Genre reference: the 1960s projector look, same flat discs + rims but lower saturation. | red/yellow on blue |
| `https://i.ytimg.com/vi/VOJxC1fLnEY/maxresdefault.jpg` | Thumbnail for a milk/paint/oil macro tutorial. Shows the *cell* topology (rings within rings) rather than free discs. | white/rainbow |
| `https://upload.wikimedia.org/wikipedia/commons/thumb/e/ed/Personal_Bubble_16x10_%2848870410266%29.jpg/1280px-Personal_Bubble_16x10_%2848870410266%29.jpg` | CC-licensed macro of backlit oil/bubble spheres on a coloured ground — good for studying rim brightness vs. disc interior. | iridescent on dark |
| `https://www.pexels.com/photo/colorful-oil-and-water-abstract-photography-29142071/` | Free stock, oil discs on a coloured light panel. Shows the *thin bright rim = background colour* effect clearly. | multi |
| `https://www.pexels.com/photo/abstract-oil-and-water-bubble-macro-photography-30158142/` | Free stock, dense small-bubble swarm on dark ground — closest stock match to our `swarm_*` block. | dark/multi |
| `https://unsplash.com/s/photos/oil-and-water` | Search page; scroll it on the phone for dozens of palettes at once. | multi |

Not found: any public frame grabs of individual loops other than the montage GIF and the
trailer. The pack's 20+ bundled JPGs are behind the paywall. Several piracy mirrors index
the pack (gfx-hub, vfxdownload, psdly, nulledbb) — I checked one for preview sheets; it had
none, and I am not linking the rest as image sources.

---

## 3. Physical technique → what maps to our knobs

### 3.1 The setup (reconstructed, high confidence)

Every tutorial in this genre converges on the same rig, and the three reference frames are
consistent with it:

- **Shallow clear glass dish** holding water, sat on a sheet of glass or a light table.
- **Light comes from underneath**, through the dish, not from the camera side. The tutorials
  are explicit that you light *the background*, not the liquid, and that you want as much
  distance as possible between the oil layer and the light source so the background colour
  diffuses (Fstoppers; Digital Photo Mentor). A coloured LED panel, gels, coloured paper or
  an iPad under the glass is the standard background.
- **Oil floated on top** — baby oil / mineral oil is preferred over cooking oil because
  cooking oils add a yellow cast. A tablespoon, then stirred with a stick or screwdriver:
  the oil breaks into discs of every size, and stirring more makes them merge or shatter.
- **Camera straight down on a tripod**, macro lens (60–100 mm), around f/5, sensor plane
  parallel to the liquid surface so the whole oil layer is in one focal plane. That parallel
  plane is why our 2D projection is physically honest: there is essentially no perspective.
- **Colour in the water, not only in the light.** The 1960s liquid-light-show practice is the
  direct ancestor: nested *clock-face glasses* on an overhead projector, mineral oil tinted
  with oil-soluble dye in one dish, water tinted with food colouring in the other, squished
  and rocked. That is exactly the two-phase, two-palette structure the Liquid Acid frames
  show — a saturated oil colour over an independently coloured, marbling water layer.

For our renderer the important consequence is that **this is a transmission image, not a
reflection image**: brightness = how much backlight survives the ink. That is the same
Beer-Lambert logic already used by `style=ink`, and it argues for driving `ink_gain` /
`ink_bias` as an absorption term rather than as an emissive colour ramp.

### 3.2 Flat saturated discs vs. webs

Oil on water is a *dewetted film*. At low coverage the oil minimises its interface and sits
as separate near-circular lenses — our `disc_frac`, with `disc_min/max`. As coverage rises
or the dish is agitated, discs touch and the layer inverts into a connected film pierced by
water holes — our `web_frac` plus `hole_min/max`. Ref-1 is the low-coverage state, ref-2 is
the inverted state. The interesting point is that **these are the same parameter**, not two
different populations: a single "oil coverage" scalar should slide the look from discs →
touching discs with flat contact seams → web with holes. We currently express it as three
independent fractions, which is why the A/B/C palettes can drift apart in feel.

The interior is *flat* because an oil lens on water is optically almost a slab: constant
thickness across the middle, curvature only at the meniscus. Any interior shading we add
(`ink_shading`, currently 0.00 in palette A — correct) reads as wrong immediately.

Spreading behaviour has a real literature: Chan & Fried, *Marangoni spreading on liquid
substrates in new media art* (arXiv 2312.05518) measure spreading-radius power laws (3/8 for
the precursor edge, 1/4 for the main body on a Newtonian substrate) and show that on a
**shear-thinning** substrate the spreading front goes *dendritic* with fractal, DLA-like
branching. That is a legitimate physical reason for the ragged filament edges in ref-3, and
suggests a "substrate viscosity" knob could switch our blobs between smooth-round and
branchy without any new geometry.

### 3.3 The rim

The thin bright edge is **refraction through the meniscus**, not a specular highlight and
not an outline. The best citation is Ryu, Zhang & Emeigh, *The Dark Annulus of a Drop in a
Hele-Shaw Cell Is Caused by the Refraction of Light through Its Meniscus* (Micromachines
13(7):1021, 2022): a confined drop imaged from above shows an annulus whose **width shrinks
as the drop's refractive index approaches that of the surrounding liquid**, with the
meniscus staying circular throughout. Rays are pulled out of the annulus and piled up just
outside it, so a dark ring and a bright caustic ring sit side by side.

Three consequences for `rim_*`:

1. **Rim width is a material property (Δn), not a style slider.** Within one shot every disc
   should have nearly the same rim width, varying only with local curvature — big discs a
   touch narrower, tiny bubbles proportionally fatter. `rim_vary=0.7` is currently doing that
   job by noise; curvature-driven would be more convincing.
2. **The rim is dark *and* bright — a paired annulus.** Ref-1 shows a dark hairline hugging
   the orange disc with a cyan glow immediately outside it. We already have `rim_dark` and
   `meniscus`/`meniscus_color` — the finding is that they must be *adjacent and ordered*
   (dark inside, bright outside), never blended into one band.
3. **The bright half is the background colour, concentrated — never white.** In ref-1 the
   halo is the teal ink; in ref-2 it is hot orange over purple. `rim_ink_follow=0.8` is
   physically right and should probably go to 1.0; a fixed `meniscus_color` is the less
   correct mode.

### 3.4 The hole swarms

Two distinct populations, and the references contain both:

- **Water droplets trapped inside the oil** — the dark circles peppering the red web in
  ref-2. They read dark because they are thin water over the same ink, with their own tiny
  rim. Maps to `swarm_holes`, `swarm_dark`, `swarm_rim_dark`.
- **Oil droplets floating on open water** — the orange discs on red marble in ref-3. Maps to
  `swarm_drops`.

Their size distribution is strongly power-law: a few large, very many small, matching our
`size_bias=2.0`. They also **clump** rather than distributing evenly (`swarm_clump=0.85`),
which is what a real emulsion does — droplets collect in the low-shear interior of a lobe
and are swept clear of the fast-moving filaments.

The fluid-art world calls the ring-in-ring version **cells**, and knows the mechanism well:
adding a low-surface-tension oil (silicone) to a high-surface-tension water-based paint makes
the oil rise and push the paint aside, and the density difference between pigments makes
layers overturn. Marangoni flow — surface-tension gradients — is the driver. Practically this
tells us hole nucleation should correlate with *shear/dilatation in the ink field*, not be
independent noise: our swarm currently does not know about the sim at all.

### 3.5 The ink underneath

Marbled bands of flat colour separated by dark seams, plus turbulent filaments — i.e. exactly
a dye-advection fluid sim, which we have. Two details worth copying from the references:

- The dark seams are *thickness*, not a separate colour: where two ink sheets fold over each
  other the transmitted light drops to near black (ref-1 sampled `#2c1506` in the seams, which
  is 5% luminance). `seam_strength` / `seam_lo` / `seam_hi` should be driven by accumulated
  dye density rather than gradient magnitude alone.
- The ink moves **noticeably faster than the oil**. In the product GIF the oil discs drift
  almost rigidly while the ink boils underneath. Our `flow_gain=1.15` couples them fairly
  tightly; a deliberate oil/ink speed ratio would buy a lot of realism cheaply.

---

## 4. Colour palettes observed

Sampled directly out of the three reference screenshots (7×7 pixel averages plus a coarse
histogram over the non-UI crop). Caveat: these are Instagram-compressed phone screenshots of
an HDR display, so hues are trustworthy, absolute levels are not.

**Ref-1 — orange discs on teal ink** (complementary, roughly 180° apart)

| Role | Hex | Note |
| --- | --- | --- |
| Oil body | `#d63d1e` | dominant, ~22% of frame in one quantised bin — very flat |
| Oil hot / marbled ink | `#f07605` | the orange lower half |
| Ink background | `#0aa1a7` – `#24b4b5` | the teal that also forms the rim halo |
| Ink seam / shadow | `#2c1506` → `#180808` | near-black, warm not neutral |

**Ref-2 — red-orange web on purple ink**

| Role | Hex | Note |
| --- | --- | --- |
| Oil body | `#e82806` | with a hotter `#f81808` core |
| Oil deep / shadowed | `#c20114` | where the web thins |
| Ink background | `#5b009b` / `#6808b8` | bright violet, ~15% of frame |
| Ink deep | `#480848` → `#380848` | the hole interiors read this |
| Accent | `#8a13bf` | magenta-violet filaments in the ink |

**Ref-3 — orange bubbles on red marble** (analogous, not complementary)

| Role | Hex | Note |
| --- | --- | --- |
| Bubble/oil | `#ff8f02` – `#f88808` | high-key orange discs |
| Hot ink | `#ff6505` / `#f80808` | pure red, blue channel ~0 |
| Yellow lobe | `#f4c304` | the lower marble |
| Deep marble | `#880828` → `#580838` | crimson-to-aubergine |
| Cold edge | `#3e0e47` | rare dark violet at frame edges |

Pattern across all three: **the blue channel is crushed to near zero over most of the frame**
(ref-1 and ref-3 histograms are almost all `xx xx 08`), the oil is one flat hue with ≤2
neighbouring variants, and the ink carries the whole hue range. Ref-1 and ref-2 are true
complementary pairs; ref-3 is a warm analogous ramp held together by a very dark cool
counterpoint. Our `oil_color_1..4` / `ink_stop_1..4` structure already matches this — four
near-identical oil variants, four widely spread ink stops — which is a good sign.

---

## 5. How the genre is graded

No published grade notes for this pack, so this is inference from the frames plus standard
film-look practice:

- **Saturation pushed hard, then clipped per channel.** The blue channel sitting at 8/255
  across whole frames is what a strong saturation + channel-limited contrast curve does. Our
  `post_saturation=1.14` with HDR `saturation=1.20` is in the right zone; the missing part is
  the *clipping*, i.e. a per-channel floor that lets a channel actually hit zero.
- **Crushed, tinted blacks.** The darkest ink is `#180808` / `#2c1506`, warm, not neutral —
  a lifted-and-tinted toe, not a crush to pure black. `shadow_floor=0.100` / `shadow_knee`
  are the right knobs; the toe should be *tinted toward the ink hue*, which we do not do.
- **Grain everywhere, strongest in the darks.** Ref-1's dark ink is visibly noisy while the
  flat orange discs are comparatively clean. That is real sensor noise (backlit macro at f/5
  means high ISO) plus his house film-grain overlay — he sells Super 8 and VHS packs, so a
  grain plate is almost certainly composited on top. Our `grain=0.030` is uniform; making it
  luminance-weighted toward shadows is a one-line change with a large payoff.
- **Halation.** The standard film-look recipe isolates highlights, blurs, tints warm and adds
  back. In ref-2 the hot orange web does bleed slightly into the purple. Cheap to fake, and it
  is the thing that most reads as "shot" rather than "rendered".
- No vignette, no letterbox, no chromatic aberration visible in the three frames.

---

## 6. Ideas for our look, ranked by payoff vs. effort

1. **Paired rim: dark hairline inside, ink-coloured caustic outside.** Highest payoff, we
   already have both halves (`rim_dark`, `meniscus`, `rim_ink_follow`). Change is ordering and
   guaranteeing they never overlap. Physically cited (Micromachines 13(7):1021). Low effort.
2. **Shadow-weighted grain.** Multiply `grain` by `(1 - luminance)`. Matches every reference
   frame, kills the "clean render" tell. Very low effort.
3. **Oil/ink speed ratio.** One new scalar; oil advects at a fraction of the ink velocity.
   The product GIF makes this obvious and our current coupling is the main reason motion reads
   as CG. Low effort, high payoff.
4. **Single "coverage" scalar driving disc→web inversion.** Replace independent
   `disc_frac` / `web_frac` / `bubble_frac` with one coverage parameter plus a shape bias, so
   the palettes stay on the same physical family. Medium effort, big consistency win.
5. **Tint the shadow toe toward the ink hue** instead of neutral. Two lines in the grade.
6. **Couple swarm nucleation to the sim.** Spawn holes where dilatation/shear is low, sweep
   them out of filaments. Medium-high effort, but it is what makes the swarms look *carried*
   rather than *stamped*.
7. **Rim width from curvature rather than noise** (`rim_vary` becomes a modulation of a
   curvature term). Medium effort; makes big and small blobs feel like the same material.
8. **Highlight halation pass.** Small blur of the top highlights, warm tint, screen back.
   Low-medium effort, moderate payoff — mostly sells stills, less visible in motion.
9. **Dendritic spreading mode** for edges (Chan & Fried's shear-thinning regime). Highest
   effort of the list, and only ref-3 really needs it — park it unless the branchy frames
   become a target.
10. **Per-channel clip in the grade** so a channel can legitimately reach zero. Cheap, but
    risky on an HDR panel — test on the OLED before adopting.

---

## 7. What I could not find

- Any statement by Steven McFarlane about oils, dyes, lighting, lens, dish or grade. The
  "real liquid acid" line is the entire published method.
- A Behance or Vimeo presence for this work.
- Public frame stills of individual loops; only the montage GIF and trailer thumbnail exist
  outside the paywall.
- The Motion Science TV interview's content (video-only; not transcribable here).
- Anything about the pack's actual colour pipeline, LUTs or grain plate.
- Confirmation of backlighting for *this* pack specifically — it is strongly implied by the
  frames and universal in the genre, but he has never said it.

### Sources

- [Liquid Acid Visuals – Steven McFarlane Design](https://www.stevenmcfarlane.design/products/liquid-acid-visuals)
- [Steven McFarlane Design (shop home)](https://www.stevenmcfarlane.design/)
- [Liquid Acid Visuals Trailer (YouTube)](https://www.youtube.com/watch?v=kClNPciY9aU)
- [An Interview with Steven McFarlane – Motion Science TV](https://www.motionscience.tv/blog/an-interview-with-steven-mcfarlane-video-podcast)
- [An Easy and Fun Home Macro Project: Oil on Water Photography – Fstoppers](https://fstoppers.com/macro-photography/easy-and-fun-home-macro-project-oil-water-photography-690576)
- [Home Project – Macro Photography with Oil and Water – Digital Photo Mentor](https://www.digitalphotomentor.com/oil-water-macro-photography-project/)
- [Oil, Milk and Paint Macro Photography Tutorial – Adaptalux](https://adaptalux.com/milk-and-paint-macro-photography/)
- [Getting the shot: macro photos of paint and water that look like CGI – DPReview](https://www.dpreview.com/articles/8391405674/getting-the-shot-macro-photos-of-paint-and-water-that-look-like-cgi)
- [How To Produce A Psychedelic Light Show – phinnweb](http://www.phinnweb.org/retro/garage/lightshows.html)
- [How to produce a psychedelic light show – Wild Bohemian](https://wild-bohemian.com/liteshow.htm)
- [A Brief History of Psychedelic Light Shows – Liquid Light Lab](https://liquidlightlab.tumblr.com/post/81279112722/mineral-oil-water-alcohol-watercolor-oil-dye)
- [Ryu, Zhang & Emeigh, "The Dark Annulus of a Drop in a Hele-Shaw Cell Is Caused by the Refraction of Light through Its Meniscus", Micromachines 13(7):1021 (2022)](https://www.ncbi.nlm.nih.gov/pmc/articles/PMC9317764/)
- [Chan & Fried, "Marangoni spreading on liquid substrates in new media art", arXiv:2312.05518](https://arxiv.org/abs/2312.05518)
- [How to Use Silicone Oil for Acrylic Pouring – Art Academy Direct](https://artacademydirect.com/blogs/art-materials-techniques/how-to-use-silicone-oil-for-beautiful-cells-in-your-acrylic-pours)
- [How to Create Cells in Acrylic Pour Paintings – Fine Art Tutorials](https://finearttutorials.com/guide/cells-in-acrylic-pour-paintings/)
- [How to Get Film Look in DaVinci Resolve – Passion Fuels Ambition](https://www.passionfuelsambition.com/how-to-get-film-look-in-davinci-resolve-color-grading-tutorial-2026/)
