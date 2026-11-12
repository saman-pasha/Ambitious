/* src/net/h2.h -- HTTP/2 (RFC 9113) for a client: a connection's frames read and written,
   its streams' states, flow control both ways, its header blocks through HPACK
   (src/net/hpack.h). No sockets: src/net/net.c moves the bytes. src/net/h2.c says how.

   The structs with vectors come after the prototypes (toolchain/REPORT.md, finding 22). */
#ifndef NET_H2_H
#define NET_H2_H
#include <stdint.h>
#include <stddef.h>
#include <vector.pl>
#include "hpack.h"

/* frame types, flags and error codes (RFC 9113, 6 and 7) */
enum { H2_DATA = 0, H2_HEADERS = 1, H2_PRIORITY = 2, H2_RST_STREAM = 3, H2_SETTINGS = 4, H2_PUSH_PROMISE = 5, H2_PING = 6, H2_GOAWAY = 7,
       H2_WINDOW_UPDATE = 8, H2_CONTINUATION = 9 };
enum { H2_END_STREAM = 1, H2_ACK = 1, H2_END_HEADERS = 4, H2_PADDED = 8, H2_PRIORITY_FLAG = 32 };
enum { H2_NO_ERROR = 0, H2_PROTOCOL_ERROR = 1, H2_INTERNAL_ERROR = 2, H2_FLOW_CONTROL_ERROR = 3, H2_SETTINGS_TIMEOUT = 4,
       H2_STREAM_CLOSED = 5, H2_FRAME_SIZE_ERROR = 6, H2_REFUSED_STREAM = 7, H2_CANCEL = 8, H2_COMPRESSION_ERROR = 9,
       H2_CONNECT_ERROR = 10, H2_ENHANCE_YOUR_CALM = 11, H2_INADEQUATE_SECURITY = 12, H2_HTTP_1_1_REQUIRED = 13 };

/* the settings Chromium sends first, and the window it grants the connection at once
   (measured by a TLS front logging its frames: tests/net/h2serve.js) */
enum { H2_OUR_TABLE = 65536, H2_OUR_WINDOW = 6291456, H2_OUR_HEADER_LIST = 262144, H2_OUR_CONN_WINDOW = 15728640,
       H2_FRAME_MAX = 16384 };

/* what a frame read came to: nothing for its user (a SETTINGS, a PING, a PRIORITY, a
   frame of a stream closed); a stream's response head or trailers; its data; its reset;
   the peer's GOAWAY; a window opened (a request's body may go on); the peer's settings
   (more streams may open); or an error of the connection, which is then dead */
enum { H2_EV_NONE, H2_EV_HEADERS, H2_EV_DATA, H2_EV_RESET, H2_EV_GOAWAY, H2_EV_WINDOW, H2_EV_SETTINGS, H2_EV_ERROR };

typedef struct h2_event {
    uint32_t kind, stream, end_stream;
    uint32_t code;          /* RST_STREAM's or GOAWAY's error code, or the connection error's */
    uint32_t last;          /* GOAWAY's last stream: those after it were not processed */
    uint32_t status;        /* HEADERS: the response's :status; 0 for trailers */
    size_t off, len;        /* DATA: its payload's place in the bytes read */
    char why[240];          /* a reset's or an error's words */
} h2_event;

/* a stream the client opened, until it closes: its priority (0, the highest, to 4) and
   whether it sent its END_STREAM and got the peer's; the windows both ways and what it
   received since its last WINDOW_UPDATE; whether its response's head has come */
typedef struct h2_stream {
    uint32_t id, priority, sent_end, got_end, got_head, unacked;
    int64_t send_window, recv_window;
} h2_stream;

typedef struct h2_conn h2_conn;

/* a connection begun as Chromium begins one: the preface, its SETTINGS (header table
   65536, push off, a stream window of 6 MB, header lists to 256 KB) and a WINDOW_UPDATE
   of the connection to 15 MB, into out */
void h2_start(h2_conn *c, vector<uint8_t> *out);
/* a request's header block sent on a new stream, as HEADERS and as many CONTINUATIONs as
   the peer's frame size asks, with END_STREAM when it has no body, and the PRIORITY of
   Chromium's tree for priority (0 to 4): an exclusive dependency on the newest open
   stream of the same priority, else of the next higher one, its weight the SPDY
   priority's. The stream's id */
uint32_t h2_request(h2_conn *c, const hpack_field *f, size_t nf, const uint8_t *bytes, int end_stream, uint32_t priority,
                    vector<uint8_t> *out);
/* as much of a request body's n bytes as the windows and the peer's frame size allow,
   as DATA, the last with END_STREAM when end is 1 and all of it went: how many went */
size_t h2_data(h2_conn *c, uint32_t stream, const uint8_t *p, size_t n, int end, vector<uint8_t> *out);
/* the next whole frame of in (n bytes) read: 0 while it has not all come; else 1, its
   bytes in *used, what it came to in ev -- a head's fields (and their bytes) appended to
   fields and fbytes -- and what the connection answers (SETTINGS' ACK, PING's,
   WINDOW_UPDATEs, a reset or GOAWAY for an error) appended to out */
int h2_read(h2_conn *c, const uint8_t *in, size_t n, size_t *used, h2_event *ev, vector<hpack_field> *fields, vector<uint8_t> *fbytes,
            vector<uint8_t> *out);
/* a stream reset by the client, and forgotten */
void h2_reset(h2_conn *c, uint32_t stream, uint32_t code, vector<uint8_t> *out);
/* a GOAWAY sent: the connection closes */
void h2_goaway(h2_conn *c, uint32_t code, vector<uint8_t> *out);
/* whether a new stream may open: within the peer's SETTINGS_MAX_CONCURRENT_STREAMS,
   before any GOAWAY and any error, its ids not run out */
int h2_can_open(const h2_conn *c);
uint32_t h2_open_streams(const h2_conn *c);
/* a stream's send window, for its body: 0 when it is not open */
int64_t h2_send_window(const h2_conn *c, uint32_t stream);

/* its vectors dropped, by its owner (toolchain/REPORT.md, finding 10) */
#define H2_CONN_DROP(c) HPACK_TABLE_DROP((c).enc); HPACK_TABLE_DROP((c).dec); vector_h2_stream_drop(&(c).streams); vector_uint8_t_drop(&(c).block)

/* the connection: its HPACK tables, its open streams, a header block owed its
   CONTINUATIONs (its stream, its END_STREAM, whether it is a stream's own or a closed
   one's, discarded), the next stream's id; the peer's settings, whether its first
   SETTINGS came; the windows both ways and what came since the last WINDOW_UPDATE; the
   peer's GOAWAY and its last stream; dead after an error */
struct h2_conn {
    hpack_table enc, dec;
    vector<h2_stream> streams;
    vector<uint8_t> block;
    uint32_t block_stream, block_end, block_discard;
    uint32_t next_id;
    uint32_t peer_table, peer_max_streams, peer_window, peer_frame, peer_header_list, settings_seen;
    int64_t send_window, recv_window;
    uint32_t unacked;
    uint32_t goaway, goaway_last, dead;
};

#endif
