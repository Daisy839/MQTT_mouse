#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h> 
#include <time.h>
#include <linux/input-event-codes.h>
#include <linux/input.h>
#include <math.h>
#include <stdbool.h>
#include "MQTTClient.h"

// ====== Cấu hình DPI và tỉ lệ quy đổi từ Mickey sang cm ======
#define DPI_X 800
#define DPI_Y 800
#define MICKEY_TO_CM_X (2.54 / DPI_X)
#define MICKEY_TO_CM_Y (2.54 / DPI_Y)

// ====== Cấu hình MQTT ======
#define ADDRESS     "tcp://broker.emqx.io:1883"
#define CLIENTID    "publisher_demo"
#define PUB_TOPIC   "cdt/topic1"
#define event_name  "/dev/input/event5"   // File thiết bị chuột

// ====== Hàm gửi dữ liệu lên MQTT ======
void publish(MQTTClient client, char* topic, char* payload) {
    MQTTClient_message pubmsg = MQTTClient_message_initializer;
    pubmsg.payload = payload;
    pubmsg.payloadlen = strlen(pubmsg.payload);
    pubmsg.qos = 1;
    pubmsg.retained = 0;
    MQTTClient_deliveryToken token;

    MQTTClient_publishMessage(client, topic, &pubmsg, &token);
    MQTTClient_waitForCompletion(client, token, 1000L);
    printf("📤 Đã gửi: '%s'\n", payload);
}

// ====== Đọc 1 event từ file chuột ======
struct input_event read_value_from_file(const char *filepath, int *status) {
    struct input_event ev;
    memset(&ev, 0, sizeof(struct input_event));

    int fd = open(filepath, O_RDONLY);
    if (fd == -1) {
        perror("❌ Không mở được file thiết bị");
        *status = -1;
        return ev;
    }

    ssize_t bytes = read(fd, &ev, sizeof(struct input_event));
    if (bytes < sizeof(struct input_event)) {
        perror("❌ Không đọc được dữ liệu thiết bị");
        close(fd);
        *status = -1;
        return ev;
    }

    close(fd);
    *status = 0;
    return ev;
}

// ====== Trả về dấu của số: -1, 0, hoặc 1 ======
int sign(int x) {
    return (x >= 0) - (x < 0);
}

// ====== Kiểm tra hướng chuyển động có giống trước đó không ======
int eqdir(int x_prev, int x, int y_prev, int y) {
    return (sign(x) == sign(x_prev)) && (sign(y) == sign(y_prev));
}

// ====== Hàm chính ======
int main(int argc, char* argv[]) {
    // ==== Khởi tạo MQTT ====
    MQTTClient client;
    MQTTClient_create(&client, ADDRESS, CLIENTID, MQTTCLIENT_PERSISTENCE_NONE, NULL);
    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;

    if (MQTTClient_connect(client, &conn_opts) != MQTTCLIENT_SUCCESS) {
        printf("❌ Kết nối MQTT thất bại!\n");
        exit(-1);
    }

    // ==== Biến lưu trữ trạng thái di chuyển ====
    int x = 0, y = 0, x_prev = 0, y_prev = 0, eqdir_total = 0;
    float dx = 0, dy = 0, x_cm = 0, y_cm = 0, total_dis = 0;
    double mean_speed, accuracy;
    char msg_event[60];

    // ==== Biến thời gian ====
    struct timespec start_time, end_time;
    clock_gettime(CLOCK_MONOTONIC, &start_time);

    while (1) {
        int status;
        struct input_event event_mouse = read_value_from_file(event_name, &status);
        if (status == -1) break;

        // ==== Lấy thời gian hệ thống để hiển thị ====
        time_t event_time = event_mouse.time.tv_sec;
        struct tm *tm_info = localtime(&event_time);
        char time_str[26];
        strftime(time_str, 26, "%Y-%m-%d %H:%M:%S", tm_info);

        // ==== Xử lý di chuyển chuột (EV_REL) ====
        if (event_mouse.type == EV_REL) {
            if (event_mouse.code == REL_X) {
                dx = event_mouse.value * MICKEY_TO_CM_X;
                x += event_mouse.value;
                x_cm += dx;
            } else if (event_mouse.code == REL_Y) {
                dy = event_mouse.value * MICKEY_TO_CM_Y;
                y += event_mouse.value;
                y_cm += dy;
            } else if (event_mouse.code == REL_WHEEL) {
                // Cuộn chuột (scroll)
                sprintf(msg_event, "left: %d, right: %d, middle: %d, roll: %d", 0, 0, 0, event_mouse.value);
                publish(client, PUB_TOPIC, msg_event);
            }

            // Cập nhật quãng đường và hướng
            total_dis += sqrt(dx * dx + dy * dy);
            eqdir_total += eqdir(x_prev, x, y_prev, y);
            x_prev = x;
            y_prev = y;

            // Tính toán tốc độ và độ chính xác mỗi 0.1 giây
            clock_gettime(CLOCK_MONOTONIC, &end_time);
            double elapsed_time = (end_time.tv_sec - start_time.tv_sec) +
                                  (end_time.tv_nsec - start_time.tv_nsec) / 1e9;
            if (elapsed_time > 0.1) {
                mean_speed = total_dis / elapsed_time;
                accuracy = (eqdir_total / (total_dis / sqrt(dx * dx + dy * dy) + 1e-6)) * 100;

                char msg_speed_acc[100];
                sprintf(msg_speed_acc, "X_pos: %.1f, Y_pos: %.1f, Speed: %.1f, Accuracy: %.1f", 
                        x_cm, y_cm, mean_speed, accuracy);
                publish(client, PUB_TOPIC, msg_speed_acc);

                // Reset
                clock_gettime(CLOCK_MONOTONIC, &start_time);
                eqdir_total = 0;
                total_dis = 0.0;
            }

        } else if (event_mouse.type == EV_KEY) {
            // ==== Xử lý nhấn nút chuột ====
            if (event_mouse.code == BTN_LEFT) {
                sprintf(msg_event, "left: %d, right: %d, middle: %d, roll: %d", 
                        event_mouse.value, 0, 0, 0);
                publish(client, PUB_TOPIC, msg_event);
            } else if (event_mouse.code == BTN_RIGHT) {
                sprintf(msg_event, "left: %d, right: %d, middle: %d, roll: %d", 
                        0, event_mouse.value, 0, 0);
                publish(client, PUB_TOPIC, msg_event);
            } else if (event_mouse.code == BTN_MIDDLE) {
                sprintf(msg_event, "left: %d, right: %d, middle: %d, roll: %d", 
                        0, 0, event_mouse.value, 0);
                publish(client, PUB_TOPIC, msg_event);
            }
        }
    }

    // ==== Ngắt kết nối MQTT ====
    MQTTClient_disconnect(client, 10);
    MQTTClient_destroy(&client);
    return 0;
}



// sudo rmmod usbhid
// sudo insmod custom_mouse_driver.ko
// sudo evtest
// gcc Mouse_pub1.c -o mousepub1 -lpaho-mqtt3c -lm
