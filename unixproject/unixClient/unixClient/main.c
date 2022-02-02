#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>

#define BUF_SIZE 1024

pthread_t pthread[2], p_thread_file;

void* sendMsg(void*);
void* recvMsg(void*);
void recvFile(int);

int mkdir_check(const char*, mode_t);

int main(int argc, const char * argv[]) {
    
    // 소켓 세팅 하고 서버에 연결
    int sock;
    
    struct sockaddr_in serv_addr;
    
    sock = socket(PF_INET, SOCK_STREAM, 0);
    if(sock == -1){
        printf("socket error");
        exit(1);
    }
    
    printf("set server addr...\n");
    memset(&serv_addr,0,sizeof(serv_addr));
    serv_addr.sin_family=AF_INET;
    serv_addr.sin_addr.s_addr=inet_addr("127.0.0.1");
    serv_addr.sin_port=htons(9000);
    
    if(connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) == -1) {
        printf("connect error");
        exit(1);
    }
    
    // 송신, 수신 쓰레드 생성
    pthread_create(&pthread[0], NULL, sendMsg, (void*)&sock);
    pthread_create(&pthread[1], NULL, recvMsg, (void*)&sock);
    pthread_join(pthread[0], NULL);
    pthread_join(pthread[1], NULL);
    
    printf("클라이언트 종료");
    close(sock);
    
    return 0;
}

// 메시지 송신 함수
void* sendMsg(void* serv_sock){
    
    int sock = *(int*)serv_sock;
    char msg[BUF_SIZE];
    char nickname[20];
    
    // 채팅에서 사용할 닉네임 입력 후 서버에 전달
    printf("채팅에서 사용할 닉네임을 입력하세요 : ");
    fgets(nickname, sizeof(nickname), stdin);
    write(sock, nickname, sizeof(nickname));
    
    // 메시지 입력받은 후 서버에 전달
    while(1){
        fgets(msg, BUF_SIZE, stdin);
        write(sock, msg, sizeof(msg));
    }
    
}

// 메시지 수신 함수
void* recvMsg(void* serv_sock){
    
    int sock = *(int*)serv_sock;
    char nickname[20];
    int msglen, socklen;
    char msg[BUF_SIZE + 20];
    
    // 서버에서 메시지 들어오면 어떤 메시지인지 구분함
    while(1){
        msglen = read(sock, msg, sizeof(msg));
        if (msglen == -1){
            fprintf(stdout, "메시지 에러입니다.\n");
        } else if (msglen == 0){
            fprintf(stdout, "서버가 종료되었습니다.\n");
            exit(1);
        } else if (strcmp(msg, "!fileinput") == 0) {
            fprintf(stdout, "서버로부터 파일 전송...\n");
            recvFile(sock);
            
        } else {
            fputs(msg, stdout);
        }
    }

}

// 파일 받는 함수
void recvFile(int sock) {
    size_t bufsize = 0;
    int nbyte = 256;
    char msg[BUF_SIZE];
    char buf[BUF_SIZE];
    
    // recvFile 실행의 의미로 OK 신호 보냄
    sprintf(msg, "OK");
    write(sock, msg, sizeof(msg));
    fprintf(stdout, "OK 전송완료...\n");
    
    // 파일 확장자 받기
    read(sock, msg, sizeof(msg));
    if(strcmp(msg, "ERROR") == 0) {
        fprintf(stdout, "에러 발생\n");
        return;
    }
    fprintf(stdout, "받은 확장자 : %s\n", msg);
    
    // 현재 시간으로 시작하는 파일 이름 생성
    struct tm *currenttime = NULL, prevtime;
    time_t currenttimep;
    char fpath[255], fileName[255];
    
    sprintf(fpath, "./file");
    mkdir_check(fpath, 0755);
    chdir (fpath);

    currenttimep = time(NULL);
    currenttime = localtime (&currenttimep);
    memcpy (&prevtime, currenttime, sizeof (struct tm));
    
    
    sprintf(fileName, "file%04d%02d%02d__%02d%02d.%s",
            currenttime->tm_year+1900,
            currenttime->tm_mon+1,
            currenttime->tm_mday,
            currenttime->tm_hour,
            currenttime->tm_min,
            msg);
    
    FILE *file = fopen(fileName, "wb");
    
    // 파일 받을 준비가 완료됐으면 READY 신호 보냄
    sprintf(msg, "READY");
    write(sock, msg, sizeof(msg));
    fprintf(stdout, "READY 전송완료...\n");
    
    // 파일 받기
    nbyte = BUF_SIZE;
    while(nbyte >= BUF_SIZE) {
        nbyte = recv(sock, buf, BUF_SIZE, 0);
        fwrite(buf, sizeof(char), nbyte, file);
    }
    
    fprintf(stdout, "파일 전송 완료...\n");
    
    chdir ("../");
    fclose(file);
    return;
}

// 폴더 만드는 함수
int mkdir_check (const char *pathname, mode_t mode)
{
  int value;
    
  value = mkdir(pathname, mode);
  if (value == 0) return 1;
  if (value == -1) {
    if (errno == EEXIST) return 0; else {
      printf("Error while creating directory");
      exit(0);
    }
  }
    return 1;
}

