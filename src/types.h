/*
 * The MIT License (MIT)
 * 
 * Copyright (c) 2015 Mantas Norvaiša
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * This file is part of spotd.
 */

#ifndef _SPOTD_TYPES_H_
#define _SPOTD_TYPES_H_

typedef enum spotd_error {
  SPOTD_ERROR_OK              = 0, // No errors encountered
  SPOTD_ERROR_BIND_FAILED     = 1, // Server bind call failed
  SPOTD_ERROR_OTHER_PERMANENT = 2, // Some other error occurred, and it is permanent
  SPOTD_ERROR_INVALID_LINK    = 3  // Invalid Spotify link
} spotd_error;

typedef enum spotd_command_type {
  SPOTD_COMMAND_PLAY_TRACK = 0 // Play a given track
} spotd_command_type;

typedef struct spotd_command {
  spotd_command_type type;
  int argc;
  char **argv;
} spotd_command;

spotd_command *spotd_command_create(spotd_command_type type, int argc, char **argv);
void spotd_command_release(spotd_command *command);

#endif /* _SPOTD_TYPES_H_ */
