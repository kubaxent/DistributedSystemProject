//Compile with mpic++, run with mpirun -np <thread num.> ./a.out

#include <mpi.h>
#include <stdio.h>
#include <pthread.h>
#include <queue>
#include <vector>
#include <string>
#include <unistd.h>
#include <algorithm> 

//Message tags
#define REQ_TAG 10
#define REP_TAG 20
#define REL_TAG 30
#define TREQ_TAG 40
#define TREP_TAG 50
#define TTAKE_TAG 60
#define TACK_TAG 70
#define CANCEL_TAG 80

//Task costants
#define X 10 //Squad size
#define P 30 //Tunnel size
#define T 3 //Tunnels amount

using namespace std;

//Global variables
int tid; //Process id
int tun_id = -1; //ID of requested/posessed tunnel, if there is none set to -1
int tsi = 0; //Lamport clock value
int cur_dir = 1; //Current direction (1 - to paradise, 2 - to real world)
vector<int> lamport_queue; //Lamport queue
int dir[T]; //Array with current directions of all tunnels, 0 - free, 1 - to paradise, 2 - to real world
vector<int> tuns[T]; //Array of vectors of tunnels waiting for/being in tunnels 

//Bools, mutexes and conditions for every time we need to wait for a response from all/several other processes
//Used to avoid active waiting
bool received_all_tun_rep = false;
pthread_mutex_t cond_lock_tun_rep = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond_tun_rep = PTHREAD_COND_INITIALIZER;

bool received_all_tun_ack = false;
pthread_mutex_t cond_lock_tun_ack = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond_tun_ack = PTHREAD_COND_INITIALIZER;

bool received_all_lamp = false;
pthread_mutex_t cond_lock_lamp = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond_lamp = PTHREAD_COND_INITIALIZER;

bool enough_space = false;
pthread_mutex_t cond_lock_space = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond_space = PTHREAD_COND_INITIALIZER;

bool at_top = false;
pthread_mutex_t cond_lock_top = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond_top = PTHREAD_COND_INITIALIZER;

bool got_rel = false;
pthread_mutex_t cond_lock_got_rel = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond_got_rel = PTHREAD_COND_INITIALIZER;

//pthread_mutex_t lamport_lock = PTHREAD_MUTEX_INITIALIZER;

pthread_t comm_thread; //The communication thread handle
int n; //Rich people amount (if set to 0, communism.cpp takes over)

bool all_resp_good = true; //If this remains true after all ack's we can get into the chosen tunnel queue
//bool awaiting_reps = false; //Flag indicating if we send back a req when someone requests a tunnel
//bool releasing = false;//Like above, if anyone else comes to our tuns we need to send them REL too

int tuns_tried = 0; //Every time we cannot secure a tunnel we increase this so that when it reaches the number of tunnels
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
		tsi=max(tsi+1,sender_time);
	}
}

void send(packet_t *pkt, int destination, int tag){
	if(destination!=tid){
		MPI_Send(pkt, sizeof(packet_t), MPI_BYTE, destination, tag, MPI_COMM_WORLD);
		lamport_clock();
	}
}

bool tuns_contains(int tun, int item){
	if(tuns[tun].size()==0)return false;
	if(find(tuns[tun].begin(), tuns[tun].end(), item) != tuns[tun].end()) {
    	return true;
	} else {
		return false;
	}
}

bool choose_tunnel(){

	printf("%d, %d entered choose_tunnel\n",tsi,tid);

	if(tuns_tried>=T){
		printf("%d, %d tried all tunnels and is waiting for any REL\n",tsi,tid);
		pthread_mutex_lock(&cond_lock_got_rel);
		while (!got_rel){
			pthread_cond_wait(&cond_got_rel,&cond_lock_got_rel);
		}
		pthread_mutex_unlock(&cond_lock_got_rel);
		tuns_tried=0;
	}

	//1.1 Get all other processes info about tunnels
	packet_t msg;
	msg.tid=tid;
	
	for(int i = 0; i < n; i++){
		msg.tsi=tsi;
		send(&msg,i,TREQ_TAG);
	}

	/*printf("%d, %d TUNS LOOK LIKE THIS AFTER TREQS\n",tsi, tid);
	for(int i = 0; i < T; i++){
		printf("TUN %d\n",i);
		for(int j = 0; j < tuns[i].size();j++){
			printf("%d ",tuns[i][j]);
		}
		printf("\n");
	}*/

	printf("%d, %d sent TREQ to everyone\n",tsi,tid);

	pthread_mutex_lock(&cond_lock_tun_rep);
	while (!received_all_tun_rep)
	{
		pthread_cond_wait(&cond_tun_rep,&cond_lock_tun_rep);
	}
	pthread_mutex_unlock(&cond_lock_tun_rep);

	printf("%d, %d got tun_rep from everyone\n",tsi,tid);

	//1.3 Finding the tunnel with the shortest queue
	int shortest_queue_length = 999;
	int best_tunnel = -1; //In case for some reason we don't find one we default to the first one
	for(int i = 0; i < T; i++){
		if(tuns[i].size()==0){
			best_tunnel = i;
			break;
		}
		if(tuns[i].size()<shortest_queue_length && dir[i]==cur_dir){
			shortest_queue_length = tuns[i].size();
			best_tunnel = i;
		}
	}

	tun_id = best_tunnel;

	if(tun_id==-1){
		printf("%d, %d didn't find suitable tunnel in first search step\n",tsi,tid);
		tuns_tried++;
		return false;
	}

	printf("%d, %d chose best tunnel - %d\n",tsi,tid,tun_id);

	//1.4 - Sending TUN_TAKE to everyone else
	msg.tun_id = tun_id;
	msg.dir = cur_dir;
	for(int i = 0; i < n; i++){
		msg.tsi = tsi;
		send(&msg,i,TTAKE_TAG);
	}

	//1.6 - If nobody is contesting from the other side we're good to go
	pthread_mutex_lock(&cond_lock_tun_ack);
	while (!received_all_tun_rep){
		pthread_cond_wait(&cond_tun_ack,&cond_lock_tun_ack);
	}
	pthread_mutex_unlock(&cond_lock_tun_ack);

	if(all_resp_good==false){
		
		msg.tun_id = tun_id;
		//We're not queing for this one after all so we're removing ourselves
		//from the other processes tuns
		for(int i = 0; i < n; i++){
			msg.tsi = tsi;
			send(&msg,i,CANCEL_TAG);
		}
		//After we sent it, we reset our local chosen tunnel and return false
		printf("%d, %d's tunnel - %d - was contested, going back to finding \n",tsi,tid,tun_id);
		tun_id = -1;
		tuns_tried++;
		return false;
	}else{
		printf("%d, %d secured tunnel - %d\n",tsi,tid,tun_id);
		return true;
	}

	lamport_clock();
	return 0;
}

int num_above(){
	//pthread_mutex_lock(&lamport_lock);
	for(int i = 0; i < lamport_queue.size(); i++){
		if(lamport_queue[i]==tid){
			//pthread_mutex_unlock(&lamport_lock);
			return i;
		}
	}
	//pthread_mutex_unlock(&lamport_lock);
	return -1;
	
}

void go_through(){
	//pthread_mutex_lock(&lamport_lock);
	lamport_queue.push_back(tid);
	//pthread_mutex_unlock(&lamport_lock);

	printf("%d, %d added itself to it's lamport queue\n",tsi,tid);

	packet_t msg;
	msg.tid = tid;

	int size = tuns[tun_id].size();
	for(int i = 0; i < size; i++){
		msg.tsi = tsi;
		printf("%d, %d sent REQ to %d for tunnel %d\n",tsi,tid,tuns[tun_id][i],tun_id);
		send(&msg,tuns[tun_id][i],REQ_TAG);
	}

	//awaiting_reps = true;
	printf("%d, %d sent REQ to everyone in queue for %d\n",tsi,tid,tun_id);

	if(size!=0){
		pthread_mutex_lock(&cond_lock_lamp);
		while (!received_all_lamp){
			pthread_cond_wait(&cond_lamp,&cond_lock_lamp);
		}
		pthread_mutex_unlock(&cond_lock_lamp);
		printf("%d, %d got REP from everyone waiting for tunnel %d\n",tsi,tid,tun_id);
	}else{
		printf("%d, %d didn't need REP for %d\n",tsi,tid,tun_id);
	}

	//awaiting_reps = false;

	//if the tunnel has enough space we go thorugh, otherwise we wait for REL
	int num = num_above();
	if(((num * X) <= (P - X))==false){
		pthread_mutex_lock(&cond_lock_space);
		while (!enough_space){
			pthread_cond_wait(&cond_space,&cond_lock_space);
		}
		pthread_mutex_unlock(&cond_lock_space);
	}	
	printf("%d, %d now has enough space to enter tunnel %d\n",tsi,tid,tun_id);

	string dir = (cur_dir==1)?"paradise":"the real world";
	printf("\n---\n%d, %d entered tunnel %d to %s.\n---\n",tsi, tid,tun_id,&(dir[0]));
	//printf("%d, tunnel %d is now at %d/%d capacity and directed to %s\n",tsi,tun_id,(int)(tuns[tun_id].size()+1)*X,P,&(dir[0]));
	lamport_clock();

	int rand_num = rand() % 5 + 1;
	sleep(rand_num); //simulating time taking to go through tunnel

	printf("%d, %d is now at the end of tunnel %d and waiting to exit !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n",tsi,tid,tun_id); 

	//Waiting for being at the top to leave the tunnel
	num = num_above();
	if(num!=0){
		pthread_mutex_lock(&cond_lock_top);
		while (!at_top){
			pthread_cond_wait(&cond_top,&cond_lock_top);
		}
		pthread_mutex_unlock(&cond_lock_top);
	}

	//printf("%d, %d is first at the exit of tunnel %d\n",tsi,tid,tun_id); 

	//pthread_mutex_lock(&lamport_lock);
	lamport_queue.erase(remove(lamport_queue.begin(), lamport_queue.end(), tid), lamport_queue.end());
	//pthread_mutex_unlock(&lamport_lock);

	msg.tun_id = tun_id;
	for(int i = 0; i < tuns[tun_id].size();i++){
		msg.tsi = tsi;
		send(&msg,tuns[tun_id][i],REL_TAG);
		printf("%d, %d sent REL to %d for tunnel %d @@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@\n",tsi,tid,tuns[tun_id][i],tun_id);
	}
	/*for(int i = 0; i < n;i++){
		msg.tsi = tsi;
		send(&msg,i,REL_TAG);
	}*/

	//releasing = true;

	printf("\n---\n%d, %d left tunnel %d and entered %s\n---\n",tsi,tid,tun_id,&(dir[0]));
	//printf("%d, tunnel %d is now at %d/%d capacity\n",tsi,tun_id,(int)(tuns[tun_id].size())*X,P);
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
		//printf("%d, msg's tun_id: %d\n",tsi,msg.tun_id);
		//printf("%d, %d received something\n",tsi, tid);

		switch (status.MPI_TAG){
		case TREQ_TAG:
			//printf("%d, %d received TREQ from %d\n",tsi, tid, msg.tid);
			resp.tsi = tsi;
			resp.tun_id = tun_id;
			resp.dir = cur_dir;
			send(&resp,msg.tid,TREP_TAG);
		break;
		case TREP_TAG:
			trep_counter++;
			//printf("%d, %d received %d/%d TREP\n",tsi,tid,trep_counter,n-1);
			//printf("GOT TREP WITH %d TUN_ID FROM %d\n",msg.tun_id,msg.tid);
			//printf("CONTAINS RETURNS %d\n",tuns_contains(msg.tun_id,msg.tid));

			if(msg.tun_id != -1 && tuns_contains(msg.tun_id,msg.tid)==false){ 
				printf("FUCKFUCKFUCKFUCKFUCKFUCKFUCKFUCKFUCKFUCKFUCKFUCKFUCK\n");
				tuns[msg.tun_id].push_back(msg.tid);
				dir[msg.tun_id] = msg.dir;
				/*if(awaiting_reps){
					resp.tsi = tsi;
					send(&resp,msg.tid,REQ_TAG);
				}*/
				//if(releasing){
				//	resp.tsi = tsi;
				//	resp.tun_id = tun_id;
				//	send(&resp,msg.tid,REL_TAG);
				//}
			}
			
			pthread_mutex_lock(&cond_lock_tun_rep);
			if(trep_counter==n-1){
				trep_counter = 0;
				received_all_tun_rep = true;
				pthread_cond_signal(&cond_tun_rep);
			}
			pthread_mutex_unlock(&cond_lock_tun_rep);
		break;
		case TTAKE_TAG:
			//printf("%d, %d received TTAKE from %d\n",tsi, tid, msg.tid);
			if(msg.tun_id!=-1&&!tuns_contains(msg.tun_id,msg.tid)){
				//printf("%d - about to push %d to %d in tuns\n", tsi, msg.tid,msg.tun_id);
				//printf("PUSHED IN TTAKE\n");
				tuns[msg.tun_id].push_back(msg.tid);
				dir[msg.tun_id] = msg.dir;
				/*if(awaiting_reps){
					resp.tsi = tsi;
					send(&resp,msg.tid,REQ_TAG);
				}*/
				//printf("%d - pushed %d to %d in tuns\n",tid,msg.tid,msg.tun_id);
			}
			if(msg.tun_id==tun_id){
				if(msg.dir==cur_dir){
					resp.resp=true;
				}else{
					resp.resp=false;
				}
			}
			resp.tsi = tsi;
			resp.tun_id = tun_id;
			resp.dir = cur_dir;
			send(&resp,msg.tid,TACK_TAG);
		break;
		case TACK_TAG:
			tack_counter++;
			//printf("%d, %d received %d/%d TACK\n",tsi,tid,tack_counter,n-1);
			if(msg.resp==false)all_resp_good = false;
			pthread_mutex_lock(&cond_lock_tun_ack);
			if(tack_counter==n-1){
				tack_counter = 0;
				received_all_tun_ack = true;
				pthread_cond_signal(&cond_tun_ack);
			}
			pthread_mutex_unlock(&cond_lock_tun_ack);
		break;
		case REQ_TAG:
			//printf("%d, %d received REQ from %d\n",tsi, tid, msg.tid);
			//pthread_mutex_lock(&lamport_lock);
			lamport_queue.push_back(msg.tid);
			//pthread_mutex_unlock(&lamport_lock);
			resp.tsi = tsi;
			send(&resp,msg.tid,REP_TAG);
		break;
		case REP_TAG:
			rep_counter++;
			//printf("%d, %d received %d/%d REP\n",tsi,tid,rep_counter,(int)tuns[tun_id].size()-1);
			pthread_mutex_lock(&cond_lock_lamp);

			if(rep_counter>=tuns[tun_id].size()-1){
				rep_counter = 0;
				received_all_lamp = true;
				pthread_cond_signal(&cond_lamp);
			}
			pthread_mutex_unlock(&cond_lock_lamp);
		break;
		case CANCEL_TAG:
			tuns[msg.tun_id].erase(remove(tuns[msg.tun_id].begin(), tuns[msg.tun_id].end(), msg.tid), tuns[msg.tun_id].end());
		break;
		case REL_TAG:
			//printf("%d, %d received REL from %d\n",tsi, tid, msg.tid);
			
			tuns[msg.tun_id].erase(remove(tuns[msg.tun_id].begin(), tuns[msg.tun_id].end(), msg.tid), tuns[msg.tun_id].end());
			
			//pthread_mutex_lock(&lamport_lock);
			lamport_queue.erase(remove(lamport_queue.begin(), lamport_queue.end(), msg.tid), lamport_queue.end());

			/*printf("%d, %d got REL and it's lq now looks like this: ",tsi,tid);
			for(int i = 0; i < lamport_queue.size(); i++){
				printf("%d ",lamport_queue[i]);
			}
			printf("\n");*/
			//pthread_mutex_unlock(&lamport_lock);

			pthread_mutex_lock(&cond_lock_got_rel);
				pthread_cond_signal(&cond_got_rel);
			pthread_mutex_unlock(&cond_lock_got_rel);

			pthread_mutex_lock(&cond_lock_top);
			printf("%d, %d got REL and num_above is %d\n",tsi,tid,num_above());
			if(num_above()==0){
				at_top = true;
				pthread_cond_signal(&cond_top);
			}
			pthread_mutex_unlock(&cond_lock_top);
			
			pthread_mutex_lock(&cond_lock_space);
			if((num_above() * X) <= (P - X)){
				enough_space = true;
				pthread_cond_signal(&cond_space);
			}
			pthread_mutex_unlock(&cond_lock_space);
		break;

		default:
		printf("%d, %d RECEIVED UNTAGGED MESSAGE, PANIC\n",tsi, tid);
			break;
		}
		//printf("I'm %d, got message from %d with timestamp %d\n",tid,msg.tid,msg.tsi);
	}
	
	lamport_clock();
}

void cleanup(){
	tun_id = -1;
	enough_space = false;
	at_top = false;
	received_all_lamp = false;
	received_all_tun_ack = false;
	received_all_tun_rep = false;
	all_resp_good = true;
}

void main_loop(){
	srand (tid);
	while(true){

		printf("%d, %d entered another main loop iteration\n",tsi,tid);
		
		bool res = false;
		while (res==false)
		{
			cleanup();
			res = choose_tunnel();
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
	printf("My id is %d from %d\n",tid, n);

	lamport_queue.reserve(n);

	pthread_create( &comm_thread, NULL, recv_thread , 0);
	main_loop();

	MPI_Finalize();
}
