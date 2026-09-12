/*
 * Copyright (c) 2013 Todd Mortimer
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <unicode/utf.h>
#include <unicode/ucnv.h>
#include <unicode/ustring.h>
#include <clipboard/clipboard.h>


#include "terminal.h"
#include "preferences.h"

#include "io.h"

/* from ecma48.c */
extern int ecma48_bracketed_paste();

static int master_fd;
static UConverter* tty_conv;
static UErrorCode  tty_conv_err = U_ZERO_ERROR;

static UConverter* utf8_conv;
static UErrorCode  utf8_conv_err = U_ZERO_ERROR;

static char readbuf[READ_BUFFER_SIZE];
static char writebuf[CHARACTER_BUFFER * U8_MAX_LENGTH];
static char* writebufLimit;

int io_init(pref_t *prefs){

	// create converters
	tty_conv  = ucnv_open(prefs->tty_encoding, &tty_conv_err);
	utf8_conv  = ucnv_open("UTF-8", &utf8_conv_err);
	return TERM_SUCCESS;
}

void io_uninit(){

	// free converts
	ucnv_close(tty_conv);
	ucnv_close(utf8_conv);
  close(master_fd);
}

void io_set_master(int fd){
	master_fd = fd;
}

int io_get_master(){
	return master_fd;
}

/* The last thing we wrote is still in writebuf.
 * This function will set buf to the character _after_ the
 * last output character, which lets the caller find the
 * upcase length. The return value is the length of the
 * last written string, which tells the caller how many
 * backspaces to send in order to erase it.
 * A return value of 0 indicates no upcase representation,
 * or an error occurred. */
int32_t io_upcase_last_write(UChar **buf, int32_t nUChar){
	UChar c[CHARACTER_BUFFER];
	UChar *target;
  UChar *targetLimit;
	const char *source;
	const char *sourceLimit;
  int32_t uCaseLen;
  int32_t lCaseLen;
  UErrorCode uCaseErr = U_ZERO_ERROR;

	/* Convert from output encoding to UChar first, then upcase */
	target = c;
	targetLimit = c + CHARACTER_BUFFER;
	source = writebuf;
	sourceLimit = writebufLimit;
	ucnv_toUnicode(tty_conv, &target, targetLimit, &source, sourceLimit, NULL, FALSE, &tty_conv_err);
	if(tty_conv_err == U_BUFFER_OVERFLOW_ERROR){
		fprintf(stderr, "ucnv_toUnicode() in io_upcase_last_write ran out of target buffer\n");
		tty_conv_err = U_ZERO_ERROR;
	}
	lCaseLen = (int32_t)(target - c);
	uCaseLen = u_strToUpper(*buf, nUChar, c, lCaseLen, NULL, &uCaseErr);
	if (uCaseLen > nUChar){
		// output was truncated
		uCaseLen = nUChar;
	}
	/* If we got something */ //, and it isn't the same as before */
	if ((uCaseLen > 0)     ){ // && (u_strCompare(*buf, uCaseLen, c, lCaseLen, TRUE) != 0)){
		// point _after_ the last UChar
		*buf = *buf + uCaseLen;
		return lCaseLen;
	}
	// no upcase value / error / no dice..
	return 0;
}

/* The master pty is O_NONBLOCK, so a write can come up short - or fail
 * outright with EAGAIN - whenever the child is not draining its end fast
 * enough. Ignoring that loses the tail of a key sequence (an arrow key
 * arriving as a bare ESC) or most of a paste, which reads as a dead
 * keyboard. Retry until the buffer is gone, with a bound so a child that
 * has stopped reading for good cannot wedge the caller: this runs on the
 * UI thread with the input lock held. */
#define WRITE_RETRY_USEC  1000
#define WRITE_RETRY_LIMIT 200   /* ~200ms */
static ssize_t write_all_master(const char* buf, size_t len){
  size_t off = 0;
  int tries = 0;
  while(off < len){
    ssize_t w = write(master_fd, buf + off, len - off);
    if(w > 0){
      off += (size_t)w;
      tries = 0;
      continue;
    }
    if(w < 0 && (errno == EAGAIN || errno == EINTR)){
      if(++tries > WRITE_RETRY_LIMIT){
        fprintf(stderr, "Gave up writing %d of %d bytes to the pty\n",
                (int)(len - off), (int)len);
        break;
      }
      usleep(WRITE_RETRY_USEC);
      continue;
    }
    /* a real error */
    if(off == 0){
      return w;
    }
    break;
  }
  return (ssize_t)off;
}

ssize_t io_write_master(const UChar *buf, size_t nUChar){
	size_t output_capacity;
	char* output;
	char* target;
	char* targetLimit;
	const UChar* source = buf;
	const UChar* sourceLimit = buf + nUChar;
	ssize_t written;

	/* CHARACTER_BUFFER is intentionally tiny for individual key events, but
	 * native IME submissions can contain thousands of UTF-16 code units.
	 * Allocate those writes to their real worst-case UTF-8 size instead of
	 * silently truncating them to CHARACTER_BUFFER * U8_MAX_LENGTH bytes. */
	output_capacity = nUChar * U8_MAX_LENGTH;
	if(output_capacity == 0){
		return 0;
	}
	output = (nUChar <= CHARACTER_BUFFER) ? writebuf : malloc(output_capacity);
	if(output == NULL){
		return -1;
	}
	target = output;
	targetLimit = output + output_capacity;

	ucnv_fromUnicode(tty_conv, &target, targetLimit, &source, sourceLimit, NULL, TRUE, &tty_conv_err);

  if(tty_conv_err == U_BUFFER_OVERFLOW_ERROR){
	  	fprintf(stderr, "ucnv_fromUnicode() in io_write_master ran out of target buffer\n");
	  	tty_conv_err = U_ZERO_ERROR;
  }

	written = write_all_master(output, (size_t)(target - output));
	if(output == writebuf){
		writebufLimit = target;
	} else {
		/* A long IME/paste submission is not eligible for Meta uppercase of
		 * the last physical keystroke. Avoid retaining a dangling pointer. */
		writebufLimit = writebuf;
		free(output);
	}
	return written;
}

ssize_t io_write_master_char(const char *buf, size_t n){
  return write_all_master(buf, n);
}

ssize_t io_read_master(UChar *buf, size_t nUChar){
	const char *source;
	const char *sourceLimit;
	int32_t count;
  UChar *target;
  UChar *targetLimit;

  /* Read nUChar bytes, which necessarily translates into <= nUChar UChars */
	count = read(master_fd, readbuf, nUChar);
	if(count <= 0){
		return count;
	}
	// else

	source = readbuf;
	sourceLimit = readbuf + count;

	target = buf;
	targetLimit = buf + nUChar;

  ucnv_toUnicode(tty_conv, &target, targetLimit, &source, sourceLimit, NULL, FALSE, &tty_conv_err);

  if(tty_conv_err == U_BUFFER_OVERFLOW_ERROR){
  	fprintf(stderr, "ucnv_toUnicode() in io_read_master ran out of target buffer\n");
  	tty_conv_err = U_ZERO_ERROR;
  }

  return (ssize_t)(target - buf);
}

ssize_t io_read_utf8_string(const char* utf8, size_t utf8len, UChar* buf){

	const char *source;
	const char *sourceLimit;
  UChar *target;
  UChar *targetLimit;


	source = utf8;
	sourceLimit = utf8 + utf8len;

	target = buf;
	targetLimit = buf + utf8len;

  ucnv_toUnicode(utf8_conv, &target, targetLimit, &source, sourceLimit, NULL, TRUE, &utf8_conv_err);

  if(utf8_conv_err == U_BUFFER_OVERFLOW_ERROR){
  	fprintf(stderr, "ucnv_toUnicode() in io_read_utf8_string ran out of target buffer when converting from utf8\n");
  	utf8_conv_err = U_ZERO_ERROR;
  }

  return (ssize_t)(target - buf);
}

/* Convert one complete UTF-8 string to the configured terminal encoding and
 * write it to the PTY.  Dialog responses are already UTF-8; resetting both
 * converters here prevents an earlier malformed sequence from poisoning a
 * later IME submission through ICU's sticky UErrorCode/state. */
ssize_t io_write_utf8_string(const char* utf8, size_t utf8len){
  UChar* unicode;
  ssize_t unicode_len;
  ssize_t written = -1;

  if(utf8 == NULL || utf8len == 0){
    return 0;
  }

  unicode = calloc(utf8len + 1, sizeof(UChar));
  if(unicode == NULL){
    return -1;
  }

  utf8_conv_err = U_ZERO_ERROR;
  ucnv_resetToUnicode(utf8_conv);
  unicode_len = io_read_utf8_string(utf8, utf8len, unicode);
  if(U_SUCCESS(utf8_conv_err) && unicode_len > 0){
    tty_conv_err = U_ZERO_ERROR;
    ucnv_resetFromUnicode(tty_conv);
    written = io_write_master(unicode, (size_t)unicode_len);
  } else {
    fprintf(stderr, "Could not convert IME text from UTF-8: %s\n",
            u_errorName(utf8_conv_err));
  }

  free(unicode);
  return written;
}

void io_paste_from_clipboard(){
  char* buffer = NULL;
  int ret;
  if(is_clipboard_format_present("text/plain") == 0){
    ret = get_clipboard_data("text/plain", &buffer);
    if(ret > 0 && buffer != NULL){
    	/* Paste and Pray. The encoding of the byte stream will be whatever
    	 * the copied source was - hopefully tty_encoding..
    	 */
      if(ecma48_bracketed_paste()){
        /* xterm bracketed paste: let the application distinguish pasted
         * text from typed text */
        write_all_master("\033[200~", 6);
        write_all_master(buffer, ret * sizeof(char));
        write_all_master("\033[201~", 6);
      } else {
        write_all_master(buffer, ret * sizeof(char));
      }
      free(buffer);
    }
  }
}

/* copy UTF-16 text to the system clipboard as UTF-8 */
void io_copy_to_clipboard(const UChar* text, int len){
  UErrorCode err = U_ZERO_ERROR;
  int32_t u8len = 0;
  char* u8 = NULL;

  u_strToUTF8(NULL, 0, &u8len, text, len, &err);
  if(u8len <= 0){
    return;
  }
  u8 = malloc(u8len + 1);
  if(u8 == NULL){
    return;
  }
  err = U_ZERO_ERROR;
  u_strToUTF8(u8, u8len + 1, NULL, text, len, &err);
  if(!U_FAILURE(err)){
    empty_clipboard();
    set_clipboard_data("text/plain", u8len, u8);
  }
  free(u8);
}

/* set the system clipboard to raw (UTF-8) bytes, e.g. from OSC 52 */
void io_set_clipboard_bytes(const char* data, int len){
  if(data != NULL && len > 0){
    empty_clipboard();
    set_clipboard_data("text/plain", len, (char*)data);
  }
}
