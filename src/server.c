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
#include <fcntl.h>
#include <poll.h>

#include "types.h"
#include "util.h"
#include "queue.h"

/* --- Globals --- */
// Server callbacks
static spotd_server_callbacks *g_callbacks;
// Server thread id
static pthread_t g_server_thread_id;
// Self-pipe, for the event when the server needs to be stopped
static int g_self_pipe[2];
// A list of client threads
static LIST_HEAD(, client_thread) g_client_threads;
// Client thread list mutex
static pthread_mutex_t g_client_threads_mutex;

/* --- Function definitions --- */
static void *server_thread(void *socket_desc);
static int create_new_client_thread(int client_sock_desc);
static void *connection_handler(void *socket_desc);
static spotd_command *parse_client_message(char *client_message);

/* -- Functions --- */

/**
 * Start the SPOTD server
 *
 * @param  port  The port to start the server on
 * @param  callbacks  The callbacks struct, to receive commands from clients
 * @return  returns a spotd_error
 */
spotd_error spotd_server_start(int port, spotd_server_callbacks *callbacks) {
  int socket_desc, *socket_desc_copy;
  int yes = 1;
  struct sockaddr_in server;

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

  if (pthread_create(&g_server_thread_id, NULL, server_thread, (void*) socket_desc_copy) < 0) {
    perror("could not create thread");
    return SPOTD_ERROR_OTHER_PERMANENT;
  }

  return SPOTD_ERROR_OK;
}

/**
 * Stops the currently running server. Blocks until the server is stopped.
 */
void spotd_server_stop() {
  write(g_self_pipe[1], "STOP", 4);
  pthread_join(g_server_thread_id, NULL);
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
  struct pollfd pfds[2];

  free(socket_desc);

  // Create a pipe
  pipe(&g_self_pipe[0]);
  // Make the read end non-blocking
  fcntl(g_self_pipe[0], F_SETFL, O_NONBLOCK);

  // Make the server socket non-blocking
  fcntl(sock, F_SETFL, O_NONBLOCK);

  // Initialize the list of threads
  LIST_INIT(&g_client_threads);
  pthread_mutex_init(&g_client_threads_mutex, NULL);

  // Listen
  listen(sock, 3);

  // Accept incoming connections
  puts("Waiting for incoming connections...");
  c = sizeof(struct sockaddr_in);

  // The main server polling loop
  for (;;) {
    // Setup server socket polling
    pfds[0].fd = sock;
    pfds[0].events = POLLIN;
    // Setup pipe polling
    pfds[1].fd = g_self_pipe[0];
    pfds[1].events = POLLIN;

    // Poll for events
    poll(&pfds[0], 2, -1);

    if (pfds[1].revents) {
      // There's an event in the pipe, stop the server
      puts("Stopping server...");
      break;
    } else if (pfds[0].revents) {
      // There are events in the server socket, accept a connection
      client_sock = accept(sock, (struct sockaddr *)&client, (socklen_t*)&c);

      if (client_sock < 0) {
        // Accept failed, continue the loop
        continue;
      }

      puts("Connection accepted");

      if (create_new_client_thread(client_sock)) {
        perror("could not create thread");
      }

      puts("Handler assigned");
    }
  }

  // Join all client threads
  puts("Waiting for client threads to stop...");

  client_thread_t *first_thread;
  pthread_t first_thread_id;
  for (;;) {
    pthread_mutex_lock(&g_client_threads_mutex);
    first_thread = LIST_FIRST(&g_client_threads);
    
    if (first_thread != NULL) {
      first_thread_id = first_thread->thread_id;
    } else {
      // All threads joined
      pthread_mutex_unlock(&g_client_threads_mutex);
      break;
    }
    
    pthread_mutex_unlock(&g_client_threads_mutex);

    printf("Joining thread %u...\n", first_thread_id);
    pthread_join(first_thread_id, NULL);
  }

  puts("Server stopped...");

  // Cleanup
  close(g_self_pipe[1]);
  close(g_self_pipe[0]);
  close(sock);

  // Stop the thread
  pthread_exit(NULL);
}

/**
 * Create a new client thread
 *
 * @param  client_sock_desc  The client socket descriptor
 * @return  int  The error code returned by pthread_create()
 */
static int create_new_client_thread(int client_sock_desc) {
  int pthread_return_code;

  // Create a client thread object
  client_thread_t *new_client_thread;
  new_client_thread = (client_thread_t*) malloc(sizeof(client_thread_t));
  new_client_thread->socket_desc = client_sock_desc;

  // Insert to the list of client threads
  pthread_mutex_lock(&g_client_threads_mutex);
  LIST_INSERT_HEAD(&g_client_threads, new_client_thread, link);
  pthread_mutex_unlock(&g_client_threads_mutex);

  // Create a new thread for the client
  pthread_return_code = pthread_create(&new_client_thread->thread_id, NULL, connection_handler, (void*) new_client_thread);
  if (pthread_return_code < 0) {
    // Cleanup if thread creation fails
    pthread_mutex_lock(&g_client_threads_mutex);
    LIST_REMOVE(new_client_thread, link);
    pthread_mutex_unlock(&g_client_threads_mutex);
    free(new_client_thread);
  }

  return pthread_return_code;
}

/**
 * Handle connections
 *
 * @param  socket_desc  The client socket descriptor
 */
static void *connection_handler(void *thread_void_ptr) {
  client_thread_t *thread = (client_thread_t*) thread_void_ptr;
  int sock = thread->socket_desc;
  int read_size;
  char message_buf[2000], client_message[2000], *message;
  spotd_command *command;

  struct pollfd pfds[2];

  // Detach the thread, so that the resources a freed to the system upon
  // termination, without the need to pthread_join
  pthread_detach(pthread_self());

  // Make the client socket non-blocking
  fcntl(sock, F_SETFL, O_NONBLOCK);

  // Send the greetings message to the client
  snprintf(message_buf, 2000, "spotd v%s\n", VERSION);
  write(sock, message_buf, strlen(message_buf));

  // The main client polling loop
  for (;;) {
    // Setup client socket polling
    pfds[0].fd = sock;
    pfds[0].events = POLLIN;
    // Setup pipe polling
    pfds[1].fd = g_self_pipe[0];
    pfds[1].events = POLLIN;

    // Poll for events
    poll(&pfds[0], 2, -1);

    if (pfds[1].revents) {
      // There's an event in the pipe, stop the polling loop
      puts("Disconnecting client...");
      break;
    } else if (pfds[0].revents) {
      // There are events in the client socket, receive the message from the client
      read_size = recv(sock, client_message, 2000, 0);

      // Check for errors
      if (read_size == 0) {
        puts("Client disconnected");
        break;
      } else if (read_size < 0) {
        perror("recv failed");
        break;
      }

      // Add the end of string marker
      client_message[read_size] = '\0';

      // Try to parse a command from the client message
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
  }

  // Cleanup
  close(sock);

  pthread_mutex_lock(&g_client_threads_mutex);
  LIST_REMOVE(thread, link);
  pthread_mutex_unlock(&g_client_threads_mutex);

  free(thread);

  // Stop the thread
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
  // Strip the message of \r and \n chars
  char *stripped_message = strip_str(client_message, "\r\n");
  int message_length = strlen(stripped_message);
  spotd_command *command = NULL;
  char **arguments;

  // Check if the message is a valid command
  if (strncmp(stripped_message, "PLAY ", 5) == 0) {
    // The PLAY command has one argument, allocate memory for it
    arguments = (char**) malloc(1 * sizeof(char*));
    char *track_name = (char *) malloc(message_length - 5 + 1);

    // Copy the argument and add it to the arguments array
    strncpy(track_name, stripped_message + 5, message_length - 5);
    track_name[message_length - 5] = '\0';
    arguments[0] = track_name;

    // Create the PLAY command object
    command = spotd_command_create(SPOTD_COMMAND_PLAY_TRACK, 1, arguments);
  }

  // Cleanup
  free(stripped_message);

  return command;
}
