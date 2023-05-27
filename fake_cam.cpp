#include <iostream>
#include <boost/asio.hpp>
#include <opencv2/opencv.hpp>

namespace asio = boost::asio;
using boost::asio::ip::udp;
using boost::asio::ip::address;

const std::vector<int> jpg_encode_params = { cv::IMWRITE_JPEG_QUALITY, 70 };

const size_t package_size = 17000;

const char* ID = "000000000000000000000000000000000000";

int main() {
	setlocale(LC_ALL, "russian");
	cv::VideoCapture cap(1);

	const int fps = cap.get(cv::CAP_PROP_FPS);

	boost::system::error_code ec;
	asio::io_context context;

	udp::socket client(context);

	client.open(udp::v4(), ec);

	if (ec) {
		return ec.value();
	}

	udp::endpoint remote_address(address::from_string("192.168.0.105"), 5001);

	client.send_to(asio::buffer(ID, 36),
		remote_address, NULL, ec);
	if (ec) {
		return ec.value();
	}

	bool failed = false;
	client.receive_from(asio::buffer(&failed, sizeof(bool)),
		remote_address, NULL, ec);

	if (failed) {
		std::cout << "ID: " << ID << " already exists" << std::endl;
		return 1;
	}

	for (;;) {
		auto start = std::chrono::system_clock::now();
		cv::Mat img;
		std::vector<uchar> data;
		if (!cap.read(img)) break;
		if (!cv::imencode(".jpg", img, data, jpg_encode_params)) {
			break;
		}

		for (size_t i = 0; i < data.size(); i += package_size) {
			size_t cur_packet_size = data.size() - i > package_size ? package_size : data.size() % package_size;
			client.send_to(asio::buffer(data.data() + i, cur_packet_size),
				remote_address, NULL, ec);
			if (ec) {
				std::cout << ec.message() << std::endl;
				return ec.value();
			}
		}
		auto end = std::chrono::system_clock::now();
		std::chrono::system_clock::duration dur = end - start;
		int dur_ms = std::chrono::duration_cast<std::chrono::milliseconds>(dur).count();
		if (fps != 0) {
			int time_to_sleep = 1000 / fps - dur_ms;
			if (time_to_sleep > 0)
				std::this_thread::sleep_for(std::chrono::milliseconds(time_to_sleep));
		}
	}
}