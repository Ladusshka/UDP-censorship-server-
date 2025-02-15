#include <arpa/inet.h>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>
#include <set>
#include <algorithm>
#include <sys/socket.h>
#include <unistd.h>

static const int PORT = 1337;
static const int BUFFER_SIZE = 101;
static const int WINDOW_SIZE = 5;
static const int TIMEOUT_SEC = 5;

static uint8_t global_expected_id = 0;
static uint8_t global_send_id = 0;
static std::string global_send_message;

void sendAckNack(int sockfd, const sockaddr_in &clientAddr, socklen_t clientLen,
                 uint8_t ack_type, uint8_t id_byte)
{
    uint8_t reply[2];
    reply[0] = ack_type;
    reply[1] = id_byte;
    sendto(sockfd, reply, 2, 0, (const sockaddr*)&clientAddr, clientLen);
}

std::pair<std::string, std::string> receiveBlacklistAndText(int sockfd, sockaddr_in &clientAddr, socklen_t &clientLen)
{
    std::string blacklist, text;
    bool gotBlacklistEnd = false;
    bool done = false;
    int index_last = 0;
    while (!done)
    {
        uint8_t packet[BUFFER_SIZE];
        memset(packet, 0, sizeof(packet));
        ssize_t recvBytes = recvfrom(sockfd, packet, BUFFER_SIZE, 0, (sockaddr*)&clientAddr, &clientLen);
        if (recvBytes < 0)
        {
            std::cerr << "[receiveBlacklistAndText] Timeout => NACK(" << (int)global_expected_id << ")\n";
            sendAckNack(sockfd, clientAddr, clientLen, 0x15, global_expected_id);
            continue;
        }
        uint8_t pkt_id = packet[0];
        if (pkt_id != global_expected_id)
        {
            std::cerr << "[receiveBlacklistAndText] Received pkt_id=" << (int)pkt_id << " but expected=" << (int)global_expected_id << " => NACK\n";
            sendAckNack(sockfd, clientAddr, clientLen, 0x15, global_expected_id);
            continue;
        }
        bool found1F = false;
        for (int i = 1; i < recvBytes; i++)
        {
            if (packet[i] == 0x1F)
            {
                found1F = true;
                if (!gotBlacklistEnd)
                {
                    gotBlacklistEnd = true;
                    for (int j = i+1; j < recvBytes; j++) {
                        text.push_back((char)packet[j]);
                    }
                }
                else
                {
                    done = true;
                }
            }
            else
            {
                if (!gotBlacklistEnd)
                    blacklist.push_back((char)packet[i]);
                else
                    text.push_back((char)packet[i]);
            }
            if (found1F)
            {
                break;
            }
        }
        sendAckNack(sockfd, clientAddr, clientLen, 0x06, pkt_id);
        global_expected_id = (uint8_t)(global_expected_id + 1);
        if (done)
            break;
    }
    return { blacklist, text };
}

std::string censorText(const std::string &text, const std::set<std::string> &censored_words)
{
    std::string result = text;
    for (const auto &w : censored_words)
    {
        size_t pos = 0;
        while ((pos = result.find(w, pos)) != std::string::npos)
        {
            result.replace(pos, w.size(), std::string(w.size(), '-'));
            pos += w.size();
        }
    }
    return result;
}

void sendWithSlidingWindow(int sockfd, sockaddr_in &clientAddr, socklen_t clientLen, const std::string &text)
{
    std::string fullText = text + (char)0x1F;
    std::vector<std::string> blocks;
    for (size_t i = 0; i < fullText.size(); i += 100)
    {
        blocks.push_back(fullText.substr(i, 100));
    }
    size_t total_blocks = blocks.size();
    uint8_t base = global_send_id;
    uint8_t next_seq = global_send_id;
    size_t sent_count = 0;
    bool done = false;
    while (!done)
    {
        while ((sent_count < total_blocks) && (((uint8_t)(next_seq - base)) < WINDOW_SIZE))
        {
            uint8_t packet[BUFFER_SIZE];
            memset(packet, 0, BUFFER_SIZE);
            packet[0] = next_seq;
            const std::string &blk = blocks[sent_count];
            memcpy(&packet[1], blk.data(), blk.size());
            ssize_t s = sendto(sockfd, packet, BUFFER_SIZE, 0, (struct sockaddr*)&clientAddr, clientLen);
            if (s >= 0)
            {
                std::cout << "[sendWithSlidingWindow] Sent ID=" << (int)next_seq << " data='" << blk << "'\n";
            }
            else
            {
                std::cerr << "[sendWithSlidingWindow] sendto error!\n";
            }
            next_seq = (uint8_t)(next_seq + 1);
            sent_count++;
        }
        uint8_t ack[2];
        ssize_t r = recvfrom(sockfd, ack, 2, 0, nullptr, nullptr);
        if (r < 0)
        {
            std::cerr << "[sendWithSlidingWindow] timeout => resend from base=" << (int)base << "\n";
            uint8_t diff = (uint8_t)(next_seq - base);
            if (diff > sent_count) diff = (uint8_t)sent_count;
            sent_count -= diff;
            next_seq = base;
            continue;
        }
        uint8_t ack_type = ack[0];
        uint8_t ack_id = ack[1];
        if (ack_type == 0x06)
        {
            if ((uint8_t)(ack_id + 1) > base)
            {
                base = (uint8_t)(ack_id + 1);
            }
            std::cout << "[sendWithSlidingWindow] ACK(" << (int)ack_id << ") => base=" << (int)base << "\n";
        }
        else
        {
            std::cout << "[sendWithSlidingWindow] NACK(" << (int)ack_id << ") => resend from " << (int)ack_id << "\n";
            uint8_t diff = (uint8_t)(next_seq - ack_id);
            if (diff > sent_count) diff = (uint8_t)sent_count;
            sent_count -= diff;
            next_seq = ack_id;
        }
        if (((size_t)((uint8_t)base - global_send_id)) >= total_blocks)
        {
            done = true;
        }
    }
    global_send_id = next_seq;
    std::cout << "[sendWithSlidingWindow] Done sending, next global_send_id=" << (int)global_send_id << "\n";
}

int main()
{
    int server_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (server_fd < 0)
    {
        std::cerr << "Error: cannot create socket\n";
        return 1;
    }
    timeval tv;
    tv.tv_sec = TIMEOUT_SEC;
    tv.tv_usec = 0;
    setsockopt(server_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    sockaddr_in srvAddr;
    memset(&srvAddr, 0, sizeof(srvAddr));
    srvAddr.sin_family = AF_INET;
    srvAddr.sin_addr.s_addr = INADDR_ANY;
    srvAddr.sin_port = htons(PORT);
    if (bind(server_fd, (sockaddr*)&srvAddr, sizeof(srvAddr)) < 0)
    {
        std::cerr << "Error: bind failed\n";
        close(server_fd);
        return 1;
    }
    std::cout << "UDP Server listening on port " << PORT << "...\n";
    while (true)
    {
        sockaddr_in clientAddr;
        socklen_t clientLen = sizeof(clientAddr);
        memset(&clientAddr, 0, sizeof(clientAddr));
        std::cout << "\n--- Receiving blacklist + text in one stream...\n";
        auto [blacklist_str, original_text] = receiveBlacklistAndText(server_fd, clientAddr, clientLen);
        std::set<std::string> censored_words;
        size_t start = 0;
        while (true)
        {
            size_t pos = blacklist_str.find((char)0x1E, start);
            if (pos == std::string::npos)
            {
                std::string w = blacklist_str.substr(start);
                if (!w.empty()) censored_words.insert(w);
                break;
            }
            else
            {
                std::string w = blacklist_str.substr(start, pos - start);
                if (!w.empty()) censored_words.insert(w);
                start = pos + 1;
            }
        }
        std::cout << "[SERVER] Got blacklist:\n";
        for (auto &w : censored_words)
        {
            std::cout << "   '" << w << "'\n";
        }
        std::cout << "[SERVER] Got text: " << original_text << "\n";
        std::string censored = censorText(original_text, censored_words);
        std::cout << "[SERVER] Censored text: " << censored << "\n";
        std::cout << "\n--- Sending censored text...\n";
        sendWithSlidingWindow(server_fd, clientAddr, clientLen, censored);
        global_expected_id = 0;
        global_send_id = 0;
        global_send_message.clear();
    }
    close(server_fd);
    return 0;
}
