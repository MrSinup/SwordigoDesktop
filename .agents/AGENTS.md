# Universal Agent Architecture & Workflow Rules

## 1. Workspaces & Repositories Architecture
- **Working Directory (`/home/quantumcreeper/SwordigoDesktop`)**:
  - This is a **fast Linux ext4 working directory**, NOT a Git repository.
  - All active C++ development, CMake builds, QML UI testing, shader edits, and mobile/desktop compilation happen HERE first.
- **Primary Git Repository (`/run/media/quantumcreeper/TVPG/Prenxy Packages/SwordigoDesktop`)**:
  - This is the **official Git repository** for SwordigoDesktop located on the external NTFS TVPG drive.
  - Remote: `https://github.com/TheAevoraLabs/SwordigoDesktop.git` (branch: `master`).
- **Mobile Distribution Repository (`/home/quantumcreeper/RubyTouch`)**:
  - Standalone, lightweight (~12 MB) Git repository for the Ruby Touch mobile edition on fast ext4 storage.
  - Remote: `https://github.com/TheAevoraLabs/RubyTouch.git` (branch: `master`).
- **Mobile TVPG Mirror (`/run/media/quantumcreeper/TVPG/Prenxy Packages/RubyTouch`)**:
  - Synchronized NTFS backup mirror of RubyTouch on the TVPG drive.

## 2. Source Code Editing Discipline (MANDATORY FOR ALL AI AGENTS)
- **DO NOT EDIT `src/` DIRECTLY IN THE TVPG REPOSITORIES OR IN RUBYTOUCH**:
  - Any and all source code changes (`src/`, C++ headers/implementations, QML files, CMakeLists.txt, shaders, bridges) **MUST** be performed first in the working directory (`/home/quantumcreeper/SwordigoDesktop`).
  - Verify and test builds in the working directory before syncing out.
- **Exception for Non-Source Files**:
  - Agents ARE permitted to edit documentation (`README.md`, `LICENSE.md`, `fastlane/` metadata) and CI workflows (`.github/workflows/`) directly in their respective target repositories.

## 3. Sync & Commit Cadence
- **Syncing to TVPG**:
  - Changes in the ext4 working directory accumulate during development shifts.
  - Every 1-2 days, or upon milestone completion, and **ONLY with user approval**, the agent syncs the ext4 working directory to the NTFS TVPG repository and commits/pushes to GitHub.
- **Updating RubyTouch**:
  - `RubyTouch` is updated **only after** the TVPG `SwordigoDesktop` repository has been updated and synchronized.
  - Use `tools/sync_rubytouch.sh` or the automated GitHub Action `.github/workflows/sync_rubytouch.yml` to propagate updates.

## 4. One-Shot Context & Sync Tool
- Agents can run:
  ```bash
  python3 /home/quantumcreeper/.agents/get_repo_context.py
  ```
  to inspect working directory vs TVPG Git repo vs RubyTouch sync status in a single shot.
