#include <stdio.h>
#include <map>
#include <vector>
#include <sys/types.h>
#include <dirent.h>
#include <syslog.h>
#include <stdlib.h>
#include <string>
#include <string.h>
#include <signal.h>
#include <sched.h>
#include <limits.h>

#define METRICA_DE_BOMB 20

using namespace std;

string get_comm(const char* pid){
    char comm[256] = {};
    char path[256] = {};
    
    snprintf(path, sizeof(path), "/proc/%s/comm", pid);

    FILE *file = fopen(path, "r");

    if(file == nullptr) return "-1"; //Arquivo proibido - sem permissão


    if(fgets(comm, sizeof(comm), file) == NULL){ //Não conseguiu receber o comm
        fclose(file);
        return "-1";
    } 
    comm[strcspn(comm, "\n")] = '\0';                     //Nome adquirido com sucesso

    fclose(file); //Arquivo fechado

    return string(comm);
}


int hunt(const char* target){
    DIR *proc = opendir("/proc");
    dirent *dir;


    vector<pid_t> bomb_pids;
    pid_t dad_pid = __INT_MAX__;
    int dad_index = -1;
    

    if(proc == nullptr){
        syslog(LOG_USER | LOG_ERR, "Can't open proc directiory");
        return -1;
    }

    while((dir = readdir(proc)) != nullptr){
        if(dir->d_name[0] < '0' || dir->d_name[0] > '9') continue; //Só pega os PID's

        
        string comm = get_comm(dir->d_name);
        if(comm == "-1") continue; //Falha ao conseguir o nome


        //Congela o processo antes que ele gere forks
        if(strcmp(target, comm.c_str()) == 0){
            pid_t pid = atoi(dir->d_name);

            kill(pid, SIGSTOP); //Congelando processos
            bomb_pids.push_back(pid);
            
            //Descobrindo quem é o pai verdadeiro, precaução
            if(pid < dad_pid){
                dad_pid = pid;
                dad_index = bomb_pids.size() -1;
            }
            
        }

    }

    closedir(proc);   //Diretório fechado

    //Mata os processos congelados
    int killed_count = 0;
    for (pid_t pid : bomb_pids) {
        if(strcmp("bash", target) == 0){
            if(pid == dad_pid) continue; //Se o bomb veio do bash, não queremo matar o original
        }
        if (kill(pid, SIGKILL) == 0) {
            killed_count++;
        }
    }

    //Se o bomb veio do bash, queremos fazel-lo voltar a funcionar
    if(strcmp("bash", target) == 0){
        kill(bomb_pids[dad_index], SIGCONT);
    }

    syslog(LOG_USER | LOG_INFO, "Neutralizado! %d processos do tipo '%s' foram destruídos.\n", killed_count, target);

    return 0;

}

int anti_bomb(){
    static bool first_time = true;
    static size_t num_total_process = 0; //Acabou não sendo usado
    size_t num_actual_total_process = 0; //...
    
    DIR *proc = opendir("/proc");
    dirent *dir;

    map<string, vector<pid_t>> process;
    
    bool bomb_spotted = false;
    string target_name;


    if(proc == nullptr){
        syslog(LOG_USER | LOG_ERR, "Can't open proc directiory");
        return -1;
    }


    
    while((dir = readdir(proc)) != nullptr){
        if(dir->d_name[0] < '0' || dir->d_name[0] > '9') continue; //Só pega os PID's

        num_actual_total_process++; //Contando os processos
 
        string comm = get_comm(dir->d_name);
        if(comm == "-1") continue; //Falha ao conseguir o nome


        process[string(comm)].push_back(atoi(dir->d_name));
        if(process[string(comm)].size() >= METRICA_DE_BOMB){
            bomb_spotted = true;
            target_name = string(comm);
        }

    }

    closedir(proc);


    num_total_process = num_actual_total_process;   //Ia ser utilizado uma métrica de aumento exponencial de processos
    num_actual_total_process = 0;                   //mas foi descartada pelo tempo

    //Bomba encontrada
    if(bomb_spotted){

        sched_param param;
        param.sched_priority = 99; //Aumentando a prioridade para impedir o bomb seja prioritário (mais do que já é)

        if (sched_setscheduler(0, SCHED_FIFO, &param) == -1) {
            syslog(LOG_USER | LOG_ERR, "Erro ao ativar SCHED_FIFO");
            return -1;
        }else{
            syslog(LOG_USER | LOG_INFO, "Começou a caça em alta prioridade");
        }
        
        hunt(target_name.c_str());

        param.sched_priority = 0; //Volta a prioridade ao normal
        sched_setscheduler(0, SCHED_OTHER, &param);
        syslog(LOG_USER | LOG_INFO, "Voltando à busca em prioridade padrao");
    }
    

    return 0;
}