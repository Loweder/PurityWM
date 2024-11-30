#include "core.h"
#include <math.h>
#include <xcb/xcb_icccm.h>
#include <stdio.h>

//TODO VERSION selection via extension
//TODO root Configure handler
//TODO select events on children
//TODO iconic client support
//TODO urgency
//TODO redesign to old C-style variable initializion (all in beginning of function)
//TODO
//WM_PROTOCOLS:
//	WM_TAKE_FOCUS support
//	WM_DELETE_WINDOW support, or forceful kill
//WM_STATE: Normal, Iconic, Withdrawn at CreateNotify
//
//WM_NAME, WM_ICON_NAME: self explanatory
//WM_CLASS: for identification
//
//WM_SIZE_HINTS: TODO needs more work: sizes, gravity?
//WM_HINTS: input hint, initial state, icon?
//WM_ICON_SIZE: may ignore
//
//WM_TRANSIENT_FOR: 'TAG' extension only
//
//State transitions
//  WM_CHANGE_STATE
//
//Configure window: enforce size following WM_SIZE_HINTS
//Change attributes: redirect
//Input focus

#define ARRAY_LENGTH(arr) (sizeof(arr) / sizeof(arr[0]))
#define LOG(format) fprintf(stderr, format "\n")
#define LOGF(format, ...) fprintf(stderr, format "\n", __VA_ARGS__)
#ifdef DEBUG
#define DEBUG_LOG(format) fprintf(stderr, "Debug: " format "\n")
#define DEBUG_LOGF(format, ...) fprintf(stderr, "Debug: " format "\n", __VA_ARGS__)
#else
#define DEBUG_LOG(format)
#define DEBUG_LOGF(format, ...)
#endif

void *(*emalloc)(size_t size);
void (*k_exit)(void);

int error_handler(xcb_generic_error_t *err);
int button_handler(xcb_button_press_event_t *ev);
int key_handler(xcb_key_press_event_t *ev);
int motion_handler(xcb_motion_notify_event_t *ev);
int map_request_handler(xcb_map_request_event_t *ev);
int configure_request_handler(xcb_configure_request_event_t *ev);
int unmap_notify_handler(xcb_unmap_notify_event_t *ev);
int expose_handler(xcb_expose_event_t *ev);
int create_notify_handler(xcb_create_notify_event_t *ev);
int destroy_notify_handler(xcb_destroy_notify_event_t *ev);

void manage_client(xcb_window_t id);
void unmanage_client(pwm_core_client_t *c);
void push_client(pwm_core_client_t *c);
void pop_client(pwm_core_client_t *c);
void resize(pwm_core_client_t *c, uint32_t width, uint32_t height); //TODO?
void focus(void);
void unfocus(void);
void restack(void);

void rotate_clients(void*);
void kill_client(void*);

//TODO better find?
pwm_core_client_t *find_client(xcb_window_t id);
//TODO remove
void kill_wm(void *data) {
	k_exit();
}

pwm_kernel_t *k;
pwm_handlerinfo_t h = {
	{
		[0] = {(pwm_event_handler_t) error_handler, -1},

		[XCB_BUTTON_PRESS] = {(pwm_event_handler_t) button_handler, -1},
		[XCB_KEY_PRESS] = {(pwm_event_handler_t) key_handler, -1},
		[XCB_MOTION_NOTIFY] = {(pwm_event_handler_t) motion_handler, -1},

		[XCB_CONFIGURE_REQUEST] = {(pwm_event_handler_t) configure_request_handler, -1},
		[XCB_MAP_REQUEST] = {(pwm_event_handler_t) map_request_handler, -1},
		[XCB_UNMAP_NOTIFY] = {(pwm_event_handler_t) unmap_notify_handler, -1},

		[XCB_CREATE_NOTIFY] = {(pwm_event_handler_t) create_notify_handler, -1},
		[XCB_DESTROY_NOTIFY] = {(pwm_event_handler_t) destroy_notify_handler, -1},

		[XCB_EXPOSE] = {(pwm_event_handler_t) expose_handler, -1},
	}, 
	0, 
	0
};
pwm_core_bind_t keys[] = {
	{ XCB_MOD_MASK_4 | XCB_MOD_MASK_SHIFT, XCB_MOD_MASK_CONTROL,   {.key = 24}, kill_wm, 0 },
	{ XCB_MOD_MASK_4 | XCB_MOD_MASK_SHIFT, 0,                      {.key = 54}, kill_client, 0 },
	{ XCB_MOD_MASK_4, 0,                                           {.key = 44}, rotate_clients, (void*) 0},
	{ XCB_MOD_MASK_4, 0,                                           {.key = 45}, rotate_clients, (void*) 1},
};
pwm_core_bind_t buttons[0] = {

};
pwm_core_config_t config = {
	keys,
	buttons,
	ARRAY_LENGTH(keys),
	ARRAY_LENGTH(buttons),
	{"WM_PROTOCOLS", "WM_DELETE_WINDOW", "WM_TAKE_FOCUS"},
	{68, 52, 120}, //left_ptr, fleur, sizing
	20
};
pwm_extinfo_t self_info = {
	"core", 
	&config, 
	0, 
	0, 
	0
};
pwm_core_protocols_t protocols = {0};

//------------------------------ Keybinds ------------------------------

void rotate_clients(void *data) {
	pwm_core_monitor_t *m = protocols.monitor;
	if (!m->clients) return;
	unfocus();
	if (data) {
		m->clients = m->clients->next;
	} else {
		m->clients = m->clients->prev;
	}
	restack();
	focus();
	xcb_flush(k->c);
}

void kill_client(void *data) {
	pwm_core_client_t *c = protocols.monitor->clients;
	if (!c) return;
	if (c->protocols & PWM_CORE_WP_DELETE) {
		xcb_client_message_event_t ev;
		ev.response_type = XCB_CLIENT_MESSAGE;
		ev.window = c->id;
		ev.type = protocols.atoms->atoms[PWM_CORE_ATOM_WM_PROTOCOLS];
		ev.format = 32;
		ev.data.data32[0] = protocols.atoms->atoms[PWM_CORE_ATOM_WM_DELETE_WINDOW];
		xcb_send_event(k->c, 0, c->id, XCB_EVENT_MASK_NO_EVENT, (const char*) &ev);
	} else {
		unmanage_client(c);
		xcb_destroy_window(k->c, c->id);
	}
	xcb_flush(k->c);
}

//------------------------------ Commands ------------------------------

void push_client(pwm_core_client_t *c) {
	pwm_core_monitor_t *m = protocols.monitor;
	if (m->clients) {
		c->prev = m->clients;
		c->next = m->clients->next;
		c->prev->next = c;
		c->next->prev = c;
	} else {
		c->next = c->prev = c;
	}	
	m->clients = c;
}
void pop_client(pwm_core_client_t *c) {
	pwm_core_monitor_t *m = protocols.monitor;
	if (c->next == c) {
		m->clients = 0;
	} else {
		c->next->prev = c->prev;
		c->prev->next = c->next;
		if (m->clients == c) m->clients = c->prev;
	}
}

pwm_core_client_t *find_client(xcb_window_t id) {
	pwm_core_monitor_t *m = protocols.monitor;
	if (!m->clients) return 0;
	pwm_core_client_t *c = m->clients;
	do {
		if (c->id == id) return c;
		c = c->next;
	} while (c != m->clients);
	return 0;
}
void manage_client(xcb_window_t id) {
	pwm_core_monitor_t *m = protocols.monitor;
	pwm_core_client_t *c =	emalloc(sizeof(pwm_core_client_t));
	//TODO account for barwin
	c->id = id;
	c->x = m->x;
	c->y = m->y;

	xcb_get_property_cookie_t hints_cookie = xcb_icccm_get_wm_hints_unchecked(k->c, id);
	xcb_get_property_cookie_t size_cookie = xcb_icccm_get_wm_normal_hints_unchecked(k->c, id);
	xcb_get_property_cookie_t proto_cookie = xcb_icccm_get_wm_protocols_unchecked(k->c, id, protocols.atoms->atoms[PWM_CORE_ATOM_WM_PROTOCOLS]);
	xcb_icccm_wm_hints_t hints;
	xcb_size_hints_t size;
	xcb_icccm_get_wm_protocols_reply_t proto;
	if (xcb_icccm_get_wm_hints_reply(k->c, hints_cookie, &hints, 0)) {
		if (hints.flags & XCB_ICCCM_WM_HINT_INPUT)
			c->protocols |= hints.input ? PWM_CORE_WP_INPUT : 0;
		if (hints.flags & XCB_ICCCM_WM_HINT_X_URGENCY)
			c->protocols |= PWM_CORE_WP_URGENT;
	}
	if (xcb_icccm_get_wm_normal_hints_reply(k->c, size_cookie, &size, 0)) {
		if (size.flags & XCB_ICCCM_SIZE_HINT_BASE_SIZE) {
			c->basew = size.base_width;
			c->baseh = size.base_height;
		} else if (size.flags & XCB_ICCCM_SIZE_HINT_P_MIN_SIZE) {
			c->basew = size.min_width;
			c->baseh = size.min_height;
		} else {
			c->basew = c->baseh = 0;
		}
		if (size.flags & XCB_ICCCM_SIZE_HINT_P_MIN_SIZE) {
			c->minw = size.min_width;
			c->minh = size.min_height;
		} else if (size.flags & XCB_ICCCM_SIZE_HINT_BASE_SIZE) {
			c->minw = size.base_width;
			c->minh = size.base_height;
		} else {
			c->minw = c->minh = 0;
		}
		if (size.flags & XCB_ICCCM_SIZE_HINT_P_MAX_SIZE) {
			c->maxw = size.max_width;
			c->maxh = size.max_height;
		} else {
			c->maxw = c->maxh = 0;
		}
		if (size.flags & XCB_ICCCM_SIZE_HINT_P_ASPECT) {
			c->maxa = (float) size.max_aspect_num / size.max_aspect_den;
			c->mina = (float) size.min_aspect_num / size.min_aspect_den;
		} else {
			c->maxa = c->mina = 0.0;
		}
		if (size.flags & XCB_ICCCM_SIZE_HINT_P_RESIZE_INC) {
			c->incw = size.width_inc;
			c->inch = size.height_inc;
		} else {
			c->inch = c->incw = 0;
		}
	}
	if (xcb_icccm_get_wm_protocols_reply(k->c, proto_cookie, &proto, 0)) {
		xcb_atom_t *atoms = protocols.atoms->atoms;
		for (uint32_t i = 0; i < proto.atoms_len; i++) {
			xcb_atom_t atom = proto.atoms[i];
			if (atom == atoms[PWM_CORE_ATOM_WM_DELETE_WINDOW]) {
				c->protocols |= PWM_CORE_WP_DELETE;
			} else if (atom == atoms[PWM_CORE_ATOM_WM_TAKE_FOCUS]) {
				c->protocols |= PWM_CORE_WP_FOCUS;
			}
		}
		xcb_icccm_get_wm_protocols_reply_wipe(&proto);
	}

	resize(c, m->width, m->height);

	xcb_configure_window_value_list_t cw;
	cw.x = c->x;
	cw.y = c->y;
	cw.width = c->width;
	cw.height = c->height;
	xcb_configure_window_aux(k->c, id, 
			XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | 
			XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, &cw);
	xcb_map_window(k->c, id);	
	
	unfocus();
	push_client(c);
	restack();
	focus();
}
void unmanage_client(pwm_core_client_t *c) {
	unfocus();
	pop_client(c);
	restack();
	focus();
	free(c);
}

void resize(pwm_core_client_t *c, uint32_t width, uint32_t height) {
	if (c->minw && c->minw > width) width = c->minw;
	if (c->minh && c->minh > height) height = c->minh;
	if (c->maxw && c->maxw < width) width = c->maxw;
	if (c->maxh && c->maxh < height) height = c->maxh;
	if (c->incw) {
		uint32_t diff = (float) (width - c->basew) / c->incw;
		width = c->basew + (diff * c->incw);
		if (c->minw && c->minw > width) diff++;
		width = c->basew + (diff * c->incw);
		if (c->maxw && c->maxw < width) diff--;
		width = c->basew + (diff * c->incw);
	}
	if (c->inch) {
		uint32_t diff = (float) (height - c->baseh) / c->inch;
		height = c->baseh + (diff * c->inch);
		if (c->minh && c->minh > height) diff++;
		height = c->baseh + (diff * c->inch);
		if (c->maxh && c->maxh < height) diff--;
		height = c->baseh + (diff * c->inch);
	}

	c->width = width;
	c->height = height;
}
void focus(void) {
	pwm_core_client_t *c = protocols.monitor->clients;
	if (!c) return;
	//TODO unset urgent, set border
	if (c->protocols & PWM_CORE_WP_INPUT) {
		LOG("Set primitive focus");
		xcb_set_input_focus(k->c, XCB_INPUT_FOCUS_POINTER_ROOT, c->id, XCB_CURRENT_TIME);
	}
	if (c->protocols & PWM_CORE_WP_FOCUS) {
		LOG("Set complex focus");
		xcb_client_message_event_t ev;
		ev.response_type = XCB_CLIENT_MESSAGE;
		ev.window = c->id;
		ev.type = protocols.atoms->atoms[PWM_CORE_ATOM_WM_PROTOCOLS];
		ev.format = 32;
		ev.data.data32[0] = protocols.atoms->atoms[PWM_CORE_ATOM_WM_TAKE_FOCUS];
		ev.data.data32[1] = XCB_CURRENT_TIME;
		xcb_send_event(k->c, 0, c->id, XCB_EVENT_MASK_NO_EVENT, (const char*) &ev);
	}
}
void unfocus(void) {
	xcb_set_input_focus(k->c, XCB_INPUT_FOCUS_POINTER_ROOT, k->root, XCB_CURRENT_TIME);
}
void restack(void) {
	pwm_core_client_t *c = protocols.monitor->clients, *f = c;
	if (!c) return;
	xcb_configure_window_value_list_t cw;
	cw.stack_mode = XCB_STACK_MODE_ABOVE;
	xcb_configure_window_aux(k->c, c->id, XCB_CONFIG_WINDOW_STACK_MODE, &cw);
	cw.sibling = c->id;
	c = c->next;
	while (c != f) {
		cw.stack_mode = XCB_STACK_MODE_BELOW;
		xcb_configure_window_aux(k->c, c->id, XCB_CONFIG_WINDOW_STACK_MODE | XCB_CONFIG_WINDOW_SIBLING, &cw);
		cw.sibling = c->id;
		c = c->next;
	}
}

//------------------------------  Events  ------------------------------

int error_handler(xcb_generic_error_t *err) {
	LOGF("XCB Error: %d", err->error_code);
	return 0;
}
int button_handler(xcb_button_press_event_t *ev) {
	return 0;
}
int key_handler(xcb_key_press_event_t *ev) {
	//TODO improve. Check if window is k->root
	if (PWM_PROTOCOL_CHECK_OWNER(protocols.binds, self_info.owner)) {
		pwm_core_binds_t *binds = protocols.binds;
		for (uint32_t i = 0; i < binds->key_size; i++) {
			pwm_core_bind_t *bind = &binds->keys[i];
			if (bind->code.key != ev->detail) continue;
			if ((bind->modmaskall & ev->state) != bind->modmaskall) continue;
			if ((bind->modmasknone & ev->state) != 0) continue;
			bind->handler(bind->data);
		}
	}
	return 0;
}
int motion_handler(xcb_motion_notify_event_t *ev) {
	return 0;
}
int map_request_handler(xcb_map_request_event_t *ev) {
	xcb_get_window_attributes_cookie_t wa_cookie = xcb_get_window_attributes_unchecked(k->c, ev->window);
	xcb_get_window_attributes_reply_t *wa = xcb_get_window_attributes_reply(k->c, wa_cookie, 0);
	if (!wa || !wa->override_redirect) {
		manage_client(ev->window);
		xcb_flush(k->c);
	}
	free(wa);
	return 0;
}
int configure_request_handler(xcb_configure_request_event_t *ev) {
	//TODO
	LOGF("Got configure: window '%x', X/Y: '%d/%d', W/H: '%d/%d'", 
			ev->window, ev->x, ev->y, ev->width, ev->height);
	xcb_configure_window_value_list_t cw;
	cw.border_width = ev->border_width;
	cw.stack_mode = ev->stack_mode;
	cw.sibling = ev->sibling;
	cw.x = ev->x;
	cw.y = ev->y;
	cw.width = ev->width;
	cw.height = ev->height;
	xcb_configure_window_aux(k->c, ev->window, ev->value_mask, &cw);
	xcb_flush(k->c);
	return 0;
}
int unmap_notify_handler(xcb_unmap_notify_event_t *ev) {
	pwm_core_client_t *client = find_client(ev->window);
	if (!client) return 0;
	unmanage_client(client);
	xcb_flush(k->c);
	return 0;
}
int expose_handler(xcb_expose_event_t *ev) {
	if (ev->window == protocols.monitor->bar_id) {
		//TODO pixmaps, reusing GC, make color palettes
		if (PWM_PROTOCOL_CHECK_OWNER(protocols.monitor, self_info.owner)) {
			pwm_core_monitor_t *monitor = protocols.monitor;
			uint16_t start = 0, end = monitor->width;
			xcb_gcontext_t gc = xcb_generate_id(k->c);
			xcb_create_gc_value_list_t gcv;
			gcv.foreground = 0x00FF88A0;
			xcb_create_gc_aux(k->c, gc, ev->window, XCB_GC_FOREGROUND, &gcv);

			//TODO remove
			xcb_rectangle_t rect = {ev->x, ev->y, ev->width, ev->height};
			xcb_poly_fill_rectangle(k->c, ev->window, gc, 1, &rect);

			pwm_core_barrender_t *render = monitor->barrenders;
			while (render) {
				render->renderer(monitor, gc, &start, &end);
				render = render->next;
			}
			xcb_free_gc(k->c, gc);
			xcb_flush(k->c);
		}
	}
	return 0;
}
int create_notify_handler(xcb_create_notify_event_t *ev) {
	return 0;
}
int destroy_notify_handler(xcb_destroy_notify_event_t *ev) {
	return 0;
}

//------------------------------  Control ------------------------------

EXTENSION_HANDSHAKE {
	k = kernel;
	emalloc = k->helpers->malloc;
	k_exit = k->helpers->exit;
	return &self_info;
}
EXTENSION_PREINIT {
	k->protocols[self_info.owner] = (pwm_generic_proto_t**) &protocols;
	protocols.monitor = PWM_PROTOCOL_CREATE(k, self_info.owner, pwm_core_monitor_t);
	protocols.binds = PWM_PROTOCOL_CREATE(k, self_info.owner, pwm_core_binds_t);
	protocols.cursors = PWM_PROTOCOL_CREATE_CHAINEND(k, self_info.owner, pwm_core_cursors_t);
	protocols.atoms = PWM_PROTOCOL_CREATE_CHAINEND(k, self_info.owner, pwm_core_atoms_t);
	{
		pwm_core_monitor_t *m = protocols.monitor;
		xcb_get_geometry_cookie_t root_geom_cookie = xcb_get_geometry_unchecked(k->c, k->root);
		xcb_query_tree_cookie_t root_tree_cookie = xcb_query_tree_unchecked(k->c, k->root);
		xcb_get_geometry_reply_t *root_geom = xcb_get_geometry_reply(k->c, root_geom_cookie, 0);
		xcb_query_tree_reply_t *root_tree = xcb_query_tree_reply(k->c, root_tree_cookie, 0);
		if (!root_geom) {
			LOG("Failed to get geometry of the root window");
			free(root_tree);
			k_exit();
		}
		if (!root_tree) {
			LOG("Failed to query tree of the root window");
			k_exit();
		}
		m->x = root_geom->x;
		m->y = root_geom->y;
		m->width = root_geom->width;
		m->height = root_geom->height;
		m->clients = 0;
		xcb_window_t *children = xcb_query_tree_children(root_tree);
		for (int i = 0; i < root_tree->children_len; i++) {
			manage_client(children[i]);
		}
		free(root_geom);
		free(root_tree);
	}
	{
		xcb_atom_t *atoms = protocols.atoms->atoms;
		const char **atoms_raw = config.atoms;
		for (uint32_t i = 0; i < PWM_CORE_ATOM_LAST; i++) {
			xcb_intern_atom_cookie_t intern_cookie = xcb_intern_atom_unchecked(k->c, 0, strlen(atoms_raw[i]), atoms_raw[i]);
			xcb_intern_atom_reply_t *intern = xcb_intern_atom_reply(k->c, intern_cookie, 0);
			if (!intern) {
				LOGF("Failed to intern atom '%s'", atoms_raw[i]);
				k_exit();
			}
			atoms[i] = intern->atom;
			free(intern);
		}
	}
	xcb_flush(k->c);
}
EXTENSION_INIT {
	return &h;
}
EXTENSION_POSTINIT {
	//FIXME remove duplicate grabs
	if (PWM_PROTOCOL_CHECK_OWNER(protocols.binds, self_info.owner)) {
		pwm_core_binds_t *binds = protocols.binds;
		binds->key_size = config.key_size;
		binds->button_size = config.button_size;
		for (int i = 0; i < k->extension_count; i++) {
			pwm_core_binds_t *subbinds = (pwm_core_binds_t*) binds->protocols[i];
			if (!subbinds) continue;
			binds->key_size += subbinds->key_size;
			binds->button_size += subbinds->button_size;
		}
		pwm_core_bind_t *curr_key = binds->keys = emalloc(sizeof(pwm_core_bind_t) * binds->key_size);
		pwm_core_bind_t *curr_button = binds->buttons = emalloc(sizeof(pwm_core_bind_t) * binds->button_size);

		for (int j = 0; j < config.key_size; j++) {
			*curr_key = config.keys[j];
			xcb_grab_key(k->c, 1, k->root, XCB_MOD_MASK_ANY, curr_key->code.key,
					XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC);
			curr_key++;
		} 
		for (int j = 0; j < config.button_size; j++) {
			*curr_button = config.buttons[j];
			xcb_grab_button(k->c, 1, k->root, XCB_EVENT_MASK_BUTTON_PRESS|XCB_EVENT_MASK_BUTTON_RELEASE, 
					XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC, 
					XCB_NONE, XCB_NONE, curr_button->code.button, XCB_MOD_MASK_ANY & ~curr_button->modmasknone);
			curr_button++;
		} 
		for (int i = 0; i < k->extension_count; i++) {
			pwm_core_binds_t *subbinds = (pwm_core_binds_t*) binds->protocols[i];
			if (!subbinds) continue;
			for (int j = 0; j < subbinds->key_size; j++) {
				*curr_key = subbinds->keys[j];
				xcb_grab_key(k->c, 1, k->root, XCB_MOD_MASK_ANY & ~curr_key->modmasknone, curr_key->code.key,
						XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC);
				curr_key++;
			} 
			for (int j = 0; j < subbinds->button_size; j++) {
				*curr_button = subbinds->buttons[j];
				xcb_grab_button(k->c, 1, k->root, XCB_EVENT_MASK_BUTTON_PRESS|XCB_EVENT_MASK_BUTTON_RELEASE, 
						XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC, 
						XCB_NONE, XCB_NONE, curr_button->code.button, XCB_MOD_MASK_ANY & ~curr_button->modmasknone);
				curr_button++;
			} 
		}
	}
	if (PWM_PROTOCOL_CHECK_OWNER(protocols.cursors, self_info.owner)) {
		xcb_cursor_t *cursors = protocols.cursors->cursors;
		uint16_t *cursors_raw = config.cursors;
		xcb_font_t cursor_fnt = xcb_generate_id(k->c);
		xcb_open_font(k->c, cursor_fnt, 6, "cursor");
		for (int i = 0; i < PWM_CORE_CURSOR_LAST; i++) {
			cursors[i] = xcb_generate_id(k->c);
			xcb_create_glyph_cursor(k->c, cursors[i], cursor_fnt, cursor_fnt, 
					cursors_raw[i], cursors_raw[i]+1, 0, 0, 0, 65535, 65535, 65535);
		}
		xcb_close_font(k->c, cursor_fnt);
	}
	if (PWM_PROTOCOL_CHECK_OWNER(protocols.monitor, self_info.owner)) {
		pwm_core_monitor_t *monitor = protocols.monitor;
		monitor->bar_id = xcb_generate_id(k->c);
		xcb_create_window_value_list_t mw;
		mw.background_pixel = k->screen->white_pixel;
		mw.event_mask = XCB_EVENT_MASK_EXPOSURE;
		xcb_create_window(k->c, XCB_COPY_FROM_PARENT, monitor->bar_id, k->root, 
				monitor->x, monitor->y, monitor->width, config.bar_size, 0,
				XCB_WINDOW_CLASS_INPUT_OUTPUT, k->screen->root_visual, 
				XCB_CW_EVENT_MASK | XCB_CW_BACK_PIXEL, &mw);
		xcb_map_window(k->c, monitor->bar_id);
	}

	xcb_change_window_attributes_value_list_t wa;
	wa.cursor = protocols.cursors->cursors[PWM_CORE_CURSOR_NORMAL];
	wa.event_mask = XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT |
		XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY |
		XCB_EVENT_MASK_POINTER_MOTION |
		XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_KEY_PRESS;
	xcb_change_window_attributes_aux(k->c, k->root, 
			XCB_CW_CURSOR | XCB_CW_EVENT_MASK, &wa);

	xcb_flush(k->c);
}
EXTENSION_CLOSE {
	//TODO
	/*	xcb_free_cursor(k.c, cursors[CURSOR_NORMAL]);*/
	/*	xcb_flush(k.c);*/
}
