# Runtime events and reviewer input snapshots

The existing **Keep continuous dialogue context** option controls character
session projection. An already sent message is immutable while that bounded
projection remains valid. New focus, expression and final-turn constraints are
sent as separate `runtime_event` user messages, rather than replacing the initial
event. Original encrypted archives remain unchanged. This is a model-facing
projection, not a replacement event bus or execution queue.

```json
{"runtime_event":{"version":1,"kind":"focus.changed","source_event_id":"evt-example","sequence":1,"changes":{"conversation_focus":{"questions":"natural"}}}}
```

The event id binds an update to its dialogue turn. Sequence numbers increase
within the turn; explicit null is a value. The program also maintains a complete
current view for policy review. This interface cannot replace the user input,
grant source access or change system instructions. A field whitelist rejects
such updates before changing the current view.

An update approved after the last character call follows the approved reply in
the next request. It is not retroactively inserted into the previous call. The
next full state comparison includes that approved update. Final-only constraints
expire with their turn. Dialogue resets rebuild from the complete current view.

## Complete current review input (default)

`contextControl.reviewLayout` defaults to `direct`. The reviewer receives the
complete current candidate, source evidence and authority without old review
candidates. `event_context` is a JSON object instead of a second JSON document
inside a string. Only exactly duplicated top-level fields are removed; their
complete current values remain outside that object. Unequal facts, source ids,
original evidence, operation receipts and explicit null values are preserved.
Stable capability declarations precede changing inputs; a plugin's current state
can be separated into `current_plugin_state` under its unchanged plugin id.
Duplicate ids or existing state fields retain their complete original form.
No reference dictionary or state-patch replay is required of the reviewer.

`legacy` retains the original complete request representation. The full/disabled
context-control preset also restores legacy review input. Visual review follows
the original complete image path. These options change request layout, not the
required reviewer response or approval rules.

## Optional reviewer snapshots

`reviewLayout: "snapshots"` is retained for explicit experiments and requires
continuous projection. Stable capability/evidence views have local ids and each
candidate gets a complete current snapshot. Only the current authorized context
confers source eligibility. The stream is bounded to the current turn and 64 KiB;
old reviewer verdicts and private reasoning are never retained. Capacity/reset
rebuilds the input. An individually oversized request is sent intact but not
retained. Fixed-format repair notices preserve the retry prefix without keeping
invalid model output.

Trials improved hit rates but showed variable output cost, including one
4096-token reply without usable JSON. This layout is not the default. Cross-turn
reviewer histories, context-only histories and view-only retention were not
adopted. They remain reproducible in the manual lab evidence.

For projected turns, source authority is rechecked after model/reviewer calls
and before each operation in a batch. Revocation stops remaining work while
preserving operations that actually completed. Reusing input never grants
permissions. Background generation also stops before further reviewer calls
when the source fence changes; normal cancellation is not a user-facing error.

## Background experiments

`contextControl.backgroundProjection` is an advanced, **default false** option,
also gated by the continuous-context setting. It can retain bounded input-only
planning and reviewer views in memory. These views are bound to model/persona,
policy fences and every retained source revision; cancellation clears them.
Authority is checked during generation and before accepting its queue. Model
usage is recorded immediately, including a call followed by cancellation or
review failure, with ownership markers preventing duplicate host accounting.

The October 29 synthetic trials reduced missed input with this background
option, but their increased output cost erased the saving. It is not enabled in
the normal workflow. The dashboard does not recommend or enable it automatically.

## Validation and measurements

`Hibernation_ContextEvents`, `Hibernation_SessionProjection`, native-tool,
protocol, vision and focus tests use local fixture providers and make no paid
calls. New metrics/ledger tests check weighted token totals, missing or
inconsistent cache data, interrupted streams and shared budget limits. The
ordinary build test selection includes these context and accounting checks.

Live lab tools are explicit manual experiments, not startup tasks or CTest tests.
They use an authorized deadline and shared durable request/token ledger, public
persona data, synthetic experiences and separate empty encrypted archives.
Configured credentials are decrypted only into environment memory; no keys,
headers or private model reasoning are stored. Saved request bodies contain only
the synthetic lab material. The activity probe never executes its plans.

Experiments and actual provider usage are summarized by `tools/CacheEventReport.mjs`.
Reports separate cold requests from steady subsets, weigh hits by input tokens,
and include output tokens and estimated whole-task fees. Missing metrics remain
unknown. Estimated prices are not account debit measurements.

The first equal-call snapshot application sample showed a 19.23% estimated
saving, but a later equal-call sample was essentially flat (0.64% lower). Fixed
candidate repetitions exposed output variance and a truncated reply; the initial
saving must not be treated as a guarantee.

The selected direct layout reduced missed input by 41.13% and total input by
22.99% in a fixed eight-candidate comparison, with a 17.26% estimated fee saving.
A related direct-layout comparison saved about 5.6%. Six plugin-state scenes
verified sleeping/walking, video playback and item existence; all three false
claims were rejected with either layout. The final four-turn application sample
had 85.57% weighted input hits, but used 21 calls instead of the baseline's 15
because of additional queries, private intentions and legitimate reply rewrites.
Its total fee was higher. Input reuse is verified; total fee and the number of
operations still depend on character/reviewer choices. A 98% aggregate hit rate
is not promised. See [the work log](cache-event-worklog.md).
