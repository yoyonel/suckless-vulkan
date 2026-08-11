# Suckless-Vulkan Development Guidelines

## 🚫 Golden Rules

1. **NEVER push to `origin/master`** — Not in any case, under any circumstance
2. **Format + Lint + Tests REQUIRED** — All must pass before commit
3. **Docs always in sync** — Update or create docs with every feature/fix
4. **Pre-commit checks** — Enforce via `just pre-commit-install`
5. **CI/CD validation** — All builds/tests must pass locally (Docker) AND remote (GitHub Actions)
6. **NO suppression of warnings/errors** — Fix issues at the source; never bypass them
7. **INTERDICTION ABSOLUE DE MERGER UNE PR** — Zéro action de merge tolérée. Le merge est la responsabilité stricte et exclusive du développeur.
8. **Zéro commit/push sans accord explicite** — Toujours demander validation avant toute altération de l'historique distant.

---

## 📝 Commit Discipline

### Separation of Concerns (SoC) for Commits

Each commit must represent one coherent change and one change theme.

- One concern per commit: do not mix unrelated topics (e.g., CI + feature logic + docs)
- Split by intent: `feat`, `fix`, `refactor`, `docs`, `ci`, `build`, `test`, `chore`
- Keep commits reviewable: small, focused, and understandable independently
- If multiple concerns are touched, create multiple commits in a logical order
- Commit history must explain project evolution step by step

Examples:
- ✅ `fix(coverage): remove stale python formatter call in gcovr CI script`
- ✅ `docs(process): enforce git status checks before commit and handoff`
- ❌ `fix: update CI + refactor engine + rewrite docs`

### Conventional Commits Format

Follow [Conventional Commits](https://www.conventionalcommits.org/en/v1.0.0/) strictly:

```
<type>(<scope>): <subject>

<body>

<footer>
```

**Types** (based on project specifics):
- `feat`: New feature (keyboard control, shader, logging, etc.)
- `fix`: Bug fix
- `refactor`: Code restructuring without feature change
- `test`: Test suite additions/modifications
- `docs`: Documentation only
- `build`: Build system, CMake, dependencies
- `ci`: GitHub Actions, Docker, CI pipeline
- `chore`: Dependencies, config, tooling, git config
- `perf`: Performance optimization

**Scope** (optional but recommended):
- `logging`, `controls`, `vulkan`, `shader`, `memory`, `ci`, `docs`, `coverage`, etc.

**Examples:**
```
feat(controls): Add F11 fullscreen toggle with state restoration

fix(logging): Correct thread ID formatting in log messages

docs(coverage): Add LLVM-Cov section with KPI metrics

ci(github-actions): Add coverage-report-llvm job with artifact uploads

refactor(engine): Extract shader compilation logic to separate function
```

### Pre-Commit Workflow

1. Run `just pre-commit-install` (one-time setup)
2. Make code changes
3. Run `just format` → fixes formatting automatically
4. Run `just lint` → must pass with no errors and no warnings
5. Run `just test-all` → all tests must pass
6. Run `just pre-push-run` → final check before commit
7. Run `git status --short --branch` and review all local changes
8. Ensure all remaining files are either:
   - intentionally staged for the current commit, or
   - explicitly deferred and communicated as next step
9. Use `git add` + `git commit` (hooks will verify format/lint/tests)
10. **NEVER** `git push` — wait for user approval

If any hook fails:
- Read the error message
- Fix the issue
- Retry the commit

Warning handling policy during lint execution:
- A lint command with `exit code 0` is NOT considered successful if warnings are present.
- Warnings must be treated and fixed before moving to the next step or committing.
- Use output checks when needed (example: `just lint 2>&1 | grep -i warning`) to confirm warning-free runs.
- `clang-tidy` warnings (including `performance-*`) are blocking and must be fixed, not deferred.
- During GitHub Actions monitoring, always run `just ci-docker-all` locally in parallel and fix local warnings/errors immediately to preempt remote failures.

---

## 📚 Documentation Strategy

### Update Existing Docs
- Feature/fix updates existing behavior → Update the relevant doc
- Examples: `docs/tooling.md`, `docs/ci_cd.md`, `docs/runtime_controls_logging.md`
- Always verify section structure after edit

### Create New Docs
- New feature/module/subsystem → Create `docs/<feature>.md`
- Update `mkdocs.yml` `nav:` section to include new doc
- Use clear hierarchy: H1 (feature name), H2 (subtopics), H3 (details)

### Documentation Best Practices

1. **No local filesystem links** — All docs assume web deployment
   - ✅ Use relative markdown links: `[Section](tooling.md)`
   - ✅ Use code block language tags: `` ```bash `` or `` ```python ``
   - ❌ No `/home/user/...` or `file:///` URLs
   - ❌ No untagged code blocks (MD040 lint fails)

2. **Diagrams & Visuals** — Use mermaid for flowcharts, architecture, state diagrams
   - Flowchart example:
     ```mermaid
     graph LR
       A[Input] --> B{Process} --> C[Output]
     ```
   - State diagram for mode transitions (fullscreen, logging levels, etc.)
   - Architecture diagrams for system interaction

3. **Examples & Snippets** — Always tag with language and context
   - Show command usage: `` ```bash ``
   - Show code: `` ```cpp ``, `` ```python ``
   - Show output: `` ```text ``

4. **Table of Contents** — Maintain clear structure for `mkdocs.yml`
   - Ensure all `.md` files are indexed
   - Keep nav in logical order (Architecture → Tooling → CI/CD → Advanced)

---

## 🏗️ Build & Tooling

### Just Recipes (Preferred)
- **All major tasks via `just`** — No raw cmake/make commands
- **Every recipe documented** — Docstring comments visible in `just help`
- Examples: `just build`, `just test-all`, `just coverage-llvm`, `just lint`

### Justfile Simplicity Rule

- Keep `justfile` recipes as orchestration glue (high-level command chaining).
- Do not embed long shell loops, branching-heavy workflows, or business logic directly in `justfile`.
- Move non-trivial shell logic to versioned scripts under `scripts/` and call them from `just` recipes.
- Scripts referenced by `justfile` must stay small, readable, shellcheck-clean, and covered by existing lint flow.
- Exception: 1-3 straightforward shell lines are acceptable inline when readability is clearly better than a separate script.

### CMake (Dependency Management)
- CMake used for:
  - Dependency discovery (`find_package`)
  - Build configuration (`ENABLE_COVERAGE`, `ENABLE_SANITIZERS`, `ENABLE_LLVM_COV`)
  - Target definitions (executables, libraries)
- Do NOT use CMake for task automation → use `just` instead

### Compiler Strategy
- **Primary:** Clang (for LLVM tooling, coverage, sanitizers)
- **Optional:** GCC builds supported, not required
- **CI Matrix:** Release build with Clang; Debug with Clang
- Future: Can add GCC dual-build if needed

### Standard Tools
- **Format:** `clang-format` (C/C++/GLSL)
- **Lint:** `clang-tidy` (C/C++), `shellcheck` (shell), `yamllint` (YAML), `hadolint` (Docker)
- **Coverage:** `llvm-cov` (preferred, clang-only), `gcovr` (alternative, GCC-compatible)
- **Test:** CTest (CMake native)
- **Package:** uv (Python deps in CI)
- **Text processing:** Use standard shell tools → `sed`, `tr`, `column`, `awk`, `grep` (NOT custom Python scripts)

### Principle: Minimize Custom Scripts

**Strongly prefer existing tools over custom scripts** (Python, Bash, etc.):

✅ **Use standard tools:**
- `llvm-cov report` outputs formatted table directly (no script needed)
- `column`, `sed`, `tr` for text transformation
- `jq` for JSON processing (if unavoidable)
- Built-in shell utilities

❌ **Avoid custom scripts unless:**
- No standard tool exists for the task
- Exception is thoroughly documented
- Script is simple, maintainable, and tested
- Value is clear (don't script for scripts' sake)

**Examples of anti-patterns:**
- Custom Python formatter for tools that already output formatted text
- Bash wrapper around single command
- Re-implementing standard utilities

**Benefit:** Reduce dependencies, improve maintainability, easier CI/CD portability

---

### Principle: No Suppression of Warnings/Errors

**NEVER bypass linting or compiler warnings using suppression mechanisms:**

❌ **Forbidden suppression patterns:**
- `// NOLINT`, `# noqa`, `# type: ignore`, `// NOLINTNEXTLINE`
- `ignore:` lists in `.hadolint.yaml`, `.hadolint.yaml`, `pylintrc`, etc.
- `ignore: DL3008 ...` params passed directly to linting actions
- `/* clang-tidy-disable */` blocks
- CMake `set_source_files_properties(... SKIP_LINTING ON)`
- GitHub Actions `continue-on-error: true` to hide failures

✅ **Correct approach: fix the root cause:**
- `DL3008` (apt pin): Pin package versions explicitly in the Dockerfile
- `DL3013/DL3813` (pip pin): Pin pip versions explicitly or use a `requirements.txt`
- `clang-tidy` warning: Refactor the code to eliminate the pattern
- Compiler warning: Fix the actual issue (cast, unused variable, narrowing, etc.)

**Exception policy:** If suppression is the **only viable path**, it requires:
1. An explicit comment **in the code** explaining why the fix is not possible
2. A clear assessment confirming no alternative exists
3. A tracking note to revisit and remove the suppression when possible
4. User explicit validation before committing

No silent suppression is ever acceptable.

---

## 🔨 Code Quality Gate

Before **every commit**, verify:

```bash
# Format all files
just format

# Lint (must pass with zero warnings)
just lint

# Run all tests
just test-all

# Optionally verify coverage
just coverage-llvm
```

### Pre-Commit Hook Verification
```bash
just pre-commit-run
```

### Pre-Push Hook Verification
```bash
just pre-push-run
```

If any step fails → Fix and retry.

---

## 🚢 CI/CD Validation

### Local CI (Docker, mimic GitHub Actions)
```bash
just ci-docker-all
```
This runs:
- Static checks (hadolint, actionlint)
- Lint (in container)
- Build + test (Release/Debug matrix)
- Memory checks (ASan/UBSan)
- Validation Layers (Vulkan)
- Coverage report (LLVM-Cov)

### Remote CI (GitHub Actions)
- Automatically triggered on `push` + `pull_request` to `master`
- Same jobs as local Docker
- Artifacts uploaded (coverage HTML, test frames)

### PR Automation (Explicit Human Order Required)

This workflow is allowed **only** when the user explicitly asks for it (example: "push", "create PR", "open PR", "follow Actions").

**Hard rule:**
- Do not push branches, create PRs, or monitor GitHub Actions unless the human explicitly requests it in the current conversation.

When explicitly requested, follow this sequence:
1. Verify repository state:
   - `git status --short --branch`
   - confirm current branch and remote target
2. Push feature branch (never `origin/master`):
   - `git push -u origin <feature-branch>`
3. Create PR with title + detailed description:
   - Use GitHub CLI (`gh pr create --base dev`) or equivalent tool integration
   - Base branch must be `dev` (NEVER target `master` directly for PRs)
4. **Parallel validation strategy** (CRITICAL for speed):
   - **Immediately after push**, launch local CI in background: `just ci-docker-all` (runs full matrix locally)
   - **Simultaneously**, monitor GitHub Actions remotely (check runs, job status)
   - **Early problem detection**: If local CI fails → fix immediately, no need to wait for remote
   - If error found locally → fix code → `just format && just lint && just test-all` → commit → push to re-trigger remote
   - By the time remote CI validation completes, code is already corrected (zero wasted cycles)
5. Monitor results:
   - Local CI: Full verbose output on-machine for debugging
   - Remote CI: Check all workflows and wait for terminal status (`SUCCESS` / `FAILURE`)
   - **MANDATORY**: You MUST actively verify the actual status of all remote CI/CD jobs via `gh pr checks` before returning control to the user. You are STRICTLY FORBIDDEN from declaring a task complete or saying "Everything is fine / I am satisfied" without empirical, 100% GREEN (PASSED) verification from CI/CD.
    - Report a concise summary of each workflow result to the user
6. If any workflow fails (local or remote):
   - treat as blocked
   - investigate, fix, re-run required checks, and update PR

### Mandatory Human Approval Before Commit Policy
- **STRICT MANDATORY RULE**: You are STRICTLY FORBIDDEN from executing `git commit` or `git push` automatically without first presenting your changes and asking for explicit human validation.
- Always wait for the user to explicitly confirm before running any commit or push command.

### Coverage Requirements
- Minimum **70% line coverage** for commits to be acceptable
- Use `just coverage-llvm` for detailed analysis (recommended over `gcovr`)
- Report files: `build/coverage-llvm/coverage_report/index.html`

### Failing Builds = Blocked
- If any CI job fails (locally or remotely) → Fix before moving forward
- No workarounds or skipping checks

---

## 📋 Commit Checklist

Before running `git commit`:

- [ ] Code formatted: `just format` ✅
- [ ] No lint errors: `just lint` ✅
- [ ] All tests pass: `just test-all` ✅
- [ ] Git working tree reviewed: `git status --short --branch` ✅
- [ ] No unexpected leftovers (unstaged/untracked) without explicit handling plan
- [ ] Docs created/updated (if feature/design change)
- [ ] `mkdocs.yml` updated (if new doc added)
- [ ] No local filesystem links in docs
- [ ] Commit message follows Conventional Commits format
- [ ] Pre-commit hooks pass: `just pre-commit-run` ✅

Before handing back control in chat/prompt:

- [ ] Re-check repo state with `git status --short --branch`
- [ ] Clearly report what is done, what remains modified, and what is deferred

---

## 🔄 Typical Feature Workflow

1. **Create feature branch** (short-lived, <3 days)
   ```bash
   git checkout -b feat/my-feature
   ```

2. **Code implementation**
   - Write code
   - Add tests
   - Format: `just format`
   - Lint: `just lint`
   - Test: `just test-all`

3. **Documentation**
   - Update existing doc OR create new `docs/<feature>.md`
   - Update `mkdocs.yml` if new doc
   - Verify no local links, all code blocks tagged

4. **Commit with Conventional Commits**
   ```bash
   git commit -m "feat(scope): Short description

   Longer explanation if needed.
   Explain the why, not the how."
   ```

5. **Verify locally**
   ```bash
   just ci-docker-all
   ```

6. **Create PR or merge to master**
   - Only on explicit human request
   - GitHub Actions runs on push
   - Monitor CI for success
   - All checks must pass

7. **NEVER** `git push origin master` — Always wait for user approval

---

## 📊 Coverage & Metrics

### Target Coverage
- **Lines:** >78% (current baseline)
- **Functions:** >85%
- **Branches:** >50%

### Measuring Coverage
```bash
# Recommended (LLVM-Cov with console output + HTML)
just coverage-llvm

# Alternative (GCovr, mostly for CI artifact uploads)
just coverage
```

### Reports Location
- LLVM-Cov: `build/coverage-llvm/coverage_report/index.html`
- GCovr: `build/coverage/reports/{coverage.txt,coverage.xml,coverage.html}`

### Interpreting Results
- Report shows all business logic files: `app_log.cpp`, `runtime_controls.cpp`, `vk_engine.cpp`, etc.
- 0% coverage on test files is expected (tests are not tested)
- Focus on coverage of testable modules, not shader/integration code

---

## 🐛 Debugging

### Local Docker CI Equivalent
```bash
# Build in container, run GCC/Clang, lint
just ci-docker-all

# Or individual steps:
just ci-docker Release
just ci-docker Debug
just ci-docker lint
```

### Sanitizer Debugging (ASan/UBSan)
```bash
just build-asan
just test-asan
```

### Validation Layer Debugging (Vulkan)
```bash
just test-validation-layers
```

### RenderDoc Observability Directive (Mandatory)

For every feature, fix, or refactor touching rendering, Vulkan resources, or GPU command recording:

- **Always add meaningful resource names** via `vkSetDebugUtilsObjectNameEXT` (through project helper wrappers) for newly created Vulkan objects:
   - Pipelines, pipeline layouts, render passes, framebuffers
   - Buffers/images/image views/samplers
   - Descriptor set layouts/pools/sets
   - Command pools/command buffers, semaphores/fences, queues
- **Always add meaningful labels** via `vkCmdBeginDebugUtilsLabelEXT` / `vkCmdEndDebugUtilsLabelEXT` around logical GPU blocks:
   - Render pass setup/bindings
   - Skybox pass, geometry pass, post-process pass
   - Upload/copy/transition/mipmap generation phases
- **Prefer hierarchical labels** (parent frame region + child pass regions) so Event Browser navigation is immediate.
- **Use stable naming conventions** (`Feature_ObjectType[_Index]`) to keep captures diff-friendly across runs.
- **Do not leave anonymous critical resources** in RenderDoc when they are part of the feature being changed.
- **Keep docs aligned**: when adding/changing labels or naming conventions, update `docs/tracing.md` in the same change.

Rationale: RenderDoc readability is a project quality requirement, not an optional debug extra.

---

## 🚫 Anti-Patterns

**Don't:**
- ❌ Commit without running `just format && just lint && just test-all`
- ❌ Use raw `cmake` commands instead of `just` recipes
- ❌ Push to `origin/master` without user approval
- ❌ Add local filesystem links in docs
- ❌ Create untagged code blocks in markdown
- ❌ Make docs-only changes without updating `mkdocs.yml`
- ❌ Forget to update docs when changing behavior
- ❌ Ignore failing CI checks — fix them, don't skip
- ❌ **NEVER leave temporary patch scripts (like python or shell files) untracked in the git working directory. ALWAYS delete them immediately after use.**

**Do:**
- ✅ Use `just` for all development tasks
- ✅ Follow Conventional Commits format
- ✅ Keep docs in sync with code
- ✅ Run local CI (`just ci-docker-all`) before claiming "done"
- ✅ Use mermaid diagrams for complex explanations
- ✅ Tag all code blocks with language
- ✅ Verify coverage >78% on changes

---

## 📞 Questions?

If unsure about commit message format, documentation structure, or CI/CD flow:
- Check existing commits: `git log --oneline`
- Check existing docs: `docs/` folder
- Run `just help` for available recipes
- Consult `mkdocs.yml` for doc structure

---

## 🔍 Code Review (/code-review)

When executing a code review, always present findings sequentially with the following detailed fields (in Caveman style):
- **Fichier**: File link
- **Finding**: Detailed description
- **Problème**: What is wrong
- **Risques**: Potential risks
- **Corrections**: Possible fixes
- **Gains**: Associated benefits
- **Testable**: Test coverage and testability

### Testing Safety
- **NEVER** run the application (`just run` or `vulkan_app`) without a `timeout` command (e.g., `timeout 5s just run`). Otherwise, the application window will stay open indefinitely and block the agent's execution.
