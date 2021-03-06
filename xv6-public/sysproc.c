#include "types.h"
#include "x86.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"

int
sys_fork(void)
{
  return fork();
}

char*
sys_getshmem(void)
{
  int pid;
  if(argint(0,&pid)<0) return (char*)(-1);
  return getshmem(pid); 
}

int
sys_proc_list(void)
{
  proc_list();
  return 0;
}

int
sys_setmemorylimit(void)
{
  int pid, limit;
  if(argint(0,&pid)<0||argint(1,&limit)<0)
    return -1;

  //cprintf("pid=%d limit=%d", pid, limit);
  return setmemorylimit(pid, limit);
}

int 
sys_getadmin(void)
{
  char *password;
  if(argstr(0, &password)<0)
    return -1;
  return getadmin(password);
}

int
sys_yield(void)
{
	myproc()->level=0;
  myproc()->time_q=4;
  yield();
	return 0;
}

int
sys_getlev(void)
{
	return getlev();
} 

int
sys_setpriority(void)
{
	int pid,pr;
	if(argint(0, &pid)<0) 
		return -1;
	if(argint(1, &pr)<0) 
		return -1;

	return setpriority(pid, pr);
}

int
sys_exit(void)
{
  exit();
  return 0;  // not reached
}

int
sys_wait(void)
{
  return wait();
}

int
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

int
sys_getpid(void)
{
  return myproc()->pid;
}

int
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

int
sys_sleep(void)
{
  myproc()->level=0;
  myproc()->time_q=4;

  int n;
  uint ticks0;

  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

// return how many clock tick interrupts have occurred
// since start.
int
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}
