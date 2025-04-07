#include "stdlib.h"
#include "string.h"
#include "unistd.h"
#include "MQTTClient.h"
#include <mysql.h>

// Địa chỉ broker MQTT
#define ADDRESS     "tcp://broker.emqx.io:1883"
#define CLIENTID    "subscriber_demo"

// Các topic đăng ký nhận dữ liệu
#define SUB_TOPIC     "cdt/topic1"
#define SUB_TOPIC_2   "cdt/topic2"


// Thông tin kết nối cơ sở dữ liệu MySQL
char *server = "localhost";
char *user = "root";
char *password = "123456"; // Đổi nếu cần
char *database = "Mouse";

// Biến toàn cục cho MySQL
MYSQL *conn;
MYSQL_RES *res;
MYSQL_ROW row;

/**
 * Callback khi nhận được tin nhắn MQTT
 */
int on_message(void *context, char *topicName, int topicLen, MQTTClient_message *message) {
    char* payload = message->payload;  // Nội dung tin nhắn
    printf("Received message: %s\n", payload);

    // Khởi tạo kết nối MySQL
    conn = mysql_init(NULL);
    if (mysql_real_connect(conn, server, user, password, database, 0, NULL, 0) == NULL) {
        fprintf(stderr, "MySQL connection error: %s\n", mysql_error(conn));
        mysql_close(conn);
        exit(1);
    }

    // Khai báo biến nhận dữ liệu từ payload
    int left = 0, right = 0, middle = 0, roll = 0;
    float x_cm = 0, y_cm = 0, speed = 0, accuracy = 0;

    // Kiểm tra và phân tích payload dạng sự kiện nhấn chuột
    if (sscanf(payload, "left: %d, right: %d, middle: %d, roll: %d", &left, &right, &middle, &roll) == 4) {
        char sql[256];
        sprintf(sql, "INSERT INTO Mouse_event(left_click, right_click, Middle_click, Roll) VALUES (%d, %d, %d, %d)", 
                left, right, middle, roll);
        mysql_query(conn, sql);  // Thực hiện truy vấn
    }

    // Kiểm tra và phân tích payload dạng di chuyển chuột
    else if (sscanf(payload, "X_pos: %f, Y_pos: %f, Speed: %f, Accuracy: %f", &x_cm, &y_cm, &speed, &accuracy) == 4) {
        char sql[256];
        sprintf(sql, "INSERT INTO Mouse_movement(X_pos, Y_pos, Speed, Accuracy) VALUES (%.1f, %.1f, %.1f, %.1f)",
                x_cm, y_cm, speed, accuracy);
        mysql_query(conn, sql);  // Thực hiện truy vấn
    }

    // Không đúng định dạng
    else {
        printf("Failed to parse message!\n");
    }

    // Đóng kết nối MySQL
    mysql_close(conn);

    // Giải phóng bộ nhớ của MQTT
    MQTTClient_freeMessage(&message);
    MQTTClient_free(topicName);
    return 1;
}

int main(int argc, char* argv[]) {
    MQTTClient client;

    // Tạo client MQTT
    MQTTClient_create(&client, ADDRESS, CLIENTID, MQTTCLIENT_PERSISTENCE_NONE, NULL);

    // Thiết lập tùy chọn kết nối
    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;

    // Đăng ký callback xử lý khi có tin nhắn đến
    MQTTClient_setCallbacks(client, NULL, NULL, on_message, NULL);

    // Kết nối đến broker
    int rc;
    if ((rc = MQTTClient_connect(client, &conn_opts)) != MQTTCLIENT_SUCCESS) {
        printf("Failed to connect to MQTT broker, return code %d\n", rc);
        exit(-1);
    }

    // Đăng ký (subscribe) các topic để lắng nghe dữ liệu
    MQTTClient_subscribe(client, SUB_TOPIC, 0);
    MQTTClient_subscribe(client, SUB_TOPIC_2, 0);


    // Vòng lặp chính (giữ chương trình chạy liên tục)
    while (1) {
        sleep(1); // Có thể xử lý thêm nếu cần
    }

    // Ngắt kết nối và hủy client khi thoát (không bao giờ tới đoạn này nếu không ngắt vòng lặp)
    MQTTClient_disconnect(client, 1000);
    MQTTClient_destroy(&client);
    return rc;
}



// gcc Mouse_sub1.c -o mousesub1 -I/usr/include/mysql -lmysqlclient -lpaho-mqtt3c