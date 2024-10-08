#include "base.hpp"
#include "include/jwt-cpp/traits/boost-json/defaults.h"
#include "shared_state.hpp"

template <class WebsocketSession>
class WebsocketSessionManager
{
    // Access the websocketSession class, this is part of
    // the Curiously Recurring Template Pattern idiom.
    WebsocketSession &
    websocketSession()
    {
        return static_cast<WebsocketSession &>(*this);
    }

    beast::flat_buffer buffer_;
    std::shared_ptr<shared_state> state_;
    std::vector<std::shared_ptr<std::string const>> queue_;
    std::string connection_id;

    void send(std::shared_ptr<std::string const> const &ss)
    {
        // Always add to queue
        queue_.push_back(ss);

        // Are we already writing?
        if (queue_.size() > 1)
            return;

        // We are not currently writing, so send this immediately
        websocketSession().ws().async_write(
            net::buffer(*queue_.front()),
            [sp = websocketSession().shared_from_this()](
                beast::error_code ec, std::size_t bytes)
            {
                sp->on_write(ec, bytes);
            });
    }

    void close_with_401(http::request<http::string_body> &req, const std::string &error_message)
    {
        // Close the WebSocket connection
        websocketSession().ws().async_close(websocket::close_code::normal,
                                            beast::bind_front_handler(
                                                &WebsocketSessionManager::on_close,
                                                websocketSession().shared_from_this()));

        // Send an HTTP response with a 401 status code and an error message
        http::response<http::string_body> res{http::status::unauthorized, req.version()};
        res.set(http::field::server, "ir-websocket-server");
        res.set(http::field::content_type, "application/json");
        res.body() = "Unauthorized: " + error_message;
        res.prepare_payload();

        using response_type = typename std::decay<decltype(res)>::type;
        auto sp = std::make_shared<response_type>(std::forward<decltype(res)>(res));

        http::async_write(websocketSession().ws().next_layer(), *sp,
                          [self = websocketSession().shared_from_this(), sp](
                              beast::error_code ec, std::size_t bytes) {});
    }

    std::string url_decode(const std::string &input)
    {
        std::string result;
        for (std::size_t i = 0; i < input.size(); ++i)
        {
            if (input[i] == '%' && i + 2 < input.size() &&
                std::isxdigit(input[i + 1]) && std::isxdigit(input[i + 2]))
            {
                result += static_cast<char>(std::stoi(input.substr(i + 1, 2), 0, 16));
                i += 2;
            }
            else if (input[i] == '+')
            {
                result += ' ';
            }
            else
            {
                result += input[i];
            }
        }
        return result;
    }

    std::string generate_random_string(int length)
    {
        const char charset[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
        const int charset_size = sizeof(charset) - 1;
        std::mt19937_64 rng(std::time(nullptr));
        std::uniform_int_distribution<int> distribution(0, charset_size - 1);

        std::string random_string;
        random_string.reserve(length);

        for (int i = 0; i < length; ++i)
        {
            random_string.push_back(charset[distribution(rng)]);
        }

        return random_string;
    }

    // Start the asynchronous operation
    template <class Body, class Allocator>
    void
    do_accept(http::request<Body, http::basic_fields<Allocator>> req)
    {
        // Set suggested timeout settings for the websocket
        websocketSession().ws().set_option(
            websocket::stream_base::timeout::suggested(
                beast::role_type::server));

        // Set a decorator to change the Server of the handshake
        websocketSession().ws().set_option(
            websocket::stream_base::decorator(
                [](websocket::response_type &res)
                {
                    res.set(http::field::server,
                            std::string(BOOST_BEAST_VERSION_STRING) +
                                " advanced-server-flex");
                }));

        // Accept the websocket handshake

        std::string token;
        if (req.target().find("?token=") != std::string::npos)
        {
            // Extract token from URI
            token = req.target().substr(req.target().find("?token=") + 7);
            token = url_decode(token);
        }
        try
        {
            const auto decoded_token = jwt::decode<jwt::traits::boost_json>(token);
            const auto verify = jwt::verify<jwt::traits::boost_json>().allow_algorithm(jwt::algorithm::hs256{"secret"}).with_issuer("auth0").with_audience("aud0");
            verify.verify(decoded_token);
            std::cout << "succeed!" << '\n';

            websocketSession().ws().async_accept(
                req,
                beast::bind_front_handler(
                    &WebsocketSessionManager::on_accept,
                    websocketSession().shared_from_this()));
        }
        catch (const std::exception &e)
        {
            std::cerr << "token error :" << e.what() << '\n';
            close_with_401(req, e.what());
        }
    }

private:
    void
    fail(beast::error_code ec, char const *what)
    {
        std::cerr << what << ": " << ec.message() << "\n";

        if (ec == net::ssl::error::stream_truncated)
            return;
    }

    void
    on_accept(beast::error_code ec)
    {
        if (ec)
            return fail(ec, "accept");

        // Read a message
        do_read();
    }

    void
    do_read()
    {
        connection_id = generate_random_string(16);
        // state_->connect(connection_id, this);

        // Read a message into our buffer
        websocketSession().ws().async_read(
            buffer_,
            beast::bind_front_handler(
                &WebsocketSessionManager::on_read,
                websocketSession().shared_from_this()));
    }

    void
    on_read(
        beast::error_code ec,
        std::size_t bytes_transferred)
    {
        boost::ignore_unused(bytes_transferred);

        // This indicates that the WebsocketSessionManager was closed
        if (ec == websocket::error::closed)
            return;

        if (ec)
            return fail(ec, "read");

        // Echo the message
        websocketSession().ws().text(websocketSession().ws().got_text());
        websocketSession().ws().async_write(
            buffer_.data(),
            beast::bind_front_handler(
                &WebsocketSessionManager::on_write,
                websocketSession().shared_from_this()));
    }

    void
    on_write(
        beast::error_code ec,
        std::size_t bytes_transferred)
    {
        boost::ignore_unused(bytes_transferred);

        if (ec)
            return fail(ec, "write");

        // Clear the buffer
        buffer_.consume(buffer_.size());

        // Do another read
        do_read();
    }
    void
    on_close(beast::error_code ec)
    {
        // Handle the error, if any
        if (ec)
            return fail(ec, "close");
    }

public:
    // Start the asynchronous operation
    template <class Body, class Allocator>
    void
    run(http::request<Body, http::basic_fields<Allocator>> req)
    {
        // Accept the WebSocket upgrade request
        do_accept(std::move(req));
    }
};

class PlainWebsocketSessionManager
    : public WebsocketSessionManager<PlainWebsocketSessionManager>,
      public std::enable_shared_from_this<PlainWebsocketSessionManager>
{
    websocket::stream<beast::tcp_stream> ws_;
    std::shared_ptr<shared_state> state_;

public:
    // Create the session
    explicit PlainWebsocketSessionManager(
        beast::tcp_stream &&stream,
        std::shared_ptr<shared_state> const &&state)
        : ws_(std::move(stream)), state_(state)
    {
    }

    // Called by the base class
    websocket::stream<beast::tcp_stream> &
    ws()
    {
        return ws_;
    }

    std::shared_ptr<shared_state> &
    state()
    {
        return state_;
    }
};

class SSLWebsocketSessionManager
    : public WebsocketSessionManager<SSLWebsocketSessionManager>,
      public std::enable_shared_from_this<SSLWebsocketSessionManager>
{
    websocket::stream<beast::ssl_stream<beast::tcp_stream>> ws_;
    std::shared_ptr<shared_state> state_;

public:
    // Create the SSLWebsocketSessionManager
    explicit SSLWebsocketSessionManager(
        beast::ssl_stream<beast::tcp_stream> &&stream,
        std::shared_ptr<shared_state> const &&state)
        : ws_(std::move(stream)), state_(state)
    {
    }

    websocket::stream<beast::ssl_stream<beast::tcp_stream>> &
    ws()
    {
        return ws_;
    }

    std::shared_ptr<shared_state> &
    state()
    {
        return state_;
    }
};

//------------------------------------------------------------------------------

template <class Body, class Allocator>
void MakeWebsocketSession(
    beast::tcp_stream stream,
    http::request<Body, http::basic_fields<Allocator>> req,
    std::shared_ptr<shared_state> state)
{
    std::make_shared<PlainWebsocketSessionManager>(
        std::move(stream), std::move(state))
        ->run(std::move(req));
}

template <class Body, class Allocator>
void MakeWebsocketSession(
    beast::ssl_stream<beast::tcp_stream> stream,
    http::request<Body, http::basic_fields<Allocator>> req,
    std::shared_ptr<shared_state> state)
{
    std::make_shared<SSLWebsocketSessionManager>(
        std::move(stream), std::move(state))
        ->run(std::move(req));
}