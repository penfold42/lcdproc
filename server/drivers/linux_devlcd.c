/** \file server/drivers/linux_devlcd.c
 * LCDd \c driver for linux kernel /dev/lcd device
 * It displays the LCD screens, one below the other on the terminal,
 */

/* Copyright (C) 2024 The LCDproc Team
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>

#include "lcd_lib.h"
#include "linux_devlcd.h"
#include "shared/report.h"
#include "adv_bignum.h"


/* Vars for the server core */
MODULE_EXPORT char *api_version = API_VERSION;
MODULE_EXPORT int stay_in_foreground = 0;
MODULE_EXPORT int supports_multiple = 0;
MODULE_EXPORT char *symbol_prefix = "linuxDevLcd_";


/**
 * Initialize the driver.
 * \param drvthis  Pointer to driver structure.
 * \retval 0       Success.
 * \retval <0      Error.
 */
MODULE_EXPORT int
linuxDevLcd_init (Driver *drvthis)
{
	PrivateData *p;
	char buf[256];
	char device[200];

	/* Allocate and store private data */
	p = (PrivateData *) calloc(1, sizeof(PrivateData));
	if (p == NULL)
		return -1;
	if (drvthis->store_private_ptr(drvthis, p))
		return -1;

	/* initialize private data */
	p->cellheight = 8;	/* Do not change this !!! This is a 
				 * controller property, not a display
				 * property !!! */
	p->cellwidth = 5;
	p->ccmode = standard;

	/* init to invalid state */
	p->backlight_state = -1;

	/* which device should be used */
	strncpy(device, drvthis->config_get_string(drvthis->name, "Device", 0, DEFAULT_DEVICE),
		sizeof(device));
	device[sizeof(device) - 1] = '\0';

	report(RPT_INFO, "%s: using Device %s",
	       drvthis->name, device);

	p->nextrefresh          = 0;
	p->refreshdisplay       = drvthis->config_get_int(drvthis->name, "refreshdisplay", 0, 0);
	p->nextkeepalive        = 0;
	p->keepalivedisplay     = drvthis->config_get_int(drvthis->name, "keepalivedisplay", 0, 0);

	// Set display sizes
	if ((drvthis->request_display_width() > 0)
	    && (drvthis->request_display_height() > 0)) {
		// Use size from primary driver
		p->width = drvthis->request_display_width();
		p->height = drvthis->request_display_height();
	}
	else {
		/* Use our own size from config file */
		strncpy(buf, drvthis->config_get_string(drvthis->name, "Size", 0, TEXTDRV_DEFAULT_SIZE), sizeof(buf));
		buf[sizeof(buf)-1] = '\0';
		if ((sscanf(buf , "%dx%d", &p->width, &p->height) != 2)
		    || (p->width <= 0) || (p->width > LCD_MAX_WIDTH)
		    || (p->height <= 0) || (p->height > LCD_MAX_HEIGHT)) {
			report(RPT_WARNING, "%s: cannot read Size: %s; using default %s",
					drvthis->name, buf, TEXTDRV_DEFAULT_SIZE);
			sscanf(TEXTDRV_DEFAULT_SIZE, "%dx%d", &p->width, &p->height);
		}
	}

	// Allocate the framebuffer
	p->framebuf = malloc(p->width * p->height);
	if (p->framebuf == NULL) {
		report(RPT_ERR, "%s: unable to create framebuffer", drvthis->name);
		return -1;
	}
	memset(p->framebuf, ' ', p->width * p->height);

	/* Allocate and clear the buffer for incremental updates */
	p->backingstore = (unsigned char *) calloc(p->width * p->height, sizeof(char));
	if (p->backingstore == NULL) {
		report(RPT_ERR, "%s: unable to allocate framebuffer backing store", drvthis->name);
		return -1;
	}

	/* open the device... */
	p->fd = fopen(device, "w" );

	if (p->fd == 0) {
		report(RPT_ERR, "%s: open(%s) failed (%s)", drvthis->name, device, strerror(errno));
		if (errno == EACCES)
			report(RPT_ERR, "%s: device %s could not be opened", drvthis->name, device);
		goto err_out;
	}
	report(RPT_INFO, "%s: opened display on %s", drvthis->name, device);

	fprintf(p->fd, "\e[LI"); // Reinitialise display
	fprintf(p->fd, "\e[Lc"); // Cursor off
	fprintf(p->fd, "\e[Lb"); // Blink off
	fprintf(p->fd, "\e[2J"); // Clear Display
	fprintf(p->fd, "\e[H");  // Move cursor home
	fprintf(p->fd, "\e[LD"); // Display on

	report(RPT_DEBUG, "%s: init() done", drvthis->name);

	return 0;

      err_out:
	linuxDevLcd_close(drvthis);
	return -1;
}


/**
 * Close the driver (do necessary clean-up).
 * \param drvthis  Pointer to driver structure.
 */
MODULE_EXPORT void
linuxDevLcd_close (Driver *drvthis)
{
	PrivateData *p = drvthis->private_data;

	if (p != NULL) {
		if (p->framebuf != NULL)
			free(p->framebuf);

		if (p->backingstore)
			free(p->backingstore);

		free(p);
	}
	drvthis->store_private_ptr(drvthis, NULL);
}


/**
 * Return the display width in characters.
 * \param drvthis  Pointer to driver structure.
 * \return         Number of characters the display is wide.
 */
MODULE_EXPORT int
linuxDevLcd_width (Driver *drvthis)
{
	PrivateData *p = drvthis->private_data;

	return p->width;
}


/**
 * Return the display height in characters.
 * \param drvthis  Pointer to driver structure.
 * \return         Number of characters the display is high.
 */
MODULE_EXPORT int
linuxDevLcd_height (Driver *drvthis)
{
	PrivateData *p = drvthis->private_data;

	return p->height;
}


/**
 * Return the width of a character in pixels.
 * \param drvthis  Pointer to driver structure.
 * \return  Number of pixel columns a character cell is wide.
 */
MODULE_EXPORT int
linuxDevLcd_cellwidth(Driver *drvthis)
{
	PrivateData *p = (PrivateData *) drvthis->private_data;

	return p->cellwidth;
}


/**
 * Return the height of a character in pixels.
 * \param drvthis  Pointer to driver structure.
 * \return  Number of pixel lines a character cell is high.
 */
MODULE_EXPORT int
linuxDevLcd_cellheight(Driver *drvthis)
{
	PrivateData *p = (PrivateData *) drvthis->private_data;

	return p->cellheight;
}


/**
 * Set position (not part of API).
 * \param drvthis  Pointer to driver structure.
 * \param x        X-coordinate to go to.
 * \param y        Y-coordinate to go to.
 */
void
linuxDevLcd_position(Driver *drvthis, int x, int y)
{
	PrivateData *p = drvthis->private_data;
	fprintf(p->fd, "\e[Lx%dy%d;", x, y);
}


/**
 * Clear the screen.
 * \param drvthis  Pointer to driver structure.
 */
MODULE_EXPORT void
linuxDevLcd_clear (Driver *drvthis)
{
	PrivateData *p = drvthis->private_data;

	memset(p->framebuf, ' ', p->width * p->height);
	p->ccmode = standard;
}


/**
 * Flush data on screen to the LCD.
 * \param drvthis  Pointer to driver structure.
 */
MODULE_EXPORT void
linuxDevLcd_flush(Driver *drvthis)
{
	PrivateData *p = (PrivateData *) drvthis->private_data;
	int x, y;
	int i;
	int count;
	char refreshNow = 0;
	char keepaliveNow = 0;
	static char doneFirstRefresh = 0;
	time_t now = time(NULL);

	/* force full refresh of display 1st time round */
	if (!doneFirstRefresh) {
		doneFirstRefresh = 1;
		refreshNow = 1;
	}

	/* force full refresh of display */
	if ((p->refreshdisplay > 0) && (now > p->nextrefresh)) {
		refreshNow = 1;
		p->nextrefresh = now + p->refreshdisplay;
	}
	/* keepalive refresh of display */
	if ((p->keepalivedisplay > 0) && (now > p->nextkeepalive)) {
		keepaliveNow = 1;
		p->nextkeepalive = now + p->keepalivedisplay;
	}

	/*
	 * LCD update algorithm: For each line skip over leading and trailing
	 * identical portions of the line. Then send everything in between.
	 * This will also update unchanged parts in the middle but is still
	 * faster than the old algorithm, especially with devices using the
	 * transmit buffer.
	 */
	count = 0;
	for (y = 0; y < p->height; y++) {
		int drawing;

		/* set pointers to start of the line */
		unsigned char *sp = p->framebuf + (y * p->width);
		unsigned char *sq = p->backingstore + (y * p->width);

		/* set pointers to end of the line */
		unsigned char *ep = sp + (p->width - 1);
		unsigned char *eq = sq + (p->width - 1);

		/* On forced refresh update everything */
		if (refreshNow || keepaliveNow) {
			x = 0;
		}
		else {
			/* find begin and end of differences */
			for (x = 0; (sp <= ep) && (*sp == *sq); sp++, sq++, x++)
			  ;
			for (; (ep >= sp) && (*ep == *eq); ep--, eq--)
			  ;
		}

		/* there are differences, ... */
		if (sp <= ep) {
			for (drawing = 0; sp <= ep; x++, sp++, sq++) {
				if (!drawing) {
					drawing = 1;
					linuxDevLcd_position(drvthis,x,y);
				}
				fputc (*sp, p->fd);
				*sq = *sp;	/* Update backing store */
				count++;
			}
			fflush(p->fd);
		}
	}
	debug(RPT_DEBUG, "linux_devlcd: flushed %d chars", count);

	/* Check which definable chars we need to update */
	count = 0;
	for (i = 0; i < NUM_CCs; i++) {
		if (!p->cc[i].clean) {
			int row;

			/* Tell the HD44780 we will redefine char number i */
			fprintf(p->fd, "\e[LG%0d", i);

			/* Send the subsequent rows */
			for (row = 0; row < p->cellheight; row++) {
				fprintf(p->fd, "%02x", p->cc[i].cache[row]);
			}
			fprintf(p->fd, ";");	/* end of define char */
			fflush(p->fd);
			p->cc[i].clean = 1;	/* mark as clean */
			count++;
		}
	}
	debug(RPT_DEBUG, "%s: flushed %d custom chars", drvthis->name, count);
}


/**
 * Print a string on the screen at position (x,y).
 * The upper-left corner is (1,1), the lower-right corner is (p->width, p->height).
 * \param drvthis  Pointer to driver structure.
 * \param x        Horizontal character position (column).
 * \param y        Vertical character position (row).
 * \param string   String that gets written.
 */
MODULE_EXPORT void
linuxDevLcd_string (Driver *drvthis, int x, int y, const char string[])
{
	PrivateData *p = drvthis->private_data;
	int i;
	char c;

	x--; y--; // Convert 1-based coords to 0-based...

	if ((y < 0) || (y >= p->height))
                return;

	for (i = 0; (string[i] != '\0') && (x < p->width); i++, x++) {
		c = string[i];
		if (c == '\e')	/* replace escapes with space */
			c = ' ';

		if (x >= 0)	// no write left of left border
			p->framebuf[(y * p->width) + x] = c;
	}
}


/**
 * Print a character on the screen at position (x,y).
 * The upper-left corner is (1,1), the lower-right corner is (p->width, p->height).
 * \param drvthis  Pointer to driver structure.
 * \param x        Horizontal character position (column).
 * \param y        Vertical character position (row).
 * \param c        Character that gets written.
 */
MODULE_EXPORT void
linuxDevLcd_chr (Driver *drvthis, int x, int y, char c)
{
	PrivateData *p = drvthis->private_data;

	y--; x--;

	if (c == '\e')	/* replace escapes with space */
		c = ' ';

	if ((x >= 0) && (y >= 0) && (x < p->width) && (y < p->height))
		p->framebuf[(y * p->width) + x] = c;
}


/**
 * Turn the display backlight on or off.
 * linux /dev/lcd driver uses escape sequences to implement this
 * \param drvthis  Pointer to driver structure.
 * \param on       New backlight status.
 */
MODULE_EXPORT void
linuxDevLcd_backlight (Driver *drvthis, int on)
{
	PrivateData *p = drvthis->private_data;

// linux /dev/lcd driver escape codes
//  \E[L+ Back light on
//  \E[L- Back light off
	if (p->backlight_state != on) {
		fprintf(p->fd, "\e[L%c", (on) ? '+' : '-');
		p->backlight_state = on;
	}

	fflush(p->fd);
}


/**
 * Provide some information about this driver.
 * \param drvthis  Pointer to driver structure.
 * \return         Constant string with information.
 */
MODULE_EXPORT const char *
linuxDevLcd_get_info (Driver *drvthis)
{
	//PrivateData *p = drvthis->private_data;
        static char *info_string = "Linux devlcd driver";

	return info_string;
}


/**
 * Get total number of custom characters available.
 * \param drvthis  Pointer to driver structure.
 * \return  Number of custom characters (always NUM_CCs).
 */
MODULE_EXPORT int
linuxDevLcd_get_free_chars(Driver *drvthis)
{
	return NUM_CCs;
}


/**
 * Draw a vertical bar bottom-up.
 * \param drvthis  Pointer to driver structure.
 * \param x        Horizontal character position (column) of the starting point.
 * \param y        Vertical character position (row) of the starting point.
 * \param len      Number of characters that the bar is high at 100%
 * \param promille Current height level of the bar in promille.
 * \param options  Options (currently unused).
 */
MODULE_EXPORT void
linuxDevLcd_vbar(Driver *drvthis, int x, int y, int len, int promille, int options)
{
	PrivateData *p = (PrivateData *) drvthis->private_data;

	if (p->ccmode != vbar) {
		unsigned char vBar[p->cellheight];
		int i;

		if (p->ccmode != standard) {
			/* Not supported(yet) */
			report(RPT_WARNING, "%s: vbar: cannot combine two modes using user-defined characters",
				drvthis->name);
			return;
		}
		p->ccmode = vbar;

		memset(vBar, 0x00, sizeof(vBar));

		for (i = 1; i < p->cellheight; i++) {
			/* add pixel line per pixel line ... */
			vBar[p->cellheight - i] = 0xFF;
			linuxDevLcd_set_char(drvthis, i, vBar);
		}
	}

	lib_vbar_static(drvthis, x, y, len, promille, options, p->cellheight, 0);
}


/**
 * Draw a horizontal bar to the right.
 * \param drvthis  Pointer to driver structure.
 * \param x        Horizontal character position (column) of the starting point.
 * \param y        Vertical character position (row) of the starting point.
 * \param len      Number of characters that the bar is long at 100%
 * \param promille Current length level of the bar in promille.
 * \param options  Options (currently unused).
 */
MODULE_EXPORT void
linuxDevLcd_hbar(Driver *drvthis, int x, int y, int len, int promille, int options)
{
	PrivateData *p = (PrivateData *) drvthis->private_data;

	if (p->ccmode != hbar) {
		unsigned char hBar[p->cellheight];
		int i;

		if (p->ccmode != standard) {
			/* Not supported(yet) */
			report(RPT_WARNING, "%s: hbar: cannot combine two modes using user-defined characters",
			      drvthis->name);
			return;
		}

		p->ccmode = hbar;

		for (i = 1; i <= p->cellwidth; i++) {
			/* fill pixel columns from left to right. */
			memset(hBar, 0xFF & ~((1 << (p->cellwidth - i)) - 1), sizeof(hBar));
			linuxDevLcd_set_char(drvthis, i, hBar);
		}
	}

	lib_hbar_static(drvthis, x, y, len, promille, options, p->cellwidth, 0);
}


/**
 * Write a big number to the screen.
 * \param drvthis  Pointer to driver structure.
 * \param x        Horizontal character position (column).
 * \param num      Character to write (0 - 10 with 10 representing ':')
 */
MODULE_EXPORT void
linuxDevLcd_num(Driver *drvthis, int x, int num)
{
	PrivateData *p = (PrivateData *) drvthis->private_data;
	int do_init = 0;

	if ((num < 0) || (num > 10))
		return;

	if (p->ccmode != bignum) {
		if (p->ccmode != standard) {
			/* Not supported (yet) */
			report(RPT_WARNING, "%s: num: cannot combine two modes using user-defined characters",
					drvthis->name);
			return;
		}

		p->ccmode = bignum;

		do_init = 1;
	}

	/* Lib_adv_bignum does everything needed to show the bignumbers. */
	lib_adv_bignum(drvthis, x, num, 0, do_init);
}


/**
 * Define a custom character and write it to the LCD.
 * \param drvthis  Pointer to driver structure.
 * \param n        Custom character to define [0 - (NUM_CCs-1)].
 * \param dat      Array of 8 (=cellheight) bytes, each representing a pixel row
 *                 starting from the top to bottom.
 *                 The bits in each byte represent the pixels where the LSB
 *                 (least significant bit) is the rightmost pixel in each pixel row.
 */
MODULE_EXPORT void
linuxDevLcd_set_char(Driver *drvthis, int n, unsigned char *dat)
{
	PrivateData *p = (PrivateData *) drvthis->private_data;
	unsigned char mask = (1 << p->cellwidth) - 1;
	int row;

	if ((n < 0) || (n >= NUM_CCs))
		return;
	if (!dat)
		return;

	for (row = 0; row < p->cellheight; row++) {
		int letter = 0;

		if (p->lastline || (row < p->cellheight - 1))
			letter = dat[row] & mask;

		if (p->cc[n].cache[row] != letter)
			p->cc[n].clean = 0;	/* only mark dirty if really different */
		p->cc[n].cache[row] = letter;
	}
}


/**
 * Place an icon on the screen.
 * \param drvthis  Pointer to driver structure.
 * \param x        Horizontal character position (column).
 * \param y        Vertical character position (row).
 * \param icon     synbolic value representing the icon.
 * \retval 0       Icon has been successfully defined/written.
 * \retval <0      Server core shall define/write the icon.
 */
MODULE_EXPORT int
linuxDevLcd_icon(Driver *drvthis, int x, int y, int icon)
{
	PrivateData *p = (PrivateData *) drvthis->private_data;

	static unsigned char heart_open[] =
		{ b__XXXXX,
		  b__X_X_X,
		  b_______,
		  b_______,
		  b_______,
		  b__X___X,
		  b__XX_XX,
		  b__XXXXX };
	static unsigned char heart_filled[] =
		{ b__XXXXX,
		  b__X_X_X,
		  b___X_X_,
		  b___XXX_,
		  b___XXX_,
		  b__X_X_X,
		  b__XX_XX,
		  b__XXXXX };
	static unsigned char arrow_up[] =
		{ b____X__,
		  b___XXX_,
		  b__X_X_X,
		  b____X__,
		  b____X__,
		  b____X__,
		  b____X__,
		  b_______ };
	static unsigned char arrow_down[] =
		{ b____X__,
		  b____X__,
		  b____X__,
		  b____X__,
		  b__X_X_X,
		  b___XXX_,
		  b____X__,
		  b_______ };
	static unsigned char checkbox_off[] =
		{ b_______,
		  b_______,
		  b__XXXXX,
		  b__X___X,
		  b__X___X,
		  b__X___X,
		  b__XXXXX,
		  b_______ };
	static unsigned char checkbox_on[] =
		{ b____X__,
		  b____X__,
		  b__XXX_X,
		  b__X_XX_,
		  b__X_X_X,
		  b__X___X,
		  b__XXXXX,
		  b_______ };
	static unsigned char checkbox_gray[] =
		{ b_______,
		  b_______,
		  b__XXXXX,
		  b__X_X_X,
		  b__XX_XX,
		  b__X_X_X,
		  b__XXXXX,
		  b_______ };
	static unsigned char block_filled[] =
		{ b__XXXXX,
		  b__XXXXX,
		  b__XXXXX,
		  b__XXXXX,
		  b__XXXXX,
		  b__XXXXX,
		  b__XXXXX,
		  b__XXXXX };

	/* Icons from CGROM will always work */
	switch (icon) {
	    case ICON_ARROW_LEFT:
		linuxDevLcd_chr(drvthis, x, y, 0x1B);
		return 0;
	    case ICON_ARROW_RIGHT:
		linuxDevLcd_chr(drvthis, x, y, 0x1A);
		return 0;
	}

	/* The full block works except if ccmode=bignum */
	if (icon == ICON_BLOCK_FILLED) {
		if (p->ccmode != bignum) {
			linuxDevLcd_set_char(drvthis, 0, block_filled);
			linuxDevLcd_chr(drvthis, x, y, 0);
			return 0;
		}
		else {
			return -1;
		}
	}

	/* The heartbeat icons do not work in bignum and vbar mode */
	if ((icon == ICON_HEART_FILLED) || (icon == ICON_HEART_OPEN)) {
		if ((p->ccmode != bignum) && (p->ccmode != vbar)) {
			switch (icon) {
			    case ICON_HEART_FILLED:
				linuxDevLcd_set_char(drvthis, 7, heart_filled);
				linuxDevLcd_chr(drvthis, x, y, 7);
				return 0;
			    case ICON_HEART_OPEN:
				linuxDevLcd_set_char(drvthis, 7, heart_open);
				linuxDevLcd_chr(drvthis, x, y, 7);
				return 0;
			}
		}
		else {
			return -1;
		}
	}

	/* All other icons work only in the standard or icon ccmode */
	if (p->ccmode != icons) {
		if (p->ccmode != standard) {
			/* Not supported (yet) */
			report(RPT_WARNING, "%s: num: cannot combine two modes using user-defined characters",
					drvthis->name);
			return -1;
		}
		p->ccmode = icons;
	}

	switch (icon) {
		case ICON_ARROW_UP:
			linuxDevLcd_set_char(drvthis, 1, arrow_up);
			linuxDevLcd_chr(drvthis, x, y, 1);
			break;
		case ICON_ARROW_DOWN:
			linuxDevLcd_set_char(drvthis, 2, arrow_down);
			linuxDevLcd_chr(drvthis, x, y, 2);
			break;
		case ICON_CHECKBOX_OFF:
			linuxDevLcd_set_char(drvthis, 3, checkbox_off);
			linuxDevLcd_chr(drvthis, x, y, 3);
			break;
		case ICON_CHECKBOX_ON:
			linuxDevLcd_set_char(drvthis, 4, checkbox_on);
			linuxDevLcd_chr(drvthis, x, y, 4);
			break;
		case ICON_CHECKBOX_GRAY:
			linuxDevLcd_set_char(drvthis, 5, checkbox_gray);
			linuxDevLcd_chr(drvthis, x, y, 5);
			break;
		default:
			return -1;	/* Let the core do other icons */
	}
	return 0;
}


