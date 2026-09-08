# Comments and documentation

Write for the person who will use or change the code next. Explain the current
responsibility, the reasoning that is not obvious from the statements, and the
conditions that callers must preserve. Keep numerical behavior in its single
maintained implementation and link that owner from other modules.

## Choose the reader first

User guides should explain a task logically and sequentially: what it calculates, what inputs are required, how to run it, and finally how to interpret the resulting output. Be sure to define any unfamiliar terms the first time they become relevant. Always keep exact parameter names in code formatting so that the reader can easily spot them in a parameter file or look them up in the Reference.

Developer pages identify the owning files, their callers and resource lifetimes.
Describe the normal modification and test workflow. A short directory README
needs useful entry points and boundaries, not an inventory of every symbol.

Validation summaries explain the question being tested, the reference used,
the measured error and the acceptance limit before linking detailed evidence.
Distinguish agreement between backends from agreement with an independent
reference. An error budget is the accepted numerical limit for that check;
it is not a general guarantee for every user problem.

## Explain responsibility and flow

[main.cpp](../../src/main.cpp) provides the pattern for an entry point: its file
comment explains responsibility, orders the main steps, and states why numerical
methods belong elsewhere. Comments within the function explain meaningful
boundaries such as configuration loading and problem registration.

Use the same approach where a reader would otherwise have to reconstruct the
flow across functions. Explain what enters a stage, what it produces, who owns
the data and when it becomes safe to read. Avoid narrating obvious assignments
or copying a function signature into prose.

## Explain mathematics beside its implementation

Use the symbols and conventions implemented by the routine. State units,
coordinate conventions, valid inputs and sign conventions when they affect the
result. For a non-obvious transformation, connect the expression to its defining
equation or reference and explain the numerical reason for the chosen form.

A CPU traversal and a CUDA kernel can call the same mathematical helper while
loading data differently. Document that distinction; backend notes should explain
storage, launches and synchronization rather than restating an independent
version of the formula. Similar-looking helpers with different admissibility or
conservation requirements need those differences stated explicitly.

## Keep prose durable

Prefer complete, direct sentences over noun lists and unexplained abbreviations.
Lead with what works, then state the relevant condition or boundary. Explain why
a restriction matters instead of repeating general warnings. Use the established
function and module names; conversational labels such as “the previous fix” or
“the new workaround” lose their meaning as the code evolves.

Keep discussions, machine-specific observations and dated repair history in
development records. Source comments describe the resulting behavior. A future
task should name the unresolved condition and its owner, not imply that a planned
feature already exists.

Write user-facing pages for someone encountering ARCH for the first time.
Describe what the interface accepts and how to use it, without assuming an
earlier public release or a migration the reader must perform. Prefer “the
`timeintegrator` alias is used when `time_integrator` is absent” to “legacy
fallback,” and “the direct-field EOS model” to “the old table path.”

Separate data-format identifiers from software release versions. Document the
accepted fields, defaults and validation rules when multiple formats can be
read. Keep real backend, dependency and data-compatibility requirements explicit;
they describe the current system. Wording cleanup does not justify deleting
reader branches, renaming public keys or changing scientific behavior. Such
changes need their own implementation review and tests.

## Preserve evidence and translations

Frozen results, failed attempts and signed review records retain their original
text, inputs and identities. Explain later work in a new record or a maintained
summary; do not rewrite historical evidence to match a newer implementation.

Update English and Chinese user pages together. Preserve commands, parameter
spelling, units, numerical budgets, compatibility rules and linked heading
anchors. Check local links after edits. Language-only changes do not establish
new scientific results or third-party permissions.
