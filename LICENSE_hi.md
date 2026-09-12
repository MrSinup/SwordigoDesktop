# SwordigoDesktop बहु-लाइसेंस सूचना (Multi-License Notice)

**AevoraLabs (prev OpenSwordigo) परियोजना लाइसेंसिंग रूपरेखा (Licensing Framework)**

> **मूल अंग्रेज़ी दस्तावेज़**: [English (LICENSE.md)](LICENSE.md) | [Français (French)](LICENSE_fr.md) | [简体中文 (Chinese)](LICENSE_cn.md)

यह रिपॉजिटरी एक समग्र (composite) परियोजना है जिसके विभिन्न घटक तीन अलग-अलग कानूनी शर्तों के तहत लाइसेंस प्राप्त हैं:
1. **GNU General Public License v3.0 (GPLv3)** — स्वॉर्डिगो-विशिष्ट टूल्स, गेम फ्रंटएंड, और एडिटर्स।
2. **MIT License** — सामान्य होस्ट इंफ्रास्ट्रक्चर, एंड्रॉइड इम्यूलेशन लेयर्स, और JNI ब्रिजेस।
3. **सर्वाधिकार सुरक्षित (All Rights Reserved - ARR)** — मालिकाना स्वॉर्डिगो रनटाइम एनवायरनमेंट (SRE)।

SRE के संयुक्त स्वामित्व को छोड़कर, इस रिपॉजिटरी में किए गए सभी मूल कार्य **विशेष रूप से AevoraLabs (prev OpenSwordigo)** (`QuantumCreeper`, `Msinup`, `ManoK`) द्वारा लाइसेंस प्राप्त हैं।

---

## 1. GNU General Public License v3.0 (GPLv3)
### स्वॉर्डिगो-विशिष्ट गेम और संपादक घटक

निम्नलिखित सबसिस्टम और डायरेक्टरीज़ **GNU General Public License, Version 3 (GPLv3)** की शर्तों के तहत लाइसेंस प्राप्त हैं:

- **Ruby और Ruby GG IDE सुइट** (`src/ruby/`):
  - Qt6 स्टूडियो एडिटर, `Graphy` विज़ुअल नोड एडिटर, व्यूपोर्ट शेडर्स, लाइटिंग और पोस्ट-प्रोसेसिंग पाइपलाइन, कैवर विज़ुअल इंजन, और एकीकृत टूल्स।
- **Swordfare लॉन्चर और इन-गेम ओवरले** (`src/launcher/`, `src/platform/`):
  - इन-गेम HUD ओवरले, मॉड मैनेजर, सेव एडिटर UI, प्रोफाइल मैनेजर, वीडियो बैकग्राउंड प्लेयर, और रनटाइम फ्रंटएंड।
- **स्वॉर्डिगो टूलिंग और कन्वर्टर्स** (`src/tools/`, `tools/`):
  - SCL/Scene से ग्राफ कन्वर्टर्स, बोल्डर टेरेन जनरेटर, रूबिमेश फॉर्मेट्स, glTF ब्रिज, और एसेट कम्पाइलर्स।

**कॉपीराइट © 2026 AevoraLabs. सर्वाधिकार सुरक्षित।**
GPLv3 के तहत लाइसेंस प्राप्त। देखें [`src/ruby/LICENSE.md`](src/ruby/LICENSE.md) और [`src/platform/LICENSE.md`](src/platform/LICENSE.md)।

---

## 2. MIT License
### सामान्य होस्ट इंफ्रास्ट्रक्चर, एंड्रॉइड और JNI ब्रिजेस

सामान्य होस्ट वातावरण, पोर्टेबल रनटाइम शिम्स, और निम्न-स्तरीय इम्यूलेशन प्लंबिंग जिनमें गेम-विशिष्ट तर्क शामिल नहीं हैं, वे उदारवादी **MIT लाइसेंस** के तहत लाइसेंस प्राप्त हैं:

- **सामान्य एंड्रॉइड इम्यूलेशन और JNI ब्रिजेस** (`src/jni/`, `src/android/`):
  - POSIX एंड्रॉइड शिम लेयर्स, एसेट मैनेजर्स, लॉगर्स, और JNI मार्शलिंग ब्रिजेस।
- **बाइनरी ELF लोडर और आर्किटेक्चर सपोर्ट** (`src/loader/`, `src/srehost/`):
  - डायनामिक ELF लोडर, सिंबल रीलोकेशन टेबल्स, और गेस्ट-होस्ट ABI बाउंड्री ग्लू।
- **सामान्य इंजन प्लेटफ़ॉर्म हेल्पर्स** (`src/platform/` के हिस्से):
  - सामान्य विंडोइंग रैपर्स, टाइमर एब्स्ट्रैक्शंस, और PVRTC/ASTC इमेज डिकोडर्स।

**कॉपीराइट © 2026 AevoraLabs.**
*(कुछ हिस्से कॉपीराइट © 2023 Rinnegatamante — Swordigo Vita Port; Imagination Technologies Ltd. — PVR SDK)*

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

---

## 3. सर्वाधिकार सुरक्षित (All Rights Reserved - ARR)
### स्वॉर्डिगो रनटाइम एनवायरनमेंट (SRE)

`src/sre/` के अंतर्गत मौजूद संपूर्ण **Swordigo Runtime Environment (SRE)** पूरी तरह से मालिकाना सॉफ्टवेयर है और **सर्वाधिकार सुरक्षित (ARR)** के तहत सुरक्षित है:

- **`src/sre/sre13/`**: स्वॉर्डिगो 1.4.13 गेस्ट रनटाइम, Caver आर्किटेक्चर हुक्स, rbmath Lua मैथ लाइब्रेरी, और कंसोल/ऑडियो सबसिस्टम।
- **`src/sre/sre12/`**: स्वॉर्डिगो 1.4.12 गेस्ट रनटाइम, कोर हुक्स, और मिनी एपीआई।
- **`src/sre/extras/`**: क्लोज्ड-सोर्स SRE एक्सटेंशन्स, FFI इंटरफेस, मेमोरी पैचेस, और सेव फाइल सिस्टम।
- **`src/sre/base/`**: SRE बेस इंजन प्लंबिंग और कस्टम रनटाइम ABI ग्लू।

### संयुक्त अधिकार स्वामित्व:
SRE पर सभी अधिकार, शीर्षक और बौद्धिक संपदा संयुक्त रूप से और विशेष रूप से इनके पास हैं:
- **AevoraLabs (prev OpenSwordigo)**: `QuantumCreeper`, `Msinup`, `ManoK`
- **Lawncher Team**: `Raijin`, `Kiziyon`

**बिना पूर्व लिखित अनुमति के किसी भी प्रकार का अनधिकृत पुनर्वितरण, संशोधन, उप-लाइसेंसिंग, डीकंपाइलेशन, या सार्वजनिक मिररिंग प्रतिबंधित है।**
पूर्ण शर्तों के लिए [`src/sre/LICENSE.md`](src/sre/LICENSE.md) देखें।
*(तृतीय-पक्ष वेंडर्ड निर्भरताएँ जैसे upstream Lua 5.1, LuaSocket, LuaFileSystem, toml-c, और RakNet अपने मूल ओपन-सोर्स लाइसेंस बनाए रखती हैं)।*

---

## सारांश मैट्रिक्स (Summary Matrix)

| डायरेक्टरी / घटक | लाइसेंस | विशिष्टता / कॉपीराइट धारक |
| :--- | :--- | :--- |
| `src/sre/` (`sre12`, `sre13`, `extras`, `base`) | **सर्वाधिकार सुरक्षित (ARR)** | **AevoraLabs (prev OpenSwordigo)** & **Lawncher Team** |
| `src/ruby/` (Ruby, Ruby GG Studio IDE) | **GNU GPLv3** | विशेष रूप से **AevoraLabs** |
| `src/launcher/`, `src/platform/` (Swordfare UI) | **GNU GPLv3** | विशेष रूप से **AevoraLabs** |
| `src/tools/`, `tools/` (कन्वर्टर्स और कम्पाइलर्स) | **GNU GPLv3** | विशेष रूप से **AevoraLabs** |
| `src/jni/`, `src/android/` (JNI ब्रिजेस और शिम्स) | **MIT** | विशेष रूप से **AevoraLabs** |
| `src/loader/`, `src/srehost/` (होस्ट ELF लोडर) | **MIT** | विशेष रूप से **AevoraLabs** |
| सामान्य प्लेटफ़ॉर्म डिकोडर्स (ASTC, PVRTC) | **MIT** | मूल लेखक और **AevoraLabs** |

---

## 4. सामुदायिक समझौते और शासन नीतियां (Community Agreements & Policies)

परियोजना में सभी योगदान और ऑनलाइन बुनियादी ढांचे का उपयोग निम्नलिखित संबंधित समझौतों के अधीन है:
- **योगदानकर्ता लाइसेंस समझौता (CLA)**: योगदान की शर्तों और 50/50 कॉपीराइट प्रतिधारण नियमों के लिए [`.github/CLA.md`](.github/CLA.md) देखें।
- **परियोजना शासन मॉडल (Governance)**: परियोजना प्रबंधन और निर्णय लेने के अधिकार के लिए [`.github/GOVERNANCE.md`](.github/GOVERNANCE.md) देखें।
- **उपयोग की शर्तें (Terms of Use)**: ऑनलाइन मॉड स्टोर और नेटवर्क बुनियादी ढांचे की उपयोग शर्तों के लिए [`.github/TERMS_OF_USE.md`](.github/TERMS_OF_USE.md) देखें।
- **आचार संहिता (Code of Conduct)**: सामुदायिक मानकों के लिए [`.github/CODE_OF_CONDUCT.md`](.github/CODE_OF_CONDUCT.md) देखें।
