/*
 * vim:ts=4:sw=4:expandtab
 *
 * © 2010 Michael Stapelberg
 *
 * See LICENSE for licensing information
 *
 */
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <xcb/xcb.h>
#include <ev.h>
#include <cairo.h>
#include <cairo/cairo-xcb.h>

#include "i3lock.h"
#include "xcb.h"
#include "unlock_indicator.h"
#include "randr.h"
#include "dpi.h"

#define BAR_WIDTH 236
#define BAR_HEIGHT 22
#define TEXT_HEIGHT 100

/*******************************************************************************
 * Variables defined in i3lock.c.
 ******************************************************************************/

extern bool debug_mode;

/* The current position in the input buffer. Useful to determine if any
 * characters of the password have already been entered or not. */
extern int input_position;

/* The lock window. */
extern xcb_window_t win;

/* The current resolution of the X11 root window. */
extern uint32_t last_resolution[2];

/* Whether the unlock indicator is enabled (defaults to true). */
extern bool unlock_indicator;

/* List of pressed modifiers, or NULL if none are pressed. */
extern char *modifier_string;

/* A Cairo surface containing the specified image (-i), if any. */
extern cairo_surface_t *img;

/* Whether the image should be tiled. */
extern bool tile;
/* The background color to use (in hex). */
extern char color[7];

/* Whether the failed attempts should be displayed. */
extern bool show_failed_attempts;
/* Number of failed unlock attempts. */
extern int failed_attempts;

/*******************************************************************************
 * Variables defined in xcb.c.
 ******************************************************************************/

/* The root screen, to determine the DPI. */
extern xcb_screen_t *screen;

/*******************************************************************************
 * Local variables.
 ******************************************************************************/

/* Cache the screen’s visual, necessary for creating a Cairo context. */
static xcb_visualtype_t *vistype;

int last_3bar_start = 0;

/* Maintain the current unlock/PAM state to draw the appropriate unlock
 * indicator. */
unlock_state_t unlock_state;
auth_state_t auth_state;

/*
 * Draws global image with fill color onto a pixmap with the given
 * resolution and returns it.
 *
 */
xcb_pixmap_t draw_image(uint32_t *resolution) {
    xcb_pixmap_t bg_pixmap = XCB_NONE;
    const double scaling_factor = get_dpi_value() / 96.0;

    if (!vistype)
        vistype = get_root_visual_type(screen);
    bg_pixmap = create_bg_pixmap(conn, screen, resolution, color);
    /* Initialize cairo: Create one in-memory surface to render the unlock
     * indicator on, create one XCB surface to actually draw (one or more,
     * depending on the amount of screens) unlock indicators on. */
    cairo_surface_t *baroutput = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, BAR_WIDTH, BAR_HEIGHT);
    cairo_surface_t *textoutput = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, BAR_WIDTH, TEXT_HEIGHT);
    cairo_t *barctx = cairo_create(baroutput);
    cairo_t *textctx = cairo_create(textoutput);

    cairo_surface_t *xcb_output = cairo_xcb_surface_create(conn, bg_pixmap, vistype, resolution[0], resolution[1]);
    cairo_t *xcb_ctx = cairo_create(xcb_output);

    if (img) {
        if (!tile) {
            cairo_set_source_surface(xcb_ctx, img, ((double)resolution[0] - 1920) / 2, ((double)resolution[1] - 1080) / 2);
            cairo_paint(xcb_ctx);
        } else {
            /* create a pattern and fill a rectangle as big as the screen */
            cairo_pattern_t *pattern;
            pattern = cairo_pattern_create_for_surface(img);
            cairo_set_source(xcb_ctx, pattern);
            cairo_pattern_set_extend(pattern, CAIRO_EXTEND_REPEAT);
            cairo_rectangle(xcb_ctx, 0, 0, resolution[0], resolution[1]);
            cairo_fill(xcb_ctx);
            cairo_pattern_destroy(pattern);
        }
    } else {
        char strgroups[3][3] = {{color[0], color[1], '\0'},
                                {color[2], color[3], '\0'},
                                {color[4], color[5], '\0'}};
        uint32_t rgb16[3] = {(strtol(strgroups[0], NULL, 16)),
                             (strtol(strgroups[1], NULL, 16)),
                             (strtol(strgroups[2], NULL, 16))};
        cairo_set_source_rgb(xcb_ctx, rgb16[0] / 255.0, rgb16[1] / 255.0, rgb16[2] / 255.0);
        cairo_rectangle(xcb_ctx, 0, 0, resolution[0], resolution[1]);
        cairo_fill(xcb_ctx);
    }

    if (unlock_indicator &&
        (unlock_state >= STATE_KEY_PRESSED || auth_state > STATE_AUTH_IDLE)) {
        cairo_scale(barctx, scaling_factor, scaling_factor);
        cairo_scale(textctx, scaling_factor, scaling_factor);

        if (auth_state == STATE_AUTH_VERIFY) {
            /* We want the bars to start over after a wrong password. */
            last_3bar_start = 0;
        }

        if (unlock_state == STATE_KEY_ACTIVE) {
            /* Move the bars forward */
            last_3bar_start = (last_3bar_start + 1) % 15;
        } else if (unlock_state == STATE_BACKSPACE_ACTIVE) {
            /* Move the bars backwards */
            last_3bar_start = (last_3bar_start + 14) % 15;
        }

        //cairo_set_source_rgb(barctx, 1, 0, 1);
        //cairo_rectangle(barctx, 0, 0, BAR_WIDTH, BAR_HEIGHT);
        //cairo_fill(barctx);

        cairo_set_source_rgb(barctx, 83.0 / 255, 118.0 / 255, 237.0 / 255);
        cairo_rectangle(barctx, last_3bar_start * 20 - 59, 0, 13, 1);
        cairo_fill(barctx);
        cairo_set_source_rgb(barctx, 83.0 / 255, 118.0 / 255, 237.0 / 255);
        cairo_rectangle(barctx, last_3bar_start * 20 - 60, 1, 15, 3);
        cairo_fill(barctx);
        cairo_set_source_rgb(barctx, 129.0 / 255, 153.0 / 255, 245.0 / 255);
        cairo_rectangle(barctx, last_3bar_start * 20 - 60, 4, 15, 5);
        cairo_fill(barctx);
        cairo_set_source_rgb(barctx, 83.0 / 255, 118.0 / 255, 237.0 / 255);
        cairo_rectangle(barctx, last_3bar_start * 20 - 60, 9, 15, 4);
        cairo_fill(barctx);
        cairo_set_source_rgb(barctx, 37.0 / 255, 53.0 / 255, 197.0 / 255);
        cairo_rectangle(barctx, last_3bar_start * 20 - 60, 13, 15, 8);
        cairo_fill(barctx);
        cairo_set_source_rgb(barctx, 37.0 / 255, 53.0 / 255, 197.0 / 255);
        cairo_rectangle(barctx, last_3bar_start * 20 - 59, 21, 13, 1);
        cairo_fill(barctx);

        cairo_set_source_rgb(barctx, 83.0 / 255, 118.0 / 255, 237.0 / 255);
        cairo_rectangle(barctx, last_3bar_start * 20 - 39, 0, 13, 1);
        cairo_fill(barctx);
        cairo_set_source_rgb(barctx, 83.0 / 255, 118.0 / 255, 237.0 / 255);
        cairo_rectangle(barctx, last_3bar_start * 20 - 40, 1, 15, 3);
        cairo_fill(barctx);
        cairo_set_source_rgb(barctx, 129.0 / 255, 153.0 / 255, 245.0 / 255);
        cairo_rectangle(barctx, last_3bar_start * 20 - 40, 4, 15, 6);
        cairo_fill(barctx);
        cairo_set_source_rgb(barctx, 83.0 / 255, 118.0 / 255, 237.0 / 255);
        cairo_rectangle(barctx, last_3bar_start * 20 - 40, 10, 15, 4);
        cairo_fill(barctx);
        cairo_set_source_rgb(barctx, 37.0 / 255, 53.0 / 255, 197.0 / 255);
        cairo_rectangle(barctx, last_3bar_start * 20 - 40, 14, 15, 7);
        cairo_fill(barctx);
        cairo_set_source_rgb(barctx, 37.0 / 255, 53.0 / 255, 197.0 / 255);
        cairo_rectangle(barctx, last_3bar_start * 20 - 39, 21, 13, 1);
        cairo_fill(barctx);

        cairo_set_source_rgb(barctx, 83.0 / 255, 118.0 / 255, 237.0 / 255);
        cairo_rectangle(barctx, last_3bar_start * 20 - 19, 0, 13, 1);
        cairo_fill(barctx);
        cairo_set_source_rgb(barctx, 83.0 / 255, 118.0 / 255, 237.0 / 255);
        cairo_rectangle(barctx, last_3bar_start * 20 - 20, 1, 15, 3);
        cairo_fill(barctx);
        cairo_set_source_rgb(barctx, 129.0 / 255, 153.0 / 255, 245.0 / 255);
        cairo_rectangle(barctx, last_3bar_start * 20 - 20, 4, 15, 6);
        cairo_fill(barctx);
        cairo_set_source_rgb(barctx, 83.0 / 255, 118.0 / 255, 237.0 / 255);
        cairo_rectangle(barctx, last_3bar_start * 20 - 20, 10, 15, 6);
        cairo_fill(barctx);
        cairo_set_source_rgb(barctx, 37.0 / 255, 53.0 / 255, 197.0 / 255);
        cairo_rectangle(barctx, last_3bar_start * 20 - 20, 16, 15, 5);
        cairo_fill(barctx);
        cairo_set_source_rgb(barctx, 37.0 / 255, 53.0 / 255, 197.0 / 255);
        cairo_rectangle(barctx, last_3bar_start * 20 - 19, 21, 13, 1);
        cairo_fill(barctx);

        /* Display a (centered) text of the current PAM state. */
        char *text = NULL;
        /* We don't want to show more than a 3-digit number. */
        char buf[4];

        cairo_set_source_rgb(textctx, 0.85, 0.85, 0.85);
        cairo_select_font_face(textctx, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(textctx, 28.0);
        switch (auth_state) {
            case STATE_AUTH_VERIFY:
                text = "Verifying…";
                break;
            case STATE_AUTH_LOCK:
                text = "Locking…";
                break;
            case STATE_AUTH_WRONG:
                text = "Wrong!";
                break;
            case STATE_I3LOCK_LOCK_FAILED:
                text = "Lock failed!";
                break;
            default:
                if (unlock_state == STATE_NOTHING_TO_DELETE) {
                    text = "No input";
                }
                if (show_failed_attempts && failed_attempts > 0) {
                    if (failed_attempts > 999) {
                        text = "> 999";
                    } else {
                        snprintf(buf, sizeof(buf), "%d", failed_attempts);
                        text = buf;
                    }
                    cairo_set_source_rgb(textctx, 1, 0, 0);
                    cairo_set_font_size(textctx, 32.0);
                }
                break;
        }

        if (text) {
            //cairo_set_source_rgb(textctx, 0, 0, 1);
            //cairo_rectangle(textctx, 0, 0, BAR_WIDTH, TEXT_HEIGHT);
            //cairo_fill(textctx);

            cairo_text_extents_t extents;
            double x, y;

            cairo_text_extents(textctx, text, &extents);
            x = BAR_WIDTH / 2 - ((extents.width / 2) + extents.x_bearing);
            y = TEXT_HEIGHT / 2 - ((extents.height / 2) + extents.y_bearing);

            cairo_move_to(textctx, x, y);
            cairo_show_text(textctx, text);
            cairo_close_path(textctx);
        }
    }

    if (xr_screens > 0) {
        /* Composite the unlock indicator in the middle of each screen. */
        for (int screen = 0; screen < xr_screens; screen++) {
            int barx = ((double)resolution[0] - 1920) / 2 + 815;
            int bary = ((double)resolution[1] - 1080) / 2 + 771;
            cairo_set_source_surface(xcb_ctx, baroutput, barx, bary);
            cairo_rectangle(xcb_ctx, barx, bary, BAR_WIDTH, BAR_HEIGHT);
            cairo_fill(xcb_ctx);
            cairo_set_source_surface(xcb_ctx, textoutput, barx, bary + 100);
            cairo_rectangle(xcb_ctx, barx, bary + 100, BAR_WIDTH, TEXT_HEIGHT);
            cairo_fill(xcb_ctx);
        }
    } else {
        /* We have no information about the screen sizes/positions, so we just
         * place the unlock indicator in the middle of the X root window and
         * hope for the best. */
        int barx = ((double)resolution[0] - 1920) / 2 + 815;
        int bary = ((double)resolution[1] - 1080) / 2 + 771;
        cairo_set_source_surface(xcb_ctx, baroutput, barx, bary);
        cairo_rectangle(xcb_ctx, barx, bary, BAR_WIDTH, BAR_HEIGHT);
        cairo_fill(xcb_ctx);
        cairo_set_source_surface(xcb_ctx, textoutput, barx, bary + 100);
        cairo_rectangle(xcb_ctx, barx, bary + 100, BAR_WIDTH, TEXT_HEIGHT);
        cairo_fill(xcb_ctx);
    }

    cairo_destroy(barctx);
    cairo_destroy(textctx);
    cairo_destroy(xcb_ctx);
    return bg_pixmap;
}

/*
 * Calls draw_image on a new pixmap and swaps that with the current pixmap
 *
 */
void redraw_screen(void) {
    DEBUG("redraw_screen(unlock_state = %d, auth_state = %d)\n", unlock_state, auth_state);
    xcb_pixmap_t bg_pixmap = draw_image(last_resolution);
    xcb_change_window_attributes(conn, win, XCB_CW_BACK_PIXMAP, (uint32_t[1]){bg_pixmap});
    /* XXX: Possible optimization: Only update the area in the middle of the
     * screen instead of the whole screen. */
    xcb_clear_area(conn, 0, win, 0, 0, last_resolution[0], last_resolution[1]);
    xcb_free_pixmap(conn, bg_pixmap);
    xcb_flush(conn);
}

/*
 * Hides the unlock indicator completely when there is no content in the
 * password buffer.
 *
 */
void clear_indicator(void) {
    if (input_position == 0) {
        unlock_state = STATE_STARTED;
        last_3bar_start = 0;
    } else
        unlock_state = STATE_KEY_PRESSED;
    redraw_screen();
}
