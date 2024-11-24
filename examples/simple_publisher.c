/**
 * @file
 * A simple program that publishes the current time in seconds and microseconds whenever ENTER is pressed.
 */

#include <sys/time.h> // For precise timestamping
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <mqtt.h>
#include "templates/posix_sockets.h"

/**
 * @brief Callback function triggered when a PUBLISH message is received.
 *
 * @note Not used in this example.
 */
void publish_callback(void** unused, struct mqtt_response_publish *published);

/**
 * @brief Client refresher to handle incoming and outgoing traffic to/from the broker.
 *
 * @note Runs continuously in a separate thread to call mqtt_sync() every 100 ms.
 */
void* client_refresher(void* client);

/**
 * @brief Safely closes the socket and cancels the client refresher thread before exiting.
 */
void exit_example(int status, int sockfd, pthread_t *client_daemon);

/**
 * @brief Main function to connect to the MQTT broker and publish messages.
 */
int main(int argc, const char *argv[])
{
    const char* addr;  // Broker address
    const char* port;  // Broker port
    const char* topic; // Topic to publish to

    // Get broker address from command line arguments or use the default
    addr = (argc > 1) ? argv[1] : "test.mosquitto.org";

    // Get port from command line arguments or use the default
    port = (argc > 2) ? argv[2] : "1883";

    // Get topic name from command line arguments or use the default
    topic = (argc > 3) ? argv[3] : "datetime";

    // Open a non-blocking socket to connect to the broker
    int sockfd = open_nb_socket(addr, port);
    if (sockfd == -1) {
        perror("Failed to open socket: ");
        exit_example(EXIT_FAILURE, sockfd, NULL);
    }

    // Initialize MQTT client
    struct mqtt_client client;
    uint8_t sendbuf[2048]; // Buffer for outgoing messages
    uint8_t recvbuf[1024]; // Buffer for incoming messages
    mqtt_init(&client, sockfd, sendbuf, sizeof(sendbuf), recvbuf, sizeof(recvbuf), publish_callback);

    // Connect to the broker with clean session flag
    const char* client_id = NULL;
    uint8_t connect_flags = MQTT_CONNECT_CLEAN_SESSION;
    mqtt_connect(&client, client_id, NULL, NULL, 0, NULL, NULL, connect_flags, 400);

    // Check for connection errors
    if (client.error != MQTT_OK) {
        fprintf(stderr, "error: %s\n", mqtt_error_str(client.error));
        exit_example(EXIT_FAILURE, sockfd, NULL);
    }

    // Start a thread to handle MQTT client traffic
    pthread_t client_daemon;
    if (pthread_create(&client_daemon, NULL, client_refresher, &client)) {
        fprintf(stderr, "Failed to start client daemon.\n");
        exit_example(EXIT_FAILURE, sockfd, NULL);
    }

    // Prompt user for input to publish messages
    printf("%s is ready to begin publishing the time.\n", argv[0]);
    printf("Press ENTER to publish the current time.\n");
    printf("Press CTRL-D (or any other key) to exit.\n\n");

    // Publish messages on ENTER press
    while (fgetc(stdin) == '\n') {
        struct timeval tv;
        gettimeofday(&tv, NULL); // Get current time in seconds and microseconds
        char application_message[256];
        snprintf(application_message, sizeof(application_message), "%ld.%06ld", tv.tv_sec, tv.tv_usec);
        printf("%s published: \"%s\"\n", argv[0], application_message);

        mqtt_publish(&client, topic, application_message, strlen(application_message) + 1, MQTT_PUBLISH_QOS_0);

        // Check for publishing errors
        if (client.error != MQTT_OK) {
            fprintf(stderr, "error: %s\n", mqtt_error_str(client.error));
            exit_example(EXIT_FAILURE, sockfd, &client_daemon);
        }
    }

    // Disconnect from the broker
    printf("\n%s disconnecting from %s\n", argv[0], addr);
    sleep(1);

    // Clean up and exit
    exit_example(EXIT_SUCCESS, sockfd, &client_daemon);
}

/**
 * @brief Closes the socket and cancels the refresher thread, then exits.
 */
void exit_example(int status, int sockfd, pthread_t *client_daemon)
{
    if (sockfd != -1) close(sockfd);
    if (client_daemon != NULL) pthread_cancel(*client_daemon);
    exit(status);
}

/**
 * @brief Callback for received PUBLISH messages (unused in this example).
 */
void publish_callback(void** unused, struct mqtt_response_publish *published)
{
    /* Not used in this example */
}

/**
 * @brief Handles periodic MQTT traffic processing.
 */
void* client_refresher(void* client)
{
    while (1) {
        mqtt_sync((struct mqtt_client*) client);
        usleep(100000U); // Refresh every 100 ms
    }
    return NULL;
}
