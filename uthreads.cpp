#include <queue>
#include <set>
#include <csetjmp>
#include "uthreads.h"
#include <iostream>
#include <cstdlib>
#include <setjmp.h>
#include <signal.h>
#include <list>
#include <map>
#include <sys/time.h>

#define READY 0
#define RUNNING 1
#define SLEEPING 2
#define BLOCKED 3
#define SECOND 1000000

#ifdef __x86_64__
/* code for 64 bit Intel arch */

typedef unsigned long address_t;
#define JB_SP 6
#define JB_PC 7

/* A translation is required when using an address of a variable.
   Use this as a black box in your code. */
address_t translate_address(address_t addr)
{
    address_t ret;
    asm volatile("xor    %%fs:0x30,%0\n"
		"rol    $0x11,%0\n"
                 : "=g" (ret)
                 : "0" (addr));
    return ret;
}

#else
/* code for 32 bit Intel arch */

typedef unsigned int address_t;
#define JB_SP 4
#define JB_PC 5 

/* A translation is required when using an address of a variable.
   Use this as a black box in your code. */
address_t translate_address(address_t addr)
{
    address_t ret;
    asm volatile("xor    %%gs:0x18,%0\n"
		"rol    $0x9,%0\n"
                 : "=g" (ret)
                 : "0" (addr));
    return ret;
}

#endif

/**
 * A struct represents a single thread containing its id, stack, state, 
 * sigjmp_buf env struct and quantum counter
 */
typedef struct Threads {
    int tid;
    char stack[STACK_SIZE];
    unsigned int state;
    sigjmp_buf env;
    unsigned int quantumCounter;
} Thread;

/* Value of 1 quantum */
int quantum = 0; 

/* ID of the current running thread */
int currThreadID = 0; 

/* Number of quantums passed from first initialization */
int numOfQuantum = 0; 

/* Array of all threads currently */
Thread* threads[MAX_THREAD_NUM] = {NULL}; 

/* List of empty ID places for threads */
std::set<int> emptyPlaces; 

/* Queue of ready threads */
std::list<int> readyThreads;

/* List of sleeping threads and how much quantums remained each one.*/
std::map<int, int> sleepingThreads;

/* sigaction struct for SIGVTALARM signal */
struct sigaction sa;

/* timer of quantums */
struct itimerval Gtimer;

/**
 * Called each time a quantum expired, or when the running thread was put to
 * sleep or blocked. Responsible for setting the next running thread. 
 * @param sig
 */
static void scheduler(int sig=1){
    numOfQuantum++;
    
    if(threads[currThreadID] != NULL && 
            sigsetjmp(threads[currThreadID]->env, sig)){
        return;
    }
    
    /* For each sleeping thread, decrease the number of remained quantums by 1.
     * If the amount of quantums reaches 0, remove from sleeping and add to
     * he ready list.
     */
    for (std::map<int,int>::iterator it = sleepingThreads.begin();
            it != sleepingThreads.end(); ++it) {
        sleepingThreads[it->first]--;
        if(sleepingThreads[it->first] == 0) {
            readyThreads.push_back(it->first);
            threads[it->first]->state = READY;
            sleepingThreads.erase(it);
        }
    }
    
    /* If the last thread was not blocked or put to sleep, add it to the end of
     * ready list.
     */
    if(threads[currThreadID] != NULL && 
            threads[currThreadID]->state == RUNNING) {
         threads[currThreadID]->state = READY;
         readyThreads.push_back(currThreadID);
    }
//    else {
//        Gtimer.it_value.tv_sec = quantum/SECOND;
//        Gtimer.it_value.tv_usec = quantum%SECOND;
//    }
    
    /* Take first thread from ready list and make it running */
    int newThread = readyThreads.front();
    readyThreads.pop_front();
    currThreadID = newThread;
    threads[newThread]->state = RUNNING;
    threads[newThread]->quantumCounter++;
    
    if (setitimer (ITIMER_VIRTUAL, &Gtimer, NULL)) {
	std::cerr <<  "system error: setitimer error." << std::endl;
        exit(1);
    }
    
    siglongjmp(threads[newThread]->env, sig);
    
    return;
}

/**
 * Find the first empty place in the array.
 */
static int find_first_place(){
    if(emptyPlaces.size() == 0){
        return -1;
    }
    std::set<int>::iterator it = emptyPlaces.begin();
    int ret = *it;
    emptyPlaces.erase(it);
    return ret;
    
} 

/**
 * Initialize the time with the given quantum.
 */
static void timer_init(){
    Gtimer.it_value.tv_sec = quantum/SECOND;
    Gtimer.it_value.tv_usec = quantum%SECOND;
    
    Gtimer.it_interval.tv_sec = quantum/SECOND;
    Gtimer.it_interval.tv_usec = quantum%SECOND;
    
    if (setitimer (ITIMER_VIRTUAL, &Gtimer, NULL)) {
	std::cerr <<  "system error: setitimer error." << std::endl;
        exit(1);
    }
}

/*
 * Description: This function initializes the thread library. 
 * You may assume that this function is called before any other thread library
 * function, and that it is called exactly once. The input to the function is
 * the length of a quantum in micro-seconds. It is an error to call this
 * function with non-positive quantum_usecs.
 * Return value: On success, return 0. On failure, return -1.
*/
int uthread_init(int quantum_usecs) {

    if (quantum_usecs <= 0) {
        std::cerr << "thread library error: quantum_usecs must be positive"
                " number" << std::endl;
        return -1;
    }
    quantum = quantum_usecs;
    
    sa.sa_handler = &scheduler;
    if (sigaction(SIGVTALRM, &sa, NULL) < 0) {
	std::cerr << "system error: sigaction error." << std::endl;
        exit(1);
    }
    
    // insert the free indexes
    for(int i=0; i<MAX_THREAD_NUM; i++){
        emptyPlaces.insert(i);
    }
    
    uthread_spawn(NULL);
    
    // initialize timer    
    timer_init();
    
    scheduler();
    
    return 0;
}

/*
 * Description: This function creates a new thread, whose entry point is the
 * function f with the signature void f(void). The thread is added to the end
 * of the READY threads list. The uthread_spawn function should fail if it
 * would cause the number of concurrent threads to exceed the limit
 * (MAX_THREAD_NUM). Each thread should be allocated with a stack of size
 * STACK_SIZE bytes.
 * Return value: On success, return the ID of the created thread.
 * On failure, return -1.
*/
int uthread_spawn(void (*f)(void)) {
    try {
        Thread* thread = new Thread();
        thread->tid = find_first_place();
        if(thread->tid == -1){
            std::cerr << "thread library error: number of threads exceeds the "
                    "limit" << std::endl;
            delete thread;
            return -1;
        }
        address_t sp, pc;

        sp = (address_t)thread->stack + STACK_SIZE - sizeof(address_t);
        pc = (address_t)f;
        sigsetjmp(thread->env, 1);
        (thread->env->__jmpbuf)[JB_SP] = translate_address(sp);
        (thread->env->__jmpbuf)[JB_PC] = translate_address(pc);
        sigemptyset(&(thread->env->__saved_mask));

        threads[thread->tid] = thread;
        readyThreads.push_back(thread->tid);
        thread->state = READY;
        return thread->tid;
    }
    catch (std::bad_alloc& e) {
        std::cerr << "system error: memory allocation error." << std::endl;
        exit(1);
    }
    
}



/*
 * Description: This function terminates the thread with ID tid and deletes
 * it from all relevant control structures. All the resources allocated by
 * the library for this thread should be released. If no thread with ID tid
 * exists it is considered as an error. Terminating the main thread
 * (tid == 0) will result in the termination of the entire process using
 * exit(0) [after releasing the assigned library memory]. 
 * Return value: The function returns 0 if the thread was successfully
 * terminated and -1 otherwise. If a thread terminates itself or the main
 * thread is terminated, the function does not return.
*/
int uthread_terminate(int tid){
    
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGVTALRM);
    if(sigprocmask(SIG_BLOCK, &set, NULL)) {
        std::cerr << "system error: sigprocmask error." << std::endl;
        exit(1);
    }
    
    if(threads[tid] == NULL) {
        std::cerr << "thread library error: no thread with given id exists"
                << std::endl;
        
        sigprocmask(SIG_UNBLOCK, &set, NULL);
        return -1;
    }
    
    if(tid == 0){
        for(int i = 0; i< MAX_THREAD_NUM; i++){
            if(threads[i] != NULL){
                delete threads[i];
                threads[i] = NULL;
            }
        }
        exit(0);
    }
    
    // If the given thread is in READY state, remove it from the ready list.
    if (threads[tid]->state == READY) {
        for(std::list<int>::iterator it=readyThreads.begin(); it != 
                readyThreads.end(); ++it){
            if((*it) == tid) {
                readyThreads.erase(it);
                break;
            }
        }
    }
    delete threads[tid];
    threads[tid] = NULL;
    
    emptyPlaces.insert(tid);
    
    if(currThreadID != tid){
        sigprocmask(SIG_UNBLOCK, &set, NULL);
        return 0;
    }
    
    scheduler();
    
    sigprocmask(SIG_UNBLOCK, &set, NULL);
    return 0;
}


/*
 * Description: This function blocks the thread with ID tid. The thread may
 * be resumed later using uthread_resume. If no thread with ID tid exists it
 * is considered as an error. In addition, it is an error to try blocking the
 * main thread (tid == 0). If a thread blocks itself, a scheduling decision
 * should be made. Blocking a thread in BLOCKED or SLEEPING states has no
 * effect and is not considered as an error.
 * Return value: On success, return 0. On failure, return -1.
*/
int uthread_block(int tid){
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGVTALRM);
    if(sigprocmask(SIG_BLOCK, &set, NULL)) {
        std::cerr << "system error: sigprocmask error." << std::endl;
        exit(1);
    }
    
    if(threads[tid] == NULL) {
        std::cerr << "thread library error: no thread with given id exists"
                << std::endl;
        sigprocmask(SIG_UNBLOCK, &set, NULL);
        return -1;
    }
    
    if(tid == 0) {
        std::cerr << "thread library error: cannot  block the main thread"
                << std::endl;
        sigprocmask(SIG_UNBLOCK, &set, NULL);
        return -1;
    }
    
    if(threads[tid]->state != BLOCKED && threads[tid]->state != SLEEPING){
        threads[tid]->state = BLOCKED;
        readyThreads.remove(tid);
        if(currThreadID == tid) {
            scheduler();
        }
    }
    
    sigprocmask(SIG_UNBLOCK, &set, NULL);
    return 0;
}


/*
 * Description: This function resumes a blocked thread with ID tid and moves
 * it to the READY state. Resuming a thread in the RUNNING, READY or SLEEPING
 * state has no effect and is not considered as an error. If no thread with
 * ID tid exists it is considered as an error.
 * Return value: On success, return 0. On failure, return -1.
*/
int uthread_resume(int tid) {
    if(threads[tid] == NULL){
        std::cerr << "thread library error: no thread with given id exists"
                << std::endl;
        return -1;
    }
    
    if(threads[tid]->state == BLOCKED){
        threads[tid]->state = READY;
        readyThreads.push_back(tid);
    }
    return 0;
}


/*
 * Description: This function puts the RUNNING thread to sleep for a period
 * of num_quantums (not including the current quantum) after which it is moved
 * to the READY state. num_quantums must be a positive number. It is an error
 * to try to put the main thread (tid==0) to sleep. Immediately after a thread
 * transitions to the SLEEPING state a scheduling decision should be made.
 * Return value: On success, return 0. On failure, return -1.
*/
int uthread_sleep(int num_quantums) {
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGVTALRM);
    if(sigprocmask(SIG_BLOCK, &set, NULL)) {
        std::cerr << "system error: sigprocmask error." << std::endl;
        exit(1);
    }
    
    if (currThreadID == 0) {
        std::cerr << "thread library error: cannot  put the main thread to "
                "sleep" << std::endl;
        sigprocmask(SIG_UNBLOCK, &set, NULL);
        return -1;
    }
    
    if (num_quantums <= 0) {
        std::cerr << "thread library error: num_quantums must be positive "
                << std::endl;
        sigprocmask(SIG_UNBLOCK, &set, NULL);
        return -1;
    }
    
    sleepingThreads[currThreadID] = num_quantums + 1;
    threads[currThreadID]->state = SLEEPING;
   
    scheduler();
    
    sigprocmask(SIG_UNBLOCK, &set, NULL);
    return 0;
}


/*
 * Description: This function returns the number of quantums until the thread
 * with id tid wakes up including the current quantum. If no thread with ID
 * tid exists it is considered as an error. If the thread is not sleeping,
 * the function should return 0.
 * Return value: Number of quantums (including current quantum) until wakeup.
*/
int uthread_get_time_until_wakeup(int tid) {
    if(threads[tid] == NULL) {
        std::cerr << "thread library error: no thread with given id exists"
                << std::endl;
        return -1;
    }
    
    if(threads[tid]->state == SLEEPING) {
        return sleepingThreads.at(tid);
    }
    
    return 0;
}


/*
 * Description: This function returns the thread ID of the calling thread.
 * Return value: The ID of the calling thread.
*/
int uthread_get_tid() {
    return currThreadID;
}


/*
 * Description: This function returns the total number of quantums that were
 * started since the library was initialized, including the current quantum.
 * Right after the call to uthread_init, the value should be 1.
 * Each time a new quantum starts, regardless of the reason, this number
 * should be increased by 1.
 * Return value: The total number of quantums.
*/
int uthread_get_total_quantums(){
    return numOfQuantum; 
}


/*
 * Description: This function returns the number of quantums the thread with
 * ID tid was in RUNNING state. On the first time a thread runs, the function
 * should return 1. Every additional quantum that the thread starts should
 * increase this value by 1 (so if the thread with ID tid is in RUNNING state
 * when this function is called, include also the current quantum). If no
 * thread with ID tid exists it is considered as an error.
 * Return value: On success, return the number of quantums of the thread with ID 
 * tid. On failure, return -1.
*/
int uthread_get_quantums(int tid){  
    if(threads[tid] == NULL) {
        std::cerr << "thread library error: no thread with given id exists"
                << std::endl;
        return -1;
    }
    
    return threads[tid]->quantumCounter;
}