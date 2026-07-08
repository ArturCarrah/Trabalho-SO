#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <syslog.h>
#include <fcntl.h>



int became_daemon(void){
	pid_t son_pid;
	FILE *file;
	int fd;
	son_pid = fork();
	if(son_pid > 0){
		_exit(0);
	}else if(son_pid < 0){
		syslog(LOG_USER | LOG_ERR, "Can't make a son!\n");
		exit(-3);
	}
	else{
		syslog(LOG_USER | LOG_INFO, "Son made\n");

		pid_t new_pid;

		new_pid = setsid();

		if(new_pid < 0 ){
			syslog(LOG_USER | LOG_ERR, "Son can't become independent\n");
			exit(-2);
		}else{
			syslog(LOG_USER | LOG_INFO, "Son became independet\n");
			pid_t grandson_pid;
			grandson_pid = fork();

			if(grandson_pid > 0){
				_exit(0);
			}else if(grandson_pid < 0){
				syslog(LOG_USER | LOG_ERR, "Can't make a grandson!\n");
                exit(-1);
			}else{
				syslog(LOG_USER | LOG_INFO, "Grandson made\n");
				close(STDIN_FILENO);
				fd = open("/dev/null", O_RDWR);
			}
		}
	}


	if(fd != STDIN_FILENO) return -1;
	if(dup2(STDIN_FILENO, STDOUT_FILENO) != STDOUT_FILENO) return -2;
	if(dup2(STDIN_FILENO, STDERR_FILENO) != STDERR_FILENO) return -3;



	return 0;
}
