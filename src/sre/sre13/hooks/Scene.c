#include "../caver/Scene.h"

static Scene *g_scene = NULL;

Scene *scene_from_L(lua_State *L) {
	lua_getglobal(L, "scene");
	if (!lua_islightuserdata(L, -1)) return NULL;
	const void *scene = lua_topointer(L, -1);
	lua_pop(L, 1);
	return (Scene *)scene;
}

Scene *scene_get(void) {
	return g_scene;
}

HOOK_SYMBOL(
	Scene_Update_Hook,
	"_ZN5Caver5Scene6UpdateEf",
	void, (Scene *scene, float dt)
) {
	g_scene = scene;
	if (orig_Scene_Update_Hook) {
		orig_Scene_Update_Hook(scene, dt);
	}
}

G_DL_SYMBOL(
	Scene_SetPaused,
	"_ZN5Caver5Scene9SetPausedEb",
	void, (Scene *scene, bool paused)
);

G_DL_SYMBOL(
	Scene_ObjectWithIdentifier,
	"_ZN5Caver5Scene20ObjectWithIdentifierERKNSt6__ndk112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEE",
	void*, (Scene *scene, String *id)
);

G_DL_SYMBOL(
	Scene_AddObject,
	"_ZN5Caver5Scene9AddObjectERKN5boost13intrusive_ptrINS_11SceneObjectEEE",
	void, (Scene *scene, void *intrusive_object)
);

G_DL_SYMBOL(
	Scene_RemoveObject,
	"_ZN5Caver5Scene12RemoveObjectERKN5boost13intrusive_ptrINS_11SceneObjectEEEb",
	void, (Scene *scene, void *intrusive_object, bool unknown)
);

G_DL_SYMBOL(
	Scene_ActivateObject,
	"_ZN5Caver5Scene14ActivateObjectERKN5boost13intrusive_ptrINS_11SceneObjectEEE",
	void, (Scene *scene, void *intrusive_object)
);

G_DL_SYMBOL(
	Scene_GetAllObjects,
	"_ZN5Caver5Scene13GetAllObjectsEPNSt6__ndk16vectorIN5boost13intrusive_ptrINS_11SceneObjectEEENS1_9allocatorIS6_EEEE",
	void, (Scene *scene, void *vector)
);

G_DL_SYMBOL(
	Scene_Update,
	"_ZN5Caver5Scene6UpdateEf",
	void, (Scene *scene, float dt)
);
