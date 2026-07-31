/*
 * TouchProbe v6 - Passport capacitive keyboard soft-touch.
 *
 * Community/SDL note: soft-touch may arrive as SCREEN_EVENT_POINTER (not MTOUCH).
 * SDL_playbookevents handlePointerEvent() DROPS events when SOURCE_POSITION.y < 0
 * ("Detected pointer swipe event") - exactly the off-LCD / keyboard region.
 *
 * v6:
 *   - Full POINTER logging (pos/src/buttons/disp/device/product/role)
 *   - Dedicated POINTER session (CONTINUE + z=-1)
 *   - Dual pump: screen_get_event (raw) + BPS screen_request_events
 *   - Still logs MTOUCH with device classification
 */

#include <bps/bps.h>
#include <bps/deviceinfo.h>
#include <bps/navigator.h>
#include <bps/screen.h>
#include <bps/virtualkeyboard.h>
#include <errno.h>
#include <screen/screen.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/keycodes.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define APP_NAME "TouchProbe"
#define LOG_BASENAME "TouchProbe.log"
#define MAX_DEVS 16

#define COL_BG       0xFF181818u
#define COL_GREEN    0xFF00CC44u
#define COL_MAGENTA  0xFFCC00CCu
#define COL_BLUE     0xFF2266FFu
#define COL_ORANGE   0xFFFF8800u
#define COL_YELLOW   0xFFFFFF00u
#define COL_WHITE    0xFFFFFFFFu
#define COL_DIM      0xFF333333u

enum { CLASS_IDLE = 0, CLASS_LCD, CLASS_CKB, CLASS_KEY, CLASS_OTHER };
enum {
	DEV_ROLE_UNKNOWN = 0,
	DEV_ROLE_LCD,
	DEV_ROLE_CKB,
	DEV_ROLE_PHY_KEY,
	DEV_ROLE_VKB
};

typedef struct {
	screen_device_t handle;
	int type;
	int role;
	char id[128];
	char vendor[64];
	char product[64];
} dev_info_t;

static screen_context_t g_ctx;
static screen_window_t g_win;
static screen_buffer_t g_buf;
static screen_session_t g_lcd_session;
static screen_session_t g_ckb_session;   /* MTOUCH CKB session */
static screen_session_t g_ptr_session;   /* POINTER session - hypothesis: CKB as POINTER */
static screen_session_t g_key_session;
static int g_ptr_count; /* POINTER events seen */
static screen_event_t g_ev;
static int g_size[2];
static int g_stride;
static volatile int g_exit;
static int g_event_count, g_lcd_count, g_ckb_count, g_key_count, g_other_count;
static FILE *g_log;
static char g_log_path[512];
static int g_angle;

static dev_info_t g_devs[MAX_DEVS];
static int g_ndevs;
static screen_device_t g_ckb_dev, g_lcd_dev, g_phy_dev;

static int g_last_class = CLASS_IDLE;
static int g_last_pos[2];
static int g_have_touch;
static int g_flash_frames;
static int g_dirty; /* HUD needs redraw */

static const char *event_type_name(int type)
{
	switch (type) {
	case SCREEN_EVENT_NONE: return "NONE";
	case SCREEN_EVENT_CREATE: return "CREATE";
	case SCREEN_EVENT_PROPERTY: return "PROPERTY";
	case SCREEN_EVENT_CLOSE: return "CLOSE";
	case SCREEN_EVENT_INPUT: return "INPUT";
	case SCREEN_EVENT_POINTER: return "POINTER";
	case SCREEN_EVENT_KEYBOARD: return "KEYBOARD";
	case SCREEN_EVENT_USER: return "USER";
	case SCREEN_EVENT_DISPLAY: return "DISPLAY";
	case SCREEN_EVENT_IDLE: return "IDLE";
	case SCREEN_EVENT_JOYSTICK: return "JOYSTICK";
	case SCREEN_EVENT_DEVICE: return "DEVICE";
	case SCREEN_EVENT_GAMEPAD: return "GAMEPAD";
#ifdef SCREEN_EVENT_MTOUCH_PRETOUCH
	case SCREEN_EVENT_MTOUCH_PRETOUCH: return "MTOUCH_PRETOUCH";
#endif
	case SCREEN_EVENT_MTOUCH_TOUCH: return "MTOUCH_TOUCH";
	case SCREEN_EVENT_MTOUCH_MOVE: return "MTOUCH_MOVE";
	case SCREEN_EVENT_MTOUCH_RELEASE: return "MTOUCH_RELEASE";
	default: {
		static char buf[32];
		snprintf(buf, sizeof(buf), "TYPE_%d", type);
		return buf;
	}
	}
}

static const char *role_name(int role)
{
	switch (role) {
	case DEV_ROLE_LCD: return "LCD";
	case DEV_ROLE_CKB: return "CKB";
	case DEV_ROLE_PHY_KEY: return "PHY_KEY";
	case DEV_ROLE_VKB: return "VKB";
	default: return "UNKNOWN";
	}
}

static uint64_t now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000ull + (uint64_t)ts.tv_nsec / 1000000ull;
}

static void log_line(const char *fmt, ...)
{
	va_list ap, ap2;
	va_start(ap, fmt);
	va_copy(ap2, ap);
	vfprintf(stderr, fmt, ap);
	fflush(stderr);
	if (g_log) {
		vfprintf(g_log, fmt, ap2);
		fflush(g_log);
	}
	va_end(ap2);
	va_end(ap);
}

static void open_log(void)
{
	const char *cands[] = {
		"/accounts/1000/shared/documents/" LOG_BASENAME,
		"/accounts/1000/shared/misc/" LOG_BASENAME,
		NULL
	};
	char path[512];
	const char *home = getenv("HOME");
	int i;
	for (i = 0; cands[i]; ++i) {
		g_log = fopen(cands[i], "a");
		if (g_log) {
			snprintf(g_log_path, sizeof(g_log_path), "%s", cands[i]);
			return;
		}
	}
	if (home) {
		snprintf(path, sizeof(path), "%s/data", home);
		mkdir(path, 0755);
		snprintf(path, sizeof(path), "%s/data/" LOG_BASENAME, home);
		g_log = fopen(path, "a");
		if (g_log) {
			snprintf(g_log_path, sizeof(g_log_path), "%s", path);
			return;
		}
	}
	g_log = NULL;
	snprintf(g_log_path, sizeof(g_log_path), "(stderr)");
}

static void note_event(int cls)
{
	g_last_class = cls;
	g_flash_frames = 12;
	g_event_count++;
	if (cls == CLASS_LCD) g_lcd_count++;
	else if (cls == CLASS_CKB) g_ckb_count++;
	else if (cls == CLASS_KEY) g_key_count++;
	else g_other_count++;
	g_dirty = 1;
}

static int str_has_ci(const char *hay, const char *needle)
{
	size_t nlen, i, j;
	if (!hay || !needle || !needle[0]) return 0;
	nlen = strlen(needle);
	for (i = 0; hay[i]; ++i) {
		for (j = 0; j < nlen; ++j) {
			char a = hay[i + j], b = needle[j];
			if (!a) return 0;
			if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
			if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
			if (a != b) break;
		}
		if (j == nlen) return 1;
	}
	return 0;
}

static int classify_role(const char *product, const char *id, const char *vendor)
{
	if (str_has_ci(vendor, "Synaptics") && str_has_ci(product, "touch_keypad"))
		return DEV_ROLE_CKB;
	if (str_has_ci(product, "touch_keypad") && !str_has_ci(product, "BlackBerry"))
		return DEV_ROLE_CKB;
	if (str_has_ci(product, "touch_display") ||
	    (str_has_ci(vendor, "Synaptics") && str_has_ci(product, "display")))
		return DEV_ROLE_LCD;
	if (str_has_ci(product, "VKB") || str_has_ci(id, "BB-VKB"))
		return DEV_ROLE_VKB;
	if (str_has_ci(id, "qwerty") || str_has_ci(product, "BlackBerry touch keypad"))
		return DEV_ROLE_PHY_KEY;
	if (str_has_ci(vendor, "Synaptics") && str_has_ci(product, "keypad"))
		return DEV_ROLE_CKB;
	return DEV_ROLE_UNKNOWN;
}

static int role_for_device(screen_device_t dev)
{
	int i;
	if (!dev) return DEV_ROLE_UNKNOWN;
	if (dev == g_ckb_dev) return DEV_ROLE_CKB;
	if (dev == g_lcd_dev) return DEV_ROLE_LCD;
	if (dev == g_phy_dev) return DEV_ROLE_PHY_KEY;
	for (i = 0; i < g_ndevs; ++i)
		if (g_devs[i].handle == dev) return g_devs[i].role;
	return DEV_ROLE_UNKNOWN;
}

static const char *product_for_device(screen_device_t dev)
{
	int i;
	for (i = 0; i < g_ndevs; ++i)
		if (g_devs[i].handle == dev) return g_devs[i].product;
	return "";
}

static void refresh_devices(void)
{
	int n = 0, i;
	screen_device_t *list = NULL;

	g_ndevs = 0;
	g_ckb_dev = g_lcd_dev = g_phy_dev = NULL;
	memset(g_devs, 0, sizeof(g_devs));

	if (screen_get_context_property_iv(g_ctx, SCREEN_PROPERTY_DEVICE_COUNT, &n) != 0 || n <= 0) {
		log_line("input_device_count=0 or query fail\n");
		return;
	}
	if (n > MAX_DEVS) n = MAX_DEVS;
	list = calloc((size_t)n, sizeof(*list));
	if (!list) return;
	if (screen_get_context_property_pv(g_ctx, SCREEN_PROPERTY_DEVICES, (void **)list) != 0) {
		free(list);
		return;
	}

	log_line("input_device_count=%d\n", n);
	for (i = 0; i < n; ++i) {
		dev_info_t *d = &g_devs[g_ndevs];
		d->handle = list[i];
		d->type = -1;
		screen_get_device_property_iv(list[i], SCREEN_PROPERTY_TYPE, &d->type);
		screen_get_device_property_cv(list[i], SCREEN_PROPERTY_ID_STRING, (int)sizeof(d->id), d->id);
		screen_get_device_property_cv(list[i], SCREEN_PROPERTY_VENDOR, (int)sizeof(d->vendor), d->vendor);
		screen_get_device_property_cv(list[i], SCREEN_PROPERTY_PRODUCT, (int)sizeof(d->product), d->product);
		d->role = classify_role(d->product, d->id, d->vendor);
		log_line("device[%d] ptr=%p type=%d role=%s product=\"%s\" id=\"%s\" vendor=\"%s\"\n",
		         i, (void *)d->handle, d->type, role_name(d->role), d->product, d->id, d->vendor);
		if (d->role == DEV_ROLE_CKB && !g_ckb_dev) g_ckb_dev = d->handle;
		if (d->role == DEV_ROLE_LCD && !g_lcd_dev) g_lcd_dev = d->handle;
		if (d->role == DEV_ROLE_PHY_KEY && !g_phy_dev) g_phy_dev = d->handle;
		g_ndevs++;
	}
	free(list);
	log_line("resolved ckb_dev=%p lcd_dev=%p phy_dev=%p\n",
	         (void *)g_ckb_dev, (void *)g_lcd_dev, (void *)g_phy_dev);
}

static void try_bind_ckb(void)
{
	void *p;
	int rc, mode;

	if (!g_ckb_session) {
		log_line("bind: no ckb session\n");
		return;
	}

	/* Session ID like Cascades device name */
	{
		const char *ids[] = {
			"BLACKBERRY TOUCH KEYPAD",
			"BlackBerry touch keypad",
			"touch_keypad",
			NULL
		};
		int i;
		for (i = 0; ids[i]; ++i) {
			rc = screen_set_session_property_cv(g_ckb_session, SCREEN_PROPERTY_ID_STRING,
			                                    (int)strlen(ids[i]) + 1, ids[i]);
			log_line("CKB session ID_STRING=\"%s\": %s\n", ids[i],
			         rc == 0 ? "ok" : strerror(errno));
			if (rc == 0) break;
		}
	}

	/* MODE: Cascades watches keyboard session MODE changes */
	mode = SCREEN_INPUT_MODE_RAW;
	rc = screen_set_session_property_iv(g_ckb_session, SCREEN_PROPERTY_MODE, &mode);
	log_line("CKB session MODE=RAW(%d): %s\n", mode, rc == 0 ? "ok" : strerror(errno));

	mode = SCREEN_INPUT_MODE_TEXT_ENTRY;
	rc = screen_set_session_property_iv(g_ckb_session, SCREEN_PROPERTY_MODE, &mode);
	log_line("CKB session MODE=TEXT_ENTRY(%d): %s\n", mode, rc == 0 ? "ok" : strerror(errno));

	mode = SCREEN_SESSION_MODE_TEXT_ENTRY;
	rc = screen_set_session_property_iv(g_ckb_session, SCREEN_PROPERTY_MODE, &mode);
	log_line("CKB session MODE=SESSION_TEXT_ENTRY(%d): %s\n", mode,
	         rc == 0 ? "ok" : strerror(errno));

	if (g_ckb_dev) {
		p = g_win;
		rc = screen_set_device_property_pv(g_ckb_dev, SCREEN_PROPERTY_WINDOW, &p);
		log_line("CKB device->WINDOW: %s\n", rc == 0 ? "ok" : strerror(errno));
		p = g_ckb_session;
		rc = screen_set_device_property_pv(g_ckb_dev, SCREEN_PROPERTY_SESSION, &p);
		log_line("CKB device->SESSION: %s\n", rc == 0 ? "ok" : strerror(errno));
		p = g_ckb_dev;
		rc = screen_set_session_property_pv(g_ckb_session, SCREEN_PROPERTY_DEVICES, &p);
		log_line("CKB session->DEVICES: %s\n", rc == 0 ? "ok" : strerror(errno));
	} else {
		log_line("CKB bind: no touch_keypad device\n");
	}

	screen_flush_context(g_ctx, 0);
}

static void process_screen_event(screen_event_t se, const char *via)
{
	int type = SCREEN_EVENT_NONE;
	if (screen_get_event_property_iv(se, SCREEN_PROPERTY_TYPE, &type) != 0)
		return;
	if (type == SCREEN_EVENT_NONE)
		return;

	switch (type) {
	case SCREEN_EVENT_MTOUCH_TOUCH:
	case SCREEN_EVENT_MTOUCH_MOVE:
	case SCREEN_EVENT_MTOUCH_RELEASE:
#ifdef SCREEN_EVENT_MTOUCH_PRETOUCH
	case SCREEN_EVENT_MTOUCH_PRETOUCH:
#endif
	{
		int touch_id = -1, pos[2] = {0}, src[2] = {0}, size[2] = {0};
		int pressure = -1, seq = -1, flags = 0;
		screen_window_t win = NULL;
		screen_device_t device = NULL;
		screen_session_t session = NULL;
		int role, cls, out;

		screen_get_event_property_iv(se, SCREEN_PROPERTY_TOUCH_ID, &touch_id);
		screen_get_event_property_iv(se, SCREEN_PROPERTY_POSITION, pos);
		screen_get_event_property_iv(se, SCREEN_PROPERTY_SOURCE_POSITION, src);
		screen_get_event_property_iv(se, SCREEN_PROPERTY_SIZE, size);
		screen_get_event_property_iv(se, SCREEN_PROPERTY_TOUCH_PRESSURE, &pressure);
		screen_get_event_property_iv(se, SCREEN_PROPERTY_SEQUENCE_ID, &seq);
		screen_get_event_property_iv(se, SCREEN_PROPERTY_FLAGS, &flags);
		screen_get_event_property_pv(se, SCREEN_PROPERTY_WINDOW, (void **)&win);
		screen_get_event_property_pv(se, SCREEN_PROPERTY_DEVICE, (void **)&device);
		screen_get_event_property_pv(se, SCREEN_PROPERTY_SESSION, (void **)&session);

		role = role_for_device(device);
		out = (pos[0] < 0 || pos[1] < 0 || pos[0] >= g_size[0] || pos[1] >= g_size[1]);
		if (role == DEV_ROLE_CKB) cls = CLASS_CKB;
		else if (role == DEV_ROLE_LCD) cls = CLASS_LCD;
		else if (out) cls = CLASS_CKB;
		else cls = CLASS_LCD;

		log_line("t=%llu via=%s %s id=%d pos=%d,%d src=%d,%d size=%d,%d win=%p "
		         "dev=%p session=%p product=\"%s\" role=%s outside=%d press=%d seq=%d\n",
		         (unsigned long long)now_ms(), via, event_type_name(type), touch_id,
		         pos[0], pos[1], src[0], src[1], size[0], size[1],
		         (void *)win, (void *)device, (void *)session,
		         product_for_device(device), role_name(role), out, pressure, seq);

		g_last_pos[0] = pos[0];
		g_last_pos[1] = pos[1];
		g_have_touch = 1;
		note_event(cls);
		break;
	}
	/*
	 * Hypothesis (community/SDL notes): Passport keyboard soft-touch may
	 * arrive as SCREEN_EVENT_POINTER, not MTOUCH. Log everything.
	 */
	case SCREEN_EVENT_POINTER: {
		int pos[2] = {0}, src[2] = {0}, size[2] = {0};
		int buttons = 0, flags = 0, seq = -1;
		int disp[2] = {0};
		screen_window_t win = NULL;
		screen_device_t device = NULL;
		screen_session_t session = NULL;
		int role, cls, out;

		screen_get_event_property_iv(se, SCREEN_PROPERTY_POSITION, pos);
		screen_get_event_property_iv(se, SCREEN_PROPERTY_SOURCE_POSITION, src);
		screen_get_event_property_iv(se, SCREEN_PROPERTY_SIZE, size);
		screen_get_event_property_iv(se, SCREEN_PROPERTY_BUTTONS, &buttons);
		screen_get_event_property_iv(se, SCREEN_PROPERTY_FLAGS, &flags);
		screen_get_event_property_iv(se, SCREEN_PROPERTY_SEQUENCE_ID, &seq);
		/* displacement may not exist on all pointer events */
		screen_get_event_property_iv(se, SCREEN_PROPERTY_DISPLACEMENT, disp);
		screen_get_event_property_pv(se, SCREEN_PROPERTY_WINDOW, (void **)&win);
		screen_get_event_property_pv(se, SCREEN_PROPERTY_DEVICE, (void **)&device);
		screen_get_event_property_pv(se, SCREEN_PROPERTY_SESSION, (void **)&session);

		role = role_for_device(device);
		out = (pos[0] < 0 || pos[1] < 0 || pos[0] >= g_size[0] || pos[1] >= g_size[1]);
		/* Prefer device role; if unknown and outside LCD or from ckb_dev -> CKB */
		if (role == DEV_ROLE_CKB || device == g_ckb_dev)
			cls = CLASS_CKB;
		else if (role == DEV_ROLE_LCD || device == g_lcd_dev)
			cls = CLASS_LCD;
		else if (out || (g_ckb_dev && device == g_ckb_dev))
			cls = CLASS_CKB;
		else
			cls = CLASS_LCD;

		log_line("t=%llu via=%s POINTER pos=%d,%d src=%d,%d size=%d,%d "
		         "buttons=%d flags=0x%x disp=%d,%d seq=%d win=%p dev=%p "
		         "session=%p product=\"%s\" role=%s outside=%d ***\n",
		         (unsigned long long)now_ms(), via,
		         pos[0], pos[1], src[0], src[1], size[0], size[1],
		         buttons, flags, disp[0], disp[1], seq,
		         (void *)win, (void *)device, (void *)session,
		         product_for_device(device), role_name(role), out);

		g_last_pos[0] = pos[0];
		g_last_pos[1] = pos[1];
		g_have_touch = 1;
		g_ptr_count++;
		note_event(cls);
		break;
	}
	case SCREEN_EVENT_KEYBOARD: {
		int flags = 0, sym = 0, scan = 0;
		screen_device_t device = NULL;
		screen_get_event_property_iv(se, SCREEN_PROPERTY_KEY_FLAGS, &flags);
		screen_get_event_property_iv(se, SCREEN_PROPERTY_KEY_SYM, &sym);
		screen_get_event_property_iv(se, SCREEN_PROPERTY_KEY_SCAN, &scan);
		screen_get_event_property_pv(se, SCREEN_PROPERTY_DEVICE, (void **)&device);
		log_line("t=%llu via=%s KEYBOARD flags=0x%x sym=%d scan=%d down=%d dev=%p product=\"%s\"\n",
		         (unsigned long long)now_ms(), via, flags, sym, scan,
		         (flags & KEY_DOWN) ? 1 : 0, (void *)device, product_for_device(device));
		note_event(CLASS_KEY);
		break;
	}
	case SCREEN_EVENT_DEVICE:
		log_line("t=%llu via=%s DEVICE hotplug\n", (unsigned long long)now_ms(), via);
		refresh_devices();
		try_bind_ckb();
		note_event(CLASS_OTHER);
		break;
	case SCREEN_EVENT_PROPERTY:
		/* quiet */
		break;
	default:
		log_line("t=%llu via=%s %s\n", (unsigned long long)now_ms(), via,
		         event_type_name(type));
		if (type != SCREEN_EVENT_IDLE)
			note_event(CLASS_OTHER);
		break;
	}
}

static void fill_rect(uint32_t *px, int x0, int y0, int x1, int y1, uint32_t c)
{
	int x, y, w = g_size[0], h = g_size[1], sp = g_stride / 4;
	if (x0 < 0) x0 = 0;
	if (y0 < 0) y0 = 0;
	if (x1 > w) x1 = w;
	if (y1 > h) y1 = h;
	for (y = y0; y < y1; ++y)
		for (x = x0; x < x1; ++x)
			px[y * sp + x] = c;
}

static uint32_t class_color(int cls)
{
	switch (cls) {
	case CLASS_LCD: return COL_GREEN;
	case CLASS_CKB: return COL_MAGENTA;
	case CLASS_KEY: return COL_BLUE;
	case CLASS_OTHER: return COL_ORANGE;
	default: return COL_BG;
	}
}

static void draw_hud(void)
{
	void *ptr = NULL;
	uint32_t *px;
	int rect[4] = {0, 0, g_size[0], g_size[1]};
	int w = g_size[0], h = g_size[1];
	int bar_h = h / 16, panel_h = h / 5, panel_w = w / 3;
	int cls = g_last_class, i, maxc;

	if (screen_get_buffer_property_pv(g_buf, SCREEN_PROPERTY_POINTER, &ptr) != 0)
		return;
	px = (uint32_t *)ptr;

	if (g_flash_frames > 0) g_flash_frames--;
	else if (g_event_count == 0) cls = CLASS_IDLE;

	fill_rect(px, 0, 0, w, h, class_color(cls) == COL_BG ? COL_BG : class_color(cls));
	fill_rect(px, 0, 0, w, bar_h, COL_DIM);
	{
		int ticks = g_event_count % (w / 8 + 1);
		for (i = 0; i < ticks; ++i)
			fill_rect(px, 4 + i * 8, 4, 4 + i * 8 + 5, bar_h - 4, COL_WHITE);
	}
	{
		int y0 = bar_h + 8, y1 = y0 + panel_h;
		fill_rect(px, 8, y0, panel_w - 4, y1, cls == CLASS_LCD ? COL_GREEN : 0xFF114422u);
		fill_rect(px, panel_w + 4, y0, 2 * panel_w - 4, y1, cls == CLASS_CKB ? COL_MAGENTA : 0xFF441144u);
		fill_rect(px, 2 * panel_w + 4, y0, w - 8, y1, cls == CLASS_KEY ? COL_BLUE : 0xFF112244u);
		maxc = g_lcd_count;
		if (g_ckb_count > maxc) maxc = g_ckb_count;
		if (g_key_count > maxc) maxc = g_key_count;
		if (maxc < 1) maxc = 1;
		fill_rect(px, 16, y1 - 12 - (panel_h - 24) * g_lcd_count / maxc, panel_w - 12, y1 - 8, COL_WHITE);
		fill_rect(px, panel_w + 12, y1 - 12 - (panel_h - 24) * g_ckb_count / maxc, 2 * panel_w - 12, y1 - 8, COL_WHITE);
		fill_rect(px, 2 * panel_w + 12, y1 - 12 - (panel_h - 24) * g_key_count / maxc, w - 16, y1 - 8, COL_WHITE);
	}
	if (g_have_touch) {
		int cx = g_last_pos[0], cy = g_last_pos[1], s = w / 20;
		if (cx < 0) cx = 0;
		if (cy < 0) cy = 0;
		if (cx >= w) cx = w - 1;
		if (cy >= h) cy = h - 1;
		fill_rect(px, cx - s, cy - s, cx + s, cy + s, COL_YELLOW);
		fill_rect(px, cx - 2, 0, cx + 2, h, COL_YELLOW);
		fill_rect(px, 0, cy - 2, w, cy + 2, COL_YELLOW);
	}
	screen_post_window(g_win, g_buf, 1, rect, 0);
	g_dirty = 0;
}

static int setup_sessions(void)
{
	int pos[2] = {0, 0};
	int sens_lcd = SCREEN_SENSITIVITY_FULLSCREEN, z_lcd = 0;
	int sens_ckb = SCREEN_SENSITIVITY_MASK_CONTINUE, z_ckb = -1;
	int rc;

	rc = screen_create_session_type(&g_lcd_session, g_ctx, SCREEN_EVENT_MTOUCH_TOUCH);
	if (rc) { log_line("lcd session FAIL %s\n", strerror(errno)); g_lcd_session = NULL; }
	else {
		screen_set_session_property_pv(g_lcd_session, SCREEN_PROPERTY_WINDOW, (void **)&g_win);
		screen_set_session_property_iv(g_lcd_session, SCREEN_PROPERTY_SIZE, g_size);
		screen_set_session_property_iv(g_lcd_session, SCREEN_PROPERTY_POSITION, pos);
		screen_set_session_property_iv(g_lcd_session, SCREEN_PROPERTY_SENSITIVITY, &sens_lcd);
		screen_set_session_property_iv(g_lcd_session, SCREEN_PROPERTY_ZORDER, &z_lcd);
		log_line("lcd session ok\n");
	}

	rc = screen_create_session_type(&g_ckb_session, g_ctx, SCREEN_EVENT_MTOUCH_TOUCH);
	if (rc) { log_line("CKB session FAIL %s\n", strerror(errno)); g_ckb_session = NULL; }
	else {
		screen_set_session_property_pv(g_ckb_session, SCREEN_PROPERTY_WINDOW, (void **)&g_win);
		screen_set_session_property_iv(g_ckb_session, SCREEN_PROPERTY_SIZE, g_size);
		screen_set_session_property_iv(g_ckb_session, SCREEN_PROPERTY_POSITION, pos);
		rc = screen_set_session_property_iv(g_ckb_session, SCREEN_PROPERTY_SENSITIVITY, &sens_ckb);
		log_line("CKB CONTINUE: %s\n", rc == 0 ? "ok" : strerror(errno));
		rc = screen_set_session_property_iv(g_ckb_session, SCREEN_PROPERTY_ZORDER, &z_ckb);
		log_line("CKB z=-1: %s\n", rc == 0 ? "ok" : strerror(errno));
		log_line("CKB session ok %p\n", (void *)g_ckb_session);
	}

	/* POINTER session - community notes claim Passport CKB arrives as POINTER */
	rc = screen_create_session_type(&g_ptr_session, g_ctx, SCREEN_EVENT_POINTER);
	if (rc) {
		log_line("POINTER session FAIL %s\n", strerror(errno));
		g_ptr_session = NULL;
	} else {
		screen_set_session_property_pv(g_ptr_session, SCREEN_PROPERTY_WINDOW, (void **)&g_win);
		screen_set_session_property_iv(g_ptr_session, SCREEN_PROPERTY_SIZE, g_size);
		screen_set_session_property_iv(g_ptr_session, SCREEN_PROPERTY_POSITION, pos);
		rc = screen_set_session_property_iv(g_ptr_session, SCREEN_PROPERTY_SENSITIVITY, &sens_ckb);
		log_line("POINTER session CONTINUE: %s\n", rc == 0 ? "ok" : strerror(errno));
		rc = screen_set_session_property_iv(g_ptr_session, SCREEN_PROPERTY_ZORDER, &z_ckb);
		log_line("POINTER session z=-1: %s\n", rc == 0 ? "ok" : strerror(errno));
		log_line("POINTER session ok %p\n", (void *)g_ptr_session);
	}

	rc = screen_create_session_type(&g_key_session, g_ctx, SCREEN_EVENT_KEYBOARD);
	if (rc) { log_line("KEY session FAIL %s\n", strerror(errno)); g_key_session = NULL; }
	else {
		screen_set_session_property_pv(g_key_session, SCREEN_PROPERTY_WINDOW, (void **)&g_win);
		log_line("KEY session ok\n");
	}
	return 0;
}

static int setup_screen(void)
{
	int usage = SCREEN_USAGE_NATIVE | SCREEN_USAGE_READ | SCREEN_USAGE_WRITE | SCREEN_USAGE_ROTATION;
	int format = SCREEN_FORMAT_RGBA8888;
	int sensitivity = SCREEN_SENSITIVITY_MASK_CONTINUE | SCREEN_SENSITIVITY_MASK_FULLSCREEN;
	int nbufs = 1, ndisp = 0, zorder = 10;
	screen_display_t *displays = NULL;

	if (screen_create_context(&g_ctx, SCREEN_APPLICATION_CONTEXT) != 0)
		return -1;
	if (screen_create_window(&g_win, g_ctx) != 0)
		return -1;
	if (screen_create_event(&g_ev) != 0)
		return -1;

	screen_set_window_property_iv(g_win, SCREEN_PROPERTY_USAGE, &usage);
	screen_set_window_property_iv(g_win, SCREEN_PROPERTY_FORMAT, &format);
	if (screen_set_window_property_iv(g_win, SCREEN_PROPERTY_SENSITIVITY, &sensitivity) != 0) {
		int s2 = SCREEN_SENSITIVITY_FULLSCREEN;
		screen_set_window_property_iv(g_win, SCREEN_PROPERTY_SENSITIVITY, &s2);
	}
	screen_set_window_property_iv(g_win, SCREEN_PROPERTY_ZORDER, &zorder);

	if (screen_get_context_property_iv(g_ctx, SCREEN_PROPERTY_DISPLAY_COUNT, &ndisp) == 0 && ndisp > 0) {
		displays = calloc((size_t)ndisp, sizeof(*displays));
		if (displays &&
		    screen_get_context_property_pv(g_ctx, SCREEN_PROPERTY_DISPLAYS, (void **)displays) == 0) {
			screen_set_window_property_pv(g_win, SCREEN_PROPERTY_DISPLAY, (void **)&displays[0]);
			screen_get_display_property_iv(displays[0], SCREEN_PROPERTY_SIZE, g_size);
		}
		free(displays);
	}
	if (g_size[0] <= 0) { g_size[0] = 1440; g_size[1] = 1440; }

	screen_set_window_property_iv(g_win, SCREEN_PROPERTY_SIZE, g_size);
	screen_set_window_property_iv(g_win, SCREEN_PROPERTY_BUFFER_SIZE, g_size);
	if (screen_create_window_buffers(g_win, nbufs) != 0)
		return -1;
	if (screen_get_window_property_pv(g_win, SCREEN_PROPERTY_RENDER_BUFFERS, (void **)&g_buf) != 0)
		return -1;
	screen_get_buffer_property_iv(g_buf, SCREEN_PROPERTY_STRIDE, &g_stride);

	setup_sessions();
	return 0;
}

static void handle_nav(bps_event_t *event)
{
	int code = bps_event_get_code(event);
	if (code == NAVIGATOR_EXIT) {
		log_line("t=%llu NAVIGATOR_EXIT\n", (unsigned long long)now_ms());
		g_exit = 1;
	} else if (code == NAVIGATOR_SWIPE_DOWN) {
		log_line("=== MARK lcd=%d ckb=%d key=%d total=%d ===\n",
		         g_lcd_count, g_ckb_count, g_key_count, g_event_count);
		try_bind_ckb();
		note_event(CLASS_OTHER);
	} else if (code == NAVIGATOR_WINDOW_STATE) {
		navigator_window_state_t st = navigator_event_get_window_state(event);
		log_line("t=%llu WINDOW_STATE %d\n", (unsigned long long)now_ms(), (int)st);
		if (st == NAVIGATOR_WINDOW_FULLSCREEN)
			try_bind_ckb();
	} else if (code == NAVIGATOR_ORIENTATION_CHECK) {
		navigator_orientation_check_response(event, true);
	} else if (code == NAVIGATOR_ORIENTATION) {
		int angle = navigator_event_get_orientation_angle(event);
		g_angle = angle;
		screen_set_window_property_iv(g_win, SCREEN_PROPERTY_ROTATION, &angle);
		screen_get_window_property_iv(g_win, SCREEN_PROPERTY_SIZE, g_size);
		navigator_done_orientation(event);
		note_event(CLASS_OTHER);
	}
}

int main(int argc, char **argv)
{
	deviceinfo_details_t *di = NULL;
	(void)argc;
	(void)argv;

	if (getenv("HOME"))
		chdir(getenv("HOME"));

	if (bps_initialize() != BPS_SUCCESS)
		return 1;
	open_log();

	if (setup_screen() != 0) {
		bps_shutdown();
		return 1;
	}

	log_line("=== %s v6 (POINTER+MTOUCH, dual raw/BPS pumps) ===\n", APP_NAME);
	log_line("time_ms=%llu log=%s\n", (unsigned long long)now_ms(), g_log_path);
	if (deviceinfo_get_details(&di) == BPS_SUCCESS) {
		log_line("model=%s os=%s kbd=%d\n",
		         deviceinfo_details_get_model_name(di),
		         deviceinfo_details_get_device_os_version(di),
		         deviceinfo_details_get_keyboard(di));
		deviceinfo_free_details(&di);
	}
	refresh_devices();
	try_bind_ckb();
	log_line("HUD GREEN=LCD MAGENTA=CKB(touch_keypad) BLUE=key\n");
	log_line("Test: soft-swipe keys, hard keys, LCD tap, bezel MARK\n");

	/*
	 * Dual delivery path:
	 *  - screen_request_events + BPS (what SDL uses)
	 *  - screen_get_event raw (what Cascades ScreenEventThread uses)
	 * Log via=raw vs via=bps so we see which path (if any) carries POINTER/CKB.
	 */
	if (screen_request_events(g_ctx) != BPS_SUCCESS)
		log_line("screen_request_events FAIL: %s\n", strerror(errno));
	else
		log_line("screen_request_events ok (BPS path armed)\n");
	navigator_request_events(0);
	virtualkeyboard_request_events(0);
	virtualkeyboard_show();
	log_line("virtualkeyboard_show() called\n");
	log_line("NOTE: SDL drops POINTER when SOURCE_POSITION.y < 0 "
	         "(see handlePointerEvent) - we log those anyway\n");

	draw_hud();

	while (!g_exit) {
		bps_event_t *be = NULL;
		int rc;

		/* Path A: raw screen_get_event (non-blocking-ish) */
		rc = screen_get_event(g_ctx, g_ev, 20 * 1000 * 1000);
		if (rc == 0)
			process_screen_event(g_ev, "raw");

		/* Path B: BPS (SDL-style) */
		while (bps_get_event(&be, 0) == BPS_SUCCESS && be) {
			int dom = bps_event_get_domain(be);
			if (dom == navigator_get_domain())
				handle_nav(be);
			else if (dom == virtualkeyboard_get_domain())
				log_line("t=%llu VKB code=%d\n", (unsigned long long)now_ms(),
				         bps_event_get_code(be));
			else if (dom == screen_get_domain()) {
				screen_event_t se = screen_event_get_event(be);
				process_screen_event(se, "bps");
			}
		}

		if (g_dirty || g_flash_frames > 0)
			draw_hud();
	}

	log_line("=== end total=%d lcd=%d ckb=%d key=%d ptr=%d other=%d ===\n",
	         g_event_count, g_lcd_count, g_ckb_count, g_key_count, g_ptr_count, g_other_count);
	if (g_log) fclose(g_log);
	virtualkeyboard_hide();
	if (g_lcd_session) screen_destroy_session(g_lcd_session);
	if (g_ckb_session) screen_destroy_session(g_ckb_session);
	if (g_ptr_session) screen_destroy_session(g_ptr_session);
	if (g_key_session) screen_destroy_session(g_key_session);
	if (g_ev) screen_destroy_event(g_ev);
	if (g_win) screen_destroy_window(g_win);
	if (g_ctx) screen_destroy_context(g_ctx);
	bps_shutdown();
	return 0;
}
