#include "microtcp.h"
#include "../utils/crc32.h"

microtcp_sock_t microtcp_socket(int domain, int type, int protocol)
{
	microtcp_sock_t new_sock;
    new_sock.sd = socket(domain, type, protocol);

    if(new_sock.sd == -1){
        perror("MicroTCP Socket");
        new_sock.state = INVALID;
        return new_sock;
    }

    new_sock.state = UNKNOWN;
    new_sock.init_win_size = MICROTCP_WIN_SIZE;
    new_sock.curr_win_size = MICROTCP_WIN_SIZE;
	
	//before start sending we have to set threshold
	new_sock.ssthresh=MICROTCP_INIT_SSTHRESH;
	new_sock.cwnd=MICROTCP_INIT_CWND;
	
	
	
    return new_sock;
}

int microtcp_bind(microtcp_sock_t socket, const struct sockaddr *address, socklen_t address_len)
{
	int mbind=0;	//my bind

	mbind=bind(socket.sd,address,address_len);		
	if(mbind<0){		//if error in bind mbind==-1<0
		perror("microTCP bind");
		return mbind;
	}
	return mbind;
}

microtcp_sock_t microtcp_connect(microtcp_sock_t socket, const struct sockaddr *address,socklen_t address_len)
{
		uint32_t	checkSum1;
		uint32_t	TMPcheckSum;
		uint8_t buffer[MICROTCP_RECVBUF_LEN];
		int tmp_recvfrom;
		microtcp_header_t *recv_head_pack=(microtcp_header_t *)malloc(sizeof(microtcp_header_t));
		microtcp_header_t send_head_pack;
		microtcp_header_t check_head_pack;
		int i;
		
		//Connect save the address in socket
		//socket.address=address;
		//socket.address_len=address_len;
		
		//Client starts 3way handshake. 1st packet
		srand(time(NULL));
		send_head_pack.seq_number = htonl(rand()%1000+1);	//N random
		send_head_pack.ack_number = 0;
		send_head_pack.control = htons(SYN);
		send_head_pack.window = 0;
		send_head_pack.data_len = 0;
		send_head_pack.future_use0 = 0;
		send_head_pack.future_use1 = 0;
		send_head_pack.future_use2 = 0;
		send_head_pack.checksum = 0;
		
		for(i=0;i<MICROTCP_RECVBUF_LEN;i++)
			buffer[i]=0;
		memcpy(buffer,&send_head_pack,sizeof(microtcp_header_t));
		TMPcheckSum=crc32(buffer,sizeof(buffer));
		send_head_pack.checksum=htonl(TMPcheckSum);
		
		if (sendto(socket.sd,(void *)&send_head_pack,sizeof(microtcp_header_t),0,address,address_len) <0){
			socket.state=INVALID;
			perror("microTCP connect unable to send the first SYN");
			return socket;
		}

		//End of first packet
		//Waiting for the second
		tmp_recvfrom=recvfrom(socket.sd,recv_head_pack,sizeof(microtcp_header_t),0,address,&address_len);
		if(tmp_recvfrom == -1){
			perror("microTCP connect unable to receive the second syn ack");
			exit(EXIT_FAILURE);
		}
		
		TMPcheckSum=ntohl(recv_head_pack->checksum);	//copy into temp checksum
		
		check_head_pack.seq_number = recv_head_pack->seq_number;
		check_head_pack.ack_number = recv_head_pack->ack_number;
		check_head_pack.control = recv_head_pack->control;
		check_head_pack.window = recv_head_pack->window;
		check_head_pack.data_len = 0;
		check_head_pack.future_use0 = 0;
		check_head_pack.future_use1 = 0;
		check_head_pack.future_use2 = 0;
		check_head_pack.checksum = 0;
		
		for(i=0;i<MICROTCP_RECVBUF_LEN;i++)
			buffer[i]=0;
		memcpy(buffer,&check_head_pack,sizeof(microtcp_header_t));
		checkSum1=crc32(buffer,sizeof(buffer));
		if(checkSum1!=TMPcheckSum){	//error on checksum
			socket.state=INVALID;
			perror("microTCP connect connection error - 2nd packet - error checksum");
			return socket;
		}		
		
		//Now we check if 2nd handshake was a SYN_ACK
		recv_head_pack->control=ntohs(recv_head_pack->control);
		if(recv_head_pack->control!=SYN_ACK){
			socket.state=INVALID;
			perror("microTCP conenct connection error - 2nd packet is not SYN ACK");
			return socket;
		}
		
		//Getting window from server side
		socket.curr_win_size=ntohs(recv_head_pack->window);
		
		recv_head_pack->seq_number = ntohl(recv_head_pack->seq_number);
		send_head_pack.seq_number = recv_head_pack->ack_number;
		send_head_pack.ack_number = htonl(recv_head_pack->seq_number+1);
		send_head_pack.control = htons(ACK);
		send_head_pack.window = htons(socket.curr_win_size);
		send_head_pack.data_len = 0;
		send_head_pack.future_use0 = 0;
		send_head_pack.future_use1 = 0;
		send_head_pack.future_use2 = 0;
		send_head_pack.checksum = 0;
		for(i=0;i<MICROTCP_RECVBUF_LEN;i++)
			buffer[i]=0;
		memcpy(buffer,&send_head_pack,sizeof(send_head_pack));
		TMPcheckSum=crc32(buffer,sizeof(buffer));
		send_head_pack.checksum=htonl(TMPcheckSum);

		//last packet of the handsake
		if (sendto(socket.sd,(void *)&send_head_pack,sizeof(microtcp_header_t),0,address,address_len) <0){
			socket.state=INVALID;
			perror("microTCP connect error - While 3rd packet send");
			return socket;
		}
		
		//We guess the connection is ESTABLISHED and we save the last seq,ack numbers
		socket.state=ESTABLISHED;
		socket.seq_number=ntohl(send_head_pack.seq_number);
		socket.ack_number=ntohl(send_head_pack.ack_number);
		return socket;
}

microtcp_sock_t microtcp_accept(microtcp_sock_t socket, struct sockaddr *address,socklen_t address_len)
{
	int tmp_recvfrom;		//tmp for recvfrom
	uint8_t buffer[MICROTCP_RECVBUF_LEN];
	microtcp_header_t *recv_head_pack=(microtcp_header_t *)malloc(sizeof(microtcp_header_t));
	microtcp_header_t send_head_pack;
	microtcp_header_t check_head_pack;
	  uint32_t	checkSum1;
	  uint32_t	TMPcheckSum;
	  int i;
	
	//Accept save the address in socket
	//socket.address=address;
	//socket.address_len=address_len;
	
	//time to call recvfrom and wait for a connection 1st handshake
	tmp_recvfrom=recvfrom(socket.sd,recv_head_pack,sizeof(microtcp_header_t),0,address,&address_len);
	if(tmp_recvfrom == -1){
		//error at waiting for a connection
		perror("microTCP accept connection fail (1st recv)");
		exit(EXIT_FAILURE);		//HELP! We exit or return an INVALID socket
	}

	//We received a packet. Time to check checksum bits
	TMPcheckSum=ntohl(recv_head_pack->checksum);	//copy packets checksum
	
	check_head_pack.seq_number = recv_head_pack->seq_number;
	check_head_pack.ack_number = recv_head_pack->ack_number;
	check_head_pack.control = recv_head_pack->control;
	check_head_pack.window = 0;
	check_head_pack.data_len = 0;
	check_head_pack.future_use0 = 0;
	check_head_pack.future_use1 = 0;
	check_head_pack.future_use2 = 0;
	check_head_pack.checksum = 0;
	
	for(i=0;i<MICROTCP_RECVBUF_LEN;i++)
			buffer[i]=0;
	memcpy(buffer,&check_head_pack,sizeof(microtcp_header_t));
	checkSum1=crc32(buffer,sizeof(buffer));					//exec crc and then check if I got the same value
	
	if(checkSum1!=TMPcheckSum){	//error on checksum
		socket.state=INVALID;
		perror("microTCP accept connection error - 1st packet - error checksum");
		return socket;
	}

	//Now we check if 1st handshake was a SYN
	recv_head_pack->control=ntohs(recv_head_pack->control);
	if(recv_head_pack->control!=SYN){
		socket.state=INVALID;
		perror("microTCP accept connection error - 1st packet is not SYN");
		return socket;
	}

	//first a random number for seq_number
	srand(time(NULL));

	//ELSE we can continue by sending the 2nd handshake
	recv_head_pack->seq_number=ntohl(recv_head_pack->seq_number);	//N:received seq
	send_head_pack.seq_number = ntohl(rand()%1000+1);//M:random number.
	send_head_pack.ack_number = htonl(recv_head_pack->seq_number+1);	//ack=N+1
	send_head_pack.control = htons(SYN_ACK);
	send_head_pack.window = htons(MICROTCP_WIN_SIZE);
	send_head_pack.data_len=0;
	send_head_pack.future_use0 = 0;		//phase2
	send_head_pack.future_use1  = 0;	//phase2
	send_head_pack.future_use2 = 0;		//phase2
	send_head_pack.checksum = 0;		//we fill it later
	//Time to  create checksum
	for(i=0;i<MICROTCP_RECVBUF_LEN;i++)
			buffer[i]=0;
	memcpy(buffer,&send_head_pack,sizeof(microtcp_header_t));
	TMPcheckSum=crc32(buffer,sizeof(buffer));
	send_head_pack.checksum=htonl(TMPcheckSum);
	
	//updating window size on socket
	socket.init_win_size=MICROTCP_WIN_SIZE;

	//Now we can send the 2nd packet of 3way handshake
	if (sendto(socket.sd,(void *)&send_head_pack,sizeof(microtcp_header_t),0,address,address_len) <0){
		socket.state=INVALID;
		perror("microTCP accept connection error - While 2nd packet send");
		return socket;
	}
	//ELSE we wait for the 3rd packet-message to complete accept and establishing the connection
	tmp_recvfrom=recvfrom(socket.sd,recv_head_pack,sizeof(microtcp_header_t),0,address,&address_len);
	if(tmp_recvfrom == -1){
		//error at waiting for a connection
		perror("microTCP accept connection fail (3rd recv)");
		exit(EXIT_FAILURE);		//HELP! We exit or return an INVALID socket
	}

	//Time to check checksum bits again
	TMPcheckSum=ntohl(recv_head_pack->checksum);
	check_head_pack.seq_number = recv_head_pack->seq_number;
	check_head_pack.ack_number = recv_head_pack->ack_number;
	check_head_pack.control = recv_head_pack->control;
	check_head_pack.window = recv_head_pack->window;
	check_head_pack.data_len = 0;
	check_head_pack.future_use0 = 0;
	check_head_pack.future_use1 = 0;
	check_head_pack.future_use2 = 0;
	check_head_pack.checksum = 0;
	for(i=0;i<MICROTCP_RECVBUF_LEN;i++)
			buffer[i]=0;
	memcpy(buffer,&check_head_pack,sizeof(check_head_pack));
	checkSum1=crc32(buffer,sizeof(buffer));
	if(checkSum1!=TMPcheckSum){	//error on checksum
		socket.state=INVALID;
		perror("microTCP accept connection error - 3rd packet - error checksum");
		return socket;
	}
	//Now we check if 3rd handshake was an ACK
	recv_head_pack->control=ntohs(recv_head_pack->control);
	if(recv_head_pack->control!=ACK){
		socket.state=INVALID;
		perror("microTCP accept connection error - 3rd packet is not ACK");
		return socket;
	}
	//Now lets check for seq number and ack
	recv_head_pack->ack_number=ntohl(recv_head_pack->ack_number);
	recv_head_pack->seq_number=ntohl(recv_head_pack->seq_number);
	send_head_pack.seq_number=ntohl(send_head_pack.seq_number);
	send_head_pack.ack_number=ntohl(send_head_pack.ack_number);
	
	if(recv_head_pack->seq_number!=send_head_pack.ack_number ||
		recv_head_pack->ack_number!=send_head_pack.seq_number+1){
			perror("microTCP accept connection error - 3rd packet -error numbers");
			socket.state = INVALID;
			return socket;
		}
	//ELSE The 3way handshake is successfull! Connection Established
	socket.ack_number = recv_head_pack->ack_number;
	socket.seq_number = recv_head_pack->seq_number+1;
	socket.state = ESTABLISHED;
	return socket;
}

microtcp_sock_t microtcp_shutdown(microtcp_sock_t socket, int how)
{
	ssize_t tmp_recvfrom;
	uint8_t buffer[MICROTCP_RECVBUF_LEN];
	microtcp_header_t *recv_head_pack=(microtcp_header_t *)malloc(sizeof(microtcp_header_t));
	microtcp_header_t send_head_pack;
	microtcp_header_t check_head_pack;
	  uint32_t	checkSum1;
	  uint32_t	TMPcheckSum;
	int i;
	
	//debug
	
	
	if(socket.state==CLOSING_BY_PEER){		//ServerSide-I have already received the first FIN_ACK packet from client
		//We create and send the 2nd message for the termination of the connection
		send_head_pack.seq_number=0;	
		send_head_pack.ack_number=htonl(socket.seq_number+1);	//X+1
		send_head_pack.control=htons(ACK);
		send_head_pack.window=htons(socket.curr_win_size);
		send_head_pack.data_len=0;
		send_head_pack.future_use0=0;
		send_head_pack.future_use1=0;
		send_head_pack.future_use2=0;
		send_head_pack.checksum=0;
		//Time to  create checksum
		for(i=0;i<MICROTCP_RECVBUF_LEN;i++)
				buffer[i]=0;
		memcpy(buffer,&send_head_pack,sizeof(send_head_pack));
		TMPcheckSum=crc32(buffer,sizeof(buffer));
		send_head_pack.checksum=htonl(TMPcheckSum);
		if (sendto(socket.sd,(void *)&send_head_pack,sizeof(microtcp_header_t),0,(struct sockaddr *)&socket.address,socket.address_len) <0){
			socket.state=INVALID;
			perror("microTCP Shutdown connection error - While 2nd packet send");
			return socket;
		}
		

		//Now we create and send the 3rd message and check if state is changed by client
		srand(time(NULL));
		send_head_pack.seq_number=rand()%1000+1;					//Y
		send_head_pack.ack_number=htonl(socket.seq_number+1);
		send_head_pack.control=htons(FIN_ACK);
		send_head_pack.window=htons(socket.curr_win_size);
		send_head_pack.data_len=0;
		send_head_pack.future_use0=0;
		send_head_pack.future_use1=0;
		send_head_pack.future_use2=0;
		send_head_pack.checksum=0;
		//Time to  create checksum
		for(i=0;i<MICROTCP_RECVBUF_LEN;i++)
				buffer[i]=0;
		memcpy(buffer,&send_head_pack,sizeof(send_head_pack));
		TMPcheckSum=crc32(buffer,sizeof(buffer));
		send_head_pack.checksum=htonl(TMPcheckSum);
		if (sendto(socket.sd,(void *)&send_head_pack,sizeof(microtcp_header_t),0,(struct sockaddr *)&socket.address,socket.address_len) <0){
			socket.state=INVALID;
			perror("microTCP Shutdown connection error - While 3rd packet send");
			return socket;
		}
		
		//Now we wait for the 4th message
		tmp_recvfrom=recvfrom(socket.sd,recv_head_pack,sizeof(microtcp_header_t),0,(struct sockaddr *)&socket.address,&socket.address_len);
		if(tmp_recvfrom == -1){
			//error at waiting for a connection
			perror("microTCP shutdown connection fail (2nd packet recv)");
			exit(EXIT_FAILURE);		//HELP! We exit or return an INVALID socket
		}
		//Lets check it
		TMPcheckSum=ntohl(recv_head_pack->checksum);	//copy packets checksum	
		check_head_pack.seq_number = recv_head_pack->seq_number;
		check_head_pack.ack_number = recv_head_pack->ack_number;
		check_head_pack.control = recv_head_pack->control;
		check_head_pack.window = recv_head_pack->window;
		check_head_pack.data_len = 0;
		check_head_pack.future_use0 = 0;
		check_head_pack.future_use1 = 0;
		check_head_pack.future_use2 = 0;
		check_head_pack.checksum = 0;
		
		for(i=0;i<MICROTCP_RECVBUF_LEN;i++)
				buffer[i]=0;
		memcpy(buffer,&check_head_pack,sizeof(microtcp_header_t));
		checkSum1=crc32(buffer,sizeof(buffer));					//exec crc and then check if I got the same value
		
		if(checkSum1!=TMPcheckSum){	//error on checksum
			socket.state=INVALID;
			perror("microTCP shutdown connection error - 4th packet - error checksum");
			return socket;
		}
		recv_head_pack->control=ntohs(recv_head_pack->control);
		if(recv_head_pack->control!=ACK){
			socket.state=INVALID;
			perror("microTCP shutdown connection error - 4th packet is not ACK");
			return socket;
		}
		
		if(ntohl(recv_head_pack->seq_number)!=ntohl(send_head_pack.ack_number) ||
			ntohl(recv_head_pack->ack_number)!=ntohl(send_head_pack.seq_number)+1){
				socket.state=INVALID;
				perror("microTCP shutdown connection error - 4th packet - error seq/ack numbers");
				return socket;
			}
	}
	else{
		//Now we create the 1st FINACKpacket for sending
		send_head_pack.seq_number=htonl(socket.seq_number+1);	//X
		send_head_pack.ack_number=0;
		send_head_pack.control=htons(FIN_ACK);
		send_head_pack.window=htons(socket.curr_win_size);
		send_head_pack.data_len=0;
		send_head_pack.future_use0=0;
		send_head_pack.future_use1=0;
		send_head_pack.future_use2=0;
		send_head_pack.checksum=0;

		//Time to  create checksum
		for(i=0;i<MICROTCP_RECVBUF_LEN;i++)
				buffer[i]=0;
		memcpy(buffer,&send_head_pack,sizeof(send_head_pack));
		TMPcheckSum=crc32(buffer,sizeof(buffer));
		send_head_pack.checksum=htonl(TMPcheckSum);
		if (sendto(socket.sd,(void *)&send_head_pack,sizeof(microtcp_header_t),0,(struct sockaddr *)&socket.address,socket.address_len) <0){
			socket.state=INVALID;
			perror("microTCP Shutdown connection error - While 1st packet send");
			return socket;
		}

		//Now we wait for two packets from server
		//First is a ACK witch ack=X+1
		tmp_recvfrom=recvfrom(socket.sd,recv_head_pack,sizeof(microtcp_header_t),0,(struct sockaddr *)&socket.address,&socket.address_len);
		if(tmp_recvfrom == -1){
			//error at waiting for a connection
			perror("microTCP shutdown connection fail (2nd packet recv)");
			exit(EXIT_FAILURE);		//HELP! We exit or return an INVALID socket
		}

		//Time to check checksum bits again
		TMPcheckSum=ntohl(recv_head_pack->checksum);
		check_head_pack.seq_number = recv_head_pack->seq_number;
		check_head_pack.ack_number = recv_head_pack->ack_number;
		check_head_pack.control = recv_head_pack->control;
		check_head_pack.window = recv_head_pack->window;
		check_head_pack.data_len = 0;
		check_head_pack.future_use0 = 0;
		check_head_pack.future_use1 = 0;
		check_head_pack.future_use2 = 0;
		check_head_pack.checksum = 0;
		for(i=0;i<MICROTCP_RECVBUF_LEN;i++)
				buffer[i]=0;
		memcpy(buffer,&check_head_pack,sizeof(check_head_pack));
		checkSum1=crc32(buffer,sizeof(buffer));
		if(checkSum1!=TMPcheckSum){	//error on checksum
			socket.state=INVALID;
			perror("microTCP shutdown connection error - 2nd packet - error checksum");
			return socket;
		}

		//Now we check if snd handshake was an ACK
		recv_head_pack->control=ntohs(recv_head_pack->control);
		if(recv_head_pack->control!=ACK){
			socket.state=INVALID;
			perror("microTCP shutdown connection error - 2nd packet is not ACK");
			return socket;
		}
		//Now lets check for seq number and ack
		recv_head_pack->ack_number=ntohl(recv_head_pack->ack_number);
		send_head_pack.seq_number=ntohl(send_head_pack.seq_number);
		if(recv_head_pack->ack_number!=send_head_pack.seq_number+1){
				perror("microTCP shutdown connection error - 2nd packet -error ack numbers");
				printf("I was waiting for \n");
				printf("%" PRIu32 "\n",send_head_pack.seq_number+1);
				printf("and I got \n");
				printf("%" PRIu32 "\n",recv_head_pack->ack_number);
				socket.state = INVALID;
				return socket;
			}
		
		//changing socket.state for CLIENT/PEER
		socket.state = CLOSING_BY_HOST;

		//Now we wait for the 3rd packet to be send. This is the second from the server. It should be an FIN ACK
		tmp_recvfrom=recvfrom(socket.sd,recv_head_pack,sizeof(microtcp_header_t),0,(struct sockaddr *)&socket.address,&socket.address_len);
		if(tmp_recvfrom == -1){
			//error at waiting for a connection
			perror("microTCP shutdown connection fail (3rd packet recv)");
			exit(EXIT_FAILURE);		//HELP! We exit or return an INVALID socket
		}

		//Time to check checksum bits again
		TMPcheckSum=ntohl(recv_head_pack->checksum);
		check_head_pack.seq_number = recv_head_pack->seq_number;
		check_head_pack.ack_number = recv_head_pack->ack_number;
		check_head_pack.control = recv_head_pack->control;
		check_head_pack.window = recv_head_pack->window;
		check_head_pack.data_len = 0;
		check_head_pack.future_use0 = 0;
		check_head_pack.future_use1 = 0;
		check_head_pack.future_use2 = 0;
		check_head_pack.checksum = 0;
		for(i=0;i<MICROTCP_RECVBUF_LEN;i++)
				buffer[i]=0;
		memcpy(buffer,&check_head_pack,sizeof(check_head_pack));
		checkSum1=crc32(buffer,sizeof(buffer));
		if(checkSum1!=TMPcheckSum){	//error on checksum
			socket.state=INVALID;
			perror("microTCP shutdown connection error - 3rd packet - error checksum");
			return socket;
		}

		//Now we check if snd handshake was an FINACK
		recv_head_pack->control=ntohs(recv_head_pack->control);
		if(recv_head_pack->control!=FIN_ACK){
			socket.state=INVALID;
			perror("microTCP shutdown connection error - 3rd packet is not FIN ACK");
			return socket;
		}
		recv_head_pack->seq_number=ntohl(recv_head_pack->seq_number);
		recv_head_pack->ack_number=ntohl(recv_head_pack->ack_number);
		//Now it is time to send the final message-packet and close the connection
	
		send_head_pack.seq_number=htonl(recv_head_pack->ack_number);	//X+1
		send_head_pack.ack_number=htonl(recv_head_pack->seq_number+1);			//Y+1
		send_head_pack.control=htons(ACK);
		send_head_pack.window=htons(socket.curr_win_size);
		send_head_pack.data_len=0;
		send_head_pack.future_use0=0;
		send_head_pack.future_use1=0;
		send_head_pack.future_use2=0;
		send_head_pack.checksum=0;

		//Time to  create checksum
		for(i=0;i<MICROTCP_RECVBUF_LEN;i++)
				buffer[i]=0;
		memcpy(buffer,&send_head_pack,sizeof(send_head_pack));
		TMPcheckSum=crc32(buffer,sizeof(buffer));
		send_head_pack.checksum=htonl(TMPcheckSum);
		if (sendto(socket.sd,(void *)&send_head_pack,sizeof(microtcp_header_t),0,(struct sockaddr *)&socket.address,socket.address_len) <0){
			socket.state=INVALID;
			perror("microTCP Shutdown connection error - While 4th packet send");
		}
	}
	
	
	
	
	socket.state=CLOSED;
	return socket;
}

ssize_t microtcp_send(microtcp_sock_t *socket, const void *buffer, size_t length, int flags)
{
	size_t remaining;
	size_t data_sent, bytes_to_send;
	size_t verified_data_sent;
	size_t data_sent_on_loop;
	size_t chunks;
	size_t semiPackSize;	
	
	//data to be send
	microtcp_header_t header2send;
	void *buffer2send;
	size_t seq_number=socket->seq_number;				//initializing seq numbers from socket saved info
	
	//checking tmp vars
	uint32_t tmpCheckSum0;
	uint32_t tmpCheckSum1;
	
	//4 acks
	ssize_t tmp_recvfrom;
	microtcp_header_t *recv_head_pack=(microtcp_header_t *)malloc(sizeof(microtcp_header_t));
	microtcp_header_t check_head_pack;
	void *buffer4rcved=malloc(sizeof(check_head_pack));;
	size_t ack_number_rcvd=0;
	int dupCount;
	
	//retransmit vars
	int retransmit=0;
	
	//etc
	int i;
	int x;
	
	//assert buffers
	assert(buffer2send);
	assert(buffer4rcved);
	
	//Timeout timer for socket
	struct timeval tv;
	tv.tv_sec = 0;
	tv.tv_usec = MICROTCP_ACK_TIMEOUT_US;
	
	if(buffer==NULL){
		perror("\nBuffer is null");
		return 0;	//no data sent
	}
	
	data_sent=0;
	remaining=length;
	
	while(data_sent<length){
		verified_data_sent=0;
		data_sent_on_loop=0;
		retransmit=0;
		
		buffer2send=malloc(MICROTCP_MSS);
		assert(buffer2send);
		
		bytes_to_send=my_min(socket->curr_win_size ,socket->cwnd,remaining);
		chunks=bytes_to_send/(MICROTCP_MSS -sizeof(microtcp_header_t));
		
		for(i=0;i<chunks;i++){
			//init header first
			seq_number++;
			
			ackListInsert(seq_number+1);					//insert seq+1 for ack checking
			
			header2send.seq_number=htonl(seq_number);
			header2send.ack_number=0;
			header2send.control=0;
			header2send.window=0;
			header2send.data_len=htonl(MICROTCP_MSS -sizeof(microtcp_header_t));
			header2send.future_use0=0;
			header2send.future_use1=0;
			header2send.future_use2=0;
			header2send.checksum=0;
			
			//init data to be sent
			memcpy(buffer2send, &header2send, sizeof(microtcp_header_t));
			memcpy(buffer2send +sizeof(microtcp_header_t), buffer +data_sent, MICROTCP_MSS -sizeof(microtcp_header_t));
      
			//create checksum
			tmpCheckSum0=crc32(buffer2send,sizeof(buffer2send));
			header2send.checksum=htonl(tmpCheckSum0);
			
			//insert checksum inside buffer2send
			memcpy(buffer2send, &header2send, sizeof(microtcp_header_t));
			memcpy(buffer2send +sizeof(microtcp_header_t), buffer +data_sent, MICROTCP_MSS -sizeof(microtcp_header_t));
			
			//Now this chunk is ready to be sent
			if (sendto(socket->sd,buffer2send,MICROTCP_MSS,0,(struct sockaddr *)&socket->address,socket->address_len) <0){
				socket->state=INVALID;
				perror("microTCP Send  - while trying to send");
				free(buffer2send);
				return -1;
			}
			data_sent = data_sent + MICROTCP_MSS - sizeof(microtcp_header_t);		//updating data_sent value
			data_sent_on_loop = data_sent_on_loop + MICROTCP_MSS - sizeof(microtcp_header_t);
		}
		free(buffer2send);
		/*Semi-filled chunk check*/
		if( bytes_to_send%(MICROTCP_MSS-sizeof(microtcp_header_t)) ){
			
			chunks++;																		//we will this info for the acks, we'll wait
			semiPackSize=bytes_to_send%(MICROTCP_MSS-sizeof(microtcp_header_t));			//datalen for this packet
			//MALLOC FOR THE BUFFER
			buffer2send=malloc(semiPackSize+sizeof(microtcp_header_t));
			assert(buffer2send);
			
			//init header
			seq_number++;
			ackListInsert(seq_number+1);				//insert seq+1 for ack checking
			header2send.seq_number=htonl(seq_number);
			header2send.ack_number=0;
			header2send.control=0;
			header2send.window=0;
			header2send.data_len=htonl(semiPackSize);
			header2send.future_use0=0;
			header2send.future_use1=0;
			header2send.future_use2=0;
			header2send.checksum=0;
			
			//init data to be sent
			memcpy(buffer2send, &header2send, sizeof(microtcp_header_t));
			memcpy(buffer2send +sizeof(microtcp_header_t), buffer +data_sent, semiPackSize);
      
			//create checksum
			tmpCheckSum0=crc32(buffer2send,sizeof(buffer2send));
			header2send.checksum=htonl(tmpCheckSum0);
			
			//insert checksum inside buffer2send
			memcpy(buffer2send, &header2send, sizeof(microtcp_header_t));
			memcpy(buffer2send +sizeof(microtcp_header_t), buffer +data_sent,semiPackSize);
			
			//Now this chunk is ready to be sent
			if (sendto(socket->sd,buffer2send, semiPackSize+sizeof(microtcp_header_t),0,(struct sockaddr *)&socket->address,socket->address_len) <0){
				socket->state=INVALID;
				perror("microTCP Send  - while trying to send for the first time");
				free(buffer2send);
				exit(EXIT_FAILURE);
			}
			data_sent = data_sent + semiPackSize;
			data_sent_on_loop = data_sent_on_loop + semiPackSize;
			free(buffer2send);
		}
		
		/*Now waiting for acks*/
		for(i=0;i<chunks;i++){
			if(setsockopt(socket->sd , SOL_SOCKET ,SO_RCVTIMEO , &tv, sizeof(struct timeval)) < 0) {
				perror("Set Timeout\n");
				socket->state = INVALID;
				return 0;
			}
			tmp_recvfrom=recvfrom(socket->sd,recv_head_pack,sizeof(microtcp_header_t),0,(struct sockaddr *)&socket->address,&socket->address_len);
			if(tmp_recvfrom == -1){
				printf("\nmicroTCP send timeout - ack recv timeout");
				retransmit=1;	//timeout
				//empty queue
				ackListFree();
				break;
			}
			//I got an ack packet.
			//first lets check checksum
			tmpCheckSum0=ntohl(recv_head_pack->checksum);
			check_head_pack.seq_number = recv_head_pack->seq_number;
			check_head_pack.ack_number = recv_head_pack->ack_number;
			check_head_pack.control = recv_head_pack->control;
			check_head_pack.window = recv_head_pack->window;
			check_head_pack.data_len = recv_head_pack->data_len;
			check_head_pack.future_use0 = 0;
			check_head_pack.future_use1 = 0;
			check_head_pack.future_use2 = 0;
			check_head_pack.checksum = 0;
			
			
			memcpy(buffer4rcved,&check_head_pack,sizeof(check_head_pack));
			tmpCheckSum1=crc32(buffer4rcved,sizeof(buffer4rcved));
			if(tmpCheckSum0!=tmpCheckSum1){	//error on checksum
				perror("microTCP send warning - Packet recved w checksum error - We can ignore it");
				i--;
				continue;
			}
			
			//We check if its an ACK
			if(ntohs(check_head_pack.control)!=ACK){	//we will have to ignore it
				perror("microTCP send warning - Packet recved but its not an ACK - We can ignore it");
				i--;				//give another chance for an ack
			}
			else{	//Surely we can not ignore it.
				//Time to refresh current window size
				socket->curr_win_size = ntohs(check_head_pack.window);	
				
				//We check the ack number now
				//First if it is the same as before it is an DUP ACK and we have to update dup counter
				if(ack_number_rcvd==ntohl(check_head_pack.ack_number)){	//dub_ack
					dupCount++;
					if(dupCount == 3){	//3 DUP ACK is not good
						retransmit = 2;		//dub ack
						dupCount=0;
						break;
					}
					i--;			//give another chance for an ack
				}
				else{
					ack_number_rcvd=ntohl(check_head_pack.ack_number);
					if(ack_number_rcvd==head->ack){
						verified_data_sent=verified_data_sent+ntohl(check_head_pack.data_len);
						if(socket->cwnd <= socket->ssthresh) //slow start state and we have a verif ack
							socket->cwnd = socket->cwnd + MICROTCP_MSS;
						else								//congestion avoidance and we have a verif ack
							socket->cwnd = socket->cwnd + 1;
						ackListRemove();
						dupCount=0;
						retransmit=0;
					}
				}
			}
			
		}
		//We have to check if we got all acks. Check the list and if it is empty we are ok.
		//else we have to do another kind of retransmit
		//if all acks are ok we have to cwnd etc...
			
		if(retransmit==1){	//timeout 
			/*
			 * We had a timeout while waiting for acks. There are two situations. First we got no ack, second we got some acks.
			 * Either way, we have to retransmit some packets or all.
			 */
			socket->ssthresh = socket->cwnd/2;
			socket->cwnd = my_min(MICROTCP_MSS, socket->ssthresh,MICROTCP_RECVBUF_LEN);//the 3rd one is junk. when timeout cwnd=min(MICRO_MSS,ssthress), but min need 3 args
			
			//Now we have to update some values so we can start send again the packets we have to
			seq_number = ntohl(header2send.seq_number) - chunks + i + 1;
			data_sent= data_sent - data_sent_on_loop + verified_data_sent;
			remaining-=verified_data_sent;
			if(socket->cwnd == 0){//to never stop
				socket->cwnd = 1;
			}
		}
		else if(retransmit==2){	//3dupacks
			socket->ssthresh = socket->cwnd/2;
			socket->cwnd = socket->cwnd/2 + 1;
			
			//Now we have to update some values so we can start send again the packets we have to
			seq_number = ntohl(header2send.seq_number) - chunks + i + 1;
			data_sent= data_sent - data_sent_on_loop + verified_data_sent;
			remaining-=verified_data_sent;
		}
		else if(retransmit==0){						//everything is fine or not at all
			
				if(data_sent_on_loop == verified_data_sent){
				  remaining = remaining - data_sent_on_loop;	//only here the remaining decreases its value
				  seq_number = ntohl(check_head_pack.ack_number)-1;
				}
				else{
				  seq_number = ntohl(header2send.seq_number) - chunks;	//seq_number palia		
				  data_sent = data_sent - data_sent_on_loop;
				}
			
		}else{
			
		}
		socket->seq_number=seq_number;
		ackListFree();
	}
	free(buffer4rcved);
	return data_sent;
}

ssize_t microtcp_recv(microtcp_sock_t *socket, void *buffer, size_t length, int flags)
{
	microtcp_header_t *recv_head_pack=(microtcp_header_t *)malloc(sizeof(microtcp_header_t));
	microtcp_header_t send_head_pack;
	microtcp_header_t check_head_pack;
	uint32_t tmpCheckSum0;
	uint32_t tmpCheckSum1;
	
	//buffers for send n recv
	void *buffer2send=malloc(sizeof(send_head_pack));
	void *buffer4recv=malloc(MICROTCP_MSS);
	void *buffer4check=malloc(MICROTCP_MSS);
	void *dataFromClient;
	void *recvBuff=malloc(MICROTCP_RECVBUF_LEN);
	int indexRecvBuff=0;
	int indexBuffer=0;
	
	//some vers to help
	size_t seq_number2send=socket->ack_number;
	size_t seq_number4recv=socket->seq_number;
	int tmp_recvfrom;		//tmp for recvfrom
	
	//var for info to be returned
	ssize_t total_data_recvd=0;
	
	//assert buffer 4 received data
	assert(buffer4recv);
	
	//Timeout timer for socket
	struct timeval tv;
	tv.tv_sec = 0;
	tv.tv_usec = MICROTCP_ACK_TIMEOUT_US;
	
	//ssize_t WHOLE_SIZE;
	
	int i=0;
	while(1){
		i++;
		if(setsockopt(socket->sd , SOL_SOCKET ,SO_RCVTIMEO , &tv, sizeof(struct timeval)) < 0) {
				perror("Set Timeout\n");
				socket->state = INVALID;
				return 0;
			}
		tmp_recvfrom=recvfrom(socket->sd,buffer4recv,MICROTCP_MSS,0,(struct sockaddr *)&socket->address,&socket->address_len);
		if(tmp_recvfrom == -1){
			//error at waiting for a connection
			perror("microTCP recv connection timeout or fail\n");
			//we update the window and send an ack
			send_head_pack.window=htons(MICROTCP_RECVBUF_LEN-indexRecvBuff);
			send_head_pack.control=htons(ACK);
			send_head_pack.seq_number=htonl(seq_number2send++);
						
			send_head_pack.seq_number=0;
			send_head_pack.data_len=0;
			send_head_pack.future_use0=0;
			send_head_pack.future_use1=0;
			send_head_pack.future_use2=0;
			send_head_pack.checksum=0;
			//create checksum
			memcpy(buffer2send,&send_head_pack,sizeof(send_head_pack));
			tmpCheckSum0=crc32(buffer2send,sizeof(buffer2send));
			send_head_pack.checksum=htonl(tmpCheckSum0);
			//time to send
			if (sendto(socket->sd,(void *)&send_head_pack,sizeof(microtcp_header_t),0,(struct sockaddr *)&socket->address,socket->address_len) <0){
				socket->state=INVALID;
				perror("microTCP rcv connection error - While trying to send ACK\n");
			}
		}
		else{	//some thing recvd
			
			//first lets check checksum and then perform the rest checksum
			recv_head_pack=(microtcp_header_t *)buffer4recv;		//header
			dataFromClient=buffer4recv+sizeof(microtcp_header_t);	//data
			
			tmpCheckSum0=ntohl(recv_head_pack->checksum);		//we keep checksum
			check_head_pack.seq_number = recv_head_pack->seq_number;
			check_head_pack.ack_number = recv_head_pack->ack_number;
			check_head_pack.control = recv_head_pack->control;
			check_head_pack.window = recv_head_pack->window;
			check_head_pack.data_len = recv_head_pack->data_len;
			check_head_pack.future_use0 = 0;
			check_head_pack.future_use1 = 0;
			check_head_pack.future_use2 = 0;
			check_head_pack.checksum = 0;
			
			//we check if it is a FIN ACK packet
			if(ntohs(recv_head_pack->control)==FIN_ACK){
				socket->seq_number=ntohl(recv_head_pack->seq_number);
				socket->state=CLOSING_BY_PEER;
				//check recvBUff if empty or not
				if(indexRecvBuff!=0)
					memcpy(buffer+indexBuffer,recvBuff,indexRecvBuff);
				return total_data_recvd;
			}
			
			//rebuild this buffer-packet
			memcpy(buffer4check, &check_head_pack, sizeof(microtcp_header_t));
			memcpy(buffer4check +sizeof(microtcp_header_t), dataFromClient, ntohl(recv_head_pack->data_len));
      
			tmpCheckSum1=crc32(buffer4check,sizeof(buffer4check));
			if(tmpCheckSum0!=tmpCheckSum1){	//error on checksum
				perror("microTCP recv warning - Packet recved w checksum error - We can ignore it\n");
			}else{
				//we check the seq_number
				if(ntohl(recv_head_pack->seq_number)==seq_number4recv){
					recv_head_pack->data_len = ntohl(recv_head_pack->data_len);
					//Store data to buffer
					memcpy(recvBuff + indexRecvBuff, dataFromClient, recv_head_pack->data_len);
					
					total_data_recvd = total_data_recvd + recv_head_pack->data_len;		//updating total data received
					indexRecvBuff=indexRecvBuff + recv_head_pack->data_len;				//updating my recvbuffers index
					
					seq_number4recv++;	//next recv we wait for the next one!
					
				}
				
					//send ack! This ack will have updated ack number or the previous one
					//time to send an ack packet
					send_head_pack.seq_number=htonl(seq_number2send++);
					
					send_head_pack.ack_number=htonl(seq_number4recv);
					send_head_pack.control=htons(ACK);
					send_head_pack.window=htons(MICROTCP_RECVBUF_LEN-indexRecvBuff);
					send_head_pack.data_len=check_head_pack.data_len;					//it is still on network edian and we will need it in send for verif
					send_head_pack.future_use0=0;
					send_head_pack.future_use1=0;
					send_head_pack.future_use2=0;
					send_head_pack.checksum=0;
					//Lets create checksum
					
					memcpy(buffer2send,&send_head_pack,sizeof(send_head_pack));
					tmpCheckSum0=crc32(buffer2send,sizeof(buffer2send));
					send_head_pack.checksum=htonl(tmpCheckSum0);
					//time to send the ACK Packet
					if (sendto(socket->sd,(void *)&send_head_pack,sizeof(microtcp_header_t),0,(struct sockaddr *)&socket->address,socket->address_len) <0){
						socket->state=INVALID;
						perror("microTCP rcv connection error - While trying to send ACK\n");
					}
			}
			if(MICROTCP_RECVBUF_LEN - indexRecvBuff  <= 1500){	//checkin my buffer free space. congestion controll 1222 is the 15% of the vuffer
				memcpy(buffer + indexBuffer, recvBuff, indexRecvBuff);
				indexBuffer = indexBuffer + indexRecvBuff;
				indexRecvBuff = 0;
				break;
			}
			
		}
	}
	socket->seq_number=seq_number4recv;
	//check recvBUff if empty or not
	if(indexRecvBuff!=0)
		memcpy(buffer+indexBuffer,recvBuff,indexRecvBuff);
	
	return total_data_recvd;
}

//my functions
microtcp_sock_t saveAddr(microtcp_sock_t socket, struct sockaddr_in sin){
	socket.address=sin;
	socket.address_len=sizeof(sin);
	return socket;
}


ssize_t my_min(size_t a, size_t b, size_t c){
	return min(a,min(b,c));
}

void ackListInsert(size_t x){
	struct ackList *new=(struct ackList *)malloc(sizeof(struct ackList));
	struct ackList *tmp=head;
	new->ack=x;
	new->next=NULL;
	if(head==NULL){
		head=new;
	}else{
		while(tmp->next!=NULL)
			tmp=tmp->next;
		tmp->next=new;
	}
}

int ackListRemove(){
	struct ackList *tmp;
	if(head != NULL){
		tmp = head->next;
		free(head);
		head = tmp;
		return 1;
  }
  return 0;
}

void printList(){
	struct ackList *tmp=head;
	while(tmp!=NULL){
		printf("* %zu ", tmp->ack);  // prints as unsigned decimal
		tmp=tmp->next;
	}
	printf("\n");
}

void ackListFree(){
	while(ackListRemove()!=0){
		//do nada. just wait for it
	}
}