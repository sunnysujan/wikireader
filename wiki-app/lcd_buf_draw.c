/*
 * Copyright (c) 2009 Openmoko Inc.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#ifdef WIKIPCF
#include <wchar.h>
#include <malloc.h>
#include <fcntl.h>
#include <unistd.h>
#else
#include <file-io.h>
#include <guilib.h>
#include <malloc-simple.h>
#include <msg.h>
#include <lcd.h>
#include <input.h>
#include "delay.h"
#include <tick.h>
#include "history.h"
#include "search.h"
#include "glyph.h"
#include "wikilib.h"
#include "restricted.h"
#endif

#include "bmf.h"
#include "lcd_buf_draw.h"
#include "search.h"
#include "bigram.h"

#define MAX_SCROLL_SECONDS 3
#define SCROLL_SPEED_FRICTION 0.9
#define SCROLL_UNIT_SECOND 0.1
#define LINK_ACTIVATION_TIME_THRESHOLD 0.1
#define LINK_INVERT_ACTIVATION_TIME_THRESHOLD 0.25

extern long finger_move_speed;
extern int last_display_mode;
extern int display_mode;
pcffont_bmf_t pcfFonts[FONT_COUNT];
static int lcd_draw_buf_inited = 0;
LCD_DRAW_BUF lcd_draw_buf;
unsigned char * file_buffer;
int restricted_article = 0;
int lcd_draw_buf_pos  = 0;
int lcd_draw_cur_y_pos = 0;
int lcd_draw_init_y_pos = 0;

int request_display_next_page = 0;
int request_y_pos = 0;
int cur_render_y_pos = 0;

ARTICLE_LINK articleLink[MAX_ARTICLE_LINKS];

#ifndef WIKIPCF
int link_to_be_activated = -1;
unsigned long link_to_be_activated_start_time = 0;
int link_to_be_inverted = -1;
unsigned long link_to_be_inverted_start_time = 0;
int link_currently_activated = -1;
int link_currently_inverted = -1;
#endif

int article_link_count;
int display_first_page = 0;

void lcd_set_pixel(unsigned char *membuffer,int x, int y);

void display_link_article(long idx_article);
void drawline_in_framebuffer_copy(unsigned char *buffer,int start_x,int start_y,int end_x,int end_y);
void buf_draw_char_external(LCD_DRAW_BUF *lcd_draw_buf_external,ucs4_t u,int start_x,int end_x,int start_y,int end_y);
int get_external_str_pixel_width(char **pUTF8);
void repaint_framebuffer(unsigned char *buf,int pos, int b_repaint_invert_link);
void repaint_invert_link(void);
char* FontFile(int idx);
void msg_info(char *data);
int framebuffer_size();
int framebuffer_width();
int framebuffer_height();

unsigned char *framebuffer_copy;
char msg_out[1024];
extern unsigned char *framebuffer;
long article_scroll_increment;
unsigned long time_scroll_article_last=0;
int stop_render_article = 0;
int b_show_scroll_bar = 0;
long saved_idx_article;

#define MIN_BAR_LEN 20
void show_scroll_bar(int bShow)
{
	int bar_len;
	int bar_pos;
	int i;
	int byte_idx;
	char c;
	static char frame_bytes[LCD_HEIGHT_LINES];
	static int b_frame_bytes;

	if (lcd_draw_buf.current_y < LCD_HEIGHT_LINES)
		return;
	if (bShow <= 0)
	{
		if (b_frame_bytes)
		{
			if (!bShow && finger_move_speed == 0)
			{
				#ifdef INCLUDED_FROM_KERNEL
				//delay_us(10000);
				#endif
			}
			for (i = 0; i < LCD_HEIGHT_LINES; i++)
			{
				byte_idx = (236 + LCD_VRAM_WIDTH_PIXELS * i) / 8;
				framebuffer[byte_idx] = frame_bytes[i];
			}
			b_frame_bytes = 0;
		}
	}
	else
	{
		bar_len = LCD_HEIGHT_LINES * LCD_HEIGHT_LINES / lcd_draw_buf.current_y;
		if (bar_len > LCD_HEIGHT_LINES)
			bar_len = LCD_HEIGHT_LINES;
		else if (bar_len < MIN_BAR_LEN)
			bar_len = MIN_BAR_LEN;
		if (lcd_draw_buf.current_y > LCD_HEIGHT_LINES)
			bar_pos = (LCD_HEIGHT_LINES - bar_len) * lcd_draw_cur_y_pos / (lcd_draw_buf.current_y - LCD_HEIGHT_LINES);
		else
			bar_pos = 0;
		if (bar_pos < 0)
			bar_pos = 0;
		else if (bar_pos + bar_len > LCD_HEIGHT_LINES)
			bar_pos = LCD_HEIGHT_LINES - bar_len;

		for (i = 0; i < LCD_HEIGHT_LINES; i++)
		{
			if (bar_pos <= i && i < bar_pos + bar_len)
				c = 0x07;
			else
				c = 0;
			byte_idx = (236 + LCD_VRAM_WIDTH_PIXELS * i) / 8;
			frame_bytes[i] = framebuffer[byte_idx];
			framebuffer[byte_idx] = (framebuffer[byte_idx] & 0xF0) | c;
		}
		b_frame_bytes = 1;
	}
}

void init_lcd_draw_buf()
{
	int i,framebuffersize,fd;

	if (!lcd_draw_buf_inited)
	{
		framebuffersize = framebuffer_size();
		framebuffer_copy = (unsigned char*)Xalloc(framebuffersize);

		lcd_draw_buf.screen_buf = (unsigned char *)Xalloc(LCD_BUF_WIDTH_BYTES * LCD_BUF_HEIGHT_PIXELS);

		for (i=0; i < FONT_COUNT; i++)
		{
			pcfFonts[i].file = FontFile(i);
			if (!pcfFonts[i].file[0])
			{
				pcfFonts[i].fd = -1;
			}
			else
			{
				fd = load_bmf(&pcfFonts[i]);
				if(fd >= 0)
				{
					if (i == ITALIC_FONT_IDX - 1)
					{
						pcfFonts[i].bPartialFont = 1;
						pcfFonts[i].supplement_font = &pcfFonts[DEFAULT_ALL_FONT_IDX - 1];
					}
					else if (i == DEFAULT_FONT_IDX - 1)
					{
						pcfFonts[i].bPartialFont = 1;
						pcfFonts[i].supplement_font = &pcfFonts[DEFAULT_ALL_FONT_IDX - 1];
					}
					else
					{
						pcfFonts[i].bPartialFont = 0;
					}
					pcfFonts[i].fd = fd;
				}
			}

		}
		lcd_draw_buf_inited = 1;
	}
	lcd_draw_buf.current_x = 0;
	lcd_draw_buf.current_y = 0;
	lcd_draw_buf.drawing = 0;
	lcd_draw_buf.pPcfFont = NULL;
	lcd_draw_buf.line_height = 0;
	lcd_draw_buf.align_adjustment = 0;


	if (lcd_draw_buf.screen_buf)
		memset(lcd_draw_buf.screen_buf, 0, LCD_BUF_WIDTH_BYTES * LCD_BUF_HEIGHT_PIXELS);
}

void draw_string(unsigned char *s)
{
	//ucs4_t u;
	unsigned char **p = &s;

	buf_draw_UTF8_str(p);
	//while (**p && (u = UTF8_to_UCS4(p)))
	//{
	//	buf_draw_char(u);
	//}
}

void init_file_buffer()
{
	file_buffer = (unsigned char*)Xalloc(FILE_BUFFER_SIZE);
}

// default font is indexed 0, font id 0 is indexed 1, etc.
char* FontFile(int idx) {
	switch(idx)
	{
		case ITALIC_FONT_IDX - 1:
			return FONT_FILE_ITALIC;
			break;
		case DEFAULT_FONT_IDX - 1:
			return FONT_FILE_DEFAULT;
			break;
		case DEFAULT_ALL_FONT_IDX - 1:
			return FONT_FILE_DEFAULT_ALL;
			break;
		case TITLE_FONT_IDX - 1:
			return FONT_FILE_TITLE;
			break;
		case SUBTITLE_FONT_IDX - 1:
			return FONT_FILE_SUBTITLE;
			break;
		default:
			return "";
			break;
	}
}

ucs4_t UTF8_to_UCS4(unsigned char **pUTF8)
{
	ucs4_t c0, c1, c2, c3;

	/* if 0 returned, it is not a invalid UTF8 character.  The pointer moves to the second byte. */
	c0 = 0;
	if (**pUTF8)
	{
		c0 = (ucs4_t)**pUTF8;
		(*pUTF8)++;
		if (c0 & 0x80) /* multi-byte UTF8 char */
		{
			if ((c0 & 0xE0) == 0xC0) /* 2-byte UTF8 */
			{
				c1 = **pUTF8;
				if ((c1 & 0xC0) == 0x80)
				{
					(*pUTF8)++;
					c0 = ((c0 & 0x1F) << 6) + (c1 & 0x3F);
				}
				else
					c0 = 0; /* invalid UTF8 character */
			}
			else if ((c0 & 0xF0) == 0xE0) /* 3-byte UTF8 */
			{
				c1 = **pUTF8;
				c2 = *(*pUTF8 + 1);
				if ((c1 & 0xC0) == 0x80 && (c2 & 0xC0) == 0x80)
				{
					(*pUTF8) += 2;
					c0 = ((c0 & 0x0F) << 12) + ((c1 & 0x3F) << 6) + (c2 & 0x3F);
				}
				else
					c0 = 0; /* invalid UTF8 character */
			}
			else if ((c0 & 0xF1) == 0xF0) /* 4-byte UTF8 */
			{
				c1 = **pUTF8;
				c2 = *(*pUTF8 + 1);
				c3 = *(*pUTF8 + 2);
				if ((c1 & 0xC0) == 0x80 && (c2 & 0xC0) == 0x80 && (c3 & 0xC0) == 0x80)
				{
					(*pUTF8) += 3;
					c0 = ((c0 & 0x07) << 18) + ((c1 & 0x3F) << 12) + ((c2 & 0x3F) << 6) + (c3 & 0x3F) ;
				}
				else
					c0 = 0; /* invalid UTF8 character */
			}
			else
					c0 = 0; /* invalid UTF8 character */
		}
	}
	return c0;
}

void UCS4_to_UTF8(ucs4_t u, unsigned char *sUTF8)
{
	if (u < 0x80)
	{
		sUTF8[0] = (unsigned char)u;
		sUTF8[1] = '\0';
	}
	else if (u < 0x800)
	{
		sUTF8[0] = (unsigned char)(0xC0 | (u >> 6));
		sUTF8[1] = (unsigned char)(0x80 | (u & 0x3F));
		sUTF8[2] = '\0';
	}
	else if (u < 0x10000)
	{
		sUTF8[0] = (unsigned char)(0xC0 | (u >> 12));
		sUTF8[1] = (unsigned char)(0x80 | ((u & 0xFFF) >> 6));
		sUTF8[2] = (unsigned char)(0x80 | (u & 0x3F));
		sUTF8[3] = '\0';
	}
	else if (u < 0x110000)
	{
		sUTF8[0] = (unsigned char)(0xC0 | (u >> 18));
		sUTF8[1] = (unsigned char)(0x80 | ((u & 0x3FFFF) >> 12));
		sUTF8[2] = (unsigned char)(0x80 | ((u & 0xFFF) >> 6));
		sUTF8[3] = (unsigned char)(0x80 | (u & 0x3F));
		sUTF8[4] = '\0';
	}
	else
	{
		sUTF8[0] = '\0';
	}
}

void buf_draw_UTF8_str_in_copy_buffer(char *framebuffer_copy,char **pUTF8,int start_x,int end_x,int start_y,int end_y,int offset_x)
{
	ucs4_t u;
	LCD_DRAW_BUF lcd_draw_buf_external;

	lcd_draw_buf_external.current_x = start_x+offset_x;
	lcd_draw_buf_external.current_y = start_y+2;
	lcd_draw_buf_external.pPcfFont = &pcfFonts[DEFAULT_FONT_IDX - 1];
	lcd_draw_buf_external.screen_buf = (unsigned char*)framebuffer_copy;
	lcd_draw_buf_external.line_height = pcfFonts[DEFAULT_FONT_IDX - 1].Fmetrics.linespace + LINE_SPACE_ADDON;

	lcd_draw_buf_external.align_adjustment = 0;

	while (**pUTF8 > MAX_ESC_CHAR)
	{
		if ((u = UTF8_to_UCS4((unsigned char**)pUTF8)))
		{
			buf_draw_char_external(&lcd_draw_buf_external,u,start_x,end_x,start_y,end_y);
		}

	}
}

void buf_draw_UTF8_str(unsigned char **pUTF8)
{
#ifndef WIKIPCF
	unsigned char c, c2;
	char c3;
	long v_line_bottom;
	ucs4_t u;
	int font_idx;

	c = **pUTF8;
	if (c <= MAX_ESC_CHAR)
	{
		(*pUTF8)++;
		switch(c)
		{
			case ESC_0_SPACE_LINE: /* space line */
				c2 = **pUTF8;
				(*pUTF8)++;
				lcd_draw_buf.current_x = 0;
				lcd_draw_buf.current_y += lcd_draw_buf.line_height;
				lcd_draw_buf.line_height = c2;
				lcd_draw_buf.align_adjustment = 0;
				if (lcd_draw_buf.current_y + lcd_draw_buf.line_height >= LCD_BUF_HEIGHT_PIXELS)
					lcd_draw_buf.current_y = LCD_BUF_HEIGHT_PIXELS - lcd_draw_buf.line_height - 1;
				break;
			case ESC_1_NEW_LINE_DEFAULT_FONT: /* new line with default font and line space */
				lcd_draw_buf.current_x = 0;
				lcd_draw_buf.current_y += lcd_draw_buf.line_height;
				lcd_draw_buf.pPcfFont = &pcfFonts[DEFAULT_FONT_IDX - 1];
				lcd_draw_buf.line_height = pcfFonts[DEFAULT_FONT_IDX - 1].Fmetrics.linespace + LINE_SPACE_ADDON;
				lcd_draw_buf.align_adjustment = 0;
				if (lcd_draw_buf.current_y + lcd_draw_buf.line_height >= LCD_BUF_HEIGHT_PIXELS)
					lcd_draw_buf.current_y = LCD_BUF_HEIGHT_PIXELS - lcd_draw_buf.line_height - 1;
				break;
			case ESC_2_NEW_LINE_SAME_FONT: /* new line with previous font and line space */
				lcd_draw_buf.current_x = 0;
				lcd_draw_buf.current_y += lcd_draw_buf.line_height;
				lcd_draw_buf.align_adjustment = 0;
				if (lcd_draw_buf.current_y + lcd_draw_buf.line_height >= LCD_BUF_HEIGHT_PIXELS)
					lcd_draw_buf.current_y = LCD_BUF_HEIGHT_PIXELS - lcd_draw_buf.line_height - 1;
				break;
			case ESC_3_NEW_LINE_WITH_FONT: /* new line with specified font and line space */
				c2 = **pUTF8;
				(*pUTF8)++;
				lcd_draw_buf.current_x = 0;
				lcd_draw_buf.current_y += lcd_draw_buf.line_height;
				font_idx = c2 & 0x07;
				if (font_idx > FONT_COUNT)
					font_idx = DEFAULT_FONT_IDX;
				lcd_draw_buf.pPcfFont = &pcfFonts[font_idx - 1];
				lcd_draw_buf.line_height = c2 >> 3;
				lcd_draw_buf.align_adjustment = 0;
				if (lcd_draw_buf.current_y + lcd_draw_buf.line_height >= LCD_BUF_HEIGHT_PIXELS)
					lcd_draw_buf.current_y = LCD_BUF_HEIGHT_PIXELS - lcd_draw_buf.line_height - 1;
				break;
			case ESC_4_CHANGE_FONT: /* change font */
				c2 = **pUTF8;
				(*pUTF8)++;
				font_idx = c2 & 0x07;
				if (font_idx > FONT_COUNT)
					font_idx = DEFAULT_FONT_IDX;
				lcd_draw_buf.pPcfFont = &pcfFonts[font_idx - 1];
				lcd_draw_buf.align_adjustment = ((signed char)c2 >> 3);
				if (lcd_draw_buf.current_y + lcd_draw_buf.line_height + lcd_draw_buf.align_adjustment >= LCD_BUF_HEIGHT_PIXELS)
					lcd_draw_buf.align_adjustment = LCD_BUF_HEIGHT_PIXELS - lcd_draw_buf.line_height - lcd_draw_buf.current_y - 1;
				if (lcd_draw_buf.current_y + lcd_draw_buf.align_adjustment < lcd_draw_buf.line_height - 1)
					lcd_draw_buf.align_adjustment = lcd_draw_buf.line_height - lcd_draw_buf.current_y - 1;
				break;
			case ESC_5_RESET_TO_DEFAULT_FONT: /* reset to the default font */
				lcd_draw_buf.pPcfFont = &pcfFonts[DEFAULT_FONT_IDX - 1];
				break;
			case ESC_6_RESET_TO_DEFAULT_ALIGN: /* reset to the default vertical alignment */
				lcd_draw_buf.vertical_adjustment = 0;
				break;
			case ESC_7_FORWARD: /* forward */
				c2 = **pUTF8;
				(*pUTF8)++;
				lcd_draw_buf.current_x += c2;
				if (lcd_draw_buf.current_x > LCD_BUF_WIDTH_PIXELS - LCD_LEFT_MARGIN - lcd_draw_buf.vertical_adjustment)
					lcd_draw_buf.current_x = LCD_BUF_WIDTH_PIXELS - LCD_LEFT_MARGIN - lcd_draw_buf.vertical_adjustment;
				break;
			case ESC_8_BACKWARD: /* backward */
				c2 = **pUTF8;
				(*pUTF8)++;
				if (lcd_draw_buf.current_x < c2)
					lcd_draw_buf.current_x = 0;
				else
					lcd_draw_buf.current_x -= c2;
				break;
			case ESC_9_ALIGN_ADJUSTMENT: /* vertical alignment adjustment */
				c3 = **pUTF8;
				(*pUTF8)++;
				lcd_draw_buf.vertical_adjustment += c3;
				//if (lcd_draw_buf.current_y + lcd_draw_buf.line_height + lcd_draw_buf.align_adjustment >= LCD_BUF_HEIGHT_PIXELS)
				//	lcd_draw_buf.align_adjustment = LCD_BUF_HEIGHT_PIXELS - lcd_draw_buf.line_height - lcd_draw_buf.current_y - 1;
				//if (lcd_draw_buf.current_y + lcd_draw_buf.align_adjustment < lcd_draw_buf.line_height - 1)
				//	lcd_draw_buf.align_adjustment = lcd_draw_buf.line_height - lcd_draw_buf.current_y - 1;
				break;
			case ESC_10_HORIZONTAL_LINE: /* drawing horizontal line */
				c2 = **pUTF8;
				(*pUTF8)++;
				if ((long)c2 > lcd_draw_buf.current_x)
					c2 = (unsigned char)lcd_draw_buf.current_x;
				buf_draw_horizontal_line(lcd_draw_buf.current_x - (unsigned long)c2 + LCD_LEFT_MARGIN + lcd_draw_buf.vertical_adjustment,
					lcd_draw_buf.current_x + LCD_LEFT_MARGIN + lcd_draw_buf.vertical_adjustment);
				break;
			case ESC_11_VERTICAL_LINE: /* drawing vertical line */
				c2 = **pUTF8;
				(*pUTF8)++;
				v_line_bottom = lcd_draw_buf.current_y + lcd_draw_buf.line_height;
				v_line_bottom -= lcd_draw_buf.align_adjustment;
				if (v_line_bottom < 0)
					v_line_bottom = 0;
				if ((long)c2 > v_line_bottom)
					c2 = (unsigned char)v_line_bottom;
				buf_draw_vertical_line(v_line_bottom - (unsigned long)c2, v_line_bottom - 1);
				break;
			case ESC_12_FULL_HORIZONTAL_LINE: /* drawing horizontal line from left-most pixel to right-most pixel */
				lcd_draw_buf.current_x = 0;
				lcd_draw_buf.current_y += lcd_draw_buf.line_height;
				lcd_draw_buf.line_height = 1;
				lcd_draw_buf.align_adjustment = 0;
				buf_draw_horizontal_line(LCD_LEFT_MARGIN + lcd_draw_buf.vertical_adjustment, LCD_BUF_WIDTH_PIXELS);
				break;
			case ESC_13_FULL_VERTICAL_LINE: /* drawing vertical line from top of the line to the bottom */
				lcd_draw_buf.current_x += 1;
				buf_draw_vertical_line(lcd_draw_buf.current_y, lcd_draw_buf.current_y + lcd_draw_buf.line_height - 1);
				lcd_draw_buf.current_x += 2;
				break;
			default:
				break;
		}
	}

	while (**pUTF8 > MAX_ESC_CHAR) /* stop at end of string or escape character */
	{
		if ((u = UTF8_to_UCS4(pUTF8)))
		{
			buf_draw_char(u);
			if(display_first_page==0 && lcd_draw_buf.current_y>LCD_HEIGHT_LINES)
			{
				if (restricted_article)
				{
					draw_restricted_mark(lcd_draw_buf.screen_buf);
				}
				repaint_framebuffer(lcd_draw_buf.screen_buf,0, 0);
				display_first_page = 1;
				lcd_draw_cur_y_pos = 0;
				finger_move_speed = 0;
				if (lcd_draw_init_y_pos > 0)
				{
					display_article_with_pcf(lcd_draw_init_y_pos);
				}
			}
		}
	}
#endif
}

void repaint_framebuffer(unsigned char *buf,int pos, int b_repaint_invert_link)
{
#ifndef WIKIPCF
	int framebuffersize;
	framebuffersize = framebuffer_size();

	guilib_fb_lock();
	guilib_clear();

	memcpy(framebuffer,buf+pos,framebuffersize);
	if (b_repaint_invert_link)
		repaint_invert_link();
	if (b_show_scroll_bar)
		show_scroll_bar(1);
	guilib_fb_unlock();
#endif
}

void buf_draw_horizontal_line(unsigned long start_x, unsigned long end_x)
{
	int i;
	long h_line_y;


	h_line_y = lcd_draw_buf.current_y + lcd_draw_buf.line_height;
	h_line_y -= lcd_draw_buf.align_adjustment + 1;


	for(i = start_x;i<end_x;i++)
	{
	    lcd_set_pixel(lcd_draw_buf.screen_buf,i, h_line_y);
	}

}

void buf_draw_vertical_line(unsigned long start_y, unsigned long end_y)
{
	unsigned long idx_in_byte;
	unsigned char *p;

	if (lcd_draw_buf.current_x + LCD_LEFT_MARGIN + lcd_draw_buf.vertical_adjustment < LCD_BUF_WIDTH_PIXELS)
	{
		idx_in_byte = 7 - ((lcd_draw_buf.current_x + LCD_LEFT_MARGIN + lcd_draw_buf.vertical_adjustment) & 0x07);
		p = lcd_draw_buf.screen_buf + start_y * LCD_BUF_WIDTH_BYTES + ((lcd_draw_buf.current_x + LCD_LEFT_MARGIN + lcd_draw_buf.vertical_adjustment)>> 3);
		while (start_y <= end_y)
		{
			*p |= 1 << idx_in_byte;
			start_y++;
			p += LCD_BUF_WIDTH_BYTES;
		}
	}
}

void lcd_set_pixel(unsigned char *membuffer,int x, int y)
{
	unsigned int byte = (x + LCD_VRAM_WIDTH_PIXELS * y) / 8;
	unsigned int bit  = (x + LCD_VRAM_WIDTH_PIXELS * y) % 8;


	membuffer[byte] |= (1 << (7 - bit));
}

void lcd_set_framebuffer_pixel(int x, int y)
{
	unsigned int byte = (x + LCD_VRAM_WIDTH_PIXELS * y) / 8;
	unsigned int bit  = (x + LCD_VRAM_WIDTH_PIXELS * y) % 8;


	framebuffer[byte] |= (1 << (7 - bit));
}

void lcd_clear_framebuffer_pixel(int x, int y)
{
	unsigned int byte = (x + LCD_VRAM_WIDTH_PIXELS * y) / 8;
	unsigned int bit  = (x + LCD_VRAM_WIDTH_PIXELS * y) % 8;


	framebuffer[byte] ^= (1 << (7 - bit));
}

void lcd_set_framebuffer_byte(char c, int x, int y)
{
	unsigned int byte = (x + LCD_VRAM_WIDTH_PIXELS * y) / 8;

	framebuffer[byte] = c;
}

char lcd_get_framebuffer_byte(int x, int y)
{
	unsigned int byte = (x + LCD_VRAM_WIDTH_PIXELS * y) / 8;

	return framebuffer[byte];
}

void lcd_clear_pixel(unsigned char *membuffer,int x, int y)
{
	unsigned int byte = (x + LCD_VRAM_WIDTH_PIXELS * y) / 8;
	unsigned int bit  = (x + LCD_VRAM_WIDTH_PIXELS * y) % 8;

	membuffer[byte] &= ~(1 << (7 - bit));
}
void buf_draw_char(ucs4_t u)
{
	bmf_bm_t *bitmap;
	charmetric_bmf Cmetrics;
	int bytes_to_process;
	int x_base;
	int y_base;
	int x_offset;
	int y_offset;
	int x_bit_idx;
	int i; // bitmap byte index
	int j; // bitmap bit index
	unsigned char *p; // pointer to lcd draw buffer



	if(pres_bmfbm(u, lcd_draw_buf.pPcfFont, &bitmap, &Cmetrics)<0)
	{
	  return;
	}
	if(u==32)
	{
	   lcd_draw_buf.current_x += Cmetrics.LSBearing + Cmetrics.width + 1;
	   return;
	}

	if (bitmap == NULL)
	    return;

	bytes_to_process = Cmetrics.widthBytes * Cmetrics.height;

	x_base = lcd_draw_buf.current_x + Cmetrics.LSBearing + LCD_LEFT_MARGIN + lcd_draw_buf.vertical_adjustment;
	if (x_base + Cmetrics.LSBearing + Cmetrics.width < LCD_BUF_WIDTH_PIXELS)
	{ // only draw the chracter if not exceeding the LCD width
		y_base = lcd_draw_buf.current_y + lcd_draw_buf.align_adjustment;
		x_offset = 0;
		y_offset = lcd_draw_buf.line_height - (lcd_draw_buf.pPcfFont->Fmetrics.descent + Cmetrics.ascent);

		x_bit_idx = x_base & 0x07;
		//p = lcd_draw_buf.screen_buf + (y_base + y_offset) * LCD_BUF_WIDTH_BYTES + (x_base >> 3);

		for (i = 0; i < bytes_to_process; i++)
		{
			j = 7;
			while (j >= 0)
			{
				if (x_offset >= Cmetrics.widthBits)
				{
					x_offset = 0;
					y_offset++;
					p = lcd_draw_buf.screen_buf + (y_base + y_offset) * LCD_BUF_WIDTH_BYTES + x_base;
				}
				if (x_offset < Cmetrics.width)
				{
					if (bitmap[i] & (1 << j))
					{
					    //*p |= 1 << ((x_base + x_offset) & 0x07);
					    lcd_set_pixel(lcd_draw_buf.screen_buf,x_base + x_offset, y_base+y_offset);
					}

				}
				x_offset++;
				x_bit_idx++;
				if (!(x_bit_idx & 0x07))
				{
					x_bit_idx = 0;
					p++;
				}
				j--;
			}
		}
	}
	lcd_draw_buf.current_x += Cmetrics.LSBearing + Cmetrics.width + 1;
}
int get_external_str_pixel_width(char **pUTF8)
{
	bmf_bm_t *bitmap;
	charmetric_bmf Cmetrics;
	int width = 0;
	ucs4_t u;

	while (**pUTF8 > MAX_ESC_CHAR)
	{
		if ((u = UTF8_to_UCS4((unsigned char**)pUTF8)))
		{
			pres_bmfbm(u, &pcfFonts[DEFAULT_FONT_IDX - 1], &bitmap, &Cmetrics);
			if (bitmap != NULL)
			   width += Cmetrics.width + 1;
		}

	}
	return width;
}
void buf_draw_char_external(LCD_DRAW_BUF *lcd_draw_buf_external,ucs4_t u,int start_x,int end_x,int start_y,int end_y)
{
	bmf_bm_t *bitmap;
	charmetric_bmf Cmetrics;
	int bytes_to_process;
	int x_base;
	int y_base;
	int x_offset;
	int y_offset;
	int x_bit_idx;
	int i; // bitmap byte index
	int j; // bitmap bit index
	unsigned char *p; // pointer to lcd draw buffer

	if(pres_bmfbm(u, lcd_draw_buf_external->pPcfFont, &bitmap, &Cmetrics)<0)
	  return;
	if(u==32)
	{
	   lcd_draw_buf_external->current_x += Cmetrics.LSBearing + Cmetrics.width + 1;
	   return;
	}
	if (bitmap == NULL)
	  return;

	bytes_to_process = Cmetrics.widthBytes * Cmetrics.height;

	x_base = lcd_draw_buf_external->current_x + Cmetrics.LSBearing;
	if((x_base +Cmetrics.width) > end_x)
	{
	    lcd_draw_buf_external->current_x = start_x+2;
	    x_base = 0;
	    lcd_draw_buf_external->current_y+=lcd_draw_buf_external->line_height;
	}
	y_base = lcd_draw_buf_external->current_y + lcd_draw_buf_external->align_adjustment;
	x_offset = 0;
	y_offset = lcd_draw_buf_external->line_height - (lcd_draw_buf_external->pPcfFont->Fmetrics.descent + Cmetrics.ascent);
	x_bit_idx = x_base & 0x07;

	for (i = 0; i < bytes_to_process; i++)
	{
		j = 7;
		while (j >= 0)
		{
			if (x_offset >= Cmetrics.widthBits)
			{
				x_offset = 0;
				y_offset++;
			}
			if (x_offset < Cmetrics.width)
			{
				if (bitmap[i] & (1 << j))
				{
				    lcd_set_pixel(lcd_draw_buf_external->screen_buf,x_base + x_offset, y_base+y_offset);
				}

			}
			x_offset++;
			x_bit_idx++;
			if (!(x_bit_idx & 0x07))
			{
				x_bit_idx = 0;
				p++;
			}
			j--;
		}
	}
	lcd_draw_buf_external->current_x += Cmetrics.LSBearing + Cmetrics.width + 1;
}

int get_UTF8_char_width(int idxFont, char **pContent, long *lenContent, int *nCharBytes)
{
	ucs4_t u;
	char *pBase;
	charmetric_bmf Cmetrics;
	bmf_bm_t *bitmap;

	pBase = *pContent;
	u = UTF8_to_UCS4((unsigned char **)pContent);
	*nCharBytes = *pContent - pBase;
	*lenContent -= *nCharBytes;

	pres_bmfbm(u, &pcfFonts[idxFont - 1], &bitmap, &Cmetrics);
	if (bitmap == NULL)
		return 0;
	else
		return  Cmetrics.LSBearing + Cmetrics.width + 1;
}
void init_render_article(long init_y_pos)
{

	//if(lcd_draw_buf.current_y>0)
	  //  memset(lcd_draw_buf.screen_buf,0,lcd_draw_buf.current_y*LCD_VRAM_WIDTH_PIXELS/8);
	if (lcd_draw_buf.screen_buf)
		memset(lcd_draw_buf.screen_buf, 0, LCD_BUF_WIDTH_BYTES * LCD_BUF_HEIGHT_PIXELS);

	article_buf_pointer = NULL;
	lcd_draw_buf.current_x = 0;
	lcd_draw_buf.current_y = 0;
	lcd_draw_buf.drawing = 0;
	lcd_draw_buf.pPcfFont = NULL;
	lcd_draw_buf.line_height = 0;
	lcd_draw_buf.align_adjustment = 0;
	lcd_draw_buf.vertical_adjustment = 0;

	display_first_page = 0;
	lcd_draw_cur_y_pos = 0;
	lcd_draw_init_y_pos = init_y_pos;
	finger_move_speed = 0;
	lcd_draw_buf_pos = 0;
}

#define LICENSE_TEXT_FONT ITALIC_FONT_IDX
#define SPACE_BEFORE_LICENSE_TEXT 40
#define APACE_AFTER_LICENSE_TEXT 5
#define LICENSE_TEXT_1 "Text is available under the Creative"
#define LICENSE_TEXT_2 "Commons Attribution/Share-Alike"
#define LICENSE_TEXT_3 "License and can be freely reused under"
#define LICENSE_TEXT_4 "the term of that license. See: Text of"
#define LICENSE_TEXT_5 "the CC-BY-SA License and Terms of"
#define LICENSE_TEXT_6 "Use for additional terms which may"
#define LICENSE_TEXT_7 "apply. The original article is available"
#define LICENSE_TEXT_8 "at: http://en.wikipedia.org/wiki/"
#define LICENSE_TEXT_9 "Wikipedia:Text_of_Creative_Commons_"
#define LICENSE_TEXT_10 "Attribution-ShareAlike_3.0_Unported"
#define LICENSE_TEXT_11 "_License"
void render_wikipedia_license_text()
{
#ifndef WIKIPCF
	long start_x, start_y, end_x, end_y;

	// if not enough space at the end, then skip
	if (lcd_draw_buf.current_y < LCD_BUF_HEIGHT_PIXELS - SPACE_BEFORE_LICENSE_TEXT - lcd_draw_buf.line_height * 7 - APACE_AFTER_LICENSE_TEXT)
	{
		lcd_draw_buf.line_height = pcfFonts[LICENSE_TEXT_FONT - 1].Fmetrics.linespace;
		lcd_draw_buf.current_x = 0;
		lcd_draw_buf.current_y += SPACE_BEFORE_LICENSE_TEXT;
		lcd_draw_buf.vertical_adjustment = 0;
		lcd_draw_buf.align_adjustment = 0;
		lcd_draw_buf.pPcfFont = &pcfFonts[LICENSE_TEXT_FONT - 1];

		draw_string(LICENSE_TEXT_1);
		lcd_draw_buf.current_x = 0;
		lcd_draw_buf.current_y += lcd_draw_buf.line_height;
		draw_string(LICENSE_TEXT_2);
		lcd_draw_buf.current_x = 0;
		lcd_draw_buf.current_y += lcd_draw_buf.line_height;
		draw_string(LICENSE_TEXT_3);
		lcd_draw_buf.current_x = 0;
		lcd_draw_buf.current_y += lcd_draw_buf.line_height;
		draw_string(LICENSE_TEXT_4);
		lcd_draw_buf.current_x = 0;
		lcd_draw_buf.current_y += lcd_draw_buf.line_height;
		draw_string(LICENSE_TEXT_5);
		start_x = 20;
		end_x = 88;
		buf_draw_horizontal_line(start_x + LCD_LEFT_MARGIN, end_x + LCD_LEFT_MARGIN);
		if (article_link_count < MAX_ARTICLE_LINKS)
		{
			start_y = lcd_draw_buf.current_y + 1;
			end_y = lcd_draw_buf.current_y + lcd_draw_buf.line_height;
			articleLink[article_link_count].start_xy = (unsigned  long)(start_x | (start_y << 8));
			articleLink[article_link_count].end_xy = (unsigned  long)(end_x | (end_y << 8));
			articleLink[article_link_count++].article_id = 1;
		}
		start_x = lcd_draw_buf.current_x - 55;
		end_x = lcd_draw_buf.current_x;
		buf_draw_horizontal_line(start_x + LCD_LEFT_MARGIN, end_x + LCD_LEFT_MARGIN);
		if (article_link_count < MAX_ARTICLE_LINKS)
		{
			start_y = lcd_draw_buf.current_y + 1;
			end_y = lcd_draw_buf.current_y + lcd_draw_buf.line_height;
			articleLink[article_link_count].start_xy = (unsigned  long)(start_x | (start_y << 8));
			articleLink[article_link_count].end_xy = (unsigned  long)(end_x | (end_y << 8));
			articleLink[article_link_count++].article_id = 2;
		}
		lcd_draw_buf.current_x = 0;
		lcd_draw_buf.current_y += lcd_draw_buf.line_height;
		draw_string(LICENSE_TEXT_6);
		start_x = 0;
		end_x = 23;
		buf_draw_horizontal_line(start_x + LCD_LEFT_MARGIN, end_x + LCD_LEFT_MARGIN);
		if (article_link_count < MAX_ARTICLE_LINKS)
		{
			start_y = lcd_draw_buf.current_y + 1;
			end_y = lcd_draw_buf.current_y + lcd_draw_buf.line_height;
			articleLink[article_link_count].start_xy = (unsigned  long)(start_x | (start_y << 8));
			articleLink[article_link_count].end_xy = (unsigned  long)(end_x | (end_y << 8));
			articleLink[article_link_count++].article_id = 2;
		}
		lcd_draw_buf.current_x = 0;
		lcd_draw_buf.current_y += lcd_draw_buf.line_height;
		draw_string(LICENSE_TEXT_7);
		lcd_draw_buf.current_x = 0;
		lcd_draw_buf.current_y += lcd_draw_buf.line_height;
		draw_string(LICENSE_TEXT_8);
		start_x = 18;
		end_x = lcd_draw_buf.current_x;
		buf_draw_horizontal_line(start_x + LCD_LEFT_MARGIN, end_x + LCD_LEFT_MARGIN);
		lcd_draw_buf.current_x = 0;
		lcd_draw_buf.current_y += lcd_draw_buf.line_height;
		draw_string(LICENSE_TEXT_9);
		start_x = 0;
		end_x = lcd_draw_buf.current_x;
		buf_draw_horizontal_line(start_x + LCD_LEFT_MARGIN, end_x + LCD_LEFT_MARGIN);
		lcd_draw_buf.current_x = 0;
		lcd_draw_buf.current_y += lcd_draw_buf.line_height;
		draw_string(LICENSE_TEXT_10);
		start_x = 0;
		end_x = lcd_draw_buf.current_x;
		buf_draw_horizontal_line(start_x + LCD_LEFT_MARGIN, end_x + LCD_LEFT_MARGIN);
		lcd_draw_buf.current_x = 0;
		lcd_draw_buf.current_y += lcd_draw_buf.line_height;
		draw_string(LICENSE_TEXT_11);
		start_x = 0;
		end_x = lcd_draw_buf.current_x;
		buf_draw_horizontal_line(start_x + LCD_LEFT_MARGIN, end_x + LCD_LEFT_MARGIN);
		lcd_draw_buf.current_y += lcd_draw_buf.line_height + APACE_AFTER_LICENSE_TEXT;
	}
#endif
}

int render_article_with_pcf()
{

	if (!article_buf_pointer)
		return 0;

	buf_draw_UTF8_str(&article_buf_pointer);
	if(stop_render_article == 1 && display_first_page == 1)
	{
	   article_buf_pointer = NULL;
	   stop_render_article = 0;
	   return 0;
	}
	if(request_display_next_page > 0 && lcd_draw_buf.current_y > request_y_pos)
	{
		lcd_draw_cur_y_pos = request_y_pos - LCD_HEIGHT_LINES;
		if (lcd_draw_cur_y_pos < 0)
			 lcd_draw_cur_y_pos = 0;
		repaint_framebuffer(lcd_draw_buf.screen_buf,lcd_draw_cur_y_pos*LCD_VRAM_WIDTH_PIXELS/8, 1);
		request_display_next_page = 0;
	}
	if(!*article_buf_pointer)
	{
		render_wikipedia_license_text();
		if(display_first_page == 0 || request_display_next_page > 0)
		{
			if(request_display_next_page > 0)
			{
				lcd_draw_cur_y_pos = request_y_pos - LCD_HEIGHT_LINES;
				if (lcd_draw_cur_y_pos + LCD_HEIGHT_LINES > lcd_draw_buf.current_y)
					lcd_draw_cur_y_pos = lcd_draw_buf.current_y - LCD_HEIGHT_LINES;
				if (lcd_draw_cur_y_pos < 0)
					lcd_draw_cur_y_pos = 0;
			}
			repaint_framebuffer(lcd_draw_buf.screen_buf, lcd_draw_cur_y_pos*LCD_VRAM_WIDTH_PIXELS/8, 1);
			display_first_page = 1;
			request_display_next_page = 0;
		}

		article_buf_pointer = NULL;

		return 0;
	}
	return 1;

}

#ifndef WIKIPCF
extern int history_count;
extern int rendered_history_count;
extern HISTORY history_list[MAX_HISTORY];
int render_history_with_pcf()
{
	int rc = 0;
	int start_x, end_x, start_y, end_y;

	restricted_article = 0;
	if (rendered_history_count < 0)
		return rc;

	guilib_fb_lock();
	if (rendered_history_count == 0)
	{
		init_render_article(0);
		lcd_draw_buf.pPcfFont = &pcfFonts[SEARCH_HEADING_FONT_IDX - 1];
		lcd_draw_buf.line_height = pcfFonts[SEARCH_HEADING_FONT_IDX - 1].Fmetrics.linespace;
		lcd_draw_buf.current_x = 0;
		lcd_draw_buf.current_y = LCD_TOP_MARGIN;
		lcd_draw_buf.vertical_adjustment = 0;
		lcd_draw_buf.align_adjustment = 0;
		draw_string(MESSAGE_HISTORY_TITLE);
		lcd_draw_buf.pPcfFont = &pcfFonts[SEARCH_LIST_FONT_IDX - 1];
		lcd_draw_buf.line_height = HISTORY_RESULT_HEIGHT;
		lcd_draw_buf.current_x = 0;
		lcd_draw_buf.current_y = HISTORY_RESULT_START;
		article_link_count = 0;
	}

	if (history_count == 0) {
		lcd_draw_buf.current_x = 83;
		lcd_draw_buf.current_y = 95;
		lcd_draw_buf.pPcfFont = &pcfFonts[SEARCH_LIST_FONT_IDX - 1];
		lcd_draw_buf.line_height = pcfFonts[SEARCH_LIST_FONT_IDX - 1].Fmetrics.linespace;
		draw_string(MESSAGE_NO_HISTORY);
		rendered_history_count = -1;
		repaint_framebuffer(lcd_draw_buf.screen_buf,0, 0);
		display_first_page = 1;
	} else if (rendered_history_count < history_count) {
		start_x = 0;
		end_x = LCD_BUF_WIDTH_PIXELS - 1;
		if (article_link_count < MAX_ARTICLE_LINKS)
		{
			start_y = lcd_draw_buf.current_y + 1;
			end_y = lcd_draw_buf.current_y + lcd_draw_buf.line_height;
			articleLink[article_link_count].start_xy = (unsigned  long)(start_x | (start_y << 8));
			articleLink[article_link_count].end_xy = (unsigned  long)(end_x | (end_y << 8));
			articleLink[article_link_count++].article_id = history_list[rendered_history_count].idx_article;
		}
		draw_string(history_list[rendered_history_count].title);
		rendered_history_count++;
		lcd_draw_buf.current_x = 0;
		lcd_draw_buf.current_y += lcd_draw_buf.line_height;
		if (rendered_history_count < history_count)
			rc = 1;
		else
		{
			rendered_history_count = -1;
			if(display_first_page == 0)
			{
				repaint_framebuffer(lcd_draw_buf.screen_buf,0, 0);
				display_first_page = 1;
			}
			else if (request_display_next_page > 0)
			{
				long y_pos = lcd_draw_buf.current_y - LCD_HEIGHT_LINES;
				if (y_pos < 0)
					y_pos = 0;
				lcd_draw_cur_y_pos = y_pos;
				repaint_framebuffer(lcd_draw_buf.screen_buf, lcd_draw_cur_y_pos * LCD_VRAM_WIDTH_PIXELS / 8, 1);
				request_display_next_page = 0;
			}
		}

		if(stop_render_article == 1 && display_first_page == 1)
		{
			rendered_history_count = -1;
			stop_render_article = 0;
		}

		if(request_display_next_page > 0 && lcd_draw_buf.current_y >= request_y_pos+LCD_HEIGHT_LINES)
		{
			lcd_draw_cur_y_pos = request_y_pos;
			repaint_framebuffer(lcd_draw_buf.screen_buf,lcd_draw_cur_y_pos*LCD_VRAM_WIDTH_PIXELS/8, 1);
			request_display_next_page = 0;
		}
	}

	guilib_fb_unlock();
	return rc;
}

extern int more_search_results;
void restore_search_list_page(void)
{
	if (article_link_count > NUMBER_OF_FIRST_PAGE_RESULTS)
	{
		more_search_results = 0;
		article_link_count = NUMBER_OF_FIRST_PAGE_RESULTS;
		memcpy(framebuffer, lcd_draw_buf.screen_buf, framebuffer_size()); // copy from the LCD frame buffer (for the first page)
	}
}

int render_search_result_with_pcf(void)
{
	int rc = 0;
	int start_x, end_x, start_y, end_y;
	long idxArticle;
	char sTitleSearch[MAX_TITLE_SEARCH];
	static long offset_next = 0;

	if (!more_search_results)
	{
		display_first_page = 1;
		return rc;
	}

	guilib_fb_lock();
	if (article_link_count == NUMBER_OF_FIRST_PAGE_RESULTS) // has not rendered any results beyond the first page
	{
		offset_next = result_list_offset_next();
		init_render_article(0);
		memcpy(lcd_draw_buf.screen_buf, framebuffer, framebuffer_size()); // copy from the LCD frame buffer (for the first page)
		display_first_page = 1;
		lcd_draw_buf.pPcfFont = &pcfFonts[SEARCH_LIST_FONT_IDX - 1];
		lcd_draw_buf.line_height = RESULT_HEIGHT;
		lcd_draw_buf.current_x = 0;
		lcd_draw_buf.current_y = (RESULT_START - 2) + RESULT_HEIGHT * NUMBER_OF_FIRST_PAGE_RESULTS;
		lcd_draw_cur_y_pos = 0;
	}

	if ((offset_next = result_list_next_result(offset_next, &idxArticle, sTitleSearch)))
	{
		start_x = 0;
		end_x = LCD_BUF_WIDTH_PIXELS - 1;
		if (article_link_count < MAX_ARTICLE_LINKS)
		{
			start_y = lcd_draw_buf.current_y + 1;
			end_y = lcd_draw_buf.current_y + lcd_draw_buf.line_height;
			articleLink[article_link_count].start_xy = (unsigned  long)(start_x | (start_y << 8));
			articleLink[article_link_count].end_xy = (unsigned  long)(end_x | (end_y << 8));
			articleLink[article_link_count++].article_id = idxArticle;
			draw_string(sTitleSearch);
			lcd_draw_buf.current_x = 0;
			lcd_draw_buf.current_y += lcd_draw_buf.line_height;
			rc = 1;
		}
		else
			more_search_results = 0;

		if (stop_render_article == 1)
		{
			more_search_results = 0;
			stop_render_article = 0;
		}

		if(request_display_next_page > 0 && lcd_draw_buf.current_y >= request_y_pos+LCD_HEIGHT_LINES)
		{
			lcd_draw_cur_y_pos = request_y_pos;
			repaint_framebuffer(lcd_draw_buf.screen_buf,lcd_draw_cur_y_pos*LCD_VRAM_WIDTH_PIXELS/8, 1);
			request_display_next_page = 0;
		}
	}
	else
	{
		more_search_results = 0;
		if (request_display_next_page > 0)
		{
			long y_pos = lcd_draw_buf.current_y - LCD_HEIGHT_LINES;
			if (y_pos < 0)
				y_pos = 0;
			lcd_draw_cur_y_pos = y_pos;
			repaint_framebuffer(lcd_draw_buf.screen_buf, lcd_draw_cur_y_pos * LCD_VRAM_WIDTH_PIXELS / 8, 1);
			request_display_next_page = 0;
		}
	}

	guilib_fb_unlock();
	return rc;
}

void display_article_with_pcf(int start_y)
{
	int pos;

	if(lcd_draw_buf.current_y<=LCD_HEIGHT_LINES || request_display_next_page ||
		(display_mode == DISPLAY_MODE_INDEX && article_link_count <= NUMBER_OF_FIRST_PAGE_RESULTS))
	    return;

	if(article_buf_pointer && (lcd_draw_cur_y_pos+start_y+LCD_HEIGHT_LINES) > lcd_draw_buf.current_y)
	{
	    request_display_next_page = 1;
	    request_y_pos = lcd_draw_cur_y_pos + start_y + LCD_HEIGHT_LINES;

	    display_str("Please wait...");

	    return;
	}
	if ((lcd_draw_cur_y_pos == 0 && start_y < 0) ||
		((lcd_draw_cur_y_pos+LCD_HEIGHT_LINES)>lcd_draw_buf.current_y && start_y >= 0))
	{
	   return;
	}

	lcd_draw_cur_y_pos += start_y;

	if ((lcd_draw_cur_y_pos+LCD_HEIGHT_LINES)>lcd_draw_buf.current_y)
		lcd_draw_cur_y_pos = lcd_draw_buf.current_y - LCD_HEIGHT_LINES;
	if (lcd_draw_cur_y_pos < 0)
		lcd_draw_cur_y_pos = 0;
	if (display_mode == DISPLAY_MODE_ARTICLE)
		history_log_y_pos(lcd_draw_cur_y_pos);

	pos = (lcd_draw_cur_y_pos*LCD_VRAM_WIDTH_PIXELS)/8;

	repaint_framebuffer(lcd_draw_buf.screen_buf,pos, 1);

}

float scroll_speed()
{
	float speed;

	speed = (float)finger_move_speed * SCROLL_SPEED_FRICTION;
	if (abs(speed) < 1 / SCROLL_UNIT_SECOND)
		speed = 0;
	return speed;
}

void scroll_article(void)
{
	unsigned long time_now, delay_time;
	long pos;

	if(finger_move_speed == 0)
	  return;

	if (!display_first_page || request_display_next_page ||
		((display_mode == DISPLAY_MODE_INDEX || display_mode == DISPLAY_MODE_HISTORY) &&
		article_link_count <= NUMBER_OF_FIRST_PAGE_RESULTS))
	{
		finger_move_speed = 0;
		return;
	}

	time_now = get_time_ticks();
	delay_time = time_diff(time_now, time_scroll_article_last);

	if (delay_time >= seconds_to_ticks(SCROLL_UNIT_SECOND))
	{
		time_scroll_article_last = time_now;

		article_scroll_increment = (float)finger_move_speed * ((float)delay_time / (float)seconds_to_ticks(1));

		lcd_draw_cur_y_pos += article_scroll_increment;
		finger_move_speed = scroll_speed();
		if(lcd_draw_cur_y_pos < 0)
		{
			lcd_draw_cur_y_pos = 0;
			finger_move_speed = 0;
		}
		else if (lcd_draw_cur_y_pos > lcd_draw_buf.current_y - LCD_HEIGHT_LINES)
		{
			lcd_draw_cur_y_pos = lcd_draw_buf.current_y - LCD_HEIGHT_LINES;
			finger_move_speed = 0;
		}
		if (display_mode == DISPLAY_MODE_ARTICLE)
			history_log_y_pos(lcd_draw_cur_y_pos);

		pos = (lcd_draw_cur_y_pos*LCD_VRAM_WIDTH_PIXELS)/8;

		repaint_framebuffer(lcd_draw_buf.screen_buf,pos, 1);

		if (finger_move_speed == 0 && b_show_scroll_bar)
		{
			b_show_scroll_bar = 0;
			show_scroll_bar(0); // clear scroll bar
		}
	}
}

void display_retrieved_article(long idx_article)
{
	int i;
	int offset,start_x,start_y,end_x,end_y;
	ARTICLE_HEADER article_header;
	char title[MAX_TITLE_SEARCH];
	int link_count_addon = 0;
	int bKeepPos = 0;

	if (last_display_mode == DISPLAY_MODE_HISTORY)
	{
		init_render_article(history_get_y_pos(idx_article));
		bKeepPos = 1;
	}
	else
	{
		init_render_article(0);
	}
	memcpy(&article_header,file_buffer,sizeof(ARTICLE_HEADER));
	offset = sizeof(ARTICLE_HEADER);

	if(article_header.article_link_count>MAX_ARTICLE_LINKS)
	    article_header.article_link_count = MAX_ARTICLE_LINKS;

	if (restricted_article)
	{
		articleLink[0].start_xy = (unsigned  long)(211 | (4 << 8));
		articleLink[0].end_xy = (unsigned  long)(230 | (24 << 8));
		articleLink[0].article_id = RESTRICTED_MARK_LINK;
		link_count_addon = 1;
	}

	if (article_header.article_link_count < MAX_ARTICLE_LINKS - link_count_addon)
		article_link_count = article_header.article_link_count;
	else
		article_link_count = MAX_ARTICLE_LINKS - link_count_addon - 1;
	for(i = 0; i < article_link_count; i++)
	{
	    memcpy(&articleLink[i + link_count_addon],file_buffer+offset,sizeof(ARTICLE_LINK));
	    offset+=sizeof(ARTICLE_LINK);

	    start_y = articleLink[i + link_count_addon].start_xy >>8;
	    start_x = articleLink[i + link_count_addon].start_xy & 0x000000ff;
	    end_y   = articleLink[i + link_count_addon].end_xy  >>8;
	    end_x   = articleLink[i + link_count_addon].end_xy & 0x000000ff;
	}

	article_link_count += link_count_addon;
	article_buf_pointer = file_buffer+article_header.offset_article;

	display_first_page = 0; // use this to disable scrolling until the first page of the linked article is loaded
	get_article_title_from_idx(idx_article, title);
	history_add(idx_article, title, bKeepPos);
}

int isArticleLinkSelectedSequentialSearch(int x,int y, int start_i, int end_i)
{
	int i;
	int x_diff, y_diff;
	int last_x_diff = 999;
	int last_y_diff = 999;
	int rc = -1;
	int article_link_start_y_pos;
	int article_link_start_x_pos;
	int article_link_end_y_pos;
	int article_link_end_x_pos;
	int left_margin;

	if (display_mode == DISPLAY_MODE_ARTICLE)
		left_margin = LCD_LEFT_MARGIN;
	else
		left_margin = 0;

	i = start_i;
	while (i <= end_i)
	{
		article_link_start_x_pos = (articleLink[i].start_xy & 0x000000ff) + left_margin;
		article_link_start_y_pos = (articleLink[i].start_xy >> 8);
		article_link_end_x_pos = (articleLink[i].end_xy & 0x000000ff) + left_margin;
		article_link_end_y_pos = (articleLink[i].end_xy >> 8);

		if (y < article_link_start_y_pos)
			y_diff = article_link_start_y_pos - y;
		else if (y > article_link_end_y_pos)
			y_diff = y - article_link_end_y_pos;
		else
			y_diff = 0;

		if (x < article_link_start_x_pos)
			x_diff = article_link_start_x_pos - x;
		else if (x > article_link_end_x_pos)
			x_diff = x - article_link_end_x_pos;
		else
			x_diff = 0;

		if (x_diff <= LINK_X_DIFF_ALLOWANCE && y_diff <= LINK_Y_DIFF_ALLOWANCE)
		{
			if (((last_x_diff && !x_diff) || (last_y_diff && !y_diff)) ||
				(x_diff < last_x_diff && y_diff < last_y_diff))
			{
				rc = i;
				last_x_diff = x_diff;
				last_y_diff = y_diff;
			}
		}
		i++;
	}
	return rc;
}

int isArticleLinkSelected(int x,int y)
{
	int i, start_i, end_i;
	int bFound;
	int article_link_start_y_pos;
	int article_link_start_x_pos;
	int article_link_end_y_pos;
	int article_link_end_x_pos;
	//char msg[1024];
	int left_margin;

	if (!display_first_page || request_display_next_page)
		return -1;

	if (display_mode == DISPLAY_MODE_ARTICLE)
		left_margin = LCD_LEFT_MARGIN;
	else
		left_margin = 0;

	y += lcd_draw_cur_y_pos;

	if (link_currently_activated >= 0 && link_currently_activated < article_link_count)
	{
		article_link_start_x_pos = (articleLink[link_currently_activated].start_xy & 0x000000ff) + left_margin - LINK_X_DIFF_ALLOWANCE;
		article_link_start_y_pos = (articleLink[link_currently_activated].start_xy >> 8) - LINK_Y_DIFF_ALLOWANCE;
		article_link_end_x_pos = (articleLink[link_currently_activated].end_xy & 0x000000ff) + left_margin + LINK_X_DIFF_ALLOWANCE;
		article_link_end_y_pos = (articleLink[link_currently_activated].end_xy >> 8) + LINK_Y_DIFF_ALLOWANCE;
		if (y>=article_link_start_y_pos && y<=article_link_end_y_pos && x>=article_link_start_x_pos && x<=article_link_end_x_pos)
			return link_currently_activated; // if more than on links are matched, the last matched one got the higher priority
	}

	start_i = 0;
	end_i = article_link_count - 1;
	i = article_link_count / 2;
	bFound = 0;
	while (!bFound && start_i <= end_i)
	{
		if (y < (int)(articleLink[i].start_xy >> 8) - LINK_Y_DIFF_ALLOWANCE)
		{
			end_i = i - 1;
			i = (start_i + end_i) / 2;
			continue;
		}
		if (y > (int)(articleLink[i].end_xy >> 8) + LINK_Y_DIFF_ALLOWANCE)
		{
			start_i = i + 1;
			i = (start_i + end_i) / 2;
			continue;
		}
		// y range identified
		return isArticleLinkSelectedSequentialSearch(x, y, start_i, end_i);
	}
	return -1;
}

#ifndef INCLUDED_FROM_KERNEL
int load_init_article(long idx_init_article)
{
	int fd;
	char file[128];
	long len;
	int i;
	int offset,start_x,start_y,end_x,end_y;
	ARTICLE_HEADER article_header;

	if (idx_init_article > 0)
	{
		sprintf(file, "./dat/%ld/%ld/%ld/%ld", (idx_init_article / 1000000), (idx_init_article / 10000) % 100, (idx_init_article / 100) % 100, idx_init_article);
		fd = wl_open(file, WL_O_RDONLY);
		if (fd >= 0)
		{
			len = wl_read(fd, (void *)file_buffer, FILE_BUFFER_SIZE - 1);
			file_buffer[len] = '\0';
			wl_close(fd);
		}

		memcpy(&article_header,file_buffer,sizeof(ARTICLE_HEADER));
		offset = sizeof(ARTICLE_HEADER);

		if(article_header.article_link_count>MAX_ARTICLE_LINKS)
		    article_header.article_link_count = MAX_ARTICLE_LINKS;

		for(i = 0; i< article_header.article_link_count;i++)
		{
		    memcpy(&articleLink[i],file_buffer+offset,sizeof(ARTICLE_LINK));
		    offset+=sizeof(ARTICLE_LINK);

		    start_y = articleLink[i].start_xy >>8;
		    start_x = articleLink[i].start_xy & 0x000000ff;
		    end_y   = articleLink[i].end_xy  >>8;
		    end_x   = articleLink[i].end_xy & 0x000000ff;

		}
		article_link_count = article_header.article_link_count;

		article_buf_pointer = file_buffer+article_header.offset_article;
		init_render_article(0);
		return 0;
	}
	return -1;
}
#endif
#endif

#ifndef WIKIPCF
void display_link_article(long idx_article)
{

	if (idx_article == RESTRICTED_MARK_LINK)
	{
		filter_option();
		return;
	}

	saved_idx_article = idx_article;
	file_buffer[0] = '\0';

	if (retrieve_article(idx_article))
	{
	    return; // article not exist
	}

	if (restricted_article && check_restriction(idx_article))
		return;

	display_retrieved_article(idx_article);
}

void display_str(char *str)
{
       int start_x,end_x,start_y,end_y;
       int offset_x,offset_y;
       int str_width;
       char *p;

       p = str;

       // framebuffer_size ==0 iwhen WIKPCF is defined
       // This causes fatal linker errors of recent gcc (Ubuntu 9.04)
       int framebuffersize = framebuffer_size();
       memset(framebuffer_copy,0,framebuffersize);

       start_x = 0;
       end_x   = framebuffer_width();
       start_y = framebuffer_height()/2-9;
       end_y   = framebuffer_height()/2+9;

//       drawline_in_framebuffer_copy(framebuffer_copy,start_x,start_y,end_x,end_y);

       str_width = get_external_str_pixel_width(&p);
       offset_x = (end_x - str_width) / 2 - start_x;
       offset_y = 0;
       buf_draw_UTF8_str_in_copy_buffer((char *)framebuffer_copy,&str,start_x,end_x,start_y,end_y,offset_x);

       repaint_framebuffer(framebuffer_copy,0, 0);

 }
#endif

int div_wiki(int a,int b)
{
    int c =0,m = 0;

    if(a<b)
       return 0;

    for(;;)
    {
       c += b;
       if(c >= a)
	   return m;
       m++;
    }
}
void drawline_in_framebuffer_copy(unsigned char *buffer,int start_x,int start_y,int end_x,int end_y)
{
      int i,m;
      for(i = start_y;i<end_y;i++)
	 for(m = start_x;m<end_x;m++)
	   lcd_clear_pixel(buffer,m,i);

      for(i = start_x ; i < end_x ; i++)
	    lcd_set_pixel(buffer,i, start_y);

      for(i = start_y ; i < end_y ; i++)
	    lcd_set_pixel(buffer,start_x,i);

      for(i = start_x ; i < end_x ; i++)
	    lcd_set_pixel(buffer,i,end_y);

      for(i = start_y ; i < end_y ; i++)
	    lcd_set_pixel(buffer,end_x,i);
}

#ifndef WIKIPCF

// when set_article_link_number is called, the link will be "activated" in a pre-defined period
// the activated link will be inverted
void set_article_link_number(int num, unsigned long event_time)
{
	if (link_currently_activated < 0)
	{
		link_to_be_activated = num;
		link_to_be_activated_start_time = event_time;
	}
	else if (link_currently_activated != num) // if on another link, deactivate both links since figer moves
	{
		reset_article_link_number();
	}
}

void reset_article_link_number(void)
{
	if (link_currently_inverted >= 0)
	{
		if (b_show_scroll_bar)
			show_scroll_bar(-1);
		invert_link(link_currently_inverted);
		if (b_show_scroll_bar)
			show_scroll_bar(-1);
	}
	link_currently_activated = -1;
	link_to_be_activated = -1;
	link_to_be_inverted = -1;
	link_currently_inverted = -1;
}

void init_invert_link(void)
{
	link_to_be_activated = -1;
	link_currently_activated = -1;
	link_to_be_inverted = -1;
	link_currently_inverted = -1;
}


int get_activated_article_link_number()
{
	return link_currently_activated;
}

void repaint_invert_link()
{
	if (link_currently_activated >= 0)
	{
		invert_link(link_currently_activated);
		link_to_be_inverted = -1;
	}
}

void invert_link(int article_link_number)
{
     int start_x,start_y,end_x,end_y;
	int left_margin;

	if(article_link_number<0)
	return;


	if (display_mode == DISPLAY_MODE_ARTICLE)
		left_margin = LCD_LEFT_MARGIN;
	else
		left_margin = 0;

	start_y = (articleLink[article_link_number].start_xy >>8) - lcd_draw_cur_y_pos;
	start_x = (articleLink[article_link_number].start_xy & 0x000000ff) + left_margin - 1;
	if (start_x < 0)
		start_x = 0;
	end_y   = (articleLink[article_link_number].end_xy  >>8) - lcd_draw_cur_y_pos;
	end_x   = (articleLink[article_link_number].end_xy & 0x000000ff) + left_margin;
	if (end_x >= LCD_BUF_WIDTH_PIXELS)
		end_x = LCD_BUF_WIDTH_PIXELS - 1;

	if (start_y >= 0 || end_y < LCD_HEIGHT_LINES)
	{
		guilib_fb_lock();
		// guilib_invert_area will only invert (x, y) within LCD range
		guilib_invert_area(start_x, start_y, end_x, end_y);
		guilib_fb_unlock();
	}
}

// invert link when timeout
int check_invert_link()
{
	if (link_to_be_activated >= 0 && time_diff(get_time_ticks(), link_to_be_activated_start_time) >=
		seconds_to_ticks(LINK_ACTIVATION_TIME_THRESHOLD))
	{
		link_currently_activated = link_to_be_activated;
		link_to_be_inverted = link_to_be_activated;
		link_to_be_inverted_start_time = link_to_be_activated_start_time;
		if (link_currently_inverted >= 0)
		{
			invert_link(link_currently_inverted);
			link_currently_inverted = -1;
		}
	}
	else if (link_to_be_inverted >= 0 && time_diff(get_time_ticks(), link_to_be_inverted_start_time) >=
		seconds_to_ticks(LINK_INVERT_ACTIVATION_TIME_THRESHOLD))
	{
		if (link_currently_inverted >= 0)
			invert_link(link_currently_inverted);
		invert_link(link_to_be_inverted);
		link_currently_inverted = link_to_be_inverted;
		link_to_be_inverted = -1;
	}

	if (link_to_be_activated >= 0 || link_to_be_inverted >= 0)
		return 1;
	else
		return 0;
}

void open_article_link(int x,int y)
{
     int article_link_number;

      article_link_number = isArticleLinkSelected(x,y);
      if(article_link_number >= 0)
      {
	 display_link_article(articleLink[article_link_number].article_id);
      }
}

void open_article_link_with_link_number(int article_link_number)
{
	long idx;

	if (article_link_number < 0 || articleLink[article_link_number].article_id <= 0)
		return;
	display_first_page = 0; // use this to disable scrolling until the first page of the linked article is loaded
	idx = articleLink[article_link_number].article_id;
	if (idx == RESTRICTED_MARK_LINK)
	{
#ifdef INCLUDED_FROM_KERNEL
		delay_us(100000);
#endif
		invert_link(article_link_number);
	}
	display_link_article(idx);
}
#endif

void msg_info(char *data)
{
#ifdef WIKIPCF
   printf(data);
#else
   msg(MSG_INFO,data);
#endif
}
int framebuffer_size()
{
#ifdef WIKIPCF
    return 0;
#else
    return guilib_framebuffer_size();
#endif
}
int framebuffer_width()
{
#ifdef WIKIPCF
    return 0;
#else
    return guilib_framebuffer_width();
#endif

}
int framebuffer_height()
{
#ifdef WIKIPCF
    return 0;
#else
    return guilib_framebuffer_height();
#endif

}

int strchr_idx(char *s, char c)
{
	int rc = -1;
	int i = 0;

	while (rc < 0 && s[i])
	{
		if (s[i] == c)
			rc = i;
		i++;
	}
	return rc;
}

#ifndef WIKIPCF
int draw_bmf_char(ucs4_t u,int font,int x,int y, int inverted)
{
	bmf_bm_t *bitmap;
	charmetric_bmf Cmetrics;
	//pcf_SCcharmet_t sm;
	int bytes_to_process;
	int x_base;
	int x_offset;
	int y_offset;
	int x_bit_idx;
	int i; // bitmap byte index
	int j; // bitmap bit index
	pcffont_bmf_t *pPcfFont;

	pPcfFont = &pcfFonts[font];

	pres_bmfbm(u, pPcfFont, &bitmap, &Cmetrics);
	if (bitmap == NULL)
	{
	    return -1;
	}

	//sprintf(msg,"char:%d,width:%d,LSBearing:%d,RSBearding:%d,widthBytes:%d,height:%d\n",u,Cmetrics.width,Cmetrics.LSBearing,Cmetrics.RSBearing,Cmetrics.widthBytes,Cmetrics.height);
	//msg_info(msg);

	if(u==32)
	{
	   x += Cmetrics.LSBearing + Cmetrics.width + 1;
	   return x;
	}

	bytes_to_process = Cmetrics.widthBytes * Cmetrics.height;

	x_base = x + Cmetrics.LSBearing;
	x_offset = 0;
	y_offset = pPcfFont->Fmetrics.linespace - (pPcfFont->Fmetrics.descent + Cmetrics.ascent);


	x_bit_idx = x_base & 0x07;

	if (x + Cmetrics.LSBearing + Cmetrics.width >= LCD_BUF_WIDTH_PIXELS)
		return -1;

	for (i = 0; i < bytes_to_process; i++)
	{
		j = 7;
		while (j >= 0)
		{
			if (x_offset >= Cmetrics.widthBits)
			{
				x_offset = 0;
				y_offset++;
			}
			if (x_offset < Cmetrics.width)
			{
				if (bitmap[i] & (1 << j))
				{
					if (inverted)
						lcd_clear_framebuffer_pixel(x_base + x_offset, y+y_offset);
					else
						lcd_set_framebuffer_pixel(x_base + x_offset, y+y_offset);
				}

			}
			x_offset++;
			x_bit_idx++;
			if (!(x_bit_idx & 0x07))
			{
				x_bit_idx = 0;

			}
			j--;
		}
	}
	x += Cmetrics.LSBearing + Cmetrics.width + 1;
	return x;
}

int buf_draw_bmf_char(char *buf, ucs4_t u,int font,int x,int y, int inverted)
{
	bmf_bm_t *bitmap;
	charmetric_bmf Cmetrics;
	//pcf_SCcharmet_t sm;
	int bytes_to_process;
	int x_base;
	int x_offset;
	int y_offset;
	int x_bit_idx;
	int i; // bitmap byte index
	int j; // bitmap bit index
	pcffont_bmf_t *pPcfFont;

	pPcfFont = &pcfFonts[font];

	pres_bmfbm(u, pPcfFont, &bitmap, &Cmetrics);
	if (bitmap == NULL)
	{
	    return -1;
	}

	//sprintf(msg,"char:%d,width:%d,LSBearing:%d,RSBearding:%d,widthBytes:%d,height:%d\n",u,Cmetrics.width,Cmetrics.LSBearing,Cmetrics.RSBearing,Cmetrics.widthBytes,Cmetrics.height);
	//msg_info(msg);

	if(u==32)
	{
	   x += Cmetrics.LSBearing + Cmetrics.width + 1;
	   return x;
	}

	bytes_to_process = Cmetrics.widthBytes * Cmetrics.height;

	x_base = x + Cmetrics.LSBearing;
	x_offset = 0;
	y_offset = pPcfFont->Fmetrics.linespace - (pPcfFont->Fmetrics.descent + Cmetrics.ascent);


	x_bit_idx = x_base & 0x07;

	if (x + Cmetrics.LSBearing + Cmetrics.width >= LCD_BUF_WIDTH_PIXELS)
		return -1;

	for (i = 0; i < bytes_to_process; i++)
	{
		j = 7;
		while (j >= 0)
		{
			if (x_offset >= Cmetrics.widthBits)
			{
				x_offset = 0;
				y_offset++;
			}
			if (x_offset < Cmetrics.width)
			{
				if (bitmap[i] & (1 << j))
				{
					if (inverted)
					{
						unsigned int byte = ((x_base+x_offset) + LCD_VRAM_WIDTH_PIXELS * (y+y_offset)) / 8;
						unsigned int bit  = ((x_base+x_offset) + LCD_VRAM_WIDTH_PIXELS * (y+y_offset)) % 8;
						buf[byte] ^= (1 << (7 - bit));
					}
					else
					{
						unsigned int byte = ((x_base+x_offset) + LCD_VRAM_WIDTH_PIXELS * (y+y_offset)) / 8;
						unsigned int bit  = ((x_base+x_offset) + LCD_VRAM_WIDTH_PIXELS * (y+y_offset)) % 8;
						buf[byte] |= (1 << (7 - bit));
					}
				}

			}
			x_offset++;
			x_bit_idx++;
			if (!(x_bit_idx & 0x07))
			{
				x_bit_idx = 0;

			}
			j--;
		}
	}
	x += Cmetrics.LSBearing + Cmetrics.width + 1;
	return x;
}
#endif

int GetFontLinespace(int font)
{
   return pcfFonts[font+1].Fmetrics.linespace;
}

void msg_on_lcd(int x, int y, char *fmt, ...)
{
#ifdef INCLUDED_FROM_KERNEL
	va_list args;
	char msg[100];
	va_start(args, fmt);
	vsprintf (msg, fmt, args);
	guilib_clear_area(x, y, 239, y+18);
	render_string(DEFAULT_FONT_IDX, x, y, msg, strlen(msg), 0);
	va_end(args);
#endif
}

void msg_on_lcd_clear(int x, int y)
{
#ifdef INCLUDED_FROM_KERNEL
//	guilib_fb_lock();
	guilib_clear_area(x, y, 239, y+18);
//	guilib_fb_unlock();
#endif
}
