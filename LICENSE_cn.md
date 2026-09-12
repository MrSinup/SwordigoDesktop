# SwordigoDesktop 多重许可声明 (Multi-License Notice)

**OpenSwordigo 项目许可框架 (Licensing Framework)**

> **英文原版文件**: [English (LICENSE.md)](LICENSE.md) | [हिन्दी (Hindi)](LICENSE_hi.md) | [Français (French)](LICENSE_fr.md)

本代码仓库是一个复合型开源/专有项目，各组件分别遵循三类不同的许可条款：
1. **GNU 通用公共许可证第 3 版 (GPLv3)** — Swordigo 专用工具、游戏前端和关卡编辑器。
2. **MIT 许可证** — 通用宿主基础设施、Android 模拟层和 JNI 桥接。
3. **保留所有权利 (All Rights Reserved - ARR)** — 专有 Swordigo 运行时环境 (SRE)。

除 SRE 特别注明的共同所有权外，本仓库内的所有原创作品均**独家授权归 OpenSwordigo Org** (`QuantumCreeper`, `Msinup`, `ManoK`) 所有。

---

## 1. GNU 通用公共许可证第 3 版 (GPLv3)
### Swordigo 专用游戏与编辑器组件

以下子系统及目录在 **GNU General Public License, Version 3 (GPLv3)** 条款下获得许可：

- **Ruby 与 Ruby GG IDE 套件** (`src/ruby/`)：
  - Qt6 Studio 编辑器、`Graphy` 可视化节点编辑器、视口着色器、光照与后处理管线、Caver 视觉引擎及集成工具。
- **Swordfare 启动器与游戏内悬浮窗 (Overlay)** (`src/launcher/`, `src/platform/`)：
  - 游戏内 HUD 悬浮层、模组管理器 (Mod Manager)、存档编辑器 UI、配置文件管理器、视频背景播放器以及运行时前端。
- **Swordigo 工具链与转换器** (`src/tools/`, `tools/`)：
  - SCL/Scene 转图表转换器、boulder 地形生成器、rubymesh 格式、glTF 桥接器及资产编译器。

**版权所有 © 2026 OpenSwordigo Org。保留所有权利。**
基于 GPLv3 许可。详见 [`src/ruby/LICENSE.md`](src/ruby/LICENSE.md) 和 [`src/platform/LICENSE.md`](src/platform/LICENSE.md)。

---

## 2. MIT 许可证
### 通用宿主基础设施、Android 与 JNI 桥接

不包含游戏专用逻辑的通用宿主环境、可移植运行时垫片以及底层模拟桥接采用宽松的 **MIT 许可证**：

- **通用 Android 模拟与 JNI 桥接** (`src/jni/`, `src/android/`):
  - POSIX Android 兼容层、资产管理器、日志记录器和 JNI 封送处理桥接。
- **二进制 ELF 加载器与架构支持** (`src/loader/`, `src/srehost/`):
  - 动态 ELF 加载器、符号重定位表以及客体-宿体 ABI 边界胶水层。
- **通用引擎平台辅助模块** (`src/platform/` 的部分组件)：
  - 通用窗口包装器、定时器抽象及 PVRTC/ASTC 图像解码器。

**版权所有 © 2026 OpenSwordigo Org。**
*(部分版权 © 2023 Rinnegatamante — Swordigo Vita Port；Imagination Technologies Ltd. — PVR SDK)*

特此免费授予任何获得本软件副本和相关文档文件（“软件”）的人无限制地处理本软件的许可，包括但不限于使用、复制、修改、合并、发布、分发、再许可和/或出售软件副本的权利，并允许向其提供软件的人在满足以下条件的情况下这样做：

上述版权声明和本许可声明应包含在软件的所有副本或重要部分中。

本软件按“原样”提供，不提供任何形式的明示或暗示保证，包括但不限于对适销性、特定用途适用性和非侵权性的保证。在任何情况下，作者或版权所有者均不对任何索赔、损害或其他责任负责，无论是在合同诉讼、侵权诉讼还是其他诉讼中，由软件或软件的使用或其他交易引起或与之相关。

---

## 3. 保留所有权利 (All Rights Reserved - ARR)
### Swordigo 运行时环境 (SRE)

`src/sre/` 中包含的完整 **Swordigo Runtime Environment (SRE)** 是受**保留所有权利 (ARR)** 保护的专有软件：

- **`src/sre/sre13/`**：Swordigo 1.4.13 客体运行时、Caver 架构挂钩、rbmath Lua 数学库和控制台/音频子系统。
- **`src/sre/sre12/`**：Swordigo 1.4.12 客体运行时、核心挂钩和 Mini API。
- **`src/sre/extras/`**：闭源 SRE 扩展、FFI 接口、内存补丁和存档文件系统。
- **`src/sre/base/`**：SRE 基础引擎与自定义运行时 ABI 胶水。

### 联合权利所有权：
SRE 的所有权利、所有权和知识产权均由以下两方共同且独家拥有：
- **OpenSwordigo Org**：`QuantumCreeper`, `Msinup`, `ManoK`
- **Lawncher Team**：`Raijin`, `Kiziyon`

**未经明确事先书面授权，严禁任何形式的未授权再分发、修改、二次许可、反编译或公开镜像。**
完整条款请参见 [`src/sre/LICENSE.md`](src/sre/LICENSE.md)。
*（`src/sre/base/` 中的第三方集成依赖项——如上游 Lua 5.1、LuaSocket、LuaFileSystem、toml-c 和 RakNet——保留其原始开源许可）。*

---

## 许可总览矩阵 (Summary Matrix)

| 目录 / 组件 | 许可证 | 专有性 / 版权所有者 |
| :--- | :--- | :--- |
| `src/sre/` (`sre12`, `sre13`, `extras`, `base`) | **保留所有权利 (ARR)** | **OpenSwordigo Org** & **Lawncher Team** |
| `src/ruby/` (Ruby, Ruby GG Studio IDE) | **GNU GPLv3** | 独家归 **OpenSwordigo Org** 所有 |
| `src/launcher/`, `src/platform/` (Swordfare UI) | **GNU GPLv3** | 独家归 **OpenSwordigo Org** 所有 |
| `src/tools/`, `tools/` (转换器与编译器) | **GNU GPLv3** | 独家归 **OpenSwordigo Org** 所有 |
| `src/jni/`, `src/android/` (JNI 桥接与垫片) | **MIT** | 独家归 **OpenSwordigo Org** 所有 |
| `src/loader/`, `src/srehost/` (宿主 ELF 加载器) | **MIT** | 独家归 **OpenSwordigo Org** 所有 |
| 通用平台解码器 (ASTC, PVRTC) | **MIT** | 上游作者 & **OpenSwordigo Org** |

---

## 4. 社区协议与治理规范

所有向本项目的代码贡献及在线基础设施的使用均须遵守以下补充协议：
- **贡献者许可协议 (CLA)**：请参阅 [`.github/CLA.md`](.github/CLA.md) 了解贡献条款与 50/50 版权保留细则。
- **项目治理模型 (Governance)**：请参阅 [`.github/GOVERNANCE.md`](.github/GOVERNANCE.md) 了解项目管理架构及决策权。
- **使用条款 (Terms of Use)**：请参阅 [`.github/TERMS_OF_USE.md`](.github/TERMS_OF_USE.md) 了解在线模组商店与网络服务规范。
- **行为准则 (Code of Conduct)**：请参阅 [`.github/CODE_OF_CONDUCT.md`](.github/CODE_OF_CONDUCT.md) 了解社区文明准则。
