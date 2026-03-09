# Vexel Design Constitution + Implementation Doctrine

## 0. Scope

This document defines the intentionality of Vexel’s online presence in enough detail that a future redesign, an external collaborator, or an agentic implementation process can act on it without falling back to generic “modern dev tool” defaults.

It covers:

* identity,
* audience handling,
* page roles,
* content hierarchy,
* tone,
* visual doctrine,
* mascot design,
* subtitle system,
* usability constraints,
* agent-facing structure,
* anti-patterns,
* acceptance criteria.

This document is a governing text.
When two design options are possible, the one that better preserves Vexel’s identity, memory, and severity wins.

## 1. Primary Intent

The Vexel presence exists to do five things in a specific order.

### 1.1 Restore the Project’s Thesis

When the author returns to the project after weeks or months away, the landing page and README must restore the reason Vexel exists.
The first screen must reactivate motivation, taste, and technical direction.
It must remind the author that Vexel is not a random language experiment but a coherent refusal of certain compromises.

### 1.2 Preserve and Sort the Knowledge Base

Vexel already has multiple forms of knowledge: tutorial material, RFC-level law, technical explanation, architecture, playground, examples, repository structure, and contributor-oriented detail.
These are strengths.
The goal is not simplification through removal.
The goal is legibility through sorting.

### 1.3 Attract the Right Curiosity

The front door must create desire in the right kind of reader.
That desire should come from the language’s tradeoffs, not from generic promises like productivity, power, or ease of use.

### 1.4 Filter Out the Wrong Curiosity

The presence should quickly tell casual mismatch readers that the project is probably not for them.
This should happen through clarity, not through clutter.
The project may be selective and excluding.
It must not be confusing.

### 1.5 Remain Memorable

The project must be easy to remember from fragments.
If someone forgets everything except a horse head, a hostile subtitle, a severe white-black page, or “the language that gives up pointers,” that must still be enough to recover Vexel.

## 2. Core Proposition

Vexel is a language for whole-program compilation.

It rejects raw pointers and pointer arithmetic because they destroy global reasoning.
It keeps compile-time execution, tests, reports, and application logic inside the same codebase.
It allows code to express intention through familiar structural forms without forcing those forms to imply a rigid runtime ABI fiction.
It rejects the scattered ecosystem of generators, side-tools, and split meta-languages.
It remains practical by lowering to C.

This proposition must not be broken into random feature bullets unless strictly necessary.
The online presence must present these ideas as one coherent trade.

### 2.1 Canonical Trade Statement

The one sentence that everything else must reinforce is:

**Vexel gives up low-level escape hatches in order to gain whole-program knowledge.**

Every page should support this, either directly or indirectly.

## 3. Positioning Doctrine

Vexel is not trying to be all-purpose, universally approachable, or culturally agreeable.
It is narrow by choice.
It should sound like a deliberate instrument built for a specific type of work and a specific taste.

The presence must not communicate:

* “everyone is welcome,”
* “you can use Vexel for anything,”
* “Vexel is intuitive,”
* “Vexel is the easiest way to do X,”
* “Vexel wants adoption at any cost.”

The presence should communicate:

* “Vexel has standards,”
* “Vexel makes hard tradeoffs on purpose,”
* “Vexel is willing to reject common expectations,”
* “if the tradeoffs attract you, continue,”
* “if they repel you, the project has already done its job.”

## 4. The Personality Stack

The Vexel presence must be built from three stacked layers, not one flat tone.

### 4.1 Structural Layer — Doctrine

This is the dominant layer.
It governs headings, main explanatory text, document labels, route selectors, README structure, navigation, anti-goals, technical overviews, and specification copy.

Properties:

* exact,
* severe,
* calm,
* declarative,
* anti-hype,
* resistant to fluff,
* lexically clean,
* structurally disciplined.

Function:
This layer creates trust, legibility, and authority.
It makes the project feel like it has laws.

### 4.2 Accent Layer — Theater

This layer appears rarely.
It governs rotating subtitles, selected hero lines, anti-goal callouts, mascot captions, empty states, certain section intros, and occasional labels.

Properties:

* dry,
* sharp,
* mildly hostile,
* slightly absurd,
* quotable,
* authored,
* compact.

Function:
This layer makes the project memorable and prevents doctrine from becoming lifeless.

### 4.3 Atmospheric Layer — Ritual

This layer is mostly visual and environmental.
It comes from layout, mascot placement, color hierarchy, spacing, compositional discipline, surface feel, and tiny non-essential motion.

Properties:

* cold,
* machined,
* orthogonal,
* restrained,
* ceremonial,
* artificial.

Function:
This layer turns the site from “documentation with personality” into a world.

### 4.4 Balance Rule

Doctrine dominates.
Theater punctures.
Ritual surrounds.

If theater becomes frequent, it becomes adolescent.
If ritual becomes loud, it becomes cosplay.
If doctrine disappears, the entire project collapses into style.

## 5. Reader-State Model

The Vexel presence must be designed for states, not demographic personas.
Each state has a desired outcome and a corresponding design treatment.

### 5.1 Future Author State

Situation:
The author returns after time away and needs to recover motivation, thesis, and the map of the project.

Desired outcome:
Within one screen and one click, the author remembers why Vexel exists and how to re-enter the work.

Design treatment:

* Thesis at the top.
* Strong memory anchors.
* Clear route split.
* No “marketing” filler.
* README as re-entry map.

Failure mode:
The site feels like a dead archive or generic docs shell.

### 5.2 Curious Casual State

Situation:
Someone skims language projects for novelty but does not enjoy effort.

Desired outcome:
They understand the trade quickly and leave if it is not for them.

Design treatment:

* Early statement of the pointer sacrifice.
* Explicit non-goals.
* No over-accommodation.
* No warm onboarding voice.

Failure mode:
The site invites them into a long explanation they were never going to finish.

### 5.3 Serious Technical State

Situation:
A reader is willing to work and wants to know whether the language is conceptually serious.

Desired outcome:
They feel tempted by the thesis, verify seriousness through examples or the RFC, and then find the implementation or tutorial without friction.

Design treatment:

* Proof near the top.
* RFC one click away.
* Tutorial path explicit.
* Technical overview available but not front-loaded.

Failure mode:
The site is either too coy or too implementation-heavy at first contact.

### 5.4 Critical State

Situation:
A reader wants to reject the language.

Desired outcome:
They can criticize the real project rather than an accidental caricature.

Design treatment:

* Anti-goals surfaced clearly.
* Hard tradeoffs stated plainly.
* No hiding of controversial choices.

Failure mode:
The reader must infer Vexel’s values indirectly.

### 5.5 Forced User State

Situation:
Someone dislikes Vexel but has to interact with it.

Desired outcome:
They can find the required material with minimal resentment caused by the site itself.

Design treatment:

* Clean hierarchy.
* Clear labels.
* Distinct routes to tutorial, spec, playground, implementation.
* Zero navigation tricks.

Failure mode:
The tone becomes an excuse for disorder.

### 5.6 Agentic State

Situation:
An agent needs to summarize, navigate, modify, or reason about the project.

Desired outcome:
The agent can map the project structure, identify the law, distinguish tutorial from reference, and operate without hallucinating relationships.

Design treatment:

* Explicit document roles.
* Consistent naming.
* Stable section titles.
* No ambiguous labels like “Guide” when five different things could be meant.

Failure mode:
The front-end identity is strong but the document stack is semantically blurry.

### 5.7 Memory-Recovery State

Situation:
Someone remembers only that Vexel was strange, severe, or had a robot horse.

Desired outcome:
A fragment is enough to re-identify the project.

Design treatment:

* Name reinforcement.
* Horse emblem consistency.
* Signature subtitle system.
* Repeated anti-pointer thesis.
* Stable visual field.

Failure mode:
The identity depends on exact memory of the URL or repo title.

## 6. Architectural Model of the Presence

The online presence must behave as three connected spaces.

### 6.1 Space One: The Gate

This is the landing page first screen and immediate continuation.

Responsibilities:

* establish identity,
* state the trade,
* provide one proof,
* signal exclusion,
* route the reader.

Requirements:

* One compact thesis.
* One visible code artifact or source-to-output proof.
* One explicit or strongly implied exclusion line.
* Immediate route access.
* Strong visual identity.

What the Gate must not do:

* front-load implementation detail,
* over-explain syntax,
* greet everyone equally,
* become a documentation index before becoming a threshold.

### 6.2 Space Two: The Router

This is the section of the landing page or near-top navigation where readers choose how to continue.

Responsibilities:

* turn curiosity into directed motion,
* differentiate learning modes,
* sort by intention.

Canonical routes:

* **Taste it** — examples, playground.
* **Read the law** — RFC / normative spec.
* **Learn the shape** — tutorial.
* **Open the machine** — architecture / compiler internals.
* **Contribute** — repo structure, contracts, tests, extension points.
* **Read the refusals** — anti-goals.

Requirements:

* Route labels must reflect purpose, not internal folder names.
* Short explanatory line per route is allowed.
* Visually, route selection should feel like choosing a mode, not opening random links.

### 6.3 Space Three: The Library

This is the depth layer containing all preserved material.

Responsibilities:

* hold the project’s law, explanation, examples, and machinery,
* preserve historical and technical value,
* support both humans and agents.

Requirements:

* Each document must have one role.
* Each role must be named clearly.
* Redundancy across documents must be minimized or explicitly acknowledged.

## 7. Page-by-Page Doctrine

### 7.1 Landing Page

Role:
Threshold, memory engine, route selector, emotional thesis restorer.

Above the fold must contain:

* Vexel name.
* Rotating subtitle.
* Core thesis.
* One trade statement.
* One proof element.
* Mascot seal or image.
* Route entry points.

Second fold may contain:

* brief anti-goal framing,
* comparison-by-principle,
* more examples,
* links to law/tutorial/playground/architecture.

The landing page must not look like:

* a GitHub Pages default docs homepage,
* a startup landing page,
* a dashboard,
* a dense changelog.

### 7.2 README

Role:
Repository threshold and reading map.

Opening structure must be:

1. project name,
2. one-sentence definition,
3. one-paragraph trade explanation,
4. small example,
5. how to try it fast,
6. how to read the project,
7. only then: implementation and contributor detail.

The README must answer in the first screenful:

* What is this?
* Why does it exist?
* What is the central trade?
* Where do I go next depending on what I want?

### 7.3 RFC

Role:
Normative law.

Constraints:

* Must be explicitly labeled as normative.
* Must avoid tone theatrics.
* Can be dry and exact.
* Should be easy to cite.

### 7.4 Detailed Spec / Deep Explanation

Role:
Semantics, elaboration, and chaptered detail.

Constraints:

* May be verbose.
* Must not be confused with the law if it is explanatory rather than normative.
* Should link back to RFC-level rules when relevant.

### 7.5 Tutorial

Role:
An earned path into the language.

Constraints:

* Linear enough to teach shape.
* Practical enough to reward effort.
* Not a reference dump.

### 7.6 Playground

Role:
Immediate proof.

Constraints:

* Reachable within one obvious click from landing page and README.
* Should show example programs fast.
* Should make source, output, or behavior legible.

### 7.7 Technical Overview / Architecture

Role:
Compiler as machine.

Constraints:

* Should define stages, invariants, backend contracts, and extension points.
* Not for first contact.
* Must be easy to find when wanted.

### 7.8 Anti-Goals

Role:
Doctrine by refusal.

Constraints:

* Must exist as a first-class page.
* Must explain refusals cleanly.
* Must not sound embarrassed.
* May contain some of the sharpest prose in the project.

## 8. Usability Policy

The Vexel presence is allowed to be demanding, but not sloppy.

### 8.1 Hard Rule

**The language may be difficult; the information architecture may not be.**

### 8.2 Usability Means

For Vexel, usability does not mean friendliness or simplification.
It means:

* stable routes,
* precise labels,
* fast orientation,
* visible hierarchy,
* predictable navigation,
* readable code,
* low ambiguity about where truth lives.

### 8.3 Usability Does Not Mean

* softening the project’s position,
* calling everything “easy,”
* burying hard tradeoffs,
* replacing specificity with fluff,
* adding excessive helper text to compensate for weak structure.

### 8.4 Navigation Rules

* Top-level navigation must stay small.
* Every label must denote a distinct kind of content.
* Routes to tutorial, playground, RFC, and architecture must remain obvious at all times.
* No decorative navigation experiments.
* No hidden essential paths.

### 8.5 Accessibility Rules

Even though the tone is severe, the site must remain physically usable.

This means:

* sufficient contrast,
* readable text sizes,
* keyboard-navigable primary controls,
* no motion-dependent interaction,
* no essential information encoded only by color,
* code blocks that remain legible under the chosen palette.

Severity is not a license for bad ergonomics.

## 9. Copywriting System

### 9.1 Primary Copy Principles

Main copy must be:

* declarative,
* exact,
* compressed,
* non-apologetic,
* meaningful per sentence,
* resistant to buzzwords,
* structurally calm.

### 9.2 Sentence Behavior

Good Vexel copy often does one of four things:

* states a trade,
* states a refusal,
* states a route,
* states a consequence.

It should not meander.
It should not oversell.
It should not try to charm constantly.

### 9.3 Vocabulary Preferences

Prefer words like:

* whole-program,
* law,
* refusal,
* route,
* contract,
* structure,
* machine,
* doctrine,
* invariant,
* codebase,
* lowering,
* shape,
* trade.

Avoid overuse of words like:

* simple,
* delightful,
* intuitive,
* modern,
* productive,
* elegant,
* revolutionary,
* powerful,
* ergonomic,
* easy.

These may be true in some contexts, but they weaken the project’s precision if used as front-door language.

### 9.4 Theater Placement Rules

Provocative lines are allowed in these zones:

* rotating subtitles,
* anti-goal page headings,
* hero microcopy,
* mascot-caption moments,
* empty states,
* selected section separators.

Provocative lines are not allowed to dominate:

* README structure,
* specification text,
* navigation labels,
* architecture documentation,
* tutorial instruction.

### 9.5 Examples of Correct Tone

Correct:

* Vexel rejects raw pointers because they hide too much from the compiler.
* This language is narrow on purpose.
* If you want escape hatches, this project has little to offer you.
* Read the law.
* Open the machine.

Incorrect:

* Vexel is a next-generation modern systems language.
* Vexel makes developers more productive.
* Vexel offers a delightful compile-time-first experience.
* We believe everyone should feel empowered.

## 10. Visual System

### 10.1 Governing Principle

The visual system must make the project feel machined, orthogonal, and governed.
It must not look soft, social, or startup-like.

The overall impression should be:

* cold,
* exact,
* bright but artificial,
* sparse,
* controlled,
* memorable.

### 10.2 Palette Doctrine

#### Primary White

This is not paper white.
It should feel artificial, metallic, bluish, or ceramic.
It may carry a slight cold tint.
It should feel fabricated rather than organic.

Use cases:

* main page field,
* large sections,
* negative space,
* identity backdrop.

#### Secondary Black

Pearl black or elegant black.
Not flat harsh pure black unless used very intentionally.
The black should carry depth and authority.

Use cases:

* anchors,
* dark slabs,
* code framing zones,
* key contrast regions,
* seal and mascot detail.

#### Accent Blue

Metallic blue, electric but disciplined.
This is the main active accent.

Use cases:

* active states,
* selected routes,
* diagrammatic emphasis,
* proof highlights,
* tiny technical markers,
* occasional optic details on the mascot.

Restrictions:

* never flood a page with it,
* never use it as a decorative wash,
* never let it dominate the white-black structure.

#### Accent Orange

A rarer accent than blue.
This is not a second primary accent.
It is an interrupt color.
It signals heat, warning, fracture, or emphasis.

Use cases:

* rare warnings,
* anti-goal accents,
* one-off important highlights,
* tiny subtitle or badge punctuation if justified.

Restrictions:

* must be visibly rarer than blue,
* should not coexist in large quantities with blue,
* must feel like a decision, not a theme.

#### Tertiary Whites / Grays

A darker and colder version of the main white may be used for emphasized regions.
This is useful for route modules, examples, pullouts, and anti-goal fields.

### 10.3 Geometry Doctrine

The mathematical motif is vectors and matrices.
This must be expressed structurally, not illustratively.

Rules:

* ninety-degree geometry only,
* no rounded corners,
* no organic curves in interface framing,
* use rectangular or cut-corner blocks,
* prefer edge alignment over framed cards,
* use grids and column tension,
* use crop-like cuts and corner treatments.

Acceptable deviation:
The mascot itself may have angled contour complexity, but the interface system must remain orthogonal.

### 10.4 Surface Doctrine

The site may suggest materiality, but lightly.
Allowed references:

* machined ceramic,
* anodized alloy,
* instrument faceplate,
* cold reflection,
* panel seams,
* technical plate geometry.

Disallowed references:

* chrome excess,
* fake 3D dashboard gimmicks,
* gamer HUD overload,
* neon-cyberpunk clutter,
* vaporwave futurism.

### 10.5 Layout Doctrine

The layout must look locked.
Not loose, not friendly, not airy in a lifestyle sense.
Whitespace is structural, not decorative.

Requirements:

* strong vertical rhythm,
* hard section boundaries,
* disciplined alignment,
* obvious primary axis,
* content blocks that feel placed rather than floated.

What not to do:

* random cards,
* whimsical block sizes,
* over-dense mosaic layouts,
* oversized startup-style hero emptiness with nothing to say.

### 10.6 Component Doctrine

#### Buttons / Actions

* Rectilinear.
* No rounded corners.
* Strong hierarchy.
* High contrast.
* Hover states may shift tone or accent lightly.
* No playful transitions.

#### Links

* Must look intentional.
* Underline or clear typographic treatment is preferred over vague low-contrast text links.
* Important route links should feel like route selectors, not inline afterthoughts.

#### Cards / Panels

* Use sparingly.
* Prefer slabs, sections, fields, or blocks over “cards.”
* If panelized, use hard edges and clear purpose.

#### Code Blocks

* Must be a first-class design element.
* Excellent monospace rendering required.
* Can live on black or cold gray fields.
* Syntax highlight, if present, must not turn into rainbow noise.

#### Labels / Eyebrows

* Small uppercase or compact technical styling is acceptable.
* These are good places for the matrix/vector atmosphere to appear.

## 11. Typography Doctrine

### 11.1 Function

Typography is the primary execution risk and the primary refinement lever.
If typography fails, the entire presence will look fake or generic.

### 11.2 Hierarchy Requirements

The type system must support at least these layers:

* Commanding display line.
* Severe section headline.
* Tight body paragraph.
* Technical micro-label.
* Monospace code block.
* Monospace inline syntax.
* Rotating subtitle / theatrical line.

### 11.3 Typographic Character

The overall typographic feeling must be:

* technical,
* disciplined,
* calm,
* sharp,
* not ornamental,
* not cute,
* not retro-computer for its own sake.

### 11.4 Practical Family Guidance

A disciplined sans + mono pairing is the safest direction.
The mono must not feel like a decorative novelty.
It must feel integral to the system.

Preferred behavior:

* sans for main structure,
* mono for code, labels, technical inserts, and occasional high-friction emphasis.

### 11.5 Spacing Rules

* Headlines must not become airy and lifestyle-like.
* Body copy must remain comfortably readable.
* Code must be compact but not cramped.
* Subtitle lines may allow slightly more punch.

### 11.6 Anti-Patterns

* novelty sci-fi fonts,
* overly geometric display fonts that reduce readability,
* font mixing beyond a strict system,
* decorative mono everywhere,
* code rendered as an afterthought.

## 12. Motion Doctrine

Motion exists to confirm life, not to carry meaning.

### 12.1 Hard Rules

* No navigation may depend on animation finishing.
* No active state may be revealed only after a theatrical sequence.
* Motion must never be the primary source of delight.

### 12.2 Allowed Motion Types

* slight fade or reveal on section entry,
* subtle line sweep,
* tiny corner-lock effect,
* restrained panel translation,
* ambient seal or optic shimmer,
* soft environmental parallax if nearly imperceptible.

### 12.3 Disallowed Motion Types

* bounce,
* overshoot,
* playful springiness,
* long staged reveal sequences,
* loader theater,
* motion that imitates startup-product demo style.

### 12.4 Why

Vexel should feel alive like an instrument warming, not like an app trying to entertain.

## 13. Mascot Doctrine

The mascot is a core memory mechanism, not an accessory.

### 13.1 Narrative Function

The mascot is the guardian of the gate.
It embodies useful aggression.
It represents the project’s refusal to be simply welcoming.
It suggests intelligence, force, and judgment.

The mascot is not a companion.
It is an evaluator that may also be a guide.

### 13.2 Form

The mascot is a mechanical horse, primarily represented as a head or bust.
It should be usable as:

* seal,
* hero image,
* profile icon,
* section insignia,
* monochrome emblem,
* occasional larger rendered illustration.

### 13.3 Emotional Target

Correct reaction:

“I do not know if this machine is helping me or assessing whether I deserve to be here.”

Incorrect reactions:

* “cute,”
* “fun mascot,”
* “friendly brand character,”
* “obvious Terminator parody.”

### 13.4 Visual Requirements

The horse should have:

* severe silhouette,
* mechanical planes,
* rigid jawline,
* plated neck,
* visible structure,
* one optical or sensor-like detail,
* contained aggression,
* noble hostility.

### 13.5 Style Range

The mascot may appear in:

* clean vector crest form,
* black/white seal form,
* minimal metallic render,
* poster-like cutout,
* sparse line construction.

The mascot should not appear in:

* cartoon style,
* plush style,
* sticker style,
* chibi style,
* overtly sentimental style.

### 13.6 Placement Rules

Use in:

* hero,
* anti-goals,
* section seals,
* certain empty states,
* favicon or icon variants,
* loading or transition moments if subtle.

Do not:

* place it on every section,
* turn it into repetitive wallpaper,
* use multiple different art styles at once.

### 13.7 Color Behavior

Default forms should work in white/black first.
Blue may accent the optic or a structural detail.
Orange should be rare or absent.

## 14. Name Doctrine

Vexel is a short, sharp, synthetic name.
It must be made sticky through context, not brute repetition.

Associations that must be reinforced repeatedly:

* Vexel ↔ whole-program compilation,
* Vexel ↔ anti-pointer trade,
* Vexel ↔ mechanical horse,
* Vexel ↔ severe white-black field,
* Vexel ↔ hostile or absurd subtitle.

## 15. Subtitle / Easter Egg System

This is one of the main stylistic accent systems.
It must be maintained intentionally.

### 15.1 Purpose

* create revisit value,
* carry theatrical voice,
* keep the site from feeling dead,
* make Vexel quotable,
* support memory.

### 15.2 Constraints

A good subtitle is:

* short,
* dry,
* mildly hostile or absurd,
* authored,
* not internet-generic,
* thematically coherent.

A bad subtitle is:

* random,
* cute,
* meme-dependent,
* excessively vulgar,
* too long,
* disconnected from the project’s tone.

### 15.3 Example Direction

Good directional examples:

* Vexel — the language you would not let your kids be friends with.
* Vexel — make love, not Vexel.
* Vexel — what is the most approachable language to learn? Not Vexel.
* Vexel — honey… I accidentally invented Vexel.
* Vexel — no pointers, no excuses.
* Vexel — if you want escape hatches, keep walking.

### 15.4 Placement

Primary placement: hero subtitle under or near the title.
Optional secondary placement: select loading or empty states.

## 16. Agent-Facing Doctrine

An agent implementing or navigating the site must not have to guess the ontology of the docs.

### 16.1 Required Explicit Roles

Every major document must declare itself clearly as one of:

* landing page,
* README,
* normative RFC,
* explanatory spec,
* tutorial,
* playground,
* technical overview,
* anti-goals,
* contributor guide.

### 16.2 Naming Policy

Avoid ambiguous page names like:

* Guide,
* Docs,
* Learn,
* Overview,

unless paired with a clarifying subtitle or role label.

Examples of clearer naming:

* RFC — Normative Language Definition
* Tutorial — Learn the Shape of Vexel
* Architecture — Open the Machine
* Anti-Goals — What Vexel Refuses

### 16.3 Machine-Legible Relationships

The presence should make these relationships inferable:

* RFC outranks explanation.
* Tutorial teaches, does not define.
* README routes, does not legislate.
* Playground proves, does not explain fully.
* Architecture describes implementation, not language law.

## 17. Transformation Constraints for Future Execution

Any redesign or implementation process must preserve the following invariants:

* The anti-pointer trade remains visible at the front.
* The route split remains explicit.
* The README becomes a map before a maintenance log.
* The mascot remains memorable and controlled.
* Theater remains accent-only.
* White/black dominance remains intact.
* Navigation remains clearer than the language itself.

## 18. Anti-Pattern Catalog

The following are not merely undesirable; they are violations of the presence.

### 18.1 Tone Violations

* sounding like product marketing,
* sounding universally welcoming,
* apologizing for core constraints,
* replacing tradeoffs with slogans,
* overusing provocative voice until it becomes parody.

### 18.2 Visual Violations

* rounded-card startup aesthetic,
* generic Tailwind-tool look,
* soft gradients everywhere,
* too many accent colors,
* dashboard composition,
* sci-fi cliché overload,
* decorative complexity without structure.

### 18.3 Information Violations

* burying the thesis,
* making the README a maintainer-only file,
* unclear distinction between law and explanation,
* too many equally weighted navigation options,
* hard-to-find playground or tutorial,
* anti-goals hidden or absent.

### 18.4 Mascot Violations

* making the horse cute,
* making the horse too literal a Terminator copy,
* using too many mascot styles at once,
* turning the horse into filler decoration.

## 19. Acceptance Tests

The design is not complete unless it passes the following mental tests.

### 19.1 Ten-Second Test

A first-time visitor should be able to answer within ten seconds:

* this is Vexel,
* it is opinionated,
* it gives up something important,
* it does so to gain compiler visibility,
* this is either for me or not.

### 19.2 One-Click Test

From the landing page, a reader must be able to reach within one obvious click:

* tutorial,
* playground,
* RFC / law,
* technical overview / machine view.

### 19.3 Forced User Test

A hostile reader must still be able to find what they need quickly.
If they fail, the tone has become an excuse for bad architecture.

### 19.4 Memory Test

After leaving, a reader should remember at least one of:

* the horse,
* the phrase about pointers,
* the name Vexel,
* a rotating subtitle,
* the white-black mechanical atmosphere.

### 19.5 Agent Test

An agent should be able to identify the repository’s major document roles without fabricating them.

### 19.6 Drift Test

If the result looks like a polished generic dev-tool site that could belong to ten other projects, the redesign has failed even if it looks “nice.”

## 20. Implementation Doctrine

This section translates the constitution into operational rules for redesigning the site, README, and supporting materials.

The purpose of this section is to reduce interpretive drift.
An implementer should not have to invent the structure of the project from scratch.
The execution may vary in polish, but it must not vary in doctrine.

### 20.1 Implementation Priorities

The transformation must happen in this order:

#### Priority 1 — Fix the Front Door

The landing page hero, first screen, and immediate route structure must be corrected first.
This is the highest leverage change because it determines memory, thesis restoration, and reader sorting.

Concrete goals:

* put the anti-pointer / whole-program trade on the first screen,
* make the route split visible immediately,
* make the page feel like Vexel rather than like docs.

#### Priority 2 — Fix the README

The README must become a repository threshold instead of primarily a maintainer memo.
This matters for GitHub discovery, future-author re-entry, and agentic interpretation.

Concrete goals:

* identity first,
* quick example second,
* reading map third,
* implementation detail later.

#### Priority 3 — Formalize the Document Stack

The major documents must be labeled by role and linked accordingly.
This includes the RFC, tutorial, technical overview, playground, anti-goals, and contributor-facing material.

Concrete goals:

* remove ambiguity,
* make agents less likely to hallucinate relationships,
* reduce the chance that future-you forgets where truth lives.

#### Priority 4 — Add Distinctive Memory Systems

Once the structure is correct, reinforce identity through the mascot, subtitle system, section titling, and visual discipline.

Concrete goals:

* establish the horse as a recurring emblem,
* make subtitles a controlled accent system,
* make Vexel look and sound recoverable from fragments.

### 20.2 Landing Page Implementation Blueprint

The landing page should be built as a sequence of strict bands or fields, not as a soft collection of “sections.”
Each band has one job.

#### Band 1 — Hero / Gate

Purpose:
state the thesis, create identity, trigger memory, and force a choice.

Must contain:

* Vexel name,
* rotating subtitle,
* one-sentence thesis,
* one-sentence trade statement,
* a small proof artifact,
* the horse emblem or guardian image,
* primary route actions.

Suggested content pattern:

* Title: Vexel.
* Subtitle: rotating line.
* Thesis line: what the language is.
* Trade line: what it rejects and why.
* Proof strip: a tiny code sample or source-to-output summary.
* Route actions: Taste it / Read the law / Learn the shape / Open the machine.

Forbidden patterns:

* no generic “Welcome to Vexel,”
* no architecture paragraph first,
* no large decorative void without content,
* no feature grid above the fold.

#### Band 2 — Proof Field

Purpose:
show that Vexel’s claims are not aesthetic only.

Must contain one or more of:

* tiny source sample,
* compile-time evaluation example,
* test/report execution example,
* generated C output snapshot,
* dead-code or residualization proof.

Implementation rule:
This band must feel evidentiary.
It should not read like copywriting.

#### Band 3 — Route Field

Purpose:
sort the reader by intention.

Recommended routes:

* Taste it.
* Read the law.
* Learn the shape.
* Open the machine.
* Read the refusals.
* Contribute.

Implementation rule:
Each route needs a title and a one-line explanation.
Routes must be visually distinct enough to feel intentional, but not cardified into generic product tiles.

#### Band 4 — Refusal / Anti-Goals Teaser

Purpose:
make the project’s boundaries visible early.

Must communicate:

* no raw pointers,
* no pointer arithmetic,
* no scattered generator culture,
* no interest in pleasing all tastes.

Implementation rule:
This band can be harsher than average.
This is a good place for sharp prose or a small theatrical strike.

#### Band 5 — Deeper Index / Library Entry

Purpose:
give the reader a stable path into the full library.

May contain:

* tutorial track,
* RFC,
* technical overview,
* examples,
* playground,
* contributor guide,
* document role map.

Implementation rule:
This must read like an organized library entrance, not like a link dump.

### 20.3 README Implementation Blueprint

The README should be rewritten into the following order.

#### Segment 1 — Definition

One sentence.
It must answer: what is Vexel?

Template behavior:

* language name,
* whole-program stance,
* central trade,
* practical output path.

#### Segment 2 — Why It Exists

One short paragraph.
It must answer: why does this project exist at all?

This paragraph should mention:

* compiler visibility,
* one codebase,
* compile-time execution inside the language,
* rejection of code-generator culture.

#### Segment 3 — Minimal Example

A tiny, memorable code sample.
This should not try to show everything.
It should show shape and strangeness.

#### Segment 4 — Fastest Way In

Direct links or commands for:

* playground,
* local quick start,
* tutorial.

This section must minimize hesitation.

#### Segment 5 — Reading Map

This is one of the most important README sections.
It should explicitly say:

* read the RFC for law,
* read the tutorial for shape,
* use the playground to taste it,
* read the technical overview to understand the machine,
* read the anti-goals to understand the refusals,
* read contributor docs if you want to modify the implementation.

#### Segment 6 — Repository Structure

Only now should the README transition into code layout, components, passes, contracts, tests, and implementation detail.

Implementation rule:
Every structural subsection in the README should answer a real repo-navigation need.
It should not exist just because a typical README has one.

### 20.4 Document Taxonomy Implementation

The documentation stack should have explicit role labels in headings, subtitles, or metadata.

Recommended role labels:

* **Landing Page** — Thesis and routes.
* **README** — Repository threshold and reading map.
* **RFC** — Normative language definition.
* **Specification** — Detailed semantics and elaboration.
* **Tutorial** — Learn the shape of Vexel.
* **Playground** — Run and inspect Vexel.
* **Architecture** — Open the machine.
* **Anti-Goals** — What Vexel refuses.
* **Contributor Guide** — Work on the implementation.

Implementation rule:
These labels should appear in-page where helpful, not only in navigation menus.
They are part of the ontology.

### 20.5 Navigation Doctrine in Practice

Top-level navigation should remain small and role-based.
Recommended top-level items or their equivalents:

* Home,
* Tutorial,
* RFC,
* Playground,
* Architecture,
* Anti-Goals,
* Repository.

Alternative stylized naming is allowed if the role remains obvious.
For example:

* Learn the Shape → Tutorial,
* Read the Law → RFC,
* Open the Machine → Architecture.

Implementation rule:
If stylized titles are used, pair them with explicit clarifying subtitles so humans and agents both understand them.

### 20.6 Component Doctrine in Practice

The following rules should govern component implementation.

#### Route Modules

Purpose:
act as intentional selectors, not marketing cards.

Rules:

* use hard edges,
* contain one title and one line of explanatory text,
* optionally include tiny labels or glyphs,
* never become oversized glossy cards,
* hover behavior must be minimal.

#### Code Panels

Purpose:
make language shape and proof visible.

Rules:

* monospace first,
* high contrast,
* compact but readable,
* line spacing must support scanning,
* syntax emphasis must not become rainbow decoration.

#### Section Headers

Purpose:
establish doctrine and route the reader.

Rules:

* concise,
* severe,
* no vague lifestyle-style language,
* may include technical micro-labels or bracket accents.

#### Quote / Slogan Areas

Purpose:
carry theater in controlled doses.

Rules:

* short only,
* no more than one sharp line in a band unless explicitly justified,
* avoid stacking multiple theatrical lines together,
* never let jokes outrank doctrine.

### 20.7 Visual Token Doctrine

The design system should be implemented with a small, stable token set.
Not because tokens are trendy, but because drift must be constrained.

Suggested token categories:

* primary white,
* colder white / panel white,
* pearl black,
* electric blue,
* warning orange,
* body text color,
* subdued label color,
* code field color,
* hard divider color,
* focus state color.

Implementation rule:
No extra accent colors should be added without a doctrinal reason.
If a page needs many colors to work, the page design is weak.

### 20.8 Typography Doctrine in Practice

Typography should be implemented as a strict system with named roles.

Suggested roles:

* Display / Title,
* Hero Subtitle,
* Section Heading,
* Body,
* Technical Label,
* Inline Code,
* Code Block,
* Caption / Annotation.

Implementation rule:
Each role should have fixed behavior for size, weight, spacing, and casing.
Random ad hoc typographic styling is forbidden.

Guideline behavior:

* Display: compact authority, not inflated hero emptiness.
* Hero Subtitle: slightly sharper or more theatrical, but still controlled.
* Section Heading: strong and exact.
* Body: highly readable and dense enough to feel serious.
* Technical Label: compact, crisp, optionally uppercase.
* Code Block: visually stable and central to the system.

### 20.9 Copy Zones

Implementation must distinguish between copy zones.
Different zones allow different levels of intensity.

#### Zone A — Law / Definition

Includes:
RFC, formal explanation, README definition lines, navigation labels.

Allowed voice:
strictly doctrinal.

Forbidden:
excessive jokes, taunting, meme energy.

#### Zone B — Structural Presentation

Includes:
landing page thesis, route copy, architecture intros, tutorial intros.

Allowed voice:
doctrinal with occasional sharpness.

Forbidden:
performative aggression as the default tone.

#### Zone C — Accent / Theater

Includes:
rotating subtitles, anti-goal taglines, mascot captions, selected empty states.

Allowed voice:
most provocative.

Forbidden:
breaking the world, becoming childish, turning into random internet humor.

### 20.10 Mascot Asset Doctrine

The mascot should eventually exist in a small controlled asset family.

Recommended asset set:

* primary crest mark,
* simplified icon mark,
* monochrome seal,
* hero illustration or rendered bust,
* outline / wireframe variant if useful,
* favicon-compatible silhouette.

Implementation rule:
All variants must clearly belong to the same creature.
No style fragmentation.

Recommended uses by asset:

* Crest mark: hero, section seals, README banner if ever used.
* Simplified icon: favicon, small UI contexts.
* Monochrome seal: black/white pages, document marks.
* Hero bust/render: landing page only or sparingly elsewhere.

### 20.11 Subtitle System Implementation

The subtitle system should be maintained as a curated pool, not generated casually.

Implementation rules:

* maintain a finite reviewed list,
* allow randomness on load,
* ensure tonal consistency,
* periodically add lines only if they match the world.

Editorial rule:
A weak subtitle is worse than no subtitle.
Do not fill the pool just for quantity.

### 20.12 Motion Implementation Rules

Motion should be implemented with a strict cap on frequency and amplitude.

Practical rules:

* transitions should be fast,
* movement distances should be small,
* no motion should hide or reveal essential information too late,
* hover states should confirm, not perform,
* any ambient motion must stop being noticeable after a moment.

### 20.13 Homepage Acceptance Checklist

A homepage implementation is acceptable only if all of the following are true:

* the first screen states the language’s trade,
* the horse is present or strongly implied,
* at least one proof artifact is visible without deep scrolling,
* at least four core routes are obvious,
* the page does not look like a template,
* the colors remain mostly white/black,
* the provocative voice appears but does not dominate,
* a hostile reader can still tell where to go.

### 20.14 README Acceptance Checklist

A README implementation is acceptable only if all of the following are true:

* the first screen answers what Vexel is,
* the central trade appears early,
* a code example appears early,
* the reading map exists and is explicit,
* the implementation material is still present,
* the file does not read like a maintainer note first.

### 20.15 Anti-Goals Page Blueprint

This page should be structured more aggressively than the others.

Suggested pattern:

* opening refusal line,
* short statement of purpose,
* list of rejected assumptions with explanation,
* why pointers are rejected,
* why tool-splitting is rejected,
* what kinds of freedom Vexel does not value,
* closing line that makes the filter explicit.

Implementation rule:
This page is allowed to be one of the sharpest textual objects in the project.
It should still remain structured and useful.

### 20.16 Architecture Page Blueprint

This page should satisfy the reader who wants the compiler as a machine.

Suggested pattern:

* what the frontend owns,
* what is decided before backend emission,
* stage order,
* invariants,
* residualization / emission concept,
* backend contract,
* extension points,
* links into repo structure.

Implementation rule:
The page should feel rigorous and unembarrassed about technical density.
It must not, however, be the first thing new readers see.

### 20.17 Tutorial Blueprint

The tutorial should teach shape, not merely syntax fragments.

Suggested early steps:

* how Vexel code feels,
* how execution and compile-time behavior relate,
* how the language expresses intention,
* what the absence of pointers means in practice,
* how to inspect output or behavior,
* small increasingly strange examples.

Implementation rule:
Each step should reward effort.
The tutorial should not pretend the language is ordinary.

### 20.18 Transformation Sequence for an Implementer or Agent

A serious transformation effort should follow this sequence:

1. Extract and freeze the core thesis lines.
2. Rewrite the landing page hero.
3. Define the route taxonomy.
4. Rewrite README top half.
5. Formalize document role labels.
6. Add or draft anti-goals page.
7. Lock color and typography tokens.
8. Define mascot asset needs.
9. Implement proof bands and code panels.
10. Apply motion and accent systems last.

Implementation rule:
Do not start with colors, micro-interactions, or mascot rendering before the thesis, routes, and document stack are correct.

### 20.19 Review Protocol

Any future revision should be reviewed against four questions:

* Does this preserve Vexel’s central trade?
* Does this make the site easier to remember?
* Does this help the right readers and filter the wrong ones?
* Does this still look and sound unlike a generic tool site?

If the answer to any of these is no, the revision is suspect.

## 21. Final Image

The Vexel presence must feel like this:

A cold white machine with black internal weight.
A technical doctrine with rare flashes of teeth.
A horse-headed gatekeeper that may help you but will not flatter you.
A library that knows where everything belongs.
A project that is easier to navigate than to accept.
