#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <dirent.h>
#include <dlfcn.h>
#include <fcntl.h>
#include "pwm_api.h"

#define LOG(format) fprintf(stderr, format "\n")
#define LOGF(format, ...) fprintf(stderr, format "\n", __VA_ARGS__)
#ifdef DEBUG
#define DEBUG_LOG(format) fprintf(stderr, "Debug: " format "\n")
#define DEBUG_LOGF(format, ...) fprintf(stderr, "Debug: " format "\n", __VA_ARGS__)
#else
#define DEBUG_LOG(format)
#define DEBUG_LOGF(format, ...)
#endif
#define FIND_SYMBOL(handle, type) ext_ ## type ## _t type; *(void**) (&type) = pwm_find_symbol(handle, "pwme_" #type);

static void *pwm_malloc(size_t size) __attribute_malloc__;
static pwm_extinfo_t *pwm_find_extension(const char *name);
static void *pwm_find_symbol(void *handle, const char *name);
static void pwm_exit(void) __attribute__((noreturn));

static void becomeWM(void);
static void event_dfs(const pwm_handlerinfo_t **source_sets, uint32_t source_count, uint32_t source_id, 
		pwm_event_handler_t *target, uint32_t *target_id, uint8_t *visited, uint8_t type, uint8_t extended);
static void events_mix(const pwm_handlerinfo_t **event_sets, uint32_t set_count);
static void extension_load(char *file, pwm_extinfo_t ***last);
static void extensions_load(void);
static void extensions_init(void);

static pwm_funcptr_t funcs = {
	pwm_malloc,
	pwm_exit,
	pwm_find_extension
};

struct _pwm_normal_handler {
	pwm_event_handler_t *handlers;
	uint32_t count;
};
struct _pwm_extended_handler {
	pwm_event_handler_t *handlers;
	struct _pwm_extended_handler *next;
	uint32_t count;
	uint8_t code;
};

static enum pwm_stage stage = PWM_STAGE_START;
static pwm_kernel_t kernel = { 0, 0, &funcs, 0, 0, 0, 0 }; //Main data struct
static struct _pwm_normal_handler handlers[XCB_LAST_EVENT] = {0}; //Core event handlers
static struct _pwm_extended_handler *ext_handlers = 0;            //XOrg Extension event handlers

//------------------------------  Helpers ------------------------------

void *pwm_malloc(size_t size) {
	void *data = malloc(size);
	if (!data) pwm_exit();
	return data;
}

pwm_extinfo_t *pwm_find_extension(const char *name) {
	pwm_extinfo_t *info = kernel.info;
	while (info) {
		if (strcmp(info->name, name) == 0) return info;
		info = info->next;
	}
	return 0;
}

void *pwm_find_symbol(void *handle, const char *name) {
	void *symbol = dlsym((void*)handle, name);
	if (!symbol) {
		LOGF("dlsym() failed: no valid '%s' found: %s", name, dlerror());
		pwm_exit();
	}
	return symbol;
}

void pwm_exit(void) {
	//Call exit on all extensions
	pwm_extinfo_t *info = kernel.info;
	while (info) {
		FIND_SYMBOL(info->handle, exit);
		exit(stage);
		info = info->next;
	}
	xcb_disconnect(kernel.c);
	exit(stage == PWM_STAGE_RUNTIME ? 0 : 1); //Runtime is the 'correct' time to exit
}


//This function loads extensions from the specified file
void extension_load(char *file, pwm_extinfo_t ***last) {
	void *handle = dlopen(file, RTLD_LAZY | RTLD_LOCAL);
	if (!handle) {
		if (file) return; // Not a dynamic extension, skip
		LOGF("dlopen() failed: %s", dlerror());
		pwm_exit();
	} else if (file) {
		LOGF("Noticed extension file: %s", file);
	}
	
	FIND_SYMBOL(handle, handshake);
	pwm_extinfo_t *info = handshake(&kernel);

	while (info) {
		LOGF("Found extension named '%s'", info->name);
		info->handle = handle;
		info->owner = kernel.extension_count;
		**last = info;
		*last = &info->next;
		info = info->next;
		kernel.extension_count++;
	}
}
// This functions finds and loads static/dynamic extensions from the EXE and /lib/puritywm
void extensions_load(void) {
	stage = PWM_STAGE_LOAD;
	pwm_extinfo_t **last = (pwm_extinfo_t**) &kernel.info;
	LOG("Loading Static/Dynamic PurityWM Extensions");

	//Loading static core extensions
	{
		LOG("Searching for Static in the EXE");
		extension_load(0, &last);
	}
	//Loading dynamic extensions from /lib/puritywm/
	{
		DIR *dirext = opendir("/lib/puritywm/");
		if (dirext) {
			struct dirent *fileext;
			LOG("Searching for Dynamic in '/lib/puritywm/'");
			while ((fileext = readdir(dirext))) {
				if (fileext->d_type == DT_REG || fileext->d_type == DT_LNK) {
					DEBUG_LOGF("found file %s", fileext->d_name);
					char path[271];
					snprintf(path, sizeof(path), "/lib/puritywm/%s", fileext->d_name);
					extension_load(path, &last);
				}
			}
			closedir(dirext);
		} else {
			LOG("Unable to search for Dynamic: '/lib/puritywm' not found'");
		}
	}

	kernel.protocols = pwm_malloc(sizeof(pwm_generic_proto_t*) * kernel.extension_count);
	LOGF("Found %d extensions", kernel.extension_count);
}

void event_dfs(const pwm_handlerinfo_t **source_sets, uint32_t source_count, uint32_t source_id, 
		pwm_event_handler_t *target, uint32_t *target_id, uint8_t *visited, uint8_t type, uint8_t extended) {
	if (source_id >= source_count) return;
	if (visited[source_id] == 1) return;
	visited[source_id] = 1;

	if (extended) {
		struct _pwm_event_set_extended handler = {0};
		for (uint32_t i = 0; i < source_sets[source_id]->extended_count; i++) {
			handler = source_sets[source_id]->extended_handlers[i];
			if (handler.code == type) break;
		}
		if (!handler.handler) return;
		event_dfs(source_sets, source_count, handler.above, target, target_id, visited, type, 1);
		target[(*target_id)--] = handler.handler;
	} else {
		struct _pwm_event_set_normal handler = source_sets[source_id]->handlers[type];
		if (!handler.handler) return;
		event_dfs(source_sets, source_count, handler.above, target, target_id, visited, type, 0);
		target[(*target_id)--] = handler.handler;
	}
}

void events_mix(const pwm_handlerinfo_t **event_sets, uint32_t set_count) {
	uint8_t *visited = pwm_malloc(set_count);
	//Process core event handlers
	{
		for (int j = 0; j < XCB_LAST_EVENT; j++) {
			for (int i = 0; i < set_count; i++) {
				if (event_sets[i]->handlers[j].handler)
					handlers[j].count++;
			}
			uint32_t count = handlers[j].count;
			DEBUG_LOGF("handler type %d, count %d", j, count);
			if (count == 0) continue;
			handlers[j].handlers = pwm_malloc(sizeof(pwm_event_handler_t) * count);
			count--;
			memset(visited, 0, set_count);

			for (int i = 0; i < set_count; i++) {
				event_dfs(event_sets, set_count, i, handlers[j].handlers, &count, visited, j, 0);
			}
		}
	}
	//Process extension event handlers
	{
		for (int i = 0; i < set_count; i++) {
			for (int j = 0; j < event_sets[i]->extended_count; j++) {
				struct _pwm_event_set_extended handler = event_sets[i]->extended_handlers[j];
				struct _pwm_extended_handler **ext_handler = &ext_handlers;
				while (*ext_handler) {
					if ((*ext_handler)->code == handler.code) break;
					ext_handler = &((*ext_handler)->next);
				}
				if (!(*ext_handler)) {
					*ext_handler = pwm_malloc(sizeof(struct _pwm_extended_handler));
					(*ext_handler)->handlers = 0;
					(*ext_handler)->code = handler.code;
					(*ext_handler)->count = 1;
					(*ext_handler)->next = 0;
				} else {
					(*ext_handler)->count++;
				}
			}
		}
		struct _pwm_extended_handler *ext_handler = ext_handlers;
		while (ext_handler) {
			uint32_t count = ext_handler->count;
			DEBUG_LOGF("handler type %d, count %d", ext_handler->code, count);
			ext_handler->handlers = pwm_malloc(sizeof(pwm_event_handler_t) * count);
			count--;
			memset(visited, 0, set_count);

			for (int i = 0; i < set_count; i++) {
				event_dfs(event_sets, set_count, i, ext_handler->handlers, &count, visited, ext_handler->code, 1);
			}
			ext_handler = ext_handler->next;
		}
	}
	free(visited);
}

// This function initializes and orders to configure the loaded extensions
void extensions_init(void) {
	pwm_extinfo_t *map = kernel.info;
	LOG("Initializing PurityWM Extensions");
	uint32_t ext_idx = 0; //For later

	//Preinit
	{
		stage = PWM_STAGE_PREINIT;
		while (map) {
			//Trying to initialize or raise fatal error
			LOGF("Trying to preinit extension: %s", map->name);
			FIND_SYMBOL(map->handle, preinit);
			preinit();
			map = map->next;	
			ext_idx++;
		}
	}
	//Init
	{
		stage = PWM_STAGE_INIT;
		map = kernel.info;
		const pwm_handlerinfo_t **event_sets = pwm_malloc(sizeof(pwm_handlerinfo_t*) * ext_idx);
		ext_idx = 0;
		while (map) {
			LOGF("Trying to init extension: %s", map->name);
			FIND_SYMBOL(map->handle, init);
			event_sets[ext_idx] = init();
			map = map->next;	
			ext_idx++;
		}
		events_mix(event_sets, ext_idx);
		free(event_sets);
	}
	//Postinit
	{
		stage = PWM_STAGE_POSTINIT;
		map = kernel.info;
		while (map) {
			LOGF("Trying to postinit extension: %s", map->name);
			FIND_SYMBOL(map->handle, postinit);
			postinit();
			map = map->next;	
		}
	}
}

void becomeWM(void) {
	uint32_t mask[] = {XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT};
	xcb_generic_error_t *err = xcb_request_check(kernel.c, xcb_change_window_attributes_checked(kernel.c, kernel.root, XCB_CW_EVENT_MASK, mask));
	
	if (err) {
		LOG("A WM is already running");
		pwm_exit();
	}
}

int main(void) {
	kernel.c = xcb_connect(0, 0);
	if (xcb_connection_has_error(kernel.c)) {
		LOGF("Failed to start the WM with XCB code %d", xcb_connection_has_error(kernel.c));
		xcb_disconnect(kernel.c);
		return 1;
	}
	kernel.screen = xcb_setup_roots_iterator(xcb_get_setup(kernel.c)).data;
	kernel.root = kernel.screen->root;
	becomeWM();
	extensions_load();
	extensions_init();
	stage = PWM_STAGE_RUNTIME;
	xcb_generic_event_t *ev;
	while ((ev = xcb_wait_for_event(kernel.c))) {
		if (ev->response_type & 0x80) {
			struct _pwm_extended_handler *handler = ext_handlers;
			while (handler) {
				if (handler->code == ev->response_type) {
					for (int i = 0; i < handler->count; i++) {
						if (handler->handlers[i](ev)) break;
					}
					break;
				}
				handler = handler->next;
			}
		} else {
			if (ev->response_type < XCB_LAST_EVENT) {
				struct _pwm_normal_handler handler = handlers[ev->response_type];
				if (handler.handlers) {
					for (int i = 0; i < handler.count; i++) {
						if (handler.handlers[i](ev)) break;
					}
				}
			} else {
				LOGF("Got unsupported event code: %d", ev->response_type);
			}
		}
		free(ev);
	}

	stage = PWM_STAGE_STOP;
	pwm_exit();
}
