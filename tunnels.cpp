//Compile with mpic++, run with mpirun -np <process num.> ./a.out

#include <mpi.h>
#include <stdio.h>
#include <pthread.h>
#include <queue>
#include <vector>
#include <string>
#include <unistd.h> 
#include <algorithm> 

//Message tags
#define REL_TAG 30
#define TTAKE_TAG 60
#define TACK_TAG 70

#define DEBUG false

//Task costants
#define X 10 //Squad size
#define P 30 //Tunnel size
#define T 2 //Tunnels amount

using namespace std;

struct lamp_request{
	int tid;
	int tsi;
	int dir;
};

//Global variables
int tid; //Process id
int tun_id = -1; //ID of requested/posessed tunnel, if there is none set to -1
int tsi = 0; //Lamport clock value
int cur_dir = 1; //Current direction (1 - to paradise, 2 - to real world)
int dir[T]; //Array with current directions of all tunnels, 0 - free, 1 - to paradise, 2 - to real world
vector<lamp_request> tuns[T]; //Array of vectors of tunnels waiting for/being in tunnels 

//Bools, mutexes and conditions for every time we need to wait for a response from all/several other processes
//Used to avoid active waiting

bool received_all_tun_ack = false;
pthread_mutex_t cond_lock_tun_ack = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond_tun_ack = PTHREAD_COND_INITIALIZER;

bool enough_space = false;
pthread_mutex_t cond_lock_space = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond_space = PTHREAD_COND_INITIALIZER;

bool at_top = false;
pthread_mutex_t cond_lock_top = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond_top = PTHREAD_COND_INITIALIZER;

pthread_t comm_thread; //The communication thread handle
int n; //Rich people amount (if set to 0, communism.cpp takes over)

bool all_resp_good = true; //If this remains true after all ack's we can get into the chosen tunnel queue
bool awaiting_reps = false; //Flag indicating if we send back a req when someone requests a tunnel

//int tuns_tried = 0; //Every time we cannot secure a tunnel we increase this so that when it reaches the number of tunnels
//we have to wait for a rel to check again

//Struct with all necessary data
struct packet_t {
    int tsi;       
    int tid;
	int tun_id;      
    int dir;
	bool resp;     
};

//Lamport clock function that increases the timer after a call,
//or compares the timer to a value from receiving a timestamped message
//if the sender_time argument is provided
void lamport_clock(int sender_time = -1){
	if(sender_time==-1){
		tsi++;
	}else{
		tsi=max(tsi,sender_time)+1;
	}
}

void send(packet_t *pkt, int destination, int tag, bool clock = false){
	if(destination!=tid){
		if(clock){
			lamport_clock();
			pkt->tsi=tsi;
		}
		MPI_Send(pkt, sizeof(packet_t), MPI_BYTE, destination, tag, MPI_COMM_WORLD);
	}
}

bool tuns_contains(int tun, int item){
	for(int i = 0; i < tuns[tun].size();i++){
		if(tuns[tun][i].tid==item){
			return true;
		}
	}
	return false;
}


void tuns_ordered_push(lamp_request req, int tun = -1){
	int t = (tun==-1)?tun_id:tun;
	if(tuns[t].size()==0){
		tuns[t].push_back(req);
		return;
	}
	for(int i = 0; i < tuns[t].size(); i++){
		if(req.tsi==tuns[t][i].tsi){
			if(req.tid < tuns[t][i].tid){
				tuns[t].insert(tuns[t].begin() + i,req);
				return;
			}
		}
		if(req.tsi < tuns[t][i].tsi){
			tuns[t].insert(tuns[t].begin() + i,req);
			return;
		}
	}
	//printf("%d, %d IS HERE\n",tsi,tid);
	tuns[t].push_back(req);
}

void tuns_remove(int rem_tid, int tun = -1){
	int t = (tun==-1)?tun_id:tun;
	if(t==-1)return;
	for(int i = 0; i < tuns[t].size(); i++){
		if(tuns[t][i].tid==rem_tid){
			tuns[t].erase(tuns[t].begin() + i);
			return;
		}
	}
}

void update_dir(int tun){
	if(tuns[tun].size()==0){
		dir[tun]=0;
		return;
	}
	if(tuns[tun].size()==1){
		dir[tun]=tuns[tun][0].dir;
		return;
	}
	bool same_dir = true;
	int fdir = tuns[tun][0].dir;
	for(int i = 0; i < tuns[tun].size()-1; i++){
		if(tuns[tun][i].dir!=tuns[tun][i+1].dir)same_dir = false;
	}
	if(same_dir)dir[tun] = fdir;
}


bool choose_tunnel(){
	lamport_clock();

	if(DEBUG)printf("%d, %d entered choose_tunnel\n",tsi,tid);

	//TODO: this is broken
	/*if(tuns_tried>=T){
		printf("%d, %d tried all tunnels and is waiting for some time\n",tsi,tid);

		int num = rand() % 5 + 1;
		sleep(num);

		tuns_tried=0;

	}*/

	//1.1 Get all other processes info about tunnels
	packet_t msg;
	msg.tid=tid;
	
	lamport_clock();

	//1.3 Finding the tunnel with the shortest queue
	int shortest_queue_length = 999;
	int best_tunnel = -1; //In case for some reason we don't find one we default to the first one
	int random_start = rand() % n;
	for(int i = random_start; i < T; (i++)%T){
		if(tuns[i].size()==0){
			best_tunnel = i;
			break;
		}
		//printf("%d, %d DIR %d, MYDIR %d, LEN %d, FQ %d\n",tsi,tid,dir[i],cur_dir,(int)tuns[i].size(), tuns[i][0].tid);
		if(tuns[i].size()<shortest_queue_length && (dir[i]==cur_dir || dir[i]==0)){
			shortest_queue_length = tuns[i].size();
			best_tunnel = i;
		}
	}

	tun_id = best_tunnel;
	if(tun_id==-1)tun_id= rand() % T; //we couldn't find the best one so we queue ourselves to a random one

	/*if(tun_id==-1){
		printf("%d, %d didn't find suitable tunnel in first search step\n",tsi,tid);
		tuns_tried++;
		return false;
	}*/

	if(DEBUG)("%d, %d chose best tunnel - %d\n",tsi,tid,tun_id);

	lamport_clock();

	lamp_request req;
	req.tid = tid;
	req.tsi = tsi; //This is the time we're going with when sending requests
	req.dir = cur_dir;
	tuns_ordered_push(req);
	update_dir(tun_id);

	if(DEBUG)printf("%d, %d added itself to chosen tunnel's queue\n",tsi,tid);

	//1.4 - Sending TUN_TAKE to everyone else
	msg.tun_id = tun_id;
	msg.dir = cur_dir;
	msg.tsi = req.tsi;
	for(int i = 0; i < n; i++){
		send(&msg,i,TTAKE_TAG);
	}

	if(DEBUG)printf("%d, %d sent TTAKE to everyone\n",tsi,tid);

	//1.6 - If nobody is contesting from the other side we're good to go
	pthread_mutex_lock(&cond_lock_tun_ack);
	while (!received_all_tun_ack){
		pthread_cond_wait(&cond_tun_ack,&cond_lock_tun_ack);
	}
	pthread_mutex_unlock(&cond_lock_tun_ack);
	//lamport_clock();

	if(all_resp_good==false){
		
		msg.tun_id = tun_id;
		//We're not queing for this one after all so we're removing ourselves
		//from the other processes tuns
		for(int i = 0; i < n; i++){
			//msg.tsi = tsi;
			send(&msg,i,REL_TAG,true);
		}
		tuns_remove(tid,tun_id);
		update_dir(tun_id);
		//After we sent it, we reset our local chosen tunnel and return false
		if(DEBUG)printf("%d, %d's tunnel - %d - was contested, going back to finding \n",tsi,tid,tun_id);
		tun_id = -1;
		//tuns_tried++;
		return false;
	}else{
		if(DEBUG)printf("%d, %d secured tunnel - %d\n",tsi,tid,tun_id);
		return true;
	}

	lamport_clock();
	return 0;
}

int num_above(){
	//pthread_mutex_lock(&lamport_lock);
	if(tun_id==-1)return -1;
	for(int i = 0; i < tuns[tun_id].size(); i++){
		if(tuns[tun_id][i].tid==tid){
			//pthread_mutex_unlock(&lamport_lock);
			return i;
		}
	}
	return -1;
	
}

void go_through(){

	packet_t msg;
	msg.tid = tid;

	lamport_clock();
	
	//if the tunnel has enough space we go through, otherwise we wait for REL
	int num = num_above();
	//printf("%d, %d, %d, %d **************************\n",tsi, tid, num*X, P-X);
	/*printf("\n%d, %d's lamport when wanting to\n",tsi,tid);
	for(int i = 0; i < tuns[tun_id].size(); i++){
		printf("(%d)%d(%d) ",tid,tuns[tun_id][i].tid, tuns[tun_id][i].tsi);
	}
	printf("\n");*/
	if(num != -1 && ((num * X) <= (P - X))==false){
		pthread_mutex_lock(&cond_lock_space);
		while (!enough_space){
			pthread_cond_wait(&cond_space,&cond_lock_space);
		}
		pthread_mutex_unlock(&cond_lock_space);
	}
	if(DEBUG)printf("%d, %d now has enough space to enter tunnel %d\n",tsi,tid,tun_id);

	string dir = (cur_dir==1)?"paradise":"the real world";
	printf("\n---\n%d, %d entered tunnel %d to %s.\n---\n",tsi, tid,tun_id,&(dir[0]));
	//printf("%d, tunnel %d is now at %d/%d capacity and directed to %s\n",tsi,tun_id,(int)(tuns[tun_id].size()+1)*X,P,&(dir[0]));
	
	lamport_clock();
	int rand_num = rand() % 5 + 1;
	sleep(rand_num); //simulating time taking to go through tunnel
	//lamport_clock();

	if(DEBUG)printf("%d, %d is now at the end of tunnel %d and waiting to exit\n",tsi,tid,tun_id); 
	/*printf("\n%d, %d's lamport when waiting\n",tsi,tid);
	for(int i = 0; i < tuns[tun_id].size(); i++){
		printf("(%d)%d(%d) ",tid,tuns[tun_id][i].tid, tuns[tun_id][i].tsi);
	}
	printf("\n");*/

	//Waiting for being at the top to leave the tunnel
	num = num_above();
	if(num!=0){
		pthread_mutex_lock(&cond_lock_top);
		while (!at_top){
			pthread_cond_wait(&cond_top,&cond_lock_top);
		}
		pthread_mutex_unlock(&cond_lock_top);
	}
	lamport_clock();

	tuns_remove(tid);
	update_dir(tun_id);

	msg.tun_id = tun_id;
	/*for(int i = 0; i < tuns[tun_id].size();i++){
		//msg.tsi = tsi;
		send(&msg,tuns[tun_id][i].tid,REL_TAG,true);
	}*/
	for(int i = 0; i < n;i++){
		//msg.tsi = tsi;
		send(&msg,i,REL_TAG,true);
	}

	printf("\n---\n%d, %d left tunnel %d and entered %s\n---\n",tsi,tid,tun_id,&(dir[0]));
	//lamport_clock();
}


void *recv_thread(void *ptr){
	printf("Communication thread of %d started\n",tid);
	MPI_Status status;
	int trep_counter = 0;
	
	int tack_counter = 0;

	int rep_counter = 0;
	
	packet_t msg;
	packet_t resp;
	resp.tid = tid;
	
	while(true){
		
		MPI_Recv( &msg, sizeof(packet_t), MPI_BYTE, MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
		lamport_clock(msg.tsi);

		switch (status.MPI_TAG){
		case TTAKE_TAG: //REQ
			//printf("%d, %d GOT TTAKE FROM %d AAAAAAAAAAAAAAA\n",tsi,tid,msg.tid);
			if(msg.tun_id!=-1&&!tuns_contains(msg.tun_id,msg.tid)){

				lamp_request req;
				req.tsi = msg.tsi;
				req.tid = msg.tid;
				req.dir = msg.dir;
				
				tuns_ordered_push(req, msg.tun_id);
				update_dir(msg.tun_id); 
				
			}
			resp.resp = true; //true by default
			if(msg.tun_id==tun_id && msg.dir!=cur_dir){
				resp.resp=false;
			}
			resp.tsi = tsi;
			resp.tun_id = tun_id;
			resp.dir = cur_dir;
			send(&resp,msg.tid,TACK_TAG);
		break;
		case TACK_TAG: //REP
			tack_counter++;

			if(msg.resp==false)all_resp_good = false;
			pthread_mutex_lock(&cond_lock_tun_ack);
			if(tack_counter==n-1){
				tack_counter = 0;
				received_all_tun_ack = true;
				pthread_cond_signal(&cond_tun_ack);
			}
			pthread_mutex_unlock(&cond_lock_tun_ack);
		break;
		case REL_TAG:
			//printf("%d, %d entered REl\n",tsi,tid);
			tuns_remove(msg.tid,msg.tun_id);
			//printf("%d, %d removed incoming from tuns\n",tsi,tid);
			update_dir(msg.tun_id);
			//printf("%d, %d updated dirs\n",tsi,tid);

			pthread_mutex_lock(&cond_lock_top);
			if(num_above()==0){
				at_top = true;
				pthread_cond_signal(&cond_top);
			}
			pthread_mutex_unlock(&cond_lock_top);
			
			pthread_mutex_lock(&cond_lock_space);
			if(num_above() != -1 && (num_above() * X) <= (P - X)){
				enough_space = true;
				pthread_cond_signal(&cond_space);
			}
			pthread_mutex_unlock(&cond_lock_space);
		break;

		default:
		printf("%d, %d RECEIVED UNTAGGED MESSAGE, PANIC\n",tsi, tid);
			break;
		}
	}
	
	lamport_clock();
}

void cleanup(){
	tun_id = -1;
	enough_space = false;
	at_top = false;
	received_all_tun_ack = false;
	all_resp_good = true;
}

void main_loop(){
	srand (tid);
	while(true){

		if(DEBUG)printf("%d, %d entered another main loop iteration\n",tsi,tid);
		
		bool res = false;
		while (res==false)
		{
			cleanup();
			res = choose_tunnel();
			if(res==false){
				int num = rand() % 5 + 1;
				sleep(num);
			}
		}
		go_through();

		cur_dir = (cur_dir==1)?2:1;
		cleanup();

		int num = rand() % 5 + 1;
		sleep(num); //Simulating being on the "other side"
	}
}

//Check of MPI thread support, shamelessly copied from the Magazines example
void check_thread_support(int provided)
{
    //printf("THREAD SUPPORT: we want %d. What are we gonna get?\n", provided);
    switch (provided) {
        case MPI_THREAD_SINGLE: 
            printf("No thread support!\n");
	    fprintf(stderr, "No thread support - I'm leaving!\n");
	    MPI_Finalize();
	    exit(-1);
	    break;
        case MPI_THREAD_FUNNELED: 
            printf("Only threads that did mpi_init can call MPI library\n");
	    break;
        case MPI_THREAD_SERIALIZED: 
            /* Need mutexes around MPI calls */
            printf("Only one thread at a time can make MPI library calls\n");
	    break;
        case MPI_THREAD_MULTIPLE: printf("Full thread support\n"); /* Want this */
	    break;
        default: printf("Nobody knows\n");
    }
}

int main(int argc, char **argv)
{
	int provided;
    MPI_Init_thread(&argc, &argv,MPI_THREAD_MULTIPLE, &provided);
    check_thread_support(provided);

	MPI_Status status;

	printf("Checking!\n");
	MPI_Comm_size( MPI_COMM_WORLD, &n ); //how many processes
	MPI_Comm_rank( MPI_COMM_WORLD, &tid ); //my id
	//printf("My id is %d from %d\n",tid, n);

	for(int i = 0; i < T; i++){
		tuns[i].reserve(n);
	}
	//lamport_queue.reserve(n);
	for(int i = 0; i < T; i++){
		dir[i]=0;
	}

	pthread_create( &comm_thread, NULL, recv_thread , 0);

	//sleep(1);

	main_loop();

	MPI_Finalize();
}
