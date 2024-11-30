#ifndef PURITY_WM
#define PURITY_WM

#include <stdlib.h>
#include <string.h>
#include <xcb/xcb.h>

/* 5 stage protocol for Dynamic PurityWM Extensions
 *
 * Stage 1: handshake - "pwm_extinfo_t *pwme_handshake(pwm_kernel_t *k)"
 *   About: This function must return the list of extensions that the file contains.
 *   Parameters: The global WM data.
 *   Returns: The linked list of extensions.
 *   Memory: 
 *     The extension retains the ownership of the "pwm_extinfo_t" structs,
 *     and the manager retains ownership of "pwm_kernel_t".
 *   Extra: 
 *     The manager will set "pwm_extid_t owner" of each returned struct 
 *     to the next available extension ID and link them to the kernel's "info" field.
 *
 * Stage 2: preinit - "void pwme_preinit(void)"
 *   About: This function should initialize protocols that the extension uses
 *   Extra: 
 *     Can also check the kernel for installed extensions,
 *     and call kernel's "exit()" if there dependency issues.
 * Stage 3: init - "pwm_handlerinfo_t *pwme_init(void)"
 *   About: This function should hook into protocols of other extensions, and return event handlers
 *   Returns: The list of event handlers
 *   Memory: The extensions retains the ownership of "pwm_handlerinfo_t"
 * Stage 4: postinit - "void pwme_postinit(void)"
 *   About: This function should perform post-init tasks like starting threads
 *   Extra: 
 *     After that point, the extension is fully responsible for its own actions, and is allowed
 *     to do any tasks it needs
 * Stage 5: exit - "void pwme_exit(void)"
 *   About: This function should uninitialize all data the extension allocates.
 *   Extra:
 *     This function must NOT call kernel's "malloc()" or "exit()".
 *     The function may also be called in 2-4 stage, so some data may not be initialized yet.
 *
 * NOTE: For extensions which returned multiple "pwm_extinfo_t" in Stage 1, each of stages 2-4 
 * will be called once for every struct, in the same order as returned
 *
 * Protocol structure:
 *   Protocols must be stored in the "pwm_generic_proto_t ***protocols" array fields of kernel/objects, where
 *   the first index is the current extension's ID, and the second is the protocol ID.
 *   Extension may use a propriatary struct pointer instead of "pwm_generic_proto **" array to
 *   contain their protocols.
 *   Each protocol is a custom field in a proprietary struct, defined by the extension.
 *   Protocols must contain at least 2 fields like defined in "pwm_generic_proto_t": 
 *     Owner ID "pwm_extid_t owner" - the Extension ID of the extension which is responsible 
 *     for managing data inside this struct.
 *     Subprotocols "pwm_generic_proto_t **protocols" - the list of protocols created by other plugins
 *     to complement this struct
 *   Some protocols may alternatively use "pwm_generic_proto **protocols" or "void *protocols" if
 *   they want to support only one/none subprotocols respectively
 * */

//TODO mark as volatile

// Typedefs

enum pwm_stage {
	PWM_STAGE_START,    //WM start
  PWM_STAGE_LOAD,     //Extension loading, Stage 1 (handshake)
	PWM_STAGE_PREINIT,  //Extension loading, Stage 2 (preinit)
	PWM_STAGE_INIT,     //Extension loading, Stage 3 (init)
	PWM_STAGE_POSTINIT, //Extension loading, Stage 4 (postinit)
	PWM_STAGE_RUNTIME,  //WM event processing
	PWM_STAGE_STOP      //WM shutdown due to server error
};

typedef struct _pwm_kernel pwm_kernel_t;
typedef struct _pwm_extinfo pwm_extinfo_t;
typedef struct _pwm_handlerinfo pwm_handlerinfo_t;
typedef struct _pwm_generic_proto pwm_generic_proto_t;
typedef int(*pwm_event_handler_t)(xcb_generic_event_t *);
typedef uint32_t pwm_extid_t;
#define XCB_LAST_EVENT (XCB_GE_GENERIC + 1)
#define EXTENSION_GEN_PROTO \
	pwm_extid_t owner; \
	pwm_generic_proto_t ***protocols
#define EXTENSION_GEN_PROTO_SINGLE \
	pwm_extid_t owner; \
	pwm_generic_proto_t **protocols
#define EXTENSION_GEN_PROTO_CHAINEND \
	pwm_extid_t owner; \
	void *protocols

// Extension hooks

typedef pwm_extinfo_t *(*ext_handshake_t)(pwm_kernel_t *kernel);
typedef void (*ext_preinit_t)(void);
typedef const pwm_handlerinfo_t *(*ext_init_t)(void);
typedef void (*ext_postinit_t)(void);
typedef void (*ext_exit_t)(enum pwm_stage stage);
//All extensions must contain this
#define EXTENSION_HANDSHAKE pwm_extinfo_t *pwme_handshake(pwm_kernel_t *kernel)
#define EXTENSION_PREINIT void pwme_preinit(void)
#define EXTENSION_INIT const pwm_handlerinfo_t *pwme_init(void)
#define EXTENSION_POSTINIT void pwme_postinit(void)
#define EXTENSION_CLOSE void pwme_exit(enum pwm_stage stage)

// Structs

struct _pwm_extinfo {
	const char *name;
	void *config;
	void *handle;
	struct _pwm_extinfo *next;
	pwm_extid_t owner;
};

struct _pwm_handlerinfo {
	struct _pwm_event_set_normal {
		int(*handler)(xcb_generic_event_t *ev);
		pwm_extid_t above;
	} handlers[XCB_LAST_EVENT];
	struct _pwm_event_set_extended {
		int(*handler)(xcb_generic_event_t *ev);
		pwm_extid_t above;
		uint8_t code;
	} *extended_handlers;
	uint32_t extended_count;
};

struct _pwm_generic_proto {
	EXTENSION_GEN_PROTO;
};

typedef struct _pwm_funcptr {
	void *(*malloc)(size_t size);
	void (*exit)(void);
	pwm_extinfo_t *(*find_extension)(const char *name);
} pwm_funcptr_t;

struct _pwm_kernel {
	pwm_extinfo_t *info;
	pwm_generic_proto_t ***protocols;
	pwm_funcptr_t *helpers;

	xcb_connection_t *c;
	xcb_screen_t *screen;
	xcb_window_t root;
	pwm_extid_t extension_count;
};

// Helper functions

//TODO move
static inline pwm_generic_proto_t *_pwm_protocol_create(pwm_kernel_t *k, pwm_extid_t owner, size_t size) {
	pwm_generic_proto_t *result = (pwm_generic_proto_t*) k->helpers->malloc(size);
	result->owner = owner;
	result->protocols = (pwm_generic_proto_t***) k->helpers->malloc(sizeof(pwm_generic_proto_t**) * k->extension_count);
	memset((pwm_generic_proto_t***)result->protocols, 0, sizeof(pwm_generic_proto_t**) * k->extension_count);

	return result;
}
static inline pwm_generic_proto_t *_pwm_protocol_create_chainend(pwm_kernel_t *k, pwm_extid_t owner, size_t size) {
	pwm_generic_proto_t *result = (pwm_generic_proto_t*) k->helpers->malloc(size);
	result->owner = owner;
	result->protocols = 0;

	return result;
}
#define PWM_PROTOCOL_CREATE(k, owner, type) ((type*) _pwm_protocol_create(k, owner, sizeof(type)))
#define PWM_PROTOCOL_CREATE_CHAINEND(k, owner, type) ((type*) _pwm_protocol_create_chainend(k, owner, sizeof(type)))
#define PWM_PROTOCOL_CHECK_OWNER(proto, _owner) (((pwm_generic_proto_t*)proto)->owner == _owner)

#endif
