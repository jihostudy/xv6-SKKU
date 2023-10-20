#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

// vruntime maximum limit (int 기준)
//uint VRUNTIME_MAX = 2147483647;

uint weight[40] = 
{
  /*  0 ~ 4  */ 88761,   71755,   56483,   46273,   36291,
  /*  5 ~ 9  */ 29154,   23254,   18705,   14949,   11916,  
  /* 10 ~ 14 */  9548,    7620,    6100,    4904,    3906,
  /* 15 ~ 19 */  3121,    2501,    1991,    1586,    1277,
  /* 20 ~ 24 */  1024,     820,     655,     526,     423,
  /* 25 ~ 29 */   335,     272,     215,     172,     137,
  /* 30 ~ 34 */   110,      87,      70,      56,      45,
  /* 35 ~ 39 */    36,      29,      23,      18,      15
};

uint total_weight;

static struct proc *initproc;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);
// ticks 사용가능하게끔 설정
extern uint ticks;

static void wakeup1(void *chan);

int get_total_weight() 
{
  struct proc* p;
  int total_weight = 0;
  //cprintf("weight합구하는중\n");
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
    if(p->state == RUNNABLE) {
      total_weight += weight[p->nice];
    }
  }
  return total_weight;
}

void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
}

// Must be called with interrupts disabled
int
cpuid() {
  return mycpu()-cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu*
mycpu(void)
{
  int apicid, i;
  
  if(readeflags()&FL_IF)
    panic("mycpu called with interrupts enabled\n");
  
  apicid = lapicid();
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  for (i = 0; i < ncpu; ++i) {
    if (cpus[i].apicid == apicid)
      return &cpus[i];
  }
  panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc*
myproc(void) {
  struct cpu *c;
  struct proc *p;
  pushcli();
  c = mycpu();
  p = c->proc;
  popcli();
  return p;
}

//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED)
      goto found;

  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;
  p->pid = nextpid++;
  
  p->nice = 20;
  p->weight = weight[p->nice];
  p->runtime = 0;
  p->sum_runtime = 0;
  p->vruntime = 0;
  p->vruntime_overflow = 0;
  p->timeslice = 0;
  
  release(&ptable.lock);

  // Allocate kernel stack.
  if((p->kstack = kalloc()) == 0){
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;

  return p;
}

//PAGEBREAK: 32
// Set up first user process.
void
userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();
  
  initproc = p;
  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0;  // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  acquire(&ptable.lock);

  p->state = RUNNABLE;

  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();

  sz = curproc->sz;
  if(n > 0){
    if((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  } else if(n < 0){
    if((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  curproc->sz = sz;
  switchuvm(curproc);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy process state from proc.
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  np->sz = curproc->sz;
  np->parent = curproc;
  // 부모의 nice, vruntime(overflow 포함) 상속, runtime = 0
  np->nice = curproc->nice;
  np->vruntime_overflow = curproc->vruntime_overflow;
  np->vruntime = curproc->vruntime;
  np->sum_runtime = 0;
  np->runtime = 0;
  *np->tf = *curproc->tf;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;

  acquire(&ptable.lock);

  np->state = RUNNABLE;

  release(&ptable.lock);

  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
exit(void)
{
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;

  if(curproc == initproc)
    panic("init exiting");

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd]){
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);

  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == curproc){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // exit되기 전에 값 저장하기
  // curproc->sum_runtime += curproc->runtime;
  // curproc->runtime = 0;
  // total_weight = 0;

  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE;
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(void)
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();
  
  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != curproc)
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){
        // Found one.
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}

//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
/*
void
scheduler(void)
{    
  struct proc *p;
  struct cpu *c = mycpu();
  // Minimum vruntime value process ㅋㅋ  
  c->proc = 0;
  for(;;)
  {
    // Enable interrupts on this processor.
    sti();
    total_weight = 0;
    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->state != RUNNABLE)
        continue;

      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
      c->proc = p;
      switchuvm(p);struct proc *MVP;
      p->state = RUNNING;

      swtch(&(c->scheduler), p->context);
      switchkvm();

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0;
    }
    release(&ptable.lock);

  }
}
*/
// acquire은 함수를 부르기 전에 해서 따로 해줄 필요없다.
// 2번 하면 Error
struct proc* minimum_vruntime_process(void) {
  struct proc* p;
  struct proc* MVP = 0;  
  
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
    if(p->state != RUNNABLE)    
      continue;
    if(!MVP || (p->vruntime_overflow <= MVP->vruntime_overflow && p->vruntime < MVP->vruntime)){
      MVP = p;
    }
  }
  //cprintf("Found minimum Vruntime Process : %s!\n",MVP->name);
  return MVP;
}


void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();  
  //struct proc *MVP;
  c->proc = 0;
  
  for(;;){
    // Enable interrupts on this processor.
    sti();
    //MVP = 0;
    total_weight = get_total_weight();

    // Loop over process table looking for process to run.    
    acquire(&ptable.lock);        
    // for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    //   if(p->state != RUNNABLE)
    //     continue;
    //   if(!MVP || p->vruntime < MVP->vruntime)
    //     MVP = p;
    // }
    //p = MVP;
    p = minimum_vruntime_process();
    if(p) {      
      if(total_weight) {
        p->timeslice = 10000 * p->weight / total_weight;
      }

      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
      c->proc = p;
      switchuvm(p);
      p->state = RUNNING;      
      
      swtch(&(c->scheduler), p->context);
      switchkvm();

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0;
    }
    release(&ptable.lock);  
  }
}

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(mycpu()->ncli != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;
  swtch(&p->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock

  //myproc()->sum_runtime += myproc()->runtime;
  myproc()->runtime = 0;
  total_weight = 0;

  myproc()->state = RUNNABLE;
  sched();
  release(&ptable.lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void
forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);

  if (first) {
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0;
    iinit(ROOTDEV);
    initlog(ROOTDEV);
  }

  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  if(p == 0)
    panic("sleep");

  if(lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    release(lk);
  }
  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// struct proc *MVP;The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;
  
  struct proc *MVP = 0; // minimum vruntime process
  int numRunnable = 0;  // false (# Runnable = 0)
  int vruntime_1tick;
  // Runnable 존재?
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
    if(p->state == RUNNABLE) {
      numRunnable = 1; // true (존재)
      break;
    }        
  }
  // 존재한다면, vruntime 최소 찾아
  if(numRunnable) {
    MVP = minimum_vruntime_process();
    // for(p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
    //   if(p->state != RUNNABLE)
    //     continue;
    //   if(!MVP || p->vruntime < MVP->vruntime)
    //     MVP = p;
    // }
  }
  
  // Update vruntime depending on number of Runnable process and MVP
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == SLEEPING && p->chan == chan){
      p->state = RUNNABLE;
      // no Runnable
      if(!numRunnable) {
        p->vruntime_overflow = 0;
        p->vruntime = 0;
      }
      // Runnable Exists
      else {
        vruntime_1tick = 1000 * 1024 / p->weight;
        p->vruntime = MVP->vruntime - vruntime_1tick;
        // if(MVP->vruntime < vruntime_1tick){
        //   // 나중에 처리필요
        //   if(MVP->vruntime_overflow == 0) {
        //     p->vruntime = 0;
        //     p->vruntime_overflow = 0;
        //   }
        //   // else {
        //   //   uint temp = vruntime_1tick - MVP->vruntime;
        //   //   p->vruntime = VRUNTIME_MAX - temp;
        //   //   p->vruntime_overflow -= 1;
        //   // }
        // }        
      }      
    }    
  }
}

// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int
kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->killed = 1;
      // Wake process from sleep if necessary.
      if(p->state == SLEEPING)
        p->state = RUNNABLE;
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %s %s", p->pid, state, p->name);
    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}

//getnice systemcall
int getnice(int pid)
{
  struct proc *p;
  int niceValue;

  if(pid < 1) return -1;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      niceValue = p->nice;
      release(&ptable.lock);
      return niceValue;
    }
  }
  release(&ptable.lock);
  return -1;
}

//setnice systemcall
int setnice(int pid, int value)
{
  struct proc *p;

  if(pid < 1) return -1;
  if(value < 0 || value > 39) return -1;
  
  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->nice = value;  
      p->weight = weight[value];    
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}
// formatting functions
void format_string(char* input, int output_len) {
  char output[30];
  int len = 0;
  int i = 0;
  int j;
  while(input[i] != '\0'){
    output[i] = input[i];
    len++;
    i++;
  }
  for(j = len; j < output_len; j++){
    output[j] = ' ';
  }
  output[j] = '\0';
  cprintf("%s",output);
} 
// formatting functions
void format_int(int input, int output_len) {
  int copy = input;
  int len = 0;  
  int i;
  while(copy > 0) {
    len++;
    copy /= 10;
  }
  if(input == 0) len = 1;
  cprintf("%d", input);
  for(i = len; i < output_len; i++) {
    cprintf(" ");
  }
}
//ps systemcall
// PA2
void ps(int pid)
{
    static char *states[] = {
        "UNUSED", //Do not print
        "EMBRYO",
        "SLEEPING",
        "RUNNABLE",
        "RUNNING",
        "ZOMBIE"
    };

    static char *thead[] = {
      /* 0 */ "name", 
      /* 1 */ "pid",
      /* 2 */ "state",
      /* 3 */ "priority",
      /* 4 */ "runtime/weight",
      /* 5 */ "runtime",
      /* 6 */ "vruntime",
      /* 7 */ "tick"      
    };  

    struct proc *p;
    int cnt = 0;

    acquire(&ptable.lock);
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
        if((p->pid == pid) || (pid == 0)) {
            //표 제목 (처음에만 출력)
            if(cnt == 0){
              format_string(thead[0],10);
              format_string(thead[1],10);
              format_string(thead[2],15);
              format_string(thead[3],15);
              format_string(thead[4],20);
              format_string(thead[5],15);
              format_string(thead[6],20);
              // cprintf("tick %d\n",ticks*1000);

              //임시코드
              format_string("timeslice",25);
              
              cprintf("tick %d\n",ticks*1000);
              cnt++;              
            }
            if(p->state != 0){
              format_string(p->name,10);
              format_int(p->pid,10);
              format_string(states[p->state],15);
              format_int(p->nice,15);
              format_int(p->sum_runtime / p->weight,20);
              format_int(p->sum_runtime,15);
              format_int(p->vruntime,20);
              format_int(p->timeslice,20);
              cprintf("\n");              
            }
        }    
    }
    release(&ptable.lock);
}

