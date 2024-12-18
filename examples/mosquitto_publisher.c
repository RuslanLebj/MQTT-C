#include <sys/sysinfo.h> // Для получения информации о памяти
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <mqtt.h>
#include "templates/posix_sockets.h"

void* client_refresher(void* client);
void exit_example(int status, int sockfd, pthread_t *client_daemon);

int main(int argc, const char *argv[]) {
    const char* addr = "localhost";  // Адрес локального брокера
    const char* port = "1883";       // Порт Mosquitto
    const char* topic = "memory_info"; // Тема для публикации

    // Открытие неблокирующего сокета
    int sockfd = open_nb_socket(addr, port);
    if (sockfd == -1) {
        perror("Ошибка подключения к брокеру: ");
        exit(EXIT_FAILURE);
    }

    // Инициализация MQTT клиента
    struct mqtt_client client;
    uint8_t sendbuf[2048]; // Буфер для исходящих сообщений
    uint8_t recvbuf[1024]; // Буфер для входящих сообщений
    mqtt_init(&client, sockfd, sendbuf, sizeof(sendbuf), recvbuf, sizeof(recvbuf), NULL);

    // Подключение к брокеру
    mqtt_connect(&client, NULL, NULL, NULL, 0, NULL, NULL, MQTT_CONNECT_CLEAN_SESSION, 400);

    if (client.error != MQTT_OK) {
        fprintf(stderr, "Ошибка MQTT: %s\n", mqtt_error_str(client.error));
        exit_example(EXIT_FAILURE, sockfd, NULL);
    }

    // Запуск отдельного потока для синхронизации клиента
    pthread_t client_daemon;
    if (pthread_create(&client_daemon, NULL, client_refresher, &client)) {
        fprintf(stderr, "Ошибка запуска потока для клиента.\n");
        exit_example(EXIT_FAILURE, sockfd, NULL);
    }

    printf("Публикатор готов. Публикация информации о памяти каждые 5 секунд.\n");

    while (1) {
        struct sysinfo info;
        sysinfo(&info); // Получение информации о системе
        long free_mem = info.freeram / 1024; // Свободная память в КБ

        char message[256];
        snprintf(message, sizeof(message), "Free memory: %ld KB", free_mem);

        mqtt_publish(&client, topic, message, strlen(message) + 1, MQTT_PUBLISH_QOS_0);
        printf("Опубликовано: %s\n", message);

        sleep(5); // Задержка перед следующей публикацией
    }

    exit_example(EXIT_SUCCESS, sockfd, &client_daemon);
}

void* client_refresher(void* client) {
    while (1) {
        mqtt_sync((struct mqtt_client*) client);
        usleep(100000U); // Синхронизация каждые 100 мс
    }
    return NULL;
}

void exit_example(int status, int sockfd, pthread_t *client_daemon) {
    if (sockfd != -1) close(sockfd);
    if (client_daemon != NULL) pthread_cancel(*client_daemon);
    exit(status);
}
