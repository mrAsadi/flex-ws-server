#include <boost/asio/buffer.hpp>
#include <boost/asio/ssl/context.hpp>
#include <iostream>
#include <fstream>
#include <stdexcept>

// Function to load a file into a string
std::string load_file(const std::string &filename)
{
    std::ifstream file(filename);
    if (file)
    {
        return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    }
    else
    {
        throw std::runtime_error("Failed to open file: " + filename);
    }
}

// Function to load certificate from file
std::string load_certificate(const std::string &cert_file)
{
    return load_file(cert_file);
}

// Function to load private key from file
std::string load_private_key(const std::string &key_file)
{
    return load_file(key_file);
}

// SSL context setup function
void setup_ssl_context(boost::asio::ssl::context &ctx, const std::string &path)
{
    // Set TLS version
    ctx.set_options(
        boost::asio::ssl::context::default_workarounds |
        boost::asio::ssl::context::no_sslv2 |
        boost::asio::ssl::context::no_sslv3 |
        boost::asio::ssl::context::single_dh_use |
        boost::asio::ssl::context::no_tlsv1 |
        boost::asio::ssl::context::no_tlsv1_1);

    // Load certificate and private key from files
    std::string cert = load_certificate(path + "/server_combined.crt");
    std::string dh_params_file = load_certificate(path + "/dhparams_combined.pem");

    // Set certificate and private key
    ctx.use_certificate_chain(boost::asio::buffer(cert.data(), cert.size()));
    ctx.use_private_key(boost::asio::buffer(cert.data(), cert.size()), boost::asio::ssl::context::file_format::pem);
    ctx.use_tmp_dh(boost::asio::buffer(dh_params_file.c_str(), dh_params_file.size()));

    // Secure password callback
    ctx.set_password_callback(
        [](std::size_t, boost::asio::ssl::context_base::password_purpose) -> std::string
        {
            // Implement a secure method to obtain the password
            // For example, prompt the user or read from a secure source
            return "test";
        });
}
