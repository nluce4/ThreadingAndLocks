#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "mythreads.h"

typedef struct Thread {
	ucontext_t *context;
	ucontext_t *home;
	int id;
	int state; 								// 0 = working, 2 = done
	void *result;
	struct Thread *next;
	struct Thread *prev;
} Thread;

//all threads
int threadCount = 0;
Thread *threads_head = NULL;
Thread *threads_current = NULL;
Thread *threads_tail = NULL;
void freeStack();

typedef struct Lock {
	int locked;										//0 is unlocked
	int cvs[CONDITIONS_PER_LOCK]; //0 unless signaled
} Lock;

Lock locks[NUM_LOCKS];

int interruptsAreDisabled;

//disables interrupts
static void interruptDisable() {
	assert(!interruptsAreDisabled);
	interruptsAreDisabled = 1;
}

//enables interrupts
static void interruptEnable() {
	assert(interruptsAreDisabled);
	interruptsAreDisabled = 0;
}


//simple wrapper to call functions
void do_func(void *(*thFuncPtr) (void *), void * args){
	void *result = thFuncPtr(args);
	threadExit(result);
}

/**initialize thread library
  *First call into the library any application should make
**/
void threadInit(){
	//block interrupts
	interruptDisable();

	ucontext_t m;

	//add main thread to thread list
  getcontext(&m);

	threads_head = malloc(sizeof(Thread));
	threads_head->context = &m;
	threads_head->home = NULL;
	threads_head->id = 0;
	threads_head->state = 0;
	threads_head->result = NULL;
	threads_head->next = NULL;
	threads_head->prev = NULL;

	threadCount++;

	threads_tail = threads_head;
	threads_current = threads_head;

	//initialize all locks to zero
	for(int i = 0; i < NUM_LOCKS; i++){
		locks[i].locked = 0;
		for(int j = 0; j < CONDITIONS_PER_LOCK; j++){
			locks[i].cvs[j] = 0;
		}
	}

	//allow interrupts again
	interruptEnable();	
}

/**create new thread
	*create stack for new thread
  *if successful, assign the thread an int
  *threads should be run immediately after creation before returning the int
**/
int threadCreate(thFuncPtr funcPtr, void *argPtr){
	interruptDisable();
	
	ucontext_t newcontext, here;
	
	getcontext(&newcontext);
	newcontext.uc_stack.ss_sp = malloc(STACK_SIZE);
	newcontext.uc_stack.ss_size = STACK_SIZE;
	newcontext.uc_stack.ss_flags = 0;

	getcontext(&here);

	//add to threads

	threads_current = malloc(sizeof(Thread));
	threads_tail->next = threads_current;

	threads_current->context = &newcontext;
	threads_current->home = &here;
	threads_current->id = threadCount;
	threads_current->state = 0;
	threads_current->result = NULL;
	threads_current->next = NULL;
	threads_current->prev = threads_tail;

	threads_tail=threads_current;

	threadCount++;

	makecontext(&newcontext, (void (*)(void))do_func, 2, funcPtr, argPtr);
	interruptEnable();
	
	swapcontext(threads_current->home, &newcontext);
	
	return threadCount - 1;
}

/**gives control to the next runnable thread
	*should save the current thread and select the next one
	*wont return until this thread is is selected to run again
**/
void threadYield(){
	interruptDisable();
	ucontext_t old;
	getcontext(&old);	

	//if there is only the main thread then return
	if(threads_head == threads_tail) return;

	//search for thread that isn't finished
	Thread *new;

	int choice = rand() % 2;
	
	if(choice){
		new = threads_head;
		while (new != NULL) {
			if(new->state == 0 && new != threads_current){
				break;
			} 
			else {
				new = new->next;
			}
		}
	}
	else {
		new = threads_tail;
		while (new != NULL) {
			if(new->state == 0 && new != threads_current){
				break;
			} 
			else {
				new = new->prev;
			}
		}	
	}

	threads_current->context = &old;
	threads_current = new;
	interruptEnable();
	
	swapcontext(&old, threads_current->context);
}

/**waits for the thread specified by thread_id exits
	*if the thread is already done or does not exist then return immediately
	*save the results of all exited threads
**/
void threadJoin(int thread_id, void **result){
	interruptDisable();
	//search for thread first
	Thread *tmp = threads_head;
	int found = 0;
	while (tmp != NULL) {
		if(tmp->id == thread_id){
			found = 1;
			break;
		} 
		else {
			tmp = tmp->next;
		}
	}
	
	if (found == 1) { //if found
		if (tmp->state != 2){
			ucontext_t here;
			getcontext(&here);

			while(tmp->state != 2) { //checks every swap if thread is done
				threads_current = tmp;
				interruptEnable();
				swapcontext(&here, threads_current->context);
				interruptDisable();
			}
		}
		if(result != NULL)
			*result = threads_current->result;
	}
	//gets here then either found it and saved it or didn't find it
	freeStack();
	interruptEnable();
}

/**causes the current thread to exit
	*result should be passed on to any threads waiting on this thread
**/
void threadExit(void *result){
	//check to see if main thread
	if(threads_current->prev != NULL) {
		interruptDisable();
		threads_current->result = result;
		threads_current->state = 2;
		interruptEnable();
		setcontext(threads_current->home);
	}
	else {
		perror("ERROR: Called thread exit on the main thread\n");
		exit(EXIT_FAILURE);
	}
}

/**waits on the specified lock
	*allows other threads to continue to execute while waiting
**/
void threadLock(int lockNum){
	interruptDisable();

	while(locks[lockNum].locked != 0) {
		interruptEnable();
		threadYield();
		interruptDisable();
	}
	locks[lockNum].locked = 1;
	interruptEnable();
}

/**unlocks the specified lock
	*unlocking an already unlocked lock should do nothing
**/
void threadUnlock(int lockNum){
	interruptDisable();
	if(locks[lockNum].locked != 0){
		locks[lockNum].locked = 0;
	}
	interruptEnable();
} 

/**the mutex must be locked before calling this function, ERROR and EXIT if not
	*unlocks the specified lock
	*waits till the specified lock and condition var is signaled by another thread
	*the mutex is locked again before returning
**/
void threadWait(int lockNum, int conditionNum){
	interruptDisable();
	//if not locked then quit
	if(locks[lockNum].locked == 0) {
		perror("ERROR: Called threadWait on unlocked mutex\n");
		exit(EXIT_FAILURE);
	}

	interruptEnable();
	threadUnlock(lockNum);
	interruptDisable();	

	//wait till signal
	while(locks[lockNum].cvs[conditionNum] == 0){
		interruptEnable();
		threadYield();
		interruptDisable();
	}
	locks[lockNum].cvs[conditionNum] = 0;

	interruptEnable();
	threadLock(lockNum);
}

/**allows a single thread that is waiting on the condition var to run
	*if no threads are waiting on the condition var, do nothing
**/
void threadSignal(int lockNum, int conditionNum){
	interruptDisable();
	locks[lockNum].cvs[conditionNum] = 1;

	interruptEnable();

	threadYield(); 
}

void freeStack(){
	//free(threads_current->context->uc_stack.ss_sp);
	//leave for results
		//free(threads_current);
}

