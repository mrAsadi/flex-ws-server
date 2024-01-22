#include "request_handler.hpp"
#include "websocket_session.hpp"
#include "http_session.hpp"
#include "shared_state.hpp"
#include <boost/asio/bind_executor.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/strand.hpp>
#include <boost/make_unique.hpp>
#include <boost/optional.hpp>
#include <algorithm>
#include <cstdlib>
#include <functional>
#include <memory>
#include <queue>
#include <string>
#include <thread>
#include <vector>

void
fail(beast::error_code ec, char const* what)
{
    // ssl::error::stream_truncated, also known as an SSL "short read",
    // indicates the peer closed the connection without performing the
    // required closing handshake (for example, Google does this to
    // improve performance). Generally this can be a security issue,
    // but if your communication protocol is self-terminated (as
    // it is with both HTTP and WebSocket) then you may simply
    // ignore the lack of close_notify.
    //
    // https://github.com/boostorg/beast/issues/38
    //
    // https://security.stackexchange.com/questions/91435/how-to-handle-a-malicious-ssl-tls-shutdown
    //
    // When a short read would cut off the end of an HTTP message,
    // Beast returns the error beast::http::error::partial_message.
    // Therefore, if we see a short read here, it has occurred
    // after the message has been completed, so it is safe to ignore it.

    if(ec == net::ssl::error::stream_truncated)
        return;

    std::cerr << what << ": " << ec.message() << "\n";
}

// Detects SSL handshakes
class detect_session : public std::enable_shared_from_this<detect_session>
{
    beast::tcp_stream stream_;
    ssl::context& ctx_;
    std::shared_ptr<shared_state> state_;
    beast::flat_buffer buffer_;

public:
    explicit
    detect_session(
        tcp::socket&& socket,
        ssl::context& ctx,
        std::shared_ptr<shared_state> const&& state)
        : stream_(std::move(socket))
        , ctx_(ctx)
        , state_(std::move(state))
    {
    }

    // Launch the detector
    void
    run()
    {
        // We need to be executing within a strand to perform async operations
        // on the I/O objects in this session. Although not strictly necessary
        // for single-threaded contexts, this example code is written to be
        // thread-safe by default.
        net::dispatch(
            stream_.get_executor(),
            beast::bind_front_handler(
                &detect_session::on_run,
                this->shared_from_this()));
    }

    void
    on_run()
    {
        // Set the timeout.
        stream_.expires_after(std::chrono::seconds(30));

        beast::async_detect_ssl(
            stream_,
            buffer_,
            beast::bind_front_handler(
                &detect_session::on_detect,
                this->shared_from_this()));
    }

    void
    on_detect(beast::error_code ec, bool result)
    {
        if(ec)
            return fail(ec, "detect");

        if(result)
        {
            // Launch SSL session
            std::make_shared<SSLHttpSession>(
                std::move(stream_),
                ctx_,
                std::move(buffer_),
                std::move(state_))->run();
            return;
        }

        // Launch plain session
        std::make_shared<PlainHttpSession>(
            std::move(stream_),
            std::move(buffer_),
            std::move(state_))->run();
    }
};

// Accepts incoming connections and launches the sessions
class listener : public std::enable_shared_from_this<listener>
{
    net::io_context& ioc_;
    ssl::context& ctx_;
    tcp::acceptor acceptor_;
    const std::shared_ptr<shared_state> state_;

public:
    listener(
        net::io_context& ioc,
        ssl::context& ctx,
        tcp::endpoint endpoint,
        std::shared_ptr<shared_state> const& state)
        : ioc_(ioc)
        , ctx_(ctx)
        , acceptor_(net::make_strand(ioc))
        , state_(state)
    {
        beast::error_code ec;

        // Open the acceptor
        acceptor_.open(endpoint.protocol(), ec);
        if(ec)
        {
            fail(ec, "open");
            return;
        }

        // Allow address reuse
        acceptor_.set_option(net::socket_base::reuse_address(true), ec);
        if(ec)
        {
            fail(ec, "set_option");
            return;
        }

        // Bind to the server address
        acceptor_.bind(endpoint, ec);
        if(ec)
        {
            fail(ec, "bind");
            return;
        }

        // Start listening for connections
        acceptor_.listen(
            net::socket_base::max_listen_connections, ec);
        if(ec)
        {
            fail(ec, "listen");
            return;
        }
    }

    // Start accepting incoming connections
    void
    run()
    {
        do_accept();
    }

private:
    void
    do_accept()
    {
        // The new connection gets its own strand
        acceptor_.async_accept(
            net::make_strand(ioc_),
            beast::bind_front_handler(
                &listener::on_accept,
                shared_from_this()));
    }

    void
    on_accept(beast::error_code ec, tcp::socket socket)
    {
        if(ec)
        {
            fail(ec, "accept");
        }
        else
        {
            // Create the detector http_session and run it
            std::make_shared<detect_session>(
                std::move(socket),
                ctx_,
                std::move(state_))->run();
        }

        // Accept another connection
        do_accept();
    }
};