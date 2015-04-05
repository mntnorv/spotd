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

#include "server.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>

#include "types.h"
#include "util.h"

/* --- Globals --- */
static spotd_server_callbacks *g_callbacks;

/* --- Function definitions --- */
static spotd_command *parse_client_message(char *client_message);
static void *server_thread(void *socket_desc);
static void *connection_handler(void *socket_desc);

/* -- Functions --- */

/**
 * Start the SPOTD server
 *
 * @param  port  The port to start the server on
 * @param  callbacks  The callbacks struct, to receive commands from clients
 * @return  returns a spotd_error
 */
spotd_error start_server(int port, spotd_server_callbacks *callbacks) {
  int socket_desc, *socket_desc_copy;
  int yes = 1;
  struct sockaddr_in server;
  pthread_t server_thread_id;

  g_callbacks = callbacks;

  // Create socket
  socket_desc = socket(AF_INET, SOCK_STREAM, 0);
  if (socket_desc == -1) {
    perror("Could not create socket");
    return SPOTD_ERROR_OTHER_PERMANENT;
  }
  puts("Socket created");

  // Set socket options
  if (setsockopt(socket_desc, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
    perror("Failed setting server socket options");
    return SPOTD_ERROR_OTHER_PERMANENT;
  }

  // Prepare the sockaddr_in structure
  server.sin_family = AF_INET;
  server.sin_addr.s_addr = INADDR_ANY;
  server.sin_port = htons( port );
  memset(&(server.sin_zero), 0, 8);

  // Bind
  if (bind(socket_desc, (struct sockaddr *)&server, sizeof(server)) < 0) {
    // Print the error message
    perror("Bind failed. Error");
    return SPOTD_ERROR_BIND_FAILED;
  }
  puts("bind done");

  // Start the server thread
  socket_desc_copy = (int *)malloc(sizeof(int));
  *socket_desc_copy = socket_desc;

  if (pthread_create(&server_thread_id, NULL, server_thread, (void*) socket_desc_copy) < 0) {
    perror("could not create thread");
    return SPOTD_ERROR_OTHER_PERMANENT;
  }

  return SPOTD_ERROR_OK;
}

/**
 * Start the server thread
 *
 * @param  socket_desc  The server socket descriptor
 */
static void *server_thread(void *socket_desc) {
  int sock = *(int*)socket_desc;
  int client_sock, c;
  struct sockaddr_in client;
  pthread_t last_client_thread_id;

  free(socket_desc);

  // Listen
  listen(sock, 3);

  // Accept incoming connections
  puts("Waiting for incoming connections...");
  c = sizeof(struct sockaddr_in);

  while ((client_sock = accept(sock, (struct sockaddr *)&client, (socklen_t*)&c))) {
    puts("Connection accepted");

    int *client_sock_copy = (int *)malloc(sizeof(int));
    *client_sock_copy = client_sock;

    if (pthread_create(&last_client_thread_id, NULL, connection_handler, (void*) client_sock_copy) < 0) {
      perror("could not create thread");
      pthread_exit(NULL);
    }

    // Now join the thread, so that we dont terminate before the thread
    // pthread_join(thread_id, NULL);
    puts("Handler assigned");
  }

  if (client_sock < 0) {
    perror("accept failed");
    pthread_exit(NULL);
  }

  pthread_exit(NULL);
}

/**
 * Handle connections
 *
 * @param  socket_desc  The client socket descriptor
 */
static void *connection_handler(void *socket_desc) {
  // Get the socket descriptor
  int sock = *(int*)socket_desc;
  int read_size;
  char message_buf[2000], client_message[2000], *message;
  spotd_command *command;

  free(socket_desc);
  pthread_detach(pthread_self());

  // Send some messages to the client
  snprintf(message_buf, 2000, "spotd v%s\n", VERSION);
  write(sock, message_buf, strlen(message_buf));

  // Receive a message from client
  while((read_size = recv(sock, client_message, 2000, 0)) > 0) {
    // end of string marker
    client_message[read_size] = '\0';

    command = parse_client_message(client_message);

    if (command != NULL) {
      // Pass the message to a callback, if it is set
      if (g_callbacks->command_received != NULL) {
        g_callbacks->command_received(command);
      }

      // Send ok response
      message = "OK\n";
      write(sock, message, strlen(message));
    } else {
      // Send invalid command response
      message = "INVALID COMMAND\n";
      write(sock, message, strlen(message));
    }

    // clear the message buffer
    memset(client_message, 0, 2000);
  }

  if (read_size == 0) {
    puts("Client disconnected");
    fflush(stdout);
  } else if(read_size == -1) {
    perror("recv failed");
  }

  close(sock);
  pthread_exit(NULL);
}

/**
 * Parse a client message
 *
 * @param  client_message  The client message to parse
 * @return  The parsed command if the message contained a valid command, NULL
 *   otherwise. The resulting command must be freed with spotd_command_release().
 */
static spotd_command *parse_client_message(char *client_message) {
  char *stripped_message = strip_str(client_message, "\r\n");
  int message_length = strlen(stripped_message);
  spotd_command *command = NULL;
  char **arguments;

  if (strncmp(stripped_message, "PLAY ", 5) == 0) {
    arguments = (char**) malloc(1 * sizeof(char*));
    char *track_name = (char *) malloc(message_length - 5 + 1);

    strncpy(track_name, stripped_message + 5, message_length - 5);
    track_name[message_length - 5] = '\0';
    arguments[0] = track_name;

    command = spotd_command_create(SPOTD_COMMAND_PLAY_TRACK, 1, arguments);
  }

  free(stripped_message);

  return command;
}
