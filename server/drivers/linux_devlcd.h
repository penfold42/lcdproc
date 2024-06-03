#ifndef LINUX_DEVLCD_H
#define LINUX_DEVLCD_H

MODULE_EXPORT int  linuxDevLcd_init (Driver *drvthis);
MODULE_EXPORT void linuxDevLcd_close (Driver *drvthis);
MODULE_EXPORT int  linuxDevLcd_width (Driver *drvthis);
MODULE_EXPORT int  linuxDevLcd_height (Driver *drvthis);
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
MODULE_EXPORT void linuxDevLcd_set_contrast (Driver *drvthis, int promille);
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


#define DEFAULT_DEVICE	"/dev/lcd"
#define TEXTDRV_DEFAULT_SIZE "20x4"

#endif
