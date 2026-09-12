# Avis Multi-Licence SwordigoDesktop

**Cadre de Licence du Projet OpenSwordigo**

> **Document original en anglais**: [English (LICENSE.md)](LICENSE.md) | [हिन्दी (Hindi)](LICENSE_hi.md) | [简体中文 (Chinese)](LICENSE_cn.md)

Ce dépôt est un projet composite composé d'éléments sous trois régimes de licences distincts :
1. **GNU General Public License v3.0 (GPLv3)** — Outils spécifiques à Swordigo, interface de jeu et éditeurs.
2. **Licence MIT** — Infrastructure hôte générique, couches d'émulation Android et ponts JNI.
3. **Tous Droits Réservés (All Rights Reserved - ARR)** — Environnement d'exécution propriétaire Swordigo Runtime Environment (SRE).

Sauf attribution conjointe spécifique pour SRE, toutes les œuvres originales de ce dépôt sont **exclusivement concédées sous licence à OpenSwordigo Org** (`QuantumCreeper`, `Msinup`, `ManoK`).

---

## 1. GNU General Public License v3.0 (GPLv3)
### Composants de Jeu et Éditeurs Spécifiques à Swordigo

Les sous-systèmes et répertoires suivants sont concédés sous les termes de la **GNU General Public License, Version 3 (GPLv3)** :

- **Suite IDE Ruby & Ruby GG** (`src/ruby/`) :
  - Éditeur Studio Qt6, éditeur de nœuds visuels `Graphy`, shaders de viewport, pipeline d'éclairage et de post-traitement, moteur visuel caver et outils intégrés.
- **Lanceur Swordfare & Overlay en jeu** (`src/launcher/`, `src/platform/`) :
  - Overlay HUD en jeu, gestionnaire de mods, interface d'édition de sauvegarde, gestionnaire de profils, lecteur vidéo en arrière-plan et interface d'exécution.
- **Outillage et Convertisseurs Swordigo** (`src/tools/`, `tools/`) :
  - Convertisseurs SCL/Scene vers graphes, générateur de terrain boulder, formats rubymesh, passerelle glTF et compilateurs d'assets.

**Copyright © 2026 OpenSwordigo Org. Tous droits réservés.**
Sous licence GPLv3. Voir [`src/ruby/LICENSE.md`](src/ruby/LICENSE.md) et [`src/platform/LICENSE.md`](src/platform/LICENSE.md).

---

## 2. Licence MIT
### Infrastructure Hôte Générique, Android & Ponts JNI

L'environnement hôte générique, les shims d'exécution portables et la plomberie d'émulation de bas niveau qui ne contiennent aucune logique spécifique au jeu sont sous licence permissive **MIT** :

- **Émulation Android Générique & Ponts JNI** (`src/jni/`, `src/android/`) :
  - Couches shims Android POSIX, gestionnaires d'assets, journaux et ponts de marshaling JNI.
- **Chargeur Binaire ELF & Support d'Architecture** (`src/loader/`, `src/srehost/`) :
  - Chargeur dynamique ELF, tables de relocalisation de symboles et liant d'interface ABI invité-hôte.
- **Aides de Plateforme Génériques** (parties de `src/platform/`) :
  - Enveloppeurs de fenêtrage génériques, abstractions de minuterie et décodeurs d'images PVRTC/ASTC.

**Copyright © 2026 OpenSwordigo Org.**
*(Parties Copyright © 2023 Rinnegatamante — Swordigo Vita Port ; Imagination Technologies Ltd. — PVR SDK)*

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

---

## 3. Tous Droits Réservés (All Rights Reserved - ARR)
### Environnement d'Exécution Swordigo (SRE)

L'intégralité du **Swordigo Runtime Environment (SRE)** contenu dans `src/sre/` est un logiciel strictement propriétaire soumis au régime **Tous Droits Réservés (ARR)** :

- **`src/sre/sre13/`** : Runtime invité Swordigo 1.4.13, hooks de l'architecture Caver, bibliothèque mathématique Lua rbmath et sous-systèmes console/audio.
- **`src/sre/sre12/`** : Runtime invité Swordigo 1.4.12, hooks centraux et mini API.
- **`src/sre/extras/`** : Extensions SRE fermées, interfaces FFI, correctifs mémoire et systèmes de fichiers de sauvegarde.
- **`src/sre/base/`** : Plomberie de base du moteur SRE et colle ABI personnalisée.

### Copropriété des Droits :
Tous les droits, titres et propriétés intellectuelles sur SRE sont détenus conjointement et exclusivement par :
- **OpenSwordigo Org** : `QuantumCreeper`, `Msinup`, `ManoK`
- **Lawncher Team** : `Raijin`, `Kiziyon`

**Aucune redistribution, modification, sous-licence, décompilation ou mise en miroir publique non autorisée n'est autorisée sans autorisation écrite préalable expresse.**
Voir [`src/sre/LICENSE.md`](src/sre/LICENSE.md) pour les conditions complètes.
*(Les dépendances tierces intégrées dans `src/sre/base/`—telles que Lua 5.1, LuaSocket, LuaFileSystem, toml-c et RakNet—conservent leurs licences open-source d'origine).*

---

## Matrice Récapitulative (Summary Matrix)

| Répertoire / Composant | Licence | Exclusivité / Titulaires des Droits |
| :--- | :--- | :--- |
| `src/sre/` (`sre12`, `sre13`, `extras`, `base`) | **Tous Droits Réservés (ARR)** | **OpenSwordigo Org** & **Lawncher Team** |
| `src/ruby/` (Ruby, Studio IDE Ruby GG) | **GNU GPLv3** | Exclusivement **OpenSwordigo Org** |
| `src/launcher/`, `src/platform/` (UI Swordfare) | **GNU GPLv3** | Exclusivement **OpenSwordigo Org** |
| `src/tools/`, `tools/` (Convertisseurs & Compilateurs) | **GNU GPLv3** | Exclusivement **OpenSwordigo Org** |
| `src/jni/`, `src/android/` (Ponts JNI & Shims) | **MIT** | Exclusivement **OpenSwordigo Org** |
| `src/loader/`, `src/srehost/` (Chargeur ELF Hôte) | **MIT** | Exclusivement **OpenSwordigo Org** |
| Décodeurs de Plateforme Génériques (ASTC, PVRTC) | **MIT** | Auteurs d'origine & **OpenSwordigo Org** |

---

## 4. Accords Communautaires et Politiques de Gouvernance

Toutes les contributions et l'utilisation de l'infrastructure en ligne du projet sont soumises aux accords suivants :
- **Accord de Licence Contributeur (CLA)** : Voir [`.github/CLA.md`](.github/CLA.md) pour les conditions de contribution et les règles de rétention de copyright 50/50.
- **Modèle de Gouvernance du Projet** : Voir [`.github/GOVERNANCE.md`](.github/GOVERNANCE.md) pour l'intendance du projet et l'autorité décisionnelle.
- **Conditions d'Utilisation (Terms of Use)** : Voir [`.github/TERMS_OF_USE.md`](.github/TERMS_OF_USE.md) pour le Mod Store en ligne et les conditions d'infrastructure réseau.
- **Code de Conduite** : Voir [`.github/CODE_OF_CONDUCT.md`](.github/CODE_OF_CONDUCT.md) pour les règles communautaires.
