#ifndef LINUX_DEVLCD_H
#define LINUX_DEVLCD_H

MODULE_EXPORT int  linuxDevLcd_init (Driver *drvthis);
MODULE_EXPORT void linuxDevLcd_close (Driver *drvthis);
MODULE_EXPORT int  linuxDevLcd_width (Driver *drvthis);
MODULE_EXPORT int  linuxDevLcd_height (Driver *drvthis);
MODULE_EXPORT int  linuxDevLcd_cellwidth (Driver *drvthis);
MODULE_EXPORT int  linuxDevLcd_cellheight (Driver *drvthis);
MODULE_EXPORT void linuxDevLcd_clear (Driver *drvthis);
MODULE_EXPORT void linuxDevLcd_flush (Driver *drvthis);
MODULE_EXPORT void linuxDevLcd_string (Driver *drvthis, int x, int y, const char string[]);
MODULE_EXPORT void linuxDevLcd_chr (Driver *drvthis, int x, int y, char c);
MODULE_EXPORT void linuxDevLcd_vbar(Driver *drvthis, int x, int y, int len, int promille, int options);
MODULE_EXPORT void linuxDevLcd_hbar(Driver *drvthis, int x, int y, int len, int promille, int options);
MODULE_EXPORT void linuxDevLcd_num(Driver *drvthis, int x, int num);
MODULE_EXPORT int  linuxDevLcd_icon(Driver *drvthis, int x, int y, int icon);
MODULE_EXPORT void linuxDevLcd_set_char(Driver *drvthis, int n, unsigned char *dat);
MODULE_EXPORT int  linuxDevLcd_get_free_chars(Driver *drvthis);
MODULE_EXPORT void linuxDevLcd_backlight (Driver *drvthis, int on);
MODULE_EXPORT const char * linuxDevLcd_get_info (Driver *drvthis);

/** number of custom characters */
#define NUM_CCs 8

/**
 *  * One entry of the custom character cache consists of 8 bytes of cache data
 *   * and a clean flag.
 *    */
typedef struct cgram_cache {
	unsigned char cache[LCD_DEFAULT_CELLHEIGHT];
	int clean;
} CGram;


/** private data for the \c text driver */
typedef struct linuxDevLcd_private_data {
	int width;		/**< display width in characters */
	int height;		/**< display height in characters */
	int cellwidth, cellheight;      /**< size a one cell (pixels) */
	CGram cc[NUM_CCs];      /**< the custom character cache */
	CGmode ccmode;          /**< character mode of the current screen */

	/**
	 * lastline controls the use of the last line, if pixel addressable
	 * (true, default) or underline effect (false). To avoid the
	 * underline effect, last line is always zeroed for whatever
	 * redefined character.
	 */
	char lastline;

	unsigned char *framebuf;		/**< fram buffer */
	unsigned char *backingstore;    /**< buffer for incremental updates */

	FILE* fd;		/**< handle to the device */

        /** \name Forced screen updates
         *@{*/
        time_t nextrefresh;     /**< Time when the next refresh is due. */
        int refreshdisplay;     /**< Seconds after which a complete display update is forced. */
        /**@}*/

        /** \name Keepalive
         *@{*/
        time_t nextkeepalive;   /**< Time the next keep-alive is due. */
        int keepalivedisplay;   /**< Refresh upper left char every \c keepalivedisplay seconds. */
        /**@}*/

	int backlight_state;	/**< Cache backlight state */
} PrivateData;


#define DEFAULT_DEVICE	"/dev/lcd"
#define TEXTDRV_DEFAULT_SIZE "20x4"

#endif
