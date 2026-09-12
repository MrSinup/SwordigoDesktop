#include "../caver/GameViewController.h"
#include "../caver/GameState.h"
#include "lua.h"

static GameViewController *g_gvc = NULL;
static GameState *g_gameState = NULL;

GameViewController *gvc_from_L(lua_State *L) {
	lua_getglobal(L, "gameController");
	if (!lua_islightuserdata(L, -1)) return NULL;
	const void *gvc = lua_topointer(L, -1);
	lua_pop(L, 1);
	return (GameViewController *)gvc;
}

extern void* sre13_get_active_gvc(void);

GameViewController *gvc_get(void) {
	if (g_gvc) return g_gvc;
	return (GameViewController *)sre13_get_active_gvc();
}

GameState *gameState_get(void) {
	if (g_gameState) return g_gameState;
	GameViewController *gvc = gvc_get();
	return gvc ? gvc->GameState : NULL;
}

extern void sre13_scene_shifter_tick(void);

HOOK_SYMBOL(
	GVC_Update_Hook,
	"_ZN5Caver18GameViewController6UpdateEf",
	void, (GameViewController *gvc, float dt)
) {
	g_gvc = gvc;
	if (gvc && gvc->GameState) {
		g_gameState = (GameState *)gvc->GameState;
	}
	if (orig_GVC_Update_Hook) {
		orig_GVC_Update_Hook(gvc, dt);
	}
	sre13_scene_shifter_tick();
}

G_DL_SYMBOL(
	GameViewController_Update,
	"_ZN5Caver18GameViewController6UpdateEf",
	void, (GameViewController *gvc, float dt)
);

G_DL_SYMBOL(
	GameViewController_LoadGameState,
	"_ZN5Caver18GameViewController13LoadGameStateEv",
	void, (GameViewController *gvc)
);

G_DL_SYMBOL(
	GameViewController_SaveGameState,
	"_ZN5Caver18GameViewController13SaveGameStateEb",
	void, (GameViewController *gvc, bool unknown)
);

G_DL_SYMBOL(
	GameViewController_GotoLevel,
	"_ZN5Caver18GameViewController9GotoLevelERKNSt6__ndk112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEES9_",
	void, (GameViewController *gvc, String *level, String *spawn)
);

G_DL_SYMBOL(
	GameViewController_LoadView,
	"_ZN5Caver18GameViewController8LoadViewEv",
	void, (GameViewController *gvc)
);

G_DL_SYMBOL(
	GameViewController_ResetView,
	"_ZN5Caver18GameViewController9ResetViewEv",
	void, (GameViewController *gvc)
);

G_DL_SYMBOL(
	GameViewController_ResumeView,
	"_ZN5Caver18GameViewController10ResumeViewEv",
	void, (GameViewController *gvc)
);

G_DL_SYMBOL(
	GameViewController_AddItemToCharacter,
	"_ZN5Caver18GameViewController18AddItemToCharacterERKN5boost10shared_ptrINS_4ItemEEE",
	void, (GameViewController *gvc, void *shared_item)
);

G_DL_SYMBOL(
	GameViewController_RemoveItemFromCharacter,
	"_ZN5Caver18GameViewController23RemoveItemFromCharacterERKN5boost10shared_ptrINS_4ItemEEE",
	void, (GameViewController *gvc, void *shared_item)
);

G_DL_SYMBOL(
	GameState_AllNodesVisited,
	"_ZN5Caver9GameState15AllNodesVisitedEv",
	bool, (GameState *gs)
);

G_DL_SYMBOL(
	GameState_Clear,
	"_ZN5Caver9GameState5ClearEv",
	void, (GameState *gs)
);
