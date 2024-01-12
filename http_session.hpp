#include "base.hpp"
#include <queue>

// Handles an HTTP server connection.
// This uses the Curiously Recurring Template Pattern so that
// the same code works with both SSL streams and regular sockets.
template <class Derived>
class http_session
{
    std::shared_ptr<std::string const> doc_root_;

    // Access the derived class, this is part of
    // the Curiously Recurring Template Pattern idiom.
    Derived &
    derived()
    {
        return static_cast<Derived &>(*this);
    }

    static constexpr std::size_t queue_limit = 8; // max responses
    std::queue<http::message_generator> response_queue_;

    // The parser is stored in an optional container so we can
    // construct it from scratch it at the beginning of each new message.
    boost::optional<http::request_parser<http::string_body>> parser_;

protected:
    beast::flat_buffer buffer_;

public:
    // Construct the session
    http_session(
        beast::flat_buffer buffer,
        std::shared_ptr<std::string const> const &doc_root)
        : doc_root_(doc_root), buffer_(std::move(buffer))
    {
    }
    
    void
    fail(beast::error_code ec, char const *what)
    {
        std::cerr << what << ": " << ec.message() << "\n";

        if (ec == net::ssl::error::stream_truncated)
            return;
    }

    void
    do_read()
    {
        // Construct a new parser for each message
        parser_.emplace();

        // Apply a reasonable limit to the allowed size
        // of the body in bytes to prevent abuse.
        parser_->body_limit(10000);

        // Set the timeout.
        beast::get_lowest_layer(
            derived().stream())
            .expires_after(std::chrono::seconds(30));

        // Read a request using the parser-oriented interface
        http::async_read(
            derived().stream(),
            buffer_,
            *parser_,
            beast::bind_front_handler(
                &http_session::on_read,
                derived().shared_from_this()));
    }

    void
    on_read(beast::error_code ec, std::size_t bytes_transferred)
    {
        boost::ignore_unused(bytes_transferred);

        // This means they closed the connection
        if (ec == http::error::end_of_stream)
            return derived().do_eof();

        if (ec)
            return fail(ec, "read");

        // See if it is a WebSocket Upgrade
        if (websocket::is_upgrade(parser_->get()))
        {
            // Disable the timeout.
            // The websocket::stream uses its own timeout settings.
            beast::get_lowest_layer(derived().stream()).expires_never();

            // Create a websocket session, transferring ownership
            // of both the socket and the HTTP request.
            return make_websocket_session(
                derived().release_stream(),
                parser_->release());
        }

        // Send the response
        queue_write(handle_request(*doc_root_, parser_->release()));

        // If we aren't at the queue limit, try to pipeline another request
        if (response_queue_.size() < queue_limit)
            do_read();
    }

    void
    queue_write(http::message_generator response)
    {
        // Allocate and store the work
        response_queue_.push(std::move(response));

        // If there was no previous work, start the write loop
        if (response_queue_.size() == 1)
            do_write();
    }

    // Called to start/continue the write-loop. Should not be called when
    // write_loop is already active.
    void
    do_write()
    {
        if (!response_queue_.empty())
        {
            bool keep_alive = response_queue_.front().keep_alive();

            beast::async_write(
                derived().stream(),
                std::move(response_queue_.front()),
                beast::bind_front_handler(
                    &http_session::on_write,
                    derived().shared_from_this(),
                    keep_alive));
        }
    }

    void
    on_write(
        bool keep_alive,
        beast::error_code ec,
        std::size_t bytes_transferred)
    {
        boost::ignore_unused(bytes_transferred);

        if (ec)
            return fail(ec, "write");

        if (!keep_alive)
        {
            // This means we should close the connection, usually because
            // the response indicated the "Connection: close" semantic.
            return derived().do_eof();
        }

        // Resume the read if it has been paused
        if (response_queue_.size() == queue_limit)
            do_read();

        response_queue_.pop();

        do_write();
    }
};

//------------------------------------------------------------------------------

// Handles a plain HTTP connection
class plain_http_session
    : public http_session<plain_http_session>,
      public std::enable_shared_from_this<plain_http_session>
{
    beast::tcp_stream stream_;

public:
    // Create the session
    plain_http_session(
        beast::tcp_stream &&stream,
        beast::flat_buffer &&buffer,
        std::shared_ptr<std::string const> const &doc_root)
        : http_session<plain_http_session>(
              std::move(buffer),
              doc_root),
          stream_(std::move(stream))
    {
    }

    // Start the session
    void
    run()
    {
        this->do_read();
    }

    // Called by the base class
    beast::tcp_stream &
    stream()
    {
        return stream_;
    }

    // Called by the base class
    beast::tcp_stream
    release_stream()
    {
        return std::move(stream_);
    }

    // Called by the base class
    void
    do_eof()
    {
        // Send a TCP shutdown
        beast::error_code ec;
        stream_.socket().shutdown(tcp::socket::shutdown_send, ec);

        // At this point the connection is closed gracefully
    }
};

//------------------------------------------------------------------------------

// Handles an SSL HTTP connection
class ssl_http_session
    : public http_session<ssl_http_session>,
      public std::enable_shared_from_this<ssl_http_session>
{
    beast::ssl_stream<beast::tcp_stream> stream_;

public:
    // Create the http_session
    ssl_http_session(
        beast::tcp_stream &&stream,
        ssl::context &ctx,
        beast::flat_buffer &&buffer,
        std::shared_ptr<std::string const> const &doc_root)
        : http_session<ssl_http_session>(
              std::move(buffer),
              doc_root),
          stream_(std::move(stream), ctx)
    {
    }

    // Start the session
    void
    run()
    {
        // Set the timeout.
        beast::get_lowest_layer(stream_).expires_after(std::chrono::seconds(30));

        // Perform the SSL handshake
        // Note, this is the buffered version of the handshake.
        stream_.async_handshake(
            ssl::stream_base::server,
            buffer_.data(),
            beast::bind_front_handler(
                &ssl_http_session::on_handshake,
                shared_from_this()));
    }

    // Called by the base class
    beast::ssl_stream<beast::tcp_stream> &
    stream()
    {
        return stream_;
    }

    // Called by the base class
    beast::ssl_stream<beast::tcp_stream>
    release_stream()
    {
        return std::move(stream_);
    }

    // Called by the base class
    void
    do_eof()
    {
        // Set the timeout.
        beast::get_lowest_layer(stream_).expires_after(std::chrono::seconds(30));

        // Perform the SSL shutdown
        stream_.async_shutdown(
            beast::bind_front_handler(
                &ssl_http_session::on_shutdown,
                shared_from_this()));
    }

private:
    void
    on_handshake(
        beast::error_code ec,
        std::size_t bytes_used)
    {
        if (ec)
            return fail(ec, "handshake");

        // Consume the portion of the buffer used by the handshake
        buffer_.consume(bytes_used);

        std::cerr << "SSL handshake successful\n";

        do_read();
    }

    void
    on_shutdown(beast::error_code ec)
    {
        if (ec)
            return fail(ec, "shutdown");

        // At this point the connection is closed gracefully
    }
};
