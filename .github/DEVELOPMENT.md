# Trunk-Based Development Policy

This project practices **Trunk-Based Development (TBD)** as described in [trunkbaseddevelopment.com](https://trunkbaseddevelopment.com/).

## Core Principle

- **Single source of truth:** All development converges on `master` branch.
- **Short-lived feature branches:** Branches exist only for code review (1–3 days max).
- **Build never breaks:** Pre-commit and CI gates ensure compilation and test success.
- **Release directly from trunk:** Releases tagged from `master`; no long-lived `release/*` branches.

## Workflow

### 1. Feature Development

**For solo/small changes (≤1 day):**

```bash
git checkout -b feature/short-description
# ... make atomic commits ...
git push origin feature/short-description
# Open PR → CI passes → merge to master
```

**Commit frequently to master** — the goal is multiple commits per developer per day into trunk.

### 2. Code Review & CI Gates

Every commit to `master` (or PR merge) must pass:

- ✅ `just lint` — code, docs, YAML, CMake, shell, justfile formatting/linting
- ✅ `just test` — integration tests (headless Vulkan rendering)
- ✅ `just test-asan` — memory safety (CPU/RAM)
- ✅ `just test-validation-layers` — memory safety (GPU/VRAM)
- ✅ GitHub Actions CI — automated lint, build (Debug/Release), tests

**Local equivalent:**

```bash
just check                          # format + lint + tests
just build-asan && just test-asan   # memory checks (optional, informational)
```

### 3. Release Strategy

**Versioning:** Semantic versioning tags (`v0.x.y`).

**Release process:**

1. Ensure `master` is in a releasable state (CI green).
2. Create and push a tag:
   ```bash
   git tag -a v0.2.0 -m "Release v0.2.0: Memory safety instrumentation"
   git push origin v0.2.0
   ```
3. GitHub Actions `release.yml` automatically:
   - Builds Release and Debug artifacts
   - Creates a GitHub Release with binaries and release notes
   - Publishes archives to release assets

**No backporting, no long-lived release branches.** Bugs in production are fixed forward on `master` and tagged as a patch release (v0.2.1, v0.2.2, etc.).

### 4. Hotfix Workflow

**Production bug fix:**

1. Create a branch from `master`:
   ```bash
   git checkout -b fix/critical-bug
   ```
2. Fix, test locally (`just test`, optionally `just test-asan`).
3. Commit and push to `master` (via PR or direct, depending on team size).
4. Tag a patch release:
   ```bash
   git tag -a v0.2.1 -m "Hotfix: Critical Vulkan validation error"
   git push origin v0.2.1
   ```
5. Release workflow publishes binaries immediately.

**No `release/0.2.x` branch persists.** The fix is live on `master` and tagged.

## Feature Flags & Branch-by-Abstraction

For **longer features** that span multiple days or parallel development:

- **Feature flags:** Wrap incomplete features in compile-time or runtime toggles (e.g., `#if FEATURE_VULKAN_1_3_COMPAT`).
- **Branch-by-abstraction:** Introduce new interfaces on `master` **alongside** old ones. Migrate callsites incrementally. Remove old code only when fully migrated.

Example: Adding a new `GeometryFactory`:

```cpp
// On master, in parallel:
class ICOsphere { /* old code */ };  // ← still used
class GeometryFactory { /* new */ }; // ← new interface

// Gradually migrate clients to GeometryFactory
// Only remove ICOsphere after full migration
```

Benefits:
- `master` is always buildable and testable.
- No long-lived branches.
- Reviewers see incremental progress.

## Pre-Commit Checklist

Before pushing a feature branch or committing to `master`:

```bash
# Auto-format and lint
just format
just lint

# Run tests locally
just test

# (Optional) Memory safety
just build-asan && just test-asan
just test-validation-layers

# Review commits
git log --oneline origin/master..HEAD
```

## Commit Message Format

Use **Conventional Commits** for clarity:

```
type(scope): short description

Detailed explanation (optional).

Fixes #123
```

**Types:**
- `feat`: New feature
- `fix`: Bug fix
- `refactor`: Code restructuring (no behavior change)
- `ci`: CI/CD updates
- `docs`: Documentation
- `test`: Test improvements
- `chore`: Tooling, dependencies

**Examples:**
```
feat(engine): add Vulkan 1.3 synchronization primitives
fix(tests): deterministic random seed for reproducible results
refactor(memory): consolidate VMA allocator patterns
ci: add optional memory safety checks (ASan/UBSan, Validation Layers)
docs(tooling): expand memory safety diagnostics guide
```

## Branching Strategy (Visual)

```
master  ●──●──●──●──●  (always green)
         ╱  ╱  ╱  ╱  ╲
feature ●    ●    ●    ●  (short-lived, <3 days)
```

vs. **Anti-pattern (Gitflow):**

```
master  ●──●──●──●──●──●──●
         ╱              ╲
develop ●──●──●──●──●──●  (long-lived merge hell)
        ╱  ╱  ╱  ╱  ╱  ╱
feature ●──●  ●──●  ●──●  (accumulate conflicts)
```

## Key Constraints

1. **Build must never break.** If CI is red, fix within minutes (revert if necessary).
2. **Tests must be deterministic.** Flaky tests undermine TBD.
3. **Code review must be swift.** Aim for turnaround <2 hours.
4. **No feature branches older than 3 days.** Rebase/merge frequently.

## Scaling Considerations

For small teams (1–3 devs): **Direct commits to `master`** are acceptable if the build is rock-solid and PRs are reviewed in <1 hour.

For larger teams: **Short-lived feature branches + PR review** ensure parallel development without merge conflicts.

## References

- [Trunk-Based Development](https://trunkbaseddevelopment.com/)
- [Continuous Integration (CI)](https://trunkbaseddevelopment.com/continuous-integration/)
- [Continuous Delivery (CD)](https://trunkbaseddevelopment.com/continuous-delivery/)
- [Feature Flags](https://trunkbaseddevelopment.com/feature-flags/)
- [Branch-by-Abstraction](https://trunkbaseddevelopment.com/branch-by-abstraction/)
