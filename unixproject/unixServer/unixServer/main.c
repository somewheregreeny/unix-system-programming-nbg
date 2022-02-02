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
#include <sys/types.h>
#include <sys/wait.h>


#define BUF_SIZE 1024
#define CLIENT_NUM 10

struct userInfo {
    char nickname[20];
    int sock;
};

struct sendFileStruct {
    int sock;
    char filepath[BUF_SIZE];
    char fileExtension[BUF_SIZE];
};

int clnt_socks_num = 0;
struct userInfo userinfo[CLIENT_NUM];
struct sendFileStruct sfstruct;
pthread_t p_thread[CLIENT_NUM], p_thread_log, p_thread_file, p_thread_command;
pthread_mutex_t mutex, logmutex;
FILE *fp;

void* clientHandle (void*);
void* writeLog(void*);
void* serverCommand(void*);
void* sendFile(void*);

void sendMsg (char*, int);
void sendRandom (char*);


void mkdir_check(const char*, mode_t);

int main(int argc, const char * argv[]) {
    
    // 소켓 세팅
    int serv_sock, clnt_sock;
    
    struct sockaddr_in serv_addr, clnt_addr;
    socklen_t clnt_addr_size;
    
    // 뮤텍스 세팅
    pthread_mutex_init(&mutex, NULL);
    pthread_mutex_init(&logmutex, NULL);
    
    // 서버 소켓, IP 설정
    serv_sock = socket(PF_INET, SOCK_STREAM,0);
    if(serv_sock == -1){
        printf("socket error");
        exit(1);
    }
    
    printf("set server addr...\n");
    
    memset(&serv_addr,0,sizeof(serv_addr));
    serv_addr.sin_family=AF_INET;
    serv_addr.sin_addr.s_addr=htonl(INADDR_ANY);
    serv_addr.sin_port=htons(9000);
    
    printf("binding...\n");
    if(bind(serv_sock,(struct sockaddr*) &serv_addr,sizeof(serv_addr)) == -1){
        printf("bind error");
        exit(1);
    }
    
    if(listen(serv_sock,5) == -1){
        printf("listen error");
        exit(1);
    }
    
    // 현재 시간으로 시작하는 log 파일 생성
    struct tm *currenttime = NULL, prevtime;
    time_t currenttimep;
    char fpath[255], logname[255];
    
    sprintf(fpath, "./log");
    mkdir_check(fpath, 0755);
    chdir (fpath);
    
    currenttimep = time(NULL);
    currenttime = localtime (&currenttimep);
    memcpy (&prevtime, currenttime, sizeof (struct tm));
    
    
    sprintf(logname, "log%04d%02d%02d__%02d%02d.txt",
            currenttime->tm_year+1900,
            currenttime->tm_mon+1,
            currenttime->tm_mday,
            currenttime->tm_hour,
            currenttime->tm_min);
    
    fp = fopen(logname, "w+");
    
    if (fp == NULL) {
        fprintf(stdout, "로그 파일 생성 에러\n");
    }
    
    // 서버종료, 파일전송을 입력 받는 쓰레드 생성
    pthread_create(&p_thread_command, NULL, serverCommand, NULL);
    
    while(1) {
        fprintf(stdout, "새 클라이언트 대기 시작...\n");
        
        clnt_addr_size = sizeof(clnt_addr);
        clnt_sock = accept(serv_sock, (struct sockaddr*)&clnt_addr, &clnt_addr_size);
        if(clnt_sock==-1) fprintf(stdout, "accept error");
        else fprintf(stdout, "새 클라이언트 accept 완료...\n");
        
        // 공유 영역 사용하므로 뮤텍스 락
        pthread_mutex_lock(&mutex);
        
        userinfo[clnt_socks_num].sock = clnt_sock;
        int temp_index = clnt_socks_num;
        
        // 클라이언트로부터 입력 받는 쓰레드 생성
        pthread_create(&p_thread[clnt_socks_num], NULL, clientHandle, (void*)&temp_index);
        
        fprintf(stdout, "새 클라이언트 등록 완료...\n");
        clnt_socks_num++;
        
        pthread_mutex_unlock(&mutex);
        
        fprintf(stdout, "클라이언트 준비 완료...\n");
    }
    close(serv_sock);
    fclose(fp);
    return 0;

}

// 클라이언트로부터 메시지 받는 함수
void* clientHandle (void* arg){
    
    int index = *(int*)arg;
    int messagelen;
    char msg[BUF_SIZE];
    char logMsg[BUF_SIZE + 20];
    char fullMsg[BUF_SIZE + 20];
    
    // 닉네임을 받아서 설정
    read(userinfo[index].sock, userinfo[index].nickname, 20);
    userinfo[index].nickname[strlen(userinfo[index].nickname) - 1] = '\0';
    
    sprintf(msg, "%d번 소켓 채팅방 입장, 설정한 닉네임 : %s\n", userinfo[index].sock, userinfo[index].nickname);
    fprintf(stdout, "%s", msg);
    sprintf(msg, "%s님이 채팅방에 입장하셨습니다.\n", userinfo[index].nickname);
    sendMsg(msg, BUF_SIZE);
    
    // 입장 로그 기록
    strcpy(logMsg, msg);
    pthread_create(&p_thread_log, NULL, writeLog, (void*)logMsg);
    
    // 클라이언트가 연결을 끊을 때까지 계속 메시지 받음
    while((messagelen = read(userinfo[index].sock, msg, sizeof(msg))) != 0){
        
        if (strcmp(msg, "OK") == 0 ) {
            
            pthread_create(&p_thread_file, NULL, sendFile, NULL);
            pthread_join(p_thread_file, NULL);
            continue;
        }
        
        sprintf(fullMsg, "%s: %s", userinfo[index].nickname, msg);
        fprintf(stdout, "%s", fullMsg);
        
        // 메시지 로그 기록
        strcpy(logMsg, fullMsg);
        pthread_create(&p_thread_log, NULL, writeLog, (void*)logMsg);
        
        if(strcmp(msg, "!random\n") == 0) {
            sendRandom(userinfo[index].nickname);
        }
        else {
            sendMsg(fullMsg, messagelen);
        }
    }
    
    fprintf(stdout, "%d번 접속종료\n", userinfo[index].sock);
    close(userinfo[index].sock);
    return 0;
}

// 로그를 쓰는 함수
void* writeLog(void* arg) {
    
    char *msg = (char*)arg;
    
    // 파일 포인터가 공유 영역이므로 뮤텍스 락
    pthread_mutex_lock(&logmutex);
    fputs(msg, fp);
    pthread_mutex_unlock(&logmutex);
    
    pthread_exit(0);
    
}

// 서버 커멘드를 입력 받는 함수
void* serverCommand(void* arg) {
    char msg[BUF_SIZE];
    while(1) {
        fgets(msg, BUF_SIZE, stdin);
        if (!strcmp(msg, "!q\n")) {
            exit(1);
        } else if (!strcmp(msg, "!file\n")) {
            
            fprintf(stdout, "전송할 소켓 번호를 입력하세요. \n");
            scanf("%d", &sfstruct.sock);
            
            fprintf(stdout, "파일의 경로를 입력하세요. \n");
            scanf("%s", sfstruct.filepath);
            
            fprintf(stdout, "파일의 확장자를 입력하세요. \n");
            scanf("%s", sfstruct.fileExtension);
            
            sprintf(msg, "!fileinput");
            write(sfstruct.sock, msg, sizeof(msg));
            fprintf(stdout, "fileinput 전송완료...\n");
        }
    }
}

// 들어온 메시지에 대한 응답을 전송하는 함수
void sendMsg (char* fullMsg, int messagelen) {
    
    pthread_mutex_lock(&mutex);
    
    for(int i = 0; i < clnt_socks_num ; i++) {
        write(userinfo[i].sock, fullMsg, messagelen);
    }
    
    pthread_mutex_unlock(&mutex);
}

// !random 커맨드에 대응해 당첨자를 전송하는 함수
void sendRandom (char *nickname) {
    srand(time(NULL));
    
    char msg [BUF_SIZE + 20];
    char logMsg[BUF_SIZE + 20];
    char servername[20] = "[SERVER]";

    pthread_mutex_lock(&mutex);
    
    int random = rand() % clnt_socks_num;
    sprintf(msg, "%s님이 당첨자를 선택합니다! %s님이 당첨!!! 축하드립니다.\n", nickname, userinfo[random].nickname);
    fprintf(stdout, "random: %d\n", random);
    fprintf(stdout, "%s", msg);
    
    // 랜덤 당첨자 기록
    strcpy(logMsg, msg);
    pthread_create(&p_thread_log, NULL, writeLog, (void*)logMsg);
    
    for(int i = 0; i < clnt_socks_num ; i++) {
        write(userinfo[i].sock, msg, sizeof(msg));
    }
    pthread_mutex_unlock(&mutex);
}

// 파일을 전송하는 함수
void* sendFile(void* arg) {
    FILE* file;
    size_t fsize, nsize = 0;
    char buf[BUF_SIZE];
    char msg[BUF_SIZE];
    
    // OK 신호 수신하면 파일 확장자 보내기
    fprintf(stdout, "OK 수신완료...\n");
    
    file = fopen(sfstruct.filepath, "rb");
    if (file == NULL) {
        fprintf(stdout, "파일 경로에 파일이 존재하지않습니다.\n");
        sprintf(msg, "ERROR");
        write(sfstruct.sock, msg, BUF_SIZE);
        return 0;
    }
    
    write(sfstruct.sock, sfstruct.fileExtension, sizeof(sfstruct.fileExtension));
    
    fseek(file, 0, SEEK_END);
    fsize = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    // 준비 완료 후 데이터 보내기전 READY 신호 받기
    read(sfstruct.sock, msg, sizeof(msg));
    if (strcmp(msg, "READY") == 0) {
        fprintf(stdout, "READY 수신완료...\n");
        while (nsize != fsize) {
            int fpsize = fread(buf, 1, BUF_SIZE, file);
            nsize += fpsize;
            send(sfstruct.sock, buf, fpsize, 0);
        }
    } else {
        fprintf(stdout, "%s", msg);
    }
    
    char msg2[BUF_SIZE];
    sprintf(msg2, "END");
    send(sfstruct.sock, msg2, BUF_SIZE, 0);
    
    fputs("파일 전송 완료\n", stdout);
    
    fclose(file);
    return 0;
}

// 폴더 만드는 함수
void mkdir_check (const char *pathname, mode_t mode)
{
  int value;
    
  value = mkdir(pathname, mode);
  if (value == 0) return;
  if (value == -1) {
    if (errno == EEXIST) return; else {
      printf("Error while creating directory");
      exit(0);
    }
  }
}
    
