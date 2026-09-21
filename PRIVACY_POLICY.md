# Privacy Policy for SwordigoDesktop

**Effective Date:** September 21, 2026  
**Last Updated:** September 21, 2026  
**Project Name:** SwordigoDesktop  
**Developer / Organization:** The Aevora Labs / OpenSwordigo Contributors  
**Source Code:** [https://github.com/TheAevoraLabs/SwordigoDesktop](https://github.com/TheAevoraLabs/SwordigoDesktop)  
**Contact Email:** [aevoralabsin@gmail.com](mailto:aevoralabsin@gmail.com)  

---

## 1. Introduction

The Aevora Labs and the OpenSwordigo community develop and maintain **SwordigoDesktop**, an open-source cross-platform desktop suite comprising the Swordigo game recreation, the Ruby GG 3D scene studio, and modding toolchains for Linux and Windows.

We believe user privacy is a fundamental right. This Privacy Policy details our data practices across all components of SwordigoDesktop.

**In brief: SwordigoDesktop is 100% offline, client-side software. We do not collect, store, transmit, process, or sell any personal data, telemetry, or user analytics.**

---

## 2. Information Collection and Processing

### Zero Personal Data Collection
- **No User Registration:** You can download, compile, and run SwordigoDesktop without providing any personal information, username, email, or credentials.
- **No Telemetry or Tracking:** SwordigoDesktop contains no tracking beacons, telemetry services, crash analytics daemons, or user profiling mechanisms.
- **No Advertising:** SwordigoDesktop is completely free of advertisements, commercial analytics, and data monetization frameworks.

### Local File System Access
SwordigoDesktop interacts exclusively with your local storage to:
- Read game assets (`.pod`, `.pvr`, `.wav`, `.mp3`) and level scenes (`.scene`, `.swdm`, `.scl`).
- Save modded scenes, extracted meshes, texture exports, and user preferences locally in your application directory or user profile.
- None of your files, modifications, or projects are ever uploaded or transmitted to external servers.

---

## 3. Network and Multiplayer Communication

Certain optional components (such as the local developer bridge or community LAN server) may open network sockets:
- **Local Debugging & ADB Session:** Used exclusively to bridge tools between desktop and locally connected devices via localhost (`127.0.0.1`).
- **MultiSW Community Server:** Any multiplayer networking operates purely peer-to-peer or connects directly to user-specified local or private servers. No personal metrics or account telemetry are collected or relayed.

---

## 4. Third-Party Dependencies

SwordigoDesktop links against well-known, industry-standard open-source libraries:
- **SDL3 (Simple DirectMedia Layer):** For window creation, hardware input, and audio output.
- **Qt 6 Framework:** For graphical user interface rendering in Ruby GG.
- **OpenGL:** For 3D hardware-accelerated graphics rendering.
- **Zlib & libgit2:** For local archive decompression and offline version control integration.

None of these upstream open-source libraries collect or transmit personal information.

---

## 5. Children's Privacy

SwordigoDesktop complies with COPPA and GDPR regulations. Because no personal data is ever collected, retained, or processed, our software is safe for users of all ages, including children under the age of 13.

---

## 6. Open Source Transparency

SwordigoDesktop is published under the **GNU General Public License v3.0 (GPLv3)**. Our entire codebase is open, public, and auditable:
- [https://github.com/TheAevoraLabs/SwordigoDesktop](https://github.com/TheAevoraLabs/SwordigoDesktop)

Anyone may inspect the code to independently verify that no hidden tracking, surveillance, or telemetry exists.

---

## 7. Changes to This Privacy Policy

If this Privacy Policy is updated, changes will be committed directly to this repository with a revised "Last Updated" date.

---

## 8. Contact Information

For any inquiries or questions concerning this policy, reach out to:
- **Email:** [aevoralabsin@gmail.com](mailto:aevoralabsin@gmail.com)
- **GitHub Issues:** [https://github.com/TheAevoraLabs/SwordigoDesktop/issues](https://github.com/TheAevoraLabs/SwordigoDesktop/issues)
- **Organization:** [https://github.com/TheAevoraLabs](https://github.com/TheAevoraLabs)
