#include <sys/time.h>    // Для получения текущего времени
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <mqtt.h>
#include "templates/posix_sockets.h"

// Глобальное время для хранения предыдущего момента
struct timeval previous_time = {0, 0};

// Функция обработки полученных сообщений
void publish_callback(void** unused, struct mqtt_response_publish *published);

// Поток для регулярного обновления MQTT-клиента
void* client_refresher(void* client);

// Завершение программы с закрытием сокета и остановкой потока
void exit_example(int status, int sockfd, pthread_t *client_daemon);

int main(int argc, const char *argv[]) {
    const char* addr = "localhost";  // Адрес локального брокера
    const char* port = "1883";       // Порт Mosquitto
    const char* topic = "memory_info"; // Тема для подписки

    int sockfd = open_nb_socket(addr, port);
    if (sockfd == -1) {
        perror("Ошибка подключения к брокеру: ");
        exit(EXIT_FAILURE);
    }

    struct mqtt_client client;
    uint8_t sendbuf[2048]; // Буфер для исходящих сообщений
    uint8_t recvbuf[1024]; // Буфер для входящих сообщений
    mqtt_init(&client, sockfd, sendbuf, sizeof(sendbuf), recvbuf, sizeof(recvbuf), publish_callback);

    mqtt_connect(&client, NULL, NULL, NULL, 0, NULL, NULL, MQTT_CONNECT_CLEAN_SESSION, 400);

    if (client.error != MQTT_OK) {
        fprintf(stderr, "Ошибка MQTT: %s\n", mqtt_error_str(client.error));
        exit_example(EXIT_FAILURE, sockfd, NULL);
    }

    pthread_t client_daemon;
    if (pthread_create(&client_daemon, NULL, client_refresher, &client)) {
        fprintf(stderr, "Ошибка запуска потока для клиента.\n");
        exit_example(EXIT_FAILURE, sockfd, NULL);
    }

    mqtt_subscribe(&client, topic, 0);

    printf("Подписчик готов. Ожидание сообщений...\n");

    while (1) pause(); // Блокировка программы, ожидание сообщений
}

void publish_callback(void** unused, struct mqtt_response_publish *published) {
    struct timeval current_time;

    char topic_name[256];
    snprintf(topic_name, published->topic_name_size + 1, "%.*s", (int)published->topic_name_size, published->topic_name);

    char message[256];
    snprintf(message, published->application_message_size + 1, "%.*s", (int)published->application_message_size, published->application_message);

    // Получение текущего времени
    gettimeofday(&current_time, NULL);

    printf("Получено сообщение на теме '%s': %s\n", topic_name, message);

    // Если это не первое сообщение, вычисляем задержку
    if (previous_time.tv_sec != 0 || previous_time.tv_usec != 0) {
        long delay_sec = current_time.tv_sec - previous_time.tv_sec;
        long delay_usec = current_time.tv_usec - previous_time.tv_usec;

        if (delay_usec < 0) {
            delay_sec -= 1;
            delay_usec += 1000000;
        }

        printf("Задержка между сообщениями: %ld.%06ld секунд\n", delay_sec, delay_usec);
    } else {
        printf("Первое сообщение - задержка не вычисляется.\n");
    }

    // Обновляем предыдущее время
    previous_time = current_time;
}

void* client_refresher(void* client) {
    while (1) {
        mqtt_sync((struct mqtt_client*) client);
        usleep(100000U);
    }
    return NULL;
}

void exit_example(int status, int sockfd, pthread_t *client_daemon) {
    if (sockfd != -1) close(sockfd);
    if (client_daemon != NULL) pthread_cancel(*client_daemon);
    exit(status);
}
