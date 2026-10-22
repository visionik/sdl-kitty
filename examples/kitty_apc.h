/* Minimal Kitty graphics APC helpers for atlas demos (not the SDL driver). */
#ifndef KITTY_APC_H
#define KITTY_APC_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

bool kitty_apc_write(int fd, const void *buf, size_t len);

/* Upload RGB24 image data only (a=t). Does not place on screen. */
bool kitty_apc_transmit_rgb(int fd, uint32_t image_id, const uint8_t *rgb, int w, int h);

/*
 * Place a source crop of an existing image at the current cursor.
 * src_x/y/w/h — crop in image pixels (0 = full image for w/h if <=0).
 * place_cols/rows — optional c/r stretch (0 = natural size).
 * off_x/off_y — Kitty X/Y pixel offset inside the starting cell (sub-cell).
 * placement_id — Kitty p= (use a stable id to replace).
 */
bool kitty_apc_place_crop(int fd, uint32_t image_id, uint32_t placement_id,
                          int src_x, int src_y, int src_w, int src_h,
                          int place_cols, int place_rows,
                          int off_x, int off_y);

bool kitty_apc_delete_image(int fd, uint32_t image_id);
bool kitty_apc_delete_placement(int fd, uint32_t image_id, uint32_t placement_id);

/* Move cursor to 1-based cell (col, row). */
bool kitty_apc_cup(int fd, int col, int row);

#endif
