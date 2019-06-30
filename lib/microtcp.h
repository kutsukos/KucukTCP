#ifndef LIB_MICROTCP_H_
#define LIB_MICROTCP_H_

#include <sys/types.h>
#include <sys/socket.h>
#include <stdint.h>

/*
 * Several useful constants
 */
#define MICROTCP_ACK_TIMEOUT_US 200000
#define MICROTCP_MSS 1400
#define MICROTCP_RECVBUF_LEN 8192
#define MICROTCP_WIN_SIZE MICROTCP_RECVBUF_LEN
#define MICROTCP_INIT_CWND (3 * MICROTCP_MSS)
#define MICROTCP_INIT_SSTHRESH MICROTCP_WIN_SIZE

/*
 * What I added kutsukos
 */
#include "../utils/crc32.h"
#include <stdio.h>
#include <string.h>		//memcpy
#include <errno.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <time.h>       /* time 4 rand*/
#include <inttypes.h>	//checksum print
#include <unistd.h>		//size_t
#include <assert.h>		//assert

#define min(a,b) ((a) < (b) ? (a) : (b))


#define ACK    	    4096	//0001000000000000
#define RST		    8192 	//0010000000000000	//useless
#define SYN    	    16384 	//0100000000000000	
#define FIN    	    32768 	//1000000000000000	//useless
#define SYN_ACK 	20480	//0101000000000000
#define FIN_ACK 	36864	//1001000000000000

struct ackList{
  size_t ack;
  struct ackList *next;
};

struct ackList *head;
void ackListInsert(size_t x);	//insert to bottom
int  ackListRemove();			//remove top
void ackListFree();				//free ackList
void printList();

//end


/**
 * Possible states of the microTCP socket
 *
 * NOTE: You can insert any other possible state
 * for your own convenience
 */
typedef enum {
	LISTEN,
	ESTABLISHED,
	CLOSING_BY_PEER,
	CLOSING_BY_HOST,
	CLOSED,
	INVALID,
	UNKNOWN				//To state otan kanw create ena socket
} mircotcp_state_t;


/**
 * This is the microTCP socket structure. It holds all the necessary
 * information of each microTCP socket.
 *
 * NOTE: Fill free to insert additional fields.
 */
typedef struct {
	int			sd;		/**< The underline UDP socket descriptor */
	mircotcp_state_t 	state;		/**< The state of the microTCP socket */
	size_t			init_win_size;	/**< The window size negotiated at the 3-way handshake */
	size_t			curr_win_size;	/**< The current window size */

	uint8_t 		*recvbuf; 	/**< The *receive* buffer of the TCP
	 connection. It is allocated during the connection establishment and
	 is freed at the shutdown of the connection. This buffer is used
	 to retrieve the data from the network. */
	size_t			buf_fill_level; /**< Amount of data in the buffer */

	size_t			cwnd;
	size_t			ssthresh;

	size_t			seq_number; /**< Keep the state of the sequence number */
	size_t			ack_number; /**< Keep the state of the ack number */

	/*
	 *		what I added -kutsukos
	 */
	struct sockaddr_in address;
	socklen_t address_len;
	//end


	} microtcp_sock_t;


/**
 * microTCP header structure
 */
typedef struct {
	uint32_t	seq_number;  /**< Sequence number */
	uint32_t	ack_number;  /**< ACK number */
	uint16_t	control;     /**< Control bits (e.g. SYN, ACK, FIN) */
	uint16_t	window;      /**< Window size in bytes */
	uint32_t	data_len;    /**< Data length in bytes (EXCLUDING header) */
	uint32_t	future_use0; /**< 32-bits for future use */
	uint32_t	future_use1; /**< 32-bits for future use */
	uint32_t	future_use2; /**< 32-bits for future use */
	uint32_t	checksum;    /**< CRC-32 checksum, see crc32() in utils folder */
} microtcp_header_t;


microtcp_sock_t microtcp_socket(int domain, int type, int protocol);

int microtcp_bind(microtcp_sock_t socket, const struct sockaddr *address,socklen_t address_len);

microtcp_sock_t microtcp_connect(microtcp_sock_t socket, const struct sockaddr *address,socklen_t address_len);

microtcp_sock_t microtcp_accept(microtcp_sock_t socket, struct sockaddr *address,socklen_t address_len);

microtcp_sock_t microtcp_shutdown(microtcp_sock_t socket, int how);

ssize_t microtcp_send(microtcp_sock_t *socket, const void *buffer, size_t length, int flags);

ssize_t microtcp_recv(microtcp_sock_t *socket, void *buffer, size_t length, int flags);

ssize_t my_min(size_t a, size_t b, size_t c);
microtcp_sock_t saveAddr(microtcp_sock_t socket, struct sockaddr_in sin);
#endif /* LIB_MICROTCP_H_ */
