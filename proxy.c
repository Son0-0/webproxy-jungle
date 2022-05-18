#include <stdio.h>
#include "csapp.h"
/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static const char *conn_hdr = "Connection: close\r\n";
static const char *prox_hdr = "Proxy-Connection: close\r\n";
static const char *host_hdr_format = "Host: %s\r\n";
static const char *requestlint_hdr_format = "GET %s HTTP/1.0\r\n";
static const char *endof_hdr = "\r\n";

static const char *connection_key = "Connection";
static const char *user_agent_key= "User-Agent";
static const char *proxy_connection_key = "Proxy-Connection";
static const char *host_key = "Host";

void doit(int connfd);
void parse_uri(char *uri,char *hostname,char *path,int *port);
void build_http_header(char *http_header,char *hostname,char *path,int port,rio_t *client_rio);
int connect_endServer(char *hostname,int port,char *http_header);

int main(int argc,char **argv)
{
    int listenfd,connfd;
    char hostname[MAXLINE],port[MAXLINE];
    socklen_t  clientlen;
    // * 요청 받을 클라이언트 주소 저장소
    struct sockaddr_storage clientaddr;

    if(argc != 2){
      fprintf(stderr,"usage :%s <port> \n",argv[0]);
      exit(1);
    }

    // * tiny 서버와 코드가 똑같다
    // * 명령어를 통해 입력받은 번호의 포트를 열고 socket() -> bind() -> listen() 까지 완료한다.
    listenfd = Open_listenfd(argv[1]);
    while(1){
      clientlen = sizeof(clientaddr);
      connfd = Accept(listenfd,(SA *)&clientaddr,&clientlen);

      // * 클라이언트의 IP 주소와 PORT 번호를 받아 출력한다.
      Getnameinfo((SA*)&clientaddr,clientlen,hostname,MAXLINE,port,MAXLINE,0);
      printf("Accepted connection from (%s %s).\n",hostname,port);

      // * doit() 함수를 통해 endServer로 클라이언트의 요청을 보내주는 작업을 수행한다.
      doit(connfd);

      Close(connfd);
    }
    return 0;
}

void doit(int connfd)
{
    char buf[MAXLINE],method[MAXLINE],uri[MAXLINE],version[MAXLINE];
    char endserver_http_header [MAXLINE];

    char hostname[MAXLINE],path[MAXLINE];
    
    // * 요청을 보낼 서버의 파일디스크립터 번호와 포트번호
    int end_serverfd, port;

    // * end 서버의 출력값을 받아와야 하기 때문에 server_rio도 함께 선언
    rio_t rio, server_rio;

    // * 클라이언트의 요청을 받아 method, uri, version에 값을 넣는다.
    Rio_readinitb(&rio,connfd);
    Rio_readlineb(&rio,buf,MAXLINE);
    sscanf(buf,"%s %s %s",method,uri,version);

    // * 프록시 서버에서 GET 메소드 외 요청은 받지 않는다.
    // * strcasecmp 함수는 대소문자 관계없이 문자열이 같을 때는 0을 반환한다.
    if(strcasecmp(method,"GET")){
      printf("Proxy does not implement the method");
      return;
    }
    
    // * 클라이언트로 받은 요청으로 hostname, path, port 번호를 파싱
    parse_uri(uri,hostname,path,&port);

    // * end 서버에 보낼 요청 헤더를 만드는 작업
    build_http_header(endserver_http_header,hostname,path,port,&rio);

    // * end 서버에 클라이언트 요청을 대신 보내주기 위해 연결해주는 작업
    end_serverfd = connect_endServer(hostname,port,endserver_http_header);
    if(end_serverfd < 0){
      printf("connection failed\n");
      return;
    }

    // * end 서버에서 응답을 받기 위해 클라이언트의 요청을 보내는 작업
    Rio_readinitb(&server_rio,end_serverfd);

    // * 프록시 서버가 end 서버에 보내는 Request Header 출력을 위한 코드
    // printf("endserver request: %s\n", endserver_http_header);
    
    Rio_writen(end_serverfd,endserver_http_header,strlen(endserver_http_header));

    // * 서버에서 응답을 받은 후 그대로 클라이언트에 보내는 작업
    size_t n;
    while((n=Rio_readlineb(&server_rio,buf,MAXLINE))!=0)
    {
        printf("proxy received %d bytes,then send\n",n);
        Rio_writen(connfd,buf,n);
    }
    Close(end_serverfd);
}

// * endServer에 보낼 request header를 만드는 작업
void build_http_header(char *http_header,char *hostname,char *path,int port,rio_t *client_rio)
{
    char buf[MAXLINE],request_hdr[MAXLINE],other_hdr[MAXLINE],host_hdr[MAXLINE];

    sprintf(request_hdr,requestlint_hdr_format,path);

    while(Rio_readlineb(client_rio,buf,MAXLINE)>0) {
      if(strcmp(buf,endof_hdr)==0) break; // * EOF

      if(!strncasecmp(buf,host_key,strlen(host_key))) { // * Host
        strcpy(host_hdr,buf);
        continue;
      }

      if(strncasecmp(buf,connection_key,strlen(connection_key)) &&strncasecmp(buf,proxy_connection_key,strlen(proxy_connection_key)) &&strncasecmp(buf,user_agent_key,strlen(user_agent_key))) {
        strcat(other_hdr,buf);
      }
    }
    
    if(strlen(host_hdr)==0) {
      sprintf(host_hdr,host_hdr_format,hostname);
    }
    
    sprintf(http_header,"%s%s%s%s%s%s%s",
            request_hdr,
            host_hdr,
            conn_hdr,
            prox_hdr,
            user_agent_hdr,
            other_hdr,
            endof_hdr);
}

// * 클라이언트가 접속을 요청한 실제 endServer로 request를 보내기 위해 연결하는 작업
inline int connect_endServer(char *hostname,int port,char *http_header){
  char portStr[100];
  sprintf(portStr,"%d",port);
  return Open_clientfd(hostname,portStr);
}

void parse_uri(char *uri,char *hostname,char *path,int *port)
{
  *port = 80;

  // * ex) http://127.0.0.1:5000/home.html
  // * "http://" 뒤에서부터 값을 처리
  char* pos = strstr(uri,"//");

  pos = pos != NULL ? pos + 2 : uri;
  // * 현재 pos = "127.0.0.1:5000/home.html"

  // * ex) pos2 == :5000
  char *pos2 = strstr(pos,":");
  if(pos2 != NULL) {
    *pos2 = '\0';
    // * pos = hostname = 127.0.0.1
    sscanf(pos,"%s",hostname);
    // * pos2 = : / pos 2 + 1 = port == 5000
    // * path = /home.html
    sscanf(pos2+1,"%d%s",port,path);
  } else {
    pos2 = strstr(pos,"/");
    if(pos2!=NULL) {
      *pos2 = '\0';
      sscanf(pos,"%s",hostname);
      *pos2 = '/';
      sscanf(pos2,"%s",path);
    } else {
      sscanf(pos,"%s",hostname);
    }
  }
  return;
}
