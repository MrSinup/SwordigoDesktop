# OpenSwordigo Contributor License Agreement (CLA)

**Version 1.0 — Effective Date: September 12, 2026**

Thank you for your interest in contributing to **OpenSwordigo** and the **SwordigoDesktop** ecosystem (including Ruby GG Studio IDE, Swordfare, and the Swordigo Runtime Environment). 

This Contributor License Agreement ("Agreement" or "CLA") establishes the intellectual property, copyright, and licensing terms under which intellectual property is submitted to OpenSwordigo repositories. This Agreement exists to protect your rights as an independent author while granting OpenSwordigo Org the enduring legal authority required to maintain, compile, distribute, and protect the project.

---

## 1. Definitions

* **"OpenSwordigo Org"** (or "Organization"): The governing organization, repository maintainers, and project stewards.
* **"Lawncher Team"**: The joint development and reverse-engineering team holding joint All Rights Reserved rights over the SRE subsystem.
* **"Contributor"** (or "You"): Any individual, collective, or legal entity that submits, proposes, or merges code, documentation, assets, or patches to any repository managed by OpenSwordigo Org.
* **"Contribution"**: Any source code, header, patch, documentation, tool, script, asset, or pull request submitted via version control, email, or discussion channels to OpenSwordigo Org for inclusion in the project.
* **"Project"**: The collective software suite including SwordigoDesktop, the Ruby and Ruby GG Studio IDE, Swordfare launcher and overlay, tools, and the Swordigo Runtime Environment (SRE).

---

## 2. Contributor Copyright Retention (50/50 Dual Ownership)

OpenSwordigo Org operates on an **author-respecting, non-exclusive dual-ownership model**:

1. **You Keep Your Copyright**: You retain full copyright ownership of the original text, logic, code, and inventions authored by you in your Contribution. Nothing in this Agreement transfers your underlying copyright ownership exclusively to OpenSwordigo Org.
2. **Freedom of Personal Reuse**: You remain entirely free to reuse, relicense, adapt, extract, or publish your own contributed code independently outside of this repository, in your own private projects, mods, or portfolios, without paying royalties or requesting permission from OpenSwordigo Org.

---

## 3. License Grant to OpenSwordigo Org

In exchange for inclusion in the official project and community distribution:

1. **Grant of Copyright License**: You hereby grant to OpenSwordigo Org an irrevocable, perpetual, worldwide, non-exclusive, no-charge, royalty-free license to reproduce, prepare derivative works of, publicly display, publicly perform, sublicense, compile, package, and distribute your Contribution as part of the official OpenSwordigo software releases.
2. **Sublicensing & Packaging Authority**: You grant OpenSwordigo Org the authority to bundle your Contribution into release packages, binaries, installers, and multi-component distributions according to the project’s architectural roadmap.
3. **Irrevocability**: Once a Contribution is submitted and merged into an official branch of the project, **this license grant is perpetual and irrevocable**. You agree that you cannot retroactively rescind, recall, or demand the deletion of your merged code, nor file copyright infringement claims or DMCA takedown requests against OpenSwordigo Org or its downstream users for code submitted under this Agreement.

---

## 4. Patent & Intellectual Property Grant

You hereby grant to OpenSwordigo Org, and to all recipients of software distributed by OpenSwordigo Org, a perpetual, worldwide, non-exclusive, no-charge, royalty-free, irrevocable patent license to make, have made, use, offer to sell, sell, import, and otherwise transfer the Work, where such license applies only to those patent claims licensable by You that are necessarily infringed by Your Contribution alone or in combination with the Work.

---

## 5. Architectural Tier Rules & Invariant Boundaries

Contributions must strictly observe the multi-license directory structure of the repository:

| Repository Tier | Target Directories | Invariant License Rule | Permitted Changes |
| :--- | :--- | :--- | :--- |
| **Tier 1: SRE Core** | `src/sre/` (`sre12`, `sre13`, `extras`, `base`) | **All Rights Reserved (ARR)** | Contributed code merges into the proprietary SRE framework. Authors retain dual ownership of their diffs; OpenSwordigo Org and Lawncher Team retain joint stewardship and distribution authority. |
| **Tier 2: Studio & Tooling** | `src/ruby/`, `src/launcher/`, `src/platform/`, `src/tools/`, `tools/` | **GNU General Public License v3.0 (GPLv3)** | Strong copyleft. Any modifications or derivative works **must remain licensed under GNU GPLv3**. Down-licensing to permissive terms (e.g. MIT) is prohibited. |
| **Tier 3: Host Infrastructure** | `src/jni/`, `src/android/`, `src/loader/`, `src/srehost/` | **MIT License** | Strictly permissive. Contributions remain under standard MIT terms to ensure platform shims and loaders can be ported to other operating systems and consoles without restriction. |

*Any pull request that crosses tier boundaries must maintain clean commit separation between tiers.*

---

## 6. Contributor Representations & Warranties

By submitting a Contribution, you represent and warrant that:

1. **Original Work**: The Contribution is either entirely your own original creation, or you have received explicit written permission from the copyright owner to submit it under the terms of this Agreement.
2. **Clean-Room Integrity**: The Contribution does **not** contain unauthorized third-party proprietary source code, decompiled game source code directly copied from commercial software without clean-room re-implementation, or assets that violate intellectual property rights.
3. **No Hidden Encumbrances**: To the best of your knowledge, your Contribution does not infringe upon any third party's copyright, trademark, trade secret, or patent rights.
4. **Employer Consent**: If your employer or academic institution has intellectual property rights over work you produce, you represent that you have received permission to make the Contribution on their behalf, or that your employer has waived such rights.

---

## 7. Governance, Stewardship & Decision-Making Authority

1. **Project Direction**: OpenSwordigo Org and its members act as the decision-making body regarding project architecture, release schedules, build targets, community infrastructure, and repository management.
2. **Relicensing Prerogatives for SRE**: Because SRE is held under All Rights Reserved terms, OpenSwordigo Org and Lawncher Team reserve the joint sole authority to adjust SRE distribution models, maintain closed components, or publish companion tools without requiring individual approval from historic patch submitters, provided original contributor authorship is honored.
3. **Sole Decision-Making Body**: OpenSwordigo Org and Lawncher Team constitute the sole decision-making authority for the project.

---

## 8. Acceptance of Terms

You manifest acceptance of this Agreement by:
- Submitting a Pull Request, patch, or issue containing code to any OpenSwordigo repository;
- Signing or acknowledging this document via digital signature, Git commit sign-off (`Signed-off-by:`), or PR description; or
- Merging contributions into the repository with the consent of the maintainers.
