//
// Created by Charlie Culbert on 9/11/25.


#ifndef CONSOLEAPPSTATICPLAYER_UDPSENDER_H
#define CONSOLEAPPSTATICPLAYER_UDPSENDER_H

//
#include <iostream>
#include <string>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

class UdpSender {
private:
    int sock_fd_; // Socket file descriptor
    sockaddr_in dest_addr_; // Destination address structure

    /**
     * @brief A helper function to close the socket.
     */
    void closeSocket() {
        if (sock_fd_ >= 0) {
            close(sock_fd_);
            sock_fd_ = -1; // Invalidate the file descriptor
        }
    }

public:
    /**
     * @brief Constructor that opens the socket and initializes the destination address.
     * @param ip_address The target IP address as a string.
     * @param port The target port number.
     */
    UdpSender(const std::string& ip_address, int port) : sock_fd_(-1) {
        openSocket(ip_address, port);
    }

    /**
     * @brief Destructor that closes the socket.
     */
    ~UdpSender() {
        closeSocket();
    }

    /**
     * @brief Opens a new UDP socket and configures the destination address.
     * @param ip_address The target IP address as a string.
     * @param port The target port number.
     * @return true if the socket was opened successfully, false otherwise.
     */
    bool openSocket(const std::string& ip_address, int port) {
        // Ensure any existing socket is closed before opening a new one
        closeSocket();

        // Create a UDP socket
        sock_fd_ = socket(AF_INET, SOCK_DGRAM, 0);
        if (sock_fd_ < 0) {
            perror("Socket creation failed");
            return false;
        }

        // Zero out the address structure and set family, port, and IP
        memset(&dest_addr_, 0, sizeof(dest_addr_));
        dest_addr_.sin_family = AF_INET;
        dest_addr_.sin_port = htons(port);

        // Convert IP string to binary format
        if (inet_pton(AF_INET, ip_address.c_str(), &dest_addr_.sin_addr) <= 0) {
            perror("Invalid address/ Address not supported");
            closeSocket(); // Clean up if address is invalid
            return false;
        }

        return true;
    }

    /**
     * @brief Sends a string message to the configured destination.
     * @param message The string to send.
     * @return true if the message was sent successfully, false otherwise.
     */
    bool send(const std::string& message) {
        if (sock_fd_ < 0) {
            std::cerr << "Socket is not open." << std::endl;
            return false;
        }

        ssize_t bytes_sent = sendto(sock_fd_, message.c_str(), message.length(), 0,
                                    (struct sockaddr*)&dest_addr_, sizeof(dest_addr_));

        if (bytes_sent < 0) {
            perror("Sendto failed");
            return false;
        }

        return true;
    }
};
#endif //CONSOLEAPPSTATICPLAYER_UDPSENDER_H