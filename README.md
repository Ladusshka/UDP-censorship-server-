# UDP-censorship-server-
using sliding window using sliding window



UDP Censorship Server with Reliable Sliding Window Protocol

This project implements a UDP server that performs text censorship using a custom, reliable communication scheme based on a sliding window. The communication occurs in two phases with role reversal:

    Phase 1 – Client-to-Server (Data Reception):
    The client initially sends a complete blacklist and a text message to the server following a specific communication protocol.
        The data is transmitted in UDP datagrams.
        Each datagram consists of an 8-bit datagram ID followed by 100 characters of text (the last datagram may contain fewer than 100 characters if it marks the end of the text).
        Within the datagrams, the byte 0x1E is used exclusively as a delimiter between words in the blacklist, while the byte 0x1F indicates the end of the blacklist or the end of the text. (Apart from signaling termination, these bytes have no other significance.)
        The client is the sender in this phase, and the server acts as the receiver.

    Phase 2 – Server-to-Client (Response Transmission):
    Once the server has received the complete text, it processes the text by censoring words that appear in the provided blacklist. After censorship, the roles are reversed:
        The server now sends the censored text back to the client using the same communication scheme (i.e., datagrams with an 8-bit ID followed by 100 characters).
        The datagram termination is marked by 0x1F as before.

Reliable Communication via Sliding Window

To ensure reliability over UDP (which is inherently connectionless and unreliable), the protocol implements a simple sliding window mechanism with the following characteristics:

    Datagram Structure:
    Each datagram is constructed as follows:
        1st byte: 8-bit datagram ID.
        Next 100 bytes: A segment of the text (fewer than 100 bytes for the last segment).

    Acknowledgements:
    The receiver confirms the receipt of each datagram with a two-byte message:
        1st byte: ASCII 0x06 for ACK or 0x15 for NACK.
        2nd byte: The ID of the corresponding datagram.

    Window Size:
    The fixed window size is 5 datagrams. This means the sender may transmit up to 5 datagrams before waiting for acknowledgements.

    Error Handling:
        If the receiver gets a datagram with an ID that does not match the expected ID (possibly due to reordering), it responds with a NACK containing the expected ID.
        Upon receiving a NACK, the sender must retransmit all datagrams starting from that ID.
        If an ACK is received, the sender may consider all datagrams up to that ID as confirmed, even if some IDs were skipped.
        If no datagram (or even an empty one) is received within 5 seconds, the situation is considered a timeout, and the sender must retransmit the unacknowledged datagrams.

Example Communication (Using Blocks of 22 Characters and a Window of 3 for Clarity)

Sender (Client) sends:

Datagram 0: '0x0' + "ipsum" + 0x1E + "gue" + 0x1E + "tor" + 0x1F + "Lorem ip"
Datagram 1: '0x1' + "sum dolor sit amet, co"
Datagram 2: '0x2' + "nsectetur adipiscing e"

Receiver (Server) responds with:

ACK: '0x6' + '0x0'
ACK: '0x6' + '0x1'   // (Assume datagram 1 was lost)
ACK: '0x6' + '0x2'

Sender then retransmits:

Datagram 3: '0x3' + "lit. Quisque auctor ne"   // (This datagram was lost)
Datagram 4: '0x4' + "que congue tortor viv" + 0x1F

Receiver responds with:

NACK: '0x15' + '0x3'

Sender retransmits again:

Datagram 3: '0x3' + "lit. Quisque auctor ne"
Datagram 4: '0x4' + "que congue tortor viv" + 0x1F

Implementation Details

    UDP Server Implementation:
    Unlike a TCP server, the UDP server does not use functions like listen and accept. Instead, it uses the general-purpose functions sendto and recvfrom.

    Communication Flow:
        Reception Phase: The server receives the complete blacklist and text using the described datagram format.
            The client’s message contains the blacklist (with words separated by 0x1E) followed by a 0x1F indicating the end of the blacklist, and then the text terminated by a final 0x1F.
        Processing Phase: The server splits the blacklist into words, censors the text by replacing each occurrence of a blacklisted word with dashes (–), and prepares the censored text for sending.
        Response Phase: The server sends the censored text back to the client using the same sliding window protocol.

    Timeouts:
    If no datagram is received within 5 seconds, the sender (client or server) considers it a timeout and triggers the retransmission mechanism.

This project demonstrates how to build a reliable application-level protocol over UDP using a sliding window mechanism with ACK/NACK messages. It provides a robust solution for text censorship where both the blacklist and text are transmitted reliably over an unreliable UDP network.
