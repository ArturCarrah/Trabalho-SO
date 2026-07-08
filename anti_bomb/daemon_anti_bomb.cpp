#include "anti_bomb.cpp"
#include "became_daemon.c"

int main(void){
	const char *LOGNAME = "antibomb";

	int daemon = -1;
	daemon = became_daemon();

	if (daemon != 0){
		syslog(LOG_USER | LOG_ERR, "Error in open the daemon!\n");
		closelog();
		return -1;
	}


	openlog(LOGNAME, LOG_PID, LOG_USER);
	syslog(LOG_USER | LOG_INFO, "starting\n");

	while(true){
		anti_bomb();
        usleep(10000);
	}

	return 0;
}