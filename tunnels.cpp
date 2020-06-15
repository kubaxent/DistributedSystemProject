//Compile with mpicc, run with mpirun -np <thread num.> ./a.out

#include <mpi.h>
#include <stdio.h>
#include <pthread.h>
#include <queue>
#include <vector> 

//Message tags
#define REQ_TAG 10
#define REP_TAG 20
#define REL_TAG 30
#define TREQ_TAG 40
#define TREL_TAG 50
#define TTAKE_TAG 60
#define TACK_TAG 70

//Task costants
#define X 10 //Squad size
#define P 50 //Tunnel size
#define T 5 //Tunnels amount

using namespace std;

//Global variables
int tid; //Process id
int tun_id = -1; //ID of requested/posessed tunnel, if there is none set to -1
int tsi = 0; //Lamport clock value
int cur_dir = 1; //Current direction (1 - to paradise, 2 - to real world)
queue<int> lamport_queue; //Lamport queue
int dir[T]; //Array with current directions of all tunnels, 0 - free, 1 - to paradise, 2 - to real world
vector<int> tuns[T]; //Array of vectors of tunnels waiting for/being in tunnels 
bool resp; //Flag telling if another process is requesting  the current tunnel but from the opposite side

int n; //Rich people amount (if set to 0, communism.cpp takes over)

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

int choose_tunnel(){

	lamport_clock();
	return 0;
}

void go_through(){


	lamport_clock();
}

void cleanup(){


	lamport_clock();
}

void send(){

	
	lamport_clock();
}


void *recv_thread(void *ptr){

	
	lamport_clock();
}

void main_loop(){
	
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
    MPI_Init_thread(&argc, &argv,MPI_THREAD_MULTIPLE, &provided); //NOTE: MPI_Init_thread and MPI_Init don't play well together
    check_thread_support(provided);

	/*MPI_Status status;
	MPI_Init(&argc, &argv); //Musi być w każdym programie na początku

	printf("Checking!\n");
	MPI_Comm_size( MPI_COMM_WORLD, &n ); //how many processes
	MPI_Comm_rank( MPI_COMM_WORLD, &tid ); //my id
	printf("My id is %d from %d\n",tid, n);*/



	/*int msg[2];
	if ( tid == ROOT)
	{
		MPI_Recv(msg, 2, MPI_INT, MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
		printf(" Otrzymalem %d %d od %d\n", msg[0], msg[1], status.MPI_SOURCE);
	}
	else
	{
		msg[0] = tid;
		msg[1] = size;
		MPI_Send( msg, 2, MPI_INT, ROOT, MSG_TAG, MPI_COMM_WORLD );
		printf(" Wyslalem %d %d do %d\n", msg[0], msg[1], ROOT );
	}*/

	MPI_Finalize(); // Musi być w każdym programie na końcu
}
