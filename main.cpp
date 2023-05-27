#include <iostream>
#include <boost/asio.hpp>
#include <map>
#include <thread>
#include <condition_variable>
#include <fstream>
#include <nlohmann/json.hpp>

namespace asio = boost::asio;
using boost::asio::ip::udp;
    
uint16_t recivers_port;
uint16_t senders_port;
size_t package_size;

#define CONFIG_PATH "D:/projs/udp_cam_server/config.json"

void setup_config() {
    using nlohmann::json;
    std::ifstream f(CONFIG_PATH);
    json config = json::parse(f);
    recivers_port = config["recivers_port"];
    senders_port = config["senders_port"];
    package_size = config["package_size"];
}

struct ID {
    char data[37];
    ID() {
        data[36] = '\0';
    }
    ID(char* data) {
        memcpy(this->data, data, 36);
        this->data[36] = '\0';
    }
};

bool operator == (const ID& id1, const ID& id2) {
    return !strcmp(id1.data, id2.data);
}

bool operator != (const ID& id1, const ID id2) {
    return !(id1 != id2);
}

bool operator < (const ID& id1, const ID& id2) {
    return strcmp(id1.data, id2.data) < 0;
}

struct Stream {
    char* data;
    size_t size;
    std::condition_variable* cond;
    Stream() {
        data = new char[package_size];
    }
    ~Stream() {
        delete[] data;
    }
};

class App {
    std::map<ID, Stream*> streams = {};

    asio::io_context context;

    udp::endpoint recv_address;

    udp::socket recv_server;

    udp::endpoint send_address;

    udp::socket send_server;

    udp::endpoint remote_recv_address;
    ID recv_id;
    udp::endpoint remote_send_address;
    ID send_id;

    size_t package_size;

public:
    const uint8_t connection_sucessful = 0;
    const uint8_t connection_failure = 1;
    App(uint16_t recivers_port, uint16_t senders_port, size_t package_size):
        recv_address(udp::v4(), recivers_port),
        recv_server(context, recv_address),
        send_address(udp::v4(), senders_port),
        send_server(context, send_address) {
        this->package_size = package_size;
    }
    ~App() {
        for (auto i : streams) {
            delete i.second;
        }
    }
    void run() {
        handle_reciver_connection();
        handle_sender_connection();
        context.run();
    }
private:
    void handle_reciver_connection() {
        auto on_recv_message = [this](boost::system::error_code ec, size_t size) {
            if (size != 36) {
                handle_sender_connection();
                return;
            }
            if (ec) {
                std::cerr << ec.message() << std::endl;
                handle_sender_connection();
                return;
            }
            if (!streams.count(recv_id)) {
                recv_server.send_to(asio::buffer(&connection_failure, 1),
                    remote_recv_address, NULL, ec);
                handle_sender_connection();
                return;
            }
            Stream* stream = streams[recv_id];
            std::thread(&App::reciver_loop, this, recv_address, stream).detach();
            handle_reciver_connection();
        };
        recv_server.async_receive_from(asio::buffer(recv_id.data, 36),
            recv_address, on_recv_message);
    }
    void reciver_loop(udp::endpoint remote_address, Stream* stream) {
        boost::system::error_code ec;
        asio::io_context context;
        udp::endpoint address = udp::endpoint(udp::v4(), 0);
        udp::socket server(context, address);
        server.send_to(asio::buffer(&connection_sucessful, 1),
            recv_address, NULL, ec);
        if (ec) {
            std::cerr << ec.message() << std::endl;
            return;
        }
        std::cout << "Reciver connected from " << remote_address.address().to_string() << ":" << remote_address.port() << std::endl;
        std::mutex m;
        std::unique_lock<std::mutex> lk(m);
		bool connected = true;
        // for (;;) {
        //     stream->cond->wait(lk);
        //     server.send_to(asio::buffer(stream->data, stream->size),
        //         remote_address, NULL, ec);
        //     if (ec) {
        //         std::cerr << ec.message() << std::endl;
        //         return;
        //     }
        // }
        uint8_t connection_check_buffer;
        std::function<void(boost::system::error_code, size_t)> on_sent = [this, &connected, stream, &lk, &server, &remote_address, &on_sent]
        (boost::system::error_code ec, size_t size)->void {
            if (ec) {
                std::cerr << ec.message() << std::endl;
                return;
            }
            if (!connected) return;
            stream->cond->wait(lk);
            server.async_send_to(asio::buffer(stream->data, stream->size), remote_address, on_sent);
        };
        server.async_send_to(asio::buffer(stream->data, stream->size), remote_address, on_sent);
        server.set_option(boost::asio::detail::socket_option::integer<SOL_SOCKET, SO_RCVTIMEO>{ 60000 });
        std::function<void(boost::system::error_code, size_t)> check_connection_loop = [this, &connected, &server, &check_connection_loop, &connection_check_buffer]
        (boost::system::error_code ec, size_t size) {
            if (ec) {
                std::cerr << ec.message() << std::endl;
                connected = false;
                return;
            }
            server.async_receive(asio::buffer(&connection_check_buffer, 1), check_connection_loop);
        };
        server.async_receive(asio::buffer(&connection_check_buffer, 1), check_connection_loop);
        context.run();
        std::cout << "Reciver " << remote_address.address().to_string() << ":" << remote_address.port() << " disconnected" << std::endl;
    }
    void handle_sender_connection() {
        auto on_send_message = [this](boost::system::error_code ec, size_t size) {
            if (size != 36) {
                handle_sender_connection();
                return;
            }
            if (ec) {
                std::cerr << ec.message() << std::endl;
                handle_sender_connection();
                return;
            }
            if (streams.count(send_id)) {
                send_server.send_to(asio::buffer(&connection_failure, 1),
                    send_address, NULL, ec);
                handle_sender_connection();
                return;
            }
            Stream* stream = new Stream();
            streams[send_id] = stream;
            std::thread(&App::sender_loop, this, send_address, stream, send_id).detach();
            handle_sender_connection();
        };
        send_server.async_receive_from(asio::buffer(send_id.data, 36),
            send_address, on_send_message);
    }
    void sender_loop(udp::endpoint remote_address, Stream* stream, const ID id) {
        std::condition_variable cond;
        stream->cond = &cond;

        boost::system::error_code ec;
        asio::io_context context;
        udp::endpoint address = udp::endpoint(udp::v4(), 0);
        udp::socket server(context, address);
        server.set_option(boost::asio::detail::socket_option::integer<SOL_SOCKET, SO_RCVTIMEO>{ 2000 });
        server.send_to(asio::buffer(&connection_sucessful, 1),
            remote_address,NULL, ec);
        if (ec) {
            std::cerr << ec.message() << std::endl;
            return;
        }
        std::cout << "Sender connected from " << remote_address.address().to_string() << ":" << remote_address.port() << std::endl;
        for (;;) {
            size_t size = server.receive(asio::buffer(stream->data, package_size),
                NULL, ec);
            if (ec || !size) {
                std::cerr << ec.message() << std::endl;
                break;
            }
            stream->size = size;
            stream->cond->notify_all();
        }
        std::cout << "Sender " << remote_address.address().to_string() << ":" << remote_address.port() << " disconnected" << std::endl;
        delete stream;
        streams.erase(id);
    }
};

int main() {
    setlocale(LC_ALL, "russian");
    setup_config();
    App app(recivers_port, senders_port, package_size);
    app.run();
}

//int main() {
//    const uint16_t port = 5000;
//    setlocale(LC_ALL, "russian");
//    //setup_config();
//    boost::system::error_code ec;
//    asio::io_context context;
//    udp::socket server(context, udp::endpoint(udp::v4(), port));
//
//    udp::endpoint reciver_address;
//    udp::endpoint sender_address;
//
//    uint8_t a;
//
//    server.receive_from(asio::buffer(&a, 1), reciver_address,
//        NULL, ec);
//    if (ec) {
//        return ec.value();
//    }
//    
//    server.receive_from(asio::buffer(&a, 1), sender_address,
//        NULL, ec);
//    if (ec) {
//        return ec.value();
//    }
//
//    char* buffer = new char[package_size];
//
//    std::function<void(boost::system::error_code, size_t)> on_message =
//        [&server, buffer, &reciver_address, &sender_address, &on_message, &file](boost::system::error_code ec, size_t len) {
//        if (ec) {
//            return;
//        }
//        server.async_send_to(asio::buffer(buffer, len), reciver_address,
//            [&server, buffer, &sender_address, &on_message](boost::system::error_code ec, size_t len) {
//                if (ec) return;
//                server.async_receive_from(asio::buffer(buffer, package_size),
//                    sender_address, on_message);
//            });
//    };
//
//    server.async_receive_from(asio::buffer(buffer, package_size),
//        sender_address, on_message);
//
//
//    context.run();
//    delete[] buffer;
//}