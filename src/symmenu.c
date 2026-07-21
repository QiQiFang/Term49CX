
#include <ioctl.h>
#include <unix.h>
#include <termios.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/select.h>

#include <bps/screen.h>
#include <bps/virtualkeyboard.h>
#include <bps/deviceinfo.h>
#include <unicode/utf.h>

#include "SDL.h"
#include "SDL_ttf.h"
#include "SDL_syswm.h"
#include "SDL_thread.h"

#include "terminal.h"
#include "ecma48.h"
#include "preferences.h"
#include "buffer.h"
#include "io.h"
#include "colors.h"

/* Map a symmenu key's raw "to" value to a readable label when it is a control
 * byte, an escape sequence, or a terminfo key name -- values that would
 * otherwise draw as an empty box (Esc, Tab, Ctrl+C) or a raw escape string
 * (up-arrow as a box followed by "[A"). Writes a UTF-8 label into out and
 * returns 1; returns 0 when "to" is already printable and should render as-is. */
static int symkey_label(const char *to, char *out, size_t outsz) {
	if (to == NULL || to[0] == '\0') {
		return 0;
	}
	size_t n = strlen(to);

	/* terminfo key names (cf. is_terminfo_keystrokes in main.c) */
	if (to[0] == 'k') {
		const char *sym = NULL;
		if      (!strcmp(to, "kcuu1")) sym = "\xe2\x86\x91"; /* up    */
		else if (!strcmp(to, "kcud1")) sym = "\xe2\x86\x93"; /* down  */
		else if (!strcmp(to, "kcuf1")) sym = "\xe2\x86\x92"; /* right */
		else if (!strcmp(to, "kcub1")) sym = "\xe2\x86\x90"; /* left  */
		else if (!strcmp(to, "khome")) sym = "Home";
		else if (!strcmp(to, "kend"))  sym = "End";
		else if (!strcmp(to, "kent"))  sym = "\xe2\x8f\x8e"; /* return */
		else if (!strcmp(to, "kf10"))  sym = "F10";
		else if (!strcmp(to, "kf11"))  sym = "F11";
		else if (!strcmp(to, "kf12"))  sym = "F12";
		else if (!strncmp(to, "kf", 2) && to[2] >= '1' && to[2] <= '9' && to[3] == '\0') {
			snprintf(out, outsz, "F%c", to[2]);
			return 1;
		}
		if (sym != NULL) {
			snprintf(out, outsz, "%s", sym);
			return 1;
		}
	}

	/* a single control byte */
	if (n == 1) {
		unsigned char c = (unsigned char)to[0];
		const char *sym = NULL;
		if      (c == 0x1b) sym = "\xe2\x8e\x8b"; /* escape */
		else if (c == 0x09) sym = "\xe2\x87\xa5"; /* tab    */
		else if (c == 0x0d || c == 0x0a) sym = "\xe2\x8f\x8e"; /* return */
		else if (c == 0x08 || c == 0x7f) sym = "\xe2\x8c\xab"; /* erase  */
		else if (c < 0x20) { snprintf(out, outsz, "^%c", '@' + c); return 1; }
		if (sym != NULL) { snprintf(out, outsz, "%s", sym); return 1; }
		return 0; /* printable single character: render literally */
	}

	/* escape sequences: ESC [ ... (CSI) or ESC O ... (SS3) */
	if ((unsigned char)to[0] == 0x1b && (to[1] == '[' || to[1] == 'O')) {
		const char *q = to + 2;
		const char *sym = NULL;
		if      (!strcmp(q, "A")) sym = "\xe2\x86\x91"; /* up    */
		else if (!strcmp(q, "B")) sym = "\xe2\x86\x93"; /* down  */
		else if (!strcmp(q, "C")) sym = "\xe2\x86\x92"; /* right */
		else if (!strcmp(q, "D")) sym = "\xe2\x86\x90"; /* left  */
		else if (!strcmp(q, "H") || !strcmp(q, "1~") || !strcmp(q, "7~")) sym = "Home";
		else if (!strcmp(q, "F") || !strcmp(q, "4~") || !strcmp(q, "8~")) sym = "End";
		else if (!strcmp(q, "5~")) sym = "PgUp";
		else if (!strcmp(q, "6~")) sym = "PgDn";
		else if (!strcmp(q, "2~")) sym = "Ins";
		else if (!strcmp(q, "3~")) sym = "Del";
		else if (!strcmp(q, "P")) sym = "F1";
		else if (!strcmp(q, "Q")) sym = "F2";
		else if (!strcmp(q, "R")) sym = "F3";
		else if (!strcmp(q, "S")) sym = "F4";
		if (sym != NULL) { snprintf(out, outsz, "%s", sym); return 1; }
	}

	/* any other string that carries control bytes: render caret notation
	 * (^[, ^C, ...) so it is at least readable instead of drawing boxes */
	int has_ctrl = 0;
	for (size_t i = 0; i < n; ++i) {
		unsigned char c = (unsigned char)to[i];
		if (c < 0x20 || c == 0x7f) { has_ctrl = 1; break; }
	}
	if (has_ctrl) {
		size_t o = 0;
		for (size_t i = 0; i < n && o + 2 < outsz; ++i) {
			unsigned char c = (unsigned char)to[i];
			if (c == 0x7f)      { out[o++] = '^'; out[o++] = '?'; }
			else if (c < 0x20)  { out[o++] = '^'; out[o++] = (char)('@' + c); }
			else                { out[o++] = (char)c; }
		}
		out[o] = '\0';
		return 1;
	}

	return 0; /* already printable */
}

/* Some monospace fonts carry the pretty keyboard glyphs (DejaVu Sans Mono has
 * escape/tab/erase), others carry the Control Pictures block instead (Cascadia
 * has the little "ESC"/"HT" glyphs). Map a pretty symbol to its control-picture
 * equivalent so a key still shows a single glyph when neither the main nor the
 * fallback font provides the pretty one. Returns 0 if there is no alternate. */
static UChar symbol_alternate(UChar c) {
	switch (c) {
		case 0x238B: return 0x241B; /* escape  -> box "ESC" */
		case 0x21E5: return 0x2409; /* tab     -> box "HT"  */
		case 0x232B: return 0x2421; /* erase   -> box "DEL" */
		case 0x23CE: return 0x240D; /* return  -> box "CR"  */
	}
	return 0;
}

void destroy_symmenu(symmenu_t *menu) {
	for (symkey_t **row = menu->keys; row != NULL; ++row) {
		for (symkey_t *key = *row; key->map != NULL; ++key) {
			free(key->uc);
			free(key);
		}
		free(row);
	}
	
	free(menu->entries);
	SDL_FreeSurface(menu->surface);
}

/* Use the preferences struct to initalize all the SDL stuff for symmenu */
SDL_Surface *render_symmenu(SDL_Surface *screen, pref_t *prefs, symmenu_t *menu) {
	/* get info about menu */
	int num_rows = 0;
	int longest_row_len = 0;
	for (; menu->keys[num_rows] != NULL; ++num_rows) {
		int col_len = 0;
		for (; menu->keys[num_rows][col_len].map != NULL; ++col_len) { }
		if (col_len > longest_row_len) {
			longest_row_len = col_len;
		}
	}
	
	if (menu->keys[0] == NULL) {
		return NULL;
	}

	/* symmenus should all be the same size (and centered, ideally) */
	int bg_font_size = preferences_guess_best_font_size(prefs, 10 * 1.25);
	int corner_font_size = bg_font_size / 5;
	int fg_font_size = (6 * bg_font_size) / 10;

	/* Load the font - if this was going to fail, it would have failed earlier in init() */
	TTF_Font* fg_font = TTF_OpenFont(prefs->font_path, fg_font_size);
	TTF_SetFontStyle(fg_font, TTF_STYLE_NORMAL);
	TTF_SetFontOutline(fg_font, 0);
	TTF_SetFontKerning(fg_font, 0);
	TTF_SetFontHinting(fg_font, TTF_HINTING_NORMAL);

	TTF_Font* bg_font = TTF_OpenFont(prefs->font_path, bg_font_size);
	TTF_SetFontStyle(bg_font, TTF_STYLE_NORMAL);
	TTF_SetFontOutline(bg_font, 0);
	TTF_SetFontKerning(bg_font, 0);
	TTF_SetFontHinting(bg_font, TTF_HINTING_NORMAL);

	/* fallback font for label glyphs (arrows, erase, ...) the main font lacks */
	TTF_Font* fg_fallback = NULL;
	if (prefs->fallback_font_path != NULL && prefs->fallback_font_path[0] != '\0') {
		fg_fallback = TTF_OpenFont(prefs->fallback_font_path, fg_font_size);
		if (fg_fallback != NULL) {
			TTF_SetFontStyle(fg_fallback, TTF_STYLE_NORMAL);
			TTF_SetFontOutline(fg_fallback, 0);
			TTF_SetFontKerning(fg_fallback, 0);
			TTF_SetFontHinting(fg_fallback, TTF_HINTING_NORMAL);
		}
	}

	TTF_Font* corner_font = TTF_OpenFont(prefs->font_path, corner_font_size);
	TTF_SetFontStyle(corner_font, TTF_STYLE_NORMAL);
	TTF_SetFontOutline(corner_font, 0);
	TTF_SetFontKerning(corner_font, 0);
	TTF_SetFontHinting(corner_font, TTF_HINTING_NORMAL);
	
	/* render a test character to set the proper height */
	UChar testchar;
	io_read_utf8_string("#", 2, &testchar);
	SDL_Surface *testsurf = TTF_RenderUNICODE_Shaded(fg_font, &testchar, (SDL_Color)SYMMENU_FONT, (SDL_Color)SYMMENU_BACKGROUND);
	int bg_h = testsurf->h + (2*SYMKEY_BORDER_SIZE) + SYMMENU_FRET_SIZE;
	SDL_FreeSurface(testsurf);
	int screen_width = atoi(getenv("WIDTH"));
	int bg_w = screen_width / longest_row_len;
	
	/* fill in the symkey entries from prefs keymap*/
	for (int row = 0; menu->keys[row] != NULL; ++row) {
		for (int col = 0; menu->keys[row][col].map != NULL; ++col) {
			symkey_t *sk = &menu->keys[row][col];
			
			/* calculate the background positions */
			sk->hitbox.x = col * bg_w;
			sk->hitbox.y = (screen->h - num_rows * bg_h) + row * bg_h;
			sk->hitbox.w = bg_w;
			sk->hitbox.h = bg_h;
			
			/* init the UChar from prefs keymap */
			int to_len = strlen(sk->map->to);
			sk->uc = (UChar*)calloc(to_len + 1, sizeof(UChar));
			int uc_len = io_read_utf8_string(sk->map->to, to_len, sk->uc);
		}
	}
	
	/* initialize the symmenu surface */
	SDL_Surface *menu_surface = SDL_CreateRGBSurface(0, screen->w, num_rows * bg_h, 24, 0, 0, 0, 0);
	/* render background color */
	SDL_Rect destrect;
	destrect.w = menu_surface->w;
	destrect.h = menu_surface->h;
	destrect.x = 0; destrect.y = 0;
	
	SDL_Color bgc = (SDL_Color)SYMMENU_BACKGROUND;
	Uint32 bg_fill_color = SDL_MapRGB(screen->format, bgc.r, bgc.b, bgc.g);
	
	if (SDL_FillRect(menu_surface, &destrect, bg_fill_color) != 0) {
		fprintf(stderr, "Symmenu bgfill failed: %s\n", SDL_GetError());
		return NULL;
	}

		/* render frets */
	for (int i = 0; i < num_rows; ++i) {
		SDL_Rect destrect;
		destrect.x = 0;
		destrect.y = bg_h * i;
		destrect.h = SYMMENU_FRET_SIZE;
		destrect.w = screen->w;

		SDL_Color bgc = (SDL_Color)SYMMENU_FRET;
		Uint32 fret_fill_color = SDL_MapRGB(screen->format, bgc.r, bgc.b, bgc.g);
	
		if (SDL_FillRect(menu_surface, &destrect, fret_fill_color) != 0) {
			fprintf(stderr, "Symmenu fret bgfill failed: %s\n", SDL_GetError());
			return NULL;
		}

		/* left-to-right borders */
		destrect.x = 0;
		destrect.y = bg_h * i + SYMMENU_FRET_SIZE;
		destrect.h = SYMKEY_BORDER_SIZE;
		destrect.w = screen->w;
		bgc = (SDL_Color)SYMMENU_BORDER;
		fret_fill_color = SDL_MapRGB(screen->format, bgc.r, bgc.b, bgc.g);
		if (SDL_FillRect(menu_surface, &destrect, fret_fill_color) != 0) {
			fprintf(stderr, "Symmenu border bgfill failed: %s\n", SDL_GetError());
			return NULL;
		}
		destrect.x = 0;
		destrect.y = bg_h * (i + 1) - SYMKEY_BORDER_SIZE;
		bgc = (SDL_Color)SYMMENU_BORDER;
		fret_fill_color = SDL_MapRGB(screen->format, bgc.r, bgc.b, bgc.g);
		if (SDL_FillRect(menu_surface, &destrect, fret_fill_color) != 0) {
			fprintf(stderr, "Symmenu border bgfill failed: %s\n", SDL_GetError());
			return NULL;
		}
	}

	/* render the keys */
	UChar cornerchar[2]; cornerchar[1] = NULL;
	for (int row = 0; menu->keys[row] != NULL; ++row) {
		for (int col = 0; menu->keys[row][col].map != NULL; ++col) {
			symkey_t *sk = &menu->keys[row][col];
			SDL_Rect destrect;

			/* main symbol: special keys (Esc, Tab, Ctrl+C, arrows, ...) get
			 * a readable label instead of an unprintable box */
			UChar labelbuf[64];
			char lbl[64];
			int label_len;
			const char *label_src;
			if (symkey_label(sk->map->to, lbl, sizeof(lbl))) {
				label_src = lbl;
			} else {
				label_src = sk->map->to;
			}
			size_t label_srclen = strlen(label_src);
			/* keep room for the terminating NUL that TTF_RenderUNICODE needs */
			if (label_srclen > 62) label_srclen = 62;
			label_len = io_read_utf8_string(label_src, label_srclen, labelbuf);
			if (label_len < 0) label_len = 0;
			if (label_len > 63) label_len = 63;
			labelbuf[label_len] = 0;

			/* choose a font that can actually draw the label: prefer the main
			 * font, fall back for glyphs it lacks, and for a lone symbol drop
			 * to a control-picture alternate if neither font has the pretty one */
			TTF_Font *label_font = fg_font;
			if (label_len == 1) {
				UChar g = labelbuf[0];
				if (TTF_GlyphIsProvided(fg_font, g)) {
					label_font = fg_font;
				} else if (fg_fallback != NULL && TTF_GlyphIsProvided(fg_fallback, g)) {
					label_font = fg_fallback;
				} else {
					UChar alt = symbol_alternate(g);
					if (alt != 0 && TTF_GlyphIsProvided(fg_font, alt)) {
						labelbuf[0] = alt;
					} else if (alt != 0 && fg_fallback != NULL && TTF_GlyphIsProvided(fg_fallback, alt)) {
						labelbuf[0] = alt;
						label_font = fg_fallback;
					}
				}
			} else if (fg_fallback != NULL) {
				for (int i = 0; i < label_len; ++i) {
					if (!TTF_GlyphIsProvided(fg_font, labelbuf[i])
					    && TTF_GlyphIsProvided(fg_fallback, labelbuf[i])) {
						label_font = fg_fallback;
						break;
					}
				}
			}

			destrect.x = sk->hitbox.x + SYMKEY_BORDER_SIZE;
			destrect.y = sk->hitbox.y - (screen->h - num_rows * bg_h) + SYMKEY_BORDER_SIZE + SYMMENU_FRET_SIZE;
			SDL_Surface *destsurf = TTF_RenderUNICODE_Shaded(label_font, labelbuf, (SDL_Color)SYMMENU_FONT, (SDL_Color)SYMMENU_BACKGROUND);

			/* shrink a multi-character label that would overflow the key */
			int avail_w = bg_w - 2 * SYMKEY_BORDER_SIZE;
			if (destsurf != NULL && avail_w > 0 && destsurf->w > avail_w) {
				int shrunk = (fg_font_size * avail_w) / destsurf->w;
				if (shrunk < 6) shrunk = 6;
				TTF_Font *sfont = TTF_OpenFont(
				    (label_font == fg_fallback) ? prefs->fallback_font_path : prefs->font_path,
				    shrunk);
				if (sfont != NULL) {
					TTF_SetFontStyle(sfont, TTF_STYLE_NORMAL);
					TTF_SetFontOutline(sfont, 0);
					TTF_SetFontKerning(sfont, 0);
					TTF_SetFontHinting(sfont, TTF_HINTING_NORMAL);
					SDL_Surface *s2 = TTF_RenderUNICODE_Shaded(sfont, labelbuf, (SDL_Color)SYMMENU_FONT, (SDL_Color)SYMMENU_BACKGROUND);
					if (s2 != NULL) {
						SDL_FreeSurface(destsurf);
						destsurf = s2;
					}
					TTF_CloseFont(sfont);
				}
			}

			if (destsurf != NULL) {
				destrect.w = destsurf->w;
				destrect.h = destsurf->h;
				if (SDL_BlitSurface(destsurf, NULL, menu_surface, &destrect) != 0){
					PRINT(stderr, "Blit Failed: %s\n", SDL_GetError());
				}
				SDL_FreeSurface(destsurf);
			}

			/* from key */
			cornerchar[0] = sk->map->from;
			destrect.x = sk->hitbox.x;
			destrect.y = sk->hitbox.y - (screen->h - num_rows * bg_h) + SYMKEY_BORDER_SIZE + SYMMENU_FRET_SIZE;
			destsurf = TTF_RenderUNICODE_Shaded(corner_font, cornerchar, (SDL_Color)SYMMENU_FONT, (SDL_Color)SYMMENU_BACKGROUND);
			destrect.w = destsurf->w;
			destrect.h = destsurf->h;
			if(SDL_BlitSurface(destsurf, NULL, menu_surface, &destrect) != 0){
				PRINT(stderr, "Blit Failed: %s\n", SDL_GetError());
			}
			SDL_FreeSurface(destsurf);
		}
	}

	TTF_CloseFont(fg_font);
	TTF_CloseFont(corner_font);
	TTF_CloseFont(bg_font);
	if (fg_fallback != NULL) {
		TTF_CloseFont(fg_fallback);
	}

	return menu_surface;
}
