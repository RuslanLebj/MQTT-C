/**
 * @file
 * A simple program that subscribes to a topic and calculates the delay of received messages.
 */

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <sys/time.h>
#include <mqtt.h>
#include "templates/posix_sockets.h"

/**
 * @brief The function will be called whenever a PUBLISH message is received.
 *
 * @param unused Unused parameter.
 * @param published The received publish message containing topic and payload.
 */
void publish_callback(void** unused, struct mqtt_response_publish *published);

/**
 * @brief Client refresher that handles incoming and outgoing traffic to/from the broker.
 *
 * @note Runs continuously in a separate thread to call mqtt_sync() every 100 ms.
 *
 * @param client Pointer to the mqtt_client structure.
 * @return NULL
 */
void* client_refresher(void* client);

/**
 * @brief Safely closes the socket and cancels the refresher thread before exiting.
 *
 * @param status Exit status code.
 * @param sockfd Socket file descriptor to close.
 * @param client_daemon Pointer to the thread managing the client refresher.
 */
void exit_example(int status, int sockfd, pthread_t *client_daemon);

int main(int argc, const char *argv[])
{
    const char* addr;  // MQTT broker address
    const char* port;  // MQTT broker port
    const char* topic; // Topic to subscribe to

    // Get broker address from command-line arguments or use the default
    addr = (argc > 1) ? argv[1] : "test.mosquitto.org";

    // Get port from command-line arguments or use the default
    port = (argc > 2) ? argv[2] : "1883";

    // Get topic name from command-line arguments or use the default
    topic = (argc > 3) ? argv[3] : "datetime";

    // Open a non-blocking socket to connect to the MQTT broker
    int sockfd = open_nb_socket(addr, port);
    if (sockfd == -1) {
        perror("Failed to open socket: ");
        exit_example(EXIT_FAILURE, sockfd, NULL);
    }

    // Initialize the MQTT client
    struct mqtt_client client;
    uint8_t sendbuf[2048]; // Buffer for outgoing messages
    uint8_t recvbuf[1024]; // Buffer for incoming messages
    mqtt_init(&client, sockfd, sendbuf, sizeof(sendbuf), recvbuf, sizeof(recvbuf), publish_callback);

    // Create an anonymous session
    const char* client_id = NULL;
    uint8_t connect_flags = MQTT_CONNECT_CLEAN_SESSION; // Clean session flag
    mqtt_connect(&client, client_id, NULL, NULL, 0, NULL, NULL, connect_flags, 400);

    // Check for connection errors
    if (client.error != MQTT_OK) {
        fprintf(stderr, "error: %s\n", mqtt_error_str(client.error));
        exit_example(EXIT_FAILURE, sockfd, NULL);
    }

    // Start a thread to manage client traffic
    pthread_t client_daemon;
    if (pthread_create(&client_daemon, NULL, client_refresher, &client)) {
        fprintf(stderr, "Failed to start client daemon.\n");
        exit_example(EXIT_FAILURE, sockfd, NULL);
    }

    // Subscribe to the specified topic
    mqtt_subscribe(&client, topic, 0);

    printf("%s is listening for '%s' messages.\n", argv[0], topic);
    printf("Press CTRL-D to exit.\n\n");

    // Block until the user presses CTRL-D
    while (fgetc(stdin) != EOF);

    // Disconnect from the broker
    printf("\n%s disconnecting from %s\n", argv[0], addr);
    sleep(1);

    // Clean up and exit
    exit_example(EXIT_SUCCESS, sockfd, &client_daemon);
}

/**
 * @brief Callback function to process received messages.
 *
 * @param unused Unused parameter.
 * @param published Pointer to the received publish message.
 */
void publish_callback(void** unused, struct mqtt_response_publish *published)
{
    // Convert the topic name to a C-string (not null-terminated by default)
    char* topic_name = (char*) malloc(published->topic_name_size + 1);
    memcpy(topic_name, published->topic_name, published->topic_name_size);
    topic_name[published->topic_name_size] = '\0';

    // Get the current time
    struct timeval current_time;
    gettimeofday(&current_time, NULL);

    // Extract the sent time from the message payload
    char* message = (char*) malloc(published->application_message_size + 1);
    memcpy(message, published->application_message, published->application_message_size);
    message[published->application_message_size] = '\0';

    long sent_sec, sent_usec;
    sscanf(message, "%ld.%06ld", &sent_sec, &sent_usec);

    // Calculate the delay between sent and received times
    long delay_sec = current_time.tv_sec - sent_sec;
    long delay_usec = current_time.tv_usec - sent_usec;

    if (delay_usec < 0) {
        delay_sec -= 1;
        delay_usec += 1000000;
    }

    // Print the received message and delay
    printf("Received publish('%s'): %s\n", topic_name, message);
    printf("Message delay: %ld.%06ld seconds\n", delay_sec, delay_usec);

    free(topic_name);
    free(message);
}

/**
 * @brief Periodically synchronizes the MQTT client to handle traffic.
 *
 * @param client Pointer to the mqtt_client structure.
 * @return NULL
 */
void* client_refresher(void* client)
{
    while (1) {
        mqtt_sync((struct mqtt_client*) client);
        usleep(100000U); // Synchronize every 100 ms
    }
    return NULL;
}

/**
 * @brief Safely closes the socket, cancels the thread, and exits the program.
 *
 * @param status Exit status code.
 * @param sockfd Socket file descriptor to close.
 * @param client_daemon Pointer to the thread managing the client refresher.
 */
void exit_example(int status, int sockfd, pthread_t *client_daemon)
{
    if (sockfd != -1) close(sockfd);
    if (client_daemon != NULL) pthread_cancel(*client_daemon);
    exit(status);
}