#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/sendfile.h>

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
void get_source_path(char *buf, char *path)
/*
 * 从http头（buf）中解析请求的文件或目录保存在path中
 * */
{
	char *method = (char *)malloc(16);
	int i = 0;
	while (i < strlen(buf) && buf[i] != ' '){
		method[i] = buf[i];
		i++;
	}
	printf("method: %s  ", method);
	i++;
	int j = 0;
	while (i < strlen(buf) && buf[i] != ' '){
		path[j] = buf[i];
		i++;
		j++;
	}
	free(method);
}

char dir[128];
void *run(void *args)
/*
 * 主函数，功能为响应请求并发送文件，传入参数为client_soc，需要强制类型转换（48行）。
 * */
{
	pthread_mutex_lock(&mutex);        // 线程互斥锁，可忽略
	char *buf = (char *)malloc(1024000);
	char *tmp_dir = (char *)malloc(128);
	char *path = (char *)malloc(64);
	int client_soc = *(int *)args;
	printf("dir:%s\n", dir);
	//memset(buf, '\0', 1024000);
	memset(path, '\0', 64);
	//memset(tmp_dir, '\0', 128);
	sprintf(tmp_dir, "%s", dir);
	if (recv(client_soc, buf, 1024, 0) < 0){
		perror("Receive error");
		goto end;
	}
	//printf("received:\n%s\n", buf);
	get_source_path(buf, path);
	printf("buf:%s path=%s\n", buf, path);  // 打印调试信息，请求分为两种，请求目录（文件夹）如（“/party”），发送该文件夹下的"index.html"文件，还有请求文件如（“/party/1.png”），直接发送该文件
	if (tmp_dir[strlen(tmp_dir) - 1] == '/'){
		tmp_dir[strlen(tmp_dir) - 1] = '\0';
	}
	strcat(tmp_dir, path);
	struct stat hah;                       // c语言文件相关，注意在windows下操作函数不同（百度）
	stat(tmp_dir, &hah);
	if (S_ISDIR(hah.st_mode))	{
		strcat(tmp_dir, tmp_dir[strlen(tmp_dir) - 1] == '/' ? "index.html" : "/index.html");
	}
	printf("source: %s\n", tmp_dir);
	//memset(buf, '\0', 1024);
	struct stat st;
	if (stat(tmp_dir, &st) == -1){
		//perror("Stat error");
		goto end;
	}
	//sprintf(buf, "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: %ld\r\n\r\n", st.st_size);
	sprintf(buf, "HTTP/1.1 200 OK\r\n");              // 响应头
	send(client_soc, buf, strlen(buf), 0);
	send(client_soc, "\r\n", 2, 0);
	int fd = open(tmp_dir, O_RDONLY);
	if (sendfile(client_soc, fd, NULL, st.st_size) == -1){      // sendfile函数向浏览器发送文件
		perror("Send file error");
		close(fd);
		goto end;
	}
	printf("already sent 200 OK...\n\n");
	close(fd);
end:
	free(buf);
	free(tmp_dir);
	free(path);
	//printf("1\n");
	//pthread_detach(pthread_self());
	//close(client_soc);
	pthread_mutex_unlock(&mutex);
	//printf("2\n");
	return NULL;
}

int main(int argc, char *argv[])
{
	int is_port = 0, is_help = 0;
	int port = 0;
	for (int i = 1; i < argc; i++){
		if (strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--port") == 0){
			is_port = 1;
			port = atoi(argv[i + 1]);
			i++;
		}
		else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0){
			is_help = 1;
			break;
		}
		else{
			strcpy(dir, argv[i]);
		}
	}
	if (is_port == 0 || is_help == 1){
		printf("使用方法: httpd [OPTION] DIR\n\t  -p, --port：指定端口号\n\t  -h, --help: 打印帮助信息\n");
		return EXIT_SUCCESS;
	}
	printf("port:%d, dir=%s\n\n", port, dir);
	int server_soc;
	if ((server_soc = socket(AF_INET, SOCK_STREAM, 0)) < 0){
		perror("Error");
		return EXIT_FAILURE;
	}
	struct sockaddr_in server;                 
	server.sin_family = AF_INET;
	server.sin_port = htons(port);                      // 绑定端口
	//server.sin_addr.s_addr = inet_addr(INADDR_ANY);
	server.sin_addr.s_addr = inet_addr("127.0.0.1");    // 绑定ip地址，可以是本机地址（127.0.0.1），也可以是局域网地址（192.168开头或者172开头），校园网下一般获取不到绝对地址
	int on = 1;
	if ((setsockopt(server_soc, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on))) < 0){       // 135~148行，对端口和ip对应的套接字初始化，135行的函数可以不用考虑
		perror("Setsockopt error");
		return EXIT_FAILURE;
	}
	if (bind(server_soc, (struct sockaddr *)&server, sizeof(server)) != 0){
		perror("Bind error");
		return EXIT_FAILURE;
	}
	printf("bind successfully...\n\n");
	if (listen(server_soc, 5) != 0){
		perror("Listen error");
		return EXIT_FAILURE;
	}
	printf("listen begin...\n");
	unsigned int tmp_len = sizeof(struct sockaddr_in);

	while (1){                                             // 主程序循环，程序接受Ctrl+c和Ctrl+z响应
		//printf("3\n");
		int client_soc;
		struct sockaddr_in client;
		printf("waiting for a new accept...\n\n");
		if ((client_soc = accept(server_soc, (struct sockaddr *)&client, &tmp_len)) < 0)
		{
			perror("Accept error");
		}
		printf("accept successfully...\n\n");
		pthread_t t;
		pthread_create(&t, NULL, run, (void *)&client_soc);  // 线程调用run()函数，带pthread的都可以不用考虑
		pthread_join(t, NULL);
		close(client_soc);
	}
	shutdown(server_soc, SHUT_RDWR);
	//close(server_soc);
	//close(server_soc);
	return EXIT_SUCCESS;
}