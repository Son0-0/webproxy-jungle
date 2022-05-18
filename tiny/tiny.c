/* $begin tinymain */
/*
 * tiny.c - A simple, iterative HTTP/1.0 Web server that uses the
 *     GET method to serve static and dynamic content.
 *
 * Updated 11/2019 droh
 *   - Fixed sprintf() aliasing issue in serve_static(), and clienterror().
 */
#include "csapp.h"

void doit(int fd);
void echo(int fd);
void read_requesthdrs(rio_t *rp);
int parse_uri(char *uri, char *filename, char *cgiargs);
void serve_static(int fd, char *filename, int filesize, char *method);
void get_filetype(char *filename, char *filetype);
void serve_dynamic(int fd, char *filename, char *cgiargs, char *method);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg,
                 char *longmsg);

// * 11.6 A Solution
// * 요청 라인과 요청 헤더를 echo하는 함수
void echo(int connfd) {
  size_t n;
  char buf[MAXLINE];
  rio_t rio;

  Rio_readinitb(&rio, connfd);
  while((n = Rio_readlineb(&rio, buf, MAXLINE)) != 0) {
    printf("%s", buf);
    Rio_writen(connfd, buf, n);
  }
}

void doit(int fd) {
  int is_static;
  struct stat sbuf;
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char filename[MAXLINE], cgiargs[MAXLINE];
  rio_t rio;

  // * 클라이언트가 요청한 내용을 출력한다.
  // * ex)  GET / HTTP/1.1
  // *      Host: localhost
  Rio_readinitb(&rio, fd);
  printf("Request headers:\n");
  Rio_readlineb(&rio, buf, MAXLINE);
  printf("%s", buf);
  sscanf(buf, "%s %s %s", method, uri, version);
  if (!(strcasecmp(method, "GET") == 0 || strcasecmp(method, "HEAD") == 0)) {
    clienterror(fd, method, "501", "Not implemented", "Tiny does not implement this method");
    return;
  }
  read_requesthdrs(&rio);

  // * 클라이언트가 요청한 컨텐츠가 어떤 컨텐츠인지 uri를 parse 하는 단계
  is_static = parse_uri(uri, filename, cgiargs);

  if (stat(filename, &sbuf) < 0) {
    clienterror(fd, filename, "404", "Not found", "Tiny couldn't find this file");
    return;
  }

  if (is_static) {
    if(!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)) {
      clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't read the file");
      return;
    }
    serve_static(fd, filename, sbuf.st_size, method);
  } else {
    if(!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode)) {
     clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't run the CGI program");
     return;
    }
    serve_dynamic(fd, filename, cgiargs, method);
  }

}

void read_requesthdrs(rio_t *rp) {
  char buf[MAXLINE];

  // * 줄바꿈 무시하기 위해 필요한 코드라고 생각 (확실 X)
  // Rio_readlineb(rp, buf, MAXLINE);

  // * buf가 \r\n일때 까지 버퍼에서 buf로 입력값을 읽어서 출력
  while(strcmp(buf, "\r\n")) {
    Rio_readlineb(rp, buf, MAXLINE);
    printf("%s", buf);
  }
  return;
}

int parse_uri(char *uri, char *filename, char *cgiargs) {
  char *ptr;

  // * 클라이언트가 요청한 uri가 정적 컨텐츠일때
  if(!strstr(uri, "cgi-bin")) {
    strcpy(cgiargs, "");
    strcpy(filename, ".");
    strcat(filename, uri);
    if(uri[strlen(uri)-1] == '/') 
      strcat(filename, "home.html");
    return 1;
  }
  // * 클라이언트가 요청한 uri가 동적 컨텐츠일때
  else {
    ptr = index(uri, '?');
    if(ptr) {
      strcpy(cgiargs, ptr+1);
      *ptr = '\0';
    }
    else
      strcpy(cgiargs, "");
    strcpy(filename, ".");
    strcat(filename, uri);
    return 0;
  }
}

void serve_static(int fd, char *filename, int filesize, char *method) {
  int srcfd;
  char *srcp, filetype[MAXLINE], buf[MAXBUF];

  // * 클라이언트에게 보낼 Response Header
  get_filetype(filename, filetype);
  sprintf(buf, "HTTP/1.0 200 OK\r\n");
  sprintf(buf, "%sServer: Tiny Web Server\r\n", buf);
  sprintf(buf, "%sConnection: close\r\n", buf);
  sprintf(buf, "%sContent-length: %d\r\n", buf, filesize);
  sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, filetype);
  Rio_writen(fd, buf, strlen(buf));
  printf("Response headers:\n");
  printf("%s", buf);

  if (strcasecmp(method, "HEAD") == 0)
    return;

  // * 클라이언트에게 응답 / 클라이언트가 요청한 파일을 Open
  srcfd = Open(filename, O_RDONLY, 0);
  // srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0);
  srcp = (char *)malloc(filesize);      // 11.9
  // * Open한 파일을 읽어들여 srcp에 저장
  Rio_readn(srcfd, srcp, filesize);     // 11.9
  Close(srcfd);
  // * 클라이언트에게 전달
  Rio_writen(fd, srcp, filesize);
  // Munmap(srcp, filesize);
  free(srcp);                           // 11.9
}

void get_filetype(char *filename, char *filetype) {
  // * MIME TYPE
  // ref: https://developer.mozilla.org/ko/docs/Web/HTTP/Basics_of_HTTP/MIME_types
  if(strstr(filename, ".html"))
    strcpy(filetype, "text/html");
  else if(strstr(filename, ".gif"))
    strcpy(filetype, "image/gif");
  else if(strstr(filename, ".png"))
    strcpy(filetype, "image/png");
  else if(strstr(filename, ".jpg"))
    strcpy(filetype, "image/jpeg");
  else if(strstr(filename, ".mpeg"))
    strcpy(filetype, "video/mpeg");
  else
    strcpy(filetype, "text/plain");
}

void serve_dynamic(int fd, char *filename, char *cgiargs, char *method) {
  char buf[MAXLINE], *emptylist[] = { NULL };

  /* Return first part of HTTP response */
  sprintf(buf, "HTTP/1.0 200 OK\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Server: Tiny Web Server\r\n");
  Rio_writen(fd, buf, strlen(buf));

  // * Fork()로 자식 프로세스를 하나 생성
  if(Fork() == 0) {
    setenv("QUERY_STRING", cgiargs, 1);
    setenv("REQUEST_METHOD", method, 1);
    // * STDOUT으로 출력될 값들을 파일 디스크립터 번호가 fd가 여는 파일에 출력이 되도록 만들어주는 함수
    // * 이때 fd는 클라이언트 소켓이다
    Dup2(fd, STDOUT_FILENO);
    // * Execve()로 프로세스를 복사하여 adder.c 실행파일 실행
    // * terminal에 ./adder 입력하는 것과 같은 효과
    Execve(filename, emptylist, environ);
  }
  // * adder를 다 실행한 후 자식 프로세스가 종료될 때 까지 wait
  Wait(NULL);
}

void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg) {
  char buf[MAXLINE], body[MAXBUF];

  /* Build the HTTP response body */
  sprintf(body, "<html><title>Tiny Error</title>");
  sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
  sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
  sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
  sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

  /* Print the HTTP response */
  sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  Rio_writen(fd, buf, strlen(buf));
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-type: text/html\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
  Rio_writen(fd, buf, strlen(buf));
  Rio_writen(fd, body, strlen(body));
}

int main(int argc, char **argv) {
  int listenfd, connfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;

  /* Check command line args */
  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }


  // * ./tiny <포트 번호>
  // * tiny 명령어를 통해 입력받은 번호의 포트를 열고 socket() -> bind() -> listen() 까지 완료한다.
  // * listen 상태에 있으면 외부의 요청이 들어올 때 수락할 수 있는 대기상태에 들어가게 된다.
  listenfd = Open_listenfd(argv[1]);

  // * 무한 루프를 돌며 클라이언트의 connect 요청에 응답한다.
  // * 해당 요청에 응답 했을 경우 Close로 연결을 종료한 후 다시 accept 함수를 통해 연결이 들어오기를 기다린다.
  while (1) {
    clientlen = sizeof(clientaddr);
    connfd = Accept(listenfd, (SA *)&clientaddr,
                    &clientlen);  // line:netp:tiny:accept
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE,
                0);
    printf("Accepted connection from (%s, %s)\n", hostname, port);
    doit(connfd);   // line:netp:tiny:doit
    // echo(connfd); // 11.6 A
    Close(connfd);  // line:netp:tiny:close
  }
}

