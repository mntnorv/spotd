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

#include "types.h"

#include <stdlib.h>

/**
 * Create a new spotd_command object
 *
 * @param  type  Type of the command
 * @param  argc  Number of arguments
 * @param  argv  The array of arguments. Both the array and each argument need
 *   to be allocated with malloc(), because they will be freed with free() when
 *   spotd_command_release is called.
 * @return  The new spotd_command. The command must be freed with
 *   spotd_command_release() when it is not used anymore.
 */
spotd_command *spotd_command_create(spotd_command_type type, int argc, char **argv) {
  spotd_command *command;

  command = (spotd_command *) malloc(sizeof(spotd_command));
  command->type = type;
  command->argc = argc;
  command->argv = argv;

  return command;
}

/**
 * Free the memory allocated for a spotd_command.
 *
 * @param  command  The command to free
 */
void spotd_command_release(spotd_command *command) {
  int i;

  if (command->argv != NULL) {
    for(i = 0; i < command->argc; i++) {
      free(command->argv[i]);
    }
    free(command->argv);
  }

  free(command);
}
