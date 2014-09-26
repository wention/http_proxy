#undef UNICODE

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <process.h>
#include <time.h>
#include <winsock2.h>
#include <ws2tcpip.h>

// Need to link with Ws2_32.lib
#pragma comment (lib, "Ws2_32.lib")
#pragma comment (lib, "Mswsock.lib")

#define DEFAULT_LOCAL_PORT 27015

#define BUF_SIZE 8192
#define MAX_HEADER_SIZE 8192
#define MAX_THREAD 20

#define _DebugBreak _asm INT 3;
#define log(fmt,...)  do { printf("%s %s ",__DATE__,__TIME__); printf(##fmt,##__VA_ARGS__); } while(0)

#define error(fmt,...) {log(##fmt,##__VA_ARGS__);return -1;}

#define assert( x,...) if(!(x)){printf(##__VA_ARGS__); iThreadCount--;return; }
#define assert1( x,...) if(!(x)){printf(##__VA_ARGS__); return -1; }
int srv_socket;
int local_port;
int iThreadCount=0;

typedef struct {
	int client_socket;
	struct sockaddr_in client_addr;
} threadata;

int Init()
{
	local_port = DEFAULT_LOCAL_PORT;

	return 0;
}

int create_srv_socket(int port)
{
	int srv_socket, optval;
	struct sockaddr_in srv_addr;

	if((srv_socket = socket(AF_INET, SOCK_STREAM, 0)) < 0)
		error("Cannot create srv_socket");

	if (setsockopt(srv_socket, SOL_SOCKET, SO_REUSEADDR, (char*)&optval, sizeof(optval)) < 0) {
		return -1;
	}
	memset(&srv_addr, 0, sizeof(srv_addr));
	srv_addr.sin_family = AF_INET;
	srv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	srv_addr.sin_port = htons(port);

	if((bind(srv_socket, (sockaddr *)&srv_addr, sizeof(srv_addr))) != 0)
		return -1;

	if (listen(srv_socket, 20) < 0) {
		return -1;
	}
	return srv_socket;
}

int readheader(int client_socket, char * header_buffer)
{
	int n,byteread =0,err;
	//char tmpbuf[MAX_HEADER_SIZE];
	memset(header_buffer, 0, MAX_HEADER_SIZE);

	do {
		n = recv(client_socket, header_buffer + byteread, MAX_HEADER_SIZE, 0);
		if(n < 0)
		{
			err = WSAGetLastError();
			log("Error: %d    //recv < 0\n");
			return -1;
		}
		byteread += n;
		if ( byteread >= MAX_HEADER_SIZE )
			printf("==================header_buffer overflow========================\n");
		if (strstr(header_buffer, "\r\n\r\n"))
			break;
	}while (n > 0);
	if (byteread <= 0)
		return -1;

	return byteread;
}
int get_remote_info(char * header, char *remote_host, int *remote_port, char *remote_path)
{
	//
	char * pPath = strstr(header,"\r\n");
	strncpy(remote_path, header,(int)(pPath  - header));
	remote_path[pPath - header] = '\0';

	char * _p = strstr(header,"CONNECT");  /* 在 CONNECT 方法中解析 隧道主机名称及端口号 */
	if(_p)
	{
		char * _p1 = strchr(_p,' ');

		char * _p2 = strchr(_p1 + 1,':');
		char * _p3 = strchr(_p1 + 1,' ');

		if(_p2)
		{
			char s_port[10];
			ZeroMemory(s_port,10);

			strncpy(remote_host,_p1+1,(int)(_p2  - _p1) - 1);
			strncpy(s_port,_p2+1,(int) (_p3 - _p2) -1);
			*remote_port = atoi(s_port);

		} else 
		{
			strncpy(remote_host,_p1+1,(int)(_p3  - _p1) -1);
			*remote_port = 80;
		}


		return 0;
	}


	char * p = strstr(header,"Host:");
	if(!p) 
	{
		return -1;
	}
	char * p1 = strchr(p,'\n');
	if(!p1) 
	{
		return -1; 
	}

	char * p2 = strchr(p + 5,':'); /* 5是指'Host:'的长度 */

	if(p2 && p2 < p1) 
	{

		int p_len = (int)(p1 - p2 -1);
		char *s_port = (char *)malloc(p_len);
		strncpy(s_port,p2+1,p_len);
		s_port[p_len] = '\0';
		*remote_port = atoi(s_port);
		free(s_port);

		int h_len = (int)(p2 - p -5 -1 );
		strncpy(remote_host,p + 5 + 1  ,h_len); //Host:
		//assert h_len < 128;
		remote_host[h_len] = '\0';
	} else 
	{   
		int h_len = (int)(p1 - p - 5 -1 -1); 
		strncpy(remote_host,p + 5 + 1,h_len);
		//assert h_len < 128;
		remote_host[h_len] = '\0';
		*remote_port = 80;
	}
	return 0;
}
int rewrite_header(char * header)
{
	char * p = strstr(header,"http://");
	char * p0 = strchr(p,'\0');
	char * p5 = strstr(header,"HTTP/"); /* "HTTP/" 是协议标识 如 "HTTP/1.1" */
	int len = strlen(header);
	if(p)
	{
		char * p1 = strchr(p + 7,'/');
		if(p1 && (p5 > p1)) 
		{
			//转换url到 path
			memcpy(p,p1,(int)(p0 -p1));
			int l = len - (p1 - p) ;
			header[l] = '\0';


		} else 
		{
			char * p2 = strchr(p,' ');  //GET http://3g.sina.com.cn HTTP/1.1

			// printf("%s\n",p2);
			memcpy(p + 1,p2,(int)(p0-p2));
			*p = '/';  //url 没有路径使用根
			int l  = len - (p2  - p ) + 1;
			header[l] = '\0';

		}
	}

	//keep-alive ==> close
	p = strstr( header, "Connection:");

	if(p)
	{
		char *p3 = strstr( p, "\r\n");
		p = strstr( p, ":");
		if (p3 - p >= 5) {
			memcpy( p+2, "close", 5);
			memmove( p+7, p3,p0-p3);
		}
	}



	return 0;
}
int create_remote_connect(char *remote_host, int remote_port)
{
	struct sockaddr_in remote_addr;
	struct hostent *remote;
	int sock;

	if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		return -1;
	}

	if ((remote = gethostbyname(remote_host)) == NULL) {
		return -1;
	}
	memset(&remote_addr, 0, sizeof(remote_addr));
	remote_addr.sin_family = AF_INET;
	memcpy(&remote_addr.sin_addr.s_addr, remote->h_addr, remote->h_length);
	remote_addr.sin_port = htons(remote_port);

	if (connect(sock, (struct sockaddr *) &remote_addr, sizeof(remote_addr)) < 0) {
		return -1;
	}

	return sock;
}
int send_data(int socket,char * buffer,int len)
{

	//if(io_flag == W_S_ENC)
	//{
	//    int i;
	//    for(i = 0; i < len ; i++)
	//    {
	//        char c = buffer[i] ;
	//        buffer[i] = c+ 1;
	//       
	//    }
	//}

	return send(socket,buffer,len,0);
}
int forward_header(int destination_sock, char * header)
{
	rewrite_header(header);

	int len = strlen(header);
	return send_data(destination_sock,header,len) ;
}
int receive_data(int socket, char * buffer, int len)
{
	int n = recv(socket, buffer, len, 0);
	//if(io_flag == R_C_DEC && n > 0)
	//{
	//    int i; 
	//    for(i = 0; i< n; i++ )
	//    {
	//        char c = buffer[i];
	//        buffer[i] = c -1;
	//        // printf("%d => %d\n",c,buffer[i]);
	//    }
	//}

	return n;
}
int forward_data(int source_sock, int destination_sock, char *log) {
	char buffer[BUF_SIZE]={0}, *p,*p1;
	int n,byte_read = 0,byte_send=0,Header_Length=-1,Content_Length=-1, logstrlen, b_loged = FALSE;

	logstrlen = strlen(log);

	struct timeval tv;
	tv.tv_sec = 2;
	tv.tv_usec = 0;

	socklen_t tvlen = sizeof(tv);
	int recvd;
	setsockopt(source_sock,SOL_SOCKET,SO_RCVTIMEO,(char *)&tv,tvlen);

	while ((n = recv(source_sock, buffer, BUF_SIZE, 0)) > 0)
	{
		byte_read += n;
		n = send(destination_sock, buffer, n, 0);
		if (n <= 0)
			return -1;
		byte_send += n;
		if (!b_loged)
			if ((p = strstr(buffer,"HTTP")) && (p = strstr(buffer," ")))
				if ((p1 = strstr(buffer,"\r\n")) && (p1 > p))
				{
					if ( (MAX_PATH - logstrlen) > (p1 - p))
					{
						strncpy(log+logstrlen, p, p1 - p );
						log[logstrlen + p1 - p] = '\0';
						b_loged = TRUE;
					} else {
						b_loged = -1;
					}

				}


				//if ( Content_Length == -1)
				//{
				//	if ((p = strstr(buffer,"\r\n\r\n")))
				//	{
				//		Header_Length = p + 4 - buffer;
				//
				//	}
				//	if((p = strstr(buffer,"Content-Length")))
				//		if((p = strchr(p,':')))
				//			if((p1 = strstr(p,"\r\n")))
				//			{
				//				int p_len = p1-p-1;
				//				char *s_port = (char *)malloc(p_len);
				//				strncpy(s_port,p+1,p_len);
				//				s_port[p_len] = '\0';
				//				Content_Length = atoi(s_port);
				//				//free(s_port);
				//			}
				//}
				//if (Content_Length != -1 && Header_Length != -1)
				//	if (byte_read >= Content_Length + Header_Length)
				//		break;

	}

	shutdown(source_sock, SD_BOTH);
	if (byte_read != byte_send )
		return -1;
	if (byte_read <0)
		_asm INT 3;
	return byte_send;
}
int handle_client(int client_socket, struct sockaddr_in client_addr)
{
	int remote_socket, remote_port, headlength , byte_send;
	char remote_host[628]={0},remote_path[1024]={0},DbgStr[1024];
	char * header_buffer =(char *) malloc(MAX_HEADER_SIZE);

	headlength = readheader(client_socket, header_buffer);
	if (headlength < 0)
		return -1;
	get_remote_info(header_buffer, remote_host, &remote_port, remote_path);

	wsprintf(DbgStr,"%s",remote_path);

	if((remote_socket = create_remote_connect(remote_host, remote_port)) < 0)
		return -1;

	if(forward_header(remote_socket, header_buffer) < 0)
		return -1;

	byte_send = forward_data(remote_socket, client_socket, remote_path);

	closesocket(remote_socket);

	free(header_buffer);

	wsprintf(DbgStr,"%s %d", remote_path, byte_send);
	printf("%s\n",DbgStr);
	return 0;
}

int count=0;
void my_work (void * data)
{
	threadata mydata = *(threadata*)data;

	handle_client(mydata.client_socket,mydata.client_addr);
	closesocket(mydata.client_socket);

	iThreadCount--;
}
void srv_loop() 
{


	for (;;) 
	{

		if (iThreadCount < 20 )
		{
			threadata * mydata =(threadata *) malloc(sizeof(threadata));
			int addrlen = sizeof (mydata->client_addr);
			mydata->client_socket = accept(srv_socket, (struct sockaddr*)&mydata->client_addr, &addrlen);
			if (mydata->client_socket < 0)
			{
				free(mydata);
				continue;
			}

			if ( (_beginthread(my_work, 0, mydata )) > 0)
				iThreadCount++;
		}
		else
		{
			Sleep(1000);
		}

	}
}

int start_srv()
{
	WSADATA wsaData;
	int iResult;
	iResult = WSAStartup(MAKEWORD(2,2), &wsaData);
	if ( iResult != 0)
	{
		log("Initialization failed: %d\n", iResult);
		return -1;
	}

	srv_socket = create_srv_socket( local_port);

	srv_loop();

	closesocket(srv_socket);
	WSACleanup();
	return 0;
}

int main(int argc, char *argv[])
{
	if(!Init())
		return start_srv();
}
