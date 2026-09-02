# M2022 AirBridge — working agreement for Claude

A production Printer Application for a USB-only Samsung SL-M2022: our own IPP/AirPrint front
end (PAPPL), raster and halftone pipeline, SPL/QPDL encoder and USB transport. Learning is a
goal, not a by-product. No interim bridges, no throwaway code (ADR-001).

## Start of every session

1. Read `PROGRESS.md` first: where we are, the exact next task, the session log. Then read the
   `SPEC.md` sections it points to and the `docs/*.md` for the modules you will touch. Do not
   re-derive facts that are recorded there; they were measured on the real hardware.
2. Confirm the build is green before changing anything:
   `export PATH=/opt/homebrew/bin:$PATH; cmake --build build && ctest --test-dir build -LE hardware`
   (configure first with `cmake -S . -B build -G Ninja` if `build/` is missing).
3. "Let's continue" means: do the **Next up** item in `PROGRESS.md`, then report.

## While working

- `SPEC.md` is the design; `docs/adr/` holds the decisions. To change a decision, add a dated
  update section to its ADR (or a new ADR) and update `SPEC.md` in the same change.
- Pure modules (`raster/`, `halftone/`, `qpdl/`): no I/O, deterministic, unit-tested. Every
  protocol constant carries a comment citing its evidence (fixture, document, or spec).
- Clean-room (ADR-010): never copy, translate or paraphrase SpliX, jbigkit or vendor code. Sources
  are published specifications, the SpliX SPL2 document, and our captured bytes.
- Tests ship in the same change. Run them and report real output. Hardware tests
  (`ctest --test-dir build -L hardware`) only when the user has said the printer is on; they
  print pages.
- Style: C17, `.clang-format`, warnings are errors, sanitizers on in Debug.
- Git: commit only when the user asks, otherwise propose the message. No AI attribution anywhere:
  no Co-Authored-By trailers, no "Generated with" lines.
- When something needs the physical printer or the iPhone, say exactly what to do and what
  output to paste back, then continue with everything that does not depend on it.

## When a task is done

1. `PROGRESS.md`: milestone table, **Next up**, a dated session-log entry (what, commits, facts
   learned), and the environment notes if the machine changed.
2. `SPEC.md`: answer open questions (section 16), update risks and the milestone text where
   reality differed from the plan.
3. `docs/`: update the doc for the module touched (`docs/README.md` is the index). Milestone-
   level work gets a "What this teaches" note in its doc.
4. `README.md`: keep Features, Planned, Compatibility, Commands and How it works accurate; the
   status paragraph and the roadmap live in `PROGRESS.md`, which the README links to. It must
   read well to a newcomer.
5. Report: outcome first, real test output, what is next.

## Map

- `SPEC.md` design · `PROGRESS.md` state · `docs/` explanations · `docs/adr/` decisions ·
  `docs/archive/` the original spec
- `src/cli` commands · `src/common` version, json · `src/usb` transport · `src/cups` queue view ·
  `src/qpdl` records, walker, codec · `include/m2022/` public headers
- `tests/unit` (CTest, `m2022_test.h`) · `tests/hardware` (label `hardware`)
- `fixtures/pages` generated inputs · `fixtures/oracle/samsung` captured vendor output (golden)
- `scripts/` fixture generation, vendor capture, survey · `artifacts/` local only (gitignored)
