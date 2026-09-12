# NeedleWarfare II X SwordigoDesktop
## RakNet Source Integration & SRE Build Plan (with Host ImGui Bridge)

> **Credits**: NeedleWarfare II (NW2) multiplayer mod architecture created by **dukinja**.

---

## Executive Summary
This document outlines the build system and source integration plan for embedding the **RakNet 4.x C++ UDP Engine** directly into SRE (`SwordigoDesktop`). It details which RakNet source files to copy from `/home/quantumcreeper/SwordigoDesktop/libneedlewarfare/cpp/RakNet-master/Source/`, the directory layout in `src/sre/raknet/`, `Makefile` compilation rules, and the **Host ImGui Bridge Stubbing Strategy**.

---

## 1. ImGui Guest Stubbing Architecture (Host Bridge Strategy)

> [!IMPORTANT]
> **Guest ImGui Stubbing**: In `libneedlewarfare`, ImGui was compiled inside the ARM guest library (`libs/imgui/imgui.cpp`). In `SwordigoDesktop`, ImGui is already running natively on the **HOST side** (in C++ via `ImGui_ImplSDL3`, `ImGui_ImplOpenGL3`, `ImGui_ImplVulkan`). 
> 
> Rather than compiling a duplicate guest ImGui library, SRE replaces guest-side ImGui with **Host ImGui Bridge Stubs** (`sre_imgui_*`). When guest Lua scripts call `ImGui.Begin()`, `ImGui.Text()`, or `ImGui.Button()`, SRE forwards the calls directly to the Host's native C++ ImGui renderer in `src/platform/swordfare_gui.cpp`. This eliminates ~1.5 MB of guest binary bloat and delivers native 144+ FPS host rendering.

---

## 2. Required RakNet Source Files Inventory

To compile RakNet cross-platform networking within SRE, the following core source files will be integrated:

```
src/sre/raknet/
├── BitStream.cpp / BitStream.h
├── CCRakNetSlidingWindow.cpp / CCRakNetSlidingWindow.h
├── CCRakNetUDT.cpp / CCRakNetUDT.h
├── DS_ByteQueue.cpp / DS_ByteQueue.h
├── DS_HuffmanEncodingTree.cpp / DS_HuffmanEncodingTree.h
├── GetTime.cpp / GetTime.h
├── MessageIdentifiers.h
├── NetworkIDManager.cpp / NetworkIDManager.h
├── NetworkIDObject.cpp / NetworkIDObject.h
├── PacketPriority.h
├── PluginInterface2.cpp / PluginInterface2.h
├── RakMemoryOverride.cpp / RakMemoryOverride.h
├── RakNetSocket.cpp / RakNetSocket.h
├── RakNetSocket2.cpp / RakNetSocket2.h
├── RakNetSocket2_Berkley.cpp
├── RakNetSocket2_Windows_Linux.cpp
├── RakNetStatistics.cpp / RakNetStatistics.h
├── RakNetTypes.cpp / RakNetTypes.h
├── RakPeer.cpp / RakPeer.h / RakPeerInterface.h
├── RakString.cpp / RakString.h
├── RakThread.cpp / RakThread.h
├── ReliabilityLayer.cpp / ReliabilityLayer.h
├── SignaledEvent.cpp / SignaledEvent.h
├── SimpleMutex.cpp / SimpleMutex.h
├── SocketLayer.cpp / SocketLayer.h
├── StringCompressor.cpp / StringCompressor.h
└── StringTable.cpp / StringTable.h
```

---

## 3. Makefile Integration Strategy

In `SwordigoDesktop/Makefile`:

```makefile
# RakNet UDP Engine Source Files
RAKNET_DIR := src/sre/raknet
RAKNET_SRCS := $(wildcard $(RAKNET_DIR)/*.cpp)
RAKNET_OBJS := $(patsan %.cpp, build/raknet/%.o, $(notdir $(RAKNET_SRCS)))

# Build Rule for RakNet Objects
build/raknet/%.o: $(RAKNET_DIR)/%.cpp
	@mkdir -p build/raknet
	@echo "[CXX/RakNet] $<"
	$(CXX) $(CXXFLAGS) -I$(RAKNET_DIR) -c $< -o $@

# Add to libswd_net.so shared library build target:
$(LIB_DIR)/libswd_net.so: $(RAKNET_OBJS) build/sre_raknet_lua.o build/sre_imgui_bridge.o
	@mkdir -p $(LIB_DIR)
	@echo "[LINK] libswd_net.so"
	$(CXX) -shared -fPIC $^ -lpthread -o $@
```

---

## 4. Network Threading & Execution Model

RakNet manages an asynchronous internal IO thread (`RakThread`) to process non-blocking UDP socket packets.

```
┌─────────────────────────────────────────────────────────────┐
│                    Host Main UI Thread                      │
│ (Frame loop calls RakNet.Peer:Receive() via Lua at 60 FPS)  │
└──────────────────────────────▲──────────────────────────────┘
                               │ (Thread-Safe Lockless Queue)
┌──────────────────────────────┴──────────────────────────────┐
│                  RakNet Asynchronous IO Thread              │
│     (RecvFrom / SendTo UDP socket loops at 100 Hz)          │
└─────────────────────────────────────────────────────────────┘
```
