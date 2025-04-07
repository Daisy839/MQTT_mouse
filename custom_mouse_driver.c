#include <linux/module.h>      // Hỗ trợ module kernel
#include <linux/kernel.h>      // Các hàm cơ bản trong kernel
#include <linux/usb.h>         // Thư viện làm việc với thiết bị USB
#include <linux/input.h>       // Quản lý input device (chuột, bàn phím,...)
#include <linux/hid.h>         // Hỗ trợ thiết bị HID (chuẩn thiết bị giao tiếp người dùng)

// Định nghĩa Vendor ID và Product ID của chuột cần nhận diện
#define VENDOR_ID  0x046d  // Logitech vendor ID
#define PRODUCT_ID 0xc077 // PixArt Product ID

// Cấu trúc lưu thông tin chuột
struct usb_mouse {
    struct input_dev *inputdev;       // Con trỏ đến thiết bị input của Linux
    struct usb_device *usbdev;        // Con trỏ thiết bị USB tương ứng
    struct urb *irq;                  // URB để xử lý ngắt từ thiết bị USB
    char name[64];                    // Tên chuột
    unsigned char *data;              // Bộ đệm chứa dữ liệu đọc từ chuột
    dma_addr_t data_dma;              // Địa chỉ DMA tương ứng với data
};

// Hàm callback được gọi khi URB được hoàn thành - xử lý dữ liệu chuột
static void usb_mouse_irq(struct urb *urb)
{
    struct usb_mouse *mouse = urb->context; // Lấy context từ URB
    signed char *data = mouse->data;        // Truy cập dữ liệu trả về từ chuột

    // Gửi tín hiệu nhấn nút chuột về hệ thống
    input_report_key(mouse->inputdev, BTN_LEFT,   data[0] & 0x01);
    input_report_key(mouse->inputdev, BTN_RIGHT,  data[0] & 0x02);
    input_report_key(mouse->inputdev, BTN_MIDDLE, data[0] & 0x04);

    // Gửi chuyển động chuột: X, Y, con lăn
    input_report_rel(mouse->inputdev, REL_X, data[1]);
    input_report_rel(mouse->inputdev, REL_Y, data[2]);
    input_report_rel(mouse->inputdev, REL_WHEEL, data[3]);

    input_sync(mouse->inputdev); // Đồng bộ sự kiện input

    // Gửi lại URB để tiếp tục nhận dữ liệu mới
    usb_submit_urb(urb, GFP_ATOMIC);
}

// Hàm probe được gọi khi thiết bị USB được cắm vào và khớp với ID
static int usb_mouse_probe(struct usb_interface *iface, const struct usb_device_id *id)
{
    struct usb_device *dev = interface_to_usbdev(iface); // Lấy USB device từ interface
    struct usb_mouse *mouse;
    struct input_dev *inputdev;
    int pipe, maxp;
    char mouse_path[64]; // Đường dẫn thiết bị /dev/...

    // Cấp phát bộ nhớ cho cấu trúc chuột
    mouse = kzalloc(sizeof(struct usb_mouse), GFP_KERNEL);
    if (!mouse) return -ENOMEM;

    // Cấp phát input device
    inputdev = input_allocate_device();
    if (!inputdev) {
        kfree(mouse);
        return -ENOMEM;
    }

    mouse->usbdev = dev;
    mouse->inputdev = inputdev;

    // Lấy đường dẫn vật lý của thiết bị USB
    usb_make_path(dev, mouse_path, sizeof(mouse_path));
    inputdev->phys = mouse_path;

    // Cấu hình thông tin thiết bị
    inputdev->name = "Custom USB Mouse";
    inputdev->id.bustype = BUS_USB;
    inputdev->id.vendor = VENDOR_ID;
    inputdev->id.product = PRODUCT_ID;

    // Khai báo các khả năng thiết bị hỗ trợ
    input_set_capability(inputdev, EV_KEY, BTN_LEFT);
    input_set_capability(inputdev, EV_KEY, BTN_RIGHT);
    input_set_capability(inputdev, EV_KEY, BTN_MIDDLE);
    input_set_capability(inputdev, EV_REL, REL_X);
    input_set_capability(inputdev, EV_REL, REL_Y);
    input_set_capability(inputdev, EV_REL, REL_WHEEL);

    // Đăng ký thiết bị input vào hệ thống
    if (input_register_device(mouse->inputdev)) {
        usb_free_urb(mouse->irq);
        input_free_device(inputdev);
        kfree(mouse);
        return -EIO;
    }

    // Tạo pipe nhận dữ liệu từ endpoint 1
    pipe = usb_rcvintpipe(dev, 1);
    maxp = usb_maxpacket(dev, pipe);

    // Cấp phát bộ đệm DMA và URB cho truyền dữ liệu
    mouse->data = usb_alloc_coherent(dev, 8, GFP_ATOMIC, &mouse->data_dma);
    mouse->irq = usb_alloc_urb(0, GFP_KERNEL);

    // Cấu hình URB nhận dữ liệu chuột
    usb_fill_int_urb(mouse->irq, dev, pipe, mouse->data, maxp, usb_mouse_irq, mouse, 10);
    mouse->irq->transfer_dma = mouse->data_dma;
    mouse->irq->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

    // Gửi URB để bắt đầu nhận dữ liệu
    usb_submit_urb(mouse->irq, GFP_KERNEL);
    return 0;
}

// Hàm được gọi khi thiết bị USB bị rút ra
static void usb_mouse_disconnect(struct usb_interface *iface)
{
    struct usb_mouse *mouse = usb_get_intfdata(iface); // Lấy dữ liệu đã lưu
    usb_kill_urb(mouse->irq);                          // Hủy URB đang chờ
    input_unregister_device(mouse->inputdev);          // Gỡ thiết bị input khỏi hệ thống
    usb_free_urb(mouse->irq);                          // Giải phóng URB
    usb_free_coherent(mouse->usbdev, 8, mouse->data, mouse->data_dma); // Giải phóng bộ đệm DMA
    kfree(mouse);                                      // Giải phóng bộ nhớ chuột
}

// Bảng định danh thiết bị USB được hỗ trợ
static const struct usb_device_id usb_mouse_id_table[] = {
    { USB_DEVICE(VENDOR_ID, PRODUCT_ID) }, // Logitech PixArt
    {}
};
MODULE_DEVICE_TABLE(usb, usb_mouse_id_table);


// Cấu trúc driver USB
static struct usb_driver usb_mouse_driver = {
    .name = "custom_usb_mouse",           // Tên driver
    .id_table = usb_mouse_id_table,       // Bảng ID thiết bị
    .probe = usb_mouse_probe,             // Hàm probe khi gắn thiết bị
    .disconnect = usb_mouse_disconnect,   // Hàm khi tháo thiết bị
};

// Macro đăng ký module USB driver
module_usb_driver(usb_mouse_driver);

MODULE_LICENSE("GPL"); // Khai báo license của module (bắt buộc)

