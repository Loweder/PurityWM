#ifndef PURITY_WM_CORE
#define PURITY_WM_CORE

#include "pwm_api.h"

//TODO mark as volatile

enum {
	PWM_CORE_CURSOR_NORMAL,
	PWM_CORE_CURSOR_MOVE,
	PWM_CORE_CURSOR_RESIZE,
	PWM_CORE_CURSOR_LAST
};
enum {
	PWM_CORE_WP_INPUT = 1,
	PWM_CORE_WP_FOCUS = 2,
	PWM_CORE_WP_DELETE = 4,
	PWM_CORE_WP_URGENT = 8,
};
enum {
	PWM_CORE_ATOM_WM_PROTOCOLS,
	PWM_CORE_ATOM_WM_DELETE_WINDOW,
	PWM_CORE_ATOM_WM_TAKE_FOCUS,
	PWM_CORE_ATOM_LAST
};

typedef struct _pwm_core_client pwm_core_client_t;
typedef struct _pwm_core_monitor pwm_core_monitor_t;

struct _pwm_core_client {
	struct _pwm_core_client *next;
	struct _pwm_core_client *prev;
	const char *name;

	xcb_window_t id;
	uint32_t protocols;

	int32_t x, y; 
	uint32_t width, height;
	uint32_t basew, baseh;
	uint32_t minw, minh;
	uint32_t maxw, maxh;
	uint32_t incw, inch;
	float mina, maxa;
};

typedef struct _pwm_core_barrender {
	struct _pwm_core_barrender *next;
	const char *name;
	void (*renderer)(pwm_core_monitor_t *monitor, xcb_gcontext_t gc, uint16_t *begin, uint16_t *end);
} pwm_core_barrender_t;

struct _pwm_core_monitor {
	EXTENSION_GEN_PROTO;
	pwm_core_client_t *clients;
	pwm_core_barrender_t *barrenders;
	int16_t x, y; 
	uint16_t width, height;
	xcb_window_t bar_id;
};


typedef struct _pwm_core_bind {
	uint16_t modmaskall;    //All modifier in this mask must be pressed.
	uint16_t modmasknone;   //None of the modifiers in this mask must be pressed.
	union {
		xcb_keycode_t key;    //Keycode which must be pressed.
		xcb_button_t button;  //Button which must be pressed.
	} code;
	void (*handler)(void*); //Event handler.
	void *data;             //User-provided data.
} pwm_core_bind_t;

/* To bind their owns keys, extensions must create a subprotocol here with "pwm_core_binds_t" type.
 * The keys will be compiled into an array in stage 3.
 * */
typedef struct _pwm_core_binds {
	EXTENSION_GEN_PROTO_SINGLE;
	pwm_core_bind_t *keys;
	pwm_core_bind_t *buttons;
	uint32_t key_size;
	uint32_t button_size;
} pwm_core_binds_t;

typedef struct _pwm_core_cursors {
	EXTENSION_GEN_PROTO_CHAINEND;
	xcb_cursor_t cursors[PWM_CORE_CURSOR_LAST];
} pwm_core_cursors_t;

typedef struct _pwm_core_atoms {
	EXTENSION_GEN_PROTO_CHAINEND;
	xcb_atom_t atoms[PWM_CORE_ATOM_LAST];
} pwm_core_atoms_t;


typedef struct _pwm_core_protocols {
	pwm_core_monitor_t *monitor;
	pwm_core_binds_t *binds;
	pwm_core_cursors_t *cursors;
	pwm_core_atoms_t *atoms;
} pwm_core_protocols_t;

typedef struct _pwm_core_config {
	pwm_core_bind_t *keys;
	pwm_core_bind_t *buttons;
	uint32_t key_size;
	uint32_t button_size;
	const char *atoms[PWM_CORE_ATOM_LAST];
	uint16_t cursors[PWM_CORE_CURSOR_LAST];
	uint16_t bar_size;
} pwm_core_config_t;

#endif
