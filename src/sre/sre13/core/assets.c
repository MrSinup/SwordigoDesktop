#define LOG_TAG "SRE13Assets"
#include "hook.h"
#include "stdstring.h"
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char g_active_mod[128] = {0};

void sre13_set_active_mod(const char *mod_id) {
    if (!mod_id) {
        g_active_mod[0] = '\0';
        return;
    }
    strncpy(g_active_mod, mod_id, sizeof(g_active_mod) - 1);
    g_active_mod[sizeof(g_active_mod) - 1] = '\0';
}

const char* sre13_get_active_mod(void) {
    return g_active_mod;
}

HOOK_SYMBOL(
	NewByteBufferFromAA,
	"_ZN5Caver29NewByteBufferFromAndroidAssetERKNSt6__ndk112basic_stringIcNS0_11char_traitsIcEENS0_9allocatorIcEEEEPj",
	void*, (String *file, unsigned int *out_size)
) {
	const char *filename = String_get(file);
	if (g_active_mod[0] && filename && *filename) {
		char path[512];
		/* Check mods/<mod>/assets/<file> */
		snprintf(path, sizeof(path), "mods/%s/assets/%s", g_active_mod, filename);
		FILE *f = fopen(path, "rb");
		if (!f) {
			/* Check mods/<mod>/<file> */
			snprintf(path, sizeof(path), "mods/%s/%s", g_active_mod, filename);
			f = fopen(path, "rb");
		}
		if (f) {
			fseek(f, 0, SEEK_END);
			long sz = ftell(f);
			fseek(f, 0, SEEK_SET);
			if (sz >= 0) {
				void *buf = malloc((size_t)sz);
				if (buf) {
					size_t read_bytes = fread(buf, 1, (size_t)sz, f);
					fclose(f);
					if (out_size) *out_size = (unsigned int)read_bytes;
					LOGI("Loaded modded asset: %s (%zu bytes)", path, read_bytes);
					return buf;
				}
			}
			fclose(f);
		}
	}

	if (orig_NewByteBufferFromAA) {
		return orig_NewByteBufferFromAA(file, out_size);
	}
	return NULL;
}
