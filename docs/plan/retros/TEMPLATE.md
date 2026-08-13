# Retro — Sprint X.Y — <name>

**Epic:** EN <name> · **Gates:** <ids, or none>

## Delivered vs. exit criteria

| Exit criterion | Result |
| -------------- | ------ |
| … | ✅ / ⚠️ / ❌ |

### Not delivered

State explicitly, or "nothing". Silence here is how scope quietly disappears.

## Gate validation

One block per gate in this sprint. Outcome is `passed`, `failed`, or `deferred` — never "done".

### GN — <name>

| | |
| --- | --- |
| **Question** | |
| **Threshold** | |
| **Measured** | |
| **Outcome** | |

Evidence, and what it means for the next sprint.

*(If the sprint carried no gate, say so.)*

## Design amendments

Where reality contradicted `simulator-design.md` or `simulator-spec.yaml`. Each entry names the
file and section changed, and why. **Apply them in the same change as this retro** — a design of
record that drifts from the implementation is worse than none.

| Document | Change | Why |
| -------- | ------ | --- |

## Revised risks

What got scarier, what stopped mattering, what is new.

## Carry-over

Work moving into the next sprint, with the reason. "Ran out of time" is a reason; write it.

## Metrics

| | |
| --- | --- |
| Checks | |
| Line coverage | |
| Branch coverage | |
| `make check` | |
