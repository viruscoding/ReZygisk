# ReZygisk

[English](../README.md)

ReZygisk làm một nhánh phát triển lấy từ ZygiskNext, một triển khai độc lập của Zygisk, cung cấp và hỗ trợ Zygisk API cho KernelSU, Magisk (bên cạnh tích hợp) và APatch (Đang trong quá trình phát triển).

Mục tiêu của ReZygisk là mô-đun hoá và viết lại toàn bộ codebase từ C++/Rust sang C, cho phép triển khai API Zygisk hiệu quả hơn và nhanh hơn với giấy phép dễ dàng tái sử dụng hơn.

> [!NOTE]
> Mô-đun này đang trong quá trình phát triển; chỉ nên sử dụng file .zip từ các bản phát hành.
>
> Mạc dù bạn có thể cài đặt mô-đun được lấy từ trang [Actions](https://github.com/PerformanC/ReZygisk/actions), nhưng sẽ chỉ phụ thuộc vào bạn có chấp nhận rủi ro hay không vì các mô-đun này có thể gây lỗi cho hệ điều hành. Ví dụ: Bootloop (Lỗi liên tục khởi động lại máy)

## Tại sao nhánh phát triển này lại xuất hiện?

Bản ra mắt mới nhất của Zygisk Next không còn là mã nguồn mở, dành toàn bộ quyền phát triển phần mềm cho những nhà phát triển gốc. Không chỉ giới hạn chúng ta đóng gốp cho dự án, mà còn bất khả thi trong việc kiểm trả độ đảm bảo của mã, điều mà có thể coi là mối quan tâm lớn về tính bảo mật và độ an toàn, bởi Zygisk Next là mô-đun chạy dưới quyền quản trị (root), có khả năng truy cập vào toàn bộ hệ thống trong thiết bị của bạn.

Các nhà phát triển Zygisk Next đều là những người nổi tiếng và được tin tưởng bởi cộng đồng sử dụng Android, tuy nhiên, điều đó không có nghĩa là mã nguồn của họ không có lỗ hổng hoặc có nguy cơ bị tấn công. Chúng tôi (PerformanC) thông cảm được lý do mà học đóng mã nguồn của họ vào, tuy nhiễn chúng tôi tin vào điều ngược lại.

## Ưu điểm

- Mã nguồn mở (Vĩnh Viễn)

## Các công cụ/thư viện được sử dụng

| Công cụ         | Mô tả                                     |
|-----------------|-------------------------------------------|
| `Android NDK`   | Bộ công cụ phát triển cốt lõi cho Android |

### Các công cụ/thư viện của C++ được sử dụng

| Thư Viện   | Mô Tả                                        |
|------------|----------------------------------------------|
| `lsplt`    | Công cụ **móc** vào PLT đơn giản cho Android |

## Các sử dụng

Chúng tôi đang trong quá trình phát triển (Sắp ra mắt)

## Cài Đặt

Hiện chưa có bản ra mắt ổn định (Sắp ra mắt)

## Dịch WebUI cho mô-đun

Hiện tai, chúng tôi chưa tích hợp nền táng dịch nào để dịch một cách thuận tiện nhưng bạn có thể đóng góp vào nhánh [add/new-webui](https://github.com/PerformanC/ReZygisk/tree/add/new-webui). Đừng quên thêm trang cá nhân Github của bạn vào [TRANSLATOR.md](https://github.com/PerformanC/ReZygisk/blob/add/new-webui/TRANSLATOR.md) để mọi người thâys được đóng góp của bạn

## Hỗ trợ
Nếu bạn có những câu hỏi nào dành cho ReZygisk hoặc bất kì một dự án nào của PerformanC, hãy tự nhiên tham gia các kênh trò chuyện dưới đây:

- Discord: [PerformanC](https://discord.gg/uPveNfTuCJ)
- Telegram [ReZygisk]: [@rezygiskchat](https://t.me/rezygiskchat)
- Telegram [PerformanC]: [@performancorg](https://t.me/performancorg)

## Đóng góp cho dự án này

Tuân theo [hướng dẫn đóng góp](https://github.com/PerformanC/contributing) của PerformanC là một điều tất yếu mà bạn bắt buộc phải làm. Hãy tuân theo chính sách bảo mật, quy tắc ứng xử/đóng góp mã nguồn và tiêu chuẩn cú pháp riêng.

## Bản quyền

Hầu hết các thành phần của ReZygisk để dưới bản quyền GPL (bởi Dr-TSNG) và AGPL 3.0 (bởiThe PerformanC Organization) cho những phần được viết lại. Bạn có thể xem thêm trong trang [Open Source Initiative](https://opensource.org/licenses/AGPL-3.0).