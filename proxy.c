#include <stdio.h>
#include <assert.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

// Tiny 웹 서버 전용 프록시 서버.
#define TINY_PORT "8090"

void doit(int client_acceptfd);
void parse_uri(char *uri, char *hostname, char *port, char *path);
void get_requesthdrs(rio_t *rp, char *request_hdrs);
void modify_request_hdrs(char *request_header, char *host);

int main(int argc, char **argv)
{
  int proxyserver_fd, client_acceptfd;
  char client_hostname[MAXLINE], client_port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;

  /* Check command line args */
  if (argc != 2)
  {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  proxyserver_fd = Open_listenfd(argv[1]);
  while (1)
  {
    clientlen = sizeof(clientaddr);

    // Client와 Proxy 서버 연결
    client_acceptfd = Accept(proxyserver_fd, (SA *)&clientaddr, &clientlen);

    Getnameinfo((SA *)&clientaddr, clientlen, client_hostname, MAXLINE, client_port, MAXLINE, 0);
    doit(client_acceptfd);
    Close(client_acceptfd);
  }
}

void doit(int client_acceptfd)
{
  char buf[MAXLINE], request_start_line[MAXLINE], request_hdrs[MAXLINE];
  char hostname[MAXLINE], port[MAXLINE], path[MAXLINE]; // {host}:{host port}/{resource}
  char method[MAXLINE], uri[MAXLINE];

  int conn_tiny_fd;
  rio_t rio_with_client;
  rio_t rio_with_tiny;

  Rio_readinitb(&rio_with_client, client_acceptfd);

  /** #1. Request HTTP MSG Start Line 정보 읽기
   * 1) buf에 Start Line 저장
   * 2) buf에서 Method, URI 가져와 저장
   */
  Rio_readlineb(&rio_with_client, buf, MAXLINE);
  sscanf(buf, "%s %s", method, uri);

  /** #2. URI에서 hostname과 port번호, 리소스 경로 읽기
   * parse_uri()에 각 변수의 pointer 전달
   */
  parse_uri(uri, hostname, port, path);

  /** #3. Tiny 서버에 보낼 Start Line 재정비
   * => 비어있는 requset_start_line를 채우기
   *
   * 1) method는 Client의 요청값 유지
   * 2) URI는 Hostname 제거한 리소스 경로만 추가 (parse_uri() 실행결과)
   * 3) Request의 HTTP 버전 1.0으로 고정하기
   */
  sprintf(request_start_line, "%s %s HTTP/1.0", method, path);

  /** #4. Tiny 서버에 보낼 Headers 재정비
   * 1) Client가 요청한 Headers 읽고,
   *    optional한 headers만 request_hdrs에 저장하기
   * 2) Essential한 headers 값 고정으로 넣어주기
   */
  get_requesthdrs(&rio_with_client, request_hdrs);
  modify_request_hdrs(request_hdrs, hostname);

  /** #5. Tiny 서버에 보낼 HTTP MSG 통합하기
   * 1) start line 끝에 "\r\n" 추가
   * 2) start line 끝에 request_hdrs 추가
   * 3) buf에 합쳐진 HTTP MSG 저장
   */
  sprintf(buf, "%s", strcat(strcat(request_start_line, "\r\n"), request_hdrs));

  printf("======After Aggragate HTTP MSG==== \n%s\n", buf);

  /** #6. Tiny 서버에 연결
   * 1) Open_clientfd로 client socket만들고 connect 요청
   * 2) Tiny 서버로부터 데이터 읽어올 준비
   * 3) Tiny 서버로 Client의 HTTP MSG 송신
   */
  conn_tiny_fd = Open_clientfd(hostname, port);
  Rio_readinitb(&rio_with_tiny, conn_tiny_fd);
  Rio_writen(conn_tiny_fd, buf, strlen(buf));

  /** #7. Client 서버에 Tiny 정보 송신
   * 1) Tiny 서버로부터 돌아온 데이터를 한 줄 씩 읽어서
   * 2) Client 서버로 한줄씩 송신 해주기
   */
  while (Rio_readlineb(&rio_with_tiny, buf, MAXLINE) != -1)
  {
    Rio_writen(client_acceptfd, buf, strlen(buf));
  }
}

void get_requesthdrs(rio_t *rp, char *request_hdrs)
{
  char buf[MAXLINE];
  char cur_buf[MAXLINE];

  Rio_readlineb(rp, cur_buf, MAXLINE);

  while (strcmp(cur_buf, "\r\n"))
  {
    int hasOptionalHeader = !(strstr(cur_buf, "Host: ") ||
                              strstr(cur_buf, "User-Agent: ") ||
                              strstr(cur_buf, "Connection: ") ||
                              strstr(cur_buf, "Proxy-Connection: "));

    // optional header가 있는 경우만 buffer에 추가
    if (hasOptionalHeader)
      strcat(buf, cur_buf);

    Rio_readlineb(rp, cur_buf, MAXLINE);
  }

  strncpy(request_hdrs, buf, MAXLINE);
}

/**
 * before => http://localhost:8080/cgi-bin/adder?3&5
 * after  => host: localhost, port:tiny_port,  path: /cgi-bin/adder?3&5
 */
void parse_uri(char *uri, char *hostname, char *port, char *path)
{
  char *hostname_ptr = strstr(uri, "//") ? strstr(uri, "//") + 2 : uri;
  assert(hostname_ptr != NULL);
  char *port_ptr = strchr(hostname_ptr, ':');
  char *path_ptr = strchr(hostname_ptr, '/');

  // URI에서 hostname 가져와 저장, uri 없는 경우 localhost 박제
  if (port_ptr)
    strncpy(hostname, hostname_ptr, port_ptr - hostname_ptr);
  else if (path_ptr)
    strncpy(hostname, hostname_ptr, path_ptr - hostname_ptr);
  else
    strcpy(hostname, hostname_ptr);

  if (strlen(hostname) == 0)
    strcpy(hostname, "localhost");

  strcpy(port, TINY_PORT); // 기존 port(유무 관계없이) Tiny 서버 포트로 교체
  strcpy(path, path_ptr);  // URI에서 리소스 경로만 parse
}

void modify_request_hdrs(char *request_header, char *hostname)
{
  char *user_agent_hdr = "Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3";
  char *ESSENTIAL_HEADERS[4] = {"Host: ", "User-Agent: ", "Connection: ", "Proxy-Connection: "};
  char *ESSENTIAL_HEADERS_CONTENT[4] = {hostname, user_agent_hdr, "close", "close"};

  // headers의 종료 라인이 존재한다면 지워주기
  int lastCRLF = strcmp(request_header, "\r\n\r\n");
  if (lastCRLF == 0)
  {
    int header_length = strlen(request_header);
    request_header[header_length - 2] = '\0';
  }

  for (int i = 0; i < 4; i++)
  {
    // headers 맨 끝에 ESSENTIAL HEADER 추가하기
    sprintf(request_header, "%s%s%s\r\n", request_header, ESSENTIAL_HEADERS[i], ESSENTIAL_HEADERS_CONTENT[i]);
  }

  // CRLF 완성해주기
  strcat(request_header, "\r\n");
}