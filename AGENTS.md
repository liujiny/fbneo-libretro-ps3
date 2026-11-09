# Working notes for this checkout

## Check the upstream optimisation log first

`liujiny/Salvia` documents every CV1000 optimisation it tried in
`docs/optimization/` (162 files, one per experiment, each with measurements and
rejected alternatives). Before designing a fix for a CV1000 performance or
correctness problem here, search that directory:

```
https://api.github.com/repos/liujiny/Salvia/contents/docs/optimization
```

Its conclusions that already apply to this port:

* the dispatcher costs ~1,080 host cycles per block, ~250 of which are generated
  code - block linking is the lever that matters;
* `nInterleave` 240 -> 60 removes 75% of slice transitions and short-budget
  fallbacks (Cave hardware has no per-scanline interrupt);
* the interpreter fallback family is `0x60xx`, addressed upstream by a direct
  decode fast path rather than by widening the recompiler.

## Console conventions

* `retroarch-salamander.cfg` is in `USRDIR` and is rewritten by the frontend on
  exit: pin it after the quit, before the mount.
* webMAN launch needs `/mount.ps3/dev_hdd0/game/SSNE10001/` with a trailing
  slash, and may need several attempts.
* JIT tables (`Sh3DrcPreallocate()`) and VRAM must be allocated before the driver
  takes its big buffers, or the recompiler silently degrades to the interpreter.

## Release build and deploy

`docs/ps3_release_and_deploy.md` is the authoritative recipe: release
configuration, exact compile/link commands, atomic FTP deploy, benchmark command,
the pitfalls that cost real time (allocation order, launcher config path, webMAN
trailing slash, stale app instances, unusable fine-grained mftb spans) and the list
of optimisation directions already excluded with their evidence. Read it before
changing the build flags or re-running an experiment.